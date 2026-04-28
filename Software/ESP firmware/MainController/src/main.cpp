#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <string.h>
#include <math.h>
#include "driver/i2s.h"
#include "sounds.h"
#include "SensorAndPoint.h"
#include "runtime_defaults.h"
#include "led_frame.h"

// Minimal base receiver:
// - ESP-NOW only
// - Receives sensor packets
// - Converts latest packet to world-frame XYZ points in loop()
// - Streams compact point CSV over Serial for a PC viewer

const uint8_t ESPNOW_CHANNEL = RuntimeDefaults::kEspNowChannel;
const uint32_t SERIAL_BAUD = RuntimeDefaults::kSerialBaud;

const int NUM_SENSORS = 8;
const uint8_t SENSOR_IDS[NUM_SENSORS] = {1, 2, 3, 4, 5, 6, 7, 8};

// Sensor pose configuration guide:
// - x_off_mm / y_off_mm / z_off_mm are the sensor origin in WORLD coordinates (mm).
// - WORLD and SENSOR axis convention is right-handed: +X forward, +Y left, +Z up.
// - alpha_deg / beta_deg / gamma_deg are Euler angles in DEGREES.
//   alpha = rotation about X, beta = rotation about Y, gamma = rotation about Z.
// - Rotation matrix composition in SensorAndPoint.h is:
//     R = Rz(gamma) * Ry(beta) * Rx(alpha)
//   and points are transformed as p_world = R * p_sensor + t.
//   This means rotations are applied in this order about FIXED/world-aligned axes:
//     1) X by alpha, 2) Y by beta, 3) Z by gamma
//   (extrinsic XYZ order, not intrinsic rotating-local-axis order).
// - Positive angles follow the right-hand rule about each axis.
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
        {1, 215.0f, 240.0f, 547.0f, 0.0f, 0.0f, 30.0f},
        {2, 200.0f, 277.0f, 547.0f, 0.0f, 0.0f, 0.0f},
        {3, 200.0f, -277.0f, 547.0f, 0.0f, 0.0f, 0.0f},
        {4, 215.0f, -240.0f, 547.0f, 0.0f, 0.0f, -30.0f},
        {5, -520.0f, -95.0f, 1200.0f, 0.0f, 20.0f, -90.0f},
        {6, -597.0f, -27.0f, 1200.0f, 0.0f, 20.0f, -150.0f},
        {7, -597.0f, 27.0f, 1200.0f, 0.0f, 20.0f, 150.0f},
        {8, -520.0f, 95.0f, 1200.0f, 0.0f, 20.0f, 90.0f},
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
const uint8_t MSG_NAV_COMMAND      = 0xB6; // CaregiverApp -> MainController
const uint8_t MSG_PRESENTATION_POLAR_GRID = 0xB7; // MainController -> presentation receiver (broadcast)

const uint8_t NAV_STOP     = 0;
const uint8_t NAV_FORWARD  = 1;
const uint8_t NAV_BACKWARD = 2;
const uint8_t NAV_LEFT     = 3;
const uint8_t NAV_RIGHT    = 4;
const uint8_t NAV_SPEEDUP  = 5;
const uint8_t NAV_SLOWDOWN = 6;
const uint8_t NAV_SPEAK    = 7;

struct NavigationCommandPacket
{
  uint8_t msg_type;  // MSG_NAV_COMMAND
  uint8_t action;    // NAV_* constant
} __attribute__((packed));   // 2 bytes

const uint8_t COMPONENT_MAIN_CONTROLLER = 1;

const uint8_t LED_RENDER_MODE_SECTOR_FILL = 0;
const uint8_t LED_RENDER_MODE_RADAR = 1;
const uint8_t LED_BRIGHTNESS_MAX = (uint8_t)RuntimeDefaults::kMaxBrightness;
const float NO_OBSTACLE_MM = 1.0e9f;

// ── Audio (I2S + MAX98357A) ───────────────────────────────────────────────────
#define I2S_BCK_IO  27
#define I2S_WS_IO   26
#define I2S_DO_IO   25
#define I2S_PORT    I2S_NUM_0

bool obstacleAudioEnabled = false;
int  currentVolume        = 255;  // navigation command audio volume
int  obstacleVolume       = 200;  // obstacle feedback audio volume (tonal + verbal)

// Tonal chirp settings — defaults match CaregiverApp defaults.
uint16_t toneRedPitchHz    = 1200;
uint16_t toneRedTempoMs    =  150;
uint16_t toneOrangePitchHz =  800;
uint16_t toneOrangeTempoMs =  500;
uint16_t toneYellowPitchHz =  400;
uint16_t toneYellowTempoMs = 1000;

static constexpr uint16_t CHIRP_DURATION_MS  = 80;
static constexpr uint32_t VERBAL_COOLDOWN_MS = 3000;
uint8_t  obstacleAudioMode        = 0;    // 0=tonal, 1=verbal
float    latestClosestMmByZone[8] = {};   // updated every proximity period
uint32_t lastChirpMs              = 0;
uint32_t lastVerbalMs             = 0;
bool     navAudioPlaying          = false;

// Sent by CaregiverApp when the caregiver changes settings in the browser UI.
struct ConfigPacket
{
  uint8_t  msg_type;           // MSG_CONFIG
  uint8_t  zone_mode;          // 4, 6, or 8
  uint8_t  brightness;         // 0-255 (capped at 50%)
  uint16_t red_threshold_mm;
  uint16_t orange_threshold_mm;
  uint16_t yellow_threshold_mm;
  uint8_t  audio_enabled;      // obstacle-audio feedback enable
  uint8_t  visual_enabled;
  uint8_t  render_mode;
  uint8_t  active_sectors;     // bitmask — bit N = zone N enabled
  uint8_t  volume;             // 0–255 audio volume
  uint16_t red_pitch_hz;       // tonal chirp frequency for red zone (Hz)
  uint16_t red_tempo_ms;       // tonal chirp interval for red zone (ms)
  uint16_t orange_pitch_hz;
  uint16_t orange_tempo_ms;
  uint16_t yellow_pitch_hz;
  uint16_t yellow_tempo_ms;
  uint8_t  audio_mode;         // 0=tonal, 1=verbal
  uint8_t  obstacle_volume;    // 0-255 volume for obstacle audio only
} __attribute__((packed));     // 28 bytes

