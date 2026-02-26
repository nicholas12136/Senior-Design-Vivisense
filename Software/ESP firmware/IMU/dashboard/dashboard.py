"""
=====================================================================
MRE481 Wheelchair Pod Monitoring Dashboard
=====================================================================
Reads serial output from the tower pod ESP32 and displays:
  - ESP-NOW connection status for each front pod
  - Current pitch and roll angles for all three pods
  - Knock status with visual indicators

Requirements:
  pip install pyserial dearpygui

Usage:
  python dashboard.py

Make sure the tower pod is plugged in via USB on COM3 before running.
=====================================================================
"""

import serial
import threading
import time
import dearpygui.dearpygui as dpg

# ---------------------------------------------------------------------
# CONFIGURATION
# ---------------------------------------------------------------------
COM_PORT   = "COM3"
BAUD_RATE  = 115200
# ---------------------------------------------------------------------

# Shared data dict updated by the serial thread, read by the GUI thread
pod_data = {
    "connected":        False,  # serial port connected
    "calibrated":       False,
    "status_message":   "Connecting to tower pod...",
    "tower_pitch":      0.0,
    "tower_roll":       0.0,
    "pod1_pitch":       0.0,
    "pod1_roll":        0.0,
    "pod1_status":      "DISCONNECTED",
    "pod2_pitch":       0.0,
    "pod2_roll":        0.0,
    "pod2_status":      "DISCONNECTED",
}

data_lock = threading.Lock()

# ---------------------------------------------------------------------
# SERIAL READER THREAD
# Runs in the background, continuously reads and parses serial lines
# ---------------------------------------------------------------------
def serial_reader():
    global pod_data
    while True:
        try:
            with serial.Serial(COM_PORT, BAUD_RATE, timeout=2) as ser:
                with data_lock:
                    pod_data["connected"] = True
                    pod_data["status_message"] = "Connected to tower pod on " + COM_PORT

                while True:
                    line = ser.readline().decode("utf-8", errors="ignore").strip()

                    if not line:
                        continue

                    # Lines starting with # are status/debug messages
                    if line.startswith("#"):
                        msg = line[1:].strip()
                        with data_lock:
                            pod_data["status_message"] = msg
                            if "Calibration complete" in msg:
                                pod_data["calibrated"] = True
                        continue

                    # Parse DATA lines
                    if line.startswith("DATA,"):
                        parts = line.split(",")
                        if len(parts) == 9:
                            try:
                                with data_lock:
                                    pod_data["tower_pitch"] = float(parts[1])
                                    pod_data["tower_roll"]  = float(parts[2])
                                    pod_data["pod1_pitch"]  = float(parts[3])
                                    pod_data["pod1_roll"]   = float(parts[4])
                                    pod_data["pod1_status"] = parts[5]
                                    pod_data["pod2_pitch"]  = float(parts[6])
                                    pod_data["pod2_roll"]   = float(parts[7])
                                    pod_data["pod2_status"] = parts[8]
                            except ValueError:
                                pass

        except serial.SerialException as e:
            with data_lock:
                pod_data["connected"]      = False
                pod_data["calibrated"]     = False
                pod_data["status_message"] = "Serial error: " + str(e) + " — retrying..."
                pod_data["pod1_status"]    = "DISCONNECTED"
                pod_data["pod2_status"]    = "DISCONNECTED"
            time.sleep(3)

        except Exception as e:
            with data_lock:
                pod_data["status_message"] = "Unexpected error: " + str(e)
            time.sleep(3)

# ---------------------------------------------------------------------
# COLOR HELPERS
# ---------------------------------------------------------------------
def status_color(status):
    if status == "OK":
        return [0, 200, 80, 255]        # green
    elif status == "KNOCKED":
        return [220, 50, 50, 255]        # red
    else:
        return [180, 180, 180, 255]      # grey for disconnected

def connection_color(connected):
    return [0, 200, 80, 255] if connected else [220, 50, 50, 255]

