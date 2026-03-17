import argparse
import csv
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk
from datetime import datetime

import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401
from matplotlib.ticker import MultipleLocator
import serial
import serial.tools.list_ports


COLORS = {
    1: "#e63946",
    2: "#f4a261",
    3: "#e9c46a",
    4: "#2a9d8f",
    5: "#457b9d",
    6: "#1d3557",
    7: "#8d99ae",
    8: "#6a4c93",
}

ONLINE_TIMEOUT_S = 1.5
UI_TICK_MS = 20
DEFAULT_DRAW_FPS = 15.0
INCH_TO_M = 0.0254
WHEELCHAIR_LENGTH_M = 31.0 * INCH_TO_M
WHEELCHAIR_WIDTH_M = 23.0 * INCH_TO_M
WHEELCHAIR_HEIGHT_M = 37.0 * INCH_TO_M
WHEELCHAIR_FRONT_OFFSET_M = 7.0 * INCH_TO_M
DEFAULT_VIEW_ELEV = 22
DEFAULT_VIEW_AZIM = 120
TOP_VIEW_ELEV = 90
TOP_VIEW_AZIM = 180
BEHIND_VIEW_ELEV = 25
BEHIND_VIEW_AZIM = 180
SIDE_VIEW_ELEV = 12
SIDE_VIEW_AZIM = 90
DEFAULT_Z_MAX_M = 5.0


