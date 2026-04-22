#pragma once
#include <stdint.h>

// Colour codes used in LedFrame_t::leds[]
#define LED_COLOR_OFF    0
#define LED_COLOR_RED    1   // close / danger
#define LED_COLOR_ORANGE 2   // medium
#define LED_COLOR_YELLOW 3   // far (in-range)
#define LED_COLOR_GREEN  4   // clear (sector mode)
#define LED_COLOR_BLUE   5   // center indicator

// Sent from MainController to LEDRingController for live render,
// and from CaregiverApp for preview mode.
typedef struct __attribute__((packed)) {
  uint8_t brightness;   // 0-64
  uint8_t leds[93];     // per-LED color code (LED_COLOR_*)
} LedFrame_t;           // 94 bytes