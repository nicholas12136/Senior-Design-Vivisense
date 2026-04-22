# Running ViviSense Beta (Current Workflow)

This is the updated bring-up flow for the beta architecture:

- Sensors -> `MainController` (compute) -> `LEDRingController` (apply frame)
- App config -> `MainController`
- App preview frame -> `LEDRingController` (kept for comm testing)

## 1. Build Caregiver UI Assets

```powershell
cd "C:\Users\damon\School\Fall 2025\Senior-Design-Vivisense\Software\CaregiverApp\ui"
npm install
npm run build
```

## 2. Flash Firmware

### 2.1 MainController (PlatformIO)

```powershell
cd "C:\Users\damon\School\Fall 2025\Senior-Design-Vivisense\Software\ESP firmware\MainController"
pio run -t upload
```

Optional monitor:

```powershell
pio device monitor -b 115200
```

### 2.2 CaregiverApp (PlatformIO)

```powershell
cd "C:\Users\damon\School\Fall 2025\Senior-Design-Vivisense\Software\CaregiverApp"
pio run -t upload
pio run -t uploadfs
```

### 2.3 LEDRingController (Arduino IDE)

Open and upload:

- `Software/ESP firmware/LEDRingController_ArduinoIDE/LEDRingController_ArduinoIDE.ino`

Board/channel notes:

- Keep ESP-NOW channel at `1`
- Confirm printed LED MAC in serial monitor and keep `LED_ESP32_MAC` in `CaregiverApp/src/main.cpp` in sync
- Keep `MAIN_CONTROLLER_MAC` in `CaregiverApp/src/main.cpp` aligned with the real MainController MAC

### 2.4 Left Pod (Arduino IDE)

Open and upload:

- `Software/ESP firmware/LeftPodSender_ArduinoIDE/LeftPodSender_ArduinoIDE.ino`

This sketch sends sensor IDs `1` and `2`.

### 2.5 Right Pod (Arduino IDE)

Open and upload:

- `Software/ESP firmware/RightPodSender_ArduinoIDE/RightPodSender_ArduinoIDE.ino`

This sketch sends sensor IDs `3` and `4`.

## 3. Power-Up Order

1. `LEDRingController`  
2. `MainController`  
3. Left and right pod senders  
4. `CaregiverApp`  
5. Connect phone/laptop to WiFi `ViviSense` / `ViviSense123`  
6. Open `http://192.168.4.1`

## 4. Runtime Defaults and Easy Tuning

Primary default values are centralized in:

- `Software/ESP firmware/MainController/include/runtime_defaults.h`

Use this file for:

- Default zone count (4/6/8)
- Brightness
- Visual enabled default
- Stale timeout
- Update rate
- Polar smoothing defaults
- Max range

Live serial tuning (MainController serial monitor) still supports:

- `GET`
- `SET,zones,<4|6|8>`
- `SET,bright,<0..64>`
- `SET,visual,<0|1>`
- `SET,sectors_mask,<0..255>`
- `SET,red_mm,<value>`
- `SET,orange_mm,<value>`
- `SET,yellow_mm,<value>`
- `SET,stale_ms,<value>`
- `SET,status_ms,<value>`
- `SET,proximity_ms,<value>`
- `SET,led_mode,<0|1>`
- `SET,polar_rise,<1..255>`
- `SET,polar_decay,<0..255>`
- `SET,polar_enter,<1..255>`
- `SET,polar_exit,<0..254>`

Notes:

- Detection mode is forced to polar in beta.
- `led_mode=0` is sector-fill, `led_mode=1` is radar.

## 5. Polar Demo Visualizer

Run:

```powershell
cd "C:\Users\damon\School\Fall 2025\Senior-Design-Vivisense\Software\Visualizer"
python serial_polar_demo_viewer.py --port COMx --baud 115200
```

What it shows:

- Left: raw polar occupancy from live point stream
- Right: filtered polar occupancy (same data, smoothing applied)

Adjust rings/zones/range and filter params live for demos/tuning.

## 6. Quick Verification Checklist

1. Move obstacle ahead/right/left and verify LED response updates.
2. Toggle sector/radar mode in app and confirm LED behavior changes.
3. Enable preview in app and verify direct app->LED path still works.
4. Disable preview and confirm live MainController rendering resumes.
