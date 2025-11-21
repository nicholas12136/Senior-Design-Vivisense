# 🚀 Senior Design Group [3] - Project Dashboard

Welcome to the central hub for our Senior Design project.

## 🔗 Access Points (Quick Links)
| Tool | Usage | Link |
| :--- | :--- | :--- |
| **📂 OneDrive** | Documentation, BOMs, & Heavy Data | [**Click to Open Shared Drive**]|https://siuecougars-my.sharepoint.com/:f:/r/personal/dafowle_siue_edu/Documents/School/Fall%202025/Senior%20Design?csf=1&web=1&e=I8oYF6|

| **⚙️ Fusion 360** | CAD Models & Mechanical Design | [**View Team Hub on Web**](PASTE_FUSION_LINK_HERE) |

---

## 📂 Repository Structure

### 1. Firmware (`/firmware`)
* **Standard:** ESP32 C++ using PlatformIO.
* **Setup:** Open this folder in VS Code. Ensure you have the PlatformIO extension installed.
* **Note:** Do not commit the `.pio` folder.

### 2. Simulation (`/simulation`)
* **Tool:** MATLAB.
* **Data:** The heavy Point Cloud data (`.pcd` / `.las`) is **NOT** in this repo.
* **How to Run:**
    1. Download the data from the [OneDrive Data Folder](PASTE_ONEDRIVE_LINK_HERE).
    2. Place it in your local `simulation/data` folder (Git is set to ignore these files).
    3. Run `main_script.m`.

### 3. Mechanical (`/mechanical`)
* Contains **Exports only** (STLs for printing, PDFs for drawings).
* For live editing of the 3D models, use the Fusion 360 link above.

---

## 🛠 Setup Guide for New Members
1. **Clone this Repo:** Use GitHub Desktop to clone `main`.
2. **Get the Data:** Go to the OneDrive link and download the "Large Assets" folder.
3. **Install VS Code Extensions:**
    * PlatformIO IDE (for ESP32)
    * MATLAB (optional, for syntax highlighting)