struct ComponentStatusPacket
{
  uint8_t msg_type;          // MSG_COMPONENT_STATUS
  uint8_t component_id;      // COMPONENT_MAIN_CONTROLLER
  uint8_t sensor_seen_mask;  // bit N = sensor (N+1) is currently alive
  uint8_t flags;             // bit0=visualEnabled
} __attribute__((packed));   // 4 bytes

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// Update this MAC to match the laptop-side PresentationGridReceiver ESP32.
static uint8_t PRESENTATION_RECEIVER_MAC[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

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
float proximityRedThresholdMm = RuntimeDefaults::kDefaultRedThresholdMm;
float proximityOrangeThresholdMm = RuntimeDefaults::kDefaultOrangeThresholdMm;
float proximityYellowThresholdMm = RuntimeDefaults::kDefaultYellowThresholdMm;
float proximityFloorZMm = RuntimeDefaults::kDefaultObstacleFloorZMm;
float proximityCeilingZMm = RuntimeDefaults::kDefaultObstacleCeilingZMm;
uint8_t proximityLedRenderMode = RuntimeDefaults::kDefaultLedRenderMode;

const int POLAR_NUM_RINGS = RuntimeDefaults::kPolarNumRings;
const int POLAR_MAX_ZONES = RuntimeDefaults::kPolarMaxZones;
const int POLAR_BIN_COUNT = POLAR_NUM_RINGS * POLAR_MAX_ZONES;
static constexpr int PRESENTATION_OWNER_PACKED_BYTES = (POLAR_BIN_COUNT + 1) / 2;

struct PresentationPolarGridPacket
{
  uint8_t  msg_type;    // MSG_PRESENTATION_POLAR_GRID
  uint8_t  ring_count;  // polar ring count
  uint8_t  zone_count;  // polar zone count
  uint8_t  reserved;
  uint16_t frame_seq;
  uint32_t source_ms;
  uint8_t  packed_owner[PRESENTATION_OWNER_PACKED_BYTES];
} __attribute__((packed));
uint8_t polarConfRise = RuntimeDefaults::kPolarConfRise;
uint8_t polarConfDecay = RuntimeDefaults::kPolarConfDecay;
uint8_t polarEnterThreshold = RuntimeDefaults::kPolarEnterThreshold;
uint8_t polarExitThreshold = RuntimeDefaults::kPolarExitThreshold;
uint8_t polarConfidence[POLAR_BIN_COUNT] = {0};
uint8_t polarOccupied[POLAR_BIN_COUNT] = {0};
uint8_t polarOwner[POLAR_BIN_COUNT] = {0};
bool    presentationGridEnabled = true;
uint16_t presentationGridFrameSeq = 0;
uint32_t lastPresentationGridBroadcastMs = 0;
static constexpr uint32_t kPresentationGridPeriodMs = 100;

uint32_t lastProximityBroadcastMs = 0;
uint32_t proximityPeriodMs = RuntimeDefaults::kProximityPeriodMs;
uint8_t  broadcastPeerAdded = 0;
uint8_t  presentationPeerAdded = 0;
char serialCmdBuffer[200] = {0};
uint16_t serialCmdLen = 0;
bool debugSensorCsvEnabled = true;     // P/E/S lines
bool debugDetectionCsvEnabled = false;  // DL lines

// Physical ring geometry (outer to inner).
static constexpr int kPhysicalRingCount = 6;
static constexpr int kRenderableRingCount = 5; // excludes center ring
static constexpr uint8_t kRingStarts[kPhysicalRingCount] = {0, 32, 56, 72, 84, 92};
static constexpr uint8_t kRingCounts[kPhysicalRingCount] = {32, 24, 16, 12, 8, 1};

// Forward declarations for proximity helpers (defined before setup())
void applyProximityThresholds(float redMaxMm, float orangeMaxMm, float yellowMaxMm);
void updateAndBroadcastLedFrame();
void sendClearLedFrame();
void invalidateStaleSensors();
void clearPolarGrid();
void accumulatePolarHits(uint8_t hitMask[POLAR_BIN_COUNT], uint8_t hitOwner[POLAR_BIN_COUNT]);
void updatePolarGrid(const uint8_t hitMask[POLAR_BIN_COUNT], const uint8_t hitOwner[POLAR_BIN_COUNT]);
void computeZoneProximityFromPolar(float closestMmByZone[8]);
int polarRingIndexFromDistance(float distMm);
float polarRepresentativeDistanceForRing(int ringIndex);
void emitDetectionDebugFrame(const float closestMmByZone[8]);
void serviceProximityAudio();
void broadcastPresentationPolarGrid(uint32_t sourceMs);
static int proximityZoneIndex(float angleDeg, int numZones);
static int clampInt(int v, int lo, int hi);
static uint8_t colorForDistanceMm(float distMm);
static int displayRingIndexFromDistance(float distMm);
static float ledAngleDegForIndex(int ringIdx, int idxInRing);
static int ledIndexFromAngleDeg(int ringIdx, float angleDeg);
static void setLedByRingAngle(LedFrame_t &frame, int ringIdx, float angleDeg, uint8_t color);
static void packPolarOwnerNibbles(uint8_t packed[PRESENTATION_OWNER_PACKED_BYTES]);
static bool pointPassesOccupancyHeightWindow(const Point &p);
void buildSectorFrame(const float closestMmByZone[8], LedFrame_t &frame);
void buildRadarFrame(LedFrame_t &frame);
void buildLedFrame(const float closestMmByZone[8], LedFrame_t &frame);
void broadcastLedFrame(const LedFrame_t &frame);
void emitTuningConfigLine();
void handleSerialCommand(char *line);
void serviceSerialCommands();
void broadcastComponentStatus();
void setupI2S();
static void playClipRaw(const unsigned char* data, unsigned int len, unsigned int rate);
void playNavigationAudio(const unsigned char* audioData, unsigned int dataLen, unsigned int sampleRate);
void playObstacleAudio(const unsigned char* audioData, unsigned int dataLen, unsigned int sampleRate);
void handleStop();
void handleGoForward();
void handleTurnLeft();
void handleTurnRight();
void handleSpeedUp();
void handleSlowDown();
void handleBackUp();
void handleSpeak();

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
  if (!debugSensorCsvEnabled)
  {
    return;
  }

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
  if (debugSensorCsvEnabled && canSerial)
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
  if (incomingData == nullptr || len <= 0)
  {
    return;
  }

  const uint8_t msgType = incomingData[0];

  if (msgType == MSG_NAV_COMMAND)
  {
    if (len < (int)sizeof(NavigationCommandPacket))
    {
      Serial.printf("[Nav] Dropped short packet len=%d\n", len);
      return;
    }

    NavigationCommandPacket navPkt;
    memcpy(&navPkt, incomingData, sizeof(navPkt));
    Serial.printf("[Nav] action=%u from %02X:%02X:%02X:%02X:%02X:%02X\n",
                  (unsigned int)navPkt.action,
                  mac ? mac[0] : 0, mac ? mac[1] : 0, mac ? mac[2] : 0,
                  mac ? mac[3] : 0, mac ? mac[4] : 0, mac ? mac[5] : 0);

    switch (navPkt.action)
    {
      case NAV_STOP:     handleStop();      break;
      case NAV_FORWARD:  handleGoForward(); break;
      case NAV_BACKWARD: handleBackUp();    break;
      case NAV_LEFT:     handleTurnLeft();  break;
      case NAV_RIGHT:    handleTurnRight(); break;
      case NAV_SPEEDUP:  handleSpeedUp();   break;
      case NAV_SLOWDOWN: handleSlowDown();  break;
      case NAV_SPEAK:    handleSpeak();     break;
      default:
        Serial.printf("[Nav] Unknown action=%u\n", (unsigned int)navPkt.action);
        break;
    }
    return;
  }

  // ConfigPacket from CaregiverApp — update local proximity settings
  if (msgType == MSG_CONFIG)
  {
    if (len < (int)sizeof(ConfigPacket))
    {
      Serial.printf("[Config] Dropped short config packet len=%d expected=%u\n",
                    len, (unsigned int)sizeof(ConfigPacket));
      return;
    }

    ConfigPacket cfg;
    memcpy(&cfg, incomingData, sizeof(cfg));
    if (cfg.zone_mode == 4 || cfg.zone_mode == 6 || cfg.zone_mode == 8)
      proximityNumZones = cfg.zone_mode;
    proximityBrightness    = clampInt((int)cfg.brightness, 0, (int)LED_BRIGHTNESS_MAX);
    obstacleAudioEnabled   = (cfg.audio_enabled != 0);
    proximityVisualEnabled = (cfg.visual_enabled != 0);
    proximityLedRenderMode = (uint8_t)clampInt((int)cfg.render_mode, 0, 1);
    proximityActiveSectors = cfg.active_sectors;
    currentVolume          = cfg.volume;
    toneRedPitchHz         = cfg.red_pitch_hz;
    toneRedTempoMs         = cfg.red_tempo_ms;
    toneOrangePitchHz      = cfg.orange_pitch_hz;
    toneOrangeTempoMs      = cfg.orange_tempo_ms;
    toneYellowPitchHz      = cfg.yellow_pitch_hz;
    toneYellowTempoMs      = cfg.yellow_tempo_ms;
    obstacleAudioMode      = cfg.audio_mode;
    obstacleVolume         = cfg.obstacle_volume;
    applyProximityThresholds(cfg.red_threshold_mm, cfg.orange_threshold_mm, cfg.yellow_threshold_mm);
    if (!proximityVisualEnabled)
    {
      clearPolarGrid();
      sendClearLedFrame();
    }
    Serial.printf("[Config] zones=%d bright=%d obstacle_audio=%s visual=%s led_mode=%d sectors=0x%02X vol=%d\n",
                  proximityNumZones, proximityBrightness,
                  obstacleAudioEnabled ? "on" : "off",
                  proximityVisualEnabled ? "on" : "off", (int)proximityLedRenderMode,
                  proximityActiveSectors, currentVolume);
    return;
  }

  if (len != (int)sizeof(SensorPacket))
  {
    if (len <= 16)
    {
      Serial.printf("[ESP-NOW] Ignored packet type=0x%02X len=%d\n",
                    (unsigned int)msgType, len);
    }
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
    polarOwner[i] = 0;
  }
}

