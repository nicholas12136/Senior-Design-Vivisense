import sys
import time
import numpy as np
import serial
import serial.tools.list_ports

from PyQt5 import QtCore, QtWidgets
import pyqtgraph as pg


# ---------------------------------------
# Serial Worker (runs in a QThread)
# ---------------------------------------
class SerialWorker(QtCore.QObject):
    # We only need XY for 2D occupancy
    pointReceived = QtCore.pyqtSignal(float, float)  # x_mm, y_mm
    frameEnded = QtCore.pyqtSignal(int)              # timestamp
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
        self.status.emit(f"Connected to {port_name} @ {baud}")

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
        # Expected:
        # P,timestamp,sensorId,cellId,worldX,worldY,worldZ,targetStatus,numTargets
        # E,timestamp
        parts = line.split(",")
        if not parts:
            return

        if parts[0] == "P":
            if len(parts) < 6:
                return
            try:
                x = float(parts[4])
                y = float(parts[5])
                self.pointReceived.emit(x, y)
            except Exception:
                return

        elif parts[0] == "E":
            if len(parts) < 2:
                return
            try:
                ts = int(parts[1])
                self.frameEnded.emit(ts)
            except Exception:
                return


# ---------------------------------------
# 2D Occupancy Grid (dynamic bounds & cell size)
# ---------------------------------------
class OccupancyGrid2D:
    def __init__(self, xmin, xmax, ymin, ymax, cell_mm):
        self.configure(xmin, xmax, ymin, ymax, cell_mm)

    def configure(self, xmin, xmax, ymin, ymax, cell_mm):
        self.xmin = float(xmin)
        self.xmax = float(xmax)
        self.ymin = float(ymin)
        self.ymax = float(ymax)
        self.cell = float(cell_mm)

        # Ensure valid
        if self.xmax <= self.xmin:
            self.xmax = self.xmin + self.cell
        if self.ymax <= self.ymin:
            self.ymax = self.ymin + self.cell
        if self.cell <= 0:
            self.cell = 100.0

        self.nx = int(np.ceil((self.xmax - self.xmin) / self.cell))
        self.ny = int(np.ceil((self.ymax - self.ymin) / self.cell))

        # counts[x_index, y_index]
        self.counts = np.zeros((self.nx, self.ny), dtype=np.uint16)

    def clear(self):
        self.counts.fill(0)

    def add_points(self, pts_xy: np.ndarray):
        # pts_xy shape (N,2)
        if pts_xy.size == 0:
            return

        # Vectorized binning
        ix = np.floor((pts_xy[:, 0] - self.xmin) / self.cell).astype(np.int32)
        iy = np.floor((pts_xy[:, 1] - self.ymin) / self.cell).astype(np.int32)

        mask = (ix >= 0) & (ix < self.nx) & (iy >= 0) & (iy < self.ny)
        ix = ix[mask]
        iy = iy[mask]
        if ix.size == 0:
            return

        # Increment counts for all hits (handles duplicates)
        np.add.at(self.counts, (ix, iy), 1)

    def occupied01(self, min_count=1) -> np.ndarray:
        return (self.counts >= int(min_count)).astype(np.uint8)


