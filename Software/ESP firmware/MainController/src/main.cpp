#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <string.h>
#include <math.h>
#include "SensorAndPoint.h"
#include "runtime_defaults.h"

// Minimal base receiver:
// - ESP-NOW only
// - Receives sensor packets
// - Converts latest packet to world-frame XYZ points in loop()
// - Streams compact point CSV over Serial for a PC viewer

const uint8_t ESPNOW_CHANNEL = RuntimeDefaults::kEspNowChannel;
const uint32_t SERIAL_BAUD = RuntimeDefaults::kSerialBaud;

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

const uint8_t MSG_CONFIG           = 0xB1; // CaregiverApp -> MainController
const uint8_t MSG_COMPONENT_STATUS = 0xB4; // MainController -> CaregiverApp (broadcast)
const uint8_t MSG_DETECTION_MODE   = 0xB5; // CaregiverApp -> MainController
const uint8_t MSG_LED_RENDER_FRAME = 0xB6; // MainController -> LEDRingController (broadcast)

const uint8_t COMPONENT_MAIN_CONTROLLER = 1;
const uint8_t DETECTION_MODE_POLAR_GRID = 2;

const uint8_t LED_COLOR_OFF = 0;
const uint8_t LED_COLOR_RED = 1;
const uint8_t LED_COLOR_ORANGE = 2;
const uint8_t LED_COLOR_YELLOW = 3;

const uint8_t LED_RENDER_MODE_SECTOR_FILL = 0;
const uint8_t LED_RENDER_MODE_RADAR = 1;

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
  uint8_t  render_mode;
  uint8_t  active_sectors;     // bitmask — bit N = zone N enabled
} __attribute__((packed));     // 11 bytes

struct ComponentStatusPacket
{
  uint8_t msg_type;          // MSG_COMPONENT_STATUS
  uint8_t component_id;      // COMPONENT_MAIN_CONTROLLER
  uint8_t sensor_seen_mask;  // bit N = sensor (N+1) is currently alive
  uint8_t flags;             // bit0=visualEnabled, bit1..2=detection mode
} __attribute__((packed));   // 4 bytes

struct DetectionModePacket
{
  uint8_t msg_type;          // MSG_DETECTION_MODE
  uint8_t mode;              // 2=polar (other values ignored)
} __attribute__((packed));   // 2 bytes

struct LedRenderFramePacket
{
  uint8_t msg_type;              // MSG_LED_RENDER_FRAME
  uint8_t render_mode;           // 0=sector-fill, 1=radar
  uint8_t num_zones;             // 4, 6, or 8
  uint8_t brightness;            // 0-64
  uint8_t center_color;          // LED_COLOR_*
  uint8_t ring_zone_colors[6][8]; // ring index 0..5 => rings 1..6, zone 0..7
} __attribute__((packed));

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

Sensor sensors[NUM_SENSORS];
uint8_t sensorSeen[NUM_SENSORS] = {0};
uint32_t packetCountBySensor[NUM_SENSORS] = {0};
uint32_t lastSensorTimestampMs[NUM_SENSORS] = {0};
volatile uint8_t sensorPendingProcess[NUM_SENSORS] = {0};
uint32_t lastRxMsBySensor[NUM_SENSORS] = {0};
float rxHzBySensor[NUM_SENSORS] = {0.0f};
uint32_t lastStatusEmitMs = 0;
uint32_t statusEmitPeriodMs = RuntimeDefaults::kStatusEmitPeriodMs;
uint32_t sensorStaleTimeoutMs = RuntimeDefaults::kSensorStaleTimeoutMs;

