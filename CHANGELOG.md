# Changelog

All notable changes to this project will be documented in this file.

## [0.4.1] - 2026-02-13

### Added
- **Motor test tool** (`tools/motor_test/`) — Standalone ESP-IDF project for testing H-bridge motor wiring. Drives two DC motors forward for 3 seconds, then stops. Configurable GPIOs and speed via `#define`.
- **Board Monitor GUI in Launchpad** (`tools/launchpad.py`) — Step 4 now includes a "Launch Board Monitor GUI" checkbox that opens `board_monitor.py` alongside the other tools.
- **5 new eye expressions** — SCARED (wide open, tiny pupils), CRYING (squinted with animated blue tear drops), CRYING_NO_TEARS (same shape, no tears), SWEATING (slight tilt with blue sweat drop), DIZZY (X-shaped eyes). New block types `0x8D`–`0x91` added across firmware, board monitor, and robot simulator.
- **Eye style selector in Launchpad** (`tools/launchpad.py`) — "Robot Build Options" section in Step 4 with an "Eyes with pupils" checkbox. Writes the Kconfig choice to `robo/sdkconfig` before building, so users can switch eye styles without running `menuconfig`.

### Changed
- `tools/launchpad.py` — Window resized to 700x750 and made resizable to fit all Step 4 options.

