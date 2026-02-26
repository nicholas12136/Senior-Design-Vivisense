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
        {1, 0.0f, 266.7f, 603.25f, 0.0f, 0.0f, 30.0f},
        {2, 25.4f, 228.6f, 603.25f, 0.0f, 0.0f, 0.0f},
        {3, 25.4f, -241.3f, 603.25f, 180.0f, 0.0f, 0.0f},
        {4, 0.0f, -279.4f, 603.25f, 180.0f, 0.0f, -30.0f},
        {5, -584.2f, -63.5f, 1333.5f, 0.0f, 10.0f, -90.0f},
        {6, -647.7f, -12.7f, 1333.5f, 0.0f, 10.0f, -150.0f},
        {7, -641.35f, 25.4f, 1333.5f, 180.0f, 10.0f, 150.0f},
        {8, -584.2f, 63.5f, 1333.5f, 180.0f, 10.0f, 90.0f},
};
struct SensorPacket
{
  uint8_t sensor_id;
  uint32_t timestamp_ms;
  uint16_t distance_mm[16];
  uint8_t target_status[16];
  uint8_t nb_targets[16];
} __attribute__((packed));

Sensor sensors[NUM_SENSORS];
uint8_t sensorSeen[NUM_SENSORS] = {0};
uint32_t packetCountBySensor[NUM_SENSORS] = {0};
uint32_t lastSensorTimestampMs[NUM_SENSORS] = {0};
volatile uint8_t sensorPendingProcess[NUM_SENSORS] = {0};
uint32_t lastRxMsBySensor[NUM_SENSORS] = {0};
float rxHzBySensor[NUM_SENSORS] = {0.0f};
uint32_t lastStatusEmitMs = 0;
const uint32_t STATUS_EMIT_PERIOD_MS = 500;

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
    // S,sid,pkts,rx_hz
    Serial.print("S,");
    Serial.print(SENSOR_IDS[i]);
    Serial.print(",");
    Serial.print(packetCountBySensor[i]);
    Serial.print(",");
    Serial.println(rxHzBySensor[i], 2);
  }
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
  (void)mac;

  if (len != (int)sizeof(SensorPacket))
    return;

  SensorPacket pkt;
  memcpy(&pkt, incomingData, sizeof(SensorPacket));

  int idx = findSensorIndex(pkt.sensor_id);
  if (idx < 0)
    return;

  sensors[idx].setAllCellData(pkt.distance_mm, pkt.target_status, pkt.nb_targets);
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
  emitReceiverStatusIfDue();
  delay(1);
}