static int polarBinIndex(int ringIndex, int zoneIndex)
{
  return (ringIndex * POLAR_MAX_ZONES) + zoneIndex;
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

void accumulatePolarHits(uint8_t hitMask[POLAR_BIN_COUNT], uint8_t hitOwner[POLAR_BIN_COUNT])
{
  float nearestMm[POLAR_BIN_COUNT];
  for (int i = 0; i < POLAR_BIN_COUNT; i++)
  {
    hitMask[i] = 0;
    hitOwner[i] = 0;
    nearestMm[i] = NO_OBSTACLE_MM;
  }

  for (int s = 0; s < NUM_SENSORS; s++)
  {
    if (!sensorSeen[s]) continue;
    for (int cell = 0; cell < 16; cell++)
    {
      const Point &p = sensors[s].latestWorldPoints[cell];
      if (!p.isValid) continue;
      if (!pointPassesOccupancyHeightWindow(p)) continue;

      float dist = sqrtf(p.worldX * p.worldX + p.worldY * p.worldY);
      if (dist > RuntimeDefaults::kMaxObstacleRangeMm) continue;

      float angleDeg = atan2f(-p.worldY, p.worldX) * (180.0f / 3.14159265f);
      int zone = proximityZoneIndex(angleDeg, POLAR_MAX_ZONES);
      if (zone < 0 || zone >= POLAR_MAX_ZONES) continue;

      int ring = polarRingIndexFromDistance(dist);
      int idx = polarBinIndex(ring, zone);
      if (idx < 0 || idx >= POLAR_BIN_COUNT) continue;

      if (!hitMask[idx] || dist < nearestMm[idx])
      {
        hitMask[idx] = 1;
        hitOwner[idx] = (uint8_t)p.sensorId;
        nearestMm[idx] = dist;
      }
    }
  }
}

static bool pointPassesOccupancyHeightWindow(const Point &p)
{
  return p.worldZ >= proximityFloorZMm && p.worldZ <= proximityCeilingZMm;
}

void updatePolarGrid(const uint8_t hitMask[POLAR_BIN_COUNT], const uint8_t hitOwner[POLAR_BIN_COUNT])
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
    if (hitMask[i] && hitOwner[i] != 0) polarOwner[i] = hitOwner[i];

    if (!polarOccupied[i] && conf >= polarEnterThreshold) polarOccupied[i] = 1;
    else if (polarOccupied[i] && conf <= polarExitThreshold)
    {
      polarOccupied[i] = 0;
      polarOwner[i] = 0;
    }
  }
}

