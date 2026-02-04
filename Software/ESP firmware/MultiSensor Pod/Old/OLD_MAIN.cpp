#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <SparkFun_VL53L5CX_Library.h>

constexpr uint8_t XSHUT_A = 19; // TODO: set your pin
constexpr uint8_t XSHUT_B = 18; // TODO: set your pin
constexpr uint8_t I2C_SDA = 21; // TODO: set your pin (ESP32 default is often 21)
constexpr uint8_t I2C_SCL = 22; // TODO: set your pin (ESP32 default is often 22)

constexpr uint8_t SENSOR_A_ID = 1; //Change for Right Pod
constexpr uint8_t SENSOR_B_ID = 2;

// Use different 7-bit I2C addresses to avoid conflicts. First sensor is moved off 0x29 (default).
constexpr uint8_t ADDR_A = 0x2A;
constexpr uint8_t ADDR_B = 0x29;

volatile bool pollingEnabled = false;
uint32_t pollUntilMs = 0;
uint32_t startTime = 0;
uint16_t lastSeq = 0;

const uint8_t baseMac[6] = {0x5C , 0x01 , 0x3B , 0x88 , 0x04 , 0x58}; 

struct SensorPacket
{
  uint8_t sensor_id;
  uint32_t timestamp_ms;
  uint16_t distance_mm[16];
  uint8_t target_status[16];
  uint8_t nb_targets[16];
} __attribute__((packed));

enum ControlCmd : uint8_t
{
  CMD_NOP = 0,
  CMD_START = 1,
  CMD_STOP = 2,
  CMD_POLL_FOR_MS = 3
};

struct CommandPacket
{
  uint8_t version = 1;
  uint8_t cmd; // ControlCmd
  uint16_t seq;
  uint32_t value; // e.g., duration_ms for CMD_POLL_FOR_MS
} __attribute__((packed));

SparkFun_VL53L5CX sensorA;
SparkFun_VL53L5CX sensorB;
VL53L5CX_ResultsData resultsA;
VL53L5CX_ResultsData resultsB;

void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status)
{
  Serial.print("ESP-NOW send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// Data Recieve Callback
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
  CommandPacket cmdPkt;
  memcpy(&cmdPkt, incomingData, sizeof(CommandPacket));

  if (cmdPkt.seq == lastSeq)
    return; // drop duplicates
  lastSeq = cmdPkt.seq;
  
  switch (cmdPkt.cmd)
  {
    case CMD_START:
      pollingEnabled = true;
    break;

    case CMD_STOP:
      pollingEnabled = false;
    break;

    case CMD_POLL_FOR_MS:
      pollingEnabled = true;
      startTime = millis();
      pollUntilMs = startTime + (cmdPkt.value * 1000); // *1000 to convert to ms
      Serial.printf("Poll for %u ms\n", pollUntilMs);
      break;
    
    default:
      break;
  }
}


bool addEspNowPeer(const uint8_t *mac)
{
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.ifidx = WIFI_IF_STA;
  peer.channel = 0; // match current channel
  peer.encrypt = false;
  bool paired = esp_now_add_peer(&peer);
  if(paired){
    Serial.println("Peer Added");
  }
  return paired;
}


bool bringUpSensor(SparkFun_VL53L5CX & sensor, uint8_t xshutPin, uint8_t newAddr, const char *name)
  {
    digitalWrite(xshutPin, HIGH);
    delay(50);

    if (!sensor.begin())
    {
      Serial.print(name);
      Serial.println(" begin() failed");
      return false;
    }

    if (newAddr != (DEFAULT_I2C_ADDR >> 1) && !sensor.setAddress(newAddr))
    {
      Serial.print(name);
      Serial.println(" setAddress() failed");
      return false;
    }

    // sensor.setIntegrationTime(20); // ms
    sensor.setRangingFrequency(1);
    sensor.setResolution(16);

    if (!sensor.startRanging())
    {
      Serial.print(name);
      Serial.println(" startRanging() failed");
      return false;
    }

    Serial.print(name);
    Serial.print(" ready at 0x");
    Serial.println(newAddr, HEX);
    return true;
  }

  void sendSynchronizedIfReady(uint32_t startTime)
  {
    // Only send when both sensors have a fresh frame so timestamps align
    if (!sensorA.isDataReady() || !sensorB.isDataReady())
      return;

    const uint32_t timeSinceStart = millis() - startTime;

    //Send Sensor 1 Data
    if (sensorA.getRangingData(&resultsA))
    {
      SensorPacket pkt{};
      pkt.sensor_id = SENSOR_A_ID;
      pkt.timestamp_ms = timeSinceStart;
      for (uint8_t i = 0; i < 16; ++i)
      {
        pkt.distance_mm[i] = resultsA.distance_mm[i];
        pkt.target_status[i] = resultsA.target_status[i];
        pkt.nb_targets[i] = resultsA.nb_target_detected[i];
      }
      esp_err_t res = esp_now_send(baseMac, (uint8_t *)&pkt, sizeof(pkt));
      if (res != ESP_OK)
      {
        Serial.print("esp_now_send error (A): ");
        Serial.println(res);
      }
    }
    else
    {
      Serial.println("getRangingData() failed for sensor A");
    }

    //Send Sensor 2 Data
    if (sensorB.getRangingData(&resultsB))
    {
      SensorPacket pkt{};
      pkt.sensor_id = SENSOR_B_ID;
      pkt.timestamp_ms = timeSinceStart;
      for (uint8_t i = 0; i < 16; ++i)
      {
        pkt.distance_mm[i] = resultsB.distance_mm[i];
        pkt.target_status[i] = resultsB.target_status[i];
        pkt.nb_targets[i] = resultsB.nb_target_detected[i];
      }
      esp_err_t res = esp_now_send(baseMac, (uint8_t *)&pkt, sizeof(pkt));
      if (res != ESP_OK)
      {
        Serial.print("esp_now_send error (B): ");
        Serial.println(res);
      }
    }
    else
    {
      Serial.println("getRangingData() failed for sensor B");
    }
  }

  void setup()
  {
    Serial.begin(115200);
    Serial.println("Serial Started");

    pinMode(XSHUT_A, OUTPUT);
    pinMode(XSHUT_B, OUTPUT);
    digitalWrite(XSHUT_A, LOW);
    digitalWrite(XSHUT_B, LOW);
    delay(100);

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK)
    {
      Serial.println("ESP-NOW init failed");
      return;
    }
    esp_now_register_send_cb(onEspNowSent);
    esp_now_register_recv_cb(OnDataRecv);
    addEspNowPeer(baseMac);

    // Bring up sensors one at a time, moving the first off the default address.
    bringUpSensor(sensorA, XSHUT_A, ADDR_A, "Sensor A");
    bringUpSensor(sensorB, XSHUT_B, ADDR_B, "Sensor B");
  }

  void loop()
  {
    if(pollUntilMs > uint32_t(millis())){
      pollingEnabled = false;
    }

    if(pollingEnabled){
      sendSynchronizedIfReady(startTime);
    }

  }