// ── Beta: proximity detection state ──────────────────────────────────────────
int     proximityNumZones       = RuntimeDefaults::kDefaultZoneCount;
int     proximityBrightness     = RuntimeDefaults::kDefaultBrightness;
bool    proximityVisualEnabled  = RuntimeDefaults::kDefaultVisualEnabled;
uint8_t proximityActiveSectors  = RuntimeDefaults::kDefaultActiveSectorMask;
// Ring distance thresholds (mm) — updated by ConfigPacket
float proximityDistRings[5] = {
    RuntimeDefaults::kRingThresholdsMm[0],
    RuntimeDefaults::kRingThresholdsMm[1],
    RuntimeDefaults::kRingThresholdsMm[2],
    RuntimeDefaults::kRingThresholdsMm[3],
    RuntimeDefaults::kRingThresholdsMm[4]};
uint8_t proximityDetectionMode = DETECTION_MODE_POLAR_GRID;
uint8_t proximityLedRenderMode = RuntimeDefaults::kDefaultLedRenderMode;

const int POLAR_NUM_RINGS = RuntimeDefaults::kPolarNumRings;
const int POLAR_MAX_ZONES = RuntimeDefaults::kPolarMaxZones;
const int POLAR_BIN_COUNT = POLAR_NUM_RINGS * POLAR_MAX_ZONES;
uint8_t polarConfRise = RuntimeDefaults::kPolarConfRise;
uint8_t polarConfDecay = RuntimeDefaults::kPolarConfDecay;
uint8_t polarEnterThreshold = RuntimeDefaults::kPolarEnterThreshold;
uint8_t polarExitThreshold = RuntimeDefaults::kPolarExitThreshold;
uint8_t polarConfidence[POLAR_BIN_COUNT] = {0};
uint8_t polarOccupied[POLAR_BIN_COUNT] = {0};

uint32_t lastProximityBroadcastMs = 0;
uint32_t proximityPeriodMs = RuntimeDefaults::kProximityPeriodMs;
uint8_t  broadcastPeerAdded = 0;
char serialCmdBuffer[200] = {0};
uint16_t serialCmdLen = 0;

// Forward declarations for proximity helpers (defined before setup())
void applyProximityThresholds(float redMaxMm, float yellowMaxMm);
void updateAndBroadcastLedFrame();
void invalidateStaleSensors();
void clearPolarGrid();
void accumulatePolarHits(uint8_t hitMask[POLAR_BIN_COUNT]);
void updatePolarGrid(const uint8_t hitMask[POLAR_BIN_COUNT]);
void computeZoneProximityFromPolar(float closestMmByZone[8]);
int proximityRingIndexFromDistance(float distMm);
int polarRingIndexFromDistance(float distMm);
float polarRepresentativeDistanceForRing(int ringIndex);
void emitDetectionDebugFrame(const float closestMmByZone[8]);
static int proximityZoneIndex(float angleDeg, int numZones);
static int clampInt(int v, int lo, int hi);
void broadcastLedRenderFrame(const float closestMmByZone[8]);
void emitTuningConfigLine();
void handleSerialCommand(char *line);
void serviceSerialCommands();
void broadcastComponentStatus();

static constexpr bool kEmitPolarGridDebugCsv = false;

static bool serialTelemetryWritable(int minBytes)
{
  if (minBytes < 1) minBytes = 1;
  return Serial.availableForWrite() >= minBytes;
}

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
  sensors[sensorIndex].computeLatestWorldPoints();
}

