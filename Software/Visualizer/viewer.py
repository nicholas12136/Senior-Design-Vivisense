import sys
import time
import csv
import threading
import numpy as np
import serial
import serial.tools.list_ports

from PyQt5 import QtCore, QtWidgets

from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.backends.backend_qt5agg import NavigationToolbar2QT as NavigationToolbar
from matplotlib.figure import Figure


# -----------------------------
# Serial worker (runs in a QThread)
# -----------------------------
class SerialWorker(QtCore.QObject):
    pointReceived = QtCore.pyqtSignal(dict)   # point row dict
    frameEnded = QtCore.pyqtSignal(int)       # timestamp
    status = QtCore.pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self._running = False
        self._ser = None

    @QtCore.pyqtSlot(str, int)
    def start(self, port_name: str, baud: int):
        if self._running:
            return

        try:
            self._ser = serial.Serial(port_name, baudrate=baud, timeout=0.05)
            self._ser.reset_input_buffer()
        except Exception as e:
            self.status.emit(f"Failed to open {port_name}: {e}")
            return

        self._running = True
        self.status.emit(f"Connected: {port_name} @ {baud}")

        buf = bytearray()

        while self._running:
            try:
                chunk = self._ser.read(4096)
                if chunk:
                    buf.extend(chunk)
                    while b"\n" in buf:
                        line, _, buf = buf.partition(b"\n")
                        line = line.strip()
                        if not line:
                            continue
                        self._handle_line(line.decode(errors="ignore"))
                else:
                    time.sleep(0.001)
            except Exception as e:
                self.status.emit(f"Serial error: {e}")
                break

        try:
            if self._ser and self._ser.is_open:
                self._ser.close()
        except Exception:
            pass

        self._ser = None
        self._running = False
        self.status.emit("Disconnected")

    @QtCore.pyqtSlot()
    def stop(self):
        self._running = False

    def _handle_line(self, line: str):
        # P,timestamp,sensorId,cellId,x,y,z,targetStatus,numTargets
        # E,timestamp
        parts = line.split(",")
        if not parts:
            return

        if parts[0] == "P":
            if len(parts) < 7:
                return
            try:
                row = {
                    "timestamp_ms": int(parts[1]),
                    "sensor_id": int(parts[2]),
                    "cell_id": int(parts[3]),
                    "x_mm": float(parts[4]),
                    "y_mm": float(parts[5]),
                    "z_mm": float(parts[6]),
                    "target_status": int(parts[7]) if len(parts) > 7 else -1,
                    "num_targets": int(parts[8]) if len(parts) > 8 else -1,
                    "raw_line": line,
                }
                self.pointReceived.emit(row)
            except Exception:
                return

        elif parts[0] == "E":
            if len(parts) < 2:
                return
            try:
                self.frameEnded.emit(int(parts[1]))
            except Exception:
                return


