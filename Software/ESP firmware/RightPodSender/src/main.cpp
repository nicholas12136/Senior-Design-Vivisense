#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <SparkFun_VL53L5CX_Library.h>

// --- Pin & device configuration ------------------------------------------------
constexpr uint8_t XSHUT_A = 18;
constexpr uint8_t XSHUT_B = 19;
constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;

constexpr uint8_t SENSOR_A_ID = 3; // 1,2 for left 3,4 for right
constexpr uint8_t SENSOR_B_ID = 4;
const uint8_t SENSOR_A_FREQ = 15;
const uint8_t SENSOR_B_FREQ = 15;

constexpr uint8_t ADDR_A = 0x23;
constexpr uint8_t ADDR_B = 0x44;

// const uint8_t baseMac[6] = {0x88, 0x56, 0xA6, 0x70, 0x09, 0xDC}; // ESPC3 Super Mini
const uint8_t baseMac[6] = {0x5C, 0x01, 0x3B, 0x88, 0x04, 0x58}; // Wroom32
uint32_t startTime;
const uint8_t ESPNOW_CHANNEL = 1; // Must match baseReciever AP_CHANNEL
const uint8_t LATENCY_TEST_MODE = 1; // Set to 0 to disable clock-sync latency test mode.

// --- Types ---------------------------------------------------------------------
struct SensorPacket
{
  uint8_t sensor_id;
  uint32_t timestamp_ms;
  uint16_t distance_mm[16];
  uint8_t target_status[16];
  uint8_t nb_targets[16];
} __attribute__((packed));

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

// --- State ---------------------------------------------------------------------
SparkFun_VL53L5CX sensorA;
SparkFun_VL53L5CX sensorB;
VL53L5CX_ResultsData resultsA;
VL53L5CX_ResultsData resultsB;
uint16_t dataSeqBySensor[9] = {0}; // indexed by sensor ID (1..8)

// --- ESP-NOW helpers -----------------------------------------------------------
void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status)
{
  (void)mac;
  if (status != ESP_NOW_SEND_SUCCESS)
  {
    Serial.print("ESP-NOW send status: FAIL (");
    Serial.print((int)status);
    Serial.println(")");
  }
}

void onEspNowRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
  if (!LATENCY_TEST_MODE)
  {
    return;
  }
  if (len != (int)sizeof(SyncRequestPacket))
  {
    return;
  }

  SyncRequestPacket req;
  memcpy(&req, incomingData, sizeof(req));
  if (req.msg_type != MSG_SYNC_REQ)
  {
    return;
  }
  if (req.sensor_id != SENSOR_A_ID && req.sensor_id != SENSOR_B_ID)
  {
    return;
  }

  SyncResponsePacket resp;
  resp.msg_type = MSG_SYNC_RESP;
  resp.sensor_id = req.sensor_id;
  resp.seq = req.seq;
  resp.t1_base_us = req.t1_base_us;
  resp.t2_pod_us = micros();
  resp.t3_pod_us = micros();
  esp_now_send(mac, (uint8_t *)&resp, sizeof(resp));
}

bool addEspNowPeer(const uint8_t *mac)
{
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.ifidx = WIFI_IF_STA;
  peer.channel = ESPNOW_CHANNEL; // explicit channel match with base receiver
  peer.encrypt = false;

  const esp_err_t res = esp_now_add_peer(&peer);
  if (res == ESP_OK)
  {
    Serial.println("Peer added");
    return true;
  }
  Serial.print("Peer add failed: ");
  Serial.println(res);
  return false;
}

// --- Sensor helpers ------------------------------------------------------------
bool setupSensor(SparkFun_VL53L5CX &sensor, uint8_t xshutPin, uint8_t newAddr, const uint8_t freq, const int name)
{
  digitalWrite(xshutPin, HIGH);
  delay(50);

  if (!sensor.begin())
  {
    Serial.print(name);
    Serial.println(" begin() failed");
    return false;
  }

  if (!sensor.setAddress(newAddr))
  {
    Serial.print(name);
    Serial.println(" setAddress() failed");
    return false;
  }

  sensor.setRangingFrequency(freq);
  sensor.setResolution(16);

  if (!sensor.startRanging())
  {
    Serial.print(name);
    Serial.println(" startRanging() failed");
    return false;
  }

  Serial.print(name);
  Serial.print(" ready at 0x");
  Serial.println(sensor.getAddress(), HEX);
  return true;
}

// --- Data path -----------------------------------------------------------------
void trySendSensor(SparkFun_VL53L5CX &sensor,
                   VL53L5CX_ResultsData &results,
                   uint8_t sensorId)
{
  if (!sensor.isDataReady())
  {
    return;
  }

  if (!sensor.getRangingData(&results))
  {
    Serial.print("getRangingData() failed for sensor ");
    Serial.println(sensorId);
    return;
  }

  SensorPacket pkt{};
  if (LATENCY_TEST_MODE)
  {
    SensorPacketV2 pkt2;
    pkt2.sensor_id = sensorId;
    pkt2.timestamp_ms = (millis() - startTime);
    for (uint8_t i = 0; i < 16; ++i)
    {
      pkt2.distance_mm[i] = results.distance_mm[i];
      pkt2.target_status[i] = results.target_status[i];
      pkt2.nb_targets[i] = results.nb_target_detected[i];
    }
    pkt2.seq = ++dataSeqBySensor[sensorId];
    pkt2.t_send_us = micros();
    esp_now_send(baseMac, (uint8_t *)&pkt2, sizeof(pkt2));
  }
  else
  {
    pkt.sensor_id = sensorId;
    pkt.timestamp_ms = (millis() - startTime);
    for (uint8_t i = 0; i < 16; ++i)
    {
      pkt.distance_mm[i] = results.distance_mm[i];
      pkt.target_status[i] = results.target_status[i];
      pkt.nb_targets[i] = results.nb_target_detected[i];
    }
    esp_now_send(baseMac, (uint8_t *)&pkt, sizeof(pkt));
  }
}

// --- Arduino lifecycle ---------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  Serial.println("Right Pod Serial Started");

  pinMode(XSHUT_A, OUTPUT);
  pinMode(XSHUT_B, OUTPUT);
  digitalWrite(XSHUT_A, LOW);
  digitalWrite(XSHUT_B, LOW);
  delay(100);

  while (!Wire.begin(I2C_SDA, I2C_SCL))
  {
    Serial.println("Wire Begin Failed");
  }
  Wire.setClock(400000);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  uint8_t ch = 0;
  wifi_second_chan_t second;
  esp_wifi_get_channel(&ch, &second);
  Serial.print("WiFi channel: ");
  Serial.println(ch);
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_send_cb(onEspNowSent);
  esp_now_register_recv_cb(onEspNowRecv);
  addEspNowPeer(baseMac);

  // Bring up sensors one at a time, moving the first off the default address.
  setupSensor(sensorA, XSHUT_A, ADDR_A, SENSOR_A_FREQ, SENSOR_A_ID);
  setupSensor(sensorB, XSHUT_B, ADDR_B, SENSOR_B_FREQ, SENSOR_B_ID);
  startTime = millis();
}

void loop()
{
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 2)
    return; // 100 Hz poll is plenty
  lastPoll = millis();

  trySendSensor(sensorA, resultsA, SENSOR_A_ID);
  trySendSensor(sensorB, resultsB, SENSOR_B_ID);
}