void emitPointsForSensor(int sensorIndex)
{
  if (!serialTelemetryWritable(128))
  {
    return;
  }

  uint8_t sid = SENSOR_IDS[sensorIndex];
  uint32_t ts = lastSensorTimestampMs[sensorIndex];

  for (int cell = 0; cell < 16; cell++)
  {
    if (!serialTelemetryWritable(96))
    {
      return;
    }

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
  if (!serialTelemetryWritable(24))
  {
    return;
  }
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
  if ((nowMs - lastStatusEmitMs) < statusEmitPeriodMs)
  {
    return;
  }
  lastStatusEmitMs = nowMs;

  bool canSerial = serialTelemetryWritable(64);
  if (canSerial)
  {
    for (int i = 0; i < NUM_SENSORS; i++)
    {
      if (!serialTelemetryWritable(32))
      {
        break;
      }

      // S,sid,pkts,rx_hz
      Serial.print("S,");
      Serial.print(SENSOR_IDS[i]);
      Serial.print(",");
      Serial.print(packetCountBySensor[i]);
      Serial.print(",");
      Serial.println(rxHzBySensor[i], 2);
    }
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
  pkt.flags = (proximityVisualEnabled ? 0x01 : 0x00) |
              ((proximityDetectionMode & 0x03) << 1);

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

  if (len == (int)sizeof(DetectionModePacket))
  {
    DetectionModePacket modePkt;
    memcpy(&modePkt, incomingData, sizeof(modePkt));
    if (modePkt.msg_type == MSG_DETECTION_MODE)
    {
      // Beta architecture is polar-only in MainController.
      (void)modePkt.mode;
      proximityDetectionMode = DETECTION_MODE_POLAR_GRID;
      clearPolarGrid();
      Serial.println("[Config] detection_mode forced to polar (2)");
      return;
    }
  }

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
      proximityLedRenderMode = (uint8_t)clampInt((int)cfg.render_mode, 0, 1);
      proximityActiveSectors = cfg.active_sectors;
      applyProximityThresholds(cfg.red_threshold_mm, cfg.yellow_threshold_mm);
      if (!proximityVisualEnabled)
      {
        clearPolarGrid();
      }
      Serial.printf("[Config] zones=%d bright=%d visual=%s led_mode=%d sectors=0x%02X\n",
                    proximityNumZones, proximityBrightness,
                    proximityVisualEnabled ? "on" : "off", (int)proximityLedRenderMode,
                    proximityActiveSectors);
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
    if (ageMs <= sensorStaleTimeoutMs) continue;

    sensorSeen[i] = 0;
    sensorPendingProcess[i] = 0;
    rxHzBySensor[i] = 0.0f;
    Serial.printf("[Sensor] sid=%d marked stale after %lums\n", SENSOR_IDS[i], ageMs);
  }
}

void clearPolarGrid()
{
  for (int i = 0; i < POLAR_BIN_COUNT; i++)
  {
    polarConfidence[i] = 0;
    polarOccupied[i] = 0;
  }
}

static int polarBinIndex(int ringIndex, int zoneIndex)
{
  return (ringIndex * POLAR_MAX_ZONES) + zoneIndex;
}

int proximityRingIndexFromDistance(float distMm)
{
  if (distMm < proximityDistRings[0]) return 0;
  if (distMm < proximityDistRings[1]) return 1;
  if (distMm < proximityDistRings[2]) return 2;
  if (distMm < proximityDistRings[3]) return 3;
  if (distMm < proximityDistRings[4]) return 4;
  return 5;
}

int polarRingIndexFromDistance(float distMm)
{
  if (distMm <= 0.0f) return 0;
  const float maxRange = RuntimeDefaults::kMaxObstacleRangeMm;
  float normalized = distMm / maxRange;
  if (normalized >= 1.0f) return POLAR_NUM_RINGS - 1;
  int ring = (int)(normalized * (float)POLAR_NUM_RINGS);
  if (ring < 0) ring = 0;
  if (ring >= POLAR_NUM_RINGS) ring = POLAR_NUM_RINGS - 1;
  return ring;
}

float polarRepresentativeDistanceForRing(int ringIndex)
{
  if (ringIndex < 0) ringIndex = 0;
  if (ringIndex >= POLAR_NUM_RINGS) ringIndex = POLAR_NUM_RINGS - 1;
  const float step = RuntimeDefaults::kMaxObstacleRangeMm / (float)POLAR_NUM_RINGS;
  float lower = step * (float)ringIndex;
  float upper = lower + step;
  return lower + ((upper - lower) * 0.5f);
}

void accumulatePolarHits(uint8_t hitMask[POLAR_BIN_COUNT])
{
  for (int i = 0; i < POLAR_BIN_COUNT; i++) hitMask[i] = 0;

  for (int s = 0; s < NUM_SENSORS; s++)
  {
    if (!sensorSeen[s]) continue;
    for (int cell = 0; cell < 16; cell++)
    {
      const Point &p = sensors[s].latestWorldPoints[cell];
      if (!p.isValid) continue;

      float dist = sqrtf(p.worldX * p.worldX + p.worldY * p.worldY);
      if (dist > RuntimeDefaults::kMaxObstacleRangeMm) continue;

      float angleDeg = atan2f(-p.worldY, p.worldX) * (180.0f / 3.14159265f);
      int zone = proximityZoneIndex(angleDeg, POLAR_MAX_ZONES);
      if (zone < 0 || zone >= POLAR_MAX_ZONES) continue;

      int ring = polarRingIndexFromDistance(dist);
      int idx = polarBinIndex(ring, zone);
      if (idx >= 0 && idx < POLAR_BIN_COUNT) hitMask[idx] = 1;
    }
  }
}

void updatePolarGrid(const uint8_t hitMask[POLAR_BIN_COUNT])
{
  for (int i = 0; i < POLAR_BIN_COUNT; i++)
  {
    uint8_t conf = polarConfidence[i];
    if (hitMask[i])
    {
      uint16_t boosted = (uint16_t)conf + polarConfRise;
      conf = (boosted > 255U) ? 255U : (uint8_t)boosted;
    }
    else
    {
      conf = (conf > polarConfDecay) ? (uint8_t)(conf - polarConfDecay) : 0;
    }
    polarConfidence[i] = conf;

    if (!polarOccupied[i] && conf >= polarEnterThreshold) polarOccupied[i] = 1;
    else if (polarOccupied[i] && conf <= polarExitThreshold) polarOccupied[i] = 0;
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
  if (numZones <= 0) return -1;
  const float step = 360.0f / (float)numZones;
  int idx = (int)floorf((angleDeg + (0.5f * step)) / step);
  idx %= numZones;
  if (idx < 0) idx += numZones;
  return idx;
}

static float polarZoneCenterAngleDeg(int zoneIdx)
{
  const float step = 360.0f / (float)POLAR_MAX_ZONES;
  float angle = (float)zoneIdx * step;
  if (angle >= 180.0f) angle -= 360.0f;
  return angle;
}

void computeZoneProximityFromPolar(float closestMmByZone[8])
{
  for (int z = 0; z < 8; z++) closestMmByZone[z] = 1e9f;

  int mappedOutputZoneByPolarZone[POLAR_MAX_ZONES];
  for (int iz = 0; iz < POLAR_MAX_ZONES; iz++)
  {
    float angleDeg = polarZoneCenterAngleDeg(iz);
    mappedOutputZoneByPolarZone[iz] = proximityZoneIndex(angleDeg, proximityNumZones);
  }

  for (int outZone = 0; outZone < proximityNumZones; outZone++)
  {
    if (!(proximityActiveSectors & (1 << outZone))) continue;

    for (int ring = 0; ring < POLAR_NUM_RINGS; ring++)
    {
      bool occupied = false;
      for (int iz = 0; iz < POLAR_MAX_ZONES; iz++)
      {
        if (mappedOutputZoneByPolarZone[iz] != outZone) continue;
        int idx = polarBinIndex(ring, iz);
        if (idx < 0 || idx >= POLAR_BIN_COUNT) continue;
        if (polarOccupied[idx])
        {
          occupied = true;
          break;
        }
      }
      if (occupied)
      {
        closestMmByZone[outZone] = polarRepresentativeDistanceForRing(ring);
        break;
      }
    }
  }
}

void emitDetectionDebugFrame(const float closestMmByZone[8])
{
  if (!serialTelemetryWritable(96))
  {
    return;
  }

  Serial.print("DL,");
  Serial.print((int)proximityDetectionMode);
  Serial.print(",");
  Serial.print((int)proximityNumZones);
  Serial.print(",");
  Serial.print((int)proximityActiveSectors);
  for (int i = 0; i < 5; i++)
  {
    Serial.print(",");
    Serial.print((int)proximityDistRings[i]);
  }
  for (int z = 0; z < 8; z++)
  {
    Serial.print(",");
    if (closestMmByZone[z] >= 1e9f) Serial.print(-1);
    else Serial.print((int)closestMmByZone[z]);
  }
  Serial.println();

  if (proximityDetectionMode != DETECTION_MODE_POLAR_GRID) return;
  if (!kEmitPolarGridDebugCsv) return;
  if (!serialTelemetryWritable(128)) return;

  Serial.print("DP,");
  Serial.print(POLAR_MAX_ZONES);
  for (int ring = 0; ring < POLAR_NUM_RINGS; ring++)
  {
    for (int zone = 0; zone < POLAR_MAX_ZONES; zone++)
    {
      int idx = polarBinIndex(ring, zone);
      Serial.print(",");
      Serial.print((int)polarConfidence[idx]);
    }
  }
  Serial.println();

  Serial.print("DO,");
  Serial.print(POLAR_MAX_ZONES);
  for (int ring = 0; ring < POLAR_NUM_RINGS; ring++)
  {
    for (int zone = 0; zone < POLAR_MAX_ZONES; zone++)
    {
      int idx = polarBinIndex(ring, zone);
      Serial.print(",");
      Serial.print((int)polarOccupied[idx]);
    }
  }
  Serial.println();
}

void updateAndBroadcastLedFrame()
{
  // Build the latest zone distances from the polar filter, emit debug serial,
  // then send a render frame for LEDRingController.
  float closestMmByZone[8];
  uint8_t hitMask[POLAR_BIN_COUNT];
  accumulatePolarHits(hitMask);
  updatePolarGrid(hitMask);
  computeZoneProximityFromPolar(closestMmByZone);

  emitDetectionDebugFrame(closestMmByZone);
  broadcastLedRenderFrame(closestMmByZone);
}

static uint8_t ringIndexToLedColor(int ringIndex)
{
  // ringIndex 0..5 => rings 1..6
  if (ringIndex <= 1) return LED_COLOR_RED;
  if (ringIndex <= 3) return LED_COLOR_ORANGE;
  return LED_COLOR_YELLOW;
}

static int mapOutputZoneIndex180(int zoneIdx, int numZones)
{
  if (numZones <= 0) return zoneIdx;
  return (zoneIdx + (numZones / 2)) % numZones;
}

void broadcastLedRenderFrame(const float closestMmByZone[8])
{
  LedRenderFramePacket pkt = {};
  pkt.msg_type = MSG_LED_RENDER_FRAME;
  pkt.render_mode = proximityLedRenderMode;
  pkt.num_zones = (uint8_t)proximityNumZones;
  pkt.brightness = (uint8_t)proximityBrightness;
  pkt.center_color = LED_COLOR_OFF;

  for (int z = 0; z < proximityNumZones && z < 8; z++)
  {
    float dist = closestMmByZone[z];
    if (dist >= RuntimeDefaults::kMaxObstacleRangeMm) continue;
    int outZone = mapOutputZoneIndex180(z, proximityNumZones);

    int ringIdx = proximityRingIndexFromDistance(dist);
    if (ringIdx < 0 || ringIdx > 5) continue;
    uint8_t color = ringIndexToLedColor(ringIdx);

    if (ringIdx == 0)
    {
      pkt.center_color = color;
    }

    if (proximityLedRenderMode == LED_RENDER_MODE_RADAR)
    {
      pkt.ring_zone_colors[ringIdx][outZone] = color;
    }
    else
    {
      // Sector mode: fill the full outward sector region (rings 2..6)
      // with the closest obstacle color.
      for (int r = 1; r <= 5; r++) pkt.ring_zone_colors[r][outZone] = color;
    }
  }

  esp_now_send(BROADCAST_MAC, (uint8_t *)&pkt, sizeof(pkt));
}

static int clampInt(int v, int lo, int hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void emitTuningConfigLine()
{
  int redMm = (int)proximityDistRings[1];
  int yellowMm = (int)proximityDistRings[3];
  Serial.print("CFG,mode,");
  Serial.print((int)proximityDetectionMode);
  Serial.print(",zones,");
  Serial.print((int)proximityNumZones);
  Serial.print(",bright,");
  Serial.print((int)proximityBrightness);
  Serial.print(",visual,");
  Serial.print(proximityVisualEnabled ? 1 : 0);
  Serial.print(",sectors_mask,");
  Serial.print((int)proximityActiveSectors);
  Serial.print(",red_mm,");
  Serial.print(redMm);
  Serial.print(",yellow_mm,");
  Serial.print(yellowMm);
  Serial.print(",stale_ms,");
  Serial.print((int)sensorStaleTimeoutMs);
  Serial.print(",status_ms,");
  Serial.print((int)statusEmitPeriodMs);
  Serial.print(",proximity_ms,");
  Serial.print((int)proximityPeriodMs);
  Serial.print(",led_mode,");
  Serial.print((int)proximityLedRenderMode);
  Serial.print(",polar_rings,");
  Serial.print(POLAR_NUM_RINGS);
  Serial.print(",polar_zones_max,");
  Serial.print(POLAR_MAX_ZONES);
  Serial.print(",polar_rise,");
  Serial.print((int)polarConfRise);
  Serial.print(",polar_decay,");
  Serial.print((int)polarConfDecay);
  Serial.print(",polar_enter,");
  Serial.print((int)polarEnterThreshold);
  Serial.print(",polar_exit,");
  Serial.println((int)polarExitThreshold);
}

void handleSerialCommand(char *line)
{
  if (line == nullptr || line[0] == '\0') return;

  if (strcmp(line, "GET") == 0)
  {
    emitTuningConfigLine();
    return;
  }

  if (strcmp(line, "HELP") == 0)
  {
    Serial.println("HELP,GET|SET,<key>,<value>");
    return;
  }

  if (strncmp(line, "SET,", 4) != 0) return;

  char *savePtr = nullptr;
  (void)strtok_r(line, ",", &savePtr); // "SET"
  char *key = strtok_r(nullptr, ",", &savePtr);
  char *value = strtok_r(nullptr, ",", &savePtr);
  if (key == nullptr || value == nullptr)
  {
    Serial.println("ERR,SET,missing_key_or_value");
    return;
  }

  char *endPtr = nullptr;
  long raw = strtol(value, &endPtr, 10);
  if (endPtr == value)
  {
    Serial.println("ERR,SET,invalid_value");
    return;
  }

  bool updated = true;
  if (strcmp(key, "mode") == 0)
  {
    // Polar only in beta architecture.
    raw = DETECTION_MODE_POLAR_GRID;
    proximityDetectionMode = DETECTION_MODE_POLAR_GRID;
    clearPolarGrid();
  }
  else if (strcmp(key, "zones") == 0)
  {
    int zones = (int)raw;
    if (zones != 4 && zones != 6 && zones != 8) updated = false;
    else proximityNumZones = zones;
  }
  else if (strcmp(key, "bright") == 0)
  {
    proximityBrightness = clampInt((int)raw, 0, 64);
  }
  else if (strcmp(key, "visual") == 0)
  {
    proximityVisualEnabled = (raw != 0);
    if (!proximityVisualEnabled)
    {
      clearPolarGrid();
    }
  }
  else if (strcmp(key, "sectors_mask") == 0)
  {
    proximityActiveSectors = (uint8_t)(raw & 0xFF);
  }
  else if (strcmp(key, "red_mm") == 0)
  {
    int yellowMm = (int)proximityDistRings[3];
    int redMm = clampInt((int)raw, 50, 2990);
    if (redMm >= yellowMm) redMm = yellowMm - 10;
    redMm = clampInt(redMm, 50, 2990);
    applyProximityThresholds((float)redMm, (float)yellowMm);
  }
  else if (strcmp(key, "yellow_mm") == 0)
  {
    int redMm = (int)proximityDistRings[1];
    int yellowMm = clampInt((int)raw, 60, 3000);
    if (yellowMm <= redMm) yellowMm = redMm + 10;
    yellowMm = clampInt(yellowMm, 60, 3000);
    applyProximityThresholds((float)redMm, (float)yellowMm);
  }
  else if (strcmp(key, "stale_ms") == 0)
  {
    sensorStaleTimeoutMs = (uint32_t)clampInt((int)raw, 50, 10000);
  }
  else if (strcmp(key, "status_ms") == 0)
  {
    statusEmitPeriodMs = (uint32_t)clampInt((int)raw, 100, 10000);
  }
  else if (strcmp(key, "proximity_ms") == 0)
  {
    proximityPeriodMs = (uint32_t)clampInt((int)raw, 20, 2000);
  }
  else if (strcmp(key, "led_mode") == 0)
  {
    proximityLedRenderMode = (uint8_t)clampInt((int)raw, 0, 1);
  }
  else if (strcmp(key, "polar_rise") == 0)
  {
    polarConfRise = (uint8_t)clampInt((int)raw, 1, 255);
  }
  else if (strcmp(key, "polar_decay") == 0)
  {
    polarConfDecay = (uint8_t)clampInt((int)raw, 0, 255);
  }
  else if (strcmp(key, "polar_enter") == 0)
  {
    polarEnterThreshold = (uint8_t)clampInt((int)raw, 1, 255);
    if (polarExitThreshold >= polarEnterThreshold) polarExitThreshold = polarEnterThreshold - 1;
  }
  else if (strcmp(key, "polar_exit") == 0)
  {
    polarExitThreshold = (uint8_t)clampInt((int)raw, 0, 254);
    if (polarExitThreshold >= polarEnterThreshold) polarEnterThreshold = polarExitThreshold + 1;
  }
  else
  {
    updated = false;
  }

  if (!updated)
  {
    Serial.print("ERR,SET,unknown_or_invalid_key,");
    Serial.println(key);
    return;
  }

  Serial.print("ACK,");
  Serial.print(key);
  Serial.print(",");
  Serial.println((int)raw);
  emitTuningConfigLine();
}

void serviceSerialCommands()
{
  while (Serial.available() > 0)
  {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n')
    {
      serialCmdBuffer[serialCmdLen] = '\0';
      if (serialCmdLen > 0) handleSerialCommand(serialCmdBuffer);
      serialCmdLen = 0;
      continue;
    }

    if (serialCmdLen < (sizeof(serialCmdBuffer) - 1))
    {
      serialCmdBuffer[serialCmdLen++] = ch;
    }
  }
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

  Serial.printf("[Config] detection_mode=%d (polar-only)\n",
                (int)DETECTION_MODE_POLAR_GRID);
  emitTuningConfigLine();

  esp_now_register_recv_cb(OnDataRecv);

  // Broadcast peer -- used to transmit render frames + status
  {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.ifidx  = WIFI_IF_STA;
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) == ESP_OK)
    {
      broadcastPeerAdded = 1;
      Serial.println("Broadcast peer registered for render/status packets");
    }
  }

  Serial.println("ESP-NOW receiver ready");
}

void loop()
{
  serviceSerialCommands();
  invalidateStaleSensors();
  processPendingSensors();
  emitReceiverStatusIfDue();

  // Broadcast render frames to LEDRingController at configured update rate.
  if (proximityVisualEnabled && broadcastPeerAdded)
  {
    uint32_t nowMs = millis();
    if ((nowMs - lastProximityBroadcastMs) >= proximityPeriodMs)
    {
      lastProximityBroadcastMs = nowMs;
      updateAndBroadcastLedFrame();
    }
  }

  delay(1);
}

