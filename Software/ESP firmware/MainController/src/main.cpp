#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <string.h>
#include <math.h>
#include "SensorAndPoint.h"

// Minimal base receiver:
// - ESP-NOW only
// - Receives sensor packets
// - Converts latest packet to world-frame XYZ points in loop()
// - Streams compact point CSV over Serial for a PC viewer

const uint8_t ESPNOW_CHANNEL = 1;
const uint32_t SERIAL_BAUD = 115200;

const int NUM_SENSORS = 8;
const uint8_t SENSOR_IDS[NUM_SENSORS] = {1, 2, 3, 4, 5, 6, 7, 8};

struct SensorConfig
{
  uint8_t sensorId;
  float x_off_mm;
  float y_off_mm;
  float z_off_mm;
  float alpha_deg;
  float beta_deg;
  float gamma_deg;
};

SensorConfig SENSOR_CONFIGS[NUM_SENSORS] =
    {
        // ID, x_mm,  y_mm,   z_mm,    alpha,  beta,  gamma
        {1, 165.1f, 295.275f, 501.65f, 0.0f, 0.0f, 30.0f},
        {2, 177.8f, 257.175f, 501.65f, 0.0f, 0.0f, 0.0f},
        {3, 177.8f, -257.175f, 501.65f, 180.0f, 0.0f, 0.0f},
        {4, 165.1f, -295.275f, 501.65f, 180.0f, 0.0f, -30.0f},
        {5, -444.5f, -63.5f, 1282.7f, 0.0f, 10.0f, -90.0f},
        {6, -508.0f, -12.7f, 1282.7f, 0.0f, 10.0f, -150.0f},
        {7, -508.0f, 25.4f, 1282.7f, 180.0f, 10.0f, 150.0f},
        {8, -444.5f, 63.5f, 1282.7f, 180.0f, 10.0f, 90.0f},
};
struct SensorPacket
{
  uint8_t sensor_id;
  uint32_t timestamp_ms;
  uint16_t distance_mm[16];
  uint8_t target_status[16];
  uint8_t nb_targets[16];
} __attribute__((packed));

// ── Beta: inter-ESP32 packets ─────────────────────────────────────────────────

const uint8_t MSG_CONFIG           = 0xB1; // CaregiverApp -> MainController (+ LEDRingController)
const uint8_t MSG_ZONE_PROXIMITY   = 0xB3; // MainController -> LEDRingController (broadcast)
const uint8_t MSG_COMPONENT_STATUS = 0xB4; // MainController -> CaregiverApp (broadcast)

const uint8_t COMPONENT_MAIN_CONTROLLER = 1;

// Sent by CaregiverApp when the caregiver changes settings in the browser UI.
struct ConfigPacket
{
  uint8_t  msg_type;           // MSG_CONFIG
  uint8_t  zone_mode;          // 4, 6, or 8
  uint8_t  brightness;         // 0–64
  uint16_t red_threshold_mm;
  uint16_t yellow_threshold_mm;
  uint8_t  audio_enabled;
  uint8_t  visual_enabled;
  uint8_t  active_sectors;     // bitmask — bit N = zone N enabled
} __attribute__((packed));     // 10 bytes

// Broadcast by MainController every ~67 ms with per-zone closest obstacle distance.
// LEDRingController receives this and renders the ring locally.
struct ZoneProximityPacket
{
  uint8_t msg_type;     // MSG_ZONE_PROXIMITY
  uint8_t num_zones;    // matches currentNumZones (4, 6, or 8)
  float   closest_mm[8]; // index = zone index; 1e9 means no obstacle in range
} __attribute__((packed)); // 34 bytes

struct ComponentStatusPacket
{
  uint8_t msg_type;          // MSG_COMPONENT_STATUS
  uint8_t component_id;      // COMPONENT_MAIN_CONTROLLER
  uint8_t sensor_seen_mask;  // bit N = sensor (N+1) is currently alive
  uint8_t flags;             // bit0 = proximityVisualEnabled
} __attribute__((packed));   // 4 bytes

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