# ---------------------------------------------------------------------
# GUI UPDATE — called every frame by Dear PyGui
# ---------------------------------------------------------------------
def update_gui():
    with data_lock:
        d = dict(pod_data)  # snapshot so we hold the lock minimally

    # --- Status bar ---
    conn_color = connection_color(d["connected"])
    dpg.configure_item("status_dot",   color=conn_color)
    dpg.configure_item("status_label", default_value=d["status_message"])

    # Calibration warning
    if d["connected"] and not d["calibrated"]:
        dpg.configure_item("calib_warning", show=True)
    else:
        dpg.configure_item("calib_warning", show=False)

    # --- Tower pod ---
    dpg.configure_item("tower_pitch_val", default_value=f"{d['tower_pitch']:.2f}°")
    dpg.configure_item("tower_roll_val",  default_value=f"{d['tower_roll']:.2f}°")

    # --- Pod 1 ---
    p1_color = status_color(d["pod1_status"])
    dpg.configure_item("pod1_status_text", default_value=d["pod1_status"], color=p1_color)
    dpg.configure_item("pod1_pitch_val",   default_value=f"{d['pod1_pitch']:.2f}°")
    dpg.configure_item("pod1_roll_val",    default_value=f"{d['pod1_roll']:.2f}°")
    dpg.configure_item("pod1_indicator",   color=p1_color)

    # --- Pod 2 ---
    p2_color = status_color(d["pod2_status"])
    dpg.configure_item("pod2_status_text", default_value=d["pod2_status"], color=p2_color)
    dpg.configure_item("pod2_pitch_val",   default_value=f"{d['pod2_pitch']:.2f}°")
    dpg.configure_item("pod2_roll_val",    default_value=f"{d['pod2_roll']:.2f}°")
    dpg.configure_item("pod2_indicator",   color=p2_color)

