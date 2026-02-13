#!/usr/bin/env python3
"""Bloco Launchpad — GUI development launcher for the Bloco project."""

import glob
import os
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk

# --- Paths ---
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
IDF_PATH = os.environ.get(
    "IDF_PATH",
    os.path.expanduser("~/.espressif/v5.5.2/esp-idf"),
)

# --- Theme ---
BG = "#1a1a2e"
BG_CARD = "#16213e"
BG_INPUT = "#0f3460"
FG = "#e0e0e0"
FG_DIM = "#7a7a8e"
ACCENT = "#00b4d8"
GREEN = "#06d6a0"
RED = "#ef476f"
YELLOW = "#ffd166"
BLUE = "#118ab2"


def detect_ports():
    return sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))


def _set_icon(root):
    icon_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icon.png")
    if os.path.exists(icon_path):
        root._icon_img = tk.PhotoImage(file=icon_path)
        root.iconphoto(True, root._icon_img)


class Launchpad(tk.Tk):
    def __init__(self):
        super().__init__(className="Bloco Launchpad")
        self.title("Bloco Launchpad")
        _set_icon(self)
        self.geometry("700x600")
        self.configure(bg=BG)
        self.resizable(False, False)

        self.board_port = None
        self.second_port = None
        self.mode = None  # "robo", "block", "board_only"

        # Title bar
        title_frame = tk.Frame(self, bg=BG)
        title_frame.pack(fill="x", padx=20, pady=(20, 0))

        tk.Label(title_frame, text="BLOCO", font=("Helvetica", 28, "bold"),
                 fg=ACCENT, bg=BG).pack(side="left")
        tk.Label(title_frame, text="  LAUNCHPAD", font=("Helvetica", 28),
                 fg=FG_DIM, bg=BG).pack(side="left")

        tk.Label(self, text="Development launcher for the Bloco physical programming system",
                 font=("Helvetica", 10), fg=FG_DIM, bg=BG).pack(anchor="w", padx=22)

        # Separator
        tk.Frame(self, bg=ACCENT, height=2).pack(fill="x", padx=20, pady=(10, 0))

        # Main content area — holds wizard steps
        self.content = tk.Frame(self, bg=BG)
        self.content.pack(fill="both", expand=True, padx=20, pady=10)

        # Status bar
        self.status_var = tk.StringVar(value="Welcome")
        status_bar = tk.Label(self, textvariable=self.status_var, font=("Helvetica", 9),
                              fg=FG_DIM, bg=BG_CARD, anchor="w", padx=10, pady=4)
        status_bar.pack(fill="x", side="bottom")

        # Start wizard
        self._show_step1()

    def _clear_content(self):
        for w in self.content.winfo_children():
            w.destroy()

    def _make_card(self, parent, title):
        card = tk.Frame(parent, bg=BG_CARD, padx=16, pady=12, highlightbackground=BG_INPUT,
                        highlightthickness=1)
        card.pack(fill="x", pady=(0, 10))
        tk.Label(card, text=title, font=("Helvetica", 13, "bold"),
                 fg=ACCENT, bg=BG_CARD, anchor="w").pack(fill="x")
        tk.Frame(card, bg=BG_INPUT, height=1).pack(fill="x", pady=(4, 8))
        return card

    def _make_button(self, parent, text, command, color=ACCENT, width=18):
        btn = tk.Button(parent, text=text, command=command, font=("Helvetica", 11, "bold"),
                        bg=color, fg="#fff", activebackground=color, activeforeground="#fff",
                        relief="flat", cursor="hand2", padx=16, pady=8, width=width)
        return btn

    # ── Step 1: Connect Board ──

    def _show_step1(self):
        self._clear_content()
        self.status_var.set("Step 1 of 4 — Connect Board")

        tk.Label(self.content, text="STEP 1", font=("Helvetica", 10, "bold"),
                 fg=YELLOW, bg=BG).pack(anchor="w")
        card = self._make_card(self.content, "Connect the Board ESP32-S3")

        tk.Label(card, text="This is the main board with the EEPROM reader + I2C mux.\n"
                            "Plug it in via USB.",
                 font=("Helvetica", 10), fg=FG, bg=BG_CARD, justify="left").pack(anchor="w")

        # Port detection area
        detect_frame = tk.Frame(card, bg=BG_CARD)
        detect_frame.pack(fill="x", pady=(10, 0))

        tk.Label(detect_frame, text="Port:", font=("Helvetica", 10, "bold"),
                 fg=FG, bg=BG_CARD).pack(side="left")

        self.board_port_var = tk.StringVar()
        self.board_port_combo = ttk.Combobox(detect_frame, textvariable=self.board_port_var,
                                             width=20, state="readonly")
        self.board_port_combo.pack(side="left", padx=8)

        refresh_btn = tk.Button(detect_frame, text="Refresh", command=self._refresh_board_ports,
                                font=("Helvetica", 9), bg=BG_INPUT, fg=FG, relief="flat",
                                cursor="hand2", padx=8)
        refresh_btn.pack(side="left")

        self.board_detect_label = tk.Label(detect_frame, text="", font=("Helvetica", 10),
                                           fg=FG_DIM, bg=BG_CARD)
        self.board_detect_label.pack(side="left", padx=10)

        self._refresh_board_ports()

        # Auto-detect polling
        self.board_poll_active = True
        self._poll_board_port()

        # Next button
        btn_frame = tk.Frame(self.content, bg=BG)
        btn_frame.pack(fill="x", pady=(10, 0))
        self._make_button(btn_frame, "Next  >>", self._step1_next).pack(side="right")

    def _refresh_board_ports(self):
        ports = detect_ports()
        self.board_port_combo["values"] = ports
        if ports and not self.board_port_var.get():
            self.board_port_var.set(ports[0])
            self.board_detect_label.config(text="Detected!", fg=GREEN)
        elif not ports:
            self.board_detect_label.config(text="No device found — plug in the board", fg=YELLOW)

    def _poll_board_port(self):
        if not hasattr(self, 'board_poll_active') or not self.board_poll_active:
            return
        ports = detect_ports()
        self.board_port_combo["values"] = ports
        if ports and not self.board_port_var.get():
            self.board_port_var.set(ports[0])
            self.board_detect_label.config(text="Detected!", fg=GREEN)
        self.after(1000, self._poll_board_port)

    def _step1_next(self):
        self.board_poll_active = False
        port = self.board_port_var.get()
        if not port:
            self.board_detect_label.config(text="Please connect a device first!", fg=RED)
            return
        self.board_port = port
        self._show_step2()

    # ── Step 2: Choose Mode ──

    def _show_step2(self):
        self._clear_content()
        self.status_var.set(f"Step 2 of 4 — Choose mode  |  Board: {self.board_port}")

        tk.Label(self.content, text="STEP 2", font=("Helvetica", 10, "bold"),
                 fg=YELLOW, bg=BG).pack(anchor="w")
        card = self._make_card(self.content, "What are you working on today?")

        tk.Label(card, text="Choose the second device to connect, or work on the board only.",
                 font=("Helvetica", 10), fg=FG, bg=BG_CARD).pack(anchor="w")

        choices_frame = tk.Frame(card, bg=BG_CARD)
        choices_frame.pack(fill="x", pady=(12, 0))

        self.mode_var = tk.StringVar(value="robo")

        modes = [
            ("robo", "Robot", "Motor controller — receives programs via ESP-NOW", BLUE),
            ("block", "Block Programmer", "Programs EEPROMs via I2C", GREEN),
            ("board_only", "Board Only", "Just work on the board, no second device", YELLOW),
        ]

        for key, label, desc, color in modes:
            row = tk.Frame(choices_frame, bg=BG_CARD)
            row.pack(fill="x", pady=3)
            rb = tk.Radiobutton(row, text=f"  {label}", variable=self.mode_var, value=key,
                                font=("Helvetica", 11, "bold"), fg=color, bg=BG_CARD,
                                selectcolor=BG_INPUT, activebackground=BG_CARD,
                                activeforeground=color, cursor="hand2", anchor="w")
            rb.pack(side="left")
            tk.Label(row, text=f"  — {desc}", font=("Helvetica", 9),
                     fg=FG_DIM, bg=BG_CARD).pack(side="left")

        # Buttons
        btn_frame = tk.Frame(self.content, bg=BG)
        btn_frame.pack(fill="x", pady=(10, 0))
        self._make_button(btn_frame, "<<  Back", self._show_step1, color=BG_INPUT).pack(side="left")
        self._make_button(btn_frame, "Next  >>", self._step2_next).pack(side="right")

    def _step2_next(self):
        self.mode = self.mode_var.get()
        if self.mode == "board_only":
            self.second_port = None
            self._show_step4()
        else:
            self._show_step3()

    # ── Step 3: Connect Second Device ──

    def _show_step3(self):
        self._clear_content()
        device_name = "Robot" if self.mode == "robo" else "Block Programmer"
        self.status_var.set(f"Step 3 of 4 — Connect {device_name}  |  Board: {self.board_port}")

        tk.Label(self.content, text="STEP 3", font=("Helvetica", 10, "bold"),
                 fg=YELLOW, bg=BG).pack(anchor="w")
        card = self._make_card(self.content, f"Connect the {device_name} ESP32-S3")

        tk.Label(card, text=f"Plug in the {device_name} ESP32-S3 via USB.\n"
                            f"It should appear as a new /dev/ttyACM* port.",
                 font=("Helvetica", 10), fg=FG, bg=BG_CARD, justify="left").pack(anchor="w")

        detect_frame = tk.Frame(card, bg=BG_CARD)
        detect_frame.pack(fill="x", pady=(10, 0))

        tk.Label(detect_frame, text="Port:", font=("Helvetica", 10, "bold"),
                 fg=FG, bg=BG_CARD).pack(side="left")

        self.second_port_var = tk.StringVar()
        self.second_port_combo = ttk.Combobox(detect_frame, textvariable=self.second_port_var,
                                              width=20, state="readonly")
        self.second_port_combo.pack(side="left", padx=8)

        refresh_btn = tk.Button(detect_frame, text="Refresh", command=self._refresh_second_ports,
                                font=("Helvetica", 9), bg=BG_INPUT, fg=FG, relief="flat",
                                cursor="hand2", padx=8)
        refresh_btn.pack(side="left")

        self.second_detect_label = tk.Label(detect_frame, text="", font=("Helvetica", 10),
                                            fg=FG_DIM, bg=BG_CARD)
        self.second_detect_label.pack(side="left", padx=10)

        self._refresh_second_ports()

        # Poll for new port
        self.second_poll_active = True
        self._poll_second_port()

        # Buttons
        btn_frame = tk.Frame(self.content, bg=BG)
        btn_frame.pack(fill="x", pady=(10, 0))
        self._make_button(btn_frame, "<<  Back", self._back_to_step2, color=BG_INPUT).pack(side="left")
        self._make_button(btn_frame, "Next  >>", self._step3_next).pack(side="right")

    def _back_to_step2(self):
        self.second_poll_active = False
        self._show_step2()

    def _refresh_second_ports(self):
        ports = [p for p in detect_ports() if p != self.board_port]
        self.second_port_combo["values"] = ports
        if ports and not self.second_port_var.get():
            self.second_port_var.set(ports[0])
            self.second_detect_label.config(text="Detected!", fg=GREEN)
        elif not ports:
            self.second_detect_label.config(text="Waiting for device...", fg=YELLOW)

    def _poll_second_port(self):
        if not hasattr(self, 'second_poll_active') or not self.second_poll_active:
            return
        ports = [p for p in detect_ports() if p != self.board_port]
        self.second_port_combo["values"] = ports
        if ports and not self.second_port_var.get():
            self.second_port_var.set(ports[0])
            self.second_detect_label.config(text="Detected!", fg=GREEN)
        self.after(1000, self._poll_second_port)

    def _step3_next(self):
        self.second_poll_active = False
        port = self.second_port_var.get()
        if not port:
            self.second_detect_label.config(text="Please connect a device first!", fg=RED)
            return
        self.second_port = port
        self._show_step4()

    # ── Step 4: Flash & Launch ──

    def _show_step4(self):
        self._clear_content()
        self.status_var.set(f"Step 4 of 4 — Flash & Launch  |  Board: {self.board_port}"
                            + (f"  |  {self.mode}: {self.second_port}" if self.second_port else ""))

        tk.Label(self.content, text="STEP 4", font=("Helvetica", 10, "bold"),
                 fg=YELLOW, bg=BG).pack(anchor="w")
        card = self._make_card(self.content, "Flash & Launch")

        # Checkboxes
        self.flash_board_var = tk.BooleanVar(value=True)
        self.flash_second_var = tk.BooleanVar(value=True)
        self.open_vscode_var = tk.BooleanVar(value=True)
        self.launch_monitor_board_var = tk.BooleanVar(value=True)
        self.launch_monitor_second_var = tk.BooleanVar(value=True)
        self.launch_gui_var = tk.BooleanVar(value=True)
        self.launch_board_gui_var = tk.BooleanVar(value=True)

        # Flash section
        flash_label = tk.Label(card, text="Flash Firmware", font=("Helvetica", 11, "bold"),
                               fg=FG, bg=BG_CARD)
        flash_label.pack(anchor="w", pady=(0, 4))

        tk.Checkbutton(card, text=f"  Flash Board firmware  ({self.board_port})",
                       variable=self.flash_board_var, font=("Helvetica", 10),
                       fg=FG, bg=BG_CARD, selectcolor=BG_INPUT, activebackground=BG_CARD,
                       activeforeground=FG, cursor="hand2").pack(anchor="w")

        if self.second_port:
            device_name = "Robot" if self.mode == "robo" else "Block"
            tk.Checkbutton(card, text=f"  Flash {device_name} firmware  ({self.second_port})",
                           variable=self.flash_second_var, font=("Helvetica", 10),
                           fg=FG, bg=BG_CARD, selectcolor=BG_INPUT, activebackground=BG_CARD,
                           activeforeground=FG, cursor="hand2").pack(anchor="w")

        tk.Frame(card, bg=BG_INPUT, height=1).pack(fill="x", pady=8)

        # Launch section
        tk.Label(card, text="Launch Tools", font=("Helvetica", 11, "bold"),
                 fg=FG, bg=BG_CARD).pack(anchor="w", pady=(0, 4))

        tk.Checkbutton(card, text="  Open project in VS Code",
                       variable=self.open_vscode_var, font=("Helvetica", 10),
                       fg=FG, bg=BG_CARD, selectcolor=BG_INPUT, activebackground=BG_CARD,
                       activeforeground=FG, cursor="hand2").pack(anchor="w")

        tk.Checkbutton(card, text=f"  Open Board monitor  ({self.board_port})",
                       variable=self.launch_monitor_board_var, font=("Helvetica", 10),
                       fg=FG, bg=BG_CARD, selectcolor=BG_INPUT, activebackground=BG_CARD,
                       activeforeground=FG, cursor="hand2").pack(anchor="w")

        tk.Checkbutton(card, text="  Launch Board Monitor GUI",
                       variable=self.launch_board_gui_var, font=("Helvetica", 10),
                       fg=FG, bg=BG_CARD, selectcolor=BG_INPUT, activebackground=BG_CARD,
                       activeforeground=FG, cursor="hand2").pack(anchor="w")

        if self.second_port:
            device_name = "Robot" if self.mode == "robo" else "Block"
            tk.Checkbutton(card, text=f"  Open {device_name} monitor  ({self.second_port})",
                           variable=self.launch_monitor_second_var, font=("Helvetica", 10),
                           fg=FG, bg=BG_CARD, selectcolor=BG_INPUT, activebackground=BG_CARD,
                           activeforeground=FG, cursor="hand2").pack(anchor="w")

            gui_name = "Robot Simulator" if self.mode == "robo" else "Block Programmer GUI"
            tk.Checkbutton(card, text=f"  Launch {gui_name}",
                           variable=self.launch_gui_var, font=("Helvetica", 10),
                           fg=FG, bg=BG_CARD, selectcolor=BG_INPUT, activebackground=BG_CARD,
                           activeforeground=FG, cursor="hand2").pack(anchor="w")

        # Output area
        self.output_frame = tk.Frame(self.content, bg=BG)
        self.output_frame.pack(fill="both", expand=True, pady=(5, 0))

        self.output_text = tk.Text(self.output_frame, wrap="word", font=("monospace", 9),
                                   bg="#0d1117", fg="#c9d1d9", insertbackground="#c9d1d9",
                                   state="disabled", height=8)
        scrollbar = ttk.Scrollbar(self.output_frame, command=self.output_text.yview)
        self.output_text.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="right", fill="y")
        self.output_text.pack(side="left", fill="both", expand=True)

        self.output_text.tag_configure("ok", foreground=GREEN)
        self.output_text.tag_configure("err", foreground=RED)
        self.output_text.tag_configure("info", foreground=ACCENT)

        # Buttons
        btn_frame = tk.Frame(self.content, bg=BG)
        btn_frame.pack(fill="x", pady=(8, 0))

        back_target = self._show_step3 if self.second_port else self._show_step2
        self._make_button(btn_frame, "<<  Back", back_target, color=BG_INPUT).pack(side="left")
        self.launch_btn = self._make_button(btn_frame, "Launch!", self._do_launch, color=GREEN)
        self.launch_btn.pack(side="right")

    def _log(self, text, tag=None):
        self.output_text.configure(state="normal")
        if tag:
            self.output_text.insert("end", text, tag)
        else:
            self.output_text.insert("end", text)
        self.output_text.see("end")
        self.output_text.configure(state="disabled")

    def _do_launch(self):
        self.launch_btn.configure(state="disabled")
        threading.Thread(target=self._launch_worker, daemon=True).start()

    def _launch_worker(self):
        # Flash board
        if self.flash_board_var.get():
            self.after(0, self._log, ">>> Building & flashing Board...\n", "info")
            self.after(0, lambda: self.status_var.set("Flashing Board..."))
            ok = self._run_flash("board", self.board_port, "Board")
            if ok:
                self.after(0, self._log, "Board flash OK!\n\n", "ok")
            else:
                self.after(0, self._log, "Board flash FAILED!\n\n", "err")

        # Flash second
        if self.second_port and self.flash_second_var.get():
            project = "robo" if self.mode == "robo" else "block"
            name = "Robot" if self.mode == "robo" else "Block"
            self.after(0, self._log, f">>> Building & flashing {name}...\n", "info")
            self.after(0, lambda: self.status_var.set(f"Flashing {name}..."))
            ok = self._run_flash(project, self.second_port, name)
            if ok:
                self.after(0, self._log, f"{name} flash OK!\n\n", "ok")
            else:
                self.after(0, self._log, f"{name} flash FAILED!\n\n", "err")

        # Launch tools
        self.after(0, lambda: self.status_var.set("Launching tools..."))

        if self.open_vscode_var.get():
            self.after(0, self._open_vscode)

        if self.launch_monitor_board_var.get():
            self.after(0, lambda: self._open_monitor(self.board_port, "board", "Board"))

        if self.second_port and self.launch_monitor_second_var.get():
            project = "robo" if self.mode == "robo" else "block"
            name = "Robot" if self.mode == "robo" else "Block"
            self.after(100, lambda: self._open_monitor(self.second_port, project, name))

        if self.launch_board_gui_var.get():
            self.after(200, self._open_board_gui)

        if self.second_port and self.launch_gui_var.get():
            self.after(300, self._open_gui)

        self.after(0, self._log, ">>> All done!\n", "ok")
        self.after(0, lambda: self.status_var.set("Ready! All tools launched."))
        self.after(0, lambda: self.launch_btn.configure(state="normal"))

    def _run_flash(self, project, port, name):
        project_dir = os.path.join(PROJECT_ROOT, project)
        export_script = os.path.join(IDF_PATH, "export.sh")
        cmd = f'source "{export_script}" && cd "{project_dir}" && idf.py -p {port} build flash'

        try:
            proc = subprocess.Popen(
                ["bash", "-c", cmd],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1,
            )
            for line in proc.stdout:
                self.after(0, self._log, line)
            proc.wait()
            ok = proc.returncode == 0

            # For board: verify PCA9548A I2C mux is responding
            if ok and project == "board":
                self.after(0, self._log, "\n>>> Verifying I2C mux (PCA9548A)...\n", "info")
                time.sleep(2)  # wait for ESP to boot
                mux_ok = self._check_board_i2c(port)
                if not mux_ok:
                    self.after(0, self._log,
                               "WARNING: PCA9548A I2C mux not detected!\n"
                               "Check wiring: SDA=GPIO8, SCL=GPIO9, MUX addr=0x70\n", "err")
                else:
                    self.after(0, self._log, "PCA9548A I2C mux OK!\n", "ok")

            return ok
        except Exception as e:
            self.after(0, self._log, f"Error: {e}\n", "err")
            return False

    def _check_board_i2c(self, port):
        """Read boot output from board to check if I2C mux initialized."""
        import serial as ser
        try:
            s = ser.Serial(port, 115200, timeout=3)
            time.sleep(0.5)
            s.reset_input_buffer()
            # Read lines for a few seconds looking for I2C status
            deadline = time.time() + 5
            while time.time() < deadline:
                raw = s.readline().decode(errors="replace")
                if "I2C bus ready" in raw or "Polling channels" in raw:
                    s.close()
                    return True
                if "EEPROM init failed" in raw:
                    s.close()
                    return False
            s.close()
            # If we didn't see either message, try a reset and read again
            return False
        except Exception as e:
            self.after(0, self._log, f"Serial check error: {e}\n", "err")
            return False

    def _open_vscode(self):
        try:
            subprocess.Popen(["code", PROJECT_ROOT],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self._log("Opened VS Code\n", "ok")
        except FileNotFoundError:
            self._log("VS Code 'code' command not found\n", "err")

    def _open_monitor(self, port, project, name):
        project_dir = os.path.join(PROJECT_ROOT, project)
        export_script = os.path.join(IDF_PATH, "export.sh")
        cmd = f'source "{export_script}" && cd "{project_dir}" && idf.py -p {port} monitor'

        terminals = [
            ["gnome-terminal", "--title", f"Bloco {name} Monitor", "--",
             "bash", "-c", f'{cmd}; read -p "Press Enter to close..."'],
            ["x-terminal-emulator", "-e",
             f"bash -c '{cmd}; read -p \"Press Enter to close...\"'"],
        ]
        for term_cmd in terminals:
            try:
                subprocess.Popen(term_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                self._log(f"Opened {name} monitor ({port})\n", "ok")
                return
            except FileNotFoundError:
                continue
        self._log(f"Could not open terminal for {name} monitor\n", "err")

    def _open_board_gui(self):
        tool = os.path.join(PROJECT_ROOT, "board", "tools", "board_monitor.py")
        try:
            subprocess.Popen([sys.executable, tool],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self._log("Launched Board Monitor GUI\n", "ok")
        except Exception as e:
            self._log(f"Failed to launch Board Monitor GUI: {e}\n", "err")

    def _open_gui(self):
        if self.mode == "robo":
            tool = os.path.join(PROJECT_ROOT, "robo", "tools", "robo_sim.py")
            name = "Robot Simulator"
        else:
            tool = os.path.join(PROJECT_ROOT, "block", "tools", "block_gui.py")
            name = "Block Programmer GUI"
        try:
            subprocess.Popen([sys.executable, tool],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self._log(f"Launched {name}\n", "ok")
        except Exception as e:
            self._log(f"Failed to launch {name}: {e}\n", "err")


def main():
    app = Launchpad()
    app.mainloop()


if __name__ == "__main__":
    main()
