#pragma once

#include <stdint.h>

// Central place for default runtime behavior in MainController.
// These values are applied at boot and can be overridden at runtime by app config
// or serial tuning commands.
namespace RuntimeDefaults {

// ESP-NOW + telemetry
static constexpr uint8_t kEspNowChannel = 1;
static constexpr uint32_t kSerialBaud = 115200;
static constexpr uint32_t kStatusEmitPeriodMs = 500;
static constexpr uint32_t kSensorStaleTimeoutMs = 400;
static constexpr uint32_t kProximityPeriodMs = 33;

// LED / zone defaults
static constexpr int kDefaultZoneCount = 6;          // Allowed: 4, 6, 8
static constexpr int kDefaultBrightness = 40;        // 0..64
static constexpr bool kDefaultVisualEnabled = true;
static constexpr uint8_t kDefaultActiveSectorMask = 0xFF;
static constexpr uint8_t kDefaultLedRenderMode = 0;  // 0=sector fill, 1=radar

// Distance ring boundaries in mm
static constexpr float kRingThresholdsMm[5] = {
    300.0f, 600.0f, 1050.0f, 1500.0f, 1950.0f};

// Polar occupancy filter defaults for internal obstacle grid resolution.
// LED output sectors are still selected separately via zone mode (4/6/8).
static constexpr int kPolarNumRings = 12;
static constexpr int kPolarMaxZones = 24;
static constexpr uint8_t kPolarConfRise = 140;
static constexpr uint8_t kPolarConfDecay = 36;
static constexpr uint8_t kPolarEnterThreshold = 120;
static constexpr uint8_t kPolarExitThreshold = 70;

// Max detection range used for pruning invalid/far points.
static constexpr float kMaxObstacleRangeMm = 3000.0f;

}  // namespace RuntimeDefaults
