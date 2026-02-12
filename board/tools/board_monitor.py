#!/usr/bin/env python3
"""Bloco Board Monitor — View connected I2C blocks and send programs to robot."""

import json
import os
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports

# Paths
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BOARD_PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
IDF_PATH = os.environ.get(
    "IDF_PATH",
    os.path.expanduser("~/.espressif/v5.5.2/esp-idf"),
)

# Block type names (mirrors block_types.h)
BLOCK_NAMES = {
    0x01: ("BEGIN", "Actions", "#4CAF50"),
    0x02: ("END", "Actions", "#f44336"),
    0x10: ("FORWARD", "Movement", "#2196F3"),
    0x11: ("BACKWARD", "Movement", "#2196F3"),
    0x12: ("TURN_RIGHT", "Movement", "#2196F3"),
    0x13: ("TURN_LEFT", "Movement", "#2196F3"),
    0x14: ("SHAKE", "Movement", "#2196F3"),
    0x15: ("SPIN", "Movement", "#2196F3"),
    0x20: ("REPEAT", "Control Flow", "#FF9800"),
    0x21: ("END_REPEAT", "Control Flow", "#FF9800"),
    0x22: ("IF", "Control Flow", "#FF9800"),
    0x23: ("END_IF", "Control Flow", "#FF9800"),
    0x30: ("BEEP", "Sound", "#9C27B0"),
    0x31: ("SING", "Sound", "#9C27B0"),
    0x32: ("PLAY_TRIANGLE", "Sound", "#9C27B0"),
    0x33: ("PLAY_CIRCLE", "Sound", "#9C27B0"),
    0x34: ("PLAY_SQUARE", "Sound", "#9C27B0"),
    0x40: ("WHITE_LIGHT_ON", "Light", "#FFEB3B"),
    0x41: ("RED_LIGHT_ON", "Light", "#f44336"),
    0x42: ("BLUE_LIGHT_ON", "Light", "#2196F3"),
    0x50: ("WAIT_FOR_CLAP", "Wait", "#795548"),
    0x60: ("PARAM_2", "Parameters", "#607D8B"),
    0x61: ("PARAM_3", "Parameters", "#607D8B"),
    0x62: ("PARAM_4", "Parameters", "#607D8B"),
    0x63: ("PARAM_FOREVER", "Parameters", "#607D8B"),
    0x64: ("PARAM_LIGHT", "Parameters", "#607D8B"),
    0x65: ("PARAM_DARK", "Parameters", "#607D8B"),
    0x66: ("PARAM_NEAR", "Parameters", "#607D8B"),
    0x67: ("PARAM_FAR", "Parameters", "#607D8B"),
    0x68: ("PARAM_UNTIL_LIGHT", "Parameters", "#607D8B"),
    0x69: ("PARAM_UNTIL_DARK", "Parameters", "#607D8B"),
    0x6A: ("PARAM_UNTIL_NEAR", "Parameters", "#607D8B"),
    0x6B: ("PARAM_UNTIL_FAR", "Parameters", "#607D8B"),
    0x70: ("SENSOR_LIGHT_BULB", "Sensors", "#00BCD4"),
    0x71: ("SENSOR_EAR", "Sensors", "#00BCD4"),
    0x72: ("SENSOR_EYE", "Sensors", "#00BCD4"),
    0x73: ("SENSOR_TELESCOPE", "Sensors", "#00BCD4"),
    0x74: ("SENSOR_SOUND_MODULE", "Sensors", "#00BCD4"),
}


def detect_port():
    for p in serial.tools.list_ports.comports():
        if "ACM" in p.device or "USB" in p.device:
            return p.device
    ports = [p.device for p in serial.tools.list_ports.comports()]
    return ports[0] if ports else None


class SerialConnection:
    def __init__(self):
        self.ser = None
        self.lock = threading.Lock()

    def connect(self, port, baud=115200):
        self.ser = serial.Serial(port, baud, timeout=1)
        time.sleep(0.3)
        self.ser.reset_input_buffer()

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.ser = None

    @property
    def connected(self):
        return self.ser is not None and self.ser.is_open

    def send_command(self, cmd_dict):
        """Send a JSON command and collect all JSON response lines until done."""
        if not self.connected:
            return None
        with self.lock:
            try:
                line = json.dumps(cmd_dict) + "\n"
                self.ser.reset_input_buffer()
                self.ser.write(line.encode())
                self.ser.flush()

                responses = []
                for _ in range(50):  # read up to 50 lines
                    raw = self.ser.readline().decode(errors="replace").strip()
                    if not raw:
                        continue
                    if raw.startswith("{"):
                        try:
                            obj = json.loads(raw)
                            responses.append(obj)
                            # Check for terminal responses
                            resp = obj.get("response", "")
                            if resp in ("SCAN_END", "SEND_OK", "STATUS"):
                                break
                        except json.JSONDecodeError:
                            continue
                return responses
            except Exception as e:
                print(f"Serial error: {e}", file=sys.stderr)
                return None

    @staticmethod
    def list_ports():
        return [p.device for p in serial.tools.list_ports.comports()]