# -----------------------------
# Matplotlib 3D widget + Toolbar + Mouse-wheel zoom
# -----------------------------
class Mpl3DWidget(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)

        self.fig = Figure()
        self.canvas = FigureCanvas(self.fig)
        self.canvas.setFocusPolicy(QtCore.Qt.StrongFocus)
        self.canvas.setFocus()

        self.toolbar = NavigationToolbar(self.canvas, self)

        self.ax = self.fig.add_subplot(111, projection="3d")
        self.fig.subplots_adjust(left=0.02, right=0.98, bottom=0.02, top=0.98)

        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.toolbar)
        layout.addWidget(self.canvas)

        self._zoom_get_limits = None
        self._zoom_set_limits = None
        self._zoom_factor = 0.9
        self._mpl_cid_scroll = None

    def enable_mouse_wheel_zoom(self, get_limits_func, set_limits_func, zoom_factor=0.9):
        """
        Adds mouse wheel zoom to an embedded Matplotlib 3D axes.

        - get_limits_func() must return (xmin,xmax,ymin,ymax,zmin,zmax)
        - set_limits_func(limits_tuple) sets those limits in the main app
        """
        self._zoom_get_limits = get_limits_func
        self._zoom_set_limits = set_limits_func
        self._zoom_factor = float(zoom_factor)

        # Ensure canvas receives events
        self.canvas.setFocusPolicy(QtCore.Qt.StrongFocus)
        self.canvas.setFocus()

        if self._mpl_cid_scroll is not None:
            try:
                self.canvas.mpl_disconnect(self._mpl_cid_scroll)
            except Exception:
                pass

        self._mpl_cid_scroll = self.canvas.mpl_connect("scroll_event", self._on_scroll)

    def _on_scroll(self, event):
        if self._zoom_get_limits is None or self._zoom_set_limits is None:
            return
        if event.button not in ("up", "down"):
            return

        xmin, xmax, ymin, ymax, zmin, zmax = self._zoom_get_limits()

        cx = 0.5 * (xmin + xmax)
        cy = 0.5 * (ymin + ymax)
        cz = 0.5 * (zmin + zmax)

        hx = 0.5 * (xmax - xmin)
        hy = 0.5 * (ymax - ymin)
        hz = 0.5 * (zmax - zmin)

        if event.button == "up":
            scale = self._zoom_factor   # zoom in
        else:
            scale = 1.0 / self._zoom_factor  # zoom out

        hx *= scale
        hy *= scale
        hz *= scale

        # prevent collapsing
        min_half = 1.0
        hx = max(hx, min_half)
        hy = max(hy, min_half)
        hz = max(hz, min_half)

        new_limits = (cx - hx, cx + hx, cy - hy, cy + hy, cz - hz, cz + hz)
        self._zoom_set_limits(new_limits)

    def set_tick_labels_visible(self, visible: bool):
        if visible:
            self.ax.tick_params(labelsize=8)
        else:
            self.ax.set_xticklabels([])
            self.ax.set_yticklabels([])
            self.ax.set_zticklabels([])

    def set_ticks_from_spacing(self, xmin, xmax, ymin, ymax, zmin, zmax, spacing):
        def make_ticks(a, b, step):
            if step <= 0:
                return []
            lo = min(a, b)
            hi = max(a, b)
            start = np.floor(lo / step) * step
            end = np.ceil(hi / step) * step
            return np.arange(start, end + step, step)

        self.ax.set_xticks(make_ticks(xmin, xmax, spacing))
        self.ax.set_yticks(make_ticks(ymin, ymax, spacing))
        self.ax.set_zticks(make_ticks(zmin, zmax, spacing))

    def draw_scene(
        self,
        points_by_sensor,
        colors_by_sensor,
        pt_size,
        limits,
        tick_spacing,
        tick_labels,
        show_limits,
    ):
        xmin, xmax, ymin, ymax, zmin, zmax = limits

        self.ax.cla()

        self.ax.set_xlabel("X (mm) [Forward]")
        self.ax.set_ylabel("Y (mm) [Left]")
        self.ax.set_zlabel("Z (mm) [Up]")
        self.ax.grid(True)

        # fixed limits (no autoscale)
        self.ax.set_xlim(xmin, xmax)
        self.ax.set_ylim(ymin, ymax)
        self.ax.set_zlim(zmin, zmax)

        # ticks + labels
        self.set_ticks_from_spacing(xmin, xmax, ymin, ymax, zmin, zmax, tick_spacing)
        self.set_tick_labels_visible(tick_labels)

        # origin + triad
        self.ax.scatter([0], [0], [0], s=90, marker="x")
        triad_len = max(200.0, tick_spacing * 0.8)
        self.ax.plot([0, triad_len], [0, 0], [0, 0])      # +X
        self.ax.plot([0, 0], [0, triad_len], [0, 0])      # +Y
        self.ax.plot([0, 0], [0, 0], [0, triad_len])      # +Z

        if show_limits:
            self._draw_limits_box(xmin, xmax, ymin, ymax, zmin, zmax)

        for sid, pts in points_by_sensor.items():
            if not pts:
                continue
            arr = np.asarray(pts, dtype=float)
            c = colors_by_sensor.get(sid, None)
            self.ax.scatter(
                arr[:, 0], arr[:, 1], arr[:, 2],
                s=float(pt_size),
                label=f"Sensor {sid}",
                c=[c] if c else None
            )

        if points_by_sensor:
            self.ax.legend(loc="upper right")

        self.canvas.draw_idle()

    def _draw_limits_box(self, xmin, xmax, ymin, ymax, zmin, zmax):
        corners = np.array([
            [xmin, ymin, zmin],
            [xmax, ymin, zmin],
            [xmax, ymax, zmin],
            [xmin, ymax, zmin],
            [xmin, ymin, zmax],
            [xmax, ymin, zmax],
            [xmax, ymax, zmax],
            [xmin, ymax, zmax],
        ], dtype=float)

        edges = [
            (0, 1), (1, 2), (2, 3), (3, 0),
            (4, 5), (5, 6), (6, 7), (7, 4),
            (0, 4), (1, 5), (2, 6), (3, 7),
        ]
        for i, j in edges:
            p0, p1 = corners[i], corners[j]
            self.ax.plot([p0[0], p1[0]], [p0[1], p1[1]], [p0[2], p1[2]])