Sensor sensors[NUM_SENSORS];
uint8_t sensorSeen[NUM_SENSORS] = {0};
uint32_t packetCountBySensor[NUM_SENSORS] = {0};
uint32_t lastSensorTimestampMs[NUM_SENSORS] = {0};
volatile uint8_t sensorPendingProcess[NUM_SENSORS] = {0};
uint32_t lastRxMsBySensor[NUM_SENSORS] = {0};
float rxHzBySensor[NUM_SENSORS] = {0.0f};
uint32_t convertUsLastBySensor[NUM_SENSORS] = {0};
float convertUsAvgBySensor[NUM_SENSORS] = {0.0f};
uint32_t lastStatusEmitMs = 0;
const uint32_t STATUS_EMIT_PERIOD_MS = 500;
const uint32_t SENSOR_STALE_TIMEOUT_MS = 400;

// ── Beta: proximity detection state ──────────────────────────────────────────
int     proximityNumZones       = 6;
int     proximityBrightness     = 40;
bool    proximityVisualEnabled  = true;
uint8_t proximityActiveSectors  = 0xFF; // all zones on by default
// Ring distance thresholds (mm) — updated by ConfigPacket
float proximityDistRings[5] = {300.0f, 600.0f, 1050.0f, 1500.0f, 1950.0f};
const uint8_t USE_OCCUPANCY_GRID = 1;

// Occupancy grid parameters (wheelchair/body frame, mm):
// - X axis: forward, sampled over [OCC_X_MIN_MM, OCC_X_MAX_MM)
// - Y axis: left/right, sampled over [OCC_Y_MIN_MM, OCC_Y_MAX_MM)
// - Cell size: OCC_GRID_RES_MM square cells
// Example default envelope: 3.0 m forward by 3.0 m wide at 0.1 m resolution => 30x30 grid.
const int OCC_GRID_RES_MM = 100;
const int OCC_X_MIN_MM = 0;
const int OCC_X_MAX_MM = 3000;
const int OCC_Y_MIN_MM = -1500;
const int OCC_Y_MAX_MM = 1500;
const int OCC_GRID_COLS = (OCC_X_MAX_MM - OCC_X_MIN_MM) / OCC_GRID_RES_MM; // 30
const int OCC_GRID_ROWS = (OCC_Y_MAX_MM - OCC_Y_MIN_MM) / OCC_GRID_RES_MM; // 30
const int OCC_GRID_CELL_COUNT = OCC_GRID_COLS * OCC_GRID_ROWS; // 900

// Temporal occupancy filter parameters:
// - occConfidence[] stores per-cell confidence in [0,255]
// - OCC_CONF_RISE increments confidence when a cell is hit in this frame
// - OCC_CONF_DECAY decrements confidence when a cell is not hit
// - OCC_ENTER_THRESHOLD / OCC_EXIT_THRESHOLD provide hysteresis for stable occupied/free state
const uint8_t OCC_CONF_RISE = 90;
const uint8_t OCC_CONF_DECAY = 24;
const uint8_t OCC_ENTER_THRESHOLD = 120;
const uint8_t OCC_EXIT_THRESHOLD = 80;

uint8_t occConfidence[OCC_GRID_CELL_COUNT] = {0};
uint8_t occOccupied[OCC_GRID_CELL_COUNT] = {0};

uint32_t lastProximityBroadcastMs = 0;
const uint32_t PROXIMITY_PERIOD_MS = 67; // ~15 Hz
uint8_t  broadcastPeerAdded = 0;

// Forward declarations for proximity helpers (defined before setup())
void applyProximityThresholds(float redMaxMm, float yellowMaxMm);
void broadcastZoneProximity();
void invalidateStaleSensors();
void clearOccupancyGrid();
void accumulateGridHits(uint8_t hitMask[OCC_GRID_CELL_COUNT]);
void updateOccupancyGrid(const uint8_t hitMask[OCC_GRID_CELL_COUNT]);
void computeZoneProximityFromGrid(float closestMmByZone[8]);
void computeZoneProximityDirect(float closestMmByZone[8]);
void broadcastComponentStatus();

