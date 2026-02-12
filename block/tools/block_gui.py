#!/usr/bin/env python3
"""Blocko Block Agent GUI - Program EEPROM blocks visually."""

import json
import os
import subprocess
import sys
import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import time
import base64
import io

# Paths
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BLOCK_PROJECT_DIR = os.path.dirname(SCRIPT_DIR)  # block/
IDF_PATH = os.environ.get(
    "IDF_PATH",
    os.path.expanduser("~/.espressif/v5.5.2/esp-idf"),
)

# 64x64 3D block icon (embedded PNG)
APP_ICON_B64 = (
    "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAACLklEQVR4nO2aP05CQRCHB2NlbWfC"
    "FbSh8wicwNLYWHgFCsINLGyMJSfAxtoDQMMBCBQm1rZYmNGJ8ng7s/Nn8e1Xkse++X1vdt8qC1Cp"
    "hHIz224j79+LuvGu4I/Dnns9rjfkPG0vGS43yWlzaxFmg1vMbQsZ6gN6LGqaItQGiljNNURkDRD9"
    "CqNIZYi+VFLw33BFJF9ccugmUmQctV0wmKy3hxge4Ouh9a9me2s/ThloMd8AAMD5xZlCWT68TJ+T"
    "rksSgKAIgDJlpIamsARQSuoKSXBELACJ6oqc0JRsARSPrtAKjqgKQCxEaAdHTAQgudPDKjTFVACF"
    "0xUewRE3AUhTV3iGpogF3F/L5vfd04+AxXwDb8u5tAQVWrfC/x1xB9AnmYK0Yz7eV63XnJz2RWMD"
    "1A7wXwSljMe3fz4bjR6yx+18B1QB0QVEY7YP4L4lojiYRVBjwduF+j5A+r6Pwq0DpFMiZ5OTQucX"
    "wSoguoBoqoDoAqJRfwscygYI6XwHJP86PJisTX4gtfyX2Go6bM0nOh+gKUNbQEpoStYJEQ0RWgK4"
    "wRG1M0JSGTkCpKEp6qfEuCIkAjSCI2bnBFNFcARoBkdcToruk9EmwCI0xfWs8C4RTQKsgyNhp8VR"
    "BhXgFboo2k5xVYyxbrlLxbFeFcf6RkuAZlAuWWJyBESGboItQ/rncInhAQR1SQSUGh5h1ScRYLIY"
    "KcKqTzoFSpXArqu+BbSqaKD4fUCl0nE+ASiyxzHlOu4SAAAAAElFTkSuQmCC"
)

# Block type definitions: {type_id: (name, category)}
BLOCK_TYPES = {
    # Actions
    0x01: ("BEGIN", "Actions"),
    0x02: ("END", "Actions"),
    # Movement
    0x10: ("FORWARD", "Movement"),
    0x11: ("BACKWARD", "Movement"),
    0x12: ("TURN_RIGHT", "Movement"),
    0x13: ("TURN_LEFT", "Movement"),
    0x14: ("SHAKE", "Movement"),
    0x15: ("SPIN", "Movement"),
    # Control Flow
    0x20: ("REPEAT", "Control Flow"),
    0x21: ("END_REPEAT", "Control Flow"),
    0x22: ("IF", "Control Flow"),
    0x23: ("END_IF", "Control Flow"),
    # Sound
    0x30: ("BEEP", "Sound"),
    0x31: ("SING", "Sound"),
    0x32: ("PLAY_TRIANGLE", "Sound"),
    0x33: ("PLAY_CIRCLE", "Sound"),
    0x34: ("PLAY_SQUARE", "Sound"),
    # Light
    0x40: ("WHITE_LIGHT_ON", "Light"),
    0x41: ("RED_LIGHT_ON", "Light"),
    0x42: ("BLUE_LIGHT_ON", "Light"),
    # Wait
    0x50: ("WAIT_FOR_CLAP", "Wait"),
    # Parameters
    0x60: ("PARAM_2", "Parameters"),
    0x61: ("PARAM_3", "Parameters"),
    0x62: ("PARAM_4", "Parameters"),
    0x63: ("PARAM_FOREVER", "Parameters"),
    0x64: ("PARAM_LIGHT", "Parameters"),
    0x65: ("PARAM_DARK", "Parameters"),
    0x66: ("PARAM_NEAR", "Parameters"),
    0x67: ("PARAM_FAR", "Parameters"),
    0x68: ("PARAM_UNTIL_LIGHT", "Parameters"),
    0x69: ("PARAM_UNTIL_DARK", "Parameters"),
    0x6A: ("PARAM_UNTIL_NEAR", "Parameters"),
    0x6B: ("PARAM_UNTIL_FAR", "Parameters"),
    # Sensors
    0x70: ("SENSOR_LIGHT_BULB", "Sensors"),
    0x71: ("SENSOR_EAR", "Sensors"),
    0x72: ("SENSOR_EYE", "Sensors"),
    0x73: ("SENSOR_TELESCOPE", "Sensors"),
    0x74: ("SENSOR_SOUND_MODULE", "Sensors"),
}

