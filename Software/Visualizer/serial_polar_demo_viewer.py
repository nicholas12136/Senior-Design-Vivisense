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
from matplotlib.patches import Circle, Wedge
import serial
import serial.tools.list_ports


ONLINE_TIMEOUT_S = 1.5

DEFAULT_RINGS = 12
DEFAULT_ZONES = 24
DEFAULT_MAX_RANGE_M = 3.0
DEFAULT_STEP_MS = 33
DEFAULT_RISE = 140
DEFAULT_DECAY = 36
DEFAULT_ENTER = 120
DEFAULT_EXIT = 70

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


def clamp_int(v: int, lo: int, hi: int) -> int:
    return lo if v < lo else hi if v > hi else v


def clamp_float(v: float, lo: float, hi: float) -> float:
    return lo if v < lo else hi if v > hi else v


def parse_int(text: str, fallback: int) -> int:
    try:
        return int(text.strip())
    except Exception:
        return fallback


def parse_float(text: str, fallback: float) -> float:
    try:
        return float(text.strip())
    except Exception:
        return fallback


def polar_zone_index(angle_deg: float, zones: int) -> int:
    step = 360.0 / float(zones)
    idx = int(math.floor((angle_deg + 180.0) / step))
    if idx < 0:
        return 0
    if idx >= zones:
        return zones - 1
    return idx


def polar_zone_center_plot_deg(zone_idx: int, zones: int) -> float:
    alpha_center = -180.0 + ((zone_idx + 0.5) * (360.0 / zones))
    return 90.0 - alpha_center


def list_ports():
    return list(serial.tools.list_ports.comports())


