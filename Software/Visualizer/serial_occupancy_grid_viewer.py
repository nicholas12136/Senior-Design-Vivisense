import argparse
import csv
import queue
import threading
import time
import tkinter as tk
from datetime import datetime
from tkinter import filedialog, ttk

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.ticker import MultipleLocator
from matplotlib.colors import ListedColormap, BoundaryNorm
import serial
import serial.tools.list_ports


ONLINE_TIMEOUT_S = 1.5
INCH_TO_M = 0.0254
WHEELCHAIR_LENGTH_M = 25.0 * INCH_TO_M
WHEELCHAIR_WIDTH_M = 27.5 * INCH_TO_M
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


def parse_axis_range(text: str):
    parts = [p.strip() for p in text.split(",")]
    if len(parts) != 2:
        raise ValueError("axis range must be min,max")
    lo = float(parts[0])
    hi = float(parts[1])
    if lo >= hi:
        raise ValueError("axis range must have min < max")
    return (lo, hi)


def list_ports():
    return list(serial.tools.list_ports.comports())


class SerialReader:
    def __init__(self):
        self.rx_queue = queue.Queue(maxsize=5000)
        self._thread = None
        self._stop = threading.Event()
        self._serial = None
        self.connected = False
        self.port = None
        self.baud = None

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
        self.root.title("ViviSense Serial Occupancy Grid")

        self.reader = SerialReader()
        self.pending_by_sensor = {sid: [] for sid in range(1, 9)}
        self.points_by_sensor = {sid: [] for sid in range(1, 9)}
        self.frames_by_sensor = {sid: 0 for sid in range(1, 9)}
        self.last_frame_local_s = {sid: 0.0 for sid in range(1, 9)}
        self.rx_hz_by_sensor = {sid: 0.0 for sid in range(1, 9)}
        self.pkts_by_sensor = {sid: 0 for sid in range(1, 9)}
        self._last_draw_s = 0.0
        self.stability_counts = None
        self.stability_shape = None
        self.recording = False
        self.record_end_time_s = 0.0
        self.csv_file = None
        self.csv_writer = None
        self.csv_filename = ""
        self.csv_rows = 0
        self.events = []
        self.playing = False
        self.play_speed = 1.0
        self.play_current_idx = -1
        self.play_started_monotonic = 0.0
        self.play_started_event_time = 0.0
        self.source_mode = "live"
        self._stability_context_key = None

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
        ttk.Button(top, text="Open CSV", command=self.open_csv_dialog).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(top, text="Live", command=self.switch_to_live).pack(side=tk.LEFT, padx=(4, 0))
        self.play_btn = ttk.Button(top, text="Play", command=self.toggle_playback)
        self.play_btn.pack(side=tk.LEFT, padx=(4, 0))
        ttk.Button(top, text="Prev", command=self.playback_prev).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Button(top, text="Next", command=self.playback_next).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Label(top, text="Speed").pack(side=tk.LEFT, padx=(8, 0))
        self.play_speed_var = tk.StringVar(value="1.0")
        ttk.Entry(top, textvariable=self.play_speed_var, width=5).pack(side=tk.LEFT)
        ttk.Label(top, text="x").pack(side=tk.LEFT)
        ttk.Label(top, text="Rec s").pack(side=tk.LEFT, padx=(10, 0))
        self.record_seconds_var = tk.StringVar(value="10")
        ttk.Entry(top, textvariable=self.record_seconds_var, width=5).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Button(top, text="Record", command=self.start_recording).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Button(top, text="Stop Rec", command=self.stop_recording).pack(side=tk.LEFT, padx=(4, 0))
        self.playback_time_var = tk.StringVar(value="t=0.00s")
        ttk.Label(top, textvariable=self.playback_time_var).pack(side=tk.RIGHT, padx=(10, 0))
        self.record_banner_var = tk.StringVar(value="")
        ttk.Label(top, textvariable=self.record_banner_var, foreground="#c1121f").pack(side=tk.RIGHT)
        self.conn_var = tk.StringVar(value="Disconnected")
        ttk.Label(top, textvariable=self.conn_var).pack(side=tk.LEFT, padx=(10, 0))

        body = ttk.Frame(self.root, padding=(8, 0, 8, 8))
        body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        left_wrap = ttk.Frame(body)
        left_wrap.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 10))
        self.left_canvas = tk.Canvas(left_wrap, width=260, highlightthickness=0)
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

        def _on_mousewheel(event):
            try:
                self.left_canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")
            except Exception:
                pass

        self.left_canvas.bind_all("<MouseWheel>", _on_mousewheel)

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
            ttk.Label(row, textvariable=txt, width=30).pack(side=tk.LEFT)

        ttk.Label(left, text="Filters").pack(anchor="w", pady=(10, 0))
        self.status_filter_var = tk.StringVar(value="5")
        ttk.Label(left, text="Target status").pack(anchor="w")
        ttk.Entry(left, textvariable=self.status_filter_var, width=20).pack(anchor="w")
        ttk.Label(left, text="Blank = all, e.g. 5 or 5,9,13", width=34).pack(anchor="w")
        self.show_invalid_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(left, text="Include invalid points", variable=self.show_invalid_var, command=self._draw).pack(anchor="w")
        self.zmin_var = tk.StringVar(value="0.0")
        self.zmax_var = tk.StringVar(value="2.5")
        ttk.Label(left, text="Z min,max (m)").pack(anchor="w")
        zrow = ttk.Frame(left)
        zrow.pack(anchor="w")
        ttk.Entry(zrow, textvariable=self.zmin_var, width=8).pack(side=tk.LEFT)
        ttk.Entry(zrow, textvariable=self.zmax_var, width=8).pack(side=tk.LEFT, padx=(4, 0))

        ttk.Label(left, text="Grid / Detection").pack(anchor="w", pady=(10, 0))
        self.xlim_var = tk.StringVar(value="-3.5,3.5")
        self.ylim_var = tk.StringVar(value="-3.5,3.5")
        self.cell_size_var = tk.StringVar(value="0.1")
        self.threshold_var = tk.StringVar(value="1")
        self.decay_var = tk.StringVar(value="0.0")
        ttk.Label(left, text="X min,max").pack(anchor="w")
        ttk.Entry(left, textvariable=self.xlim_var, width=20).pack(anchor="w")
        ttk.Label(left, text="Y min,max").pack(anchor="w")
        ttk.Entry(left, textvariable=self.ylim_var, width=20).pack(anchor="w")
        ttk.Label(left, text="Cell size (m)").pack(anchor="w")
        ttk.Entry(left, textvariable=self.cell_size_var, width=20).pack(anchor="w")
        ttk.Label(left, text="Obstacle threshold (# hits/cell)").pack(anchor="w")
        ttk.Entry(left, textvariable=self.threshold_var, width=20).pack(anchor="w")
        ttk.Label(left, text="Persistence / decay (s, 0=off)").pack(anchor="w")
        ttk.Entry(left, textvariable=self.decay_var, width=20).pack(anchor="w")
        self.mode_var = tk.StringVar(value="binary")
        ttk.Label(left, text="Display mode").pack(anchor="w")
        ttk.Combobox(left, textvariable=self.mode_var, state="readonly", width=18, values=["binary", "count"]).pack(anchor="w")
        self.binary_sensor_color_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            left,
            text="Binary: color by sensor",
            variable=self.binary_sensor_color_var,
            command=self._draw,
        ).pack(anchor="w")
        ttk.Label(left, text="Sensor Color Key").pack(anchor="w", pady=(6, 0))
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
        self.show_wheelchair_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(left, text="Show wheelchair footprint", variable=self.show_wheelchair_var, command=self._draw).pack(anchor="w", pady=(4, 0))
        ttk.Label(left, text="Tick spacing (m)").pack(anchor="w")
        self.tick_spacing_var = tk.StringVar(value="0.5")
        ttk.Entry(left, textvariable=self.tick_spacing_var, width=20).pack(anchor="w")
        ttk.Button(left, text="Apply Params", command=self._draw).pack(anchor="w", pady=(6, 0))

        ttk.Label(left, text="Detection Overlays").pack(anchor="w", pady=(10, 0))

        self.temporal_enable_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(left, text="Temporal stability", variable=self.temporal_enable_var, command=self._draw).pack(anchor="w")
        trow = ttk.Frame(left)
        trow.pack(anchor="w")
        ttk.Label(trow, text="Stable frames N").pack(side=tk.LEFT)
        self.temporal_n_var = tk.StringVar(value="2")
        ttk.Entry(trow, textvariable=self.temporal_n_var, width=6).pack(side=tk.LEFT, padx=(4, 0))
        self.temporal_overlay_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(left, text="Show temporal overlay", variable=self.temporal_overlay_var, command=self._draw).pack(anchor="w")

        self.inflate_enable_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(left, text="Inflation overlay", variable=self.inflate_enable_var, command=self._draw).pack(anchor="w")
        irow = ttk.Frame(left)
        irow.pack(anchor="w")
        ttk.Label(irow, text="Radius (m)").pack(side=tk.LEFT)
        self.inflate_radius_var = tk.StringVar(value="0.35")
        ttk.Entry(irow, textvariable=self.inflate_radius_var, width=6).pack(side=tk.LEFT, padx=(4, 0))

        self.record_status_var = tk.StringVar(value="CSV: idle")
        ttk.Label(left, textvariable=self.record_status_var, width=34, wraplength=260).pack(anchor="w", pady=(10, 0))

        self.fig = plt.Figure(figsize=(8.8, 6.8), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.canvas = FigureCanvasTkAgg(self.fig, master=body)
        self.canvas.get_tk_widget().pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    def refresh_ports(self):
        ports = list_ports()
        names = [p.device for p in ports]
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
        self._reset_state()
        self.reader.connect(port, baud)
        self.conn_var.set(f"Connecting {port}...")

    def disconnect(self):
        self.reader.disconnect()
        self.conn_var.set("Disconnected")
        self.stop_recording()

    def _reset_state(self):
        self.stability_counts = None
        self.stability_shape = None
        self._stability_context_key = None
        for sid in range(1, 9):
            self.pending_by_sensor[sid] = []
            self.points_by_sensor[sid] = []
            self.frames_by_sensor[sid] = 0
            self.last_frame_local_s[sid] = 0.0
            self.rx_hz_by_sensor[sid] = 0.0
            self.pkts_by_sensor[sid] = 0

    def open_csv_dialog(self):
        path = filedialog.askopenfilename(
            title="Open point CSV",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
        )
        if path:
            self.load_playback_csv(path)

    def switch_to_live(self):
        self.source_mode = "live"
        self.playing = False
        self.play_btn.configure(text="Play")
        self.conn_var.set("Live mode")
        self.playback_time_var.set("t=0.00s")

    def load_playback_csv(self, path: str):
        self.source_mode = "playback"
        self.playing = False
        self.play_btn.configure(text="Play")
        self.events = self._parse_csv_to_events(path)
        self.play_current_idx = -1
        self._reset_state()
        if self.events:
            self._apply_playback_index(0)
            self.conn_var.set(f"Playback loaded ({len(self.events)} events)")
        else:
            self.conn_var.set("Playback CSV empty")
        self._update_playback_time_label()

    def _parse_csv_to_events(self, path: str):
        events = []
        current_key = None
        current_event = None
        with open(path, "r", newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                try:
                    sid = int(row["sid"])
                    cell = int(row["cell"])
                    valid = int(row.get("valid", "1"))
                    x = float(row["x_mm"]) / 1000.0
                    y = float(row["y_mm"]) / 1000.0
                    z = float(row["z_mm"]) / 1000.0
                    target_status = int(row["target_status"])
                    ts_ms = int(row["sensor_ts_ms"])
                    local_time_s = float(row["local_time_s"])
                except Exception:
                    continue
                key = (sid, ts_ms)
                if key != current_key:
                    current_event = {"sid": sid, "ts_ms": ts_ms, "local_time_s": local_time_s, "points": []}
                    events.append(current_event)
                    current_key = key
                current_event["points"].append((cell, valid, x, y, z, target_status))
        return events

    def _event_time_s(self, idx: int) -> float:
        if idx < 0 or idx >= len(self.events):
            return 0.0
        return float(self.events[idx]["local_time_s"])

    def _apply_playback_index(self, idx: int):
        if not self.events:
            return
        idx = max(0, min(idx, len(self.events) - 1))
        if idx < self.play_current_idx:
            self._reset_state()
            start = 0
        else:
            start = self.play_current_idx + 1
        for i in range(start, idx + 1):
            ev = self.events[i]
            sid = ev["sid"]
            self.points_by_sensor[sid] = list(ev["points"])
            self.frames_by_sensor[sid] += 1
            self.last_frame_local_s[sid] = time.time()
        self.play_current_idx = idx
        self._update_playback_time_label()

    def _update_playback_time_label(self):
        if self.source_mode != "playback" or not self.events or self.play_current_idx < 0:
            self.playback_time_var.set("t=0.00s")
            return
        t0 = self._event_time_s(0)
        t = self._event_time_s(self.play_current_idx) - t0
        self.playback_time_var.set(f"t={t:.2f}s")

    def toggle_playback(self):
        if self.source_mode != "playback" or not self.events:
            return
        if self.playing:
            self.playing = False
            self.play_btn.configure(text="Play")
            return
        if self.play_current_idx >= len(self.events) - 1:
            self._apply_playback_index(0)
        try:
            self.play_speed = float(self.play_speed_var.get().strip())
        except ValueError:
            self.play_speed = 1.0
        if self.play_speed <= 0:
            self.play_speed = 1.0
        self.play_started_monotonic = time.monotonic()
        self.play_started_event_time = self._event_time_s(self.play_current_idx)
        self.playing = True
        self.play_btn.configure(text="Pause")

    def playback_next(self):
        if self.source_mode != "playback" or not self.events:
            return
        self.playing = False
        self.play_btn.configure(text="Play")
        if self.play_current_idx < 0:
            self._apply_playback_index(0)
        else:
            self._apply_playback_index(min(self.play_current_idx + 1, len(self.events) - 1))

    def playback_prev(self):
        if self.source_mode != "playback" or not self.events:
            return
        self.playing = False
        self.play_btn.configure(text="Play")
        self._apply_playback_index(max(self.play_current_idx - 1, 0))

    def start_recording(self):
        if self.recording:
            return
        try:
            duration_s = float(self.record_seconds_var.get().strip())
        except ValueError:
            self.record_status_var.set("CSV: invalid seconds")
            return
        if duration_s <= 0:
            self.record_status_var.set("CSV: seconds must be > 0")
            return
        fn = f"vivisense_occupancy_points_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        try:
            f = open(fn, "w", newline="", encoding="utf-8")
        except Exception as exc:
            self.record_status_var.set(f"CSV open failed: {exc}")
            return
        w = csv.writer(f)
        w.writerow(["local_time_s", "sid", "cell", "valid", "x_mm", "y_mm", "z_mm", "target_status", "sensor_ts_ms"])
        self.csv_file = f
        self.csv_writer = w
        self.csv_filename = fn
        self.csv_rows = 0
        self.recording = True
        self.record_end_time_s = time.time() + duration_s
        self.record_status_var.set(f"CSV: recording {fn}")
        self.record_banner_var.set("RECORDING")

    def stop_recording(self):
        if not self.recording and self.csv_file is None:
            self.record_status_var.set("CSV: idle")
            return
        fn = self.csv_filename
        rows = self.csv_rows
        try:
            if self.csv_file is not None:
                self.csv_file.flush()
                self.csv_file.close()
        except Exception:
            pass
        self.recording = False
        self.record_end_time_s = 0.0
        self.csv_file = None
        self.csv_writer = None
        self.csv_filename = ""
        self.csv_rows = 0
        self.record_banner_var.set("")
        self.record_status_var.set(f"CSV: saved {fn} ({rows} rows)" if fn else "CSV: idle")

    def _record_points_for_sensor(self, sid: int, points, ts_ms: int):
        if not self.recording or self.csv_writer is None:
            return
        now_s = time.time()
        for cell, valid, x, y, z, target_status in points:
            self.csv_writer.writerow(
                [f"{now_s:.3f}", sid, cell, valid, f"{x*1000.0:.2f}", f"{y*1000.0:.2f}", f"{z*1000.0:.2f}", target_status, ts_ms]
            )
            self.csv_rows += 1

    def _parse_status_filter(self):
        text = self.status_filter_var.get().strip()
        if not text:
            return None
        vals = set()
        for part in text.split(","):
            part = part.strip()
            if not part:
                continue
            try:
                vals.add(int(part))
            except ValueError:
                continue
        return vals if vals else None

    def _handle_line(self, line: str):
        if line.startswith("P,"):
            parts = line.split(",")
            if len(parts) not in (8, 9):
                return
            try:
                sid = int(parts[1])
                cell = int(parts[2])
                if len(parts) == 9:
                    valid = int(parts[3])
                    x = float(parts[4])
                    y = float(parts[5])
                    z = float(parts[6])
                    target_status = int(parts[7])
                else:
                    valid = 1
                    x = float(parts[3])
                    y = float(parts[4])
                    z = float(parts[5])
                    target_status = int(parts[6])
            except ValueError:
                return
            if sid in self.pending_by_sensor:
                self.pending_by_sensor[sid].append((cell, valid, x, y, z, target_status))
            return

        if line.startswith("E,"):
            parts = line.split(",")
            if len(parts) != 3:
                return
            try:
                sid = int(parts[1])
                ts_ms = int(parts[2])
            except ValueError:
                return
            if sid not in self.pending_by_sensor:
                return
            self.points_by_sensor[sid] = self.pending_by_sensor[sid]
            self._record_points_for_sensor(sid, self.points_by_sensor[sid], ts_ms)
            self.pending_by_sensor[sid] = []
            self.frames_by_sensor[sid] += 1
            self.last_frame_local_s[sid] = time.time()
            return

        if line.startswith("S,"):
            parts = line.split(",")
            if len(parts) != 4:
                return
            try:
                sid = int(parts[1])
                pkts = int(parts[2])
                hz = float(parts[3])
            except ValueError:
                return
            if sid in self.pkts_by_sensor:
                self.pkts_by_sensor[sid] = pkts
                self.rx_hz_by_sensor[sid] = hz
            return

        if line.startswith("INFO,open,"):
            self.conn_var.set("Connected")
        elif line.startswith("INFO,closed"):
            self.conn_var.set("Disconnected")
        elif line.startswith("ERR,"):
            self.conn_var.set("Error")

    def _drain_serial(self):
        n = 0
        while n < 4000:
            try:
                line = self.reader.rx_queue.get_nowait()
            except queue.Empty:
                break
            self._handle_line(line)
            n += 1

    def _filtered_points(self):
        allowed_status = self._parse_status_filter()
        show_invalid = self.show_invalid_var.get()
        try:
            zmin = float(self.zmin_var.get().strip())
            zmax = float(self.zmax_var.get().strip())
        except ValueError:
            zmin, zmax = -1e9, 1e9
        rows = []
        for sid in range(1, 9):
            if not self.sensor_enabled_vars[sid].get():
                continue
            for p in self.points_by_sensor[sid]:
                cell, valid, x, y, z, target_status = p
                if (not show_invalid) and (not valid):
                    continue
                if allowed_status is not None and target_status not in allowed_status:
                    continue
                if z < zmin or z > zmax:
                    continue
                rows.append((sid, x, y, z, target_status, cell, valid))
        return rows

    def _draw_wheelchair_footprint(self):
        if not self.show_wheelchair_var.get():
            return
        # Apply same display rotation as grid: -90 deg (clockwise) in XY.
        x0, x1 = -WHEELCHAIR_LENGTH_M, 0.0
        y0, y1 = -WHEELCHAIR_WIDTH_M * 0.5, WHEELCHAIR_WIDTH_M * 0.5
        xs = [x0, x1, x1, x0, x0]
        ys = [y0, y0, y1, y1, y0]
        # (x', y') = (y, -x)
        rx = ys
        ry = [-x for x in xs]
        # Flip displayed Y so the chair front faces "up" on screen.
        ry = [-v for v in ry]
        origin_rx, origin_ry = 0.0, 0.0
        self.ax.plot(rx, ry, color="#202020", linewidth=1.5)
        self.ax.scatter([origin_rx], [origin_ry], s=25, color="#202020", marker="x")

    @staticmethod
    def _to_view_xy(x_vals, y_vals):
        # Rotated occupancy display uses (u, v) = (Y, X)
        return y_vals, x_vals

    def _disk_offsets(self, radius_cells: int):
        offs = []
        r2 = radius_cells * radius_cells
        for dy in range(-radius_cells, radius_cells + 1):
            for dx in range(-radius_cells, radius_cells + 1):
                if (dx * dx + dy * dy) <= r2:
                    offs.append((dy, dx))
        return offs

    def _inflate_mask(self, mask: np.ndarray, radius_cells: int) -> np.ndarray:
        if radius_cells <= 0:
            return mask.copy()
        h, w = mask.shape
        out = np.zeros_like(mask, dtype=bool)
        ys, xs = np.nonzero(mask)
        if len(xs) == 0:
            return out
        offsets = self._disk_offsets(radius_cells)
        for y, x in zip(ys, xs):
            for dy, dx in offsets:
                yy = y + dy
                xx = x + dx
                if 0 <= yy < h and 0 <= xx < w:
                    out[yy, xx] = True
        return out

    def _apply_temporal_stability(self, occ_mask: np.ndarray, stable_n: int) -> np.ndarray:
        shape = occ_mask.shape
        if self.stability_counts is None or self.stability_shape != shape:
            self.stability_counts = np.zeros(shape, dtype=np.uint16)
            self.stability_shape = shape
        self.stability_counts[occ_mask] = np.minimum(self.stability_counts[occ_mask] + 1, 65535)
        self.stability_counts[~occ_mask] = 0
        return self.stability_counts >= max(1, stable_n)

    def _cell_center_world(self, ix: int, iy: int, x_min: float, y_min: float, cell: float):
        x = x_min + (ix + 0.5) * cell
        y = y_min + (iy + 0.5) * cell
        return x, y

    def _draw(self):
        try:
            xlim = parse_axis_range(self.xlim_var.get().strip())
            ylim = parse_axis_range(self.ylim_var.get().strip())
            cell = float(self.cell_size_var.get().strip())
            threshold = max(1, int(self.threshold_var.get().strip()))
            decay_s = max(0.0, float(self.decay_var.get().strip()))
            tick_spacing = float(self.tick_spacing_var.get().strip())
            temporal_n = max(1, int(self.temporal_n_var.get().strip()))
            inflate_radius_m = max(0.0, float(self.inflate_radius_var.get().strip()))
        except ValueError:
            return
        if cell <= 0:
            return
        if tick_spacing <= 0:
            tick_spacing = 0.5

        rows = self._filtered_points()
        x_min, x_max = xlim
        y_min, y_max = ylim
        nx = max(1, int(np.ceil((x_max - x_min) / cell)))
        ny = max(1, int(np.ceil((y_max - y_min) / cell)))
        grid = np.zeros((ny, nx), dtype=np.uint16)
        sensor_grid = np.zeros((ny, nx), dtype=np.uint8)

        now = time.time()
        for sid, x_m, y_m, _z_m, _status, _cell_id, _valid in rows:
            if decay_s > 0 and self.last_frame_local_s[sid] > 0 and (now - self.last_frame_local_s[sid]) > decay_s:
                continue
            if x_m < x_min or x_m >= x_max or y_m < y_min or y_m >= y_max:
                continue
            ix = int((x_m - x_min) / cell)
            iy = int((y_m - y_min) / cell)
            if 0 <= ix < nx and 0 <= iy < ny:
                grid[iy, ix] = min(grid[iy, ix] + 1, 65535)
                if sensor_grid[iy, ix] == 0:
                    sensor_grid[iy, ix] = sid

        mode = self.mode_var.get().strip().lower()
        raw_occ = grid >= threshold
        stability_context_key = (
            tuple(bool(self.sensor_enabled_vars[s].get()) for s in range(1, 9)),
            self.status_filter_var.get().strip(),
            bool(self.show_invalid_var.get()),
            self.zmin_var.get().strip(),
            self.zmax_var.get().strip(),
            xlim,
            ylim,
            round(cell, 6),
            threshold,
            round(decay_s, 6),
            bool(self.temporal_enable_var.get()),
            temporal_n,
            bool(self.inflate_enable_var.get()),
            round(inflate_radius_m, 6),
        )
        if self._stability_context_key != stability_context_key:
            self.stability_counts = None
            self.stability_shape = None
            self._stability_context_key = stability_context_key
        if self.temporal_enable_var.get():
            occ_for_detection = self._apply_temporal_stability(raw_occ, temporal_n)
        else:
            self.stability_counts = None
            self.stability_shape = None
            occ_for_detection = raw_occ.copy()

        inflate_cells = int(round(inflate_radius_m / cell)) if cell > 0 else 0
        inflated_occ = self._inflate_mask(occ_for_detection, inflate_cells) if self.inflate_enable_var.get() else occ_for_detection.copy()

        if mode == "binary":
            if self.binary_sensor_color_var.get():
                display = np.zeros_like(sensor_grid, dtype=np.uint8)
                occupied = raw_occ
                display[occupied] = sensor_grid[occupied]
                colors = ["#ffffff"] + [f"#{r:02x}{g:02x}{b:02x}" for _, (r, g, b) in sorted(SENSOR_RGB.items())]
                cmap = ListedColormap(colors)
                norm = BoundaryNorm(np.arange(-0.5, 9.5, 1.0), cmap.N)
                vmax = None
            else:
                display = inflated_occ.astype(np.uint8)
                cmap = "Greys"
                norm = None
                vmax = 1
        else:
            display = grid
            cmap = "viridis"
            norm = None
            vmax = max(int(grid.max()), threshold)

        self.ax.clear()
        # Rotate displayed occupancy by -90 deg (clockwise) for easier interpretation.
        display_rot = np.rot90(display, k=-1)
        # Rotated display swaps axes: x' corresponds to original Y, y' corresponds to -original X.
        # Flip displayed vertical axis so wheelchair front points upward.
        rot_extent = [y_min, y_max, x_min, x_max]

        imshow_kwargs = dict(
            origin="lower",
            extent=rot_extent,
            interpolation="nearest",
            aspect="equal",
            cmap=cmap,
        )
        if norm is not None:
            imshow_kwargs["norm"] = norm
        else:
            imshow_kwargs["vmin"] = 0
            imshow_kwargs["vmax"] = vmax
        self.ax.imshow(display_rot, **imshow_kwargs)
        # Inflation overlay (for color-by-sensor binary or count mode where inflation is not the base image).
        if self.inflate_enable_var.get() and (mode != "binary" or self.binary_sensor_color_var.get()):
            infl_disp = np.rot90(inflated_occ.astype(np.uint8), k=-1)
            self.ax.imshow(
                infl_disp,
                origin="lower",
                extent=rot_extent,
                interpolation="nearest",
                aspect="equal",
                cmap=ListedColormap([(1, 1, 1, 0.0), (1.0, 0.0, 0.0, 0.22)]),
                vmin=0,
                vmax=1,
            )
        if self.temporal_enable_var.get() and self.temporal_overlay_var.get():
            temp_disp = np.rot90(occ_for_detection.astype(np.uint8), k=-1)
            self.ax.imshow(
                temp_disp,
                origin="lower",
                extent=rot_extent,
                interpolation="nearest",
                aspect="equal",
                cmap=ListedColormap([(1, 1, 1, 0.0), (0.0, 0.8, 1.0, 0.28)]),
                vmin=0,
                vmax=1,
            )
        self._draw_wheelchair_footprint()

        self.ax.set_xlabel("Y (m)  [rotated view]")
        self.ax.set_ylabel("X (m)  [rotated view]")
        self.ax.xaxis.set_major_locator(MultipleLocator(tick_spacing))
        self.ax.yaxis.set_major_locator(MultipleLocator(tick_spacing))
        self.ax.set_title(
            f"Live Occupancy Grid  mode={mode}  cell={cell:.3f}m  threshold={threshold}  points={len(rows)}"
        )
        self.ax.grid(True, color=(0.8, 0.8, 0.8, 0.5), linewidth=0.6)
        self.canvas.draw_idle()

    def _update_status(self):
        now = time.time()
        for sid in range(1, 9):
            online = self.frames_by_sensor[sid] and ((now - self.last_frame_local_s[sid]) < ONLINE_TIMEOUT_S)
            state = "online" if online else "offline"
            self.sensor_status_vars[sid].set(
                f"S{sid}: {state:<7} pkts={self.pkts_by_sensor[sid]:<6} hz={self.rx_hz_by_sensor[sid]:>4.1f}"
            )

    def _tick(self):
        if self.source_mode == "live":
            self._drain_serial()
            if self.recording and time.time() >= self.record_end_time_s:
                self.stop_recording()
        elif self.source_mode == "playback" and self.playing and self.events:
            try:
                self.play_speed = float(self.play_speed_var.get().strip())
            except ValueError:
                self.play_speed = 1.0
            if self.play_speed <= 0:
                self.play_speed = 1.0
            now_mono = time.monotonic()
            if self.play_started_monotonic == 0.0:
                self.play_started_monotonic = now_mono
                self.play_started_event_time = self._event_time_s(max(self.play_current_idx, 0))
            elapsed_real = now_mono - self.play_started_monotonic
            target_event_time = self.play_started_event_time + (elapsed_real * self.play_speed)
            while self.play_current_idx < len(self.events) - 1 and self._event_time_s(self.play_current_idx + 1) <= target_event_time:
                self._apply_playback_index(self.play_current_idx + 1)
            if self.play_current_idx >= len(self.events) - 1:
                self.playing = False
                self.play_btn.configure(text="Play")
            self._update_playback_time_label()
        elif self.source_mode == "playback":
            self._update_playback_time_label()
        self._update_status()
        now = time.time()
        if (now - self._last_draw_s) >= 0.08:
            self._draw()
            self._last_draw_s = now
        self.root.after(40, self._tick)

    def _on_close(self):
        self.reader.disconnect()
        self.root.destroy()


def default_port():
    ports = list_ports()
    return ports[0].device if ports else None


def main():
    parser = argparse.ArgumentParser(description="Live 2D occupancy grid viewer over serial (ViviSense receiver)")
    parser.add_argument("--port", default=default_port(), help="Default serial port (e.g. COM4)")
    parser.add_argument("--baud", type=int, default=921600, help="Default serial baud rate")
    args = parser.parse_args()

    root = tk.Tk()
    App(root, args.port, args.baud)
    root.mainloop()


if __name__ == "__main__":
    main()
