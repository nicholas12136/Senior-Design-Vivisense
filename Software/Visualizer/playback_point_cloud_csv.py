import argparse
import csv
import time
import tkinter as tk
from tkinter import filedialog, ttk

import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401
from matplotlib.ticker import MultipleLocator


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
INCH_TO_M = 0.0254
WHEELCHAIR_LENGTH_M = 25.0 * INCH_TO_M
WHEELCHAIR_WIDTH_M = 27.5 * INCH_TO_M
WHEELCHAIR_HEIGHT_M = 37.0 * INCH_TO_M
DEFAULT_VIEW_ELEV = 22
DEFAULT_VIEW_AZIM = 180
TOP_VIEW_ELEV = 90
TOP_VIEW_AZIM = -90
BEHIND_VIEW_ELEV = 18
BEHIND_VIEW_AZIM = 180


def draw_wheelchair_box(ax):
    # World-frame origin is the wheelchair front-center-bottom.
    x0, x1 = -WHEELCHAIR_LENGTH_M, 0.0
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
        ax.plot(
            [pts[p0][0], pts[p1][0]],
            [pts[p0][1], pts[p1][1]],
            [pts[p0][2], pts[p1][2]],
            color="#444444",
            linewidth=1.5,
        )


def parse_axis_range(text: str):
    parts = [p.strip() for p in text.split(",")]
    if len(parts) != 2:
        raise ValueError("axis range must be min,max")
    lo = float(parts[0])
    hi = float(parts[1])
    if lo >= hi:
        raise ValueError("axis range must have min < max")
    return (lo, hi)


