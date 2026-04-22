# ViviSense System Overview (Beta Plan)

ViviSense uses eight ToF sensors to detect obstacles around a wheelchair and render feedback on a 93-LED ring (6 rings + center LED).

## Core Architecture

1. `LeftPodSender_ArduinoIDE` and `RightPodSender_ArduinoIDE` send raw `SensorPacket` frames to `MainController`.
2. `MainController` transforms points into wheelchair coordinates, runs **polar occupancy filtering**, and computes LED colors/modes.
3. `MainController` sends `LedRenderFramePacket` over ESP-NOW.
4. `LEDRingController` only applies render frames (plus app preview frames).

This keeps decision logic in one place and makes sector + radar use the same obstacle pipeline.

## Control / App Path

1. Browser UI talks to `CaregiverApp` over WebSocket.
2. `CaregiverApp` sends config + detection-mode packets to `MainController` (unicast).
3. `CaregiverApp` can still send preview `LedFrame_t` frames directly to LED controller for comm testing.
4. Navigation/audio controls remain in UI, but audio playback is currently disabled in firmware for beta focus.

## Detection Model

- Detection is **polar-only** in `MainController`.
- Allowed zone counts are only `4`, `6`, `8`.
- Temporal smoothing uses confidence rise/decay + enter/exit hysteresis.
- Stale sensor timeout is used to drop disconnected sensors.

## LED Rendering Modes

- `led_mode = 0` sector-fill:
  - Find closest obstacle in each active sector.
  - Fill outward sector rings with that color.
- `led_mode = 1` radar:
  - Light only the ring/zone cells corresponding to occupied polar bins.

Main controller decides the frame; LED controller only applies.

## Default Runtime Parameters

Edit these defaults in:

- `Software/ESP firmware/MainController/include/runtime_defaults.h`

Key defaults:

- ESP-NOW channel: `1`
- Default zones: `6`
- Default brightness: `40` (0..64)
- Visual enabled: `true`
- Active sectors mask: `0xFF`
- Stale timeout: `400 ms`
- Render period: `67 ms`
- Polar grid: `6 rings x 8 max zones`
- Polar filter: `rise=90`, `decay=24`, `enter=120`, `exit=80`
- Max obstacle range: `3000 mm`

## Firmware Projects in Beta

- `Software/ESP firmware/MainController` (PlatformIO)
- `Software/ESP firmware/LEDRingController` (PlatformIO)
- `Software/CaregiverApp` (PlatformIO)
- `Software/ESP firmware/LeftPodSender_ArduinoIDE/LeftPodSender_ArduinoIDE.ino`
- `Software/ESP firmware/RightPodSender_ArduinoIDE/RightPodSender_ArduinoIDE.ino`
- `Software/ESP firmware/LEDRingController_ArduinoIDE/LEDRingController_ArduinoIDE.ino`

## Visualizer

For demo/tuning of the polar filter:

- `Software/Visualizer/serial_polar_demo_viewer.py`

This shows:

- Left panel: raw polar occupancy from live points.
- Right panel: filtered polar occupancy with smoothing parameters.

## Bring-up Guide

Use:

- `Software/RUNNING_THE_SYSTEM.md`

for full flash order and startup steps.