class SerialReader:
    def __init__(self):
        self.rx_queue = queue.Queue(maxsize=12000)
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
        self.root.title("ViviSense Polar Demo Viewer")

        self.reader = SerialReader()
        self.pending_by_sensor = {sid: [] for sid in range(1, 9)}
        self.points_by_sensor = {sid: [] for sid in range(1, 9)}
        self.last_frame_s = {sid: 0.0 for sid in range(1, 9)}
        self.pkts = {sid: 0 for sid in range(1, 9)}
        self.hz = {sid: 0.0 for sid in range(1, 9)}

        self.polar_signature = None
        self.polar_conf = np.zeros((1, 1), dtype=np.uint8)
        self.polar_occ = np.zeros((1, 1), dtype=np.uint8)
        self.polar_owner = np.zeros((1, 1), dtype=np.uint8)
        self.raw_owner = np.zeros((1, 1), dtype=np.uint8)
        self.last_filter_step_s = time.time()

        self._build_ui(default_port, baud)
        self.refresh_ports()
        self._ensure_grids(force=True)
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
        ttk.Button(top, text="Reset", command=self.reset_filters).pack(side=tk.LEFT, padx=(6, 0))
        self.conn_var = tk.StringVar(value="Disconnected")
        ttk.Label(top, textvariable=self.conn_var).pack(side=tk.LEFT, padx=(10, 0))

        body = ttk.Frame(self.root, padding=(8, 0, 8, 8))
        body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        controls = ttk.Frame(body)
        controls.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 10))

        self.rings_var = tk.StringVar(value=str(DEFAULT_RINGS))
        self.zones_var = tk.StringVar(value=str(DEFAULT_ZONES))
        self.range_var = tk.StringVar(value=f"{DEFAULT_MAX_RANGE_M:.2f}")
        self.step_var = tk.StringVar(value=str(DEFAULT_STEP_MS))
        self.rise_var = tk.StringVar(value=str(DEFAULT_RISE))
        self.decay_var = tk.StringVar(value=str(DEFAULT_DECAY))
        self.enter_var = tk.StringVar(value=str(DEFAULT_ENTER))
        self.exit_var = tk.StringVar(value=str(DEFAULT_EXIT))
        self.grid_labels_var = tk.BooleanVar(value=True)

        self._entry_row(controls, "Rings", self.rings_var)
        self._entry_row(controls, "Zones", self.zones_var)
        self._entry_row(controls, "Max Range (m)", self.range_var)
        self._entry_row(controls, "Filter Step (ms)", self.step_var)
        ttk.Separator(controls, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=6)
        self._entry_row(controls, "polar_rise", self.rise_var)
        self._entry_row(controls, "polar_decay", self.decay_var)
        self._entry_row(controls, "polar_enter", self.enter_var)
        self._entry_row(controls, "polar_exit", self.exit_var)
        ttk.Checkbutton(
            controls,
            text="Show ring/zone labels",
            variable=self.grid_labels_var,
            command=self._draw,
        ).pack(anchor="w", pady=(6, 0))

        ttk.Separator(controls, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=8)
        ttk.Label(controls, text="Sensor Status").pack(anchor="w")
        self.sensor_status_vars = {}
        for sid in range(1, 9):
            txt = tk.StringVar(value=f"S{sid}: offline")
            self.sensor_status_vars[sid] = txt
            ttk.Label(controls, textvariable=txt, width=40).pack(anchor="w")

        self.summary_var = tk.StringVar(value="Waiting for data...")
        ttk.Label(controls, textvariable=self.summary_var, wraplength=310, justify=tk.LEFT).pack(anchor="w", pady=(8, 0))

        self.fig = plt.Figure(figsize=(10.8, 6.8), dpi=100)
        self.ax_left = self.fig.add_subplot(121)
        self.ax_right = self.fig.add_subplot(122)
        self.canvas = FigureCanvasTkAgg(self.fig, master=body)
        self.canvas.get_tk_widget().pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    @staticmethod
    def _entry_row(parent, label: str, var: tk.StringVar):
        row = ttk.Frame(parent)
        row.pack(anchor="w")
        ttk.Label(row, text=label, width=14).pack(side=tk.LEFT)
        ttk.Entry(row, textvariable=var, width=10).pack(side=tk.LEFT)

    def _parse_polar(self):
        rings = clamp_int(parse_int(self.rings_var.get(), DEFAULT_RINGS), 2, 40)
        zones = clamp_int(parse_int(self.zones_var.get(), DEFAULT_ZONES), 4, 72)
        max_m = clamp_float(parse_float(self.range_var.get(), DEFAULT_MAX_RANGE_M), 0.5, 6.0)
        return rings, zones, max_m

    def _parse_filter(self):
        rise = clamp_int(parse_int(self.rise_var.get(), DEFAULT_RISE), 1, 255)
        decay = clamp_int(parse_int(self.decay_var.get(), DEFAULT_DECAY), 0, 255)
        enter = clamp_int(parse_int(self.enter_var.get(), DEFAULT_ENTER), 1, 255)
        exit_ = clamp_int(parse_int(self.exit_var.get(), DEFAULT_EXIT), 0, 254)
        if exit_ >= enter:
            exit_ = enter - 1
        step_ms = clamp_int(parse_int(self.step_var.get(), DEFAULT_STEP_MS), 20, 2000)
        return rise, decay, enter, exit_, step_ms / 1000.0

    def _ensure_grids(self, force=False):
        rings, zones, max_m = self._parse_polar()
        sig = (rings, zones, round(max_m, 3))
        if force or self.polar_signature != sig:
            self.polar_conf = np.zeros((rings, zones), dtype=np.uint8)
            self.polar_occ = np.zeros((rings, zones), dtype=np.uint8)
            self.polar_owner = np.zeros((rings, zones), dtype=np.uint8)
            self.raw_owner = np.zeros((rings, zones), dtype=np.uint8)
            self.polar_signature = sig
        return rings, zones, max_m

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
        baud = parse_int(self.baud_var.get(), 115200)
        self.reset_stream()
        self.reader.connect(port, baud)
        self.conn_var.set(f"Connecting {port}...")

    def disconnect(self):
        self.reader.disconnect()
        self.conn_var.set("Disconnected")

    def reset_stream(self):
        for sid in range(1, 9):
            self.pending_by_sensor[sid] = []
            self.points_by_sensor[sid] = []
            self.last_frame_s[sid] = 0.0
            self.pkts[sid] = 0
            self.hz[sid] = 0.0
        self.reset_filters()

    def reset_filters(self):
        self._ensure_grids(force=True)
        self.last_filter_step_s = time.time()
        self._draw()

    def _handle_line(self, line: str) -> int:
        if line.startswith("P,"):
            parts = line.split(",")
            if len(parts) not in (8, 9):
                return 0
            try:
                sid = int(parts[1])
                if len(parts) == 9:
                    valid = int(parts[3])
                    x = float(parts[4])
                    y = float(parts[5])
                    z = float(parts[6])
                    status = int(parts[7])
                else:
                    valid = 1
                    x = float(parts[3])
                    y = float(parts[4])
                    z = float(parts[5])
                    status = int(parts[6])
            except ValueError:
                return 0
            if valid and status == 5 and sid in self.pending_by_sensor:
                self.pending_by_sensor[sid].append((x, y, z))
            return 0

        if line.startswith("E,"):
            parts = line.split(",")
            if len(parts) != 3:
                return 0
            try:
                sid = int(parts[1])
            except ValueError:
                return 0
            if sid in self.pending_by_sensor:
                self.points_by_sensor[sid] = self.pending_by_sensor[sid]
                self.pending_by_sensor[sid] = []
                self.last_frame_s[sid] = time.time()
            return 1

        if line.startswith("S,"):
            parts = line.split(",")
            if len(parts) < 4:
                return 0
            try:
                sid = int(parts[1])
                self.pkts[sid] = int(parts[2])
                self.hz[sid] = float(parts[3])
            except Exception:
                return 0
            return 0

        if line.startswith("INFO,open"):
            self.conn_var.set("Connected")
        elif line.startswith("INFO,closed"):
            self.conn_var.set("Disconnected")
        elif line.startswith("ERR,"):
            self.conn_var.set("Error")
        return 0

    def _drain_serial(self):
        n = 0
        while n < 6000:
            try:
                line = self.reader.rx_queue.get_nowait()
            except queue.Empty:
                break
            self._handle_line(line)
            n += 1

    def _collect_points(self):
        now = time.time()
        rows = []
        for sid in range(1, 9):
            if (now - self.last_frame_s[sid]) > ONLINE_TIMEOUT_S:
                continue
            for p in self.points_by_sensor[sid]:
                rows.append((sid, p[0], p[1], p[2]))
        return rows

    def _step_filter_if_due(self, points):
        rings, zones, max_m = self._ensure_grids(force=False)
        rise, decay, enter, exit_, step_s = self._parse_filter()
        now = time.time()
        if (now - self.last_filter_step_s) < step_s:
            return

        while (now - self.last_filter_step_s) >= step_s:
            hit = np.zeros((rings, zones), dtype=np.uint8)
            owner = np.zeros((rings, zones), dtype=np.uint8)
            max_mm = max_m * 1000.0
            for sid, x_m, y_m, _z_m in points:
                x_mm = x_m * 1000.0
                y_mm = y_m * 1000.0
                dist = math.sqrt((x_mm * x_mm) + (y_mm * y_mm))
                if dist > max_mm:
                    continue
                zone = polar_zone_index(math.degrees(math.atan2(-y_mm, x_mm)), zones)
                ring = int((dist / max_mm) * rings)
                if ring >= rings:
                    ring = rings - 1
                hit[ring, zone] = 1
                if owner[ring, zone] == 0:
                    owner[ring, zone] = sid

            self.raw_owner = owner.copy()

            conf = self.polar_conf.astype(np.int16)
            conf[hit > 0] = np.minimum(conf[hit > 0] + rise, 255)
            conf[hit == 0] = np.maximum(conf[hit == 0] - decay, 0)
            self.polar_conf = conf.astype(np.uint8)

            self.polar_owner[hit > 0] = owner[hit > 0]
            enter_mask = (self.polar_occ == 0) & (self.polar_conf >= enter)
            exit_mask = (self.polar_occ == 1) & (self.polar_conf <= exit_)
            self.polar_occ[enter_mask] = 1
            self.polar_occ[exit_mask] = 0
            self.polar_owner[exit_mask] = 0

            self.last_filter_step_s += step_s

    def _draw_panel(self, ax, owner_grid, title, rings, zones, max_m):
        ax.clear()
        ax.set_aspect("equal")
        max_r = max_m * 1000.0
        edges = np.linspace(0.0, max_r, rings + 1)
        step = 360.0 / zones
        ax.set_xlim(-max_r, max_r)
        ax.set_ylim(-max_r, max_r)
        ax.set_xticks([])
        ax.set_yticks([])
        ax.set_title(title)

        for z in range(zones):
            center = polar_zone_center_plot_deg(z, zones)
            th1 = center - (step * 0.5)
            th2 = center + (step * 0.5)
            for r in range(rings):
                inner = float(edges[r])
                outer = float(edges[r + 1])
                sid = int(owner_grid[r, z])
                if sid <= 0:
                    face = "#ffffff"
                else:
                    rr, gg, bb = SENSOR_RGB[sid]
                    face = "#{:02x}{:02x}{:02x}".format(rr, gg, bb)
                ax.add_patch(
                    Wedge(
                        center=(0.0, 0.0),
                        r=outer,
                        theta1=th1,
                        theta2=th2,
                        width=max(1.0, outer - inner),
                        facecolor=face,
                        edgecolor="#d1d5db",
                        linewidth=0.6,
                    )
                )

        for r in edges[1:]:
            ax.add_patch(Circle((0.0, 0.0), float(r), fill=False, edgecolor="#9ca3af", linewidth=0.6, alpha=0.8))
        for z in range(zones):
            alpha_boundary = -180.0 + (z * step)
            plot_deg = 90.0 - alpha_boundary
            rad = math.radians(plot_deg)
            ax.plot([0.0, max_r * math.cos(rad)], [0.0, max_r * math.sin(rad)], linestyle="--", color="#9ca3af", linewidth=0.5, alpha=0.55)

        if self.grid_labels_var.get():
            label_angle = math.radians(84.0)
            for r in edges[1:]:
                lx = float(r) * math.cos(label_angle)
                ly = float(r) * math.sin(label_angle)
                ax.text(
                    lx,
                    ly,
                    f"{r/1000.0:.2f}m",
                    fontsize=7,
                    color="#374151",
                    ha="left",
                    va="center",
                    bbox=dict(boxstyle="round,pad=0.12", facecolor="white", edgecolor="none", alpha=0.75),
                )
            for z in range(zones):
                center = polar_zone_center_plot_deg(z, zones)
                rad = math.radians(center)
                ax.text(
                    (max_r * 1.04) * math.cos(rad),
                    (max_r * 1.04) * math.sin(rad),
                    f"Z{z}",
                    fontsize=7,
                    color="#374151",
                    ha="center",
                    va="center",
                    bbox=dict(boxstyle="round,pad=0.10", facecolor="white", edgecolor="none", alpha=0.65),
                )

        ax.arrow(0.0, 0.0, 0.0, max_r * 0.18, width=max_r * 0.008, head_width=max_r * 0.05, color="#374151")
        ax.text(0.0, max_r * 0.22, "Front", ha="center", va="bottom", fontsize=8)

    def _update_sensor_status(self):
        now = time.time()
        for sid in range(1, 9):
            age = now - self.last_frame_s[sid]
            state = "online" if age <= ONLINE_TIMEOUT_S else "offline"
            self.sensor_status_vars[sid].set(
                f"S{sid}: {state}  pkts={self.pkts[sid]}  hz={self.hz[sid]:.1f}  age={age:.2f}s"
            )

    def _draw(self):
        rings, zones, max_m = self._ensure_grids(force=False)
        filt_owner = np.where(self.polar_occ > 0, self.polar_owner, 0)
        self._draw_panel(self.ax_left, self.raw_owner, "Raw Polar Occupancy (status=5 points)", rings, zones, max_m)
        self._draw_panel(self.ax_right, filt_owner, "Filtered Polar Occupancy", rings, zones, max_m)
        self.summary_var.set(
            f"Grid={rings}x{zones}  raw_bins={(self.raw_owner > 0).sum()}  filtered_bins={(filt_owner > 0).sum()}"
        )
        self._update_sensor_status()
        self.fig.tight_layout()
        self.canvas.draw_idle()

    def _tick(self):
        self._drain_serial()
        points = self._collect_points()
        self._step_filter_if_due(points)
        self._draw()
        self.root.after(100, self._tick)

    def _on_close(self):
        self.reader.disconnect()
        self.root.destroy()


def default_port():
    ports = list_ports()
    return ports[0].device if ports else None


def main():
    parser = argparse.ArgumentParser(description="ViviSense real-time polar demo viewer")
    parser.add_argument("--port", default=default_port(), help="COM port (example: COM5)")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud")
    args = parser.parse_args()

    root = tk.Tk()
    App(root, args.port, args.baud)
    root.mainloop()


if __name__ == "__main__":
    main()