CATEGORIES = ["All", "Actions", "Movement", "Control Flow", "Sound", "Light", "Wait", "Parameters", "Sensors"]

# Reverse lookup: name -> type_id
NAME_TO_ID = {v[0]: k for k, v in BLOCK_TYPES.items()}

# Colors
COLOR_SELECTED = "#3584e4"
COLOR_DEFAULT_BG = "#d0d0d0"
COLOR_BLANK = "#f0f0f0"
COLOR_PROGRAMMED = "#a0d0a0"


def detect_port():
    """Auto-detect the most likely serial port."""
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
        time.sleep(0.3)  # Let ESP boot messages flush
        self.ser.reset_input_buffer()

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.ser = None

    @property
    def connected(self):
        return self.ser is not None and self.ser.is_open

    def send_command(self, cmd_dict):
        if not self.connected:
            return None
        with self.lock:
            try:
                line = json.dumps(cmd_dict) + "\n"
                self.ser.reset_input_buffer()
                self.ser.write(line.encode())
                self.ser.flush()
                # Read response lines, skip ESP log lines
                for _ in range(10):
                    raw = self.ser.readline().decode(errors="replace").strip()
                    if not raw:
                        continue
                    print(f"[serial] rx: {raw}", file=sys.stderr)
                    if raw.startswith("{"):
                        try:
                            return json.loads(raw)
                        except json.JSONDecodeError:
                            continue
                print(f"[serial] no response for: {cmd_dict.get('cmd')}", file=sys.stderr)
            except Exception as e:
                print(f"Serial error: {e}", file=sys.stderr)
            return None

    @staticmethod
    def list_ports():
        return [p.device for p in serial.tools.list_ports.comports()]


class FlashTab:
    """Tab that builds and flashes the block firmware, streaming output."""

    def __init__(self, parent, notebook, on_done):
        self.frame = ttk.Frame(parent)
        self.notebook = notebook
        self.on_done = on_done
        self.process = None

        # Port selector row
        port_frame = ttk.Frame(self.frame)
        port_frame.pack(fill="x", padx=8, pady=(8, 0))

        ttk.Label(port_frame, text="Flash port:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(port_frame, textvariable=self.port_var, width=18, state="readonly")
        self.port_combo.pack(side="left", padx=4)
        self._refresh_ports()

        self.flash_btn = ttk.Button(port_frame, text="Re-flash", command=self.start_flash)
        self.flash_btn.pack(side="left", padx=4)

        self.status_label = ttk.Label(port_frame, text="", foreground="blue")
        self.status_label.pack(side="right", padx=8)

        # Output text area
        text_frame = ttk.Frame(self.frame)
        text_frame.pack(fill="both", expand=True, padx=8, pady=8)

        self.text = tk.Text(text_frame, wrap="word", font=("monospace", 9),
                            bg="#1e1e1e", fg="#d4d4d4", insertbackground="#d4d4d4",
                            state="disabled")
        scrollbar = ttk.Scrollbar(text_frame, command=self.text.yview)
        self.text.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="right", fill="y")
        self.text.pack(side="left", fill="both", expand=True)

        # Color tags
        self.text.tag_configure("error", foreground="#f44747")
        self.text.tag_configure("success", foreground="#6a9955")
        self.text.tag_configure("info", foreground="#569cd6")

    def _refresh_ports(self):
        ports = SerialConnection.list_ports()
        self.port_combo["values"] = ports
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
            messagebox.showerror("Error", "No serial port found. Connect your ESP32-S3.")
            return

        self.flash_btn.configure(state="disabled")
        self.port_combo.configure(state="disabled")
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        self.text.configure(state="disabled")

        self.status_label.configure(text="Building & flashing...", foreground="blue")
        self._append_text(f">>> Building and flashing block firmware to {port}...\n\n", "info")

        def run_flash():
            export_script = os.path.join(IDF_PATH, "export.sh")
            cmd = f'source "{export_script}" && cd "{BLOCK_PROJECT_DIR}" && idf.py -p {port} build flash'
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
            self.status_label.configure(text="Flash FAILED — fix errors and click Re-flash", foreground="red")


