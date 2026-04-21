#pragma once
#include <stdint.h>

// Colour codes used in LedFrame_t::leds[]
#define LED_COLOR_OFF    0
#define LED_COLOR_RED    1   // 0xFF0000 — close / danger
#define LED_COLOR_ORANGE 2   // 0xFF5500 — medium
#define LED_COLOR_YELLOW 3   // 0xFFEE00 — far / safe

// Sent from the main ESP32 to this device via ESP-NOW on every
// obstacle-detection cycle (or whenever the display state changes).
// Must be kept identical on both sides.
typedef struct __attribute__((packed)) {
  uint8_t brightness;   // 0–64  (maps from 0–100% slider)
  uint8_t leds[93];     // per-LED colour code (LED_COLOR_* above)
} LedFrame_t;           // 94 bytes — well within ESP-NOW's 250-byte limit