### Known Issues
- **ESP-NOW transmission not verified** — Programs are sent from board to robot via broadcast with no acknowledgment. Lost packets result in incomplete programs on the robot with no error reported. See [#2](https://github.com/aabdelghani/Bloco/issues/2).
- **Simulator send requires I2C tab connection** — The Board Monitor Simulator tab shares the serial connection from the I2C Blocks tab. Users must connect via the I2C tab before sending from the Simulator. If the board is not flashed with the debug build (`CONFIG_BOARD_SERIAL_CMD`), the `SEND_BLOCKS` command is not available and sends will fail silently.

## [0.4.0] - 2026-02-12

### Added
- **Block Simulator tab** (`board/tools/board_monitor.py`) — Drag-and-drop block composer in the Board Monitor GUI. Click palette blocks to build a program, drag to reorder, right-click to remove, and send directly to the robot via serial.
- **`SEND_BLOCKS` serial command** (`board/main/main.c`) — New debug-mode serial command that accepts a JSON block array and relays it to the robot via ESP-NOW, enabling wireless program upload without physical EEPROM blocks.
- **Eye block types** (`common/include/block_types.h`) — 13 new block types for controlling robot eye expressions (`0x80`–`0x87`: NORMAL, HAPPY, SAD, ANGRY, SURPRISED, SLEEPING, EXCITED, FOCUSED) and look direction (`0x88`–`0x8C`: CENTER, LEFT, RIGHT, UP, DOWN).
- **Device role auto-detection** — Board and robot firmware write their role ("board"/"robo") to NVS on boot and print `DEVICE_ROLE=` at startup. Python tools auto-detect the correct serial port by scanning for this marker.
- **Robot eye style configuration** (`robo/main/Kconfig.projbuild`) — Compile-time `menuconfig` choice between "Solid (no pupils)" and "With pupils" eye rendering styles. Solid style is the default.

### Changed
- `robo/main/eyes.c` — New solid eye style with shape-only emotion (no pupils). Expressions tuned for more expressive shapes: wider happy squeeze, sharper angry tilt, bigger surprised circles. Angry expression made more intense (wider, heavier lid, steeper tilt).
- `robo/main/executor.c` — Eye block handlers with 1-second display delay so expressions are visible during execution.
- `board/main/main.c` — Refactored `send_program_to_robot()` to accept block array parameters instead of reading from global EEPROM data. UART buffer increased to 1024 bytes for JSON commands.
- `board/main/CMakeLists.txt` — Added `json` component for cJSON support.
- `board/tools/board_monitor.py` — Added eye block types to block palette, device role auto-detection.
- `robo/tools/robo_sim.py` — Added eye block types to block names/colors, device role auto-detection, executor eye log highlighting.

## [0.3.0] - 2026-02-12

### Added
- **GC9A01 round LCD display** — 1.28" 240x240 RGB565 display on the robot, driven over SPI at 40 MHz with DMA.
- **Cozmo-style animated eyes** (`robo/main/eyes.c/h`) — 8 expression presets (NORMAL, HAPPY, SAD, ANGRY, SURPRISED, SLEEPING, EXCITED, FOCUSED) with smooth 250ms transitions, auto-blink every 2–6 seconds, and directional pupil tracking.
- **Idle sleep mode** — Eyes transition to SLEEPING after 1 minute of inactivity, wake instantly on new program. Timeout configurable via `EYES_IDLE_TIMEOUT_MS`.
- **Display driver** (`robo/main/display.c/h`) — Low-level GC9A01 SPI driver with band-buffer rendering (240x30 strips, ~14KB RAM), LEDC backlight PWM on TIMER_1/CH2 (avoids motor conflict).
- **App icons** — Custom 64x64 PNG taskbar icons for all GUI tools (PCB chip, robot face, colored blocks, rocket).
- `robo/main/idf_component.yml` — Declares `espressif/esp_lcd_gc9a01 ^2.0.0` dependency.

### Changed
- `robo/main/executor.c` — Block execution now sets eye expressions and look direction (FORWARD→focused+up, turns→look L/R, SHAKE→excited, sounds→happy, etc.).
- `robo/main/main.c` — Calls `display_init()` and `eyes_init()` on boot.
- `robo/main/CMakeLists.txt` — Added `display.c`, `eyes.c` to SRCS; `esp_lcd`, `esp_driver_spi` to REQUIRES.
- All GUI tools now set `className` and `iconphoto` for proper Linux taskbar display.

### Fixed
- "Blocko" typo → "Bloco" in block_gui.py window title.

## [0.2.0] - 2026-02-12

### Added
- **Board Monitor GUI** (`board/tools/board_monitor.py`) — Tkinter tool for scanning I2C block slots, displaying block data as color-coded cards, and triggering ESP-NOW sends to the robot.
- **Launchpad GUI** (`tools/launchpad.py`) — 4-step development wizard for connecting devices, choosing workflow mode (Robot / Block Programmer / Board Only), flashing firmware, and launching tools. Verifies I2C mux (PCA9548A) connectivity after board flash.
- **Robot Simulator GUI** (`robo/tools/robo_sim.py`) — Tkinter tool for flashing and monitoring the robot firmware with visual block display and serial log.
- **Conditional debug serial handler** — Board serial JSON command handler (`SCAN_CHANNELS`, `SEND_TO_ROBOT`, `GET_STATUS`) is now behind `CONFIG_BOARD_SERIAL_CMD` Kconfig option, disabled by default in release builds.
- `board/main/Kconfig.projbuild` — Custom Kconfig menu for board build options.
- `board/sdkconfig.defaults.debug` — Debug build defaults enabling serial command handler.

### Changed
- `board/main/main.c` — Serial command handler wrapped in `#ifdef CONFIG_BOARD_SERIAL_CMD`.
- `.gitignore` — Added `build_*/` pattern to exclude debug/release build directories.

## [0.1.0] - 2026-02-07

### Added
- **Block Agent firmware** (`block/`) — Programs block data onto AT24C256 EEPROMs via I2C. Accepts JSON commands over UART. WS2812 LED status feedback.
- **Board Reader firmware** (`board/`) — Polls I2C mux channels for EEPROM blocks, sends programs to robot via ESP-NOW broadcast.
- **Robot Controller firmware** (`robo/`) — Receives block programs via ESP-NOW, interprets and executes them using 2 DC motors (differential drive with LEDC PWM).
- **Common library** (`common/`) — Shared ESP-IDF component with block type definitions (40+ block types across 9 categories), EEPROM/I2C mux driver, and ESP-NOW protocol structs.
- **Block GUI** (`block/tools/block_gui.py`) — Python/Tkinter GUI for programming blocks with Flash and Program tabs.
- **ESP-NOW protocol** — 3-message sequence (PROGRAM_START, BLOCK_DATA, PROGRAM_END) for wireless board-to-robot communication.