void applyProximityThresholds(float redMaxMm, float orangeMaxMm, float yellowMaxMm)
{
  if (redMaxMm <= 0.0f) return;
  if (orangeMaxMm <= redMaxMm) return;
  if (yellowMaxMm <= orangeMaxMm) return;
  proximityRedThresholdMm = redMaxMm;
  proximityOrangeThresholdMm = orangeMaxMm;
  proximityYellowThresholdMm = yellowMaxMm;
}

// Returns 0-based zone index matching the CaregiverApp UI sector convention.
static int proximityZoneIndex(float angleDeg, int numZones)
{
  if (numZones <= 0) return -1;

  if (numZones == 6)
  {
    if (angleDeg >= -30.0f && angleDeg < 30.0f) return 0;    // AHEAD
    if (angleDeg >= 30.0f && angleDeg <= 90.0f) return 1;    // TOP_RIGHT
    if (angleDeg > 90.0f && angleDeg < 150.0f) return 2;     // BOTTOM_RIGHT
    if (angleDeg < -150.0f || angleDeg >= 150.0f) return 3;  // BEHIND
    if (angleDeg >= -150.0f && angleDeg < -90.0f) return 4;  // BOTTOM_LEFT
    return 5;                                                 // TOP_LEFT
  }

  const float step = 360.0f / (float)numZones;
  int idx = (int)floorf((angleDeg + (0.5f * step)) / step);
  idx %= numZones;
  if (idx < 0) idx += numZones;
  return idx;
}

static float normalizeAngleDeg(float angleDeg)
{
  while (angleDeg >= 180.0f) angleDeg -= 360.0f;
  while (angleDeg < -180.0f) angleDeg += 360.0f;
  return angleDeg;
}

static uint8_t colorForDistanceMm(float distMm)
{
  if (distMm <= 0.0f || distMm >= NO_OBSTACLE_MM) return LED_COLOR_OFF;
  if (distMm <= proximityRedThresholdMm) return LED_COLOR_RED;
  if (distMm <= proximityOrangeThresholdMm) return LED_COLOR_ORANGE;
  if (distMm <= proximityYellowThresholdMm) return LED_COLOR_YELLOW;
  return LED_COLOR_GREEN;
}

static int displayRingIndexFromDistance(float distMm)
{
  const float maxRange = RuntimeDefaults::kMaxObstacleRangeMm;
  float clamped = distMm;
  if (clamped < 0.0f) clamped = 0.0f;
  if (clamped > maxRange) clamped = maxRange;
  float normalized = clamped / maxRange;
  int outwardIdx = (int)floorf(normalized * (float)kRenderableRingCount);
  if (outwardIdx >= kRenderableRingCount) outwardIdx = kRenderableRingCount - 1;
  int ring = (kRenderableRingCount - 1) - outwardIdx;
  if (ring < 0) ring = 0;
  if (ring >= kRenderableRingCount) ring = kRenderableRingCount - 1;
  return ring;
}

static float ledAngleDegForIndex(int ringIdx, int idxInRing)
{
  if (ringIdx < 0 || ringIdx >= kPhysicalRingCount) return 0.0f;
  int count = (int)kRingCounts[ringIdx];
  if (count <= 0) return 0.0f;
  float step = 360.0f / (float)count;
  float angle = 180.0f + ((float)idxInRing * step);
  return normalizeAngleDeg(angle);
}

static int ledIndexFromAngleDeg(int ringIdx, float angleDeg)
{
  if (ringIdx < 0 || ringIdx >= kPhysicalRingCount) return -1;
  int count = (int)kRingCounts[ringIdx];
  if (count <= 0) return -1;
  float unsignedAngle = fmodf(angleDeg + 360.0f, 360.0f);
  float cwFromBottom = fmodf((unsignedAngle - 180.0f) + 360.0f, 360.0f);
  int idxInRing = (int)lroundf((cwFromBottom / 360.0f) * (float)count) % count;
  if (idxInRing < 0) idxInRing += count;
  return (int)kRingStarts[ringIdx] + idxInRing;
}

static int ledColorPriority(uint8_t color)
{
  switch (color)
  {
    case LED_COLOR_RED: return 5;
    case LED_COLOR_ORANGE: return 4;
    case LED_COLOR_YELLOW: return 3;
    case LED_COLOR_GREEN: return 2;
    case LED_COLOR_BLUE: return 1;
    default: return 0;
  }
}