# ---------------------------------------------------------------------
# BUILD GUI
# ---------------------------------------------------------------------
def build_gui():
    dpg.create_context()

    with dpg.font_registry():
        pass  # use default font

    with dpg.theme() as global_theme:
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(dpg.mvThemeCol_WindowBg,      [18,  18,  24,  255])
            dpg.add_theme_color(dpg.mvThemeCol_ChildBg,       [28,  28,  38,  255])
            dpg.add_theme_color(dpg.mvThemeCol_FrameBg,       [40,  40,  55,  255])
            dpg.add_theme_color(dpg.mvThemeCol_Text,          [220, 220, 230, 255])
            dpg.add_theme_color(dpg.mvThemeCol_Border,        [60,  60,  80,  255])
            dpg.add_theme_style(dpg.mvStyleVar_WindowRounding, 8)
            dpg.add_theme_style(dpg.mvStyleVar_ChildRounding,  6)
            dpg.add_theme_style(dpg.mvStyleVar_FrameRounding,  4)
            dpg.add_theme_style(dpg.mvStyleVar_ItemSpacing,    8, 8)
            dpg.add_theme_style(dpg.mvStyleVar_WindowPadding,  16, 16)

    dpg.bind_theme(global_theme)

    with dpg.window(label="MRE481 Pod Monitor", tag="main_window",
                    no_close=True, no_collapse=True):

        # ── Title ──
        dpg.add_text("Wheelchair Pod Displacement Monitor", color=[100, 180, 255, 255])
        dpg.add_separator()
        dpg.add_spacer(height=4)

        # ── Connection status bar ──
        with dpg.group(horizontal=True):
            dpg.add_text("●", tag="status_dot", color=[180, 180, 180, 255])
            dpg.add_text("Connecting...", tag="status_label")

        dpg.add_text("⚠  Calibrating — hold all pods still...",
                     tag="calib_warning", color=[255, 200, 0, 255], show=False)

        dpg.add_spacer(height=10)
        dpg.add_separator()
        dpg.add_spacer(height=10)

        # ── Three pod panels side by side ──
        with dpg.group(horizontal=True):

            # --- Tower Pod panel ---
            with dpg.child_window(label="Tower Pod", width=220, height=160, border=True):
                dpg.add_text("TOWER POD", color=[100, 180, 255, 255])
                dpg.add_text("(Reference)", color=[140, 140, 160, 255])
                dpg.add_separator()
                dpg.add_spacer(height=6)
                with dpg.group(horizontal=True):
                    dpg.add_text("Pitch:", color=[180, 180, 200, 255])
                    dpg.add_text("0.00°", tag="tower_pitch_val")
                with dpg.group(horizontal=True):
                    dpg.add_text("Roll: ", color=[180, 180, 200, 255])
                    dpg.add_text("0.00°", tag="tower_roll_val")

            dpg.add_spacer(width=12)

            # --- Front Pod 1 panel ---
            with dpg.child_window(label="Front Pod 1", width=220, height=160, border=True):
                with dpg.group(horizontal=True):
                    dpg.add_text("●", tag="pod1_indicator", color=[180, 180, 180, 255])
                    dpg.add_text("FRONT POD 1", color=[100, 180, 255, 255])
                dpg.add_text("(Left)", color=[140, 140, 160, 255])
                dpg.add_separator()
                dpg.add_spacer(height=4)
                dpg.add_text("DISCONNECTED", tag="pod1_status_text",
                             color=[180, 180, 180, 255])
                dpg.add_spacer(height=4)
                with dpg.group(horizontal=True):
                    dpg.add_text("Pitch:", color=[180, 180, 200, 255])
                    dpg.add_text("0.00°", tag="pod1_pitch_val")
                with dpg.group(horizontal=True):
                    dpg.add_text("Roll: ", color=[180, 180, 200, 255])
                    dpg.add_text("0.00°", tag="pod1_roll_val")

            dpg.add_spacer(width=12)

            # --- Front Pod 2 panel ---
            with dpg.child_window(label="Front Pod 2", width=220, height=160, border=True):
                with dpg.group(horizontal=True):
                    dpg.add_text("●", tag="pod2_indicator", color=[180, 180, 180, 255])
                    dpg.add_text("FRONT POD 2", color=[100, 180, 255, 255])
                dpg.add_text("(Right)", color=[140, 140, 160, 255])
                dpg.add_separator()
                dpg.add_spacer(height=4)
                dpg.add_text("DISCONNECTED", tag="pod2_status_text",
                             color=[180, 180, 180, 255])
                dpg.add_spacer(height=4)
                with dpg.group(horizontal=True):
                    dpg.add_text("Pitch:", color=[180, 180, 200, 255])
                    dpg.add_text("0.00°", tag="pod2_pitch_val")
                with dpg.group(horizontal=True):
                    dpg.add_text("Roll: ", color=[180, 180, 200, 255])
                    dpg.add_text("0.00°", tag="pod2_roll_val")

        dpg.add_spacer(height=12)
        dpg.add_separator()
        dpg.add_spacer(height=6)

        # ── Legend ──
        dpg.add_text("Status Legend:", color=[140, 140, 160, 255])
        with dpg.group(horizontal=True):
            dpg.add_text("●", color=[0,   200, 80,  255])
            dpg.add_text("OK  ", color=[180, 180, 200, 255])
            dpg.add_text("●", color=[220, 50,  50,  255])
            dpg.add_text("KNOCKED  ", color=[180, 180, 200, 255])
            dpg.add_text("●", color=[180, 180, 180, 255])
            dpg.add_text("DISCONNECTED", color=[180, 180, 200, 255])

    dpg.create_viewport(title="MRE481 Pod Monitor", width=740, height=380,
                        resizable=False)
    dpg.setup_dearpygui()
    dpg.set_primary_window("main_window", True)
    dpg.show_viewport()

    # Main render loop
    while dpg.is_dearpygui_running():
        update_gui()
        dpg.render_dearpygui_frame()

    dpg.destroy_context()

# ---------------------------------------------------------------------
# ENTRY POINT
# ---------------------------------------------------------------------
if __name__ == "__main__":
    # Start serial reader in background thread
    t = threading.Thread(target=serial_reader, daemon=True)
    t.start()

    # Build and run the GUI (must be on main thread)
    build_gui()