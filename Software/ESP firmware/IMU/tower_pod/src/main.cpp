/*
  =====================================================================
  TOWER POD - Reference Hub (Updated)
  =====================================================================
  Changes from previous version:
    - All three pods report angles RELATIVE to their calibration baseline
    - Tower will read near 0/0 after calibration, drift is corrected
    - Front pods report their offset from their own baseline, minus
      the tower's current offset — giving true relative displacement
    - Clean serial output for monitoring

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
// CONFIGURATION
// ---------------------------------------------------------------------

// Knock threshold in degrees of relative displacement from baseline
#define KNOCK_THRESHOLD_DEG 15.0

// Complementary filter weight — 0.98 trusts gyro 98%, accel 2%
#define COMP_FILTER_ALPHA 0.98

// Stale data timeout — if no packet received in this time, mark disconnected
#define STALE_DATA_TIMEOUT_MS 1000

// LSM6DS33 sensitivity constants
#define ACCEL_SENSITIVITY 0.061f   // mg per LSB at +/-2g
#define GYRO_SENSITIVITY  8.75f    // mdps per LSB at +/-245dps

// ---------------------------------------------------------------------
// DATA STRUCTURES
// ---------------------------------------------------------------------

// Packet sent by each front pod — must match front pod sketch exactly
typedef struct {
  float pitch;
  float roll;
  uint8_t podID;
} PodOrientationPacket;

// State tracked for each front pod
typedef struct {
  float pitch;          // raw complementary filter output from that pod
  float roll;
  bool knocked;
  bool connected;
  unsigned long lastReceived;
} FrontPodState;

// ---------------------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------------------

LSM6 imu;

// Raw complementary filter outputs for tower
float towerPitch = 0.0;
float towerRoll  = 0.0;

// Baselines recorded at calibration time
float towerBasePitch = 0.0;
float towerBaseRoll  = 0.0;
float pod1BasePitch  = 0.0;
float pod1BaseRoll   = 0.0;
float pod2BasePitch  = 0.0;
float pod2BaseRoll   = 0.0;

bool baselineSet = false;

FrontPodState frontPods[2];

unsigned long lastTime = 0;

unsigned long lastPrintTime = 0;

float gyroBiasX = 0.0;
float gyroBiasY = 0.0;

// ---------------------------------------------------------------------
// MATH EXPLANATION
// ---------------------------------------------------------------------
/*
  HOW WE CONVERT ACCELERATION TO ANGLES:

  The accelerometer measures gravity (9.8 m/s²) split across X, Y, Z axes.
  When still, we can treat the gravity vector as a known reference and
  compute the tilt angle using trigonometry.

  PITCH (forward/backward tilt):
    pitch = atan2(-ax, sqrt(ay² + az²))
    - ax is how much gravity is pulling in the forward/back direction
    - sqrt(ay² + az²) is the magnitude of gravity in the other two axes
    - atan2 gives the angle between them

  ROLL (side to side tilt):
    roll = atan2(ay, az)
    - ay is gravity in the left/right direction
    - az is gravity in the up/down direction
    - when flat: ay≈0, az≈1g → roll≈0°
    - when tilted 90°: ay≈1g, az≈0g → roll≈90°

  Both results come out in radians, multiplied by 180/π for degrees.

  GYROSCOPE INTEGRATION:
    angle += gyro_dps * dt
    The gyro gives us degrees/second directly, so multiplying by elapsed
    time (dt in seconds) gives degrees rotated since last reading.

  COMPLEMENTARY FILTER:
    angle = 0.98 * (angle + gyro*dt) + 0.02 * accel_angle
    Gyro is accurate short-term but drifts. Accel is stable long-term
    but noisy. Blending 98/2 gives stable, responsive angle estimate.

  BASELINE SUBTRACTION:
    relative_angle = current_angle - baseline_angle
    After calibration we record each pod's angle as its "zero point".
    Subtracting that baseline means the tower always reads near 0/0,
    and front pods report how far they've moved from their starting position.
    The tower's relative angle is then subtracted from each front pod's
    relative angle to cancel out any shared wheelchair motion.
*/

