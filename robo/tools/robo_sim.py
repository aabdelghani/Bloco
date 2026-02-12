#!/usr/bin/env python3
"""Bloco Robot Simulator - Flash robo firmware and monitor received programs."""

import os
import re
import subprocess
import sys
import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import time

# Paths
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROBO_PROJECT_DIR = os.path.dirname(SCRIPT_DIR)  # robo/
IDF_PATH = os.environ.get(
    "IDF_PATH",
    os.path.expanduser("~/.espressif/v5.5.2/esp-idf"),
)

# Block type names (mirrors block_types.h)
BLOCK_NAMES = {
    0x01: "BEGIN", 0x02: "END",
    0x10: "FORWARD", 0x11: "BACKWARD", 0x12: "TURN_RIGHT", 0x13: "TURN_LEFT",
    0x14: "SHAKE", 0x15: "SPIN",
    0x20: "REPEAT", 0x21: "END_REPEAT", 0x22: "IF", 0x23: "END_IF",
    0x30: "BEEP", 0x31: "SING", 0x32: "PLAY_TRIANGLE", 0x33: "PLAY_CIRCLE", 0x34: "PLAY_SQUARE",
    0x40: "WHITE_LIGHT_ON", 0x41: "RED_LIGHT_ON", 0x42: "BLUE_LIGHT_ON",
    0x50: "WAIT_FOR_CLAP",
    0x60: "PARAM_2", 0x61: "PARAM_3", 0x62: "PARAM_4", 0x63: "PARAM_FOREVER",
    0x64: "PARAM_LIGHT", 0x65: "PARAM_DARK", 0x66: "PARAM_NEAR", 0x67: "PARAM_FAR",
    0x68: "PARAM_UNTIL_LIGHT", 0x69: "PARAM_UNTIL_DARK", 0x6A: "PARAM_UNTIL_NEAR", 0x6B: "PARAM_UNTIL_FAR",
    0x70: "SENSOR_LIGHT_BULB", 0x71: "SENSOR_EAR", 0x72: "SENSOR_EYE",
    0x73: "SENSOR_TELESCOPE", 0x74: "SENSOR_SOUND_MODULE",
}

# Colors for block categories
BLOCK_COLORS = {
    "BEGIN": "#4CAF50", "END": "#f44336",
    "FORWARD": "#2196F3", "BACKWARD": "#2196F3", "TURN_RIGHT": "#2196F3",
    "TURN_LEFT": "#2196F3", "SHAKE": "#2196F3", "SPIN": "#2196F3",
    "REPEAT": "#FF9800", "END_REPEAT": "#FF9800", "IF": "#FF9800", "END_IF": "#FF9800",
    "BEEP": "#9C27B0", "SING": "#9C27B0", "PLAY_TRIANGLE": "#9C27B0",
    "PLAY_CIRCLE": "#9C27B0", "PLAY_SQUARE": "#9C27B0",
    "WHITE_LIGHT_ON": "#FFEB3B", "RED_LIGHT_ON": "#f44336", "BLUE_LIGHT_ON": "#2196F3",
    "WAIT_FOR_CLAP": "#795548",
}


def detect_port():
    for p in serial.tools.list_ports.comports():
        if "ACM" in p.device or "USB" in p.device:
            return p.device
    ports = [p.device for p in serial.tools.list_ports.comports()]
    return ports[0] if ports else None


