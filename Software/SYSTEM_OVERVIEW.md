# Vivisense — System Overview

Vivisense is a wheelchair obstacle-awareness system for pediatric manual wheelchair users. Eight VL53L7CX time-of-flight sensors continuously scan the environment, and a NeoPixel LED ring mounted on the wheelchair gives the user real-time directional proximity feedback. A caregiver can connect from a distance to issue audio commands and adjust settings.

---

## Hardware

| Component | Role |
|---|---|
| 8× VL53L7CX | ToF sensor, 60°×60° FOV, 4×4 zone array, up to 15 Hz |
| ESP32 (MainController) | Receives sensor data, computes proximity, drives LEDs |
| ESP32 (CaregiverApp) | WiFi access point, browser UI, I2S audio playback |
| ESP32 (LED Controller) | Receives LED frames, drives NeoPixel ring |
| 93-LED NeoPixel ring | 6 concentric rings: 1, 8, 12, 16, 24, 32 LEDs |
| MAX98357A amp | I2S digital amplifier; BCK=27, WS=26, DO=25 |

### Sensor Pod Layout

- **Front pods** (sensors 1–4): mounted at ~500 mm height, positioned left and right of center, pointing outward
- **Tower pods** (sensors 5–8): mounted at ~1283 mm height on the push handle, pointing outward and downward

### LED Ring Layout

Rings are indexed by closeness to the center:

| Ring | LED count | Index range | Color at max distance |
|---|---|---|---|
| 1 (center) | 1 | 92 | Omnidirectional; always matches the closest obstacle color |
| 2 | 8 | 84–91 | Red |
| 3 | 12 | 72–83 | Orange |
| 4 | 16 | 56–71 | Orange |
| 5 | 24 | 32–55 | Yellow |
| 6 (outer) | 32 | 0–31 | Yellow |

A closer obstacle lights an inner ring (ring 1 = danger); a farther obstacle lights an outer ring.

---

## Data Flow (Beta — Self-Contained)

```
Sensor Pods
    │  ESP-NOW  SensorPacket (69 B) or SensorPacketV2 (75 B)
    ▼
MainController ESP32
    ├─ SensorAndPoint.h: p_world = R × p_sensor + t  (per cell)
    ├─ Accumulate closest obstacle per zone across all 128 cells
    ├─ Broadcast ZoneProximityPacket (34 B) every ~67 ms at 15 Hz
    │        ↓ ESP-NOW broadcast
    └──────────────────────────────────►  CaregiverApp ESP32
                                              │
                                         processZoneProximity()
                                              │  builds LedFrame_t
                                              │  ESP-NOW unicast (94 B)
                                              ▼
                                         LED Controller ESP32
                                              │
                                         NeoPixel ring (93 LEDs)

Browser (caregiver phone/tablet)
    │  WiFi → WebSocket /ws
    ▼
CaregiverApp ESP32
    ├─ Audio: play voice commands via I2S → MAX98357A
    └─ ConfigPacket (10 B) → ESP-NOW broadcast → MainController
```

### Alpha Path (Development / Demo Only)

The alpha path still works in parallel for development:

```
MainController  --USB Serial CSV-->  PC Visualizer (Python)
```

The `P,<sid>,<cell>,<valid>,<x_m>,<y_m>,<z_m>,<status>,<ts>` serial stream is still emitted. It does not interfere with the beta feedback loop.

---

## Coordinate System

- **+x** = forward (in the direction the wheelchair moves)
- **+y** = LEFT
- **+z** = up

Angles for zone assignment: `atan2(-y, x)` converts this right-handed frame into a clockwise-from-forward bearing where:
- 0° = forward
- +90° = right
- ±180° = behind
- −90° = left (270° clockwise)

---

## Sensor Geometry

Each sensor is modeled as a rigid body with a known pose:

```
p_world = R × p_sensor + t
R = Rz(γ) × Ry(β) × Rx(α)   (extrinsic XYZ Euler)
```

The pose for each sensor is hardcoded in `BaseReciever/src/main.cpp`:

