# Bloco

A physical block-based programming system for kids, built on ESP32-S3 microcontrollers. Users program logical instruction blocks onto EEPROM chips, plug them into a reader board, and wirelessly send the program to a robot that executes movement, sound, and light commands.

The project is split into three independent firmware projects and a shared library:

| Directory | Description | Build Target |
|-----------|-------------|--------------|
| `block/`  | Block Agent — programs blocks onto EEPROMs via I2C | `block` |
| `board/`  | Board Reader — reads EEPROM blocks, sends program to robot via ESP-NOW | `board` |
| `robo/`   | Robot Controller — receives program via ESP-NOW, drives 2 DC motors | `robo` |
| `common/` | Shared ESP-IDF component (block types, EEPROM driver, ESP-NOW protocol) | library |

## Hardware

- **MCU:** ESP32-S3 (all three boards)
- **EEPROM:** AT24C256 (I2C address `0x50`, 32KB per chip)
- **I2C Mux:** PCA9548A (address `0x70`, up to 8 channels)
- **Status LED:** WS2812 RGB on GPIO 48 (block agent)
- **I2C Bus:** SDA=GPIO 8, SCL=GPIO 9, 100 kHz
- **Robot Motors:** 2x DC motors (L/R differential drive), H-bridge on GPIOs 4/5/6/7 + PWM on 15/16
- **Send Button:** GPIO 0 (BOOT) on the board — press to send program to robot

## Prerequisites

- [ESP-IDF v5.5.2](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/get-started/)
- Python 3 (for the Block GUI tool and ESP-IDF build system)
- ESP32-S3 development boards (x3)

## Build & Flash

Each project is built independently from its own directory.

### Block Agent

```bash
cd block
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### Board Reader

```bash
cd board
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### Robot Controller

```bash
cd robo
idf.py set-target esp32s3   # first time only
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### Board Reader (Debug Build)

The board supports an optional serial JSON command handler for use with the board monitor GUI. To enable it, build with the debug config:

```bash
cd board
idf.py -B build_debug -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.debug" build
idf.py -B build_debug -p /dev/ttyACM0 flash monitor
```

Release builds (default `idf.py build`) exclude the serial handler for a smaller footprint.

## Development Tools

All GUI tools require Python 3 and `pyserial`:

```bash
pip install pyserial
```

### Launchpad (`tools/launchpad.py`)

A 4-step development wizard that guides you through connecting devices, flashing firmware, and launching the appropriate tools. Supports Board+Robot, Board+Block Programmer, and Board-only workflows. Verifies I2C mux connectivity after flashing the board.

```bash
python3 tools/launchpad.py
```

### Block GUI (`block/tools/block_gui.py`)

Tkinter GUI for programming blocks onto EEPROMs via serial. Includes a Flash tab that auto-builds and flashes the block firmware.

```bash
python3 block/tools/block_gui.py
```

### Board Monitor (`board/tools/board_monitor.py`)

Tkinter GUI for inspecting I2C block slots on the board. Requires the board to be flashed with the debug build (CONFIG_BOARD_SERIAL_CMD enabled). Scans slots, displays block data with color-coded cards, and can trigger ESP-NOW sends to the robot.

```bash
python3 board/tools/board_monitor.py
```

### Robot Simulator (`robo/tools/robo_sim.py`)

Tkinter GUI for flashing and monitoring the robot firmware. Displays received block programs, tracks execution progress, and shows color-coded serial logs.

```bash
python3 robo/tools/robo_sim.py
```

## Project Structure

```
Bloco/
├── common/                     Shared ESP-IDF component
│   ├── include/
│   │   ├── block_types.h       Block type IDs, data structure, checksum
│   │   ├── eeprom.h            EEPROM + I2C mux driver API
│   │   └── espnow_protocol.h   ESP-NOW message format (board <-> robo)
│   └── src/
│       └── eeprom.c            I2C driver (AT24C256 + PCA9548A mux)
│
├── block/                      Block Agent firmware
│   ├── main/
│   │   ├── main.c              Entry point, FreeRTOS task creation
│   │   ├── command.c/h         UART JSON command handler
│   │   ├── programmer.c/h      Block write/read/erase/verify logic
│   │   └── led.c/h             WS2812 LED status feedback
│   └── tools/
│       └── block_gui.py        Python GUI for block programming
│
├── board/                      Board Reader firmware
│   ├── main/
│   │   ├── main.c              EEPROM polling + ESP-NOW sender
│   │   └── Kconfig.projbuild   Debug build options (serial cmd handler)
│   └── tools/
│       └── board_monitor.py    GUI for inspecting I2C block slots
│
├── robo/                       Robot Controller firmware
│   ├── main/
│   │   ├── main.c              ESP-NOW receiver + task orchestration
│   │   ├── motor.c/h           2-motor differential drive (LEDC PWM)
│   │   └── executor.c/h        Block program interpreter
│   └── tools/
│       └── robo_sim.py         GUI for flashing and monitoring robot
│
└── tools/
    └── launchpad.py            Development wizard (connect, flash, launch)
