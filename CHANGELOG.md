# Changelog

All notable changes to this project will be documented in this file.

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
