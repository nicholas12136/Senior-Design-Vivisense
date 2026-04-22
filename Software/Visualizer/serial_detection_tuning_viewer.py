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
from matplotlib.colors import BoundaryNorm, ListedColormap
from matplotlib.patches import Circle, Wedge
import serial
import serial.tools.list_ports


ONLINE_TIMEOUT_S = 1.5

DEFAULT_CART_X_MIN_M = 0.0
DEFAULT_CART_X_MAX_M = 3.0
DEFAULT_CART_Y_MIN_M = -1.5
DEFAULT_CART_Y_MAX_M = 1.5
DEFAULT_CART_CELL_M = 0.10

DEFAULT_POLAR_RINGS = 12
DEFAULT_POLAR_ZONES = 24
DEFAULT_POLAR_MAX_RANGE_M = 3.0

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
    # alpha = wheelchair-frame angle where 0 deg is forward (+X), +90 is right.
    alpha_center = -180.0 + ((zone_idx + 0.5) * (360.0 / zones))
    # Matplotlib angle: 0 = +X (right), 90 = +Y (up/front)
    return 90.0 - alpha_center


class SerialReader:
    def __init__(self):
        self.rx_queue = queue.Queue(maxsize=12000)
        self._thread = None
        self._stop = threading.Event()
        self._serial = None
        self.connected = False
        self.port = ""
        self.baud = 0

    def connect(self, port: str, baud: int):
        if self.connected:
            return
        self._stop.clear()
        self.port = port
        self.baud = baud
        self._thread = threading.Thread(target=self._worker, daemon=True)
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

    def _put_line(self, line: str):
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

    def _worker(self):
        try:
            self._serial = serial.Serial(self.port, self.baud, timeout=0.1)
            self.connected = True
            self._put_line(f"INFO,open,{self.port}@{self.baud}")
        except Exception as exc:
            self.connected = False
            self._put_line(f"ERR,open,{exc}")
            return

        while not self._stop.is_set():
            try:
                raw = self._serial.readline()
            except Exception as exc:
                self._put_line(f"ERR,read,{exc}")
                break
            if not raw:
                continue
            try:
                line = raw.decode("utf-8", errors="ignore").strip()
            except Exception:
                continue
            if line:
                self._put_line(line)

        self.connected = False
        try:
            if self._serial is not None:
                self._serial.close()
        except Exception:
            pass
        self._serial = None
        self._put_line("INFO,closed")