class FlashTab:
    """Tab that builds and flashes the board firmware."""

    def __init__(self, parent, notebook, on_done):
        self.frame = ttk.Frame(parent)
        self.notebook = notebook
        self.on_done = on_done
        self.process = None

        port_frame = ttk.Frame(self.frame)
        port_frame.pack(fill="x", padx=8, pady=(8, 0))

        ttk.Label(port_frame, text="Flash port:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(port_frame, textvariable=self.port_var, width=18, state="readonly")
        self.port_combo.pack(side="left", padx=4)
        self._refresh_ports()

        ttk.Button(port_frame, text="Refresh", command=self._refresh_ports).pack(side="left", padx=2)
        self.flash_btn = ttk.Button(port_frame, text="Flash", command=self.start_flash)
        self.flash_btn.pack(side="left", padx=4)

        self.status_label = ttk.Label(port_frame, text="Select port and click Flash", foreground="blue")
        self.status_label.pack(side="right", padx=8)

        text_frame = ttk.Frame(self.frame)
        text_frame.pack(fill="both", expand=True, padx=8, pady=8)

        self.text = tk.Text(text_frame, wrap="word", font=("monospace", 9),
                            bg="#1e1e1e", fg="#d4d4d4", insertbackground="#d4d4d4",
                            state="disabled")
        scrollbar = ttk.Scrollbar(text_frame, command=self.text.yview)
        self.text.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="right", fill="y")
        self.text.pack(side="left", fill="both", expand=True)

        self.text.tag_configure("error", foreground="#f44747")
        self.text.tag_configure("success", foreground="#6a9955")
        self.text.tag_configure("info", foreground="#569cd6")

    def _refresh_ports(self):
        ports = SerialConnection.list_ports()
        self.port_combo["values"] = ports
        detected = detect_port()
        if detected and not self.port_var.get():
            self.port_var.set(detected)
        elif ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def _append_text(self, text, tag=None):
        self.text.configure(state="normal")
        if tag:
            self.text.insert("end", text, tag)
        else:
            self.text.insert("end", text)
        self.text.see("end")
        self.text.configure(state="disabled")

    def start_flash(self):
        port = self.port_var.get()
        if not port:
            messagebox.showerror("Error", "No serial port selected.")
            return

        self.flash_btn.configure(state="disabled")
        self.port_combo.configure(state="disabled")
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        self.text.configure(state="disabled")

        self.status_label.configure(text="Building & flashing...", foreground="blue")
        self._append_text(f">>> Building and flashing board firmware to {port}...\n\n", "info")

        def run_flash():
            export_script = os.path.join(IDF_PATH, "export.sh")
            cmd = f'source "{export_script}" && cd "{BOARD_PROJECT_DIR}" && idf.py -p {port} build flash'
            try:
                proc = subprocess.Popen(
                    ["bash", "-c", cmd],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, bufsize=1,
                )
                self.process = proc
                for line in proc.stdout:
                    self.frame.after(0, self._append_text, line)
                proc.wait()
                self.frame.after(0, self._on_flash_done, proc.returncode)
            except Exception as e:
                self.frame.after(0, self._append_text, f"\nError: {e}\n", "error")
                self.frame.after(0, self._on_flash_done, 1)

        threading.Thread(target=run_flash, daemon=True).start()

    def _on_flash_done(self, returncode):
        self.process = None
        self.flash_btn.configure(state="normal")
        self.port_combo.configure(state="readonly")

        if returncode == 0:
            self._append_text("\n>>> Flash complete!\n", "success")
            self.status_label.configure(text="Flash OK", foreground="green")
            self.on_done(self.port_var.get())
        else:
            self._append_text(f"\n>>> Flash failed (exit code {returncode})\n", "error")
            self.status_label.configure(text="Flash FAILED", foreground="red")


