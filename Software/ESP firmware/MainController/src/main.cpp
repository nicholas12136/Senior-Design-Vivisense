#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <string.h>
#include <math.h>
#include "SensorAndPoint.h"
#include "led_frame.h"

// Minimal base receiver:
// - ESP-NOW only
// - Receives sensor packets
// - Converts latest packet to world-frame XYZ points in loop()
// - Streams compact point CSV over Serial for a PC viewer

const uint8_t ESPNOW_CHANNEL = 1;
const uint32_t SERIAL_BAUD = 115200;
const uint8_t LATENCY_TEST_MODE = 0; // Set to 0 to disable clock-sync latency test mode.

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

// V2 extends the data packet with sender-side timing metadata.
struct SensorPacketV2
{
  uint8_t sensor_id;
  uint32_t timestamp_ms;
  uint16_t distance_mm[16];
  uint8_t target_status[16];
  uint8_t nb_targets[16];
  uint16_t seq;
  uint32_t t_send_us;
} __attribute__((packed));

const uint8_t MSG_SYNC_REQ = 0xA1;
const uint8_t MSG_SYNC_RESP = 0xA2;

struct SyncRequestPacket
{
  uint8_t msg_type;
  uint8_t sensor_id;
  uint16_t seq;
  uint32_t t1_base_us;
} __attribute__((packed));

struct SyncResponsePacket
{
  uint8_t msg_type;
  uint8_t sensor_id;
  uint16_t seq;
  uint32_t t1_base_us;
  uint32_t t2_pod_us;
  uint32_t t3_pod_us;
} __attribute__((packed));

// ── Beta: inter-ESP32 packets ─────────────────────────────────────────────────

const uint8_t MSG_CONFIG         = 0xB1; // CaregiverApp -> MainController (+ LEDRingController)
const uint8_t MSG_ZONE_PROXIMITY = 0xB3; // MainController -> LEDRingController (broadcast)

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
uint8_t sensorMacKnown[NUM_SENSORS] = {0};
uint8_t sensorMacBySensor[NUM_SENSORS][6] = {{0}};
uint8_t peerAddedBySensor[NUM_SENSORS] = {0};

// Per-sensor latency test metrics. Valid only when LATENCY_TEST_MODE == 1 and V2 packets arrive.
int32_t syncOffsetUsBySensor[NUM_SENSORS] = {0}; // pod clock - base clock
uint32_t syncRttUsBySensor[NUM_SENSORS] = {0};
uint8_t syncValidBySensor[NUM_SENSORS] = {0};
uint16_t syncSeqBySensor[NUM_SENSORS] = {0};
uint32_t wirelessUsLastBySensor[NUM_SENSORS] = {0};
float wirelessUsAvgBySensor[NUM_SENSORS] = {0.0f};
uint16_t dataSeqBySensor[NUM_SENSORS] = {0};
uint32_t lastSyncSendMs = 0;
const uint32_t SYNC_PERIOD_MS = 1000;
const uint32_t SENSOR_STALE_TIMEOUT_MS = 400;

// ── Beta: proximity detection state ──────────────────────────────────────────
int     proximityNumZones       = 6;
int     proximityBrightness     = 40;
bool    proximityVisualEnabled  = true;
uint8_t proximityActiveSectors  = 0xFF; // all zones on by default
// Ring distance thresholds (mm) — updated by ConfigPacket
float proximityDistRings[5] = {300.0f, 600.0f, 1050.0f, 1500.0f, 1950.0f};
float filteredClosestMmByZone[8] = {1e9f, 1e9f, 1e9f, 1e9f, 1e9f, 1e9f, 1e9f, 1e9f};
uint8_t filteredValidByZone[8] = {0};
uint8_t filteredMissCountByZone[8] = {0};
const float PROXIMITY_DISTANCE_EMA_ALPHA = 0.35f;
const uint8_t PROXIMITY_MISS_HOLD_FRAMES = 2; // hold ~130 ms at 15 Hz to reduce flicker.

uint32_t lastProximityBroadcastMs = 0;
const uint32_t PROXIMITY_PERIOD_MS = 67; // ~15 Hz
uint8_t  broadcastPeerAdded = 0;

// Forward declarations for proximity helpers (defined before setup())
void applyProximityThresholds(float redMaxMm, float yellowMaxMm);
void broadcastZoneProximity();
void invalidateStaleSensors();

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