// ---------------------------------------------------------------------
// ESP-NOW RECEIVE CALLBACK
// ---------------------------------------------------------------------
void onDataReceived(const uint8_t *mac_addr, const uint8_t *data, int len) {
  if (len != sizeof(PodOrientationPacket)) return;

  PodOrientationPacket packet;
  memcpy(&packet, data, sizeof(packet));

  int idx = packet.podID - 1;
  if (idx < 0 || idx > 1) return;

  frontPods[idx].pitch        = packet.pitch;
  frontPods[idx].roll         = packet.roll;
  frontPods[idx].connected    = true;
  frontPods[idx].lastReceived = millis();

  // Knock detection only runs after calibration
  if (baselineSet) {
    // Compute this pod's relative angle (offset from its own baseline)
    float podBasePitch = (packet.podID == 1) ? pod1BasePitch : pod2BasePitch;
    float podBaseRoll  = (packet.podID == 1) ? pod1BaseRoll  : pod2BaseRoll;

    float podRelPitch = packet.pitch - podBasePitch;
    float podRelRoll  = packet.roll  - podBaseRoll;

    // Compute tower's current relative angle (how much tower has drifted)
    float towerRelPitch = towerPitch - towerBasePitch;
    float towerRelRoll  = towerRoll  - towerBaseRoll;

    // True relative displacement = pod offset minus tower offset
    // This cancels out any shared wheelchair motion
    float diffPitch = fabs(podRelPitch - towerRelPitch);
    float diffRoll  = fabs(podRelRoll  - towerRelRoll);

    frontPods[idx].knocked = (diffPitch > KNOCK_THRESHOLD_DEG ||
                              diffRoll  > KNOCK_THRESHOLD_DEG);
  }
}

// ---------------------------------------------------------------------
// COMPLEMENTARY FILTER HELPERS
// ---------------------------------------------------------------------
float calcAccelPitch(float ax, float ay, float az) {
  // atan2(-ax, sqrt(ay²+az²)) gives forward/back tilt in radians → degrees
  return atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / M_PI;
}

float calcAccelRoll(float ay, float az) {
  // atan2(ay, az) gives side tilt in radians → degrees
  return atan2(ay, az) * 180.0 / M_PI;
}

// ---------------------------------------------------------------------
// CALIBRATION
// Run once after sensors warm up — records each pod's starting position
// ---------------------------------------------------------------------
void calibrate() {
  Serial.println("Calibrating... hold all pods still.");

  // Wait for front pods to connect and send a few packets
  delay(3000);

  // Record tower baseline
  towerBasePitch = towerPitch;
  towerBaseRoll  = towerRoll;

  // Record front pod baselines
  pod1BasePitch = frontPods[0].pitch;
  pod1BaseRoll  = frontPods[0].roll;
  pod2BasePitch = frontPods[1].pitch;
  pod2BaseRoll  = frontPods[1].roll;

  baselineSet = true;

  Serial.println("Calibration complete! Monitoring started.");
  Serial.println("Tower should now read near 0.00 / 0.00");
  Serial.println("============================================");
}

void calibrateGyroBias() {
  Serial.println("Calibrating gyro bias... keep tower still.");
  float sumX = 0, sumY = 0;
  int samples = 200;
  for (int i = 0; i < samples; i++) {
    imu.read();
    sumX += imu.g.x * GYRO_SENSITIVITY / 1000.0f;
    sumY += imu.g.y * GYRO_SENSITIVITY / 1000.0f;
    delay(5);
  }
  gyroBiasX = sumX / samples;
  gyroBiasY = sumY / samples;
  Serial.printf("Gyro bias: X=%.4f  Y=%.4f dps\n", gyroBiasX, gyroBiasY);
}

