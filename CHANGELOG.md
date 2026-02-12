# Changelog

All notable changes to this project will be documented in this file.

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
