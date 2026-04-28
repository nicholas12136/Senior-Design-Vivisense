import argparse
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import serial
import serial.tools.list_ports


def list_ports():
    return list(serial.tools.list_ports.comports())


class SerialReader:
    def __init__(self):
        self.rx_queue: queue.Queue[str] = queue.Queue(maxsize=10000)
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._serial: serial.Serial | None = None
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
        self.root.title("ViviSense LED Reason Debug Viewer")

        self.reader = SerialReader()

        self.mode = 2
        self.num_zones = 6
        self.active_mask = 0xFF
        self.ring_thresholds_mm = [300, 600, 1050, 1500, 1950]
        self.closest_mm = [-1] * 8
        self.polar_conf = np.zeros((6, 8), dtype=np.uint8)
        self.polar_occ = np.zeros((6, 8), dtype=np.uint8)
        self.last_update_s = 0.0

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
        ttk.Label(top, textvariable=self.conn_var).pack(side=tk.LEFT, padx=(12, 0))

        self.summary_var = tk.StringVar(value="Waiting for DL/DP/DO lines...")
        ttk.Label(self.root, textvariable=self.summary_var, padding=(8, 0)).pack(side=tk.TOP, anchor="w")

        body = ttk.Frame(self.root, padding=(8, 0, 8, 8))
        body.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        self.fig = plt.Figure(figsize=(10.0, 6.0), dpi=100)
        self.ax_conf = self.fig.add_subplot(121)
        self.ax_zone = self.fig.add_subplot(122)
        self.canvas = FigureCanvasTkAgg(self.fig, master=body)
        self.canvas.get_tk_widget().pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.reason_text = tk.Text(body, width=46, height=30)
        self.reason_text.pack(side=tk.LEFT, fill=tk.Y, padx=(8, 0))
        self.reason_text.configure(state=tk.DISABLED)

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
        self.reader.connect(port, baud)
        self.conn_var.set(f"Connecting {port}...")

    def disconnect(self):
        self.reader.disconnect()
        self.conn_var.set("Disconnected")

    @staticmethod
    def _mode_name(mode: int) -> str:
        return "polar"

    def _distance_to_ring_and_color(self, dist_mm: int) -> tuple[int, str]:
        r1, r2, r3, r4, r5 = self.ring_thresholds_mm
        if dist_mm < 0:
            return 0, "off"
        if dist_mm < r1:
            return 1, "red"
        if dist_mm < r2:
            return 2, "red"
        if dist_mm < r3:
            return 3, "orange"
        if dist_mm < r4:
            return 4, "orange"
        if dist_mm < r5:
            return 5, "yellow"
        return 6, "yellow"

    def _set_reason_text(self, text: str):
        self.reason_text.configure(state=tk.NORMAL)
        self.reason_text.delete("1.0", tk.END)
        self.reason_text.insert("1.0", text)
        self.reason_text.configure(state=tk.DISABLED)

    def _handle_line(self, line: str):
        if line.startswith("INFO,open,"):
            self.conn_var.set("Connected")
            return
        if line.startswith("INFO,closed"):
            self.conn_var.set("Disconnected")
            return
        if line.startswith("ERR,"):
            self.conn_var.set("Error")
            return

        if line.startswith("DL,"):
            parts = line.split(",")
            if len(parts) < 17:
                return
            try:
                self.mode = int(parts[1])
                self.num_zones = int(parts[2])
                self.active_mask = int(parts[3])
                self.ring_thresholds_mm = [int(parts[4 + i]) for i in range(5)]
                self.closest_mm = [int(parts[9 + i]) for i in range(8)]
                self.last_update_s = time.time()
            except ValueError:
                return
            return

        if line.startswith("DP,"):
            parts = line.split(",")
            if len(parts) < 2 + 48:
                return
            try:
                values = [int(v) for v in parts[2:2 + 48]]
            except ValueError:
                return
            self.polar_conf = np.array(values, dtype=np.uint8).reshape((6, 8))
            return

        if line.startswith("DO,"):
            parts = line.split(",")
            if len(parts) < 2 + 48:
                return
            try:
                values = [int(v) for v in parts[2:2 + 48]]
            except ValueError:
                return
            self.polar_occ = np.array(values, dtype=np.uint8).reshape((6, 8))
            return

    def _draw(self):
        self.ax_conf.clear()
        self.ax_zone.clear()

        self.ax_conf.imshow(self.polar_conf, origin="upper", aspect="auto", vmin=0, vmax=255, cmap="viridis")
        self.ax_conf.set_title("Polar Confidence (ring x zone)")
        self.ax_conf.set_xlabel("Zone")
        self.ax_conf.set_ylabel("Ring (1=closest)")
        self.ax_conf.set_xticks(range(8))
        self.ax_conf.set_yticks(range(6))
        self.ax_conf.set_yticklabels([str(i) for i in range(1, 7)])

        for ring in range(6):
            for zone in range(8):
                val = int(self.polar_conf[ring, zone])
                if val >= 120:
                    self.ax_conf.text(zone, ring, "X", color="white", ha="center", va="center", fontsize=7)

        zone_idxs = list(range(self.num_zones))
        zone_vals_m = []
        zone_colors = []
        zone_labels = []
        reasons: list[str] = []

        for z in zone_idxs:
            is_active = ((self.active_mask >> z) & 0x01) != 0
            dist_mm = self.closest_mm[z]
            ring, color_name = self._distance_to_ring_and_color(dist_mm)
            if dist_mm < 0:
                zone_vals_m.append(0.0)
                zone_colors.append("#d1d5db")
                zone_labels.append("none")
                reasons.append(f"Zone {z}: no obstacle")
            else:
                zone_vals_m.append(dist_mm / 1000.0)
                zone_colors.append({"red": "#ef4444", "orange": "#f59e0b", "yellow": "#eab308"}.get(color_name, "#9ca3af"))
                zone_labels.append(f"R{ring}")
                conf = int(self.polar_conf[max(ring - 1, 0), z]) if ring > 0 else 0
                occ = int(self.polar_occ[max(ring - 1, 0), z]) if ring > 0 else 0
                reasons.append(
                    f"Zone {z}: {dist_mm/1000.0:.2f} m -> ring {ring} ({color_name}), active={is_active}, conf={conf}, occ={occ}"
                )

        self.ax_zone.bar(zone_idxs, zone_vals_m, color=zone_colors)
        self.ax_zone.set_title("Closest Distance By Zone")
        self.ax_zone.set_xlabel("Zone")
        self.ax_zone.set_ylabel("Distance (m)")
        self.ax_zone.set_xticks(zone_idxs)
        ymax = max(zone_vals_m) if zone_vals_m else 1.0
        self.ax_zone.set_ylim(0.0, max(0.5, ymax * 1.25))
        for idx, lbl in zip(zone_idxs, zone_labels):
            self.ax_zone.text(idx, zone_vals_m[idx] + 0.02, lbl, ha="center", va="bottom", fontsize=8)

        mode_name = self._mode_name(self.mode)
        age_ms = (time.time() - self.last_update_s) * 1000.0 if self.last_update_s > 0 else -1.0
        self.summary_var.set(
            f"Mode={mode_name} ({self.mode}) zones={self.num_zones} activeMask=0x{self.active_mask:02X} "
            f"thresholds={self.ring_thresholds_mm} ageMs={age_ms:.0f}"
        )
        self._set_reason_text("\n".join(reasons) if reasons else "No zone data yet")

        self.fig.tight_layout()
        self.canvas.draw_idle()

    def _drain_serial(self):
        n = 0
        while n < 5000:
            try:
                line = self.reader.rx_queue.get_nowait()
            except queue.Empty:
                break
            self._handle_line(line)
            n += 1

    def _tick(self):
        self._drain_serial()
        self._draw()
        self.root.after(100, self._tick)

    def _on_close(self):
        self.reader.disconnect()
        self.root.destroy()


def default_port():
    ports = list_ports()
    return ports[0].device if ports else None


def main():
    parser = argparse.ArgumentParser(description="ViviSense live LED reason viewer (DL/DP/DO serial debug)")
    parser.add_argument("--port", default=default_port(), help="Default serial port (e.g. COM5)")
    parser.add_argument("--baud", type=int, default=115200, help="Default serial baud")
    args = parser.parse_args()

    root = tk.Tk()
    App(root, args.port, args.baud)
    root.mainloop()


if __name__ == "__main__":
    main()
