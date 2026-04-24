# 🚀 Senior Design Group [3] - Vivisense Project

Welcome to the Vivisense Senior Design project repository. This project consists of embedded firmware, MATLAB simulation, and a caregiver application.

## 🔗 Quick Links
| Resource | Purpose | Link |
| :--- | :--- | :--- |
| **📂 OneDrive** | Documentation, BOMs, & Heavy Data | [Shared Drive](https://siuecougars-my.sharepoint.com/:f:/r/personal/dafowle_siue_edu/Documents/School/Fall%202025/Senior%20Design?csf=1&web=1&e=I8oYF6) |
| **⚙️ Fusion 360** | CAD Models & Mechanical Design | [View on Web](PASTE_FUSION_LINK_HERE) |

---

## 📂 Repository Structure

```
Senior-Design-Vivisense/
├── README.md                          # This file
├── mechanical/                        # 3D models and mechanical design
│   ├── Viusal Unit Print Files/      # STEP files for 3D printing
│   └── damon place stl or stp here.txt
├── simulation/                        # MATLAB simulation and visualization
│   ├── PointCloud.m
│   ├── matlab_visualizer/            # MATLAB point cloud viewer
│   │   ├── mainPlot.m
│   │   ├── Sensor.m
│   │   ├── getXYZdata.m
│   │   ├── OutputXYZVAluesToCSV.m
│   │   ├── Test.m
│   │   └── Data files (CSV)
│   └── Animations/
└── Software/                          # Application & embedded firmware
    ├── RUNNING_THE_SYSTEM.md         # Start here for setup
    ├── SYSTEM_OVERVIEW.md            # Architecture overview
    ├── AUDIO_FEEDBACK_PLAN.md        # Audio feedback specifications
    ├── CaregiverApp/                 # Web-based caregiver interface
    │   ├── platformio.ini
    │   ├── src/main.cpp              # Embedded C++ code
    │   ├── include/                  # Header files
    │   ├── ui/                       # Vue.js frontend
    │   └── data/                     # Compiled web assets
    ├── ESP firmware/                 # ESP32 microcontroller firmware
    │   ├── MainController/           # Central controller (PlatformIO)
    │   ├── BaseSender/               # Base station firmware
    │   ├── LEDRingController/        # LED ring control
    │   ├── Mac Scanner/              # Bluetooth scanning
    │   └── *_ArduinoIDE/             # Arduino IDE versions
    └── Visualizer/                   # Python visualization tools
        ├── playback_point_cloud_csv.py
        ├── playback_occupancy_grid_csv.py
        ├── serial_point_cloud_viewer.py
        ├── serial_occupancy_grid_viewer.py
        ├── latency_monitor.py
        ├── requirements.txt
        └── Recordings/               # Sample data files
```

---

## 🚀 Getting Started

### 1. **First-Time Setup**
1. Clone this repository: `git clone <repo-url>`
2. Download large data files from [OneDrive](PASTE_ONEDRIVE_LINK_HERE) and place in appropriate `data/` folders
3. Install required software:
   - **VS Code** with PlatformIO IDE extension (for ESP32 firmware)
   - **MATLAB** (for simulation)
   - **Python 3.8+** (for visualization tools)

### 2. **Running the System**
See [Software/RUNNING_THE_SYSTEM.md](Software/RUNNING_THE_SYSTEM.md) for detailed instructions.

### 3. **Understanding the Architecture**
See [Software/SYSTEM_OVERVIEW.md](Software/SYSTEM_OVERVIEW.md) for system architecture and component descriptions.

---

## 📋 Project Components

### **Mechanical** (`/mechanical`)
- **Purpose:** 3D models for the physical housing and LED ring unit
- **Format:** STEP files (Fusion 360 exports) for 3D printing
- **For editing:** Use Fusion 360 (link above)
- **Note:** STL/STP files only; source CAD is managed in Fusion 360

### **Simulation** (`/simulation`)
- **Purpose:** MATLAB-based point cloud and occupancy grid simulation
- **Tools:** MATLAB with Phased Array toolbox
- **Data:** CSV and sample output files in subdirectories
- **Large data:** Not included in repo; download from OneDrive
- **Run:** Execute `mainPlot.m` in `matlab_visualizer/`

### **Software** (`/Software`)

#### **CaregiverApp** - Web-based caregiver interface
- **Frontend:** Vue.js TypeScript SPA (in `ui/`)
- **Backend:** C++ embedded system (in `src/`)
- **Build:** PlatformIO
- **Run:** See RUNNING_THE_SYSTEM.md

#### **ESP Firmware** - Embedded controllers for ESP32
- **MainController:** Primary sensor data aggregator & processor
- **BaseSender:** Base station transceiver
- **LEDRingController:** LED ring status indicators
- **Mac Scanner:** Bluetooth device discovery
- **Build tool:** PlatformIO (preferred) or Arduino IDE

#### **Visualizer** - Python visualization utilities
- **Point cloud viewer:** Real-time 3D point cloud display
- **Occupancy grid viewer:** 2D occupancy map visualization
- **Serial data streaming:** Live sensor data from hardware
- **Playback tools:** Analyze recorded CSV data
- **Requirements:** See `requirements.txt`

---

## 🛠 Development Notes

- **PlatformIO:** Recommended over Arduino IDE for multi-file projects
- **Git ignore:** `.pio/` and `data/` folders are ignored
- **Large files:** Download separately from OneDrive (not in repo)
- **Python virtual environment:** Recommended for Visualizer tools
- **Documentation:** See Software/ folder for technical docs