# -----------------------------
# Main Window
# -----------------------------
class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Vivisense Live 3D Point Cloud (World Frame)")

        # Change the former WHITE sensor color to something more visible (lime green here)
        # (Adjust mapping as needed)
        self.sensor_colors = {
            5: (1.0, 0.0, 0.0),   # red
            6: (0.0, 1.0, 1.0),   # cyan
            7: (0.2, 1.0, 0.2),   # <-- was white, now bright green
            8: (0.6, 0.3, 1.0),   # purple
        }

        self._lock = threading.Lock()
        self.frame_points = {}      # sid -> list[(x,y,z,meta)]
        self.last_frame_points = {} # sid -> list[(x,y,z)]
        self.last_frame_ts = 0
        self.last_total = 0
        self.last_shown = 0

        # Recording
        self.recording = False
        self.record_end_time = 0.0
        self.csv_file = None
        self.csv_writer = None

        # Default fixed limits (no autoscaling)
        self.current_limits = (-3000.0, 3000.0, -3000.0, 3000.0, 0.0, 6000.0)

        # UI
        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        root = QtWidgets.QVBoxLayout(central)

        # Row 1: serial
        r1 = QtWidgets.QHBoxLayout()
        root.addLayout(r1)

        r1.addWidget(QtWidgets.QLabel("Port:"))
        self.port_combo = QtWidgets.QComboBox()
        r1.addWidget(self.port_combo, 1)

        self.refresh_btn = QtWidgets.QPushButton("Refresh")
        self.refresh_btn.clicked.connect(self.refresh_ports)
        r1.addWidget(self.refresh_btn)

        r1.addWidget(QtWidgets.QLabel("Baud:"))
        self.baud_combo = QtWidgets.QComboBox()
        self.baud_combo.addItems(["115200", "921600", "460800", "230400"])
        self.baud_combo.setCurrentText("115200")
        r1.addWidget(self.baud_combo)

        self.connect_btn = QtWidgets.QPushButton("Connect")
        self.connect_btn.clicked.connect(self.connect_serial)
        r1.addWidget(self.connect_btn)

        self.disconnect_btn = QtWidgets.QPushButton("Disconnect")
        self.disconnect_btn.clicked.connect(self.disconnect_serial)
        self.disconnect_btn.setEnabled(False)
        r1.addWidget(self.disconnect_btn)

        # Row 2: filters + pt size
        r2 = QtWidgets.QHBoxLayout()
        root.addLayout(r2)

        r2.addWidget(QtWidgets.QLabel("MinTargets:"))
        self.min_targets = QtWidgets.QSpinBox()
        self.min_targets.setRange(0, 99)
        self.min_targets.setValue(0)
        r2.addWidget(self.min_targets)

        self.status_allow = QtWidgets.QLineEdit()
        self.status_allow.setPlaceholderText("targetStatus allowlist (comma list), e.g. 0,5,9  (empty=all)")
        r2.addWidget(self.status_allow, 1)

        self.clip_cb = QtWidgets.QCheckBox("Clip")
        self.clip_cb.setChecked(True)
        r2.addWidget(self.clip_cb)

        self.show_limits_cb = QtWidgets.QCheckBox("Show limits")
        self.show_limits_cb.setChecked(False)
        r2.addWidget(self.show_limits_cb)

        r2.addStretch(1)

        r2.addWidget(QtWidgets.QLabel("PtSize:"))
        self.pt_size = QtWidgets.QSpinBox()
        self.pt_size.setRange(1, 200)
        self.pt_size.setValue(20)
        r2.addWidget(self.pt_size)

        self.freeze_cb = QtWidgets.QCheckBox("Freeze")
        self.freeze_cb.setChecked(False)
        r2.addWidget(self.freeze_cb)

        # Row 3: ticks/grid + limits presets
        r3 = QtWidgets.QHBoxLayout()
        root.addLayout(r3)

        r3.addStretch(1)
        r3.addWidget(QtWidgets.QLabel("Grid spacing (mm):"))
        self.grid_spacing = QtWidgets.QSpinBox()
        self.grid_spacing.setRange(10, 5000)
        self.grid_spacing.setValue(500)
        r3.addWidget(self.grid_spacing)

        r3.addWidget(QtWidgets.QLabel("Grid size preset (mm):"))
        self.grid_size = QtWidgets.QSpinBox()
        self.grid_size.setRange(500, 30000)
        self.grid_size.setValue(6000)
        r3.addWidget(self.grid_size)

        self.tick_labels_cb = QtWidgets.QCheckBox("Tick labels")
        self.tick_labels_cb.setChecked(True)
        r3.addWidget(self.tick_labels_cb)

        self.apply_grid_btn = QtWidgets.QPushButton("Apply preset limits")
        self.apply_grid_btn.clicked.connect(self.apply_grid_limits)
        r3.addWidget(self.apply_grid_btn)

        # Row 4: manual axis limits
        r4 = QtWidgets.QHBoxLayout()
        root.addLayout(r4)

        r4.addWidget(QtWidgets.QLabel("Xmin"))
        self.xmin = self._dspin(-50000, 50000, self.current_limits[0]); r4.addWidget(self.xmin)
        r4.addWidget(QtWidgets.QLabel("Xmax"))
        self.xmax = self._dspin(-50000, 50000, self.current_limits[1]); r4.addWidget(self.xmax)

        r4.addWidget(QtWidgets.QLabel("Ymin"))
        self.ymin = self._dspin(-50000, 50000, self.current_limits[2]); r4.addWidget(self.ymin)
        r4.addWidget(QtWidgets.QLabel("Ymax"))
        self.ymax = self._dspin(-50000, 50000, self.current_limits[3]); r4.addWidget(self.ymax)

        r4.addWidget(QtWidgets.QLabel("Zmin"))
        self.zmin = self._dspin(-50000, 50000, self.current_limits[4]); r4.addWidget(self.zmin)
        r4.addWidget(QtWidgets.QLabel("Zmax"))
        self.zmax = self._dspin(-50000, 50000, self.current_limits[5]); r4.addWidget(self.zmax)

        self.apply_limits_btn = QtWidgets.QPushButton("Apply manual limits")
        self.apply_limits_btn.clicked.connect(self.apply_manual_limits)
        r4.addWidget(self.apply_limits_btn)

        # Row 5: CSV recording
        r5 = QtWidgets.QHBoxLayout()
        root.addLayout(r5)

        r5.addWidget(QtWidgets.QLabel("Record (s):"))
        self.rec_seconds = QtWidgets.QSpinBox()
        self.rec_seconds.setRange(1, 3600)
        self.rec_seconds.setValue(10)
        r5.addWidget(self.rec_seconds)

        self.rec_btn = QtWidgets.QPushButton("Start CSV")
        self.rec_btn.clicked.connect(self.start_recording)
        r5.addWidget(self.rec_btn)

        self.stop_rec_btn = QtWidgets.QPushButton("Stop CSV")
        self.stop_rec_btn.clicked.connect(self.stop_recording)
        self.stop_rec_btn.setEnabled(False)
        r5.addWidget(self.stop_rec_btn)

        r5.addStretch(1)

        # Status
        self.status_label = QtWidgets.QLabel("Not connected")
        root.addWidget(self.status_label)

        # Viewer
        self.viewer = Mpl3DWidget()
        root.addWidget(self.viewer, 1)

        # Enable mouse-wheel zoom
        self.viewer.enable_mouse_wheel_zoom(
            get_limits_func=lambda: self.current_limits,
            set_limits_func=self._set_limits_from_zoom,
            zoom_factor=0.9
        )

        # Redraw triggers
        self.pt_size.valueChanged.connect(self.redraw_last_frame)
        self.grid_spacing.valueChanged.connect(self.redraw_last_frame)
        self.tick_labels_cb.stateChanged.connect(self.redraw_last_frame)
        self.show_limits_cb.stateChanged.connect(self.redraw_last_frame)

        # Serial thread
        self.thread = QtCore.QThread()
        self.worker = SerialWorker()
        self.worker.moveToThread(self.thread)
        self.worker.pointReceived.connect(self.on_point_received)
        self.worker.frameEnded.connect(self.on_frame_ended)
        self.worker.status.connect(self.set_status)
        self.thread.start()

        # Recording timeout timer
        self.rec_timer = QtCore.QTimer(self)
        self.rec_timer.timeout.connect(self._check_recording_timeout)
        self.rec_timer.start(100)

        self.refresh_ports()
        self.redraw_last_frame()

    def _dspin(self, lo, hi, val):
        sb = QtWidgets.QDoubleSpinBox()
        sb.setRange(lo, hi)
        sb.setDecimals(1)
        sb.setSingleStep(50.0)
        sb.setValue(float(val))
        return sb

    def set_status(self, msg: str):
        self.status_label.setText(msg)

    def refresh_ports(self):
        self.port_combo.clear()
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            self.port_combo.addItem(f"{p.device} — {p.description}", p.device)
        if not ports:
            self.port_combo.addItem("No ports found", "")

    def connect_serial(self):
        port = self.port_combo.currentData()
        if not port:
            self.set_status("No valid serial port selected.")
            return
        baud = int(self.baud_combo.currentText())

        self.connect_btn.setEnabled(False)
        self.disconnect_btn.setEnabled(True)

        QtCore.QMetaObject.invokeMethod(
            self.worker, "start",
            QtCore.Qt.QueuedConnection,
            QtCore.Q_ARG(str, port),
            QtCore.Q_ARG(int, baud),
        )

    def disconnect_serial(self):
        self.connect_btn.setEnabled(True)
        self.disconnect_btn.setEnabled(False)
        QtCore.QMetaObject.invokeMethod(self.worker, "stop", QtCore.Qt.QueuedConnection)

    # -------- wheel zoom limits sync ----------
    def _set_limits_from_zoom(self, limits):
        self.current_limits = tuple(float(v) for v in limits)

        # sync UI boxes
        self.xmin.setValue(self.current_limits[0])
        self.xmax.setValue(self.current_limits[1])
        self.ymin.setValue(self.current_limits[2])
        self.ymax.setValue(self.current_limits[3])
        self.zmin.setValue(self.current_limits[4])
        self.zmax.setValue(self.current_limits[5])

        self.redraw_last_frame()

    # -------- limits ----------
    def apply_grid_limits(self):
        size = float(self.grid_size.value())
        xmin, xmax = -size / 2.0, size / 2.0
        ymin, ymax = -size / 2.0, size / 2.0
        zmin, zmax = 0.0, size

        self.xmin.setValue(xmin); self.xmax.setValue(xmax)
        self.ymin.setValue(ymin); self.ymax.setValue(ymax)
        self.zmin.setValue(zmin); self.zmax.setValue(zmax)

        self.apply_manual_limits()

    def apply_manual_limits(self):
        self.current_limits = (
            float(self.xmin.value()), float(self.xmax.value()),
            float(self.ymin.value()), float(self.ymax.value()),
            float(self.zmin.value()), float(self.zmax.value()),
        )
        self.redraw_last_frame()

    # -------- filtering ----------
    def _parse_allowlist(self):
        text = self.status_allow.text().strip()
        if not text:
            return None
        vals = set()
        for part in text.split(","):
            part = part.strip()
            if not part:
                continue
            try:
                vals.add(int(part))
            except Exception:
                pass
        return vals if vals else None

    def _clip_point(self, x, y, z, limits):
        xmin, xmax, ymin, ymax, zmin, zmax = limits
        return (xmin <= x <= xmax) and (ymin <= y <= ymax) and (zmin <= z <= zmax)

    # -------- serial callbacks ----------
    def on_point_received(self, row: dict):
        self._record_row_if_needed(row)

        sid = row["sensor_id"]
        x, y, z = row["x_mm"], row["y_mm"], row["z_mm"]
        with self._lock:
            self.frame_points.setdefault(sid, []).append((x, y, z, row))

    def on_frame_ended(self, ts: int):
        if self.freeze_cb.isChecked():
            return

        allow = self._parse_allowlist()
        min_t = int(self.min_targets.value())
        limits = self.current_limits
        clip = self.clip_cb.isChecked()

        points_by_sensor = {}
        total = 0
        shown = 0

        with self._lock:
            snapshot = {sid: pts[:] for sid, pts in self.frame_points.items()}
            self.frame_points.clear()

        for sid, items in snapshot.items():
            out = []
            for (x, y, z, meta) in items:
                total += 1
                if min_t > 0 and meta.get("num_targets", -1) < min_t:
                    continue
                if allow is not None and meta.get("target_status", -999) not in allow:
                    continue
                if clip and not self._clip_point(x, y, z, limits):
                    continue
                out.append((x, y, z))
                shown += 1
            if out:
                points_by_sensor[sid] = out

        self.last_frame_points = points_by_sensor
        self.last_frame_ts = ts
        self.last_total = total
        self.last_shown = shown

        self.redraw_last_frame()
        self.set_status(f"Frame {ts} | points={total} shown={shown} | spacing={self.grid_spacing.value()}mm")

    def redraw_last_frame(self):
        self.viewer.draw_scene(
            points_by_sensor=self.last_frame_points,
            colors_by_sensor=self.sensor_colors,
            pt_size=float(self.pt_size.value()),
            limits=self.current_limits,
            tick_spacing=float(self.grid_spacing.value()),
            tick_labels=self.tick_labels_cb.isChecked(),
            show_limits=self.show_limits_cb.isChecked(),
        )

    # -------- CSV recording ----------
    def start_recording(self):
        if self.recording:
            return

        seconds = int(self.rec_seconds.value())
        default_name = time.strftime("pointcloud_%Y%m%d_%H%M%S.csv")
        path, _ = QtWidgets.QFileDialog.getSaveFileName(self, "Save CSV", default_name, "CSV Files (*.csv)")
        if not path:
            return

        try:
            f = open(path, "w", newline="", encoding="utf-8")
            w = csv.DictWriter(f, fieldnames=[
                "timestamp_ms", "sensor_id", "cell_id",
                "x_mm", "y_mm", "z_mm",
                "target_status", "num_targets", "raw_line"
            ])
            w.writeheader()
        except Exception as e:
            self.set_status(f"Could not open CSV: {e}")
            return

        self.csv_file = f
        self.csv_writer = w
        self.recording = True
        self.record_end_time = time.time() + seconds

        self.rec_btn.setEnabled(False)
        self.stop_rec_btn.setEnabled(True)
        self.set_status(f"Recording CSV for {seconds} seconds...")

    def stop_recording(self):
        if not self.recording:
            return

        self.recording = False
        self.record_end_time = 0.0

        try:
            if self.csv_file:
                self.csv_file.flush()
                self.csv_file.close()
        except Exception:
            pass

        self.csv_file = None
        self.csv_writer = None

        self.rec_btn.setEnabled(True)
        self.stop_rec_btn.setEnabled(False)
        self.set_status("Recording stopped.")

    def _check_recording_timeout(self):
        if self.recording and time.time() >= self.record_end_time:
            self.stop_recording()

    def _record_row_if_needed(self, row: dict):
        if not self.recording:
            return
        if time.time() > self.record_end_time:
            return
        try:
            if self.csv_writer:
                self.csv_writer.writerow({
                    "timestamp_ms": row.get("timestamp_ms", ""),
                    "sensor_id": row.get("sensor_id", ""),
                    "cell_id": row.get("cell_id", ""),
                    "x_mm": row.get("x_mm", ""),
                    "y_mm": row.get("y_mm", ""),
                    "z_mm": row.get("z_mm", ""),
                    "target_status": row.get("target_status", ""),
                    "num_targets": row.get("num_targets", ""),
                    "raw_line": row.get("raw_line", ""),
                })
        except Exception:
            pass

    def closeEvent(self, event):
        self.stop_recording()
        self.disconnect_serial()
        self.thread.quit()
        self.thread.wait(1000)
        event.accept()


def main():
    app = QtWidgets.QApplication(sys.argv)
    w = MainWindow()
    w.resize(1600, 950)
    w.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