```cpp
SensorConfig SENSOR_CONFIGS[NUM_SENSORS] = {
    // ID,  x_mm,     y_mm,      z_mm,    alpha, beta,  gamma
    {1,  165.1f,  295.275f,  501.65f,  0.0f,  0.0f,  30.0f},
    {2,  177.8f,  257.175f,  501.65f,  0.0f,  0.0f,   0.0f},
    {3,  177.8f, -257.175f,  501.65f, 180.0f, 0.0f,   0.0f},
    {4,  165.1f, -295.275f,  501.65f, 180.0f, 0.0f, -30.0f},
    {5, -444.5f,  -63.5f,  1282.7f,  0.0f, 10.0f, -90.0f},
    {6, -508.0f,  -12.7f,  1282.7f,  0.0f, 10.0f,-150.0f},
    {7, -508.0f,   25.4f,  1282.7f, 180.0f, 10.0f, 150.0f},
    {8, -444.5f,   63.5f,  1282.7f, 180.0f, 10.0f,  90.0f},
};
```

Each VL53L7CX has a 4×4 grid of 16 zones. Cell direction vectors are precomputed in the sensor frame using 60°×60° FOV geometry, with row/column flipped to match the VL53L7CX lens inversion (per datasheet).

### Target Status Filtering

Only cells with `targetStatus == 5` (VL53L7CX 100%-confidence flag) produce valid world points. All other statuses (noise, wrap-around, sigma failure, no target) are discarded. This prevents phantom obstacle alerts.

---

## Zone Modes

The LED ring is divided into angular sectors. The caregiver can choose 4, 6, or 8 zones from the browser UI.

| Mode | Zones | Zone width |
|---|---|---|
| 4-zone | N, E, S, W | 90° each |
| 6-zone (default) | AHEAD, TOP_RIGHT, BOTTOM_RIGHT, BEHIND, BOTTOM_LEFT, TOP_LEFT | 60° each |
| 8-zone | N, NE, E, SE, S, SW, W, NW | 45° each |

Zone 0 is always centered at 0° (forward). Zone numbering goes clockwise.

---

## LED Feedback Logic

1. **MainController** accumulates the closest valid obstacle per zone across all 128 cells (8 sensors × 16 cells), using floor-plane distance `sqrt(x² + y²)`.
2. Per-zone distances are broadcast in a `ZoneProximityPacket` every ~67 ms (15 Hz).
3. **CaregiverApp** receives the packet and maps each zone's distance to a ring:

| Distance | Ring | Color |
|---|---|---|
| < DIST_RING1 (default 300 mm) | 1 (center) | Red |
| 300–600 mm | 2 | Red |
| 600–1050 mm | 3 | Orange |
| 1050–1500 mm | 4 | Orange |
| 1500–1950 mm | 5 | Yellow |
| 1950–3000 mm | 6 | Yellow |
| > 3000 mm | — | Off (no obstacle) |

The center LED (ring 1) is omnidirectional — it lights regardless of which zone the obstacle is in.

4. The resulting `LedFrame_t` (94 bytes: brightness + 93 color codes) is sent via ESP-NOW to the LED Controller.

---

## ESP-NOW Packet Reference

All devices operate on ESP-NOW channel 1.

| Packet | Size | Direction | Description |
|---|---|---|---|
| `SensorPacket` | 69 B | Pod → MainController | One frame of 16-zone ToF data |
| `SensorPacketV2` | 75 B | Pod → MainController | Same + sequence number + send timestamp |
| `SyncRequestPacket` | 8 B | MainController → Pod | NTP-style clock sync request |
| `SyncResponsePacket` | 16 B | Pod → MainController | Clock sync response (t1/t2/t3) |
| `ZoneProximityPacket` | 34 B | MainController → CaregiverApp (broadcast) | Closest distance per zone |
| `ConfigPacket` | 10 B | CaregiverApp → MainController (broadcast) | Zone mode + brightness + thresholds |
| `LedFrame_t` | 94 B | CaregiverApp → LED Controller | Per-LED color codes + brightness |

The broadcast MAC (`FF:FF:FF:FF:FF:FF`) is used for `ZoneProximityPacket` and `ConfigPacket`. Unicast MACs are used for `LedFrame_t` (LED Controller).

---

## CaregiverApp WebSocket Protocol

The browser connects to `ws://<device-ip>/ws`. The device IP is `192.168.4.1` when connected to the ViviSense WiFi AP.

### Browser → ESP32

