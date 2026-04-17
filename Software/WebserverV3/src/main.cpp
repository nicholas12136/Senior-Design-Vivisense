/*
 * ViviSense — Unified ESP32 Firmware
 *
 * Combines:
 *   - LED ring controller  (WebserverV3)
 *   - WiFi AP + WebSocket server
 *   - I2S audio playback   (webserver / webserver.ino)
 *
 * Hardware:
 *   - ESP32 DoIT DevKit V1
 *   - 94-LED NeoPixel ring on GPIO 18
 *   - MAX98357A I2S amp: BCK=27, WS=26, DO=25
 *
 * Audio:
 *   - Copy audio_files/ from the webserver project into this directory
 *   - Run  python webserver/wav_to_header.py  to regenerate include/sounds.h
 *   - Flash filesystem:  pio run -t uploadfs
 *   - Flash firmware:    pio run -t upload
 *
 * WebSocket protocol (browser → ESP32):
 *   {"type":"obstacle",  "x":200,  "y":-760}
 *   {"type":"config",    "zoneMode":6, "brightness":80,
 *                        "redThreshold":60, "yellowThreshold":150,
 *                        "activeSectors":[true,true,true,true,true,true]}   (cm)
 *   {"type":"navigate",  "action":"forward|backward|left|right|stop|speedup|slowdown|speak"}
 *   {"type":"volume",    "level":200}   (0–255)
 *   {"type":"preview",   "active":true, "zoneMode":6, "brightness":80,
 *                        "redThreshold":60, "yellowThreshold":150,
 *                        "activeSectors":[true,true,true,true,true,true]}
 *   {"type":"preview",   "active":false}   → clears ring, resumes obstacle detection
 *
 * WebSocket protocol (ESP32 → browser):
 *   {"type":"status", "zoneMode":6, "brightness":80}
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "driver/i2s.h"
#include "sounds.h"
#include <math.h>

// ── Pin & hardware config ─────────────────────────────────────────────────────
#define LED_PIN   18
#define NUM_LEDS  94

#define I2S_BCK_IO  27
#define I2S_WS_IO   26
#define I2S_DO_IO   25
#define I2S_PORT    I2S_NUM_0

// ── Network config ────────────────────────────────────────────────────────────
const char* WIFI_SSID = "ViviSense";
const char* WIFI_PASS = "ViviSense123";

// ── Runtime state ─────────────────────────────────────────────────────────────
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

int currentBrightness = 191;  // 0–255 (maps from 0–100%)
int currentZoneMode   = 6;    // 4, 6, or 8
int currentVolume     = 255;  // 0–255

// ── Preview state (Display button on UI) ──────────────────────────────────────
bool  previewMode           = false;
int   previewZoneMode       = 6;
int   previewBrightness     = 191;
float previewRedMm          = 600.0f;
float previewYellowMm       = 1500.0f;
bool  previewActiveSectors[8]  = {true,true,true,true,true,true,false,false};
bool  currentActiveSectors[8] = {true,true,true,true,true,true,true,true};

// Distance thresholds in mm.
// Defaults match the UI's default cm values × 10.
float DIST_RING1 = 300.0f;   // inner red boundary
float DIST_RING2 = 600.0f;   // red / orange boundary  (= redMax cm × 10)
float DIST_RING3 = 1050.0f;  // mid-orange
float DIST_RING4 = 1500.0f;  // orange / yellow boundary (= yellowMax cm × 10)
float DIST_RING5 = 1950.0f;  // first yellow-green ring

// ── Server & WebSocket ────────────────────────────────────────────────────────
AsyncWebServer httpServer(80);
AsyncWebSocket wsServer("/ws");

// =========================================================
// RING 1 (1 LED, center — no zones)
// =========================================================
int ring1_any[] = {93, -1};

// =========================================================
// 6-ZONE ARRAYS  (AHEAD / TOP_RIGHT / BOTTOM_RIGHT /
//                 BEHIND / BOTTOM_LEFT / TOP_LEFT)
// =========================================================
int r2_6_AHEAD[]        = {85, -1};
int r2_6_TOP_RIGHT[]    = {86, -1};
int r2_6_BOTTOM_RIGHT[] = {87, 88, -1};
int r2_6_BEHIND[]       = {89, -1};
int r2_6_BOTTOM_LEFT[]  = {90, 91, -1};
int r2_6_TOP_LEFT[]     = {92, -1};

int r3_6_AHEAD[]        = {73, -1};
int r3_6_TOP_RIGHT[]    = {74, 75, -1};
int r3_6_BOTTOM_RIGHT[] = {76, 77, 78, -1};
int r3_6_BEHIND[]       = {79, -1};
int r3_6_BOTTOM_LEFT[]  = {80, 81, -1};
int r3_6_TOP_LEFT[]     = {82, 83, 84, -1};

int r4_6_AHEAD[]        = {57, 58, 72, -1};
int r4_6_TOP_RIGHT[]    = {59, 60, -1};
int r4_6_BOTTOM_RIGHT[] = {61, 62, 63, -1};
int r4_6_BEHIND[]       = {64, 65, 66, -1};
int r4_6_BOTTOM_LEFT[]  = {67, 68, -1};
int r4_6_TOP_LEFT[]     = {69, 70, 71, -1};

int r5_6_AHEAD[]        = {33, 34, 56, -1};
int r5_6_TOP_RIGHT[]    = {35, 36, 37, 38, -1};
int r5_6_BOTTOM_RIGHT[] = {39, 40, 41, 42, -1};
int r5_6_BEHIND[]       = {43, 44, 45, -1};
int r5_6_BOTTOM_LEFT[]  = {46, 47, 48, 49, -1};
int r5_6_TOP_LEFT[]     = {50, 51, 52, 53, 54, 55, -1};

int r6_6_AHEAD[]        = {30, 31, 32, 1, 2, -1};
int r6_6_TOP_RIGHT[]    = {3, 4, 5, 6, 7, 8, -1};
int r6_6_BOTTOM_RIGHT[] = {9, 10, 11, 12, 13, -1};
int r6_6_BEHIND[]       = {14, 15, 16, 17, -1};
int r6_6_BOTTOM_LEFT[]  = {18, 19, 20, 21, 22, 23, -1};
int r6_6_TOP_LEFT[]     = {24, 25, 26, 27, 28, 29, -1};

// =========================================================
// 4-ZONE ARRAYS  (N / E / S / W)
// =========================================================
int r2_4_N[] = {92, 85, -1};
int r2_4_E[] = {86, 87, -1};
int r2_4_S[] = {88, 89, -1};
int r2_4_W[] = {90, 91, -1};

int r3_4_N[] = {83, 84, 73, -1};
int r3_4_E[] = {74, 76, 77, -1};
int r3_4_S[] = {75, 78, 79, -1};
int r3_4_W[] = {80, 81, 82, -1};

int r4_4_N[] = {71, 72, 57, 58, -1};
int r4_4_E[] = {59, 60, 61, 62, -1};
int r4_4_S[] = {63, 64, 65, 66, -1};
int r4_4_W[] = {67, 68, 69, 70, -1};

int r5_4_N[] = {54, 55, 56, 33, 34, 35, -1};
int r5_4_E[] = {36, 37, 38, 39, 40, 41, -1};
int r5_4_S[] = {42, 43, 44, 45, 46, 47, -1};
int r5_4_W[] = {48, 49, 50, 51, 52, 53, -1};

int r6_4_N[] = {29, 30, 31, 32, 1, 2, 3, 4, -1};
int r6_4_E[] = {5, 6, 7, 8, 9, 10, 11, 12, -1};
int r6_4_S[] = {13, 14, 15, 16, 17, 18, 19, 20, -1};
int r6_4_W[] = {21, 22, 23, 24, 25, 26, 27, 28, -1};

// =========================================================
// 8-ZONE ARRAYS  (N / NE / E / SE / S / SW / W / NW)
// =========================================================
int r2_8_N[]  = {92, -1};  int r2_8_NE[] = {85, -1};
int r2_8_E[]  = {86, -1};  int r2_8_SE[] = {87, -1};
int r2_8_S[]  = {88, -1};  int r2_8_SW[] = {89, -1};
int r2_8_W[]  = {90, -1};  int r2_8_NW[] = {91, -1};

int r3_8_N[]  = {84, 73, -1};  int r3_8_NE[] = {74, -1};
int r3_8_E[]  = {75, 76, -1};  int r3_8_SE[] = {77, -1};
int r3_8_S[]  = {78, 79, -1};  int r3_8_SW[] = {80, -1};
int r3_8_W[]  = {81, 82, -1};  int r3_8_NW[] = {83, -1};

int r4_8_N[]  = {72, 57, -1};  int r4_8_NE[] = {58, 59, -1};
int r4_8_E[]  = {60, 61, -1};  int r4_8_SE[] = {62, 63, -1};
int r4_8_S[]  = {64, 65, -1};  int r4_8_SW[] = {66, 67, -1};
int r4_8_W[]  = {68, 69, -1};  int r4_8_NW[] = {70, 71, -1};

int r5_8_N[]  = {55, 56, 33, -1};  int r5_8_NE[] = {34, 35, 36, -1};
int r5_8_E[]  = {37, 38, 39, -1};  int r5_8_SE[] = {40, 41, 42, -1};
int r5_8_S[]  = {43, 44, 45, -1};  int r5_8_SW[] = {46, 47, 48, -1};
int r5_8_W[]  = {49, 50, 51, -1};  int r5_8_NW[] = {52, 53, 54, -1};

int r6_8_N[]  = {31, 32, 1, 2, -1};    int r6_8_NE[] = {3, 4, 5, 6, -1};
int r6_8_E[]  = {7, 8, 9, 10, -1};     int r6_8_SE[] = {11, 12, 13, 14, -1};
int r6_8_S[]  = {15, 16, 17, 18, -1};  int r6_8_SW[] = {19, 20, 21, 22, -1};
int r6_8_W[]  = {23, 24, 25, 26, -1};  int r6_8_NW[] = {27, 28, 29, 30, -1};

// =========================================================
// COLORS  — match the UI constants (RED=#ff2222, YELLOW=#ff8800, GREEN=#ffdd00)
// =========================================================
#define COLOR_RED    0xFF0000  // red    — close/danger
#define COLOR_YELLOW 0xFF5500  // orange — medium (saturated, clearly distinct from red and yellow)
#define COLOR_GREEN  0xFFEE00  // yellow — far/safe (bright, clearly distinct from orange)

uint32_t ringColors[] = {
  0,
  COLOR_RED,    // Ring 1
  COLOR_RED,    // Ring 2
  COLOR_YELLOW, // Ring 3
  COLOR_YELLOW, // Ring 4
  COLOR_GREEN,  // Ring 5
  COLOR_GREEN,  // Ring 6
};

// =========================================================
// LED HELPERS
// =========================================================

void lightUpZone(int* ledArray, uint32_t color) {
  strip.clear();
  uint8_t r = ((color >> 16) & 0xFF) * currentBrightness / 255;
  uint8_t g = ((color >>  8) & 0xFF) * currentBrightness / 255;
  uint8_t b = ((color      ) & 0xFF) * currentBrightness / 255;
  uint32_t dimColor = strip.Color(r, g, b);

  for (int i = 0; ledArray[i] != -1; i++) {
    int led = ledArray[i];
    if (led >= 0 && led < NUM_LEDS) strip.setPixelColor(led, dimColor);
  }
  strip.show();
}

// Recalculate all ring boundaries from the two color-transition thresholds.
// redMaxMm   = distance (mm) where RED zone ends
// yellowMaxMm = distance (mm) where YELLOW zone ends
void applyThresholds(float redMaxMm, float yellowMaxMm) {
  DIST_RING1 = redMaxMm * 0.5f;
  DIST_RING2 = redMaxMm;
  DIST_RING3 = redMaxMm + (yellowMaxMm - redMaxMm) * 0.5f;
  DIST_RING4 = yellowMaxMm;
  DIST_RING5 = yellowMaxMm + (yellowMaxMm - redMaxMm) * 0.5f;
}

// =========================================================
// PREVIEW HELPERS
// =========================================================

// Forward declarations — zone pickers are defined after this section.
int* getZone4(int ring, float angleDeg);
int* getZone6(int ring, float angleDeg);
int* getZone8(int ring, float angleDeg);

// Representative distance (mm) for each ring (index = ring number 1-6).
// Mirrors the UI's RING_DISTANCES = [20,50,100,150,200,300] cm.
static const float PREVIEW_RING_DIST_MM[] = {0, 200, 500, 1000, 1500, 2000, 3000};

// One representative angle per zone index for each zone mode.
// Matches the label order in the TypeScript: N/E/S/W,  AHEAD/TR/BR/BEHIND/BL/TL,  N/NE/E/SE/S/SW/W/NW
static const float ZONE4_ANGLES[] = {0, 90, 180, -90};
static const float ZONE6_ANGLES[] = {0, 60, 120, 180, -120, -60};
static const float ZONE8_ANGLES[] = {0, 45, 90, 135, 180, -135, -90, -45};

// Scale a packed 0xRRGGBB color by a 0-255 brightness factor.
uint32_t applyBrightness(uint32_t color, int brightness) {
  uint8_t r = ((color >> 16) & 0xFF) * brightness / 255;
  uint8_t g = ((color >>  8) & 0xFF) * brightness / 255;
  uint8_t b = ((color      ) & 0xFF) * brightness / 255;
  return strip.Color(r, g, b);
}

// Light the full ring as it appears in the Live Preview on the UI:
// all rings shown simultaneously, only active sectors lit, colour based on thresholds.
void lightPreview() {
  strip.clear();

  int zoneCount = previewZoneMode;
  const float* zoneAngles = (zoneCount == 4) ? ZONE4_ANGLES :
                            (zoneCount == 8) ? ZONE8_ANGLES : ZONE6_ANGLES;

  for (int ring = 1; ring <= 6; ring++) {
    float dist = PREVIEW_RING_DIST_MM[ring];

    uint32_t color;
    if      (dist <= previewRedMm)    color = COLOR_RED;
    else if (dist <= previewYellowMm) color = COLOR_YELLOW;
    else                              color = COLOR_GREEN;

    uint32_t dimColor = applyBrightness(color, previewBrightness);

    for (int z = 0; z < zoneCount; z++) {
      if (!previewActiveSectors[z]) continue;
      int* leds;
      if (ring == 1) {
        leds = ring1_any;
      } else if (zoneCount == 4) {
        leds = getZone4(ring, zoneAngles[z]);
      } else if (zoneCount == 8) {
        leds = getZone8(ring, zoneAngles[z]);
      } else {
        leds = getZone6(ring, zoneAngles[z]);
      }
      for (int i = 0; leds[i] != -1; i++) {
        int led = leds[i];
        if (led >= 0 && led < NUM_LEDS) strip.setPixelColor(led, dimColor);
      }
    }
  }
  strip.show();
}

// =========================================================
// ZONE PICKERS
// =========================================================

int* getZone4(int ring, float angleDeg) {
  int zone;
  if      (angleDeg >= -45 && angleDeg <  45)  zone = 1; // N
  else if (angleDeg >=  45 && angleDeg < 135)  zone = 2; // E
  else if (angleDeg < -135 || angleDeg >= 135) zone = 3; // S
  else                                          zone = 4; // W

  switch (ring) {
    case 2: return (zone==1)?r2_4_N:(zone==2)?r2_4_E:(zone==3)?r2_4_S:r2_4_W;
    case 3: return (zone==1)?r3_4_N:(zone==2)?r3_4_E:(zone==3)?r3_4_S:r3_4_W;
    case 4: return (zone==1)?r4_4_N:(zone==2)?r4_4_E:(zone==3)?r4_4_S:r4_4_W;
    case 5: return (zone==1)?r5_4_N:(zone==2)?r5_4_E:(zone==3)?r5_4_S:r5_4_W;
    case 6: return (zone==1)?r6_4_N:(zone==2)?r6_4_E:(zone==3)?r6_4_S:r6_4_W;
  }
  return ring1_any;
}

int* getZone6(int ring, float angleDeg) {
  int zone;
  if      (angleDeg >= -30  && angleDeg <  30)  zone = 1; // AHEAD
  else if (angleDeg >=  30  && angleDeg <  90)  zone = 2; // TOP_RIGHT
  else if (angleDeg >=  90  && angleDeg < 150)  zone = 3; // BOTTOM_RIGHT
  else if (angleDeg <  -150 || angleDeg >= 150) zone = 4; // BEHIND
  else if (angleDeg >= -150 && angleDeg < -90)  zone = 5; // BOTTOM_LEFT
  else                                           zone = 6; // TOP_LEFT

  switch (ring) {
    case 2:
      if (zone==1) return r2_6_AHEAD; if (zone==2) return r2_6_TOP_RIGHT;
      if (zone==3) return r2_6_BOTTOM_RIGHT; if (zone==4) return r2_6_BEHIND;
      if (zone==5) return r2_6_BOTTOM_LEFT; return r2_6_TOP_LEFT;
    case 3:
      if (zone==1) return r3_6_AHEAD; if (zone==2) return r3_6_TOP_RIGHT;
      if (zone==3) return r3_6_BOTTOM_RIGHT; if (zone==4) return r3_6_BEHIND;
      if (zone==5) return r3_6_BOTTOM_LEFT; return r3_6_TOP_LEFT;
    case 4:
      if (zone==1) return r4_6_AHEAD; if (zone==2) return r4_6_TOP_RIGHT;
      if (zone==3) return r4_6_BOTTOM_RIGHT; if (zone==4) return r4_6_BEHIND;
      if (zone==5) return r4_6_BOTTOM_LEFT; return r4_6_TOP_LEFT;
    case 5:
      if (zone==1) return r5_6_AHEAD; if (zone==2) return r5_6_TOP_RIGHT;
      if (zone==3) return r5_6_BOTTOM_RIGHT; if (zone==4) return r5_6_BEHIND;
      if (zone==5) return r5_6_BOTTOM_LEFT; return r5_6_TOP_LEFT;
    case 6:
      if (zone==1) return r6_6_AHEAD; if (zone==2) return r6_6_TOP_RIGHT;
      if (zone==3) return r6_6_BOTTOM_RIGHT; if (zone==4) return r6_6_BEHIND;
      if (zone==5) return r6_6_BOTTOM_LEFT; return r6_6_TOP_LEFT;
  }
  return ring1_any;
}

int* getZone8(int ring, float angleDeg) {
  int zone;
  if      (angleDeg >= -22.5 && angleDeg <  22.5)  zone = 1; // N
  else if (angleDeg >=  22.5 && angleDeg <  67.5)  zone = 2; // NE
  else if (angleDeg >=  67.5 && angleDeg < 112.5)  zone = 3; // E
  else if (angleDeg >= 112.5 && angleDeg < 157.5)  zone = 4; // SE
  else if (angleDeg < -157.5 || angleDeg >= 157.5) zone = 5; // S
  else if (angleDeg >= -157.5 && angleDeg < -112.5) zone = 6; // SW
  else if (angleDeg >= -112.5 && angleDeg <  -67.5) zone = 7; // W
  else                                               zone = 8; // NW

  switch (ring) {
    case 2:
      if (zone==1) return r2_8_N; if (zone==2) return r2_8_NE;
      if (zone==3) return r2_8_E; if (zone==4) return r2_8_SE;
      if (zone==5) return r2_8_S; if (zone==6) return r2_8_SW;
      if (zone==7) return r2_8_W; return r2_8_NW;
    case 3:
      if (zone==1) return r3_8_N; if (zone==2) return r3_8_NE;
      if (zone==3) return r3_8_E; if (zone==4) return r3_8_SE;
      if (zone==5) return r3_8_S; if (zone==6) return r3_8_SW;
      if (zone==7) return r3_8_W; return r3_8_NW;
    case 4:
      if (zone==1) return r4_8_N; if (zone==2) return r4_8_NE;
      if (zone==3) return r4_8_E; if (zone==4) return r4_8_SE;
      if (zone==5) return r4_8_S; if (zone==6) return r4_8_SW;
      if (zone==7) return r4_8_W; return r4_8_NW;
    case 5:
      if (zone==1) return r5_8_N; if (zone==2) return r5_8_NE;
      if (zone==3) return r5_8_E; if (zone==4) return r5_8_SE;
      if (zone==5) return r5_8_S; if (zone==6) return r5_8_SW;
      if (zone==7) return r5_8_W; return r5_8_NW;
    case 6:
      if (zone==1) return r6_8_N; if (zone==2) return r6_8_NE;
      if (zone==3) return r6_8_E; if (zone==4) return r6_8_SE;
      if (zone==5) return r6_8_S; if (zone==6) return r6_8_SW;
      if (zone==7) return r6_8_W; return r6_8_NW;
  }
  return ring1_any;
}

// =========================================================
// OBSTACLE PROCESSING
// =========================================================

// Returns the 0-based zone index for an angle + zone mode.
// Index order matches the activeSectors[] arrays sent by the UI:
//   4-zone: N=0, E=1, S=2, W=3
//   6-zone: AHEAD=0, TOP_RIGHT=1, BOTTOM_RIGHT=2, BEHIND=3, BOTTOM_LEFT=4, TOP_LEFT=5
//   8-zone: N=0, NE=1, E=2, SE=3, S=4, SW=5, W=6, NW=7
int getZoneIndex(float angleDeg, int zoneMode) {
  if (zoneMode == 4) {
    if (angleDeg >= -45  && angleDeg <   45) return 0; // N
    if (angleDeg >=  45  && angleDeg <  135) return 1; // E
    if (angleDeg < -135  || angleDeg >= 135) return 2; // S
    return 3; // W
  }
  if (zoneMode == 8) {
    if (angleDeg >= -22.5 && angleDeg <  22.5) return 0; // N
    if (angleDeg >=  22.5 && angleDeg <  67.5) return 1; // NE
    if (angleDeg >=  67.5 && angleDeg < 112.5) return 2; // E
    if (angleDeg >= 112.5 && angleDeg < 157.5) return 3; // SE
    if (angleDeg < -157.5 || angleDeg >= 157.5) return 4; // S
    if (angleDeg >= -157.5 && angleDeg < -112.5) return 5; // SW
    if (angleDeg >= -112.5 && angleDeg <  -67.5) return 6; // W
    return 7; // NW
  }
  // 6 zones
  if (angleDeg >= -30  && angleDeg <  30)  return 0; // AHEAD
  if (angleDeg >=  30  && angleDeg <  90)  return 1; // TOP_RIGHT
  if (angleDeg >=  90  && angleDeg < 150)  return 2; // BOTTOM_RIGHT
  if (angleDeg < -150  || angleDeg >= 150) return 3; // BEHIND
  if (angleDeg >= -150 && angleDeg < -90)  return 4; // BOTTOM_LEFT
  return 5; // TOP_LEFT
}

void processCoordinates(float x, float y) {
  if (previewMode) return;  // hands are on the preview — ignore live obstacle data
  float distance = sqrt(x * x + y * y);

  if (distance > 3000.0f) {
    strip.clear();
    strip.show();
    return;
  }

  int ring;
  if      (distance < DIST_RING1) ring = 1;
  else if (distance < DIST_RING2) ring = 2;
  else if (distance < DIST_RING3) ring = 3;
  else if (distance < DIST_RING4) ring = 4;
  else if (distance < DIST_RING5) ring = 5;
  else                            ring = 6;

  float angleRad = atan2(-y, x);
  float angleDeg = angleRad * (180.0f / PI);

  // If the zone this obstacle falls in is disabled, clear the ring and return.
  // Ring 1 (center LED) is omnidirectional — always shown.
  if (ring != 1 && !currentActiveSectors[getZoneIndex(angleDeg, currentZoneMode)]) {
    strip.clear();
    strip.show();
    return;
  }

  int* zone;
  if (ring == 1) {
    zone = ring1_any;
  } else if (currentZoneMode == 4) {
    zone = getZone4(ring, angleDeg);
  } else if (currentZoneMode == 8) {
    zone = getZone8(ring, angleDeg);
  } else {
    zone = getZone6(ring, angleDeg);
  }

  lightUpZone(zone, ringColors[ring]);
}

// =========================================================
// AUDIO ENGINE (I2S + MAX98357A)
// =========================================================

void setupI2S() {
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

void playAudio(const unsigned char* audioData, unsigned int dataLen, unsigned int sampleRate) {
  if (dataLen == 0) return;
  i2s_set_sample_rates(I2S_PORT, sampleRate);

  size_t  bytesWritten;
  int16_t buf[128];
  int     bufIdx   = 0;
  float   volFactor = (float)currentVolume / 255.0f;

  for (unsigned int i = 0; i + 1 < dataLen; i += 2) {
    uint8_t lo  = pgm_read_byte(&audioData[i]);
    uint8_t hi  = pgm_read_byte(&audioData[i + 1]);
    int16_t smp = (int16_t)((hi << 8) | lo);
    smp = (int16_t)(smp * volFactor);
    buf[bufIdx++] = smp; // Left
    buf[bufIdx++] = smp; // Right
    if (bufIdx >= 128) {
      i2s_write(I2S_PORT, buf, sizeof(buf), &bytesWritten, portMAX_DELAY);
      bufIdx = 0;
    }
  }
  if (bufIdx > 0)
    i2s_write(I2S_PORT, buf, bufIdx * 2, &bytesWritten, portMAX_DELAY);
  i2s_zero_dma_buffer(I2S_PORT);
}

// =========================================================
// NAVIGATION AUDIO ACTIONS
// =========================================================

void handleStop()      { Serial.println("Action: STOP");     playAudio(stop_data,       stop_len,       stop_rate);      }
void handleGoForward() { Serial.println("Action: Forward");  playAudio(go_forward_data, go_forward_len, go_forward_rate); }
void handleTurnLeft()  { Serial.println("Action: Left");     playAudio(turn_left_data,  turn_left_len,  turn_left_rate);  }
void handleTurnRight() { Serial.println("Action: Right");    playAudio(turn_right_data, turn_right_len, turn_right_rate); }
void handleSpeedUp()   { Serial.println("Action: Speed Up"); playAudio(speed_up_data,   speed_up_len,   speed_up_rate);   }
void handleSlowDown()  { Serial.println("Action: Slow Down");playAudio(slow_down_data,  slow_down_len,  slow_down_rate);  }
void handleBackUp()    { Serial.println("Action: Back Up");  playAudio(back_up_data,    back_up_len,    back_up_rate);    }
void handleSpeak()     { Serial.println("Action: Speak");    /* TODO: trigger voice input */ }