class ProgramTab:
    """Tab with the block programming GUI."""

    def __init__(self, parent):
        self.frame = ttk.Frame(parent)
        self.root = parent
        self.conn = SerialConnection()
        self.selected_channel = 0
        self.channel_data = [None] * 8
        self._build_ui()
        self._select_channel(0)

    def _build_ui(self):
        # ── Connection bar ──
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

        # ── Channel buttons ──
        ch_frame = ttk.LabelFrame(self.frame, text="Channels (click to select)")
        ch_frame.pack(fill="x", padx=8, pady=8)

        self.ch_buttons = []
        for i in range(8):
            btn = tk.Button(
                ch_frame, text=f"Ch {i}\n---",
                width=8, height=2, relief="groove",
                bg=COLOR_DEFAULT_BG,
                activebackground=COLOR_SELECTED,
                command=lambda ch=i: self._select_channel(ch),
            )
            btn.pack(side="left", padx=4, pady=4, expand=True)
            self.ch_buttons.append(btn)

        # ── Main content ──
        content = ttk.Frame(self.frame)
        content.pack(fill="both", expand=True, padx=8)

        # Right panel - EEPROM content
        right = ttk.LabelFrame(content, text="EEPROM Content")
        right.pack(side="right", fill="both", expand=True, padx=(4, 0))

        self.detail_labels = {}
        fields = [
            ("Channel", "channel"),
            ("Type", "type_str"),
            ("Subtype", "subtype"),
            ("Param 1", "param1"),
            ("Param 2", "param2"),
            ("Serial", "serial"),
            ("Name", "name"),
            ("Status", "checksum"),
        ]
        for i, (label, key) in enumerate(fields):
            ttk.Label(right, text=f"{label}:", font=("", 10, "bold")).grid(
                row=i, column=0, sticky="w", padx=8, pady=3
            )
            val = ttk.Label(right, text="---", font=("", 10))
            val.grid(row=i, column=1, sticky="w", padx=8, pady=3)
            self.detail_labels[key] = val

        right.columnconfigure(1, weight=1)

        # ── Bottom panel - Block selector ──
        bottom = ttk.LabelFrame(self.frame, text="Program Block")
        bottom.pack(fill="x", padx=8, pady=(0, 4))

        row1 = ttk.Frame(bottom)
        row1.pack(fill="x", padx=8, pady=4)

        ttk.Label(row1, text="Category:").pack(side="left")
        self.cat_var = tk.StringVar(value="All")
        cat_combo = ttk.Combobox(row1, textvariable=self.cat_var, values=CATEGORIES, state="readonly", width=12)
        cat_combo.pack(side="left", padx=4)
        cat_combo.bind("<<ComboboxSelected>>", self._on_category_change)

        ttk.Label(row1, text="Block:").pack(side="left", padx=(12, 0))
        self.block_var = tk.StringVar()
        self.block_combo = ttk.Combobox(row1, textvariable=self.block_var, state="readonly", width=18)
        self.block_combo.pack(side="left", padx=4)
        self.block_combo.bind("<<ComboboxSelected>>", self._on_block_change)

        ttk.Label(row1, text="Name:").pack(side="left", padx=(12, 0))
        self.name_var = tk.StringVar()
        ttk.Entry(row1, textvariable=self.name_var, width=16).pack(side="left", padx=4)

        row2 = ttk.Frame(bottom)
        row2.pack(fill="x", padx=8, pady=(0, 8))

        self.write_btn = ttk.Button(row2, text="Write Block", command=self._write_block)
        self.write_btn.pack(side="left", padx=4)
        self.erase_btn = ttk.Button(row2, text="Erase", command=self._erase_block)
        self.erase_btn.pack(side="left", padx=4)
        self.refresh_btn = ttk.Button(row2, text="Refresh All", command=self._refresh_all)
        self.refresh_btn.pack(side="left", padx=4)

        # ── Status bar ──
        self.status_var = tk.StringVar(value="Flash firmware first, then connect")
        ttk.Label(self.frame, textvariable=self.status_var, relief="sunken", anchor="w").pack(
            fill="x", side="bottom", padx=8, pady=(0, 8)
        )

        # Populate block combo
        self._on_category_change()

    def auto_connect(self, port):
        """Auto-connect to the given port after flashing."""
        self.port_var.set(port)
        self._set_status(f"Connecting to {port}...")
        self.connect_btn.config(state="disabled")

        def do_connect():
            time.sleep(1)  # Give ESP time to boot after flash
            self.conn.connect(port)
            return port

        def on_connected(result):
            self.connect_btn.config(state="normal")
            if isinstance(result, Exception):
                self._set_status(f"Auto-connect failed: {result}")
                return
            self.connect_btn.config(text="Disconnect")
            self.conn_label.config(text=f"  Connected: {result}", foreground="green")
            self._set_status(f"Connected to {result} - reading channels...")
            self._refresh_all()

        self._run_async(do_connect, on_connected)

    # ── Async helper ────────────────────────────────────────

    def _run_async(self, work_fn, done_fn):
        """Run work_fn in a background thread, then call done_fn(result) on the GUI thread."""
        def _worker():
            try:
                result = work_fn()
            except Exception as e:
                result = e
            self.frame.after(0, lambda: done_fn(result))
        threading.Thread(target=_worker, daemon=True).start()

    # ── Connection ─────────────────────────────────────────

    def _refresh_ports(self):
        ports = SerialConnection.list_ports()
        self.port_combo["values"] = ports
        if ports:
            for p in ports:
                if "ACM" in p or "USB" in p:
                    self.port_var.set(p)
                    return
            self.port_var.set(ports[0])

    def _toggle_connect(self):
        if self.conn.connected:
            self.conn.disconnect()
            self.connect_btn.config(text="Connect")
            self.conn_label.config(text="  Disconnected", foreground="red")
            self._set_status("Disconnected")
        else:
            port = self.port_var.get()
            if not port:
                messagebox.showerror("Error", "No port selected. Click 'Refresh Ports'.")
                return
            self._set_status(f"Connecting to {port}...")
            self.connect_btn.config(state="disabled")

            def do_connect():
                self.conn.connect(port)
                return port

            def on_connected(result):
                self.connect_btn.config(state="normal")
                if isinstance(result, Exception):
                    self._set_status(f"Connection failed: {result}")
                    messagebox.showerror("Connection Error", str(result))
                    return
                self.connect_btn.config(text="Disconnect")
                self.conn_label.config(text=f"  Connected: {result}", foreground="green")
                self._set_status(f"Connected to {result} - reading channels...")
                self._refresh_all()

            self._run_async(do_connect, on_connected)

    # ── Channel Selection ──────────────────────────────────

    def _select_channel(self, ch):
        self.selected_channel = ch
        for i, btn in enumerate(self.ch_buttons):
            if i == ch:
                btn.config(relief="solid", bg=COLOR_SELECTED, fg="white",
                           activebackground=COLOR_SELECTED, activeforeground="white")
            else:
                data = self.channel_data[i]
                if data and data.get("type", 255) != 255:
                    bg = COLOR_PROGRAMMED
                else:
                    bg = COLOR_DEFAULT_BG
                btn.config(relief="groove", bg=bg, fg="black",
                           activebackground=bg, activeforeground="black")

        # Update detail from cache or read
        if self.conn.connected:
            self._read_channel(ch)
        elif self.channel_data[ch]:
            self._update_detail(ch, self.channel_data[ch])
        else:
            self._show_no_data(ch)

    def _read_channel(self, ch):
        if not self.conn.connected:
            self._set_status("Not connected - click Connect first")
            return

        self._set_status(f"Reading channel {ch}...")

        def do_read():
            return self.conn.send_command({"cmd": "READ_BLOCK", "channel": ch})

        def on_read(resp):
            if isinstance(resp, Exception):
                self._set_status(f"Read error: {resp}")
                return
            if resp and resp.get("response") == "READ_DATA":
                self.channel_data[ch] = resp
                self._update_detail(ch, resp)
                self._update_channel_button(ch, resp)
            else:
                self.channel_data[ch] = None
                self._show_no_data(ch)
                self._set_status(f"Failed to read channel {ch}")

        self._run_async(do_read, on_read)

    def _show_no_data(self, ch):
        self.detail_labels["channel"].config(text=str(ch))
        self.detail_labels["type_str"].config(text="---")
        self.detail_labels["subtype"].config(text="---")
        self.detail_labels["param1"].config(text="---")
        self.detail_labels["param2"].config(text="---")
        self.detail_labels["serial"].config(text="---")
        self.detail_labels["name"].config(text="---")
        self.detail_labels["checksum"].config(text="No data", foreground="gray")

    def _update_detail(self, ch, data):
        type_id = data.get("type", 255)
        if type_id in BLOCK_TYPES:
            name, cat = BLOCK_TYPES[type_id]
            type_str = f"{name} (0x{type_id:02X})"
        elif type_id == 255:
            type_str = "[blank] (0xFF)"
        else:
            type_str = f"Unknown (0x{type_id:02X})"

        self.detail_labels["channel"].config(text=str(ch))
        self.detail_labels["type_str"].config(text=type_str)
        self.detail_labels["subtype"].config(text=str(data.get("subtype", "---")))
        self.detail_labels["param1"].config(text=str(data.get("param1", "---")))
        self.detail_labels["param2"].config(text=str(data.get("param2", "---")))
        self.detail_labels["serial"].config(text=data.get("serial", "---"))
        self.detail_labels["name"].config(text=data.get("name", "") or "(empty)")

        if type_id in BLOCK_TYPES:
            self.detail_labels["checksum"].config(text="Valid - Programmed", foreground="green")
        elif type_id == 255:
            self.detail_labels["checksum"].config(text="Blank EEPROM", foreground="gray")
        else:
            self.detail_labels["checksum"].config(text="Unknown block type", foreground="orange")

        self._set_status(f"Channel {ch}: {type_str}")

    def _update_channel_button(self, ch, data):
        type_id = data.get("type", 255)
        if type_id in BLOCK_TYPES:
            name = BLOCK_TYPES[type_id][0]
            if len(name) > 10:
                name = name[:9] + ".."
            self.ch_buttons[ch].config(text=f"Ch {ch}\n{name}")
        elif type_id == 255:
            self.ch_buttons[ch].config(text=f"Ch {ch}\n[blank]")
        else:
            self.ch_buttons[ch].config(text=f"Ch {ch}\n0x{type_id:02X}")

        # Re-apply selection colors
        self._recolor_buttons()

    def _recolor_buttons(self):
        for i, btn in enumerate(self.ch_buttons):
            if i == self.selected_channel:
                btn.config(relief="solid", bg=COLOR_SELECTED, fg="white",
                           activebackground=COLOR_SELECTED, activeforeground="white")
            else:
                data = self.channel_data[i]
                if data and data.get("type", 255) != 255:
                    bg = COLOR_PROGRAMMED
                else:
                    bg = COLOR_DEFAULT_BG
                btn.config(relief="groove", bg=bg, fg="black",
                           activebackground=bg, activeforeground="black")

    # ── Block Type Selection ───────────────────────────────

    def _on_category_change(self, event=None):
        cat = self.cat_var.get()
        if cat == "All":
            names = [v[0] for v in BLOCK_TYPES.values()]
        else:
            names = [v[0] for v in BLOCK_TYPES.values() if v[1] == cat]
        self.block_combo["values"] = names
        if names:
            self.block_combo.current(0)
            self._on_block_change()

    def _on_block_change(self, event=None):
        name = self.block_var.get()
        friendly = name.replace("_", " ").title()
        if len(friendly) > 15:
            friendly = friendly[:15]
        self.name_var.set(friendly)

    # ── Actions ────────────────────────────────────────────

    def _write_block(self):
        if not self.conn.connected:
            messagebox.showwarning("Not Connected", "Connect to a serial port first.")
            return

        block_name = self.block_var.get()
        if not block_name or block_name not in NAME_TO_ID:
            messagebox.showwarning("Warning", "Select a block type first.")
            return

        ch = self.selected_channel
        type_id = NAME_TO_ID[block_name]
        label = self.name_var.get().strip()[:15]

        self._set_status(f"Writing {block_name} to channel {ch}...")

        def do_write():
            cmd = {"cmd": "WRITE_BLOCK", "channel": ch, "type": type_id, "name": label}
            return self.conn.send_command(cmd)

        def on_write(resp):
            if isinstance(resp, Exception):
                self._set_status(f"Write error: {resp}")
                return
            if resp and resp.get("response") == "WRITE_OK":
                ser = resp.get("serial", "?")
                self._set_status(f"Wrote {block_name} to channel {ch} (serial: {ser})")
                self._read_channel(ch)
                self._recolor_buttons()
            else:
                msg = resp.get("message", "Unknown error") if resp else "No response from device"
                self._set_status(f"Write failed: {msg}")
                messagebox.showerror("Write Error", msg)

        self._run_async(do_write, on_write)

    def _erase_block(self):
        if not self.conn.connected:
            messagebox.showwarning("Not Connected", "Connect to a serial port first.")
            return

        ch = self.selected_channel
        if not messagebox.askyesno("Confirm Erase", f"Erase channel {ch}?"):
            return

        self._set_status(f"Erasing channel {ch}...")

        def do_erase():
            return self.conn.send_command({"cmd": "ERASE_BLOCK", "channel": ch})

        def on_erase(resp):
            if isinstance(resp, Exception):
                self._set_status(f"Erase error: {resp}")
                return
            if resp and resp.get("response") == "ERASE_OK":
                self._set_status(f"Erased channel {ch}")
                self._read_channel(ch)
                self._recolor_buttons()
            else:
                msg = resp.get("message", "Unknown error") if resp else "No response from device"
                self._set_status(f"Erase failed: {msg}")

        self._run_async(do_erase, on_erase)

    def _refresh_all(self):
        if not self.conn.connected:
            messagebox.showwarning("Not Connected", "Connect to a serial port first.")
            return

        self._set_status("Reading all channels...")

        def do_refresh():
            results = {}
            for ch in range(8):
                resp = self.conn.send_command({"cmd": "READ_BLOCK", "channel": ch})
                results[ch] = resp
            return results

        def on_refresh(results):
            if isinstance(results, Exception):
                self._set_status(f"Refresh error: {results}")
                return
            for ch in range(8):
                resp = results.get(ch)
                if resp and resp.get("response") == "READ_DATA":
                    self.channel_data[ch] = resp
                    self._update_channel_button(ch, resp)
                else:
                    self.channel_data[ch] = None
                    self.ch_buttons[ch].config(text=f"Ch {ch}\n???")

            ch = self.selected_channel
            if self.channel_data[ch]:
                self._update_detail(ch, self.channel_data[ch])
            else:
                self._show_no_data(ch)

            self._recolor_buttons()
            self._set_status("All channels refreshed")

        self._run_async(do_refresh, on_refresh)

    def _set_status(self, msg):
        self.status_var.set(msg)
        self.frame.update_idletasks()


def main():
    root = tk.Tk(className="Bloco Block Agent")
    root.title("Bloco Block Agent")
    icon_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icon.png")
    if os.path.exists(icon_path):
        root._icon_img = tk.PhotoImage(file=icon_path)
        root.iconphoto(True, root._icon_img)
    root.geometry("750x600")
    root.resizable(False, False)

    # Notebook with two tabs
    notebook = ttk.Notebook(root)
    notebook.pack(fill="both", expand=True)

    # Program tab (created first so flash can reference it)
    program_tab = ProgramTab(root)
    notebook.add(program_tab.frame, text="  Program  ")

    def on_flash_done(port):
        notebook.select(program_tab.frame)
        program_tab.auto_connect(port)

    # Flash tab
    flash_tab = FlashTab(root, notebook, on_flash_done)
    notebook.insert(0, flash_tab.frame, text="  Flash  ")
    notebook.select(flash_tab.frame)

    # Auto-start flash
    root.after(300, flash_tab.start_flash)

    root.mainloop()


if __name__ == "__main__":
    main()