class App:
    def __init__(self, root: tk.Tk, default_port: str | None, baud: int):
        self.root = root
        self.root.title("ViviSense Occupancy Filter Debugger")

        self.reader = SerialReader()
        self.pending_by_sensor = {sid: [] for sid in range(1, 9)}
        self.points_by_sensor = {sid: [] for sid in range(1, 9)}
        self.last_frame_local_s = {sid: 0.0 for sid in range(1, 9)}
        self.pkts_by_sensor = {sid: 0 for sid in range(1, 9)}
        self.hz_by_sensor = {sid: 0.0 for sid in range(1, 9)}

        self.cart_signature = None
        self.polar_signature = None

        self.cart_conf = np.zeros((1, 1), dtype=np.uint8)
        self.cart_occ = np.zeros((1, 1), dtype=np.uint8)
        self.cart_owner = np.zeros((1, 1), dtype=np.uint8)
        self.raw_cart_owner = np.zeros((1, 1), dtype=np.uint8)

        self.polar_conf = np.zeros((1, 1), dtype=np.uint8)
        self.polar_occ = np.zeros((1, 1), dtype=np.uint8)
        self.polar_owner = np.zeros((1, 1), dtype=np.uint8)
        self.raw_polar_owner = np.zeros((1, 1), dtype=np.uint8)

        self.last_filter_step_s = time.time()

        self._build_ui(default_port, baud)
        self.refresh_ports()
        self._ensure_grids(force_reset=True)
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
        ttk.Button(top, text="Reset Filter State", command=self.reset_filter_state).pack(side=tk.LEFT, padx=(8, 0))

        self.conn_var = tk.StringVar(value="Disconnected")
        ttk.Label(top, textvariable=self.conn_var).pack(side=tk.LEFT, padx=(10, 0))
        self.summary_var = tk.StringVar(value="Left: raw occupancy | Right: filtered occupancy")
        ttk.Label(top, textvariable=self.summary_var).pack(side=tk.RIGHT)

        body = ttk.Frame(self.root, padding=(8, 0, 8, 8))
        body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        left_wrap = ttk.Frame(body)
        left_wrap.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 10))
        self.left_canvas = tk.Canvas(left_wrap, width=430, highlightthickness=0)
        self.left_scroll = ttk.Scrollbar(left_wrap, orient=tk.VERTICAL, command=self.left_canvas.yview)
        self.left_canvas.configure(yscrollcommand=self.left_scroll.set)
        self.left_canvas.pack(side=tk.LEFT, fill=tk.Y)
        self.left_scroll.pack(side=tk.LEFT, fill=tk.Y)

        left = ttk.Frame(self.left_canvas)
        self.left_canvas_window = self.left_canvas.create_window((0, 0), window=left, anchor="nw")

        def _on_left_configure(_event=None):
            self.left_canvas.configure(scrollregion=self.left_canvas.bbox("all"))

        def _on_canvas_configure(event):
            self.left_canvas.itemconfigure(self.left_canvas_window, width=event.width)

        left.bind("<Configure>", _on_left_configure)
        self.left_canvas.bind("<Configure>", _on_canvas_configure)

        ttk.Label(left, text="View Mode").pack(anchor="w")
        self.display_mode_var = tk.StringVar(value="cartesian")
        self.mode_notebook = ttk.Notebook(left)
        self.mode_notebook.pack(anchor="w", fill=tk.X, pady=(4, 8))

        self.cart_tab = ttk.Frame(self.mode_notebook)
        self.polar_tab = ttk.Frame(self.mode_notebook)
        self.mode_notebook.add(self.cart_tab, text="Cartesian")
        self.mode_notebook.add(self.polar_tab, text="Polar")
        self.mode_notebook.bind("<<NotebookTabChanged>>", self._on_mode_tab_changed)

        ttk.Label(left, text="Sensor Status").pack(anchor="w")
        self.sensor_enabled_vars = {}
        self.sensor_status_vars = {}
        for sid in range(1, 9):
            row = ttk.Frame(left)
            row.pack(anchor="w", fill=tk.X)
            en = tk.BooleanVar(value=True)
            self.sensor_enabled_vars[sid] = en
            ttk.Checkbutton(row, variable=en, command=self._draw).pack(side=tk.LEFT)
            txt = tk.StringVar(value=f"S{sid}: offline pkts=0 hz=0.0")
            self.sensor_status_vars[sid] = txt
            ttk.Label(row, textvariable=txt, width=52).pack(side=tk.LEFT)

        ttk.Separator(left, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=8)
        ttk.Label(left, text="Global").pack(anchor="w")

        self.filter_period_ms_var = tk.StringVar(value="67")
        grow = ttk.Frame(left)
        grow.pack(anchor="w")
        ttk.Label(grow, text="Filter step period (ms)", width=28).pack(side=tk.LEFT)
        ttk.Entry(grow, textvariable=self.filter_period_ms_var, width=10).pack(side=tk.LEFT)

        self.show_invalid_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            left,
            text="Include invalid points",
            variable=self.show_invalid_var,
            command=self._draw,
        ).pack(anchor="w")

        ttk.Separator(left, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=8)
        self._build_cartesian_controls(self.cart_tab)
        self._build_polar_controls(self.polar_tab)

        ttk.Label(left, text="Sensor Color Key").pack(anchor="w", pady=(8, 0))
        key_frame = ttk.Frame(left)
        key_frame.pack(anchor="w", fill=tk.X)
        for sid in range(1, 9):
            item = ttk.Frame(key_frame)
            item.pack(anchor="w")
            color_hex = "#{:02x}{:02x}{:02x}".format(*SENSOR_RGB[sid])
            swatch = tk.Canvas(item, width=12, height=12, highlightthickness=0, bd=0)
            swatch.create_rectangle(0, 0, 12, 12, fill=color_hex, outline=color_hex)
            swatch.pack(side=tk.LEFT, padx=(0, 4))
            ttk.Label(item, text=f"S{sid}", width=4).pack(side=tk.LEFT)

        self.fig = plt.Figure(figsize=(11.2, 6.8), dpi=100)
        self.ax_left = self.fig.add_subplot(121)
        self.ax_right = self.fig.add_subplot(122)
        self.canvas = FigureCanvasTkAgg(self.fig, master=body)
        self.canvas.get_tk_widget().pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    def _build_cartesian_controls(self, parent):
        ttk.Label(parent, text="Cartesian Grid (like occupancy viewer)").pack(anchor="w", pady=(4, 0))
        self.cart_x_min_var = tk.StringVar(value=f"{DEFAULT_CART_X_MIN_M:.2f}")
        self.cart_x_max_var = tk.StringVar(value=f"{DEFAULT_CART_X_MAX_M:.2f}")
        self.cart_y_min_var = tk.StringVar(value=f"{DEFAULT_CART_Y_MIN_M:.2f}")
        self.cart_y_max_var = tk.StringVar(value=f"{DEFAULT_CART_Y_MAX_M:.2f}")
        self.cart_cell_var = tk.StringVar(value=f"{DEFAULT_CART_CELL_M:.2f}")

        self._labeled_entry(parent, "X min (m)", self.cart_x_min_var)
        self._labeled_entry(parent, "X max (m)", self.cart_x_max_var)
        self._labeled_entry(parent, "Y min (m)", self.cart_y_min_var)
        self._labeled_entry(parent, "Y max (m)", self.cart_y_max_var)
        self._labeled_entry(parent, "Cell size (m)", self.cart_cell_var)

        ttk.Label(parent, text="Cartesian Filter").pack(anchor="w", pady=(8, 0))
        self.occ_rise_var = tk.StringVar(value="90")
        self.occ_decay_var = tk.StringVar(value="24")
        self.occ_enter_var = tk.StringVar(value="120")
        self.occ_exit_var = tk.StringVar(value="80")
        self._labeled_entry(parent, "occ_rise", self.occ_rise_var)
        self._labeled_entry(parent, "occ_decay", self.occ_decay_var)
        self._labeled_entry(parent, "occ_enter", self.occ_enter_var)
        self._labeled_entry(parent, "occ_exit", self.occ_exit_var)

    def _build_polar_controls(self, parent):
        ttk.Label(parent, text="Polar Grid").pack(anchor="w", pady=(4, 0))
        self.polar_rings_var = tk.StringVar(value=str(DEFAULT_POLAR_RINGS))
        self.polar_zones_var = tk.StringVar(value=str(DEFAULT_POLAR_ZONES))
        self.polar_max_range_var = tk.StringVar(value=f"{DEFAULT_POLAR_MAX_RANGE_M:.2f}")
        self._labeled_entry(parent, "Rings (bins)", self.polar_rings_var)
        self._labeled_entry(parent, "Zones (bins)", self.polar_zones_var)
        self._labeled_entry(parent, "Max range (m)", self.polar_max_range_var)

        ttk.Label(parent, text="Polar Filter").pack(anchor="w", pady=(8, 0))
        self.polar_rise_var = tk.StringVar(value="90")
        self.polar_decay_var = tk.StringVar(value="24")
        self.polar_enter_var = tk.StringVar(value="120")
        self.polar_exit_var = tk.StringVar(value="80")
        self._labeled_entry(parent, "polar_rise", self.polar_rise_var)
        self._labeled_entry(parent, "polar_decay", self.polar_decay_var)
        self._labeled_entry(parent, "polar_enter", self.polar_enter_var)
        self._labeled_entry(parent, "polar_exit", self.polar_exit_var)

        self.show_polar_distance_grid_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            parent,
            text="Show polar distance grid labels",
            variable=self.show_polar_distance_grid_var,
            command=self._draw,
        ).pack(anchor="w", pady=(6, 0))

        self.show_polar_cell_labels_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            parent,
            text="Show per-cell distance labels",
            variable=self.show_polar_cell_labels_var,
            command=self._draw,
        ).pack(anchor="w")

    @staticmethod
    def _labeled_entry(parent, label: str, var: tk.StringVar):
        row = ttk.Frame(parent)
        row.pack(anchor="w")
        ttk.Label(row, text=label, width=22).pack(side=tk.LEFT)
        ttk.Entry(row, textvariable=var, width=10).pack(side=tk.LEFT)

    def _on_mode_tab_changed(self, _event=None):
        idx = self.mode_notebook.index(self.mode_notebook.select())
        self.display_mode_var.set("cartesian" if idx == 0 else "polar")
        self._draw()

    def refresh_ports(self):
        names = [p.device for p in list_ports()]
        self.port_combo["values"] = names
        if self.port_var.get() not in names:
            self.port_var.set(names[0] if names else "")

    def connect(self):
        if self.reader.connected:
            return
        port = self.port_var.get().strip()
        if not port:
            self.conn_var.set("No COM port")
            return
        try:
            baud = int(self.baud_var.get().strip())
        except ValueError:
            self.conn_var.set("Invalid baud")
            return
        self._reset_stream_state()
        self.reader.connect(port, baud)
        self.conn_var.set(f"Connecting {port}...")

    def disconnect(self):
        self.reader.disconnect()
        self.conn_var.set("Disconnected")

    def _reset_stream_state(self):
        for sid in range(1, 9):
            self.pending_by_sensor[sid] = []
            self.points_by_sensor[sid] = []
            self.last_frame_local_s[sid] = 0.0
            self.pkts_by_sensor[sid] = 0
            self.hz_by_sensor[sid] = 0.0
        self.reset_filter_state()

    def reset_filter_state(self):
        self._ensure_grids(force_reset=True)
        self.last_filter_step_s = time.time()
        self._draw()

    def _parse_cart_params(self):
        x_min = parse_float(self.cart_x_min_var.get(), DEFAULT_CART_X_MIN_M)
        x_max = parse_float(self.cart_x_max_var.get(), DEFAULT_CART_X_MAX_M)
        y_min = parse_float(self.cart_y_min_var.get(), DEFAULT_CART_Y_MIN_M)
        y_max = parse_float(self.cart_y_max_var.get(), DEFAULT_CART_Y_MAX_M)
        cell_m = parse_float(self.cart_cell_var.get(), DEFAULT_CART_CELL_M)

        cell_m = clamp_float(cell_m, 0.02, 0.80)
        if x_max <= x_min + 1e-6:
            x_min = DEFAULT_CART_X_MIN_M
            x_max = DEFAULT_CART_X_MAX_M
        if y_max <= y_min + 1e-6:
            y_min = DEFAULT_CART_Y_MIN_M
            y_max = DEFAULT_CART_Y_MAX_M

        cols = int(math.ceil((x_max - x_min) / cell_m))
        rows = int(math.ceil((y_max - y_min) / cell_m))
        cols = clamp_int(cols, 1, 350)
        rows = clamp_int(rows, 1, 350)

        return {
            "x_min_m": x_min,
            "x_max_m": x_max,
            "y_min_m": y_min,
            "y_max_m": y_max,
            "cell_m": cell_m,
            "cols": cols,
            "rows": rows,
        }

    def _parse_cart_filter_params(self):
        rise = clamp_int(parse_int(self.occ_rise_var.get(), 90), 1, 255)
        decay = clamp_int(parse_int(self.occ_decay_var.get(), 24), 0, 255)
        enter = clamp_int(parse_int(self.occ_enter_var.get(), 120), 1, 255)
        exit_ = clamp_int(parse_int(self.occ_exit_var.get(), 80), 0, 254)
        if exit_ >= enter:
            exit_ = enter - 1
        return rise, decay, enter, exit_

    def _parse_polar_params(self):
        rings = clamp_int(parse_int(self.polar_rings_var.get(), DEFAULT_POLAR_RINGS), 2, 40)
        zones = clamp_int(parse_int(self.polar_zones_var.get(), DEFAULT_POLAR_ZONES), 4, 72)
        max_range_m = clamp_float(parse_float(self.polar_max_range_var.get(), DEFAULT_POLAR_MAX_RANGE_M), 0.5, 6.0)
        return {"rings": rings, "zones": zones, "max_range_m": max_range_m}

    def _parse_polar_filter_params(self):
        rise = clamp_int(parse_int(self.polar_rise_var.get(), 90), 1, 255)
        decay = clamp_int(parse_int(self.polar_decay_var.get(), 24), 0, 255)
        enter = clamp_int(parse_int(self.polar_enter_var.get(), 120), 1, 255)
        exit_ = clamp_int(parse_int(self.polar_exit_var.get(), 80), 0, 254)
        if exit_ >= enter:
            exit_ = enter - 1
        return rise, decay, enter, exit_

    def _parse_filter_period_s(self):
        ms = clamp_int(parse_int(self.filter_period_ms_var.get(), 67), 20, 2000)
        return ms / 1000.0

    def _ensure_grids(self, force_reset=False):
        cart = self._parse_cart_params()
        polar = self._parse_polar_params()

        cart_sig = (
            cart["rows"],
            cart["cols"],
            round(cart["x_min_m"], 4),
            round(cart["x_max_m"], 4),
            round(cart["y_min_m"], 4),
            round(cart["y_max_m"], 4),
            round(cart["cell_m"], 4),
        )
        polar_sig = (
            polar["rings"],
            polar["zones"],
            round(polar["max_range_m"], 4),
        )

        if force_reset or self.cart_signature != cart_sig:
            self.cart_conf = np.zeros((cart["rows"], cart["cols"]), dtype=np.uint8)
            self.cart_occ = np.zeros((cart["rows"], cart["cols"]), dtype=np.uint8)
            self.cart_owner = np.zeros((cart["rows"], cart["cols"]), dtype=np.uint8)
            self.raw_cart_owner = np.zeros((cart["rows"], cart["cols"]), dtype=np.uint8)
            self.cart_signature = cart_sig

        if force_reset or self.polar_signature != polar_sig:
            self.polar_conf = np.zeros((polar["rings"], polar["zones"]), dtype=np.uint8)
            self.polar_occ = np.zeros((polar["rings"], polar["zones"]), dtype=np.uint8)
            self.polar_owner = np.zeros((polar["rings"], polar["zones"]), dtype=np.uint8)
            self.raw_polar_owner = np.zeros((polar["rings"], polar["zones"]), dtype=np.uint8)
            self.polar_signature = polar_sig

        return cart, polar

    def _handle_line(self, line: str):
        if line.startswith("P,"):
            parts = line.split(",")
            if len(parts) not in (8, 9):
                return 0
            try:
                sid = int(parts[1])
                cell = int(parts[2])
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
            if sid in self.pending_by_sensor:
                self.pending_by_sensor[sid].append((cell, valid, x, y, z, status))
            return 0

        if line.startswith("E,"):
            parts = line.split(",")
            if len(parts) != 3:
                return 0
            try:
                sid = int(parts[1])
            except ValueError:
                return 0
            if sid not in self.pending_by_sensor:
                return 0
            self.points_by_sensor[sid] = self.pending_by_sensor[sid]
            self.pending_by_sensor[sid] = []
            self.last_frame_local_s[sid] = time.time()
            return 1

        if line.startswith("S,"):
            parts = line.split(",")
            if len(parts) < 4:
                return 0
            try:
                sid = int(parts[1])
                pkts = int(parts[2])
                hz = float(parts[3])
            except ValueError:
                return 0
            if sid in self.pkts_by_sensor:
                self.pkts_by_sensor[sid] = pkts
                self.hz_by_sensor[sid] = hz
            return 0

        if line.startswith("INFO,open,"):
            self.conn_var.set("Connected")
        elif line.startswith("INFO,closed"):
            self.conn_var.set("Disconnected")
        elif line.startswith("ERR,"):
            self.conn_var.set("Error")
        return 0

    def _drain_serial(self):
        frame_events = 0
        n = 0
        while n < 5000:
            try:
                line = self.reader.rx_queue.get_nowait()
            except queue.Empty:
                break
            frame_events += self._handle_line(line)
            n += 1
        return frame_events

    def _collect_points(self):
        now = time.time()
        include_invalid = self.show_invalid_var.get()
        rows = []
        for sid in range(1, 9):
            if not self.sensor_enabled_vars[sid].get():
                continue
            age = now - self.last_frame_local_s[sid]
            if age > ONLINE_TIMEOUT_S:
                continue
            for cell, valid, x, y, z, status in self.points_by_sensor[sid]:
                if (not include_invalid) and (valid == 0):
                    continue
                rows.append((sid, cell, valid, x, y, z, status))
        return rows

    def _step_filters_if_due(self, rows):
        cart, polar = self._ensure_grids(force_reset=False)
        now = time.time()
        period_s = self._parse_filter_period_s()
        if (now - self.last_filter_step_s) < period_s:
            return

        steps = 0
        while (now - self.last_filter_step_s) >= period_s and steps < 8:
            self._step_cartesian_filter(rows, cart)
            self._step_polar_filter(rows, polar)
            self.last_filter_step_s += period_s
            steps += 1
        if (now - self.last_filter_step_s) >= period_s:
            self.last_filter_step_s = now

    def _step_cartesian_filter(self, rows, cart):
        rise, decay, enter, exit_ = self._parse_cart_filter_params()
        hit = np.zeros((cart["rows"], cart["cols"]), dtype=np.uint8)
        owner_hits = np.zeros((cart["rows"], cart["cols"]), dtype=np.uint8)

        x_min_mm = cart["x_min_m"] * 1000.0
        y_min_mm = cart["y_min_m"] * 1000.0
        x_max_mm = cart["x_max_m"] * 1000.0
        y_max_mm = cart["y_max_m"] * 1000.0
        cell_mm = cart["cell_m"] * 1000.0

        for sid, _cell, _valid, x_m, y_m, _z_m, _status in rows:
            x_mm = x_m * 1000.0
            y_mm = y_m * 1000.0
            if x_mm < x_min_mm or x_mm >= x_max_mm or y_mm < y_min_mm or y_mm >= y_max_mm:
                continue
            col = int((x_mm - x_min_mm) / cell_mm)
            row = int((y_mm - y_min_mm) / cell_mm)
            if row < 0 or row >= cart["rows"] or col < 0 or col >= cart["cols"]:
                continue
            hit[row, col] = 1
            if owner_hits[row, col] == 0:
                owner_hits[row, col] = sid

        self.raw_cart_owner = owner_hits.copy()

        conf = self.cart_conf.astype(np.int16)
        conf[hit > 0] = np.minimum(conf[hit > 0] + rise, 255)
        conf[hit == 0] = np.maximum(conf[hit == 0] - decay, 0)
        self.cart_conf = conf.astype(np.uint8)

        self.cart_owner[hit > 0] = owner_hits[hit > 0]
        enter_mask = (self.cart_occ == 0) & (self.cart_conf >= enter)
        exit_mask = (self.cart_occ == 1) & (self.cart_conf <= exit_)
        self.cart_occ[enter_mask] = 1
        self.cart_occ[exit_mask] = 0
        self.cart_owner[exit_mask] = 0

    def _step_polar_filter(self, rows, polar):
        rise, decay, enter, exit_ = self._parse_polar_filter_params()
        rings = polar["rings"]
        zones = polar["zones"]
        max_range_mm = polar["max_range_m"] * 1000.0

        hit = np.zeros((rings, zones), dtype=np.uint8)
        owner_hits = np.zeros((rings, zones), dtype=np.uint8)

        for sid, _cell, _valid, x_m, y_m, _z_m, _status in rows:
            x_mm = x_m * 1000.0
            y_mm = y_m * 1000.0
            dist = math.sqrt((x_mm * x_mm) + (y_mm * y_mm))
            if dist > max_range_mm:
                continue
            angle_deg = math.degrees(math.atan2(-y_mm, x_mm))
            zone = polar_zone_index(angle_deg, zones)
            ring = int((dist / max_range_mm) * rings)
            if ring >= rings:
                ring = rings - 1
            if ring < 0 or zone < 0:
                continue
            hit[ring, zone] = 1
            if owner_hits[ring, zone] == 0:
                owner_hits[ring, zone] = sid

        self.raw_polar_owner = owner_hits.copy()

        conf = self.polar_conf.astype(np.int16)
        conf[hit > 0] = np.minimum(conf[hit > 0] + rise, 255)
        conf[hit == 0] = np.maximum(conf[hit == 0] - decay, 0)
        self.polar_conf = conf.astype(np.uint8)

        self.polar_owner[hit > 0] = owner_hits[hit > 0]
        enter_mask = (self.polar_occ == 0) & (self.polar_conf >= enter)
        exit_mask = (self.polar_occ == 1) & (self.polar_conf <= exit_)
        self.polar_occ[enter_mask] = 1
        self.polar_occ[exit_mask] = 0
        self.polar_owner[exit_mask] = 0

    def _update_sensor_status_labels(self):
        now = time.time()
        for sid in range(1, 9):
            age = now - self.last_frame_local_s[sid]
            online = age <= ONLINE_TIMEOUT_S
            state = "online" if online else "offline"
            self.sensor_status_vars[sid].set(
                f"S{sid}: {state} pkts={self.pkts_by_sensor[sid]} hz={self.hz_by_sensor[sid]:.1f} age={age:.2f}s"
            )

    def _draw_cartesian(self, rows, cart):
        self.ax_left.clear()
        self.ax_right.clear()

        extent = [cart["y_min_m"], cart["y_max_m"], cart["x_min_m"], cart["x_max_m"]]
        cmap = ListedColormap(
            ["#ffffff"] + ["#{:02x}{:02x}{:02x}".format(*SENSOR_RGB[s]) for s in range(1, 9)]
        )
        norm = BoundaryNorm(np.arange(-0.5, 9.5, 1.0), cmap.N)

        raw_disp = np.rot90(self.raw_cart_owner, k=-1)
        filt_disp = np.rot90(np.where(self.cart_occ > 0, self.cart_owner, 0), k=-1)

        self.ax_left.imshow(
            raw_disp,
            origin="lower",
            extent=extent,
            interpolation="nearest",
            aspect="equal",
            cmap=cmap,
            norm=norm,
        )
        self.ax_right.imshow(
            filt_disp,
            origin="lower",
            extent=extent,
            interpolation="nearest",
            aspect="equal",
            cmap=cmap,
            norm=norm,
        )

        self.ax_left.set_title("Raw Occupancy (All Points)")
        self.ax_right.set_title("Filtered Occupancy (Cartesian)")
        self.ax_left.set_xlabel("Y (m)")
        self.ax_right.set_xlabel("Y (m)")
        self.ax_left.set_ylabel("X (m)")
        self.ax_right.set_ylabel("X (m)")
        self.ax_left.grid(True, alpha=0.2)
        self.ax_right.grid(True, alpha=0.2)

        self.summary_var.set(
            f"Cartesian  points={len(rows)}  grid={cart['rows']}x{cart['cols']}  "
            f"raw_cells={(self.raw_cart_owner > 0).sum()}  filtered_cells={(self.cart_occ > 0).sum()}"
        )

    def _draw_single_polar_panel(self, ax, owner_grid, title, polar):
        ax.clear()
        ax.set_aspect("equal")
        max_r = polar["max_range_m"] * 1000.0
        rings = polar["rings"]
        zones = polar["zones"]
        ring_edges = np.linspace(0.0, max_r, rings + 1)
        step = 360.0 / zones

        ax.set_xlim(-max_r, max_r)
        ax.set_ylim(-max_r, max_r)
        ax.set_xticks([])
        ax.set_yticks([])
        ax.set_title(title)

        for z in range(zones):
            center_plot = polar_zone_center_plot_deg(z, zones)
            theta1 = center_plot - (step * 0.5)
            theta2 = center_plot + (step * 0.5)

            for ring in range(rings):
                inner = float(ring_edges[ring])
                outer = float(ring_edges[ring + 1])
                sid = int(owner_grid[ring, z])
                if sid <= 0:
                    face = "#ffffff"
                    alpha = 1.0
                else:
                    r, g, b = SENSOR_RGB[sid]
                    face = "#{:02x}{:02x}{:02x}".format(r, g, b)
                    alpha = 0.95
                wedge = Wedge(
                    center=(0.0, 0.0),
                    r=outer,
                    theta1=theta1,
                    theta2=theta2,
                    width=max(1.0, outer - inner),
                    facecolor=face,
                    edgecolor="#d1d5db",
                    linewidth=0.6,
                    alpha=alpha,
                )
                ax.add_patch(wedge)

        # ring guides
        for r in ring_edges[1:]:
            ax.add_patch(Circle((0.0, 0.0), float(r), fill=False, edgecolor="#9ca3af", linewidth=0.6, alpha=0.7))

        # zone boundaries
        for z in range(zones):
            alpha_boundary = -180.0 + (z * step)
            plot_deg = 90.0 - alpha_boundary
            rad = math.radians(plot_deg)
            x2 = max_r * math.cos(rad)
            y2 = max_r * math.sin(rad)
            ax.plot([0.0, x2], [0.0, y2], linestyle="--", color="#9ca3af", linewidth=0.5, alpha=0.55)

        if self.show_polar_distance_grid_var.get():
            # Ring boundary labels (meters).
            label_angle = math.radians(84.0)
            for r in ring_edges[1:]:
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

            # Zone index labels around the outside edge.
            for z in range(zones):
                center_plot = polar_zone_center_plot_deg(z, zones)
                rad = math.radians(center_plot)
                tx = (max_r * 1.04) * math.cos(rad)
                ty = (max_r * 1.04) * math.sin(rad)
                ax.text(
                    tx,
                    ty,
                    f"Z{z}",
                    fontsize=7,
                    color="#374151",
                    ha="center",
                    va="center",
                    bbox=dict(boxstyle="round,pad=0.10", facecolor="white", edgecolor="none", alpha=0.65),
                )

        if self.show_polar_cell_labels_var.get():
            # Avoid unreadable clutter when grid is very dense.
            if (rings * zones) <= 180:
                for z in range(zones):
                    center_plot = polar_zone_center_plot_deg(z, zones)
                    rad = math.radians(center_plot)
                    for ring in range(rings):
                        inner = float(ring_edges[ring])
                        outer = float(ring_edges[ring + 1])
                        mid = (inner + outer) * 0.5
                        tx = mid * math.cos(rad)
                        ty = mid * math.sin(rad)
                        ax.text(
                            tx,
                            ty,
                            f"{inner/1000.0:.1f}-{outer/1000.0:.1f}",
                            fontsize=5,
                            color="#111827",
                            ha="center",
                            va="center",
                            bbox=dict(boxstyle="round,pad=0.08", facecolor="white", edgecolor="none", alpha=0.60),
                        )
            else:
                ax.text(
                    0.0,
                    -max_r * 1.08,
                    "Per-cell labels hidden (grid too dense)",
                    fontsize=8,
                    color="#6b7280",
                    ha="center",
                    va="center",
                )

        ax.arrow(0.0, 0.0, 0.0, max_r * 0.18, width=max_r * 0.008, head_width=max_r * 0.05, color="#374151")
        ax.text(0.0, max_r * 0.22, "Front", ha="center", va="bottom", fontsize=8)

    def _draw_polar(self, rows, polar):
        raw = self.raw_polar_owner
        filt = np.where(self.polar_occ > 0, self.polar_owner, 0)

        self._draw_single_polar_panel(self.ax_left, raw, "Raw Polar Occupancy (All Points)", polar)
        self._draw_single_polar_panel(self.ax_right, filt, "Filtered Polar Occupancy", polar)

        self.summary_var.set(
            f"Polar  points={len(rows)}  grid={polar['rings']}x{polar['zones']}  "
            f"raw_bins={(raw > 0).sum()}  filtered_bins={(filt > 0).sum()}"
        )

    def _draw(self):
        cart, polar = self._ensure_grids(force_reset=False)
        rows = self._collect_points()
        mode = self.display_mode_var.get().strip().lower()

        if mode == "polar":
            self._draw_polar(rows, polar)
        else:
            self._draw_cartesian(rows, cart)

        self._update_sensor_status_labels()
        self.fig.tight_layout()
        self.canvas.draw_idle()

    def _tick(self):
        _ = self._drain_serial()
        rows = self._collect_points()
        self._step_filters_if_due(rows)
        self._draw()
        self.root.after(100, self._tick)

    def _on_close(self):
        self.reader.disconnect()
        self.root.destroy()


def default_port():
    ports = list_ports()
    return ports[0].device if ports else None


def main():
    parser = argparse.ArgumentParser(
        description="ViviSense occupancy filter debugger (left raw, right filtered; cartesian/polar toggle)"
    )
    parser.add_argument("--port", default=default_port(), help="Default serial port (e.g. COM5)")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud")
    args = parser.parse_args()

    root = tk.Tk()
    App(root, args.port, args.baud)
    root.mainloop()


if __name__ == "__main__":
    main()
