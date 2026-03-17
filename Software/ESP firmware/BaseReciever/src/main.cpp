#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <string.h>
#include "SensorAndPoint.h"

// Minimal base receiver:
// - ESP-NOW only
// - Receives sensor packets
// - Converts latest packet to world-frame XYZ points in loop()
// - Streams compact point CSV over Serial for a PC viewer

const uint8_t ESPNOW_CHANNEL = 1;
const uint32_t SERIAL_BAUD = 921600;
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

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("ESP-NOW receiver ready");
}

void loop()
{
  processPendingSensors();
  maybeSendSyncRequests();
  emitReceiverStatusIfDue();
  delay(1);
}
