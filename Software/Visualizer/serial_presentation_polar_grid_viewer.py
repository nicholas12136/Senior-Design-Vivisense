import argparse
import math
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.patches import Circle, Patch, Wedge
import serial
import serial.tools.list_ports


DEFAULT_BAUD = 230400
ONLINE_TIMEOUT_S = 1.5
DEFAULT_MAX_RANGE_M = 3.0

SENSOR_RGB = {
    1: (230, 57, 70),
    2: (244, 162, 97),
    3: (233, 196, 106),
    4: (42, 157, 143),
    5: (69, 123, 157),
    6: (29, 53, 87),
    7: (141, 153, 174),
    8: (106, 76, 147),
}


def list_ports():
    return list(serial.tools.list_ports.comports())


def polar_zone_center_plot_deg(zone_idx: int, zones: int) -> float:
    alpha_center = -180.0 + ((zone_idx + 0.5) * (360.0 / zones))
    # Rotate displayed grid by 180 degrees to match expected orientation.
    return (90.0 - alpha_center) + 180.0


class SerialReader:
    def __init__(self):
        self.rx_queue = queue.Queue(maxsize=4000)
        self._thread = None
        self._stop = threading.Event()
        self._serial = None
        self.connected = False

    def connect(self, port: str, baud: int):
        if self.connected:
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._worker, args=(port, baud), daemon=True)
        self._thread.start()

    def disconnect(self):
        self._stop.set()
        self.connected = False
        try:
            if self._serial is not None:
                self._serial.close()
        except Exception:
            pass
        self._serial = None

    def _put(self, line: str):
        try:
            self.rx_queue.put_nowait(line)
        except queue.Full:
            try:
                _ = self.rx_queue.get_nowait()
            except queue.Empty:
                pass
            try:
                self.rx_queue.put_nowait(line)
            except queue.Full:
                pass

    def _worker(self, port: str, baud: int):
        try:
            self._serial = serial.Serial(port, baud, timeout=0.1)
            self.connected = True
            self._put("INFO,open")
        except Exception as exc:
            self._put(f"ERR,open,{exc}")
            return

        while not self._stop.is_set():
            try:
                raw = self._serial.readline()
            except Exception as exc:
                self._put(f"ERR,read,{exc}")
                break
            if not raw:
                continue
            line = raw.decode("utf-8", errors="ignore").strip()
            if line:
                self._put(line)

        self.connected = False
        try:
            if self._serial is not None:
                self._serial.close()
        except Exception:
            pass
        self._serial = None
        self._put("INFO,closed")