def draw_wheelchair_box(ax):
    # World-frame origin is 7 inches behind the wheelchair front-center.
    # +X is forward, so front is at +WHEELCHAIR_FRONT_OFFSET_M and rear is behind it.
    x1 = WHEELCHAIR_FRONT_OFFSET_M
    x0 = x1 - WHEELCHAIR_LENGTH_M
    y0, y1 = -WHEELCHAIR_WIDTH_M * 0.5, WHEELCHAIR_WIDTH_M * 0.5
    z0, z1 = 0.0, WHEELCHAIR_HEIGHT_M
    pts = {
        "a": (x0, y0, z0),
        "b": (x1, y0, z0),
        "c": (x1, y1, z0),
        "d": (x0, y1, z0),
        "e": (x0, y0, z1),
        "f": (x1, y0, z1),
        "g": (x1, y1, z1),
        "h": (x0, y1, z1),
    }
    edges = [("a", "b"), ("b", "c"), ("c", "d"), ("d", "a"),
             ("e", "f"), ("f", "g"), ("g", "h"), ("h", "e"),
             ("a", "e"), ("b", "f"), ("c", "g"), ("d", "h")]
    for p0, p1 in edges:
        x = [pts[p0][0], pts[p1][0]]
        y = [pts[p0][1], pts[p1][1]]
        z = [pts[p0][2], pts[p1][2]]
        ax.plot(x, y, z, color="#444444", linewidth=1.5)


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
        # Larger queue prevents line drops when plotting briefly stalls.
        self.rx_queue = queue.Queue(maxsize=30000)
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
    def __init__(
        self,
        root: tk.Tk,
        default_port: str | None,
        baud: int,
        axis_limit: float,
        xlim=None,
        ylim=None,
        zlim=None,
        major_tick_m: float = 0.5,
    ):
        self.root = root
        self.root.title("ViviSense Serial Point Cloud")

        self.axis_limit = axis_limit
        self.xlim = xlim if xlim is not None else (-axis_limit, axis_limit)
        self.ylim = ylim if ylim is not None else (-axis_limit, axis_limit)
        self.zlim = zlim if zlim is not None else (0.0, DEFAULT_Z_MAX_M)
        self.major_tick_m = major_tick_m
        self.reader = SerialReader()

        self.pending_by_sensor = {sid: [] for sid in range(1, 9)}
        self.points_by_sensor = {sid: [] for sid in range(1, 9)}
        self.frames_by_sensor = {sid: 0 for sid in range(1, 9)}
        self.last_ts_by_sensor = {sid: -1 for sid in range(1, 9)}
        self.last_frame_local_s = {sid: 0.0 for sid in range(1, 9)}
        self.rx_hz_by_sensor = {sid: 0.0 for sid in range(1, 9)}
        self.pkts_by_sensor = {sid: 0 for sid in range(1, 9)}
        self.recording = False
        self.record_end_time_s = 0.0
        self.csv_file = None
        self.csv_writer = None
        self.csv_filename = ""
        self.csv_rows = 0
        self._view_initialized = False
        self.draw_fps = DEFAULT_DRAW_FPS
        self._last_draw_monotonic = 0.0

        self._build_ui(default_port, baud)
        self.refresh_ports()
        self.root.after(UI_TICK_MS, self._tick)

    def _build_ui(self, default_port: str | None, baud: int):
        top = ttk.Frame(self.root, padding=8)
        top.pack(side=tk.TOP, fill=tk.X)

        ttk.Label(top, text="COM").pack(side=tk.LEFT)
        self.port_var = tk.StringVar(value=default_port or "")
        self.port_combo = ttk.Combobox(top, textvariable=self.port_var, width=18, state="readonly")
        self.port_combo.pack(side=tk.LEFT, padx=(4, 8))

        ttk.Button(top, text="Refresh", command=self.refresh_ports).pack(side=tk.LEFT)

        ttk.Label(top, text="Baud").pack(side=tk.LEFT, padx=(12, 0))
        self.baud_var = tk.StringVar(value=str(baud))
        ttk.Entry(top, textvariable=self.baud_var, width=10).pack(side=tk.LEFT, padx=(4, 8))

        self.connect_btn = ttk.Button(top, text="Connect", command=self.connect)
        self.connect_btn.pack(side=tk.LEFT)
        self.disconnect_btn = ttk.Button(top, text="Disconnect", command=self.disconnect)
        self.disconnect_btn.pack(side=tk.LEFT, padx=(6, 0))

        self.conn_label_var = tk.StringVar(value="Disconnected")
        ttk.Label(top, textvariable=self.conn_label_var).pack(side=tk.LEFT, padx=(12, 0))

        body = ttk.Frame(self.root, padding=(8, 0, 8, 8))
        body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        left = ttk.Frame(body)
        left.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 10))

        ttk.Label(left, text="Sensor Status").pack(anchor="w")
        self.sensor_status_vars = {}
        self.sensor_enabled_vars = {}
        for sid in range(1, 9):
            var = tk.StringVar(value=f"S{sid}: offline  pkts=0  hz=0.0")
            self.sensor_status_vars[sid] = var
            row = ttk.Frame(left)
            row.pack(anchor="w", fill=tk.X)
            enabled = tk.BooleanVar(value=True)
            self.sensor_enabled_vars[sid] = enabled
            ttk.Checkbutton(row, variable=enabled).pack(side=tk.LEFT)
            ttk.Label(row, textvariable=var, width=30).pack(side=tk.LEFT)

        ttk.Label(left, text="Target Status Filter").pack(anchor="w", pady=(10, 0))
        self.status_filter_var = tk.StringVar(value="")
        ttk.Entry(left, textvariable=self.status_filter_var, width=20).pack(anchor="w")
        ttk.Label(left, text="Blank = all, e.g. 5 or 5,9,13", width=34).pack(anchor="w")
        self.show_wheelchair_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(left, text="Show wheelchair box", variable=self.show_wheelchair_var).pack(anchor="w", pady=(6, 0))

        ttk.Label(left, text="Axis Limits (m)").pack(anchor="w", pady=(10, 0))
        self.xlim_var = tk.StringVar(value=f"{self.xlim[0]},{self.xlim[1]}")
        self.ylim_var = tk.StringVar(value=f"{self.ylim[0]},{self.ylim[1]}")
        self.zlim_var = tk.StringVar(value=f"{self.zlim[0]},{self.zlim[1]}")
        ttk.Label(left, text="X min,max").pack(anchor="w")
        ttk.Entry(left, textvariable=self.xlim_var, width=20).pack(anchor="w")
        ttk.Label(left, text="Y min,max").pack(anchor="w")
        ttk.Entry(left, textvariable=self.ylim_var, width=20).pack(anchor="w")
        ttk.Label(left, text="Z min,max").pack(anchor="w")
        ttk.Entry(left, textvariable=self.zlim_var, width=20).pack(anchor="w")
        ttk.Button(left, text="Apply Axes", command=self.apply_axes).pack(anchor="w", pady=(4, 0))
        ttk.Label(left, text="Major XY tick (m)").pack(anchor="w")
        self.major_tick_var = tk.StringVar(value=f"{self.major_tick_m:g}")
        ttk.Entry(left, textvariable=self.major_tick_var, width=20).pack(anchor="w")
        ttk.Label(left, text="View").pack(anchor="w", pady=(10, 0))
        view_row = ttk.Frame(left)
        view_row.pack(anchor="w", fill=tk.X)
        ttk.Button(view_row, text="Default", command=self.set_view_default).pack(side=tk.LEFT)
        ttk.Button(view_row, text="Behind", command=self.set_view_behind).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Button(view_row, text="Top", command=self.set_view_top).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Button(view_row, text="Side", command=self.set_view_side).pack(side=tk.LEFT, padx=(4, 0))

        ttk.Label(left, text="CSV Recording").pack(anchor="w", pady=(10, 0))
        rec_row = ttk.Frame(left)
        rec_row.pack(anchor="w", fill=tk.X)
        ttk.Label(rec_row, text="Seconds").pack(side=tk.LEFT)
        self.record_seconds_var = tk.StringVar(value="10")
        ttk.Entry(rec_row, textvariable=self.record_seconds_var, width=8).pack(side=tk.LEFT, padx=(4, 6))
        ttk.Button(rec_row, text="Record", command=self.start_recording).pack(side=tk.LEFT)
        ttk.Button(rec_row, text="Stop", command=self.stop_recording).pack(side=tk.LEFT, padx=(6, 0))
        self.record_status_var = tk.StringVar(value="CSV: idle")
        ttk.Label(left, textvariable=self.record_status_var, width=34, wraplength=260).pack(anchor="w")

        self.fig = plt.Figure(figsize=(8.5, 6.0), dpi=100)
        self.ax = self.fig.add_subplot(111, projection="3d")
        self.canvas = FigureCanvasTkAgg(self.fig, master=body)
        self.canvas.get_tk_widget().pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

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
            self.conn_label_var.set("No COM port")
            return
        try:
            baud = int(self.baud_var.get().strip())
        except ValueError:
            self.conn_label_var.set("Invalid baud")
            return

        self._reset_stream_state()
        self.reader.connect(port, baud)
        self.conn_label_var.set(f"Connecting to {port}...")

    def disconnect(self):
        self.reader.disconnect()
        self.conn_label_var.set("Disconnected")
        self.stop_recording()

    def _reset_stream_state(self):
        for sid in range(1, 9):
            self.pending_by_sensor[sid] = []
            self.points_by_sensor[sid] = []
            self.frames_by_sensor[sid] = 0
            self.last_ts_by_sensor[sid] = -1
            self.last_frame_local_s[sid] = 0.0
            self.rx_hz_by_sensor[sid] = 0.0
            self.pkts_by_sensor[sid] = 0

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

        fn = f"vivisense_serial_points_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        try:
            f = open(fn, "w", newline="", encoding="utf-8")
        except Exception as exc:
            self.record_status_var.set(f"CSV open failed: {exc}")
            return

        w = csv.writer(f)
        w.writerow([
            "local_time_s",
            "sid",
            "cell",
            "valid",
            "x_mm",
            "y_mm",
            "z_mm",
            "target_status",
            "sensor_ts_ms",
        ])

        self.csv_file = f
        self.csv_writer = w
        self.csv_filename = fn
        self.csv_rows = 0
        self.recording = True
        self.record_end_time_s = time.time() + duration_s
        self.record_status_var.set(f"CSV: recording {fn}")

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

        if fn:
            self.record_status_var.set(f"CSV: saved {fn} ({rows} rows)")
        else:
            self.record_status_var.set("CSV: idle")

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
                    # Backward compatibility with older receiver format.
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
                ts = int(parts[2])
            except ValueError:
                return
            if sid not in self.pending_by_sensor:
                return
            self.points_by_sensor[sid] = self.pending_by_sensor[sid]
            self._record_points_for_sensor(sid, self.points_by_sensor[sid], ts)
            self.pending_by_sensor[sid] = []
            self.frames_by_sensor[sid] += 1
            self.last_ts_by_sensor[sid] = ts
            self.last_frame_local_s[sid] = time.time()
            return

        if line.startswith("S,"):
            parts = line.split(",")
            if len(parts) < 4:
                return
            try:
                sid = int(parts[1])
                pkts = int(parts[2])
                hz = float(parts[3])
            except ValueError:
                return
            if sid not in self.rx_hz_by_sensor:
                return
            self.pkts_by_sensor[sid] = pkts
            self.rx_hz_by_sensor[sid] = hz
            return

        if line.startswith("INFO,open,"):
            self.conn_label_var.set("Connected")
            return

        if line.startswith("INFO,closed"):
            self.conn_label_var.set("Disconnected")
            return

        if line.startswith("ERR,"):
            self.conn_label_var.set("Error")
            return

    def _record_points_for_sensor(self, sid: int, points, ts_ms: int):
        if not self.recording or self.csv_writer is None:
            return
        now_s = time.time()
        for p in points:
            # points are stored as meters in viewer state; convert to mm for CSV
            self.csv_writer.writerow([
                f"{now_s:.3f}",
                sid,
                p[0],
                p[1],
                f"{p[2] * 1000.0:.2f}",
                f"{p[3] * 1000.0:.2f}",
                f"{p[4] * 1000.0:.2f}",
                p[5],
                ts_ms,
            ])
            self.csv_rows += 1

    def _drain_serial_queue(self):
        processed = 0
        while processed < 12000:
            try:
                line = self.reader.rx_queue.get_nowait()
            except queue.Empty:
                break
            self._handle_line(line)
            processed += 1

    def _update_status_panel(self):
        now = time.time()
        for sid in range(1, 9):
            online = (now - self.last_frame_local_s[sid]) < ONLINE_TIMEOUT_S if self.frames_by_sensor[sid] else False
            state = "online" if online else "offline"
            self.sensor_status_vars[sid].set(
                f"S{sid}: {state:<7} pkts={self.pkts_by_sensor[sid]:<6} hz={self.rx_hz_by_sensor[sid]:>4.1f}"
            )

    def _draw_plot(self):
        self.ax.clear()
        self.ax.set_xlim(*self.xlim)
        self.ax.set_ylim(*self.ylim)
        self.ax.set_zlim(*self.zlim)
        self.ax.set_xlabel("X (m)")
        self.ax.set_ylabel("Y (m)")
        self.ax.set_zlabel("Z (m)")
        if not self._view_initialized:
            self.ax.view_init(elev=DEFAULT_VIEW_ELEV, azim=DEFAULT_VIEW_AZIM)
            self._view_initialized = True
        self.ax.grid(True)
        try:
            if self.major_tick_m > 0:
                self.ax.xaxis.set_major_locator(MultipleLocator(self.major_tick_m))
                self.ax.yaxis.set_major_locator(MultipleLocator(self.major_tick_m))
            # Disable minor ticks for cleaner display.
            self.ax.xaxis.set_minor_locator(MultipleLocator(1e9))
            self.ax.yaxis.set_minor_locator(MultipleLocator(1e9))
            self.ax.zaxis.set_minor_locator(MultipleLocator(1e9))
            self.ax.tick_params(axis="z", which="minor", length=0, width=0)
            self.ax.tick_params(axis="x", which="major", length=7, width=1.4, colors="#1f1f1f")
            self.ax.tick_params(axis="y", which="major", length=7, width=1.4, colors="#1f1f1f")
            self.ax.tick_params(axis="z", which="major", length=7, width=1.4, colors="#1f1f1f")
            self.ax.tick_params(axis="x", which="minor", length=0, width=0)
            self.ax.tick_params(axis="y", which="minor", length=0, width=0)
            # Slightly darker major grid lines for readability.
            self.ax.xaxis._axinfo["grid"]["color"] = (0.62, 0.62, 0.62, 0.75)
            self.ax.yaxis._axinfo["grid"]["color"] = (0.62, 0.62, 0.62, 0.75)
            self.ax.zaxis._axinfo["grid"]["color"] = (0.78, 0.78, 0.78, 0.55)
            self.ax.xaxis._axinfo["grid"]["linewidth"] = 1.0
            self.ax.yaxis._axinfo["grid"]["linewidth"] = 1.0
            self.ax.zaxis._axinfo["grid"]["linewidth"] = 0.8
        except Exception:
            pass
        if self.show_wheelchair_var.get():
            draw_wheelchair_box(self.ax)

        allowed_status = self._parse_status_filter()
        total_pts = 0
        xs_all = []
        ys_all = []
        zs_all = []
        cs_all = []
        for sid in range(1, 9):
            if not self.sensor_enabled_vars[sid].get():
                continue
            pts = self.points_by_sensor[sid]
            if not pts:
                continue
            if allowed_status is None:
                draw_pts = pts
            else:
                draw_pts = [p for p in pts if p[5] in allowed_status]
            draw_pts = [p for p in draw_pts if p[1]]
            if not draw_pts:
                continue
            color = COLORS.get(sid, "#333333")
            for p in draw_pts:
                xs_all.append(p[2])
                ys_all.append(p[3])
                zs_all.append(p[4])
                cs_all.append(color)
            total_pts += len(draw_pts)

        if total_pts:
            # Single scatter call is much faster than per-sensor scatter + legend rebuild.
            self.ax.scatter(xs_all, ys_all, zs_all, s=18, c=cs_all, depthshade=False)

        port_text = self.port_var.get().strip() or "no-port"
        self.ax.set_title(f"{port_text}  points={total_pts}  draw_fps={self.draw_fps:.0f}  q={self.reader.rx_queue.qsize()}")
        self.canvas.draw_idle()

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

    def apply_axes(self):
        try:
            self.xlim = parse_axis_range(self.xlim_var.get().strip())
            self.ylim = parse_axis_range(self.ylim_var.get().strip())
            self.zlim = parse_axis_range(self.zlim_var.get().strip())
            self.major_tick_m = float(self.major_tick_var.get().strip())
            if self.major_tick_m <= 0:
                self.major_tick_m = 0.0
        except ValueError:
            return

    def _set_view(self, elev: float, azim: float):
        self.ax.view_init(elev=elev, azim=azim)
        self._view_initialized = True
        self.canvas.draw_idle()

    def set_view_default(self):
        self._set_view(DEFAULT_VIEW_ELEV, DEFAULT_VIEW_AZIM)

    def set_view_behind(self):
        self._set_view(BEHIND_VIEW_ELEV, BEHIND_VIEW_AZIM)

    def set_view_top(self):
        self._set_view(TOP_VIEW_ELEV, TOP_VIEW_AZIM)

    def set_view_side(self):
        self._set_view(SIDE_VIEW_ELEV, SIDE_VIEW_AZIM)

    def _tick(self):
        self._drain_serial_queue()
        if self.recording and time.time() >= self.record_end_time_s:
            self.stop_recording()
        self._update_status_panel()
        now = time.monotonic()
        draw_period_s = 1.0 / self.draw_fps if self.draw_fps > 0.0 else 0.0
        if (now - self._last_draw_monotonic) >= draw_period_s:
            self._draw_plot()
            self._last_draw_monotonic = now
        self.root.after(UI_TICK_MS, self._tick)

    def _on_close(self):
        self.stop_recording()
        self.reader.disconnect()
        self.root.destroy()