# ---------------------------------------
# Main UI
# ---------------------------------------
class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("2D Occupancy Grid (Custom Bounds + Serial)")

        # Default bounds: 3m x 3m, 10cm cells
        self.grid = OccupancyGrid2D(
            xmin=0, xmax=3000,
            ymin=-1500, ymax=1500,
            cell_mm=100
        )

        # Frame buffer of points (x,y)
        self.frame_points = []

        # ---- UI ----
        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        root = QtWidgets.QHBoxLayout(central)

        # Left panel
        left = QtWidgets.QVBoxLayout()
        root.addLayout(left, 0)

        # Port row
        port_row = QtWidgets.QHBoxLayout()
        left.addLayout(port_row)

        self.port_combo = QtWidgets.QComboBox()
        port_row.addWidget(self.port_combo, 1)

        self.refresh_btn = QtWidgets.QPushButton("Refresh")
        self.refresh_btn.clicked.connect(self.refresh_ports)
        port_row.addWidget(self.refresh_btn)

        # Baud row + connect/disconnect
        baud_row = QtWidgets.QHBoxLayout()
        left.addLayout(baud_row)

        baud_row.addWidget(QtWidgets.QLabel("Baud:"))
        self.baud_combo = QtWidgets.QComboBox()
        self.baud_combo.addItems(["115200", "921600", "460800", "230400"])
        self.baud_combo.setCurrentText("115200")
        baud_row.addWidget(self.baud_combo, 1)

        self.connect_btn = QtWidgets.QPushButton("Connect")
        self.connect_btn.clicked.connect(self.connect_serial)
        baud_row.addWidget(self.connect_btn)

        self.disconnect_btn = QtWidgets.QPushButton("Disconnect")
        self.disconnect_btn.clicked.connect(self.disconnect_serial)
        self.disconnect_btn.setEnabled(False)
        baud_row.addWidget(self.disconnect_btn)

        left.addSpacing(10)

        # Grid parameters
        params_box = QtWidgets.QGroupBox("Grid parameters (mm)")
        left.addWidget(params_box)
        g = QtWidgets.QGridLayout(params_box)

        self.xmin_box = self._dspin(-50000, 50000, self.grid.xmin)
        self.xmax_box = self._dspin(-50000, 50000, self.grid.xmax)
        self.ymin_box = self._dspin(-50000, 50000, self.grid.ymin)
        self.ymax_box = self._dspin(-50000, 50000, self.grid.ymax)
        self.cell_box = self._dspin(10, 5000, self.grid.cell)
        self.cell_box.setSingleStep(10.0)

        g.addWidget(QtWidgets.QLabel("X min"), 0, 0); g.addWidget(self.xmin_box, 0, 1)
        g.addWidget(QtWidgets.QLabel("X max"), 0, 2); g.addWidget(self.xmax_box, 0, 3)
        g.addWidget(QtWidgets.QLabel("Y min"), 1, 0); g.addWidget(self.ymin_box, 1, 1)
        g.addWidget(QtWidgets.QLabel("Y max"), 1, 2); g.addWidget(self.ymax_box, 1, 3)
        g.addWidget(QtWidgets.QLabel("Cell size"), 2, 0); g.addWidget(self.cell_box, 2, 1)

        self.grid_info = QtWidgets.QLabel("")
        self.grid_info.setWordWrap(True)
        g.addWidget(self.grid_info, 3, 0, 1, 4)

        self.apply_btn = QtWidgets.QPushButton("Apply grid changes (clears grid)")
        self.apply_btn.clicked.connect(self.apply_grid_changes)
        left.addWidget(self.apply_btn)

        # Options
        opts = QtWidgets.QGroupBox("Options")
        left.addWidget(opts)
        v = QtWidgets.QVBoxLayout(opts)

        self.accumulate = QtWidgets.QCheckBox("Accumulate (persist occupied cells)")
        self.accumulate.setChecked(True)
        v.addWidget(self.accumulate)

        minrow = QtWidgets.QHBoxLayout()
        minrow.addWidget(QtWidgets.QLabel("Min count to show:"))
        self.min_count = QtWidgets.QSpinBox()
        self.min_count.setRange(1, 999999)
        self.min_count.setValue(1)
        self.min_count.valueChanged.connect(self.redraw)
        minrow.addWidget(self.min_count, 1)
        v.addLayout(minrow)

        self.clear_btn = QtWidgets.QPushButton("Clear grid")
        self.clear_btn.clicked.connect(self.clear_grid)
        v.addWidget(self.clear_btn)

        left.addStretch(1)

        self.status_label = QtWidgets.QLabel("Not connected")
        self.status_label.setWordWrap(True)
        left.addWidget(self.status_label)

        # Right: plot
        self.plot = pg.PlotWidget()
        self.plot.setAspectLocked(True)
        self.plot.showGrid(x=True, y=True, alpha=0.25)
        self.plot.setLabel("bottom", "X (mm) [Forward]")
        self.plot.setLabel("left", "Y (mm) [Left]")
        root.addWidget(self.plot, 1)

        # Heatmap image
        self.img = pg.ImageItem()
        self.plot.addItem(self.img)

        # Make 0 = white, 1 = red (binary occupancy)
        cmap = pg.ColorMap(
            pos=np.array([0.0, 1.0]),
            color=np.array([[255, 255, 255, 255], [255, 0, 0, 255]], dtype=np.ubyte)
        )
        self.img.setLookupTable(cmap.getLookupTable(0.0, 1.0, 256))
        self.img.setLevels([0, 1])

        # Grid-lines overlay (cell boundaries)
        self.grid_lines = pg.GraphItem()
        self.plot.addItem(self.grid_lines)

        # ---- Serial thread ----
        self.thread = QtCore.QThread()
        self.worker = SerialWorker()
        self.worker.moveToThread(self.thread)
        self.worker.pointReceived.connect(self.on_point_received)
        self.worker.frameEnded.connect(self.on_frame_ended)
        self.worker.status.connect(self.set_status)
        self.thread.start()

        self.refresh_ports()
        self._update_grid_info()
        self._rebuild_grid_overlay()
        self.redraw()

    def _dspin(self, lo, hi, val):
        sb = QtWidgets.QDoubleSpinBox()
        sb.setRange(lo, hi)
        sb.setDecimals(1)
        sb.setSingleStep(50.0)
        sb.setValue(float(val))
        return sb

    def closeEvent(self, event):
        self.disconnect_serial()
        self.thread.quit()
        self.thread.wait(1000)
        event.accept()

    # ---------------- Serial UI ----------------
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
            self.worker,
            "start",
            QtCore.Qt.QueuedConnection,
            QtCore.Q_ARG(str, port),
            QtCore.Q_ARG(int, baud),
        )

    def disconnect_serial(self):
        self.connect_btn.setEnabled(True)
        self.disconnect_btn.setEnabled(False)
        QtCore.QMetaObject.invokeMethod(self.worker, "stop", QtCore.Qt.QueuedConnection)

    def set_status(self, msg: str):
        self.status_label.setText(msg)

    # ---------------- Grid controls ----------------
    def apply_grid_changes(self):
        xmin = self.xmin_box.value()
        xmax = self.xmax_box.value()
        ymin = self.ymin_box.value()
        ymax = self.ymax_box.value()
        cell = self.cell_box.value()

        self.grid.configure(xmin, xmax, ymin, ymax, cell)
        self.frame_points.clear()
        self.grid.clear()

        self._update_grid_info()
        self._rebuild_grid_overlay()
        self.redraw()

        self.set_status(f"Applied grid: nx={self.grid.nx}, ny={self.grid.ny}, cell={self.grid.cell:.1f} mm (grid cleared)")

    def clear_grid(self):
        self.grid.clear()
        self.frame_points.clear()
        self.redraw()
        self.set_status("Grid cleared.")

    def _update_grid_info(self):
        self.grid_info.setText(
            f"Computed cells: NX={self.grid.nx}, NY={self.grid.ny} "
            f"(total {self.grid.nx * self.grid.ny})"
        )

    def _rebuild_grid_overlay(self):
        # Draw cell boundary lines without drawing a million items:
        # Use a single GraphItem with line segments.
        xmin, xmax = self.grid.xmin, self.grid.xmax
        ymin, ymax = self.grid.ymin, self.grid.ymax
        cell = self.grid.cell

        xs = np.arange(xmin, xmax + cell, cell, dtype=np.float32)
        ys = np.arange(ymin, ymax + cell, cell, dtype=np.float32)

        verts = []
        edges = []

        def add_seg(p0, p1):
            i0 = len(verts); verts.append(p0)
            i1 = len(verts); verts.append(p1)
            edges.append((i0, i1))

        # Vertical lines (constant x)
        for x in xs:
            add_seg((x, ymin), (x, ymax))

        # Horizontal lines (constant y)
        for y in ys:
            add_seg((xmin, y), (xmax, y))

        if len(verts) == 0:
            self.grid_lines.setData(pos=np.zeros((0, 2), dtype=np.float32), adj=np.zeros((0, 2), dtype=np.int32))
            return

        pos = np.array(verts, dtype=np.float32)
        adj = np.array(edges, dtype=np.int32)

        self.grid_lines.setData(
            pos=pos,
            adj=adj,
            pen=pg.mkPen((180, 180, 180, 120), width=1),
            symbol=None
        )

        # Keep viewbox aligned to grid rect
        self.plot.setXRange(xmin, xmax, padding=0.02)
        self.plot.setYRange(ymin, ymax, padding=0.02)

        # Map image pixels to world mm rectangle
        self.img.setRect(QtCore.QRectF(xmin, ymin, (xmax - xmin), (ymax - ymin)))

    # ---------------- Serial callbacks ----------------
    def on_point_received(self, x_mm: float, y_mm: float):
        self.frame_points.append((x_mm, y_mm))

    def on_frame_ended(self, timestamp: int):
        if not self.accumulate.isChecked():
            self.grid.clear()

        if self.frame_points:
            pts = np.array(self.frame_points, dtype=np.float32)
            self.grid.add_points(pts)

        self.frame_points.clear()
        self.redraw()

        occ = self.grid.occupied01(min_count=int(self.min_count.value()))
        occupied_cells = int(np.sum(occ))
        self.set_status(f"Frame {timestamp} -> occupied cells: {occupied_cells}")

    # ---------------- Drawing ----------------
    def redraw(self):
        occ01 = self.grid.occupied01(min_count=int(self.min_count.value()))

        # ImageItem expects array [rows, cols] ~ [y, x]
        img = occ01.T.astype(np.float32)

        self.img.setImage(img, autoLevels=False)


def main():
    app = QtWidgets.QApplication(sys.argv)
    w = MainWindow()
    w.resize(1300, 800)
    w.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