int findSensorIndex(uint8_t id)
{
  for (int i = 0; i < NUM_SENSORS; i++)
  {
    if (SENSOR_IDS[i] == id)
      return i;
  }
  return -1;
}

void configureSensors()
{
  for (int i = 0; i < NUM_SENSORS; i++)
  {
    sensors[i].configureSensor(
        SENSOR_CONFIGS[i].sensorId,
        SENSOR_CONFIGS[i].x_off_mm,
        SENSOR_CONFIGS[i].y_off_mm,
        SENSOR_CONFIGS[i].z_off_mm,
        degreesToRadians(SENSOR_CONFIGS[i].alpha_deg),
        degreesToRadians(SENSOR_CONFIGS[i].beta_deg),
        degreesToRadians(SENSOR_CONFIGS[i].gamma_deg));
  }
}

void convertPacketToPoints(int sensorIndex)
{
  // Measure only XYZ conversion time for one sensor packet (16 cells).
  uint32_t t0 = micros();
  sensors[sensorIndex].computeLatestWorldPoints();
  uint32_t dtUs = micros() - t0;
  convertUsLastBySensor[sensorIndex] = dtUs;

  // Exponential moving average for steadier reporting in the status stream.
  if (convertUsAvgBySensor[sensorIndex] <= 0.0f)
  {
    convertUsAvgBySensor[sensorIndex] = (float)dtUs;
  }
  else
  {
    convertUsAvgBySensor[sensorIndex] =
        (0.85f * convertUsAvgBySensor[sensorIndex]) + (0.15f * (float)dtUs);
  }
}

void emitPointsForSensor(int sensorIndex)
{
  uint8_t sid = SENSOR_IDS[sensorIndex];
  uint32_t ts = lastSensorTimestampMs[sensorIndex];

  for (int cell = 0; cell < 16; cell++)
  {
    const Point &p = sensors[sensorIndex].latestWorldPoints[cell];
    // P,sid,cell,valid,x_m,y_m,z_m,targetStatus,ts_ms
    Serial.print("P,");
    Serial.print(sid);
    Serial.print(",");
    Serial.print(cell);
    Serial.print(",");
    Serial.print(p.isValid ? 1 : 0);
    Serial.print(",");
    Serial.print(p.worldX / 1000.0f, 4);
    Serial.print(",");
    Serial.print(p.worldY / 1000.0f, 4);
    Serial.print(",");
    Serial.print(p.worldZ / 1000.0f, 4);
    Serial.print(",");
    Serial.print((unsigned int)p.targetStatus);
    Serial.print(",");
    Serial.println(ts);
  }

  // End-of-sensor-frame marker: E,sid,ts_ms
  Serial.print("E,");
  Serial.print(sid);
  Serial.print(",");
  Serial.println(ts);
}

void processPendingSensors()
{
  for (int i = 0; i < NUM_SENSORS; i++)
  {
    if (!sensorPendingProcess[i])
    {
      continue;
    }

    sensorPendingProcess[i] = 0;
    convertPacketToPoints(i);
    emitPointsForSensor(i);
  }
}

void emitReceiverStatusIfDue()
{
  uint32_t nowMs = millis();
  if ((nowMs - lastStatusEmitMs) < STATUS_EMIT_PERIOD_MS)
  {
    return;
  }
  lastStatusEmitMs = nowMs;

  for (int i = 0; i < NUM_SENSORS; i++)
  {
    // S,sid,pkts,rx_hz,conv_us_last,conv_us_avg
    Serial.print("S,");
    Serial.print(SENSOR_IDS[i]);
    Serial.print(",");
    Serial.print(packetCountBySensor[i]);
    Serial.print(",");
    Serial.print(rxHzBySensor[i], 2);
    Serial.print(",");
    Serial.print(convertUsLastBySensor[i]);
    Serial.print(",");
    Serial.println(convertUsAvgBySensor[i], 1);
  }

  if (broadcastPeerAdded)
  {
    broadcastComponentStatus();
  }
}

