<table border="0">
  <tr>
    <td valign="center">
      <img src=".images/vivisense-logo.png" width="110">
    </td>
    <td>
      <h1>ViviSense — Wheelchair Obstacle Awareness System</h1>
      <blockquote>
        SIUE Senior Design Group 3 · A real-time obstacle detection and feedback system for wheelchairs
      </blockquote>
    </td>
  </tr>
</table>

[![Platform](https://img.shields.io/badge/Platform-ESP32-blue)](https://www.espressif.com/en/products/socs/esp32)
[![Language](https://img.shields.io/badge/Language-C++-orange)](https://isocpp.org/)
[![Visualizer](https://img.shields.io/badge/Visualizer-Python-yellow)](https://www.python.org/)
[![Protocol](https://img.shields.io/badge/Wireless-ESP--NOW-lightgrey)](https://www.espressif.com/en/solutions/low-power-solutions/esp-now)

---

## Project Overview

ViviSense is an obstacle-awareness system designed to improve the safety of wheelchair users. The system mounts multiple [**Time-of-Flight (ToF) sensors**](https://www.pololu.com/product/3418) to the wheelchair, continuously maps the surrounding environment into a **polar occupancy grid**, and communicates obstacle proximity through three channels simultaneously:

- **An LED ring** mounted to the chair that shows direction and severity of nearby obstacles
- **Audio cues** that warn the user as obstacles enter the danger zone
- **A caregiver web app** accessible over Wi-Fi, showing live sector status and allowing remote configuration

The core philosophy of the system is clean separation of concerns: sensors *measure*, `MainController` *decides*, and everything else *displays or controls*.

<p align="center">
  <img src=".images/ViviSensePoster.png" width="750">
</p>

<p align="center">
  <img src=".images/ViviSense Architecture.png" width="750">
</p>

> *ViviSense end-to-end system architecture.*

---

## The Design Journey

### Physical Design

Before any firmware was written, the physical sensor pod housings were designed in CAD and the sensor field-of-view coverage was mapped out to determine optimal mounting positions on the chair.

<table border="0">
  <tr>
    <td width="50%" align="center">
      <img src=".images/cad-model.jpg" width="90%">
      <br><i>CAD model of the sensor pod enclosure (Fusion 360).</i>
    </td>
    <td width="50%" align="center">
      <img src=".images/area-coverage.jpg" width="90%">
      <br><i>Sensor field-of-view coverage around the wheelchair.</i>
    </td>
  </tr>
</table>

---

### Pre-Alpha — Proving the Concept

The project started with a bench-top sensor validation rig. Before mounting anything to a wheelchair, we needed to confirm the ToF sensors could detect obstacles reliably and that ESP-NOW wireless communication between pods and the main controller was stable.

<table border="0">
  <tr>
    <td width="50%" align="center">
      <img src=".images/pre-alpha-setup1.jpg" width="90%">
      <br><i>Early bench setup for sensor validation and firmware bring-up.</i>
    </td>
    <td width="50%" align="center">
      <img src=".images/pre-alpha-setup2.jpg" width="90%">
      <br><i>Testing point cloud geometry before mounting hardware to the chair.</i>
    </td>
  </tr>
</table>

---

### Alpha — First Hardware Integration

With sensor communication validated, we moved to integrating the pods onto an actual wheelchair. The alpha build introduced the physical sensor pod enclosures, the LED ring, and the first end-to-end run of the full pipeline — from raw ToF data all the way to lit LEDs on the chair.

<table border="0">
  <tr>
    <td width="50%" align="center">
      <img src=".images/alpha-wheelchair-pic1.jpg" width="90%">
      <br><i>Alpha hardware integration — sensor pods and LED ring mounted to the chair.</i>
    </td>
    <td width="50%" align="center">
      <img src=".images/alpha-wheelchair-pic2.jpg" width="90%">
      <br><i>Full alpha system running for the first time with live obstacle feedback.</i>
    </td>
  </tr>
</table>

---

### Final System

The final design refined the physical pod enclosures, tuned the polar occupancy algorithm for stable real-world behavior, completed the caregiver web UI, and added audio feedback. The result is a fully integrated system that works across all three feedback channels simultaneously.

<table border="0">
  <tr>
    <td width="50%" align="center">
      <img src=".images/pod-interals.png" width="90%">
      <br><i>Inside a sensor pod — ToF sensor array and ESP32 module.</i>
    </td>
    <td width="50%" align="center">
      <img src=".images/final-wheelchair.jpg" width="90%">
      <br><i>The completed ViviSense system on the final wheelchair.</i>
    </td>
  </tr>
</table>

---

## How It Works

ViviSense processes obstacle data through a multi-stage pipeline:

1. **Sense** — Each sensor pod reads a grid of distances from its ToF array and transmits a `SensorPacket` wirelessly via ESP-NOW.
2. **Convert** — `MainController` receives packets and converts each valid range cell into a world-frame 3D point relative to the wheelchair (`+X` forward, `+Y` left, `+Z` up).
3. **Filter** — Points outside the height window (floor/ceiling cutoffs), below minimum distance, or with invalid target status are discarded.
4. **Classify** — Valid points are placed into a polar occupancy grid (12 rings × 24 angular zones). Each cell accumulates confidence over time — preventing flicker through rise/decay hysteresis.
5. **Output** — Occupancy drives the LED ring (sector or radar mode), audio proximity logic, and the caregiver web UI simultaneously.

<p align="center">
  <img src=".images/3d-point-cloud.png" width="65%">
  <br><i>Live 3D point cloud — raw sensor data visualized in world-frame coordinates.</i>
</p>

### LED Display Modes

`MainController` supports two visual modes, both driven by the same polar occupancy grid:

<table border="0">
  <tr>
    <td width="50%" align="center">
      <img src=".images/sector-mode-img.png" width="90%">
      <br><i><b>Sector Mode</b> — the ring is divided into angular sectors (4, 6, or 8). Each lights up with severity color based on the nearest obstacle in that direction.</i>
    </td>
    <td width="50%" align="center">
      <img src=".images/radar-mode-img.png" width="90%">
      <br><i><b>Radar Mode</b> — individual polar bins are drawn by angle and distance, giving a fine-grained sweep-like display.</i>
    </td>
  </tr>
</table>

### Occupancy Grid Visualization

<table border="0">
  <tr>
    <td width="50%" align="center">
      <img src=".images/2d-grid.png" width="90%">
      <br><i>2D polar occupancy grid — each cell shows which sensor "owns" that zone.</i>
    </td>
    <td width="50%" align="center">
      <img src=".images/demo-video.gif" width="90%">
      <br><i>Live presentation viewer showing the occupancy grid updating in real-time.</i>
    </td>
  </tr>
</table>

---

## Tech Stack

| Layer | Technology |
| :--- | :--- |
| **Sensor Pods** | ESP32 + VL53L5CX Time-of-Flight ranging arrays |
| **Wireless** | ESP-NOW (low-latency, connectionless) |
| **Main Controller** | ESP32 (C++ / PlatformIO) |
| **LED Ring** | NeoPixel ring driven by a dedicated ESP32 |
| **Caregiver App** | ESP32 Wi-Fi AP + C++ HTTP server + Vue.js TypeScript SPA |
| **Visualization** | Python 3 (matplotlib, numpy, pyserial) |
| **CAD** | Fusion 360 |
| **Build System** | PlatformIO (firmware), npm (UI) |

---

## Repository Structure

| Folder | Purpose |
| :--- | :--- |
| [`/Software/ESP firmware`](./Software/ESP%20firmware) | All ESP32 firmware — main controller, sensor pods, LED ring, and presentation receiver |
| [`/Software/CaregiverApp`](./Software/CaregiverApp) | ESP32 Wi-Fi AP + Vue.js caregiver web interface |
| [`/Software/Visualizer`](./Software/Visualizer) | Python tools: point cloud, occupancy grid, polar grid, latency monitor |
| [`/mechanical`](./mechanical) | 3D print files (STEP/STL) for sensor pod housings |
| [`/simulation`](./simulation) | MATLAB point cloud and occupancy grid simulation |
| [`/.images`](./.images) | Project images and media |
| **[OneDrive Hub]** | [**Click here for OneDrive**](https://siuecougars-my.sharepoint.com/:f:/r/personal/dafowle_siue_edu/Documents/School/Fall%202025/Senior%20Design?csf=1&web=1&e=I8oYF6) |

---

## Getting Started

<details>
<summary><b>Click to expand: Full Setup Guide</b></summary>

### Prerequisites

- **VS Code** with the [PlatformIO IDE](https://platformio.org/) extension — for ESP32 firmware
- **Arduino IDE** — for LED ring and sensor pod firmware
- **Node.js / npm** — for building the caregiver Vue.js UI
- **Python 3.11+** — for the visualizer tools (`pip install -r Software/Visualizer/requirements.txt`)
- **MATLAB** — for the simulation (optional)

---

### 1. Build the Caregiver UI

```powershell
cd Software/CaregiverApp/ui
npm install
npm run build
```

---

### 2. Flash Firmware

**MainController** (PlatformIO):
```powershell
cd "Software/ESP firmware/MainController"
pio run -t upload
```

**CaregiverApp** (PlatformIO):
```powershell
cd Software/CaregiverApp
pio run -t upload
pio run -t uploadfs
```

**LEDRingController** — open and upload via Arduino IDE:
```
Software/ESP firmware/LEDRingController_ArduinoIDE/LEDRingController_ArduinoIDE.ino
```

**Sensor Pods** — open and upload via Arduino IDE:
```
Software/ESP firmware/LeftPodSender_ArduinoIDE/LeftPodSender_ArduinoIDE.ino
Software/ESP firmware/RightPodSender_ArduinoIDE/RightPodSender_ArduinoIDE.ino
```

---

### 3. Power-Up Order

1. LEDRingController
2. MainController
3. Left and right sensor pods
4. CaregiverApp
5. Connect phone/laptop to Wi-Fi: **`ViviSense`** / password **`ViviSense123`**
6. Open **`http://192.168.4.1`** in a browser

---

### 4. Run the Visualizer Tools

```powershell
cd Software/Visualizer
pip install -r requirements.txt

# Live polar grid presentation viewer
python serial_presentation_polar_grid_viewer.py --port COM5 --baud 230400

# Live point cloud
python serial_point_cloud_viewer.py --port COM5

# Playback from CSV recording
python playback_occupancy_grid_csv.py path/to/recording.csv
```

</details>

---

## Key Takeaways

<details>
<summary><b>Click to expand: Lessons Learned & Design Decisions</b></summary>

- **Polar-only architecture paid off.** Early iterations explored both Cartesian and polar obstacle models. Consolidating to a single polar occupancy grid simplified tuning significantly — one set of parameters now governs LED output, audio, and the presentation visualizer simultaneously.

- **Hysteresis is essential.** Without confidence rise/decay on each occupancy cell, the LED ring flickered badly in real-world noise. The `polar_rise`/`polar_decay`/`polar_enter`/`polar_exit` parameter set made behavior dramatically more stable.

- **Separation of concerns between controllers mattered.** Keeping obstacle *logic* in `MainController` and obstacle *display* in `LEDRingController` made both easier to debug in isolation. A bad LED pattern could be traced to the render layer without questioning the detection pipeline.

- **Height filtering (`floor_z_mm` / `ceiling_z_mm`) is surprisingly impactful.** Without a ceiling cutoff, the sensors reliably detected the user's own arms and lap. Without a floor cutoff, floor-level reflections created phantom obstacles. Tuning the vertical window dramatically cleaned up real-world behavior.

- **ESP-NOW was the right wireless choice.** Its connectionless, broadcast nature meant sensor pods could start sending immediately without a pairing sequence, and the latency was consistently below 10 ms.

</details>

---

## Further Reading

- [Software/SYSTEM_OVERVIEW.md](Software/SYSTEM_OVERVIEW.md) — Full architecture walkthrough and where to edit code
- [Software/RUNNING_THE_SYSTEM.md](Software/RUNNING_THE_SYSTEM.md) — Detailed bring-up checklist
- [Software/OCCUPANCY_GRID_TUNING_GUIDE.md](Software/OCCUPANCY_GRID_TUNING_GUIDE.md) — Parameter reference for tuning obstacle behavior
- [Software/AUDIO_FEEDBACK_PLAN.md](Software/AUDIO_FEEDBACK_PLAN.md) — Audio cue design and implementation notes
