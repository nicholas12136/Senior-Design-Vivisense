import argparse
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk

import serial
import serial.tools.list_ports


def list_ports():
    return list(serial.tools.list_ports.comports())


class SerialReader:
    def __init__(self):
        self.rx_queue = queue.Queue(maxsize=5000)
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
                self.rx_queue.put_nowait(line)
            except Exception:
                pass

    def _worker(self, port: str, baud: int):
        try:
            self._serial = serial.Serial(port, baud, timeout=0.1)
            self.connected = True
            self._put(f"INFO,open,{port}@{baud}")
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
            try:
                line = raw.decode("utf-8", errors="ignore").strip()
            except Exception:
                continue
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
        self.root.title("ViviSense Latency Monitor")
        self.reader = SerialReader()
        self.last_update_s = 0.0
        self.lines_seen = 0

        self.sensor_rows = {}
        for sid in range(1, 9):
            self.sensor_rows[sid] = {
                "pkts": 0,
                "rx_hz": 0.0,
                "conv_last": 0,
                "conv_avg": 0.0,
                "wire_last": 0,
                "wire_avg": 0.0,
                "sync_rtt": 0,
                "sync_offset": 0,
                "sync_ok": 0,
                "last_seen_s": 0.0,
            }

        self._build_ui(default_port, baud)
        self.refresh_ports()
        self.root.after(60, self._tick)
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
        ttk.Entry(top, textvariable=self.baud_var, width=8).pack(side=tk.LEFT, padx=(4, 8))

        ttk.Button(top, text="Connect", command=self.connect).pack(side=tk.LEFT)
        ttk.Button(top, text="Disconnect", command=self.disconnect).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Button(top, text="Clear Log", command=self.clear_log).pack(side=tk.LEFT, padx=(12, 0))

        self.conn_var = tk.StringVar(value="Disconnected")
        ttk.Label(top, textvariable=self.conn_var).pack(side=tk.LEFT, padx=(12, 0))
        self.rate_var = tk.StringVar(value="S lines/s: 0.0")
        ttk.Label(top, textvariable=self.rate_var).pack(side=tk.RIGHT)

        body = ttk.Panedwindow(self.root, orient=tk.VERTICAL)
        body.pack(fill=tk.BOTH, expand=True, padx=8, pady=(0, 8))

        table_frame = ttk.Frame(body)
        body.add(table_frame, weight=3)

        columns = (
            "sid",
            "pkts",
            "rx_hz",
            "conv_last",
            "conv_avg",
            "wire_last",
            "wire_avg",
            "sync_rtt",
            "sync_offset",
            "sync_ok",
            "age",
        )
        self.tree = ttk.Treeview(table_frame, columns=columns, show="headings", height=10)
        headings = {
            "sid": "SID",
            "pkts": "Pkts",
            "rx_hz": "Rx Hz",
            "conv_last": "Conv us last",
            "conv_avg": "Conv us avg",
            "wire_last": "Wireless us last",
            "wire_avg": "Wireless us avg",
            "sync_rtt": "Sync RTT us",
            "sync_offset": "Sync offset us",
            "sync_ok": "Sync OK",
            "age": "Age s",
        }
        widths = {
            "sid": 45,
            "pkts": 75,
            "rx_hz": 75,
            "conv_last": 95,
            "conv_avg": 95,
            "wire_last": 115,
            "wire_avg": 115,
            "sync_rtt": 95,
            "sync_offset": 105,
            "sync_ok": 70,
            "age": 70,
        }
        for c in columns:
            self.tree.heading(c, text=headings[c])
            self.tree.column(c, width=widths[c], anchor=tk.CENTER, stretch=False)

        vsb = ttk.Scrollbar(table_frame, orient=tk.VERTICAL, command=self.tree.yview)
        hsb = ttk.Scrollbar(table_frame, orient=tk.HORIZONTAL, command=self.tree.xview)
        self.tree.configure(yscrollcommand=vsb.set, xscrollcommand=hsb.set)
        self.tree.grid(row=0, column=0, sticky="nsew")
        vsb.grid(row=0, column=1, sticky="ns")
        hsb.grid(row=1, column=0, sticky="ew")
        table_frame.rowconfigure(0, weight=1)
        table_frame.columnconfigure(0, weight=1)

        for sid in range(1, 9):
            self.tree.insert("", tk.END, iid=f"s{sid}", values=self._row_values(sid))

        log_frame = ttk.Frame(body)
        body.add(log_frame, weight=2)
        ttk.Label(log_frame, text="Raw status lines (S,...)").pack(anchor="w")
        self.log = tk.Text(log_frame, height=10, wrap="none")
        self.log.pack(fill=tk.BOTH, expand=True)

    def refresh_ports(self):
        ports = list_ports()
        names = [p.device for p in ports]
        self.port_combo["values"] = names
        if self.port_var.get() not in names and names:
            self.port_var.set(names[0])

    def connect(self):
        try:
            baud = int(self.baud_var.get().strip())
        except ValueError:
            self.conn_var.set("Invalid baud")
            return
        port = self.port_var.get().strip()
        if not port:
            self.conn_var.set("Select COM port")
            return
        self.reader.connect(port, baud)

    def disconnect(self):
        self.reader.disconnect()

    def clear_log(self):
        self.log.delete("1.0", tk.END)

    def _append_log(self, line: str):
        self.log.insert(tk.END, line + "\n")
        if int(self.log.index("end-1c").split(".")[0]) > 500:
            self.log.delete("1.0", "100.0")
        self.log.see(tk.END)

    def _row_values(self, sid: int):
        row = self.sensor_rows[sid]
        age = 0.0
        if row["last_seen_s"] > 0:
            age = time.time() - row["last_seen_s"]
        return (
            sid,
            row["pkts"],
            f"{row['rx_hz']:.2f}",
            row["conv_last"],
            f"{row['conv_avg']:.1f}",
            row["wire_last"],
            f"{row['wire_avg']:.1f}",
            row["sync_rtt"],
            row["sync_offset"],
            row["sync_ok"],
            f"{age:.1f}",
        )

    def _handle_status_line(self, line: str):
        parts = line.split(",")
        if len(parts) < 4 or parts[0] != "S":
            return
        try:
            sid = int(parts[1])
            pkts = int(parts[2])
            hz = float(parts[3])
        except ValueError:
            return
        if sid not in self.sensor_rows:
            return
        row = self.sensor_rows[sid]
        row["pkts"] = pkts
        row["rx_hz"] = hz
        # Optional conversion timing fields.
        if len(parts) >= 6:
            try:
                row["conv_last"] = int(parts[4])
                row["conv_avg"] = float(parts[5])
            except ValueError:
                pass
        # Optional sync/wireless fields.
        if len(parts) >= 11:
            try:
                row["wire_last"] = int(parts[6])
                row["wire_avg"] = float(parts[7])
                row["sync_rtt"] = int(parts[8])
                row["sync_offset"] = int(parts[9])
                row["sync_ok"] = int(parts[10])
            except ValueError:
                pass
        row["last_seen_s"] = time.time()
        self.tree.item(f"s{sid}", values=self._row_values(sid))

    def _drain_serial(self):
        processed = 0
        s_lines = 0
        while processed < 5000:
            try:
                line = self.reader.rx_queue.get_nowait()
            except queue.Empty:
                break
            processed += 1
            if line.startswith("INFO,open,"):
                self.conn_var.set("Connected")
            elif line.startswith("INFO,closed"):
                self.conn_var.set("Disconnected")
            elif line.startswith("ERR,"):
                self.conn_var.set("Error")
                self._append_log(line)
            elif line.startswith("S,"):
                self._handle_status_line(line)
                self._append_log(line)
                s_lines += 1

        now = time.time()
        dt = now - self.last_update_s
        if dt >= 0.5:
            rate = (self.lines_seen + s_lines) / dt if dt > 0 else 0.0
            self.rate_var.set(f"S lines/s: {rate:.1f}")
            self.last_update_s = now
            self.lines_seen = 0
        else:
            self.lines_seen += s_lines

        # Update age column even when no new data arrives.
        for sid in range(1, 9):
            self.tree.item(f"s{sid}", values=self._row_values(sid))

    def _tick(self):
        self._drain_serial()
        self.root.after(80, self._tick)

    def _on_close(self):
        self.reader.disconnect()
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description="Live monitor for receiver latency status lines (S,...).")
    parser.add_argument("--port", default=None, help="COM port, e.g. COM7")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate (default: 921600)")
    args = parser.parse_args()

    root = tk.Tk()
    app = App(root, args.port, args.baud)
    root.geometry("1300x700")
    root.mainloop()


if __name__ == "__main__":
    main()