// =========================================================
// WEBSOCKET MESSAGE HANDLER
// =========================================================

void handleWebSocketMessage(const char* msg) {
  JsonDocument doc;
  if (deserializeJson(doc, msg) != DeserializationError::Ok) return;

  const char* type = doc["type"] | "";

  if (strcmp(type, "obstacle") == 0) {
    float x = doc["x"].as<float>();
    float y = doc["y"].as<float>();
    processCoordinates(x, y);

  } else if (strcmp(type, "config") == 0) {
    if (!doc["zoneMode"].isNull()) {
      int mode = doc["zoneMode"].as<int>();
      if (mode == 4 || mode == 6 || mode == 8) currentZoneMode = mode;
    }
    if (!doc["brightness"].isNull()) {
      currentBrightness = constrain((int)(doc["brightness"].as<float>() * 2.55f), 0, 255);
    }
    if (!doc["redThreshold"].isNull() && !doc["yellowThreshold"].isNull()) {
      float redMm    = doc["redThreshold"].as<float>()    * 10.0f; // cm → mm
      float yellowMm = doc["yellowThreshold"].as<float>() * 10.0f;
      if (redMm > 0 && yellowMm > redMm) applyThresholds(redMm, yellowMm);
    }
    if (!doc["activeSectors"].isNull()) {
      JsonArray sectors = doc["activeSectors"].as<JsonArray>();
      int count = min((int)sectors.size(), 8);
      for (int i = 0; i < count; i++) currentActiveSectors[i] = sectors[i].as<bool>();
    }
    Serial.printf("[Config] zoneMode=%d  brightness=%d%%\n",
                  currentZoneMode, (int)(currentBrightness / 2.55f));

  } else if (strcmp(type, "navigate") == 0) {
    const char* action = doc["action"] | "";
    if      (strcmp(action, "forward")  == 0) handleGoForward();
    else if (strcmp(action, "backward") == 0) handleBackUp();
    else if (strcmp(action, "left")     == 0) handleTurnLeft();
    else if (strcmp(action, "right")    == 0) handleTurnRight();
    else if (strcmp(action, "stop")     == 0) handleStop();
    else if (strcmp(action, "speedup")  == 0) handleSpeedUp();
    else if (strcmp(action, "slowdown") == 0) handleSlowDown();
    else if (strcmp(action, "speak")    == 0) handleSpeak();

  } else if (strcmp(type, "volume") == 0) {
    currentVolume = constrain(doc["level"].as<int>(), 0, 255);
    Serial.printf("[Volume] %d\n", currentVolume);

  } else if (strcmp(type, "preview") == 0) {
    bool active = doc["active"].as<bool>();
    if (!active) {
      previewMode = false;
      strip.clear();
      strip.show();
      Serial.println("[Preview] Disabled — resuming obstacle detection");
    } else {
      previewMode = true;

      int mode = doc["zoneMode"] | previewZoneMode;
      if (mode == 4 || mode == 6 || mode == 8) previewZoneMode = mode;

      if (!doc["brightness"].isNull()) {
        previewBrightness = constrain((int)(doc["brightness"].as<float>() * 2.55f), 0, 255);
      }
      if (!doc["redThreshold"].isNull() && !doc["yellowThreshold"].isNull()) {
        float rMm = doc["redThreshold"].as<float>()    * 10.0f;
        float yMm = doc["yellowThreshold"].as<float>() * 10.0f;
        if (rMm > 0 && yMm > rMm) { previewRedMm = rMm; previewYellowMm = yMm; }
      }
      if (!doc["activeSectors"].isNull()) {
        JsonArray sectors = doc["activeSectors"].as<JsonArray>();
        int count = min((int)sectors.size(), 8);
        for (int i = 0; i < count; i++) previewActiveSectors[i] = sectors[i].as<bool>();
      }

      lightPreview();
      Serial.printf("[Preview] zoneMode=%d  brightness=%d%%  red=%.0fmm  yellow=%.0fmm\n",
                    previewZoneMode, (int)(previewBrightness / 2.55f),
                    previewRedMm, previewYellowMm);
    }
  }
}