class PlaybackApp:
    def __init__(
        self,
        root: tk.Tk,
        csv_path: str | None,
        axis_limit_m: float,
        xlim=None,
        ylim=None,
        zlim=None,
        major_tick_m: float = 0.5,
    ):
        self.root = root
        self.root.title("ViviSense CSV Point Cloud Playback")

        self.axis_limit_m = axis_limit_m
        self.xlim = xlim if xlim is not None else (-axis_limit_m, axis_limit_m)
        self.ylim = ylim if ylim is not None else (-axis_limit_m, axis_limit_m)
        self.zlim = zlim if zlim is not None else (0.0, axis_limit_m * 2.0)
        self.major_tick_m = major_tick_m
        self.events = []  # each: {"sid","ts_ms","local_time_s","points":[(x_m,y_m,z_m,target_status,cell,valid)]}
        self.latest_points_by_sensor = {sid: [] for sid in range(1, 9)}
        self.current_idx = -1
        self.playing = False
        self.play_started_monotonic = 0.0
        self.play_started_event_time = 0.0
        self._view_initialized = False

        self._build_ui()
        if csv_path:
            self.load_csv(csv_path)
        self.root.after(80, self._tick)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self):
        top = ttk.Frame(self.root, padding=8)
        top.pack(side=tk.TOP, fill=tk.X)

        ttk.Button(top, text="Open CSV", command=self.open_csv_dialog).pack(side=tk.LEFT)
        self.play_btn = ttk.Button(top, text="Play", command=self.toggle_play)
        self.play_btn.pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(top, text="Prev", command=self.step_prev).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(top, text="Next", command=self.step_next).pack(side=tk.LEFT, padx=(4, 0))

        ttk.Label(top, text="Speed").pack(side=tk.LEFT, padx=(12, 0))
        self.speed_var = tk.StringVar(value="1.0")
        ttk.Entry(top, textvariable=self.speed_var, width=6).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Label(top, text="x").pack(side=tk.LEFT)

        self.file_var = tk.StringVar(value="No file loaded")
        ttk.Label(top, textvariable=self.file_var).pack(side=tk.LEFT, padx=(12, 0))

        mid = ttk.Frame(self.root, padding=(8, 0, 8, 8))
        mid.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        left = ttk.Frame(mid)
        left.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 10))

        self.status_var = tk.StringVar(value="No recording loaded")
        ttk.Label(left, textvariable=self.status_var, width=42, wraplength=300).pack(anchor="w")

        ttk.Label(left, text="Sensors").pack(anchor="w", pady=(10, 0))
        self.sensor_enabled_vars = {}
        for sid in range(1, 9):
            var = tk.BooleanVar(value=True)
            self.sensor_enabled_vars[sid] = var
            ttk.Checkbutton(left, text=f"Sensor {sid}", variable=var).pack(anchor="w")

        ttk.Label(left, text="Target Status Filter").pack(anchor="w", pady=(10, 0))
        self.status_filter_var = tk.StringVar(value="")
        ttk.Entry(left, textvariable=self.status_filter_var, width=20).pack(anchor="w")
        ttk.Label(left, text="Blank = all, e.g. 5 or 5,9,13", width=34).pack(anchor="w")
        self.show_invalid_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(left, text="Show invalid points", variable=self.show_invalid_var).pack(anchor="w", pady=(6, 0))
        self.show_wheelchair_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(left, text="Show wheelchair box", variable=self.show_wheelchair_var).pack(anchor="w")

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

        self.fig = plt.Figure(figsize=(9, 6), dpi=100)
        self.ax = self.fig.add_subplot(111, projection="3d")
        self.canvas = FigureCanvasTkAgg(self.fig, master=mid)
        self.canvas.get_tk_widget().pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.slider_var = tk.IntVar(value=0)
        self.slider = tk.Scale(
            self.root,
            from_=0,
            to=0,
            orient=tk.HORIZONTAL,
            variable=self.slider_var,
            command=self._on_slider,
            label="Frame Event",
        )
        self.slider.pack(side=tk.TOP, fill=tk.X, padx=8, pady=(0, 8))

    def open_csv_dialog(self):
        path = filedialog.askopenfilename(
            title="Open point CSV",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
        )
        if path:
            self.load_csv(path)

    def load_csv(self, path: str):
        self.playing = False
        self.play_btn.configure(text="Play")
        self.play_started_monotonic = 0.0
        self.play_started_event_time = 0.0
        self.events = self._parse_csv_to_events(path)
        self.file_var.set(path)
        self.slider.configure(to=max(0, len(self.events) - 1))
        self.current_idx = -1
        self.latest_points_by_sensor = {sid: [] for sid in range(1, 9)}
        if self.events:
            self._set_index(0)
        else:
            self.status_var.set("No events found in CSV")
            self._draw()

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
                    x_m = float(row["x_mm"]) / 1000.0
                    y_m = float(row["y_mm"]) / 1000.0
                    z_m = float(row["z_mm"]) / 1000.0
                    target_status = int(row["target_status"])
                    ts_ms = int(row["sensor_ts_ms"])
                    local_time_s = float(row["local_time_s"])
                    valid = int(row.get("valid", "1"))
                except Exception:
                    continue

                key = (sid, ts_ms)
                if key != current_key:
                    current_event = {
                        "sid": sid,
                        "ts_ms": ts_ms,
                        "local_time_s": local_time_s,
                        "points": [],
                    }
                    events.append(current_event)
                    current_key = key

                current_event["points"].append((x_m, y_m, z_m, target_status, cell, valid))

        return events

    def _rebuild_state_to_index(self, idx: int):
        self.latest_points_by_sensor = {sid: [] for sid in range(1, 9)}
        if idx < 0:
            self.current_idx = -1
            return
        for i in range(0, idx + 1):
            ev = self.events[i]
            self.latest_points_by_sensor[ev["sid"]] = ev["points"]
        self.current_idx = idx

    def _set_index(self, idx: int):
        if not self.events:
            return
        idx = max(0, min(idx, len(self.events) - 1))
        if idx == self.current_idx + 1 and self.current_idx >= 0:
            ev = self.events[idx]
            self.latest_points_by_sensor[ev["sid"]] = ev["points"]
            self.current_idx = idx
        else:
            self._rebuild_state_to_index(idx)
        self.slider_var.set(idx)
        self._update_status()
        self._draw()

    def _event_time_s(self, idx: int) -> float:
        if idx < 0 or idx >= len(self.events):
            return 0.0
        return float(self.events[idx]["local_time_s"])

    def _update_status(self):
        if self.current_idx < 0 or not self.events:
            self.status_var.set("No frame selected")
            return
        ev = self.events[self.current_idx]
        total_rows = sum(len(self.latest_points_by_sensor[s]) for s in self.latest_points_by_sensor)
        self.status_var.set(
            f"Event {self.current_idx + 1}/{len(self.events)}  sid={ev['sid']}  ts_ms={ev['ts_ms']}  "
            f"local_time={ev['local_time_s']:.3f}s  total_rows={total_rows}"
        )

    def _draw(self):
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
            self.ax.xaxis.set_minor_locator(MultipleLocator(1e9))
            self.ax.yaxis.set_minor_locator(MultipleLocator(1e9))
            self.ax.zaxis.set_minor_locator(MultipleLocator(1e9))
            self.ax.tick_params(axis="z", which="minor", length=0, width=0)
            self.ax.tick_params(axis="x", which="major", length=7, width=1.4, colors="#1f1f1f")
            self.ax.tick_params(axis="y", which="major", length=7, width=1.4, colors="#1f1f1f")
            self.ax.tick_params(axis="z", which="major", length=7, width=1.4, colors="#1f1f1f")
            self.ax.tick_params(axis="x", which="minor", length=0, width=0)
            self.ax.tick_params(axis="y", which="minor", length=0, width=0)
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
        show_invalid = self.show_invalid_var.get()
        total_pts = 0
        for sid in range(1, 9):
            if not self.sensor_enabled_vars[sid].get():
                continue
            pts = self.latest_points_by_sensor[sid]
            if not pts:
                continue
            if allowed_status is None:
                draw_pts = pts
            else:
                draw_pts = [p for p in pts if p[3] in allowed_status]
            if not show_invalid:
                draw_pts = [p for p in draw_pts if p[5]]
            if not draw_pts:
                continue
            xs = [p[0] for p in draw_pts]
            ys = [p[1] for p in draw_pts]
            zs = [p[2] for p in draw_pts]
            self.ax.scatter(xs, ys, zs, s=18, color=COLORS.get(sid, "#333333"), label=f"S{sid}")
            total_pts += len(draw_pts)
        if total_pts:
            self.ax.legend(loc="upper left", fontsize=8)
        self.ax.set_title(f"Playback  points={total_pts}")
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
        self._draw()

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

    def toggle_play(self):
        if not self.events:
            return
        self.playing = not self.playing
        self.play_btn.configure(text="Pause" if self.playing else "Play")
        if self.playing:
            if self.current_idx < 0:
                self._set_index(0)
            self.play_started_monotonic = time.monotonic()
            self.play_started_event_time = self._event_time_s(self.current_idx)

    def step_next(self):
        if not self.events:
            return
        self.playing = False
        self.play_btn.configure(text="Play")
        self.play_started_monotonic = 0.0
        self._set_index(0 if self.current_idx < 0 else min(self.current_idx + 1, len(self.events) - 1))

    def step_prev(self):
        if not self.events:
            return
        self.playing = False
        self.play_btn.configure(text="Play")
        self.play_started_monotonic = 0.0
        self._set_index(max(0, self.current_idx - 1))

    def _on_slider(self, _value):
        if not self.events:
            return
        idx = self.slider_var.get()
        if idx != self.current_idx:
            self.playing = False
            self.play_btn.configure(text="Play")
            self.play_started_monotonic = 0.0
            self._set_index(idx)

    def _tick(self):
        if self.playing and self.events:
            try:
                speed = float(self.speed_var.get().strip())
            except ValueError:
                speed = 1.0
            if speed <= 0:
                speed = 1.0

            now_mono = time.monotonic()
            if self.play_started_monotonic == 0.0:
                self.play_started_monotonic = now_mono
                self.play_started_event_time = self._event_time_s(max(self.current_idx, 0))

            elapsed_real = now_mono - self.play_started_monotonic
            target_event_time = self.play_started_event_time + (elapsed_real * speed)

            advanced = False
            while self.current_idx < len(self.events) - 1 and self._event_time_s(self.current_idx + 1) <= target_event_time:
                self._set_index(self.current_idx + 1)
                advanced = True

            if self.current_idx >= len(self.events) - 1:
                self.playing = False
                self.play_btn.configure(text="Play")
                self.play_started_monotonic = 0.0
            self.root.after(20 if advanced else 40, self._tick)
            return

        self.root.after(80, self._tick)

    def _on_close(self):
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description="Playback ViviSense point CSV in a 3D point cloud viewer")
    parser.add_argument("csv_file", nargs="?", default=None, help="Recorded CSV file to open")
    parser.add_argument("--axis", type=float, default=3.5, help="Symmetric XY axis limit in meters; Z defaults to 0..2*axis")
    parser.add_argument("--xlim", type=str, default=None, help="X axis range in meters as min,max (e.g. -2.5,2.5)")
    parser.add_argument("--ylim", type=str, default=None, help="Y axis range in meters as min,max")
    parser.add_argument("--zlim", type=str, default=None, help="Z axis range in meters as min,max")
    parser.add_argument("--major-tick", type=float, default=0.5, help="Major XY tick spacing in meters (<=0 disables)")
    args = parser.parse_args()

    try:
        xlim = parse_axis_range(args.xlim) if args.xlim else None
        ylim = parse_axis_range(args.ylim) if args.ylim else None
        zlim = parse_axis_range(args.zlim) if args.zlim else None
    except ValueError as exc:
        parser.error(str(exc))

    root = tk.Tk()
    PlaybackApp(
        root,
        args.csv_file,
        args.axis,
        xlim=xlim,
        ylim=ylim,
        zlim=zlim,
        major_tick_m=args.major_tick,
    )
    root.mainloop()


if __name__ == "__main__":
    main()
