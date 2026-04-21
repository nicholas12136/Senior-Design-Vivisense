# Running ViviSense (Beta Self-Contained)

This guide explains exactly what to flash, configure, and start to run the current firmware stack.

---

## 1. Prerequisites

- Windows machine with USB access to all ESP32 boards
- PlatformIO (VS Code extension or CLI)
- Node.js (for CaregiverApp web UI build)
- Known board mappings for:
  - MainController ESP32
  - LEDRingController ESP32
  - CaregiverApp ESP32
  - Sensor pod ESP32 boards (LeftPodSender, RightPodSender, BaseSender)

Repo root in this guide:

- `c:\Users\damon\School\Fall 2025\Senior-Design-Vivisense`

---

## 2. Build Caregiver UI Assets

CaregiverApp serves static UI files from SPIFFS, so build the UI before uploading filesystem data.

```powershell
cd "c:\Users\damon\School\Fall 2025\Senior-Design-Vivisense\Software\CaregiverApp\ui"
npm install
npm run build
```

This writes assets into:

- `Software/CaregiverApp/data`

---

## 3. Flash LEDRingController First (to get MAC)

```powershell
cd "c:\Users\damon\School\Fall 2025\Senior-Design-Vivisense\Software\LEDRingController"
pio run -t upload
pio device monitor -b 115200
```

Copy the printed MAC from monitor output:

- `[ESP-NOW] LED ESP32 MAC address: XX:XX:XX:XX:XX:XX`

Set this MAC in:

- `Software/CaregiverApp/src/main.cpp`
- Constant: `LED_ESP32_MAC`

Reflash CaregiverApp after changing it.

---

## 4. Flash CaregiverApp

```powershell
cd "c:\Users\damon\School\Fall 2025\Senior-Design-Vivisense\Software\CaregiverApp"
pio run -t upload
pio run -t uploadfs
```

Optional monitor:

```powershell
pio device monitor -b 115200
```

Defaults in firmware:

- SSID: `ViviSense`
- Password: `ViviSense123`
- AP IP: `192.168.4.1`
- ESP-NOW channel: `1`

---

## 5. Flash MainController

```powershell
cd "c:\Users\damon\School\Fall 2025\Senior-Design-Vivisense\Software\ESP firmware\MainController"
pio run -t upload
```

Optional monitor:

```powershell
pio device monitor -b 115200
```

MainController broadcasts live zone proximity packets at about 15 Hz.

---

## 6. Flash Sensor Pod Firmware

Flash all pod firmwares that are part of your hardware setup.

Left pod:

```powershell
cd "c:\Users\damon\School\Fall 2025\Senior-Design-Vivisense\Software\ESP firmware\LeftPodSender"
pio run -t upload
```

Right pod:

```powershell
cd "c:\Users\damon\School\Fall 2025\Senior-Design-Vivisense\Software\ESP firmware\RightPodSender"
pio run -t upload
```

Tower/base sender:

```powershell
cd "c:\Users\damon\School\Fall 2025\Senior-Design-Vivisense\Software\ESP firmware\BaseSender"
pio run -t upload
```

---

## 7. Power-Up Order (Recommended)

1. LEDRingController
2. MainController
3. Sensor pods
4. CaregiverApp
5. Phone/tablet connects to `ViviSense` WiFi

Then open:

- `http://192.168.4.1`

Use Feedback Config page to verify:

- `visualEnabled` on
- correct zone mode (4/6/8)
- expected thresholds and active sectors

---

## 8. Quick Functional Check

1. In Caregiver UI, toggle `Display` preview on/off and confirm ring updates immediately.
2. Exit preview; ring should resume live obstacle behavior.
3. Move an obstacle in front/right/left and verify directional sector response.
4. Trigger a navigation action and verify audio output on MAX98357A.

---

## 9. Troubleshooting

- No UI page:
  - Re-run `npm run build` in `CaregiverApp/ui`
  - Re-run `pio run -t uploadfs` in `CaregiverApp`

- Preview works, live does not:
  - Check MainController is powered and broadcasting
  - Check both MainController and LEDRingController are on ESP-NOW channel 1
  - Ensure CaregiverApp is not stuck in preview mode

- No LED preview frames:
  - Recheck `LED_ESP32_MAC` in `CaregiverApp/src/main.cpp`
  - Reflash CaregiverApp after MAC update

- Audio missing:
  - Verify I2S wiring (BCK=27, WS=26, DO=25)
  - Confirm amp power and speaker wiring