bool addPeerIfNeeded(int sensorIndex, const uint8_t *mac)
{
  if (peerAddedBySensor[sensorIndex])
  {
    return true;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.ifidx = WIFI_IF_STA;
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  esp_err_t res = esp_now_add_peer(&peer);
  if (res == ESP_OK || res == ESP_ERR_ESPNOW_EXIST)
  {
    peerAddedBySensor[sensorIndex] = 1;
    return true;
  }
  return false;
}

void updateWirelessLatencyMetrics(int idx, uint32_t rxCbUs, uint32_t tSendPodUs)
{
  if (!syncValidBySensor[idx])
  {
    return;
  }

  // Convert pod send time into base clock estimate:
  // base ~= pod - offset, where offset = pod - base.
  uint32_t tSendBaseEstUs = (uint32_t)((int32_t)tSendPodUs - syncOffsetUsBySensor[idx]);
  uint32_t wirelessUs = rxCbUs - tSendBaseEstUs; // unsigned subtraction is wrap-safe for micros().
  wirelessUsLastBySensor[idx] = wirelessUs;
  if (wirelessUsAvgBySensor[idx] <= 0.0f)
  {
    wirelessUsAvgBySensor[idx] = (float)wirelessUs;
  }
  else
  {
    wirelessUsAvgBySensor[idx] = (0.85f * wirelessUsAvgBySensor[idx]) + (0.15f * (float)wirelessUs);
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
    // S,sid,pkts,rx_hz,conv_us_last,conv_us_avg,wireless_us_last,wireless_us_avg,sync_rtt_us,sync_offset_us,sync_ok
    Serial.print("S,");
    Serial.print(SENSOR_IDS[i]);
    Serial.print(",");
    Serial.print(packetCountBySensor[i]);
    Serial.print(",");
    Serial.print(rxHzBySensor[i], 2);
    Serial.print(",");
    Serial.print(convertUsLastBySensor[i]);
    Serial.print(",");
    Serial.print(convertUsAvgBySensor[i], 1);
    Serial.print(",");
    Serial.print(wirelessUsLastBySensor[i]);
    Serial.print(",");
    Serial.print(wirelessUsAvgBySensor[i], 1);
    Serial.print(",");
    Serial.print(syncRttUsBySensor[i]);
    Serial.print(",");
    Serial.print(syncOffsetUsBySensor[i]);
    Serial.print(",");
    Serial.println(syncValidBySensor[i] ? 1 : 0);
  }
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
  uint32_t rxCbUs = micros();

  if (LATENCY_TEST_MODE && len == (int)sizeof(SyncResponsePacket))
  {
    SyncResponsePacket resp;
    memcpy(&resp, incomingData, sizeof(resp));
    if (resp.msg_type == MSG_SYNC_RESP)
    {
      int idx = findSensorIndex(resp.sensor_id);
      if (idx >= 0)
      {
        uint32_t t1 = resp.t1_base_us;
        uint32_t t2 = resp.t2_pod_us;
        uint32_t t3 = resp.t3_pod_us;
        uint32_t t4 = rxCbUs;

        // NTP-style offset in microseconds: pod clock - base clock.
        int32_t offsetNew = (int32_t)(((int64_t)((int32_t)(t2 - t1)) + (int64_t)((int32_t)(t3 - t4))) / 2);
        uint32_t rttNew = (t4 - t1) - (t3 - t2);

        if (!syncValidBySensor[idx])
        {
          syncOffsetUsBySensor[idx] = offsetNew;
        }
        else
        {
          // Simple EMA to reduce jitter.
          syncOffsetUsBySensor[idx] = (int32_t)((0.9f * (float)syncOffsetUsBySensor[idx]) + (0.1f * (float)offsetNew));
        }
        syncRttUsBySensor[idx] = rttNew;
        syncValidBySensor[idx] = 1;
      }
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
      proximityActiveSectors = cfg.active_sectors;
      applyProximityThresholds(cfg.red_threshold_mm, cfg.yellow_threshold_mm);
      Serial.printf("[Config] zones=%d bright=%d visual=%s sectors=0x%02X\n",
                    proximityNumZones, proximityBrightness,
                    proximityVisualEnabled ? "on" : "off", proximityActiveSectors);
      return;
    }
  }

  SensorPacket pktV1;
  SensorPacketV2 pktV2;
  uint8_t isV2 = 0;
  if (len == (int)sizeof(SensorPacketV2))
  {
    memcpy(&pktV2, incomingData, sizeof(SensorPacketV2));
    isV2 = 1;
  }
  else if (len == (int)sizeof(SensorPacket))
  {
    memcpy(&pktV1, incomingData, sizeof(SensorPacket));
    isV2 = 0;
  }
  else
  {
    return;
  }

  uint8_t sid = isV2 ? pktV2.sensor_id : pktV1.sensor_id;
  int idx = findSensorIndex(sid);
  if (idx < 0)
    return;

  if (!sensorMacKnown[idx])
  {
    memcpy(sensorMacBySensor[idx], mac, 6);
    sensorMacKnown[idx] = 1;
    addPeerIfNeeded(idx, mac);
  }

  sensors[idx].setAllCellData(
      isV2 ? pktV2.distance_mm : pktV1.distance_mm,
      isV2 ? pktV2.target_status : pktV1.target_status,
      isV2 ? pktV2.nb_targets : pktV1.nb_targets);
  sensorSeen[idx] = 1;
  packetCountBySensor[idx]++;
  lastSensorTimestampMs[idx] = isV2 ? pktV2.timestamp_ms : pktV1.timestamp_ms;
  if (isV2)
  {
    dataSeqBySensor[idx] = pktV2.seq;
    if (LATENCY_TEST_MODE)
    {
      updateWirelessLatencyMetrics(idx, rxCbUs, pktV2.t_send_us);
    }
  }
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

void maybeSendSyncRequests()
{
  if (!LATENCY_TEST_MODE)
  {
    return;
  }
  uint32_t nowMs = millis();
  if ((nowMs - lastSyncSendMs) < SYNC_PERIOD_MS)
  {
    return;
  }
  lastSyncSendMs = nowMs;

  for (int i = 0; i < NUM_SENSORS; i++)
  {
    if (!sensorMacKnown[i])
    {
      continue;
    }
    if (!addPeerIfNeeded(i, sensorMacBySensor[i]))
    {
      continue;
    }
    SyncRequestPacket req;
    req.msg_type = MSG_SYNC_REQ;
    req.sensor_id = SENSOR_IDS[i];
    req.seq = ++syncSeqBySensor[i];
    req.t1_base_us = micros();
    esp_now_send(sensorMacBySensor[i], (uint8_t *)&req, sizeof(req));
  }
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

void broadcastZoneProximity()
{
  ZoneProximityPacket pkt;
  pkt.msg_type  = MSG_ZONE_PROXIMITY;
  pkt.num_zones = (uint8_t)proximityNumZones;

  float rawClosestMmByZone[8];
  for (int z = 0; z < 8; z++) rawClosestMmByZone[z] = 1e9f;

  // Accumulate closest valid world point per zone across all sensors
  for (int s = 0; s < NUM_SENSORS; s++)
  {
    if (!sensorSeen[s]) continue;
    for (int cell = 0; cell < 16; cell++)
    {
      const Point &p = sensors[s].latestWorldPoints[cell];
      if (!p.isValid) continue;

      // Horizontal (floor-plane) distance
      float dist = sqrtf(p.worldX * p.worldX + p.worldY * p.worldY);
      if (dist > 3000.0f) continue;

      // atan2(-y, x) maps +y=LEFT coord to CW-from-forward angle in degrees
      float angleDeg = atan2f(-p.worldY, p.worldX) * (180.0f / 3.14159265f);
      int zone = proximityZoneIndex(angleDeg, proximityNumZones);
      if (!(proximityActiveSectors & (1 << zone))) continue;

      if (dist < rawClosestMmByZone[zone]) rawClosestMmByZone[zone] = dist;
    }
  }

  for (int z = 0; z < 8; z++)
  {
    bool zoneEnabled = (z < proximityNumZones) && ((proximityActiveSectors & (1 << z)) != 0);
    if (!zoneEnabled)
    {
      filteredValidByZone[z] = 0;
      filteredMissCountByZone[z] = 0;
      filteredClosestMmByZone[z] = 1e9f;
      pkt.closest_mm[z] = 1e9f;
      continue;
    }

    float rawDist = rawClosestMmByZone[z];
    if (rawDist < 1e9f)
    {
      if (!filteredValidByZone[z])
      {
        filteredClosestMmByZone[z] = rawDist;
      }
      else
      {
        filteredClosestMmByZone[z] =
            (1.0f - PROXIMITY_DISTANCE_EMA_ALPHA) * filteredClosestMmByZone[z] +
            (PROXIMITY_DISTANCE_EMA_ALPHA * rawDist);
      }
      filteredValidByZone[z] = 1;
      filteredMissCountByZone[z] = 0;
      pkt.closest_mm[z] = filteredClosestMmByZone[z];
      continue;
    }

    if (filteredValidByZone[z] && filteredMissCountByZone[z] < PROXIMITY_MISS_HOLD_FRAMES)
    {
      filteredMissCountByZone[z]++;
      pkt.closest_mm[z] = filteredClosestMmByZone[z];
    }
    else
    {
      filteredValidByZone[z] = 0;
      filteredMissCountByZone[z] = 0;
      filteredClosestMmByZone[z] = 1e9f;
      pkt.closest_mm[z] = 1e9f;
    }
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
  maybeSendSyncRequests();
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