```json
{"type": "config",
 "zoneMode": 6,
 "brightness": 80,
 "redThreshold": 60,
 "yellowThreshold": 150,
 "activeSectors": [true, true, true, true, true, true],
 "audioEnabled": true,
 "visualEnabled": true}
```
Thresholds are in **cm**. `activeSectors` length must match `zoneMode`.

```json
{"type": "navigate", "action": "forward|backward|left|right|stop|speedup|slowdown|speak"}
```
Triggers the corresponding pre-recorded audio clip.

```json
{"type": "volume", "level": 200}
```
Volume 0–255.

```json
{"type": "preview", "active": true,
 "zoneMode": 6, "brightness": 80,
 "redThreshold": 60, "yellowThreshold": 150,
 "activeSectors": [true, true, true, true, true, true]}
```
Shows a static LED preview without obstacle detection. Send `{"type":"preview","active":false}` to return to live mode.

```json
{"type": "getConfig"}
```
Requests current settings; ESP32 replies with a `status` message.

### ESP32 → Browser

```json
{"type": "status",
 "zoneMode": 6,
 "brightness": 80,
 "redThreshold": 60,
 "yellowThreshold": 150,
 "audioEnabled": true,
 "visualEnabled": true,
 "activeSectors": [true, true, true, true, true, true]}
```

---

## Firmware Targets

| Firmware | Board | Key libraries |
|---|---|---|
| `BaseReciever` (MainController) | `esp32dev` | esp_now, esp_wifi |
| `WebserverV3` (CaregiverApp) | `esp32doit-devkit-v1` | AsyncTCP, ESPAsyncWebServer, ArduinoJson, esp_now |
| `LEDControllerV1` | `esp32doit-devkit-v1` | Adafruit NeoPixel, esp_now |
| `LeftPodSender` | `esp32dev` | VL53L7CX driver, esp_now |
| `RightPodSender` | `esp32dev` | VL53L7CX driver, esp_now |
| `BaseSender` (tower) | `esp32dev` | VL53L7CX driver, esp_now |

Build and flash with PlatformIO (`pio run -t upload`).

### First-time MAC Setup

1. Flash `LEDControllerV1`. Open Serial Monitor — it prints the device MAC.
2. Copy that MAC into `WebserverV3/src/main.cpp` → `LED_ESP32_MAC[]`.
3. Flash `WebserverV3`.

The MainController (BaseReceiver) uses broadcast for proximity packets, so no MAC configuration is needed there.

---

## Known Limitations (Beta)

- **Sensor poses are hardcoded** in `BaseReciever/src/main.cpp`. If the physical mount changes, edit `SENSOR_CONFIGS` and reflash.
- **LED controller MAC is hardcoded** in `WebserverV3/src/main.cpp`. Replacing the LED controller ESP32 requires editing `LED_ESP32_MAC` and reflashing.
- **Zone assignment uses angle only** — no height filtering. An obstacle above the wheelchair (e.g., a shelf being passed under) will trigger a proximity alert.
- **Audio clips are pre-recorded** and stored in SPIFFS. To add or replace clips, update `audio_files/`, run `python wav_to_header.py`, and re-upload the filesystem with `pio run -t uploadfs`.

---

## File Structure

```
Software/
├── ESP firmware/
│   ├── BaseReciever/          ← MainController firmware
│   │   ├── include/
│   │   │   ├── SensorAndPoint.h   ← sensor geometry + point math
│   │   │   └── led_frame.h        ← shared LED frame struct
│   │   └── src/main.cpp
│   ├── LeftPodSender/         ← left front pod sensor firmware
│   ├── RightPodSender/        ← right front pod sensor firmware
│   └── BaseSender/            ← tower pod sensor firmware
├── WebserverV3/               ← CaregiverApp firmware
│   ├── include/
│   │   ├── led_frame.h        ← shared LED frame struct (duplicate)
│   │   └── sounds.h           ← generated audio data (run wav_to_header.py)
│   ├── src/main.cpp
│   └── ui/                    ← browser UI source (Vite + TypeScript)
├── LEDControllerV1/           ← LED ring driver firmware
│   ├── include/led_frame.h    ← source of truth for LedFrame_t
│   └── src/main.cpp
└── Visualizer/                ← PC-side development tools (Python)
    ├── serial_point_cloud_viewer.py
    └── serial_occupancy_grid_viewer.py
```