// =========================================================
// WEBSOCKET EVENT HANDLER
// =========================================================

void onWsEvent(AsyncWebSocket* /*server*/, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Client #%u connected from %s\n",
                  client->id(), client->remoteIP().toString().c_str());
    // Push current config to the new client
    JsonDocument doc;
    doc["type"]       = "status";
    doc["zoneMode"]   = currentZoneMode;
    doc["brightness"] = (int)(currentBrightness / 2.55f);
    String out;
    serializeJson(doc, out);
    client->text(out);

  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Client #%u disconnected\n", client->id());

  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len
        && info->opcode == WS_TEXT) {
      data[len] = 0; // null-terminate
      handleWebSocketMessage((char*)data);
    }
  }
}

// =========================================================
// SETUP & LOOP
// =========================================================

void setup() {
  Serial.begin(115200);

  // LED ring
  strip.begin();
  strip.setBrightness(255); // per-LED scaling handled in lightUpZone
  strip.setPixelColor(5,555);
  strip.show();

  // SPIFFS (serves the TypeScript UI)
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] Mount failed — UI will not be served");
  }

  // I2S audio
  setupI2S();

  // WiFi Access Point
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] AP started — IP: ");
  Serial.println(WiFi.softAPIP());

  // WebSocket
  wsServer.onEvent(onWsEvent);
  httpServer.addHandler(&wsServer);

  // Serve UI from SPIFFS; SPA uses hash routing so all paths → index.html.
  // index.html itself is served with no-cache so the browser always fetches the
  // latest file (which carries the correct hashed asset filenames).
  // Hashed assets (/assets/*.js, /assets/*.css) can be cached freely.
  httpServer.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    AsyncWebServerResponse* res = req->beginResponse(SPIFFS, "/index.html", "text/html");
    res->addHeader("Cache-Control", "no-cache");
    req->send(res);
  });
  httpServer.serveStatic("/assets/", SPIFFS, "/assets/");
  httpServer.onNotFound([](AsyncWebServerRequest* req) {
    AsyncWebServerResponse* res = req->beginResponse(SPIFFS, "/index.html", "text/html");
    res->addHeader("Cache-Control", "no-cache");
    req->send(res);
  });

  httpServer.begin();
  Serial.println("[HTTP] Server started on port 80");

}

void loop() {
  // Keep WebSocket connections clean
  wsServer.cleanupClients();

  // Optional serial pass-through for bench testing without a browser
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;

    int comma = input.indexOf(',');
    if (comma != -1) {
      float x = input.substring(0, comma).toFloat();
      float y = input.substring(comma + 1).toFloat();
      float d = sqrt(x * x + y * y);
      Serial.printf("X: %.1f  Y: %.1f  Dist: %.1f mm\n", x, y, d);
      processCoordinates(x, y);
    }
  }


}