```

## Block Types

Blocks are 32-byte data structures stored on EEPROM. Action blocks perform operations; parameter blocks modify the preceding action.

| Category     | Blocks | IDs |
|--------------|--------|-----|
| Actions      | BEGIN, END | `0x01`–`0x02` |
| Movement     | FORWARD, BACKWARD, TURN_RIGHT, TURN_LEFT, SHAKE, SPIN | `0x10`–`0x15` |
| Control Flow | REPEAT, END_REPEAT, IF, END_IF | `0x20`–`0x23` |
| Sound        | BEEP, SING, PLAY_TRIANGLE, PLAY_CIRCLE, PLAY_SQUARE | `0x30`–`0x34` |
| Light        | WHITE_LIGHT_ON, RED_LIGHT_ON, BLUE_LIGHT_ON | `0x40`–`0x42` |
| Wait         | WAIT_FOR_CLAP | `0x50` |
| Parameters   | 2, 3, 4, FOREVER, LIGHT, DARK, NEAR, FAR, UNTIL_LIGHT, UNTIL_DARK, UNTIL_NEAR, UNTIL_FAR | `0x60`–`0x6B` |
| Sensors      | LIGHT_BULB, EAR, EYE, TELESCOPE, SOUND_MODULE | `0x70`–`0x74` |

### EEPROM Data Layout (32 bytes)

```
Offset  Size  Field       Description
0x00    1     type        Block type ID
0x01    1     subtype     Sub-classification
0x02    1     param1      Parameter 1
0x03    1     param2      Parameter 2
0x04    4     serial      MAC-based unique serial number
0x08    1     version     Data format version (0x01)
0x09    1     checksum    XOR of bytes 0x00–0x08
0x0A    6     reserved    Reserved
0x10    16    name        Human-readable label (null-terminated)
```

## ESP-NOW Protocol

The board sends programs to the robot over ESP-NOW (channel 1, broadcast mode). Messages:

1. **PROGRAM_START** (2 bytes): `{msg_type=0x01, block_count}`
2. **BLOCK_DATA** (34 bytes each): `{msg_type=0x02, index, block_data[32]}`
3. **PROGRAM_END** (1 byte): `{msg_type=0x03}`

The robot auto-executes the program immediately upon receiving PROGRAM_END.

## Command Protocol

The Block Agent accepts newline-delimited JSON commands over UART (115200 baud):

```jsonc
// Write a block
{"cmd": "WRITE_BLOCK", "channel": 0, "type": 1, "subtype": 0, "param1": 0, "param2": 0, "name": "Start"}
// → {"response": "WRITE_OK", "type": 1, "serial": "AABBCCDD"}

// Read a block
{"cmd": "READ_BLOCK", "channel": 0}
// → {"response": "READ_DATA", "type": 1, "subtype": 0, ...}

// Erase a block
{"cmd": "ERASE_BLOCK", "channel": 0}
// → {"response": "ERASE_OK"}

// Verify checksum
{"cmd": "VERIFY_BLOCK", "channel": 0}
// → {"response": "VERIFY_OK", "match": true}

// Batch program
{"cmd": "BATCH_PROGRAM", "blocks": [{"type": 1, "name": "A", "channel": 0}, ...]}
```