class App:
    def __init__(self, root: tk.Tk, default_port: str | None, baud: int):
        self.root = root
        self.root.title("ViviSense Presentation Polar Grid Viewer")

        self.reader = SerialReader()
        self.owner_grid = np.zeros((12, 24), dtype=np.uint8)
        self.rings = 12
        self.zones = 24
        self.max_range_m = DEFAULT_MAX_RANGE_M
        self.last_frame_wall_s = 0.0
        self.last_source_ms = 0
        self.last_seq = -1
        self.frames_received = 0
        self.last_rate_sample_s = time.time()
        self.frames_in_rate_window = 0
        self.current_fps = 0.0

        self._build_ui(default_port, baud)
        self.refresh_ports()
        self.root.after(50, self._tick)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self, default_port: str | None, baud: int):
        top = ttk.Frame(self.root, padding=8)
        top.pack(side=tk.TOP, fill=tk.X)

        ttk.Label(top, text="COM").pack(side=tk.LEFT)
        self.port_var = tk.StringVar(value=default_port or "")
        self.port_combo = ttk.Combobox(top, textvariable=self.port_var, width=16, state="readonly")
        self.port_combo.pack(side=tk.LEFT, padx=(4, 8))
        ttk.Button(top, text="Refresh", command=self.refresh_ports).pack(side=tk.LEFT)
        ttk.Label(top, text="Baud").pack(side=tk.LEFT, padx=(10, 0))
        self.baud_var = tk.StringVar(value=str(baud))
        ttk.Entry(top, textvariable=self.baud_var, width=9).pack(side=tk.LEFT, padx=(4, 8))
        ttk.Button(top, text="Connect", command=self.connect).pack(side=tk.LEFT)
        ttk.Button(top, text="Disconnect", command=self.disconnect).pack(side=tk.LEFT, padx=(4, 0))
        self.conn_var = tk.StringVar(value="Disconnected")
        ttk.Label(top, textvariable=self.conn_var).pack(side=tk.LEFT, padx=(10, 0))

        body = ttk.Frame(self.root, padding=(8, 0, 8, 8))
        body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        sidebar = ttk.Frame(body)
        sidebar.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 12))

        ttk.Label(sidebar, text="Display").pack(anchor="w")
        self.range_var = tk.StringVar(value=f"{DEFAULT_MAX_RANGE_M:.2f}")
        range_row = ttk.Frame(sidebar)
        range_row.pack(anchor="w", pady=(4, 0))
        ttk.Label(range_row, text="Max Range (m)", width=14).pack(side=tk.LEFT)
        ttk.Entry(range_row, textvariable=self.range_var, width=8).pack(side=tk.LEFT)

        self.labels_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            sidebar,
            text="Show ring/zone labels",
            variable=self.labels_var,
            command=self._draw,
        ).pack(anchor="w", pady=(8, 0))

        ttk.Separator(sidebar, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=8)
        self.summary_var = tk.StringVar(value="Waiting for PG frames...")
        ttk.Label(sidebar, textvariable=self.summary_var, wraplength=280, justify=tk.LEFT).pack(anchor="w")

        ttk.Label(sidebar, text="Sensor Colors").pack(anchor="w", pady=(10, 4))
        for sid in sorted(SENSOR_RGB):
            row = ttk.Frame(sidebar)
            row.pack(anchor="w")
            rgb = SENSOR_RGB[sid]
            color_hex = "#{:02x}{:02x}{:02x}".format(*rgb)
            swatch = tk.Canvas(row, width=12, height=12, highlightthickness=0, bd=0)
            swatch.create_rectangle(0, 0, 12, 12, fill=color_hex, outline=color_hex)
            swatch.pack(side=tk.LEFT, padx=(0, 6))
            ttk.Label(row, text=f"Sensor {sid}").pack(side=tk.LEFT)

        self.fig = plt.Figure(figsize=(8.2, 7.0), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.canvas = FigureCanvasTkAgg(self.fig, master=body)
        self.canvas.get_tk_widget().pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    def refresh_ports(self):
        names = [p.device for p in list_ports()]
        self.port_combo["values"] = names
        if self.port_var.get() not in names:
            self.port_var.set(names[0] if names else "")

    def connect(self):
        port = self.port_var.get().strip()
        if not port:
            self.conn_var.set("No COM port")
            return
        try:
            baud = int(self.baud_var.get().strip())
        except ValueError:
            self.conn_var.set("Invalid baud")
            return
        self.reader.connect(port, baud)
        self.conn_var.set(f"Connecting {port}...")

    def disconnect(self):
        self.reader.disconnect()
        self.conn_var.set("Disconnected")

    def _decode_hex_payload(self, hex_payload: str, rings: int, zones: int) -> np.ndarray | None:
        expected_hex_len = ((rings * zones + 1) // 2) * 2
        if len(hex_payload) != expected_hex_len:
            return None

        try:
            raw = bytes.fromhex(hex_payload)
        except ValueError:
            return None

        values = np.zeros(rings * zones, dtype=np.uint8)
        for idx in range(rings * zones):
            byte_value = raw[idx // 2]
            if (idx & 1) == 0:
                values[idx] = byte_value & 0x0F
            else:
                values[idx] = (byte_value >> 4) & 0x0F
        return values.reshape((rings, zones))

    def _handle_line(self, line: str):
        if line.startswith("INFO,open"):
            self.conn_var.set("Connected")
            return
        if line.startswith("INFO,closed"):
            self.conn_var.set("Disconnected")
            return
        if line.startswith("ERR,"):
            self.conn_var.set("Error")
            return
        if not line.startswith("PG,"):
            return

        parts = line.split(",", 5)
        if len(parts) != 6:
            return

        try:
            seq = int(parts[1])
            source_ms = int(parts[2])
            rings = int(parts[3])
            zones = int(parts[4])
        except ValueError:
            return

        grid = self._decode_hex_payload(parts[5], rings, zones)
        if grid is None:
            return

        self.owner_grid = grid
        self.rings = rings
        self.zones = zones
        self.last_seq = seq
        self.last_source_ms = source_ms
        self.last_frame_wall_s = time.time()
        self.frames_received += 1
        self.frames_in_rate_window += 1

        now = time.time()
        dt = now - self.last_rate_sample_s
        if dt >= 1.0:
            self.current_fps = self.frames_in_rate_window / dt
            self.frames_in_rate_window = 0
            self.last_rate_sample_s = now

    def _drain_serial(self):
        processed = 0
        while processed < 500:
            try:
                line = self.reader.rx_queue.get_nowait()
            except queue.Empty:
                break
            self._handle_line(line)
            processed += 1

    def _sensor_color(self, sid: int) -> str:
        if sid <= 0:
            return "#ffffff"
        rgb = SENSOR_RGB.get(sid, (107, 114, 128))
        return "#{:02x}{:02x}{:02x}".format(*rgb)

    def _draw(self):
        self.ax.clear()
        self.ax.set_aspect("equal")

        try:
            max_range_m = float(self.range_var.get().strip())
        except ValueError:
            max_range_m = DEFAULT_MAX_RANGE_M
        max_range_m = max(0.5, min(6.0, max_range_m))
        max_r = max_range_m * 1000.0
        edges = np.linspace(0.0, max_r, self.rings + 1)
        step = 360.0 / self.zones

        self.ax.set_xlim(-max_r, max_r)
        self.ax.set_ylim(-max_r, max_r)
        self.ax.set_xticks([])
        self.ax.set_yticks([])
        self.ax.set_title("Live Polar Occupancy Grid")

        for z in range(self.zones):
            center = polar_zone_center_plot_deg(z, self.zones)
            th1 = center - (step * 0.5)
            th2 = center + (step * 0.5)
            for r in range(self.rings):
                inner = float(edges[r])
                outer = float(edges[r + 1])
                sid = int(self.owner_grid[r, z])
                self.ax.add_patch(
                    Wedge(
                        center=(0.0, 0.0),
                        r=outer,
                        theta1=th1,
                        theta2=th2,
                        width=max(1.0, outer - inner),
                        facecolor=self._sensor_color(sid),
                        edgecolor="#d1d5db",
                        linewidth=0.6,
                    )
                )

        for r in edges[1:]:
            self.ax.add_patch(Circle((0.0, 0.0), float(r), fill=False, edgecolor="#9ca3af", linewidth=0.6, alpha=0.8))
        for z in range(self.zones):
            alpha_boundary = -180.0 + (z * step)
            plot_deg = (90.0 - alpha_boundary) + 180.0
            rad = math.radians(plot_deg)
            self.ax.plot(
                [0.0, max_r * math.cos(rad)],
                [0.0, max_r * math.sin(rad)],
                linestyle="--",
                color="#9ca3af",
                linewidth=0.45,
                alpha=0.45,
            )

        if self.labels_var.get():
            label_angle = math.radians(84.0)
            for r in edges[1:]:
                lx = float(r) * math.cos(label_angle)
                ly = float(r) * math.sin(label_angle)
                self.ax.text(
                    lx,
                    ly,
                    f"{r/1000.0:.2f}m",
                    fontsize=7,
                    color="#374151",
                    ha="left",
                    va="center",
                    bbox=dict(boxstyle="round,pad=0.10", facecolor="white", edgecolor="none", alpha=0.75),
                )

        self.ax.arrow(0.0, 0.0, 0.0, max_r * 0.18, width=max_r * 0.008, head_width=max_r * 0.05, color="#374151")
        self.ax.text(0.0, max_r * 0.22, "Front", ha="center", va="bottom", fontsize=8)

        legend_handles = [
            Patch(facecolor=self._sensor_color(sid), edgecolor="none", label=f"S{sid}")
            for sid in sorted(SENSOR_RGB)
        ]
        self.ax.legend(handles=legend_handles, loc="upper right", fontsize=8, framealpha=0.92)

        age_ms = -1.0
        if self.last_frame_wall_s > 0:
            age_ms = (time.time() - self.last_frame_wall_s) * 1000.0
        occupied = int((self.owner_grid > 0).sum())
        self.summary_var.set(
            f"seq={self.last_seq}  rings={self.rings}  zones={self.zones}  occupied={occupied}  "
            f"fps={self.current_fps:.1f}  age_ms={age_ms:.0f}  source_ms={self.last_source_ms}"
        )

        self.fig.tight_layout()
        self.canvas.draw_idle()

    def _tick(self):
        self._drain_serial()
        self._draw()
        self.root.after(75, self._tick)

    def _on_close(self):
        self.reader.disconnect()
        self.root.destroy()


def default_port():
    ports = list_ports()
    return ports[0].device if ports else None


def main():
    parser = argparse.ArgumentParser(description="ViviSense live presentation polar grid viewer")
    parser.add_argument("--port", default=default_port(), help="COM port (example: COM5)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Serial baud")
    args = parser.parse_args()

    root = tk.Tk()
    App(root, args.port, args.baud)
    root.mainloop()


if __name__ == "__main__":
    main()
