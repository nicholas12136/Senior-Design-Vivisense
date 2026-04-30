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
static constexpr int kDefaultBrightness = 26;        // 20% of capped max (50%)
static constexpr int kMaxBrightness = 128;           // 50%
static constexpr bool kDefaultVisualEnabled = true;
static constexpr uint8_t kDefaultActiveSectorMask = 0xFF;
static constexpr uint8_t kDefaultLedRenderMode = 0;  // 0=sector fill, 1=radar

// Color thresholds in mm
// <= red => RED, <= orange => ORANGE, <= yellow => YELLOW, else clear.
static constexpr float kDefaultRedThresholdMm = 650.0f;
static constexpr float kDefaultOrangeThresholdMm = 950.0f;
static constexpr float kDefaultYellowThresholdMm = 1250.0f;

// Polar occupancy filter defaults for internal obstacle grid resolution.
// LED output sectors are still selected separately via zone mode (4/6/8).
static constexpr int kPolarNumRings = 12;
static constexpr int kPolarMaxZones = 24;
static constexpr uint8_t kPolarConfRise = 80;
static constexpr uint8_t kPolarConfDecay = 12;
static constexpr uint8_t kPolarEnterThreshold = 200;
static constexpr uint8_t kPolarExitThreshold = 45;

// Max detection range used for pruning invalid/far points.
static constexpr float kMaxObstacleRangeMm = 3000.0f;

// Vertical occupancy window in world Z (mm).
// Points below the floor cutoff or above the ceiling cutoff do not count
// toward the polar occupancy grid.
static constexpr float kDefaultObstacleFloorZMm = 50.0f;
static constexpr float kDefaultObstacleCeilingZMm = 5000.0f;

}  // namespace RuntimeDefaults
