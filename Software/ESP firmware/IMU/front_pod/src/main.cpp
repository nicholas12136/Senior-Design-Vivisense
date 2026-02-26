/*
  =====================================================================
  FRONT POD - Sender Node
  =====================================================================
  This ESP32 acts as a sender node. It:
    1. Reads its own IMU (LSM6DS33) and maintains a stable orientation
       estimate using a complementary filter (same as tower)
    2. Packages its pitch and roll into a small struct
    3. Sends that struct to the tower pod via ESP-NOW at ~20Hz

  IMPORTANT: Before flashing, set POD_ID below.
    Left front pod  -> #define POD_ID 1
    Right front pod -> #define POD_ID 2

  Also set the TOWER_MAC_ADDRESS to match what the tower printed
  on its Serial Monitor when it was first powered on.

  PIN SETUP (MinIMU-9 v5 <-> ESP32)
  -----------------------------------------------
  VIN   -> 3.3V
  GND   -> GND
  SDA   -> GPIO 21
  SCL   -> GPIO 22
  =====================================================================
*/

#include <Arduino.h>
#include <Wire.h>
#include <LSM6.h>
#include <esp_now.h>
#include <WiFi.h>
#include <math.h>

// ---------------------------------------------------------------------
// CONFIGURATION — SET THESE BEFORE FLASHING
// ---------------------------------------------------------------------

// Set to 1 for left front pod, 2 for right front pod
#define POD_ID 1

// Replace these with the MAC address printed by the tower pod on startup
// Format: {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
uint8_t TOWER_MAC_ADDRESS[] = {0x14, 0x33, 0x5C, 0x48, 0x0D, 0x68};

// Complementary filter weight — keep this the same as the tower
#define COMP_FILTER_ALPHA 0.98

// Accelerometer sensitivity for LSM6DS33 at default +/-2g setting
#define ACCEL_SENSITIVITY 0.061f

// Gyroscope sensitivity for LSM6DS33 at default +/-245 dps setting
#define GYRO_SENSITIVITY 8.75f

// ---------------------------------------------------------------------
// DATA STRUCTURES
// Must be identical to the struct in the tower pod sketch
// ---------------------------------------------------------------------
typedef struct {
  float pitch;
  float roll;
  uint8_t podID;
} PodOrientationPacket;

// ---------------------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------------------
LSM6 imu;

float podPitch = 0.0;
float podRoll  = 0.0;

unsigned long lastTime = 0;

esp_now_peer_info_t towerPeer;

// Flag set by the send callback so we can log success/failure
bool lastSendSuccess = false;

// ---------------------------------------------------------------------
// ESP-NOW SEND CALLBACK
// Called automatically after each send attempt, confirms delivery
// ---------------------------------------------------------------------
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  lastSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  if (!lastSendSuccess) {
    Serial.println("WARNING: Packet send failed. Is tower powered on?");
  }
}

// ---------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Wire.begin();

  // --- IMU Init ---
  if (!imu.init()) {
    Serial.println("Failed to detect IMU! Check wiring.");
    while (1);
  }
  imu.enableDefault();
  Serial.printf("Front Pod %d IMU initialized.\n", POD_ID);

  // --- ESP-NOW Init ---
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("This pod's MAC Address: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    while (1);
  }

  // Register the send callback
  esp_now_register_send_cb(onDataSent);

  // Register the tower as a peer (recipient of our data)
  memset(&towerPeer, 0, sizeof(towerPeer));
  memcpy(towerPeer.peer_addr, TOWER_MAC_ADDRESS, 6);
  towerPeer.channel = 0;   // use current WiFi channel
  towerPeer.encrypt = false; // no encryption for now

  if (esp_now_add_peer(&towerPeer) != ESP_OK) {
    Serial.println("Failed to add tower as peer!");
    while (1);
  }

  Serial.printf("Front Pod %d ready. Sending to tower...\n\n", POD_ID);

  lastTime = millis();
}

// ---------------------------------------------------------------------
// COMPLEMENTARY FILTER HELPERS
// (identical to tower — both pods must compute orientation the same way)
// ---------------------------------------------------------------------
float calcAccelPitch(float ax, float ay, float az) {
  return atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / M_PI;
}

float calcAccelRoll(float ay, float az) {
  return atan2(ay, az) * 180.0 / M_PI;
}

// ---------------------------------------------------------------------
// MAIN LOOP
// ---------------------------------------------------------------------
void loop() {
  // --- Read IMU ---
  imu.read();

  // Convert raw values to real units
  float ax = imu.a.x * ACCEL_SENSITIVITY / 1000.0f;
  float ay = imu.a.y * ACCEL_SENSITIVITY / 1000.0f;
  float az = imu.a.z * ACCEL_SENSITIVITY / 1000.0f;

  float gx = imu.g.x * GYRO_SENSITIVITY / 1000.0f;
  float gy = imu.g.y * GYRO_SENSITIVITY / 1000.0f;

  // Compute dt
  unsigned long now = millis();
  float dt = (now - lastTime) / 1000.0f;
  lastTime = now;

  if (dt <= 0 || dt > 1.0) {
    delay(10);
    return;
  }

  // Accel-based angle estimates
  float accelPitch = calcAccelPitch(ax, ay, az);
  float accelRoll  = calcAccelRoll(ay, az);

  // Complementary filter
  podPitch = COMP_FILTER_ALPHA * (podPitch + gx * dt) + (1.0 - COMP_FILTER_ALPHA) * accelPitch;
  podRoll  = COMP_FILTER_ALPHA * (podRoll  + gy * dt) + (1.0 - COMP_FILTER_ALPHA) * accelRoll;

  // --- Build and send packet ---
  PodOrientationPacket packet;
  packet.pitch = podPitch;
  packet.roll  = podRoll;
  packet.podID = POD_ID;

  esp_now_send(TOWER_MAC_ADDRESS, (uint8_t *)&packet, sizeof(packet));

  // Print local orientation for debugging
  Serial.printf("Pod %d -> Pitch: %.2f  Roll: %.2f  |  Last send: %s\n",
    POD_ID, podPitch, podRoll, lastSendSuccess ? "OK" : "FAILED");

  delay(50); // ~20Hz, matches tower update rate
}
