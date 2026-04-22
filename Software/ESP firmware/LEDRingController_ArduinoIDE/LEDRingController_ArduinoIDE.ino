/*
 * ViviSense - LED Ring Controller ESP32
 *
 * Live path:
 *   MainController (ZoneProximityPacket) -> this controller -> NeoPixel ring
 *
 * Preview path:
 *   CaregiverApp sends LedFrame_t directly to this controller for UI preview mode.
 *
 * Config path:
 *   CaregiverApp broadcasts ConfigPacket; this controller uses it for
 *   zone mode, thresholds, brightness, active sectors, and visual enable.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#if __has_include(<esp_idf_version.h>)
#include <esp_idf_version.h>
#endif
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include "led_frame.h"

#define LED_PIN         0
#define NUM_LEDS        93
#define ESPNOW_CHANNEL  1

const uint8_t MSG_CONFIG = 0xB1;
const uint8_t MSG_ZONE_PROXIMITY = 0xB3;
const uint8_t MSG_COMPONENT_STATUS = 0xB4;
const uint8_t MSG_LED_RENDER_FRAME = 0xB6;

const uint8_t COMPONENT_LED_CONTROLLER = 2;
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct __attribute__((packed)) ConfigPacket {
  uint8_t  msg_type;
  uint8_t  zone_mode;          // 4, 6, or 8
  uint8_t  brightness;         // 0-64
  uint16_t red_threshold_mm;
  uint16_t yellow_threshold_mm;
  uint8_t  audio_enabled;
  uint8_t  visual_enabled;
  uint8_t  active_sectors;     // bit N = zone N enabled
};

struct __attribute__((packed)) ZoneProximityPacket {
  uint8_t msg_type;
  uint8_t num_zones;
  float   closest_mm[8];
};

struct __attribute__((packed)) ComponentStatusPacket {
  uint8_t msg_type;
  uint8_t component_id;
  uint8_t sensor_seen_mask;
  uint8_t flags;
};

struct __attribute__((packed)) LedRenderFramePacket {
  uint8_t msg_type;
  uint8_t render_mode;
  uint8_t num_zones;
  uint8_t brightness;
  uint8_t center_color;
  uint8_t ring_zone_colors[6][8];
};

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

int currentBrightness = 64;
int currentZoneMode = 6;
bool visualEnabled = true;
bool currentActiveSectors[8] = {true, true, true, true, true, true, true, true};
uint32_t lastHeartbeatMs = 0;
const uint32_t HEARTBEAT_PERIOD_MS = 1000;

float DIST_RING1 = 300.0f;
float DIST_RING2 = 600.0f;
float DIST_RING3 = 1050.0f;
float DIST_RING4 = 1500.0f;
float DIST_RING5 = 1950.0f;

// Ring 1 (center)
int ring1_any[] = {92, -1};

// 6-zone arrays
int r2_6_AHEAD[] = {84, -1};
int r2_6_TOP_RIGHT[] = {85, -1};
int r2_6_BOTTOM_RIGHT[] = {86, 87, -1};
int r2_6_BEHIND[] = {88, -1};
int r2_6_BOTTOM_LEFT[] = {89, 90, -1};
int r2_6_TOP_LEFT[] = {91, -1};

int r3_6_AHEAD[] = {72, -1};
int r3_6_TOP_RIGHT[] = {73, 74, -1};
int r3_6_BOTTOM_RIGHT[] = {75, 76, 77, -1};
int r3_6_BEHIND[] = {78, -1};
int r3_6_BOTTOM_LEFT[] = {79, 80, -1};
int r3_6_TOP_LEFT[] = {81, 82, 83, -1};

int r4_6_AHEAD[] = {56, 57, 71, -1};
int r4_6_TOP_RIGHT[] = {58, 59, -1};
int r4_6_BOTTOM_RIGHT[] = {60, 61, 62, -1};
int r4_6_BEHIND[] = {63, 64, 65, -1};
int r4_6_BOTTOM_LEFT[] = {66, 67, -1};
int r4_6_TOP_LEFT[] = {68, 69, 70, -1};

int r5_6_AHEAD[] = {32, 33, 55, -1};
int r5_6_TOP_RIGHT[] = {34, 35, 36, 37, -1};
int r5_6_BOTTOM_RIGHT[] = {38, 39, 40, 41, -1};
int r5_6_BEHIND[] = {42, 43, 44, -1};
int r5_6_BOTTOM_LEFT[] = {45, 46, 47, 48, -1};
int r5_6_TOP_LEFT[] = {49, 50, 51, 52, 53, 54, -1};

int r6_6_AHEAD[] = {29, 30, 31, 0, 1, -1};
int r6_6_TOP_RIGHT[] = {2, 3, 4, 5, 6, 7, -1};
int r6_6_BOTTOM_RIGHT[] = {8, 9, 10, 11, 12, -1};
int r6_6_BEHIND[] = {13, 14, 15, 16, -1};
int r6_6_BOTTOM_LEFT[] = {17, 18, 19, 20, 21, 22, -1};
int r6_6_TOP_LEFT[] = {23, 24, 25, 26, 27, 28, -1};

// 4-zone arrays
int r2_4_N[] = {91, 84, -1};
int r2_4_E[] = {85, 86, -1};
int r2_4_S[] = {87, 88, -1};
int r2_4_W[] = {89, 90, -1};

int r3_4_N[] = {82, 83, 72, -1};
int r3_4_E[] = {73, 75, 76, -1};
int r3_4_S[] = {74, 77, 78, -1};
int r3_4_W[] = {79, 80, 81, -1};

int r4_4_N[] = {70, 71, 56, 57, -1};
int r4_4_E[] = {58, 59, 60, 61, -1};
int r4_4_S[] = {62, 63, 64, 65, -1};
int r4_4_W[] = {66, 67, 68, 69, -1};

int r5_4_N[] = {53, 54, 55, 32, 33, 34, -1};
int r5_4_E[] = {35, 36, 37, 38, 39, 40, -1};
int r5_4_S[] = {41, 42, 43, 44, 45, 46, -1};
int r5_4_W[] = {47, 48, 49, 50, 51, 52, -1};

int r6_4_N[] = {28, 29, 30, 31, 0, 1, 2, 3, -1};
int r6_4_E[] = {4, 5, 6, 7, 8, 9, 10, 11, -1};
int r6_4_S[] = {12, 13, 14, 15, 16, 17, 18, 19, -1};
int r6_4_W[] = {20, 21, 22, 23, 24, 25, 26, 27, -1};

// 8-zone arrays
int r2_8_N[] = {91, -1};
int r2_8_NE[] = {84, -1};
int r2_8_E[] = {85, -1};
int r2_8_SE[] = {86, -1};
int r2_8_S[] = {87, -1};
int r2_8_SW[] = {88, -1};
int r2_8_W[] = {89, -1};
int r2_8_NW[] = {90, -1};

int r3_8_N[] = {83, 72, -1};
int r3_8_NE[] = {73, -1};
int r3_8_E[] = {74, 75, -1};
int r3_8_SE[] = {76, -1};
int r3_8_S[] = {77, 78, -1};
int r3_8_SW[] = {79, -1};
int r3_8_W[] = {80, 81, -1};
int r3_8_NW[] = {82, -1};

int r4_8_N[] = {71, 56, -1};
int r4_8_NE[] = {57, 58, -1};
int r4_8_E[] = {59, 60, -1};
int r4_8_SE[] = {61, 62, -1};
int r4_8_S[] = {63, 64, -1};
int r4_8_SW[] = {65, 66, -1};
int r4_8_W[] = {67, 68, -1};
int r4_8_NW[] = {69, 70, -1};

int r5_8_N[] = {54, 55, 32, -1};
int r5_8_NE[] = {33, 34, 35, -1};
int r5_8_E[] = {36, 37, 38, -1};
int r5_8_SE[] = {39, 40, 41, -1};
int r5_8_S[] = {42, 43, 44, -1};
int r5_8_SW[] = {45, 46, 47, -1};
int r5_8_W[] = {48, 49, 50, -1};
int r5_8_NW[] = {51, 52, 53, -1};

int r6_8_N[] = {30, 31, 0, 1, -1};
int r6_8_NE[] = {2, 3, 4, 5, -1};
int r6_8_E[] = {6, 7, 8, 9, -1};
int r6_8_SE[] = {10, 11, 12, 13, -1};
int r6_8_S[] = {14, 15, 16, 17, -1};
int r6_8_SW[] = {18, 19, 20, 21, -1};
int r6_8_W[] = {22, 23, 24, 25, -1};
int r6_8_NW[] = {26, 27, 28, 29, -1};

static const float kZone4Angles[] = {0.0f, 90.0f, 180.0f, -90.0f};
static const float kZone6Angles[] = {0.0f, 60.0f, 120.0f, 180.0f, -120.0f, -60.0f};
static const float kZone8Angles[] = {0.0f, 45.0f, 90.0f, 135.0f, 180.0f, -135.0f, -90.0f, -45.0f};

static uint32_t applyBrightness(uint32_t base, uint8_t brightness) {
  uint8_t r = ((base >> 16) & 0xFF) * brightness / 255;
  uint8_t g = ((base >> 8) & 0xFF) * brightness / 255;
  uint8_t b = ((base) & 0xFF) * brightness / 255;
  return strip.Color(r, g, b);
}

static uint32_t resolveColor(uint8_t code, uint8_t brightness) {
  switch (code) {
    case LED_COLOR_RED:    return applyBrightness(0xFF0000, brightness);
    case LED_COLOR_ORANGE: return applyBrightness(0xFF5500, brightness);
    case LED_COLOR_YELLOW: return applyBrightness(0xFFEE00, brightness);
    default:               return 0;
  }
}

static uint8_t ringToColorCode(int ring) {
  if (ring <= 2) return LED_COLOR_RED;
  if (ring <= 4) return LED_COLOR_ORANGE;
  return LED_COLOR_YELLOW;
}

static void fillZoneInFrame(LedFrame_t& frame, int* ledArray, uint8_t colorCode) {
  for (int i = 0; ledArray[i] != -1; i++) {
    int led = ledArray[i];
    if (led >= 0 && led < NUM_LEDS) frame.leds[led] = colorCode;
  }
}

static void applyThresholds(float redMaxMm, float yellowMaxMm) {
  DIST_RING1 = redMaxMm * 0.5f;
  DIST_RING2 = redMaxMm;
  DIST_RING3 = redMaxMm + (yellowMaxMm - redMaxMm) * 0.5f;
  DIST_RING4 = yellowMaxMm;
  DIST_RING5 = yellowMaxMm + (yellowMaxMm - redMaxMm) * 0.5f;
}

static void applyFrame(const LedFrame_t& frame) {
  strip.clear();
  for (int i = 0; i < NUM_LEDS; i++) {
    if (frame.leds[i] != LED_COLOR_OFF) {
      strip.setPixelColor(i, resolveColor(frame.leds[i], frame.brightness));
    }
  }
  strip.show();
}

static void clearRing() {
  LedFrame_t frame = {};
  frame.brightness = (uint8_t)currentBrightness;
  applyFrame(frame);
}

int* getZone4(int ring, float angleDeg) {
  int zone;
  if      (angleDeg >= -45 && angleDeg < 45) zone = 1;
  else if (angleDeg >= 45 && angleDeg < 135) zone = 2;
  else if (angleDeg < -135 || angleDeg >= 135) zone = 3;
  else zone = 4;

  switch (ring) {
    case 2: return (zone == 1) ? r2_4_N : (zone == 2) ? r2_4_E : (zone == 3) ? r2_4_S : r2_4_W;
    case 3: return (zone == 1) ? r3_4_N : (zone == 2) ? r3_4_E : (zone == 3) ? r3_4_S : r3_4_W;
    case 4: return (zone == 1) ? r4_4_N : (zone == 2) ? r4_4_E : (zone == 3) ? r4_4_S : r4_4_W;
    case 5: return (zone == 1) ? r5_4_N : (zone == 2) ? r5_4_E : (zone == 3) ? r5_4_S : r5_4_W;
    case 6: return (zone == 1) ? r6_4_N : (zone == 2) ? r6_4_E : (zone == 3) ? r6_4_S : r6_4_W;
    default: return ring1_any;
  }
}

int* getZone6(int ring, float angleDeg) {
  int zone;
  if      (angleDeg >= -30 && angleDeg < 30) zone = 1;
  else if (angleDeg >= 30 && angleDeg < 90) zone = 2;
  else if (angleDeg >= 90 && angleDeg < 150) zone = 3;
  else if (angleDeg < -150 || angleDeg >= 150) zone = 4;
  else if (angleDeg >= -150 && angleDeg < -90) zone = 5;
  else zone = 6;

  switch (ring) {
    case 2:
      if (zone == 1) return r2_6_AHEAD; if (zone == 2) return r2_6_TOP_RIGHT;
      if (zone == 3) return r2_6_BOTTOM_RIGHT; if (zone == 4) return r2_6_BEHIND;
      if (zone == 5) return r2_6_BOTTOM_LEFT; return r2_6_TOP_LEFT;
    case 3:
      if (zone == 1) return r3_6_AHEAD; if (zone == 2) return r3_6_TOP_RIGHT;
      if (zone == 3) return r3_6_BOTTOM_RIGHT; if (zone == 4) return r3_6_BEHIND;
      if (zone == 5) return r3_6_BOTTOM_LEFT; return r3_6_TOP_LEFT;
    case 4:
      if (zone == 1) return r4_6_AHEAD; if (zone == 2) return r4_6_TOP_RIGHT;
      if (zone == 3) return r4_6_BOTTOM_RIGHT; if (zone == 4) return r4_6_BEHIND;
      if (zone == 5) return r4_6_BOTTOM_LEFT; return r4_6_TOP_LEFT;
    case 5:
      if (zone == 1) return r5_6_AHEAD; if (zone == 2) return r5_6_TOP_RIGHT;
      if (zone == 3) return r5_6_BOTTOM_RIGHT; if (zone == 4) return r5_6_BEHIND;
      if (zone == 5) return r5_6_BOTTOM_LEFT; return r5_6_TOP_LEFT;
    case 6:
      if (zone == 1) return r6_6_AHEAD; if (zone == 2) return r6_6_TOP_RIGHT;
      if (zone == 3) return r6_6_BOTTOM_RIGHT; if (zone == 4) return r6_6_BEHIND;
      if (zone == 5) return r6_6_BOTTOM_LEFT; return r6_6_TOP_LEFT;
    default: return ring1_any;
  }
}

int* getZone8(int ring, float angleDeg) {
  int zone;
  if      (angleDeg >= -22.5f && angleDeg < 22.5f) zone = 1;
  else if (angleDeg >= 22.5f && angleDeg < 67.5f) zone = 2;
  else if (angleDeg >= 67.5f && angleDeg < 112.5f) zone = 3;
  else if (angleDeg >= 112.5f && angleDeg < 157.5f) zone = 4;
  else if (angleDeg < -157.5f || angleDeg >= 157.5f) zone = 5;
  else if (angleDeg >= -157.5f && angleDeg < -112.5f) zone = 6;
  else if (angleDeg >= -112.5f && angleDeg < -67.5f) zone = 7;
  else zone = 8;

  switch (ring) {
    case 2:
      if (zone == 1) return r2_8_N; if (zone == 2) return r2_8_NE;
      if (zone == 3) return r2_8_E; if (zone == 4) return r2_8_SE;
      if (zone == 5) return r2_8_S; if (zone == 6) return r2_8_SW;
      if (zone == 7) return r2_8_W; return r2_8_NW;
    case 3:
      if (zone == 1) return r3_8_N; if (zone == 2) return r3_8_NE;
      if (zone == 3) return r3_8_E; if (zone == 4) return r3_8_SE;
      if (zone == 5) return r3_8_S; if (zone == 6) return r3_8_SW;
      if (zone == 7) return r3_8_W; return r3_8_NW;
    case 4:
      if (zone == 1) return r4_8_N; if (zone == 2) return r4_8_NE;
      if (zone == 3) return r4_8_E; if (zone == 4) return r4_8_SE;
      if (zone == 5) return r4_8_S; if (zone == 6) return r4_8_SW;
      if (zone == 7) return r4_8_W; return r4_8_NW;
    case 5:
      if (zone == 1) return r5_8_N; if (zone == 2) return r5_8_NE;
      if (zone == 3) return r5_8_E; if (zone == 4) return r5_8_SE;
      if (zone == 5) return r5_8_S; if (zone == 6) return r5_8_SW;
      if (zone == 7) return r5_8_W; return r5_8_NW;
    case 6:
      if (zone == 1) return r6_8_N; if (zone == 2) return r6_8_NE;
      if (zone == 3) return r6_8_E; if (zone == 4) return r6_8_SE;
      if (zone == 5) return r6_8_S; if (zone == 6) return r6_8_SW;
      if (zone == 7) return r6_8_W; return r6_8_NW;
    default: return ring1_any;
  }
}

static int* getZoneByIndex(int numZones, int ring, int zoneIdx) {
  if (numZones == 4) {
    if (zoneIdx < 0 || zoneIdx >= 4) return ring1_any;
    return getZone4(ring, kZone4Angles[zoneIdx]);
  }
  if (numZones == 8) {
    if (zoneIdx < 0 || zoneIdx >= 8) return ring1_any;
    return getZone8(ring, kZone8Angles[zoneIdx]);
  }
  if (zoneIdx < 0 || zoneIdx >= 6) return ring1_any;
  return getZone6(ring, kZone6Angles[zoneIdx]);
}

static void processZoneProximity(const ZoneProximityPacket& pkt) {
  if (!visualEnabled) return;

  int numZones = (int)pkt.num_zones;
  if (numZones != 4 && numZones != 6 && numZones != 8) return;

  const float* zoneAngles = (numZones == 4) ? kZone4Angles :
                            (numZones == 8) ? kZone8Angles : kZone6Angles;

  LedFrame_t frame = {};
  frame.brightness = (uint8_t)currentBrightness;

  for (int z = 0; z < numZones; z++) {
    float dist = pkt.closest_mm[z];
    if (dist >= 3000.0f) continue;
    if (!currentActiveSectors[z]) continue;

    int ring;
    if      (dist < DIST_RING1) ring = 1;
    else if (dist < DIST_RING2) ring = 2;
    else if (dist < DIST_RING3) ring = 3;
    else if (dist < DIST_RING4) ring = 4;
    else if (dist < DIST_RING5) ring = 5;
    else                        ring = 6;

    uint8_t color = ringToColorCode(ring);
    // Sector mode should fill the full sector (rings 2..6) using the
    // closest obstacle's color, not only a single ring band.
    for (int drawRing = 2; drawRing <= 6; drawRing++) {
      int* leds;
      if (numZones == 4)      leds = getZone4(drawRing, zoneAngles[z]);
      else if (numZones == 8) leds = getZone8(drawRing, zoneAngles[z]);
      else                    leds = getZone6(drawRing, zoneAngles[z]);
      fillZoneInFrame(frame, leds, color);
    }
  }

  applyFrame(frame);
}

static void processLedRenderFrame(const LedRenderFramePacket& pkt) {
  if (!visualEnabled) return;

  int numZones = (int)pkt.num_zones;
  if (numZones != 4 && numZones != 6 && numZones != 8) return;

  LedFrame_t frame = {};
  frame.brightness = (uint8_t)constrain((int)pkt.brightness, 0, 64);

  if (pkt.center_color != LED_COLOR_OFF) {
    frame.leds[92] = pkt.center_color;
  }

  for (int z = 0; z < numZones; z++) {
    if (!currentActiveSectors[z]) continue;
    for (int ring = 2; ring <= 6; ring++) {
      uint8_t color = pkt.ring_zone_colors[ring - 1][z];
      if (color == LED_COLOR_OFF) continue;
      int* leds = getZoneByIndex(numZones, ring, z);
      fillZoneInFrame(frame, leds, color);
    }
  }

  applyFrame(frame);
}

static void applyConfig(const ConfigPacket& cfg) {
  if (cfg.zone_mode == 4 || cfg.zone_mode == 6 || cfg.zone_mode == 8) {
    currentZoneMode = cfg.zone_mode;
  }

  currentBrightness = constrain((int)cfg.brightness, 0, 64);
  visualEnabled = (cfg.visual_enabled != 0);

  for (int i = 0; i < 8; i++) {
    currentActiveSectors[i] = ((cfg.active_sectors >> i) & 0x01) != 0;
  }

  if (cfg.red_threshold_mm > 0 && cfg.yellow_threshold_mm > cfg.red_threshold_mm) {
    applyThresholds((float)cfg.red_threshold_mm, (float)cfg.yellow_threshold_mm);
  }

  if (!visualEnabled) {
    clearRing();
  }

  Serial.printf("[Config] zones=%d bright=%d visual=%s sectors=0x%02X\n",
                currentZoneMode,
                currentBrightness,
                visualEnabled ? "on" : "off",
                cfg.active_sectors);
}

static void handleEspNowPayload(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;

  if (len == (int)sizeof(ConfigPacket)) {
    const ConfigPacket* cfg = reinterpret_cast<const ConfigPacket*>(data);
    if (cfg->msg_type == MSG_CONFIG) {
      applyConfig(*cfg);
    }
    return;
  }

  if (len == (int)sizeof(ZoneProximityPacket)) {
    const ZoneProximityPacket* pkt = reinterpret_cast<const ZoneProximityPacket*>(data);
    if (pkt->msg_type == MSG_ZONE_PROXIMITY) {
      processZoneProximity(*pkt);
    }
    return;
  }

  if (len == (int)sizeof(LedRenderFramePacket)) {
    const LedRenderFramePacket* pkt = reinterpret_cast<const LedRenderFramePacket*>(data);
    if (pkt->msg_type == MSG_LED_RENDER_FRAME) {
      processLedRenderFrame(*pkt);
    }
    return;
  }

  if (len == (int)sizeof(LedFrame_t)) {
    const LedFrame_t* frame = reinterpret_cast<const LedFrame_t*>(data);
    applyFrame(*frame);
    Serial.printf("[LED] Preview frame applied brightness=%d\n", frame->brightness);
    return;
  }

  Serial.printf("[ESP-NOW] Ignored packet size=%d\n", len);
}

#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
void onEspNowReceived(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  const uint8_t* mac = (info != nullptr) ? info->src_addr : nullptr;
  handleEspNowPayload(mac, data, len);
}
#else
void onEspNowReceived(const uint8_t* mac, const uint8_t* data, int len) {
  handleEspNowPayload(mac, data, len);
}
#endif

static void sendHeartbeat() {
  ComponentStatusPacket pkt = {};
  pkt.msg_type = MSG_COMPONENT_STATUS;
  pkt.component_id = COMPONENT_LED_CONTROLLER;
  pkt.sensor_seen_mask = 0;
  pkt.flags = visualEnabled ? 0x01 : 0x00;
  esp_now_send(BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

void setup() {
  Serial.begin(115200);

  strip.begin();
  strip.setBrightness(255);
  strip.show();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.printf("\n[ESP-NOW] LED ESP32 MAC address: %s\n", WiFi.macAddress().c_str());
  Serial.printf("[ESP-NOW] Listening on channel %d\n", ESPNOW_CHANNEL);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init FAILED - halting");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(onEspNowReceived);

  {
    esp_now_peer_info_t broadcastPeer = {};
    memcpy(broadcastPeer.peer_addr, BROADCAST_MAC, 6);
    broadcastPeer.channel = ESPNOW_CHANNEL;
    broadcastPeer.ifidx = WIFI_IF_STA;
    broadcastPeer.encrypt = false;
    if (esp_now_add_peer(&broadcastPeer) != ESP_OK) {
      Serial.println("[ESP-NOW] Broadcast peer FAILED");
    }
  }

  Serial.println("[ESP-NOW] Ready - waiting for zone/render/config/preview packets");
}

void loop() {
  uint32_t nowMs = millis();
  if ((nowMs - lastHeartbeatMs) >= HEARTBEAT_PERIOD_MS) {
    lastHeartbeatMs = nowMs;
    sendHeartbeat();
  }
}