void broadcastComponentStatus()
{
  ComponentStatusPacket pkt = {};
  pkt.msg_type = MSG_COMPONENT_STATUS;
  pkt.component_id = COMPONENT_MAIN_CONTROLLER;
  pkt.flags = proximityVisualEnabled ? 0x01 : 0x00;

  uint8_t seenMask = 0;
  for (int i = 0; i < NUM_SENSORS; i++)
  {
    if (sensorSeen[i]) seenMask |= (uint8_t)(1 << i);
  }
  pkt.sensor_seen_mask = seenMask;

  esp_now_send(BROADCAST_MAC, (uint8_t *)&pkt, sizeof(pkt));
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
  (void)mac;

  // ConfigPacket from CaregiverApp — update local proximity settings
  if (len == (int)sizeof(ConfigPacket))
  {
    ConfigPacket cfg;
    memcpy(&cfg, incomingData, sizeof(cfg));
    if (cfg.msg_type == MSG_CONFIG)
    {
      if (cfg.zone_mode == 4 || cfg.zone_mode == 6 || cfg.zone_mode == 8)
        proximityNumZones = cfg.zone_mode;
      proximityBrightness    = cfg.brightness;
      proximityVisualEnabled = (cfg.visual_enabled != 0);
      proximityActiveSectors = cfg.active_sectors;
      applyProximityThresholds(cfg.red_threshold_mm, cfg.yellow_threshold_mm);
      if (!proximityVisualEnabled)
      {
        clearOccupancyGrid();
      }
      Serial.printf("[Config] zones=%d bright=%d visual=%s sectors=0x%02X\n",
                    proximityNumZones, proximityBrightness,
                    proximityVisualEnabled ? "on" : "off", proximityActiveSectors);
      return;
    }
  }

  if (len != (int)sizeof(SensorPacket))
  {
    return;
  }

  SensorPacket pkt;
  memcpy(&pkt, incomingData, sizeof(pkt));

  uint8_t sid = pkt.sensor_id;
  int idx = findSensorIndex(sid);
  if (idx < 0)
    return;

  sensors[idx].setAllCellData(
      pkt.distance_mm,
      pkt.target_status,
      pkt.nb_targets);
  sensorSeen[idx] = 1;
  packetCountBySensor[idx]++;
  lastSensorTimestampMs[idx] = pkt.timestamp_ms;
  uint32_t nowMs = millis();
  if (lastRxMsBySensor[idx] != 0)
  {
    uint32_t dt = nowMs - lastRxMsBySensor[idx];
    if (dt > 0)
    {
      rxHzBySensor[idx] = 1000.0f / (float)dt;
    }
  }
  lastRxMsBySensor[idx] = nowMs;
  sensorPendingProcess[idx] = 1;
}

// ── Beta: proximity helpers ───────────────────────────────────────────────────

void invalidateStaleSensors()
{
  uint32_t nowMs = millis();
  for (int i = 0; i < NUM_SENSORS; i++)
  {
    if (!sensorSeen[i]) continue;
    uint32_t ageMs = nowMs - lastRxMsBySensor[i];
    if (ageMs <= SENSOR_STALE_TIMEOUT_MS) continue;

    sensorSeen[i] = 0;
    sensorPendingProcess[i] = 0;
    rxHzBySensor[i] = 0.0f;
    Serial.printf("[Sensor] sid=%d marked stale after %lums\n", SENSOR_IDS[i], ageMs);
  }
}

static int occupancyCellIndexFromWorld(float worldXmm, float worldYmm)
{
  if (worldXmm < OCC_X_MIN_MM || worldXmm >= OCC_X_MAX_MM) return -1;
  if (worldYmm < OCC_Y_MIN_MM || worldYmm >= OCC_Y_MAX_MM) return -1;

  int col = (int)((worldXmm - OCC_X_MIN_MM) / OCC_GRID_RES_MM);
  int row = (int)((worldYmm - OCC_Y_MIN_MM) / OCC_GRID_RES_MM);
  if (col < 0 || col >= OCC_GRID_COLS || row < 0 || row >= OCC_GRID_ROWS) return -1;
  return row * OCC_GRID_COLS + col;
}

