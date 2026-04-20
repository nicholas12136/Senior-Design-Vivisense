/*
 * ViviSense — LED Controller ESP32
 *
 * Receives LedFrame_t structs from the main ESP32 via ESP-NOW and
 * drives the 93-LED NeoPixel ring.
 *
 * Hardware:
 *   - ESP32 DoIT DevKit V1
 *   - 93-LED NeoPixel ring on GPIO 18  (change LED_PIN if wired differently)
 *
 * Setup:
 *   1. Flash this firmware.
 *   2. Open the serial monitor — it will print this device's MAC address.
 *   3. Copy that MAC into the main ESP32's WebserverV3 firmware so it can
 *      register this device as an ESP-NOW peer.
 *
 * ESP-NOW channel note:
 *   The main ESP32 runs a SoftAP on channel 1 (default).
 *   This device explicitly sets its channel to 1 to match.
 *   If you change the AP channel on the main ESP32, update ESPNOW_CHANNEL here.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Adafruit_NeoPixel.h>
#include "led_frame.h"

// ── Hardware config ───────────────────────────────────────────────────────────
#define LED_PIN        5
#define NUM_LEDS       93
#define ESPNOW_CHANNEL  1   // must match the main ESP32's SoftAP channel

// ── NeoPixel ──────────────────────────────────────────────────────────────────
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ── Colour lookup ─────────────────────────────────────────────────────────────
// Scales a packed 0xRRGGBB base colour by the frame's brightness (0–64).
static uint32_t applyBrightness(uint32_t base, uint8_t brightness) {
  uint8_t r = ((base >> 16) & 0xFF) * brightness / 255;
  uint8_t g = ((base >>  8) & 0xFF) * brightness / 255;
  uint8_t b = ((base      ) & 0xFF) * brightness / 255;
  return strip.Color(r, g, b);
}

static uint32_t resolveColor(uint8_t code, uint8_t brightness) {
  switch (code) {
    case LED_COLOR_RED:    return applyBrightness(0xFF0000, brightness);
    case LED_COLOR_ORANGE: return applyBrightness(0xFF5500, brightness);
    case LED_COLOR_YELLOW: return applyBrightness(0xFFEE00, brightness);
    default:               return 0; // off
  }
}

// ── ESP-NOW receive callback ──────────────────────────────────────────────────
void onFrameReceived(const uint8_t* mac, const uint8_t* data, int len) {
  if (len != sizeof(LedFrame_t)) {
    Serial.printf("[ESP-NOW] Unexpected frame size: %d (expected %d)\n",
                  len, (int)sizeof(LedFrame_t));
    return;
  }

  const LedFrame_t* frame = reinterpret_cast<const LedFrame_t*>(data);

  strip.clear();
  for (int i = 0; i < NUM_LEDS; i++) {
    if (frame->leds[i] != LED_COLOR_OFF) {
      strip.setPixelColor(i, resolveColor(frame->leds[i], frame->brightness));
    }
  }
  strip.show();

  Serial.printf("[LED] Frame applied  brightness=%d\n", frame->brightness);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // NeoPixel — start dark
  strip.begin();
  strip.setBrightness(255); // brightness scaling handled per-pixel in resolveColor
  strip.show();

  // WiFi in station mode (required for ESP-NOW; no AP connection needed)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Fix channel to match the main ESP32's SoftAP channel
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // Print MAC so it can be hardcoded into the main ESP32 firmware
  Serial.printf("\n[ESP-NOW] LED ESP32 MAC address: %s\n", WiFi.macAddress().c_str());
  Serial.printf("[ESP-NOW] Listening on channel %d\n", ESPNOW_CHANNEL);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init FAILED — halting");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(onFrameReceived);
  Serial.println("[ESP-NOW] Ready — waiting for frames from main ESP32");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  // ESP-NOW is interrupt-driven; nothing to do here.
}