class FlashTab:
    """Tab that builds and flashes the robo firmware."""

    def __init__(self, parent, notebook, on_done):
        self.frame = ttk.Frame(parent)
        self.notebook = notebook
        self.on_done = on_done
        self.process = None

        # Port selector
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

        # Output text
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
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo["values"] = ports
        if self.port_var.get() not in ports:
            detected = detect_port()
            if detected:
                self.port_var.set(detected)
            elif ports:
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
        self._append_text(f">>> Building and flashing robo firmware to {port}...\n\n", "info")

        def run_flash():
            export_script = os.path.join(IDF_PATH, "export.sh")
            cmd = f'source "{export_script}" && cd "{ROBO_PROJECT_DIR}" && idf.py -p {port} build flash'
            try:
                proc = subprocess.Popen(
                    ["bash", "-c", cmd],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
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


class MonitorTab:
    """Tab that monitors serial output and visualizes received programs."""

    def __init__(self, parent):
        self.frame = ttk.Frame(parent)
        self.serial_thread = None
        self.ser = None
        self.running = False

        # Top bar
        top = ttk.Frame(self.frame)
        top.pack(fill="x", padx=8, pady=(8, 0))

        ttk.Label(top, text="Port:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(top, textvariable=self.port_var, width=18, state="readonly")
        self.port_combo.pack(side="left", padx=4)
        self._refresh_ports()

        ttk.Button(top, text="Refresh", command=self._refresh_ports).pack(side="left", padx=2)
        self.connect_btn = ttk.Button(top, text="Connect", command=self._toggle_connect)
        self.connect_btn.pack(side="left", padx=4)

        self.conn_label = ttk.Label(top, text="Disconnected", foreground="red")
        self.conn_label.pack(side="left", padx=8)

        ttk.Button(top, text="Clear", command=self._clear_all).pack(side="right", padx=4)

        # Paned window: top=blocks visualization, bottom=serial log
        paned = ttk.PanedWindow(self.frame, orient="vertical")
        paned.pack(fill="both", expand=True, padx=8, pady=8)

        # Program visualization
        viz_frame = ttk.LabelFrame(self.frame, text="Received Program")
        paned.add(viz_frame, weight=1)

        self.block_canvas = tk.Canvas(viz_frame, bg="#2d2d2d", height=120, highlightthickness=0)
        self.block_canvas.pack(fill="both", expand=True, padx=4, pady=4)

        # Execution status
        status_row = ttk.Frame(viz_frame)
        status_row.pack(fill="x", padx=4, pady=(0, 4))
        self.exec_label = ttk.Label(status_row, text="Waiting for program...", font=("", 10))
        self.exec_label.pack(side="left")

        # Serial log
        log_frame = ttk.LabelFrame(self.frame, text="Serial Monitor")
        paned.add(log_frame, weight=2)

        self.log_text = tk.Text(log_frame, wrap="word", font=("monospace", 9),
                                bg="#1e1e1e", fg="#d4d4d4", insertbackground="#d4d4d4",
                                state="disabled")
        scrollbar = ttk.Scrollbar(log_frame, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="right", fill="y")
        self.log_text.pack(side="left", fill="both", expand=True)

        self.log_text.tag_configure("recv", foreground="#569cd6")
        self.log_text.tag_configure("exec", foreground="#6a9955")
        self.log_text.tag_configure("warn", foreground="#ce9178")
        self.log_text.tag_configure("error", foreground="#f44747")
        self.log_text.tag_configure("info", foreground="#d4d4d4")

        # State
        self.received_blocks = []
        self.current_exec_index = -1

    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo["values"] = ports
        if self.port_var.get() not in ports:
            detected = detect_port()
            if detected:
                self.port_var.set(detected)
            elif ports:
                self.port_var.set(ports[0])

    def auto_connect(self, port):
        self.port_var.set(port)
        self.frame.after(1500, self._do_connect)

    def _toggle_connect(self):
        if self.running:
            self._disconnect()
        else:
            self._do_connect()

    def _do_connect(self):
        port = self.port_var.get()
        if not port:
            messagebox.showerror("Error", "No port selected.")
            return

        try:
            self.ser = serial.Serial(port, 115200, timeout=0.5)
            time.sleep(0.1)
            self.ser.reset_input_buffer()
        except Exception as e:
            messagebox.showerror("Error", f"Could not open {port}: {e}")
            return

        self.running = True
        self.connect_btn.configure(text="Disconnect")
        self.conn_label.configure(text=f"Connected: {port}", foreground="green")
        self.port_combo.configure(state="disabled")

        self.serial_thread = threading.Thread(target=self._serial_reader, daemon=True)
        self.serial_thread.start()

    def _disconnect(self):
        self.running = False
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.ser = None
        self.connect_btn.configure(text="Connect")
        self.conn_label.configure(text="Disconnected", foreground="red")
        self.port_combo.configure(state="readonly")

    def _serial_reader(self):
        while self.running and self.ser and self.ser.is_open:
            try:
                raw = self.ser.readline()
                if raw:
                    line = raw.decode(errors="replace").rstrip()
                    if line:
                        self.frame.after(0, self._process_line, line)
            except Exception:
                if self.running:
                    self.frame.after(0, self._disconnect)
                break

    def _process_line(self, line):
        # Determine tag based on content
        tag = "info"
        if "Program start" in line or "Received block" in line:
            tag = "recv"
        elif "Executing" in line or "Forward" in line or "Backward" in line or \
             "Turn" in line or "Shake" in line or "Spin" in line or "Beep" in line or \
             "Program END" in line or "Program finished" in line:
            tag = "exec"
        elif "WARN" in line or "Incomplete" in line:
            tag = "warn"
        elif "ERROR" in line:
            tag = "error"

        self._append_log(line + "\n", tag)

        # Parse ESP-NOW receive events
        # "Program start: expecting N blocks"
        m = re.search(r"Program start: expecting (\d+) block", line)
        if m:
            self.received_blocks = []
            self.current_exec_index = -1
            self.exec_label.configure(text=f"Receiving {m.group(1)} blocks...")
            self._draw_blocks()
            return

        # "Received block N: type=0xXX name=..."
        m = re.search(r"Received block (\d+): type=0x([0-9A-Fa-f]+)\s+name=(\S*)", line)
        if m:
            idx = int(m.group(1))
            type_id = int(m.group(2), 16)
            name = m.group(3)
            block_name = BLOCK_NAMES.get(type_id, f"0x{type_id:02X}")
            self.received_blocks.append({"index": idx, "type": type_id, "name": block_name, "label": name})
            self.exec_label.configure(text=f"Received {len(self.received_blocks)} block(s)...")
            self._draw_blocks()
            return

        # "Program end"
        if "Program end" in line:
            self.exec_label.configure(text=f"Program received ({len(self.received_blocks)} blocks) - executing...")
            self._draw_blocks()
            return

        # Executor lines: "[N] type=0xXX"
        m = re.search(r"\[(\d+)\] type=0x", line)
        if m:
            self.current_exec_index = int(m.group(1))
            self._draw_blocks()
            return

        # Execution complete
        if "Program finished" in line or "Program END" in line:
            self.current_exec_index = -1
            self.exec_label.configure(text="Execution complete - waiting for next program...")
            self._draw_blocks()

    def _draw_blocks(self):
        c = self.block_canvas
        c.delete("all")

        if not self.received_blocks:
            c.create_text(c.winfo_width() // 2 or 200, 60,
                          text="No program received yet", fill="#666", font=("", 12))
            return

        n = len(self.received_blocks)
        pad = 10
        available = (c.winfo_width() or 500) - 2 * pad
        block_w = min(80, max(40, available // n - 8))
        block_h = 70
        total_w = n * (block_w + 8) - 8
        start_x = max(pad, ((c.winfo_width() or 500) - total_w) // 2)
        y = 25

        for i, blk in enumerate(self.received_blocks):
            x = start_x + i * (block_w + 8)
            name = blk["name"]
            color = BLOCK_COLORS.get(name, "#607D8B")

            # Highlight currently executing block
            if i == self.current_exec_index:
                c.create_rectangle(x - 3, y - 3, x + block_w + 3, y + block_h + 3,
                                   outline="#FFD700", width=3)

            c.create_rectangle(x, y, x + block_w, y + block_h,
                               fill=color, outline="#fff", width=1)

            # Block name (truncate if needed)
            display = name if len(name) <= 10 else name[:9] + ".."
            # Pick text color based on background brightness
            text_color = "#000" if name in ("WHITE_LIGHT_ON",) else "#fff"
            c.create_text(x + block_w // 2, y + block_h // 2,
                          text=display, fill=text_color, font=("", 8, "bold"),
                          width=block_w - 4)
            c.create_text(x + block_w // 2, y + block_h + 10,
                          text=f"#{i}", fill="#888", font=("", 7))

            # Arrow between blocks
            if i < n - 1:
                ax = x + block_w + 1
                c.create_line(ax, y + block_h // 2, ax + 6, y + block_h // 2,
                              fill="#888", arrow="last", width=1)

    def _append_log(self, text, tag=None):
        self.log_text.configure(state="normal")
        if tag:
            self.log_text.insert("end", text, tag)
        else:
            self.log_text.insert("end", text)
        # Keep log from growing too large
        lines = int(self.log_text.index("end-1c").split(".")[0])
        if lines > 2000:
            self.log_text.delete("1.0", f"{lines - 1500}.0")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _clear_all(self):
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")
        self.received_blocks = []
        self.current_exec_index = -1
        self.exec_label.configure(text="Waiting for program...")
        self._draw_blocks()


def main():
    root = tk.Tk(className="Bloco Robot Simulator")
    root.title("Bloco Robot Simulator")
    icon_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icon.png")
    if os.path.exists(icon_path):
        root._icon_img = tk.PhotoImage(file=icon_path)
        root.iconphoto(True, root._icon_img)
    root.geometry("800x650")
    root.resizable(True, True)

    notebook = ttk.Notebook(root)
    notebook.pack(fill="both", expand=True)

    # Monitor tab (created first so flash can reference it)
    monitor_tab = MonitorTab(root)
    notebook.add(monitor_tab.frame, text="  Monitor  ")

    def on_flash_done(port):
        notebook.select(monitor_tab.frame)
        monitor_tab.auto_connect(port)

    # Flash tab
    flash_tab = FlashTab(root, notebook, on_flash_done)
    notebook.insert(0, flash_tab.frame, text="  Flash  ")
    notebook.select(flash_tab.frame)

    root.mainloop()


if __name__ == "__main__":
    main()