class SlotsTab:
    """Tab showing connected I2C block slots with visual cards."""

    def __init__(self, parent):
        self.frame = ttk.Frame(parent)
        self.conn = SerialConnection()
        self.channels = {}  # ch -> block data dict

        # Connection bar
        conn_frame = ttk.Frame(self.frame)
        conn_frame.pack(fill="x", padx=8, pady=(8, 0))

        ttk.Label(conn_frame, text="Port:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(conn_frame, textvariable=self.port_var, width=18, state="readonly")
        self.port_combo.pack(side="left", padx=4)
        self._refresh_ports()

        self.connect_btn = ttk.Button(conn_frame, text="Connect", command=self._toggle_connect)
        self.connect_btn.pack(side="left", padx=4)
        ttk.Button(conn_frame, text="Refresh Ports", command=self._refresh_ports).pack(side="left")

        self.conn_label = ttk.Label(conn_frame, text="  Disconnected", foreground="red")
        self.conn_label.pack(side="right")

        # Slot cards area
        slots_frame = ttk.LabelFrame(self.frame, text="I2C Block Slots")
        slots_frame.pack(fill="both", expand=True, padx=8, pady=8)

        self.cards_canvas = tk.Canvas(slots_frame, bg="#2d2d2d", highlightthickness=0)
        self.cards_canvas.pack(fill="both", expand=True, padx=4, pady=4)

        # Details panel
        detail_frame = ttk.LabelFrame(self.frame, text="Block Details")
        detail_frame.pack(fill="x", padx=8, pady=(0, 4))

        self.detail_labels = {}
        fields = [
            ("Channel", "channel"), ("Type", "type_str"), ("Category", "category"),
            ("Name", "name"), ("Serial", "serial"),
            ("Checksum", "checksum"), ("Params", "params"),
        ]
        detail_grid = ttk.Frame(detail_frame)
        detail_grid.pack(fill="x", padx=8, pady=4)
        for i, (label, key) in enumerate(fields):
            col = (i % 4) * 2
            row = i // 4
            ttk.Label(detail_grid, text=f"{label}:", font=("", 9, "bold")).grid(
                row=row, column=col, sticky="w", padx=(8, 2), pady=2)
            val = ttk.Label(detail_grid, text="---", font=("", 9))
            val.grid(row=row, column=col + 1, sticky="w", padx=(0, 12), pady=2)
            self.detail_labels[key] = val

        # Action buttons
        action_frame = ttk.Frame(self.frame)
        action_frame.pack(fill="x", padx=8, pady=(0, 8))

        self.scan_btn = ttk.Button(action_frame, text="Scan All Slots", command=self._scan_all)
        self.scan_btn.pack(side="left", padx=4)

        self.send_btn = ttk.Button(action_frame, text="Send to Robot (ESP-NOW)", command=self._send_to_robot)
        self.send_btn.pack(side="left", padx=4)

        # Status
        self.status_var = tk.StringVar(value="Connect to board to scan I2C blocks")
        ttk.Label(self.frame, textvariable=self.status_var, relief="sunken", anchor="w").pack(
            fill="x", side="bottom", padx=8, pady=(0, 8))

        # Auto-refresh timer
        self.auto_scan_active = False
        self.selected_channel = None

        # Draw empty slots
        self.frame.after(100, self._draw_slots)

    def auto_connect(self, port):
        self.port_var.set(port)
        self.frame.after(1500, self._do_connect)

    def _refresh_ports(self):
        ports = SerialConnection.list_ports()
        self.port_combo["values"] = ports
        if ports and not self.port_var.get():
            detected = detect_port()
            if detected:
                self.port_var.set(detected)
            else:
                self.port_var.set(ports[0])

    def _toggle_connect(self):
        if self.conn.connected:
            self.auto_scan_active = False
            self.conn.disconnect()
            self.connect_btn.config(text="Connect")
            self.conn_label.config(text="  Disconnected", foreground="red")
            self.status_var.set("Disconnected")
        else:
            self._do_connect()

    def _do_connect(self):
        port = self.port_var.get()
        if not port:
            messagebox.showerror("Error", "No port selected.")
            return

        self.status_var.set(f"Connecting to {port}...")
        self.connect_btn.config(state="disabled")

        def work():
            self.conn.connect(port)
            return port

        def done(result):
            self.connect_btn.config(state="normal")
            if isinstance(result, Exception):
                self.status_var.set(f"Connection failed: {result}")
                return
            self.connect_btn.config(text="Disconnect")
            self.conn_label.config(text=f"  Connected: {result}", foreground="green")
            self.status_var.set(f"Connected — scanning slots...")
            self.auto_scan_active = True
            self._scan_all()
            self._auto_scan_loop()

        self._run_async(work, done)

    def _run_async(self, work_fn, done_fn):
        def worker():
            try:
                result = work_fn()
            except Exception as e:
                result = e
            self.frame.after(0, lambda: done_fn(result))
        threading.Thread(target=worker, daemon=True).start()

    def _scan_all(self):
        if not self.conn.connected:
            self.status_var.set("Not connected")
            return

        self.status_var.set("Scanning I2C slots...")
        self.scan_btn.config(state="disabled")

        def work():
            return self.conn.send_command({"cmd": "SCAN_CHANNELS"})

        def done(responses):
            self.scan_btn.config(state="normal")
            if isinstance(responses, Exception) or not responses:
                self.status_var.set("Scan failed")
                return

            self.channels = {}
            for resp in responses:
                if resp.get("response") == "BLOCK_DATA":
                    ch = resp.get("channel", -1)
                    self.channels[ch] = resp

            present = sum(1 for d in self.channels.values() if d.get("present"))
            self.status_var.set(f"Found {present} block(s) across {len(self.channels)} channels")
            self._draw_slots()

        self._run_async(work, done)

    def _auto_scan_loop(self):
        if not self.auto_scan_active or not self.conn.connected:
            return
        self._scan_all()
        self.frame.after(2000, self._auto_scan_loop)

    def _send_to_robot(self):
        if not self.conn.connected:
            self.status_var.set("Not connected")
            return

        self.status_var.set("Sending program to robot via ESP-NOW...")
        self.send_btn.config(state="disabled")

        def work():
            return self.conn.send_command({"cmd": "SEND_TO_ROBOT"})

        def done(responses):
            self.send_btn.config(state="normal")
            if isinstance(responses, Exception) or not responses:
                self.status_var.set("Send failed")
                return
            for r in responses:
                if r.get("response") == "SEND_OK":
                    self.status_var.set("Program sent to robot!")
                    return
            self.status_var.set("Send completed (no confirmation)")

        self._run_async(work, done)

    def _draw_slots(self):
        c = self.cards_canvas
        c.delete("all")

        w = c.winfo_width() or 600
        h = c.winfo_height() or 250

        num_slots = max(len(self.channels), 2)
        card_w = min(120, (w - 40) // num_slots - 10)
        card_h = 160
        total_w = num_slots * (card_w + 10) - 10
        start_x = max(20, (w - total_w) // 2)
        y = max(20, (h - card_h) // 2 - 10)

        if not self.channels:
            # Draw placeholder slots
            for i in range(2):
                x = start_x + i * (card_w + 10)
                c.create_rectangle(x, y, x + card_w, y + card_h,
                                   fill="#3a3a3a", outline="#555", width=1, dash=(4, 4))
                c.create_text(x + card_w // 2, y + card_h // 2,
                              text=f"Slot {i}\n\nEmpty", fill="#666",
                              font=("Helvetica", 10), justify="center")
            return

        for ch, data in sorted(self.channels.items()):
            x = start_x + ch * (card_w + 10)
            present = data.get("present", False)

            if present:
                type_id = data.get("type", 0xFF)
                if type_id in BLOCK_NAMES:
                    name, category, color = BLOCK_NAMES[type_id]
                elif type_id == 0xFF:
                    name, category, color = "BLANK", "", "#555"
                else:
                    name, category, color = f"0x{type_id:02X}", "Unknown", "#555"

                is_selected = (ch == self.selected_channel)

                # Card outline
                if is_selected:
                    c.create_rectangle(x - 3, y - 3, x + card_w + 3, y + card_h + 3,
                                       outline="#FFD700", width=3)

                # Card body
                c.create_rectangle(x, y, x + card_w, y + card_h,
                                   fill=color, outline="#fff", width=1)

                # Slot number
                c.create_rectangle(x, y, x + card_w, y + 22, fill="#1a1a1a", outline="")
                c.create_text(x + card_w // 2, y + 11,
                              text=f"SLOT {ch}", fill="#fff",
                              font=("Helvetica", 8, "bold"))

                # Block name
                text_color = "#000" if name == "WHITE_LIGHT_ON" else "#fff"
                display = name if len(name) <= 12 else name[:11] + ".."
                c.create_text(x + card_w // 2, y + card_h // 2,
                              text=display, fill=text_color,
                              font=("Helvetica", 10, "bold"), width=card_w - 8)

                # Category
                if category:
                    c.create_text(x + card_w // 2, y + card_h // 2 + 20,
                                  text=category, fill=text_color,
                                  font=("Helvetica", 7))

                # Block label from EEPROM
                block_name = data.get("name", "")
                if block_name and block_name.strip():
                    c.create_text(x + card_w // 2, y + card_h - 20,
                                  text=block_name[:12], fill=text_color,
                                  font=("Helvetica", 8, "italic"))

                # Checksum indicator
                if data.get("checksum_valid"):
                    c.create_oval(x + card_w - 14, y + card_h - 14,
                                  x + card_w - 4, y + card_h - 4,
                                  fill="#4CAF50", outline="")
                else:
                    c.create_oval(x + card_w - 14, y + card_h - 14,
                                  x + card_w - 4, y + card_h - 4,
                                  fill="#f44336", outline="")

                # Click handler
                tag = f"slot_{ch}"
                c.addtag_withtag(tag, c.create_rectangle(x, y, x + card_w, y + card_h,
                                                          fill="", outline="", width=0))
                c.tag_bind(tag, "<Button-1>", lambda e, channel=ch: self._select_slot(channel))

            else:
                # Empty slot
                c.create_rectangle(x, y, x + card_w, y + card_h,
                                   fill="#3a3a3a", outline="#555", width=1, dash=(4, 4))
                c.create_text(x + card_w // 2, y + 11,
                              text=f"SLOT {ch}", fill="#888",
                              font=("Helvetica", 8, "bold"))
                c.create_text(x + card_w // 2, y + card_h // 2,
                              text="No Block\nInserted", fill="#666",
                              font=("Helvetica", 9), justify="center")

    def _select_slot(self, ch):
        self.selected_channel = ch
        data = self.channels.get(ch, {})

        self.detail_labels["channel"].config(text=str(ch))

        if not data.get("present"):
            for key in self.detail_labels:
                if key != "channel":
                    self.detail_labels[key].config(text="---")
            self.status_var.set(f"Slot {ch}: empty")
            self._draw_slots()
            return

        type_id = data.get("type", 0xFF)
        if type_id in BLOCK_NAMES:
            name, category, _ = BLOCK_NAMES[type_id]
            self.detail_labels["type_str"].config(text=f"{name} (0x{type_id:02X})")
            self.detail_labels["category"].config(text=category)
        elif type_id == 0xFF:
            self.detail_labels["type_str"].config(text="BLANK (0xFF)")
            self.detail_labels["category"].config(text="---")
        else:
            self.detail_labels["type_str"].config(text=f"Unknown (0x{type_id:02X})")
            self.detail_labels["category"].config(text="---")

        self.detail_labels["name"].config(text=data.get("name", "") or "(empty)")
        self.detail_labels["serial"].config(text=data.get("serial", "---"))

        ck_valid = data.get("checksum_valid", False)
        self.detail_labels["checksum"].config(
            text=f"{data.get('checksum', '?')} ({'OK' if ck_valid else 'MISMATCH'})",
            foreground="green" if ck_valid else "red")

        self.detail_labels["params"].config(
            text=f"p1={data.get('param1', '?')}  p2={data.get('param2', '?')}  "
                 f"sub={data.get('subtype', '?')}  v={data.get('version', '?')}")

        self.status_var.set(f"Slot {ch}: {BLOCK_NAMES.get(type_id, ('?',))[0]}")
        self._draw_slots()


def main():
    root = tk.Tk()
    root.title("Bloco Board Monitor")
    root.geometry("700x580")
    root.resizable(True, True)

    notebook = ttk.Notebook(root)
    notebook.pack(fill="both", expand=True)

    # Slots tab (created first so flash can reference it)
    slots_tab = SlotsTab(root)
    notebook.add(slots_tab.frame, text="  I2C Blocks  ")

    def on_flash_done(port):
        notebook.select(slots_tab.frame)
        slots_tab.auto_connect(port)

    # Flash tab
    flash_tab = FlashTab(root, notebook, on_flash_done)
    notebook.insert(0, flash_tab.frame, text="  Flash  ")
    notebook.select(flash_tab.frame)

    root.mainloop()


if __name__ == "__main__":
    main()
