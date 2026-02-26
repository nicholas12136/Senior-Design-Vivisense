import argparse
import csv
import time
import tkinter as tk
from tkinter import filedialog, ttk

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg


def parse_axis_range(text: str):
    parts = [p.strip() for p in text.split(",")]
    if len(parts) != 2:
        raise ValueError("axis range must be min,max")
    lo = float(parts[0])
    hi = float(parts[1])
    if lo >= hi:
        raise ValueError("axis range must have min < max")
    return (lo, hi)


class OccupancyPlaybackApp:
    def __init__(self, root: tk.Tk, csv_path: str | None, xlim, ylim, cell_size_m: float):
        self.root = root
        self.root.title("ViviSense CSV Occupancy Grid Playback")

        self.xlim = xlim
        self.ylim = ylim
        self.cell_size_m = cell_size_m

        self.events = []  # {"sid","ts_ms","local_time_s","points":[(x_m,y_m,z_m,target_status,cell,valid)]}
        self.latest_points_by_sensor = {sid: [] for sid in range(1, 9)}
        self.current_idx = -1
        self.playing = False
        self.play_started_monotonic = 0.0
        self.play_started_event_time = 0.0

        self._build_ui()
        if csv_path:
            self.load_csv(csv_path)
        self.root.after(40, self._tick)
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

        body = ttk.Frame(self.root, padding=(8, 0, 8, 8))
        body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        left = ttk.Frame(body)
        left.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 10))

        self.status_var = tk.StringVar(value="No recording loaded")
        ttk.Label(left, textvariable=self.status_var, width=42, wraplength=320).pack(anchor="w")

        ttk.Label(left, text="Sensors").pack(anchor="w", pady=(10, 0))
        self.sensor_enabled_vars = {}
        for sid in range(1, 9):
            var = tk.BooleanVar(value=True)
            self.sensor_enabled_vars[sid] = var
            ttk.Checkbutton(left, text=f"Sensor {sid}", variable=var, command=self._draw).pack(anchor="w")

        ttk.Label(left, text="Target Status Filter").pack(anchor="w", pady=(10, 0))
        self.status_filter_var = tk.StringVar(value="")
        ttk.Entry(left, textvariable=self.status_filter_var, width=20).pack(anchor="w")
        ttk.Label(left, text="Blank = all, e.g. 5 or 5,9,13", width=34).pack(anchor="w")

        self.show_invalid_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(left, text="Show invalid points", variable=self.show_invalid_var, command=self._draw).pack(
            anchor="w", pady=(6, 0)
        )

        ttk.Label(left, text="Grid Settings").pack(anchor="w", pady=(10, 0))
        self.xlim_var = tk.StringVar(value=f"{self.xlim[0]},{self.xlim[1]}")
        self.ylim_var = tk.StringVar(value=f"{self.ylim[0]},{self.ylim[1]}")
        self.cell_size_var = tk.StringVar(value=f"{self.cell_size_m:.3f}")
        ttk.Label(left, text="X min,max (m)").pack(anchor="w")
        ttk.Entry(left, textvariable=self.xlim_var, width=20).pack(anchor="w")
        ttk.Label(left, text="Y min,max (m)").pack(anchor="w")
        ttk.Entry(left, textvariable=self.ylim_var, width=20).pack(anchor="w")
        ttk.Label(left, text="Cell size (m)").pack(anchor="w")
        ttk.Entry(left, textvariable=self.cell_size_var, width=20).pack(anchor="w")
        ttk.Button(left, text="Apply Grid", command=self.apply_grid_settings).pack(anchor="w", pady=(4, 0))

        self.fig = plt.Figure(figsize=(8.5, 6.5), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.canvas = FigureCanvasTkAgg(self.fig, master=body)
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

    def _event_time_s(self, idx: int) -> float:
        if idx < 0 or idx >= len(self.events):
            return 0.0
        return float(self.events[idx]["local_time_s"])

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

    def _filtered_points(self):
        allowed_status = self._parse_status_filter()
        show_invalid = self.show_invalid_var.get()
        rows = []
        for sid in range(1, 9):
            if not self.sensor_enabled_vars[sid].get():
                continue
            for p in self.latest_points_by_sensor[sid]:
                x_m, y_m, z_m, target_status, cell, valid = p
                if (not show_invalid) and (not valid):
                    continue
                if allowed_status is not None and target_status not in allowed_status:
                    continue
                rows.append((sid, x_m, y_m, z_m, target_status, cell, valid))
        return rows

    def _update_status(self):
        if self.current_idx < 0 or not self.events:
            self.status_var.set("No frame selected")
            return
        ev = self.events[self.current_idx]
        total_rows = sum(len(self.latest_points_by_sensor[s]) for s in self.latest_points_by_sensor)
        shown_rows = len(self._filtered_points())
        self.status_var.set(
            f"Event {self.current_idx + 1}/{len(self.events)}  sid={ev['sid']}  ts_ms={ev['ts_ms']}  "
            f"local_time={ev['local_time_s']:.3f}s  rows={shown_rows}/{total_rows}"
        )

    def _draw(self):
        self.ax.clear()

        x_min, x_max = self.xlim
        y_min, y_max = self.ylim
        cell = max(1e-4, self.cell_size_m)
        nx = max(1, int(np.ceil((x_max - x_min) / cell)))
        ny = max(1, int(np.ceil((y_max - y_min) / cell)))
        grid = np.zeros((ny, nx), dtype=np.uint16)

        rows = self._filtered_points()
        for _sid, x_m, y_m, _z_m, _status, _cell_id, _valid in rows:
            if x_m < x_min or x_m >= x_max or y_m < y_min or y_m >= y_max:
                continue
            ix = int((x_m - x_min) / cell)
            iy = int((y_m - y_min) / cell)
            if 0 <= ix < nx and 0 <= iy < ny:
                grid[iy, ix] = min(grid[iy, ix] + 1, 65535)

        self.ax.imshow(
            grid,
            origin="lower",
            extent=[x_min, x_max, y_min, y_max],
            interpolation="nearest",
            aspect="equal",
            cmap="viridis",
        )
        self.ax.set_xlabel("X (m)")
        self.ax.set_ylabel("Y (m)")
        self.ax.set_title(f"Occupancy Grid  cells={nx}x{ny}  points={len(rows)}")
        self.canvas.draw_idle()

    def apply_grid_settings(self):
        try:
            self.xlim = parse_axis_range(self.xlim_var.get().strip())
            self.ylim = parse_axis_range(self.ylim_var.get().strip())
            self.cell_size_m = float(self.cell_size_var.get().strip())
            if self.cell_size_m <= 0:
                raise ValueError
        except ValueError:
            return
        self._update_status()
        self._draw()

    def toggle_play(self):
        if not self.events:
            return
        if self.playing:
            self.playing = False
            self.play_btn.configure(text="Play")
            return

        if self.current_idx >= len(self.events) - 1:
            self._set_index(0)
        elif self.current_idx < 0:
            self._set_index(0)

        self.playing = True
        self.play_btn.configure(text="Pause")
        if self.playing:
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

        self.root.after(40, self._tick)

    def _on_close(self):
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description="Playback ViviSense point CSV as a 2D occupancy grid")
    parser.add_argument("csv_file", nargs="?", default=None, help="Recorded CSV file to open")
    parser.add_argument("--xlim", type=str, default="-2.5,2.5", help="X axis range in meters as min,max")
    parser.add_argument("--ylim", type=str, default="-2.5,2.5", help="Y axis range in meters as min,max")
    parser.add_argument("--cell", type=float, default=0.05, help="Occupancy cell size in meters")
    args = parser.parse_args()

    try:
        xlim = parse_axis_range(args.xlim)
        ylim = parse_axis_range(args.ylim)
        if args.cell <= 0:
            raise ValueError("cell size must be > 0")
    except ValueError as exc:
        parser.error(str(exc))

    root = tk.Tk()
    OccupancyPlaybackApp(root, args.csv_file, xlim=xlim, ylim=ylim, cell_size_m=args.cell)
    root.mainloop()


if __name__ == "__main__":
    main()
