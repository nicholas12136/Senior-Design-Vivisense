# ViviSense System Overview

ViviSense is a wheelchair obstacle-awareness system for pediatric manual wheelchair users. Eight VL53L7CX time-of-flight sensors scan the environment, and a 93-LED NeoPixel ring provides directional proximity feedback. A caregiver can connect over WiFi to configure feedback and play navigation audio prompts.

For step-by-step bring-up from scratch, see `Software/RUNNING_THE_SYSTEM.md`.

---

## Hardware

| Component | Role |
|---|---|
| 8x VL53L7CX | ToF sensor, 60x60 degree FOV, 4x4 zone array, up to 15 Hz |
| ESP32 (MainController) | Receives pod sensor packets, computes per-zone proximity, broadcasts zone proximity |
| ESP32 (LEDRingController) | Receives zone proximity + config, maps zones to LED ring, drives NeoPixel strip |
| ESP32 (CaregiverApp) | WiFi AP, browser UI, WebSocket server, I2S audio playback, preview frames |
| 93-LED NeoPixel ring | 6 concentric rings: 1, 8, 12, 16, 24, 32 LEDs |
| MAX98357A amp | I2S amplifier for caregiver-triggered navigation audio |

---

## Data Flow (Beta, Self-Contained)

```
Sensor Pods
  -> ESP-NOW SensorPacket / SensorPacketV2
MainController
  -> transforms points to world frame
  -> finds nearest obstacle per zone
  -> broadcasts ZoneProximityPacket (~15 Hz)
LEDRingController
  -> receives ZoneProximityPacket
  -> maps zone distances to ring + color
  -> updates NeoPixel ring

Caregiver phone/tablet
  -> WiFi + WebSocket to CaregiverApp
CaregiverApp
  -> broadcasts ConfigPacket to MainController + LEDRingController
  -> sends preview LedFrame_t directly to LEDRingController when preview is active
  -> plays navigation audio via I2S amp
```

Alpha/development serial output is still available from MainController for the Python visualizer tools.

---

## Coordinate System

- `+x` = forward (wheelchair motion direction)
- `+y` = left
- `+z` = up

Zone angle assignment uses `atan2(-y, x)` so:

- `0 deg` = forward
- `+90 deg` = right
- `+/-180 deg` = behind
- `-90 deg` = left

---

## Sensor Geometry

Each sensor uses a rigid transform:

`p_world = R * p_sensor + t`

`R = Rz(gamma) * Ry(beta) * Rx(alpha)` (extrinsic XYZ Euler)

Sensor pose data is hardcoded in:

- `Software/ESP firmware/MainController/src/main.cpp` (`SENSOR_CONFIGS`)

Only `targetStatus == 5` is treated as valid for obstacle processing.

---

## Zone Modes

The LED logic supports 4, 6, or 8 angular zones:

| Mode | Zones | Width |
|---|---|---|
| 4-zone | N, E, S, W | 90 deg |
| 6-zone (default) | AHEAD, TOP_RIGHT, BOTTOM_RIGHT, BEHIND, BOTTOM_LEFT, TOP_LEFT | 60 deg |
| 8-zone | N, NE, E, SE, S, SW, W, NW | 45 deg |

Zone index `0` is centered at forward (`0 deg`), with clockwise ordering.

---

## LED Feedback Logic

1. MainController computes nearest valid obstacle per zone from all 128 cells (8 sensors x 16 cells), using planar distance `sqrt(x^2 + y^2)`.
2. MainController broadcasts one `ZoneProximityPacket` about every 67 ms.
3. LEDRingController maps each active zone distance into one of six rings:

| Distance (mm) | Ring | Color |
|---|---|---|
| `< 300` | 1 (center) | Red |
| `300-600` | 2 | Red |
| `600-1050` | 3 | Orange |
| `1050-1500` | 4 | Orange |
| `1500-1950` | 5 | Yellow |
| `1950-3000` | 6 | Yellow |
| `> 3000` | Off | Off |

The center LED is omnidirectional (not tied to a single zone).

Preview behavior:

- CaregiverApp sends temporary `LedFrame_t` preview frames to LEDRingController.
- CaregiverApp temporarily disables live visual broadcast from MainController during preview to avoid frame contention.

---

## ESP-NOW Packet Reference

All devices use ESP-NOW channel `1`.

| Packet | Size | Direction | Purpose |
|---|---|---|---|
| `SensorPacket` | 69 B | Pod -> MainController | One sensor frame (16 cells) |
| `SensorPacketV2` | 75 B | Pod -> MainController | Sensor frame + seq + send timestamp |
| `SyncRequestPacket` | 8 B | MainController -> Pod | Clock sync request |
| `SyncResponsePacket` | 16 B | Pod -> MainController | Clock sync response |
| `ZoneProximityPacket` | 34 B | MainController -> LEDRingController (broadcast) | Closest distance per zone |
| `ConfigPacket` | 10 B | CaregiverApp -> MainController + LEDRingController (broadcast) | Zone mode, thresholds, brightness, active sectors, audio/visual flags |
| `LedFrame_t` | 94 B | CaregiverApp -> LEDRingController (unicast) | Preview LED frame |

Broadcast MAC (`FF:FF:FF:FF:FF:FF`) is used for `ZoneProximityPacket` and `ConfigPacket`.

---

## CaregiverApp WebSocket API

Browser connects to:

- `ws://<caregiver-ip>/ws`
- AP default IP is `192.168.4.1`

Common browser -> CaregiverApp messages:

```json
{"type":"config","zoneMode":6,"brightness":80,"redThreshold":60,"yellowThreshold":150,"activeSectors":[true,true,true,true,true,true],"audioEnabled":true,"visualEnabled":true}
```

```json
{"type":"navigate","action":"forward|backward|left|right|stop|speedup|slowdown|speak"}
```

```json
{"type":"volume","level":200}
```

```json
{"type":"preview","active":true,"zoneMode":6,"brightness":80,"redThreshold":60,"yellowThreshold":150,"activeSectors":[true,true,true,true,true,true]}
```

```json
{"type":"getConfig"}
```

CaregiverApp -> browser status example:

```json
{"type":"status","zoneMode":6,"brightness":80,"redThreshold":60,"yellowThreshold":150,"audioEnabled":true,"visualEnabled":true,"activeSectors":[true,true,true,true,true,true]}
```

Threshold units in WebSocket messages are centimeters.

---

## Firmware Targets

| Firmware Project | Board |
|---|---|
| `Software/ESP firmware/MainController` | `esp32dev` |
| `Software/ESP firmware/LEDRingController` | `esp32doit-devkit-v1` |
| `Software/CaregiverApp` | `esp32doit-devkit-v1` |
| `Software/ESP firmware/LeftPodSender` | `esp32dev` |
| `Software/ESP firmware/RightPodSender` | `esp32dev` (or c3 env if needed) |
| `Software/ESP firmware/BaseSender` | `esp32dev` |

See `Software/RUNNING_THE_SYSTEM.md` for exact commands and flashing order.

---

## Known Limitations

- Sensor pose calibration is hardcoded in MainController firmware.
- Zone assignment is angle-only (no height filtering).
- LED preview unicast uses a hardcoded LEDRingController MAC in CaregiverApp (`LED_ESP32_MAC`).
- Audio clips are compiled into `Software/CaregiverApp/include/sounds.h`.