void clearOccupancyGrid()
{
  for (int i = 0; i < OCC_GRID_CELL_COUNT; i++)
  {
    occConfidence[i] = 0;
    occOccupied[i] = 0;
  }
}

void accumulateGridHits(uint8_t hitMask[OCC_GRID_CELL_COUNT])
{
  for (int i = 0; i < OCC_GRID_CELL_COUNT; i++) hitMask[i] = 0;

  for (int s = 0; s < NUM_SENSORS; s++)
  {
    if (!sensorSeen[s]) continue;
    for (int cell = 0; cell < 16; cell++)
    {
      const Point &p = sensors[s].latestWorldPoints[cell];
      if (!p.isValid) continue;

      float dist = sqrtf(p.worldX * p.worldX + p.worldY * p.worldY);
      if (dist > 3000.0f) continue;

      int idx = occupancyCellIndexFromWorld(p.worldX, p.worldY);
      if (idx >= 0) hitMask[idx] = 1;
    }
  }
}

void updateOccupancyGrid(const uint8_t hitMask[OCC_GRID_CELL_COUNT])
{
  for (int i = 0; i < OCC_GRID_CELL_COUNT; i++)
  {
    uint8_t conf = occConfidence[i];
    if (hitMask[i])
    {
      uint16_t boosted = (uint16_t)conf + OCC_CONF_RISE;
      conf = (boosted > 255U) ? 255U : (uint8_t)boosted;
    }
    else
    {
      conf = (conf > OCC_CONF_DECAY) ? (uint8_t)(conf - OCC_CONF_DECAY) : 0;
    }
    occConfidence[i] = conf;

    if (!occOccupied[i] && conf >= OCC_ENTER_THRESHOLD) occOccupied[i] = 1;
    else if (occOccupied[i] && conf <= OCC_EXIT_THRESHOLD) occOccupied[i] = 0;
  }
}

void applyProximityThresholds(float redMaxMm, float yellowMaxMm)
{
  if (redMaxMm <= 0 || yellowMaxMm <= redMaxMm) return;
  proximityDistRings[0] = redMaxMm * 0.5f;
  proximityDistRings[1] = redMaxMm;
  proximityDistRings[2] = redMaxMm + (yellowMaxMm - redMaxMm) * 0.5f;
  proximityDistRings[3] = yellowMaxMm;
  proximityDistRings[4] = yellowMaxMm + (yellowMaxMm - redMaxMm) * 0.5f;
}

// Returns 0-based zone index matching the CaregiverApp UI sector convention.
static int proximityZoneIndex(float angleDeg, int numZones)
{
  if (numZones == 4)
  {
    if (angleDeg >= -45  && angleDeg <  45)  return 0;
    if (angleDeg >=  45  && angleDeg < 135)  return 1;
    if (angleDeg < -135  || angleDeg >= 135) return 2;
    return 3;
  }
  if (numZones == 8)
  {
    if (angleDeg >= -22.5  && angleDeg <  22.5)  return 0;
    if (angleDeg >=  22.5  && angleDeg <  67.5)  return 1;
    if (angleDeg >=  67.5  && angleDeg < 112.5)  return 2;
    if (angleDeg >= 112.5  && angleDeg < 157.5)  return 3;
    if (angleDeg < -157.5  || angleDeg >= 157.5) return 4;
    if (angleDeg >= -157.5 && angleDeg < -112.5) return 5;
    if (angleDeg >= -112.5 && angleDeg <  -67.5) return 6;
    return 7;
  }
  // 6 zones (default)
  if (angleDeg >= -30  && angleDeg <  30)  return 0;
  if (angleDeg >=  30  && angleDeg <  90)  return 1;
  if (angleDeg >=  90  && angleDeg < 150)  return 2;
  if (angleDeg < -150  || angleDeg >= 150) return 3;
  if (angleDeg >= -150 && angleDeg < -90)  return 4;
  return 5;
}