def default_port():
    ports = list_ports()
    return ports[0].device if ports else None


def main():
    parser = argparse.ArgumentParser(description="Live 3D point cloud viewer over serial (ViviSense receiver)")
    parser.add_argument("--port", default=default_port(), help="Default serial port (e.g. COM5)")
    parser.add_argument("--baud", type=int, default=921600, help="Default serial baud rate")
    parser.add_argument("--axis", type=float, default=3.5, help="Symmetric XY axis limit in meters")
    parser.add_argument("--xlim", type=str, default=None, help="X axis range in meters as min,max (e.g. -2.5,2.5)")
    parser.add_argument("--ylim", type=str, default=None, help="Y axis range in meters as min,max")
    parser.add_argument("--zlim", type=str, default=None, help="Z axis range in meters as min,max")
    parser.add_argument("--major-tick", type=float, default=0.5, help="Major XY tick spacing in meters (<=0 disables)")
    parser.add_argument("--list-ports", action="store_true", help="List serial ports and exit")
    args = parser.parse_args()

    if args.list_ports:
        for p in list_ports():
            print(f"{p.device}  {p.description}")
        return

    try:
        xlim = parse_axis_range(args.xlim) if args.xlim else None
        ylim = parse_axis_range(args.ylim) if args.ylim else None
        zlim = parse_axis_range(args.zlim) if args.zlim else None
    except ValueError as exc:
        parser.error(str(exc))

    root = tk.Tk()
    app = App(
        root,
        args.port,
        args.baud,
        args.axis,
        xlim=xlim,
        ylim=ylim,
        zlim=zlim,
        major_tick_m=args.major_tick,
    )
    root.mainloop()


if __name__ == "__main__":
    main()
