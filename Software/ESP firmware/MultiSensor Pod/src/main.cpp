#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <SparkFun_VL53L5CX_Library.h>

// --- Pin & device configuration ------------------------------------------------
constexpr uint8_t XSHUT_A = 18;
constexpr uint8_t XSHUT_B = 19;
constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;

constexpr uint8_t SENSOR_A_ID = 5; // Change for Right Pod
constexpr uint8_t SENSOR_B_ID = 6;

constexpr uint8_t ADDR_A = 0x23;
constexpr uint8_t ADDR_B = 0x44;

const uint8_t baseMac[6] = {0x88, 0x56, 0xA6, 0x70, 0x09, 0xDC};
uint32_t startTime;

// --- Types ---------------------------------------------------------------------
struct SensorPacket
{
    uint8_t sensor_id;
    uint32_t timestamp_ms;
    uint16_t distance_mm[16];
    uint8_t target_status[16];
    uint8_t nb_targets[16];
} __attribute__((packed));

// --- State ---------------------------------------------------------------------
SparkFun_VL53L5CX sensorA;
SparkFun_VL53L5CX sensorB;
VL53L5CX_ResultsData resultsA;
VL53L5CX_ResultsData resultsB;

// --- ESP-NOW helpers -----------------------------------------------------------
void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status)
{
    Serial.print("ESP-NOW send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

bool addEspNowPeer(const uint8_t *mac)
{
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac, 6);
    peer.ifidx = WIFI_IF_STA;
    peer.channel = 0; // match current channel
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
bool setupSensor(SparkFun_VL53L5CX &sensor, uint8_t xshutPin, uint8_t newAddr, const char *name)
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
    Serial.println(sensor.getAddress(), HEX);
    return true;
}

// --- Data path -----------------------------------------------------------------
void sendSynchronizedIfReady(uint32_t startTime)
{
    // Only send when both sensors have a fresh frame so timestamps align
    if (!sensorA.isDataReady() || !sensorB.isDataReady())
        return;

    const uint32_t timeSinceStart = millis() - startTime;

    // Send Sensor 1 Data
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
            Serial.println(pkt.distance_mm[i]);
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

    // Send Sensor 2 Data
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

// --- Arduino lifecycle ---------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    Serial.println("Serial Started");

    pinMode(XSHUT_A, OUTPUT);
    pinMode(XSHUT_B, OUTPUT);
    digitalWrite(XSHUT_A, LOW);
    digitalWrite(XSHUT_B, LOW);
    delay(100);

    while (!Wire.begin(I2C_SDA, I2C_SCL))
    {
        Serial.println("Wire Begin Failed");
    }
    Wire.setClock(100000);
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("ESP-NOW init failed");
        return;
    }
    esp_now_register_send_cb(onEspNowSent);
    addEspNowPeer(baseMac);

    // Bring up sensors one at a time, moving the first off the default address.
    setupSensor(sensorA, XSHUT_A, ADDR_A, "Sensor A");
    setupSensor(sensorB, XSHUT_B, ADDR_B, "Sensor B");
    startTime = millis();
}

void loop()
{
    static uint32_t lastPoll = 0;
    if (millis() - lastPoll < 10)
        return; // 100 Hz poll is plenty
    lastPoll = millis();

    sendSynchronizedIfReady(startTime);
}