// ---------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  // --- IMU init ---
  if (!imu.init()) {
    Serial.println("ERROR: IMU not found. Check wiring.");
    while (1);
  }
  imu.enableDefault();
  Serial.println("Tower IMU initialized.");

  calibrateGyroBias();

  // --- ESP-NOW init ---
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.print("Tower MAC Address: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init failed.");
    while (1);
  }

  esp_now_register_recv_cb(onDataReceived);
  Serial.println("ESP-NOW ready. Warming up sensors...");

  // --- Init front pod state ---
  for (int i = 0; i < 2; i++) {
    frontPods[i] = {0.0, 0.0, false, false, 0};
  }

  lastTime = millis();

  // Warm up the complementary filter for ~1 second so angles settle
  // before we record the calibration baseline
  for (int i = 0; i < 50; i++) {
    imu.read();
    float ax = imu.a.x * ACCEL_SENSITIVITY / 1000.0f;
    float ay = imu.a.y * ACCEL_SENSITIVITY / 1000.0f;
    float az = imu.a.z * ACCEL_SENSITIVITY / 1000.0f;
    float gx = (imu.g.x * GYRO_SENSITIVITY / 1000.0f) - gyroBiasX;
    float gy = (imu.g.y * GYRO_SENSITIVITY / 1000.0f) - gyroBiasY;

    unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0f;
    lastTime = now;

    if (dt > 0 && dt < 1.0) {
      towerPitch = COMP_FILTER_ALPHA * (towerPitch + gx * dt) +
                   (1.0 - COMP_FILTER_ALPHA) * calcAccelPitch(ax, ay, az);
      towerRoll  = COMP_FILTER_ALPHA * (towerRoll  + gy * dt) +
                   (1.0 - COMP_FILTER_ALPHA) * calcAccelRoll(ay, az);
    }
    delay(20);
  }

  calibrate();
}

// ---------------------------------------------------------------------
// MAIN LOOP
// ---------------------------------------------------------------------
void loop() {
  imu.read();

  // Convert raw readings to real units
  float ax = imu.a.x * ACCEL_SENSITIVITY / 1000.0f;
  float ay = imu.a.y * ACCEL_SENSITIVITY / 1000.0f;
  float az = imu.a.z * ACCEL_SENSITIVITY / 1000.0f;
  float gx = (imu.g.x * GYRO_SENSITIVITY / 1000.0f) - gyroBiasX;
  float gy = (imu.g.y * GYRO_SENSITIVITY / 1000.0f) - gyroBiasY;

  unsigned long now = millis();
  float dt = (now - lastTime) / 1000.0f;
  lastTime = now;

  if (dt <= 0 || dt > 1.0) {
    delay(10);
    return;
  }

  // Update tower complementary filter
  towerPitch = COMP_FILTER_ALPHA * (towerPitch + gx * dt) +
               (1.0 - COMP_FILTER_ALPHA) * calcAccelPitch(ax, ay, az);
  towerRoll  = COMP_FILTER_ALPHA * (towerRoll  + gy * dt) +
               (1.0 - COMP_FILTER_ALPHA) * calcAccelRoll(ay, az);

  // Tower relative angles (should stay near 0/0 after calibration)
  float towerRelPitch = towerPitch - towerBasePitch;
  float towerRelRoll  = towerRoll  - towerBaseRoll;

  // Check for stale front pod data
  for (int i = 0; i < 2; i++) {
    if (frontPods[i].lastReceived > 0 &&
        (millis() - frontPods[i].lastReceived) > STALE_DATA_TIMEOUT_MS) {
      frontPods[i].connected = false;
    }
  }

  // Compute front pod relative displacements for display
  // = (pod offset from its own baseline) - (tower offset from its baseline)
  // This removes any shared motion between all pods
  float pod1RelPitch = (frontPods[0].pitch - pod1BasePitch) - towerRelPitch;
  float pod1RelRoll  = (frontPods[0].roll  - pod1BaseRoll)  - towerRelRoll;
  float pod2RelPitch = (frontPods[1].pitch - pod2BasePitch) - towerRelPitch;
  float pod2RelRoll  = (frontPods[1].roll  - pod2BaseRoll)  - towerRelRoll;

  String pod1Status = !frontPods[0].connected ? "DISCONNECTED"
                    : (frontPods[0].knocked    ? "** KNOCKED **" : "OK");
  String pod2Status = !frontPods[1].connected ? "DISCONNECTED"
                    : (frontPods[1].knocked    ? "** KNOCKED **" : "OK");

// Run filter fast, only print every 500ms
  if (millis() - lastPrintTime >= 500) {
    lastPrintTime = millis();
    Serial.println("============================================");
    Serial.printf("Tower  -> Pitch: %6.2f  Roll: %6.2f\n",
                  towerRelPitch, towerRelRoll);
    Serial.printf("Pod 1  -> Pitch: %6.2f  Roll: %6.2f  | %s\n",
                  pod1RelPitch, pod1RelRoll, pod1Status.c_str());
    Serial.printf("Pod 2  -> Pitch: %6.2f  Roll: %6.2f  | %s\n",
                  pod2RelPitch, pod2RelRoll, pod2Status.c_str());
  }
  delay(10); // Run filter at ~100Hz instead of 2Hz
}