void computeZoneProximityFromGrid(float closestMmByZone[8])
{
  for (int z = 0; z < 8; z++) closestMmByZone[z] = 1e9f;

  for (int idx = 0; idx < OCC_GRID_CELL_COUNT; idx++)
  {
    if (!occOccupied[idx]) continue;

    int row = idx / OCC_GRID_COLS;
    int col = idx % OCC_GRID_COLS;

    float x = OCC_X_MIN_MM + ((float)col + 0.5f) * (float)OCC_GRID_RES_MM;
    float y = OCC_Y_MIN_MM + ((float)row + 0.5f) * (float)OCC_GRID_RES_MM;
    float dist = sqrtf(x * x + y * y);
    if (dist > 3000.0f) continue;

    float angleDeg = atan2f(-y, x) * (180.0f / 3.14159265f);
    int zone = proximityZoneIndex(angleDeg, proximityNumZones);
    if (zone < 0 || zone >= proximityNumZones) continue;
    if (!(proximityActiveSectors & (1 << zone))) continue;

    if (dist < closestMmByZone[zone]) closestMmByZone[zone] = dist;
  }
}

void computeZoneProximityDirect(float closestMmByZone[8])
{
  for (int z = 0; z < 8; z++) closestMmByZone[z] = 1e9f;

  for (int s = 0; s < NUM_SENSORS; s++)
  {
    if (!sensorSeen[s]) continue;
    for (int cell = 0; cell < 16; cell++)
    {
      const Point &p = sensors[s].latestWorldPoints[cell];
      if (!p.isValid) continue;

      float dist = sqrtf(p.worldX * p.worldX + p.worldY * p.worldY);
      if (dist > 3000.0f) continue;

      float angleDeg = atan2f(-p.worldY, p.worldX) * (180.0f / 3.14159265f);
      int zone = proximityZoneIndex(angleDeg, proximityNumZones);
      if (zone < 0 || zone >= proximityNumZones) continue;
      if (!(proximityActiveSectors & (1 << zone))) continue;

      if (dist < closestMmByZone[zone]) closestMmByZone[zone] = dist;
    }
  }
}

void broadcastZoneProximity()
{
  ZoneProximityPacket pkt;
  pkt.msg_type  = MSG_ZONE_PROXIMITY;
  pkt.num_zones = (uint8_t)proximityNumZones;

  float closestMmByZone[8];
  if (USE_OCCUPANCY_GRID)
  {
    uint8_t hitMask[OCC_GRID_CELL_COUNT];
    accumulateGridHits(hitMask);
    updateOccupancyGrid(hitMask);
    computeZoneProximityFromGrid(closestMmByZone);
  }
  else
  {
    computeZoneProximityDirect(closestMmByZone);
  }

  for (int z = 0; z < 8; z++)
  {
    bool zoneEnabled = (z < proximityNumZones) && ((proximityActiveSectors & (1 << z)) != 0);
    pkt.closest_mm[z] = zoneEnabled ? closestMmByZone[z] : 1e9f;
  }

  esp_now_send(BROADCAST_MAC, (uint8_t *)&pkt, sizeof(pkt));
}

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println("Base receiver minimal ESP-NOW point converter");

  configureSensors();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  Serial.printf("[ESP-NOW] MainController MAC address: %s\n", WiFi.macAddress().c_str());
  Serial.printf("[ESP-NOW] Listening on channel %d\n", ESPNOW_CHANNEL);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  // Broadcast peer -- used to transmit ZoneProximityPackets to LEDRingController
  {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.ifidx  = WIFI_IF_STA;
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) == ESP_OK)
    {
      broadcastPeerAdded = 1;
      Serial.println("Broadcast peer registered for ZoneProximityPackets");
    }
  }

  Serial.println("ESP-NOW receiver ready");
}

void loop()
{
  invalidateStaleSensors();
  processPendingSensors();
  emitReceiverStatusIfDue();

  // Broadcast per-zone closest obstacle distances to LEDRingController at ~15 Hz
  if (proximityVisualEnabled && broadcastPeerAdded)
  {
    uint32_t nowMs = millis();
    if ((nowMs - lastProximityBroadcastMs) >= PROXIMITY_PERIOD_MS)
    {
      lastProximityBroadcastMs = nowMs;
      broadcastZoneProximity();
    }
  }

  delay(1);
}