static void setLedByRingAngle(LedFrame_t &frame, int ringIdx, float angleDeg, uint8_t color)
{
  int ledIdx = ledIndexFromAngleDeg(ringIdx, angleDeg);
  if (ledIdx < 0 || ledIdx >= 93) return;
  uint8_t existing = frame.leds[ledIdx];
  if (ledColorPriority(color) >= ledColorPriority(existing))
  {
    frame.leds[ledIdx] = color;
  }
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
  for (int z = 0; z < 8; z++) closestMmByZone[z] = NO_OBSTACLE_MM;

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
  if (!debugDetectionCsvEnabled)
  {
    return;
  }

  if (!serialTelemetryWritable(96))
  {
    return;
  }

  Serial.print("DL,2,");
  Serial.print((int)proximityNumZones);
  Serial.print(",");
  Serial.print((int)proximityActiveSectors);
  Serial.print(",");
  Serial.print((int)proximityRedThresholdMm);
  Serial.print(",");
  Serial.print((int)proximityOrangeThresholdMm);
  Serial.print(",");
  Serial.print((int)proximityYellowThresholdMm);
  for (int z = 0; z < 8; z++)
  {
    Serial.print(",");
    if (closestMmByZone[z] >= NO_OBSTACLE_MM) Serial.print(-1);
    else Serial.print((int)closestMmByZone[z]);
  }
  Serial.println();

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

static void packPolarOwnerNibbles(uint8_t packed[PRESENTATION_OWNER_PACKED_BYTES])
{
  for (int i = 0; i < PRESENTATION_OWNER_PACKED_BYTES; i++) packed[i] = 0;

  for (int idx = 0; idx < POLAR_BIN_COUNT; idx++)
  {
    uint8_t owner = polarOccupied[idx] ? (uint8_t)(polarOwner[idx] & 0x0F) : 0;
    int byteIndex = idx / 2;
    if ((idx & 1) == 0) packed[byteIndex] = owner;
    else packed[byteIndex] |= (uint8_t)(owner << 4);
  }
}

void broadcastPresentationPolarGrid(uint32_t sourceMs)
{
  if (!presentationGridEnabled || !presentationPeerAdded) return;

  uint32_t nowMs = millis();
  if ((nowMs - lastPresentationGridBroadcastMs) < kPresentationGridPeriodMs) return;
  lastPresentationGridBroadcastMs = nowMs;

  PresentationPolarGridPacket pkt = {};
  pkt.msg_type = MSG_PRESENTATION_POLAR_GRID;
  pkt.ring_count = (uint8_t)POLAR_NUM_RINGS;
  pkt.zone_count = (uint8_t)POLAR_MAX_ZONES;
  pkt.frame_seq = presentationGridFrameSeq++;
  pkt.source_ms = sourceMs;
  packPolarOwnerNibbles(pkt.packed_owner);
  esp_now_send(PRESENTATION_RECEIVER_MAC, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

void updateAndBroadcastLedFrame()
{
  // Always update the polar grid and zone distances — audio needs this even when
  // visual is disabled.
  uint8_t hitMask[POLAR_BIN_COUNT];
  uint8_t hitOwner[POLAR_BIN_COUNT];
  accumulatePolarHits(hitMask, hitOwner);
  updatePolarGrid(hitMask, hitOwner);
  computeZoneProximityFromPolar(latestClosestMmByZone);
  emitDetectionDebugFrame(latestClosestMmByZone);
  broadcastPresentationPolarGrid(millis());

  if (proximityVisualEnabled)
  {
    LedFrame_t frame = {};
    buildLedFrame(latestClosestMmByZone, frame);
    broadcastLedFrame(frame);
  }
}

void sendClearLedFrame()
{
  LedFrame_t frame = {};
  frame.brightness = (uint8_t)proximityBrightness;
  broadcastLedFrame(frame);
}

void buildSectorFrame(const float closestMmByZone[8], LedFrame_t &frame)
{
  uint8_t zoneColors[8] = {0};
  for (int z = 0; z < proximityNumZones && z < 8; z++)
  {
    bool enabled = ((proximityActiveSectors & (1 << z)) != 0);
    if (!enabled)
    {
      zoneColors[z] = LED_COLOR_OFF;
      continue;
    }

    float dist = closestMmByZone[z];
    if (dist <= proximityYellowThresholdMm)
    {
      zoneColors[z] = colorForDistanceMm(dist);
    }
    else
    {
      zoneColors[z] = LED_COLOR_GREEN;
    }
  }

  for (int ring = 0; ring < kRenderableRingCount; ring++)
  {
    int count = (int)kRingCounts[ring];
    int start = (int)kRingStarts[ring];
    for (int idx = 0; idx < count; idx++)
    {
      float angleDeg = ledAngleDegForIndex(ring, idx);
      int zone = proximityZoneIndex(angleDeg, proximityNumZones);
      if (zone < 0 || zone >= 8) continue;
      frame.leds[start + idx] = zoneColors[zone];
    }
  }
}

void buildRadarFrame(LedFrame_t &frame)
{
  for (int ring = 0; ring < POLAR_NUM_RINGS; ring++)
  {
    float distMm = polarRepresentativeDistanceForRing(ring);
    uint8_t color = colorForDistanceMm(distMm);
    if (color == LED_COLOR_OFF) continue;
    int displayRing = displayRingIndexFromDistance(distMm);

    for (int zone = 0; zone < POLAR_MAX_ZONES; zone++)
    {
      int idx = polarBinIndex(ring, zone);
      if (idx < 0 || idx >= POLAR_BIN_COUNT) continue;
      if (!polarOccupied[idx]) continue;

      float angleDeg = polarZoneCenterAngleDeg(zone);
      int uiZone = proximityZoneIndex(angleDeg, proximityNumZones);
      if (uiZone < 0 || uiZone >= 8) continue;
      if ((proximityActiveSectors & (1 << uiZone)) == 0) continue;
      setLedByRingAngle(frame, displayRing, angleDeg, color);
    }
  }
}

void buildLedFrame(const float closestMmByZone[8], LedFrame_t &frame)
{
  frame.brightness = (uint8_t)proximityBrightness;
  if (proximityLedRenderMode == LED_RENDER_MODE_RADAR) buildRadarFrame(frame);
  else buildSectorFrame(closestMmByZone, frame);
  frame.leds[kRingStarts[kPhysicalRingCount - 1]] = LED_COLOR_BLUE;
}

void broadcastLedFrame(const LedFrame_t &frame)
{
  esp_now_send(BROADCAST_MAC, (const uint8_t *)&frame, sizeof(frame));
}

static int clampInt(int v, int lo, int hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void emitTuningConfigLine()
{
  int redMm = (int)proximityRedThresholdMm;
  int orangeMm = (int)proximityOrangeThresholdMm;
  int yellowMm = (int)proximityYellowThresholdMm;
  Serial.print("CFG,zones,");
  Serial.print((int)proximityNumZones);
  Serial.print(",bright,");
  Serial.print((int)proximityBrightness);
  Serial.print(",visual,");
  Serial.print(proximityVisualEnabled ? 1 : 0);
  Serial.print(",sectors_mask,");
  Serial.print((int)proximityActiveSectors);
  Serial.print(",red_mm,");
  Serial.print(redMm);
  Serial.print(",orange_mm,");
  Serial.print(orangeMm);
  Serial.print(",yellow_mm,");
  Serial.print(yellowMm);
  Serial.print(",floor_z_mm,");
  Serial.print((int)proximityFloorZMm);
  Serial.print(",ceiling_z_mm,");
  Serial.print((int)proximityCeilingZMm);
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
  Serial.print((int)polarExitThreshold);
  Serial.print(",present_grid,");
  Serial.print(presentationGridEnabled ? 1 : 0);
  Serial.print(",dbg_sensor_csv,");
  Serial.print(debugSensorCsvEnabled ? 1 : 0);
  Serial.print(",dbg_detection_csv,");
  Serial.println(debugDetectionCsvEnabled ? 1 : 0);
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
    Serial.println("HELP,GET|SET,<key>,<value> (debug keys: dbg_sensor_csv, dbg_detection_csv, present_grid; occupancy keys: floor_z_mm, ceiling_z_mm)");
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
  if (strcmp(key, "zones") == 0)
  {
    int zones = (int)raw;
    if (zones != 4 && zones != 6 && zones != 8) updated = false;
    else proximityNumZones = zones;
  }
  else if (strcmp(key, "bright") == 0)
  {
    proximityBrightness = clampInt((int)raw, 0, (int)LED_BRIGHTNESS_MAX);
  }
  else if (strcmp(key, "visual") == 0)
  {
    proximityVisualEnabled = (raw != 0);
    if (!proximityVisualEnabled)
    {
      clearPolarGrid();
      sendClearLedFrame();
    }
  }
  else if (strcmp(key, "sectors_mask") == 0)
  {
    proximityActiveSectors = (uint8_t)(raw & 0xFF);
  }
  else if (strcmp(key, "red_mm") == 0)
  {
    int orangeMm = (int)proximityOrangeThresholdMm;
    int yellowMm = (int)proximityYellowThresholdMm;
    int redMm = clampInt((int)raw, 50, 2990);
    if (redMm >= orangeMm) redMm = orangeMm - 10;
    redMm = clampInt(redMm, 50, 2990);
    applyProximityThresholds((float)redMm, (float)orangeMm, (float)yellowMm);
  }
  else if (strcmp(key, "orange_mm") == 0)
  {
    int redMm = (int)proximityRedThresholdMm;
    int yellowMm = (int)proximityYellowThresholdMm;
    int orangeMm = clampInt((int)raw, 60, 2995);
    if (orangeMm <= redMm) orangeMm = redMm + 10;
    if (orangeMm >= yellowMm) orangeMm = yellowMm - 10;
    orangeMm = clampInt(orangeMm, 60, 2995);
    applyProximityThresholds((float)redMm, (float)orangeMm, (float)yellowMm);
  }
  else if (strcmp(key, "yellow_mm") == 0)
  {
    int redMm = (int)proximityRedThresholdMm;
    int orangeMm = (int)proximityOrangeThresholdMm;
    int yellowMm = clampInt((int)raw, 70, 3000);
    if (yellowMm <= orangeMm) yellowMm = orangeMm + 10;
    yellowMm = clampInt(yellowMm, 70, 3000);
    applyProximityThresholds((float)redMm, (float)orangeMm, (float)yellowMm);
  }
  else if (strcmp(key, "floor_z_mm") == 0)
  {
    int floorMm = clampInt((int)raw, -2000, 4000);
    int ceilingMm = (int)proximityCeilingZMm;
    if (floorMm >= ceilingMm) floorMm = ceilingMm - 10;
    proximityFloorZMm = (float)floorMm;
  }
  else if (strcmp(key, "ceiling_z_mm") == 0)
  {
    int floorMm = (int)proximityFloorZMm;
    int ceilingMm = clampInt((int)raw, -1990, 5000);
    if (ceilingMm <= floorMm) ceilingMm = floorMm + 10;
    proximityCeilingZMm = (float)ceilingMm;
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
  else if (strcmp(key, "present_grid") == 0)
  {
    presentationGridEnabled = (raw != 0);
    if (!presentationGridEnabled) lastPresentationGridBroadcastMs = 0;
    raw = presentationGridEnabled ? 1 : 0;
  }
  else if (strcmp(key, "dbg_sensor_csv") == 0)
  {
    debugSensorCsvEnabled = (raw != 0);
    raw = debugSensorCsvEnabled ? 1 : 0;
  }
  else if (strcmp(key, "dbg_detection_csv") == 0)
  {
    debugDetectionCsvEnabled = (raw != 0);
    raw = debugDetectionCsvEnabled ? 1 : 0;
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

// =========================================================
// AUDIO ENGINE (I2S + MAX98357A)
// =========================================================

void setupI2S()
{
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = 16000,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 8,
    .dma_buf_len          = 128,
    .use_apll             = false,
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);

  i2s_pin_config_t pins = {
    .bck_io_num   = I2S_BCK_IO,
    .ws_io_num    = I2S_WS_IO,
    .data_out_num = I2S_DO_IO,
    .data_in_num  = I2S_PIN_NO_CHANGE,
  };
  i2s_set_pin(I2S_PORT, &pins);
}

// Plays a single PCM clip over I2S without touching navAudioPlaying or lastChirpMs.
// Used to chain multiple clips (e.g. direction + proximity word) in one verbal alert.
static void playClipRaw(const unsigned char* data, unsigned int len, unsigned int rate)
{
  if (len == 0 || obstacleVolume <= 0) return;
  i2s_set_sample_rates(I2S_PORT, rate);
  float   volFactor = (float)obstacleVolume / 255.0f;
  int16_t buf[128];
  int     bufIdx = 0;
  for (unsigned int i = 0; i + 1 < len; i += 2)
  {
    uint8_t lo  = pgm_read_byte(&data[i]);
    uint8_t hi  = pgm_read_byte(&data[i + 1]);
    int16_t smp = (int16_t)((int16_t)((hi << 8) | lo) * volFactor);
    buf[bufIdx++] = smp;
    buf[bufIdx++] = smp;
    if (bufIdx >= 128)
    {
      size_t written;
      i2s_write(I2S_PORT, buf, sizeof(buf), &written, portMAX_DELAY);
      bufIdx = 0;
    }
  }
  if (bufIdx > 0)
  {
    size_t written;
    i2s_write(I2S_PORT, buf, (size_t)bufIdx * sizeof(int16_t), &written, portMAX_DELAY);
  }
  i2s_zero_dma_buffer(I2S_PORT);
}

// Maps a zone index to a direction audio clip based on the active sector count.
// Sector ordering: 4=['N','E','S','W'], 6=['N','NE','SE','S','SW','NW'], 8=['N','NE','E','SE','S','SW','W','NW']
static void directionAudioForZone(int zoneIdx, int numZones,
                                   const unsigned char** outData,
                                   unsigned int* outLen, unsigned int* outRate)
{
  // 0=ahead, 1=right, 2=behind, 3=left
  static const int dir4[4] = {0, 1, 2, 3};
  static const int dir6[6] = {0, 1, 1, 2, 3, 3};
  static const int dir8[8] = {0, 0, 1, 1, 2, 3, 3, 0};
  const int* dirs = (numZones == 8) ? dir8 : (numZones == 4) ? dir4 : dir6;
  int dir = (zoneIdx >= 0 && zoneIdx < numZones) ? dirs[zoneIdx] : 0;
  switch (dir)
  {
    case 1:  *outData = right_data;  *outLen = right_len;  *outRate = right_rate;  break;
    case 2:  *outData = behind_data; *outLen = behind_len; *outRate = behind_rate; break;
    case 3:  *outData = left_data;   *outLen = left_len;   *outRate = left_rate;   break;
    default: *outData = ahead_data;  *outLen = ahead_len;  *outRate = ahead_rate;  break;
  }
}

void playNavigationAudio(const unsigned char* audioData, unsigned int dataLen, unsigned int sampleRate)
{
  navAudioPlaying = true;
  Serial.printf("[NavAudio] received len=%u rate=%u vol=%d obstacle_audio=%d\n",
                dataLen, sampleRate, currentVolume, obstacleAudioEnabled ? 1 : 0);
  if (dataLen == 0)
  {
    Serial.println("[NavAudio] Skipped empty clip");
    navAudioPlaying = false;
    return;
  }
  if (currentVolume <= 0)
  {
    Serial.println("[NavAudio] Skipped because volume is 0");
    navAudioPlaying = false;
    return;
  }

  i2s_set_sample_rates(I2S_PORT, sampleRate);

  size_t  bytesWritten = 0;
  size_t  totalBytesQueued = 0;
  size_t  totalBytesWritten = 0;
  uint32_t writeCalls = 0;
  esp_err_t firstWriteErr = ESP_OK;
  int16_t buf[128];
  int     bufIdx    = 0;
  float   volFactor = (float)currentVolume / 255.0f;

  for (unsigned int i = 0; i + 1 < dataLen; i += 2)
  {
    uint8_t lo  = pgm_read_byte(&audioData[i]);
    uint8_t hi  = pgm_read_byte(&audioData[i + 1]);
    int16_t smp = (int16_t)((hi << 8) | lo);
    smp         = (int16_t)(smp * volFactor);
    buf[bufIdx++] = smp; // Left
    buf[bufIdx++] = smp; // Right
    if (bufIdx >= 128)
    {
      writeCalls++;
      totalBytesQueued += sizeof(buf);
      esp_err_t err = i2s_write(I2S_PORT, buf, sizeof(buf), &bytesWritten, portMAX_DELAY);
      if (err != ESP_OK && firstWriteErr == ESP_OK) firstWriteErr = err;
      totalBytesWritten += bytesWritten;
      bufIdx = 0;
    }
  }
  if (bufIdx > 0)
  {
    const size_t tailBytes = (size_t)bufIdx * sizeof(int16_t);
    writeCalls++;
    totalBytesQueued += tailBytes;
    esp_err_t err = i2s_write(I2S_PORT, buf, tailBytes, &bytesWritten, portMAX_DELAY);
    if (err != ESP_OK && firstWriteErr == ESP_OK) firstWriteErr = err;
    totalBytesWritten += bytesWritten;
  }

  if (firstWriteErr != ESP_OK || totalBytesQueued != totalBytesWritten)
  {
    Serial.printf("[NavAudio] i2s_write issue err=0x%x queued=%u written=%u calls=%u\n",
                  (unsigned int)firstWriteErr,
                  (unsigned int)totalBytesQueued,
                  (unsigned int)totalBytesWritten,
                  (unsigned int)writeCalls);
  }
  else
  {
    Serial.printf("[NavAudio] i2s_write ok queued=%u written=%u calls=%u\n",
                  (unsigned int)totalBytesQueued,
                  (unsigned int)totalBytesWritten,
                  (unsigned int)writeCalls);
  }
  i2s_zero_dma_buffer(I2S_PORT);
  navAudioPlaying = false;
  lastChirpMs = millis();  // brief gap before chirps resume after nav audio
}

void playObstacleAudio(const unsigned char* audioData, unsigned int dataLen, unsigned int sampleRate)
{
  if (!obstacleAudioEnabled)
  {
    return;
  }
  playNavigationAudio(audioData, dataLen, sampleRate);
}

void playToneChirp(uint16_t freqHz, uint16_t durationMs)
{
  if (!obstacleAudioEnabled || obstacleVolume <= 0) return;

  i2s_set_sample_rates(I2S_PORT, 16000);

  const uint32_t sampleRate   = 16000;
  const uint32_t totalSamples = (sampleRate * (uint32_t)durationMs) / 1000;
  const float    volFactor    = (float)obstacleVolume / 255.0f;
  const float    twoPiF       = 2.0f * 3.14159265f * (float)freqHz;

  int16_t  buf[128];
  int      bufIdx      = 0;
  size_t   bytesWritten = 0;

  for (uint32_t i = 0; i < totalSamples; i++)
  {
    float   t   = (float)i / (float)sampleRate;
    int16_t smp = (int16_t)(sinf(twoPiF * t) * 32767.0f * volFactor);
    buf[bufIdx++] = smp;  // Left
    buf[bufIdx++] = smp;  // Right
    if (bufIdx >= 128)
    {
      i2s_write(I2S_PORT, buf, sizeof(buf), &bytesWritten, portMAX_DELAY);
      bufIdx = 0;
    }
  }
  if (bufIdx > 0)
    i2s_write(I2S_PORT, buf, (size_t)bufIdx * sizeof(int16_t), &bytesWritten, portMAX_DELAY);

  i2s_zero_dma_buffer(I2S_PORT);
}

void serviceProximityAudio()
{
  if (!obstacleAudioEnabled || navAudioPlaying) return;

  // Find the closest obstacle and which zone it's in.
  float closestMm   = NO_OBSTACLE_MM;
  int   closestZone = 0;
  for (int z = 0; z < proximityNumZones; z++)
  {
    if (!(proximityActiveSectors & (1 << z))) continue;
    if (latestClosestMmByZone[z] < closestMm)
    {
      closestMm   = latestClosestMmByZone[z];
      closestZone = z;
    }
  }

  if (closestMm > proximityYellowThresholdMm)
  {
    // No obstacle within alert range — reset timers so the next detection
    // starts a fresh cycle rather than firing immediately.
    lastChirpMs  = millis();
    lastVerbalMs = millis();
    return;
  }

  if (obstacleAudioMode == 1)
  {
    // ── Verbal mode ──────────────────────────────────────────────────────────
    uint32_t nowMs = millis();
    if ((nowMs - lastVerbalMs) < VERBAL_COOLDOWN_MS) return;
    lastVerbalMs = nowMs;

    const unsigned char* dirData; unsigned int dirLen, dirRate;
    directionAudioForZone(closestZone, proximityNumZones, &dirData, &dirLen, &dirRate);

    // Proximity word: "very close" for red, "close" for orange, nothing for yellow.
    const unsigned char* proxData = nullptr;
    unsigned int proxLen = 0, proxRate = 16000;
    if (closestMm <= proximityRedThresholdMm)
    {
      proxData = very_close_data; proxLen = very_close_len; proxRate = very_close_rate;
    }
    else if (closestMm <= proximityOrangeThresholdMm)
    {
      proxData = close_data; proxLen = close_len; proxRate = close_rate;
    }

    navAudioPlaying = true;
    if (proxData) playClipRaw(proxData, proxLen, proxRate);
    playClipRaw(dirData, dirLen, dirRate);
    navAudioPlaying = false;
    lastChirpMs = millis();
  }
  else
  {
    // ── Tonal mode ───────────────────────────────────────────────────────────
    uint16_t pitchHz = 0;
    uint16_t tempoMs = 0;
    if (closestMm <= proximityRedThresholdMm)
    {
      pitchHz = toneRedPitchHz;
      tempoMs = toneRedTempoMs;
    }
    else if (closestMm <= proximityOrangeThresholdMm)
    {
      pitchHz = toneOrangePitchHz;
      tempoMs = toneOrangeTempoMs;
    }
    else
    {
      pitchHz = toneYellowPitchHz;
      tempoMs = toneYellowTempoMs;
    }

    uint32_t nowMs = millis();
    if ((nowMs - lastChirpMs) >= (uint32_t)tempoMs)
    {
      lastChirpMs = nowMs;
      playToneChirp(pitchHz, CHIRP_DURATION_MS);
    }
  }
}

// =========================================================
// NAVIGATION AUDIO ACTIONS
// =========================================================

void handleStop()      { Serial.println("Nav: STOP");      playNavigationAudio(stop_data,       stop_len,       stop_rate);       }
void handleGoForward() { Serial.println("Nav: Forward");   playNavigationAudio(go_forward_data, go_forward_len, go_forward_rate); }
void handleTurnLeft()  { Serial.println("Nav: Left");      playNavigationAudio(turn_left_data,  turn_left_len,  turn_left_rate);  }
void handleTurnRight() { Serial.println("Nav: Right");     playNavigationAudio(turn_right_data, turn_right_len, turn_right_rate); }
void handleSpeedUp()   { Serial.println("Nav: Speed Up");  playNavigationAudio(speed_up_data,   speed_up_len,   speed_up_rate);   }
void handleSlowDown()  { Serial.println("Nav: Slow Down"); playNavigationAudio(slow_down_data,  slow_down_len,  slow_down_rate);  }
void handleBackUp()    { Serial.println("Nav: Back Up");   playNavigationAudio(back_up_data,    back_up_len,    back_up_rate);    }
void handleSpeak()     { Serial.println("Nav: Speak");     playNavigationAudio(stay_on_line_data, stay_on_line_len, stay_on_line_rate); }

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println("Base receiver minimal ESP-NOW point converter");

  for (int i = 0; i < 8; i++) latestClosestMmByZone[i] = NO_OBSTACLE_MM;

  setupI2S();
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

  Serial.println("[Config] detection pipeline=polar-only");
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

  // Dedicated peer for the laptop-side presentation grid receiver.
  {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, PRESENTATION_RECEIVER_MAC, 6);
    peer.ifidx  = WIFI_IF_STA;
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) == ESP_OK)
    {
      presentationPeerAdded = 1;
      Serial.printf("Presentation receiver peer registered: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    PRESENTATION_RECEIVER_MAC[0], PRESENTATION_RECEIVER_MAC[1], PRESENTATION_RECEIVER_MAC[2],
                    PRESENTATION_RECEIVER_MAC[3], PRESENTATION_RECEIVER_MAC[4], PRESENTATION_RECEIVER_MAC[5]);
    }
    else
    {
      Serial.println("Presentation receiver peer registration failed");
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

  // Update polar grid and zone distances at configured rate.
  // Runs regardless of visual enable so audio always has fresh data.
  if (broadcastPeerAdded)
  {
    uint32_t nowMs = millis();
    if ((nowMs - lastProximityBroadcastMs) >= proximityPeriodMs)
    {
      lastProximityBroadcastMs = nowMs;
      updateAndBroadcastLedFrame();
    }
  }

  serviceProximityAudio();
  delay(1);
}

