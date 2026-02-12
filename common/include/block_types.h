#pragma once

#include <stdbool.h>
#include <stdint.h>

// EEPROM data structure layout
#define BLOCK_ADDR_TYPE       0x0000
#define BLOCK_ADDR_SUBTYPE    0x0001
#define BLOCK_ADDR_PARAM1     0x0002
#define BLOCK_ADDR_PARAM2     0x0003
#define BLOCK_ADDR_SERIAL     0x0004  // 4 bytes (0x0004-0x0007)
#define BLOCK_ADDR_VERSION    0x0008
#define BLOCK_ADDR_CHECKSUM   0x0009
#define BLOCK_ADDR_RESERVED   0x000A  // 6 bytes (0x000A-0x000F)
#define BLOCK_ADDR_NAME       0x0010  // 16 bytes (0x0010-0x001F)

#define BLOCK_DATA_SIZE       0x0020  // Total: 32 bytes
#define BLOCK_NAME_MAX_LEN    16      // 15 chars + null
#define BLOCK_VERSION         0x01

// --- Block Type IDs ---

// Actions
#define BLOCK_BEGIN             0x01
#define BLOCK_END               0x02

// Movement
#define BLOCK_FORWARD           0x10
#define BLOCK_BACKWARD          0x11
#define BLOCK_TURN_RIGHT        0x12
#define BLOCK_TURN_LEFT         0x13
#define BLOCK_SHAKE             0x14
#define BLOCK_SPIN              0x15

// Control Flow
#define BLOCK_REPEAT            0x20
#define BLOCK_END_REPEAT        0x21
#define BLOCK_IF                0x22
#define BLOCK_END_IF            0x23

// Sound
#define BLOCK_BEEP              0x30
#define BLOCK_SING              0x31
#define BLOCK_PLAY_TRIANGLE     0x32
#define BLOCK_PLAY_CIRCLE       0x33
#define BLOCK_PLAY_SQUARE       0x34

// Light
#define BLOCK_WHITE_LIGHT_ON    0x40
#define BLOCK_RED_LIGHT_ON      0x41
#define BLOCK_BLUE_LIGHT_ON     0x42

// Wait
#define BLOCK_WAIT_FOR_CLAP     0x50

// Parameters (modifiers for preceding action block)
#define BLOCK_PARAM_2           0x60
#define BLOCK_PARAM_3           0x61
#define BLOCK_PARAM_4           0x62
#define BLOCK_PARAM_FOREVER     0x63
#define BLOCK_PARAM_LIGHT       0x64
#define BLOCK_PARAM_DARK        0x65
#define BLOCK_PARAM_NEAR        0x66
#define BLOCK_PARAM_FAR         0x67
#define BLOCK_PARAM_UNTIL_LIGHT 0x68
#define BLOCK_PARAM_UNTIL_DARK  0x69
#define BLOCK_PARAM_UNTIL_NEAR  0x6A
#define BLOCK_PARAM_UNTIL_FAR   0x6B

// Eyes (expressions)
#define BLOCK_EYES_NORMAL           0x80
#define BLOCK_EYES_HAPPY            0x81
#define BLOCK_EYES_SAD              0x82
#define BLOCK_EYES_ANGRY            0x83
#define BLOCK_EYES_SURPRISED        0x84
#define BLOCK_EYES_SLEEPING         0x85
#define BLOCK_EYES_EXCITED          0x86
#define BLOCK_EYES_FOCUSED          0x87

// Eyes (look direction)
#define BLOCK_EYES_LOOK_CENTER      0x88
#define BLOCK_EYES_LOOK_LEFT        0x89
#define BLOCK_EYES_LOOK_RIGHT       0x8A
#define BLOCK_EYES_LOOK_UP          0x8B
#define BLOCK_EYES_LOOK_DOWN        0x8C

// Sensors (hardware modules)
#define BLOCK_SENSOR_LIGHT_BULB     0x70
#define BLOCK_SENSOR_EAR            0x71
#define BLOCK_SENSOR_EYE            0x72
#define BLOCK_SENSOR_TELESCOPE      0x73
#define BLOCK_SENSOR_SOUND_MODULE   0x74

// Block data as stored in EEPROM
typedef struct {
    uint8_t  type;
    uint8_t  subtype;
    uint8_t  param1;
    uint8_t  param2;
    uint8_t  serial[4];
    uint8_t  version;
    uint8_t  checksum;
    uint8_t  reserved[6];
    char     name[BLOCK_NAME_MAX_LEN];
} __attribute__((packed)) block_data_t;

_Static_assert(sizeof(block_data_t) == BLOCK_DATA_SIZE, "block_data_t must be 32 bytes");

// Check if a type ID is valid
static inline bool block_type_valid(uint8_t type)
{
    switch (type) {
    // Actions
    case BLOCK_BEGIN: case BLOCK_END:
    // Movement
    case BLOCK_FORWARD: case BLOCK_BACKWARD:
    case BLOCK_TURN_RIGHT: case BLOCK_TURN_LEFT:
    case BLOCK_SHAKE: case BLOCK_SPIN:
    // Control Flow
    case BLOCK_REPEAT: case BLOCK_END_REPEAT:
    case BLOCK_IF: case BLOCK_END_IF:
    // Sound
    case BLOCK_BEEP: case BLOCK_SING:
    case BLOCK_PLAY_TRIANGLE: case BLOCK_PLAY_CIRCLE: case BLOCK_PLAY_SQUARE:
    // Light
    case BLOCK_WHITE_LIGHT_ON: case BLOCK_RED_LIGHT_ON: case BLOCK_BLUE_LIGHT_ON:
    // Wait
    case BLOCK_WAIT_FOR_CLAP:
    // Parameters
    case BLOCK_PARAM_2: case BLOCK_PARAM_3: case BLOCK_PARAM_4:
    case BLOCK_PARAM_FOREVER:
    case BLOCK_PARAM_LIGHT: case BLOCK_PARAM_DARK:
    case BLOCK_PARAM_NEAR: case BLOCK_PARAM_FAR:
    case BLOCK_PARAM_UNTIL_LIGHT: case BLOCK_PARAM_UNTIL_DARK:
    case BLOCK_PARAM_UNTIL_NEAR: case BLOCK_PARAM_UNTIL_FAR:
    // Eyes (expressions)
    case BLOCK_EYES_NORMAL: case BLOCK_EYES_HAPPY:
    case BLOCK_EYES_SAD: case BLOCK_EYES_ANGRY:
    case BLOCK_EYES_SURPRISED: case BLOCK_EYES_SLEEPING:
    case BLOCK_EYES_EXCITED: case BLOCK_EYES_FOCUSED:
    // Eyes (look direction)
    case BLOCK_EYES_LOOK_CENTER: case BLOCK_EYES_LOOK_LEFT:
    case BLOCK_EYES_LOOK_RIGHT: case BLOCK_EYES_LOOK_UP:
    case BLOCK_EYES_LOOK_DOWN:
    // Sensors
    case BLOCK_SENSOR_LIGHT_BULB: case BLOCK_SENSOR_EAR:
    case BLOCK_SENSOR_EYE: case BLOCK_SENSOR_TELESCOPE:
    case BLOCK_SENSOR_SOUND_MODULE:
        return true;
    default:
        return false;
    }
}

// Calculate XOR checksum of bytes 0x00-0x08
static inline uint8_t block_calc_checksum(const block_data_t *b)
{
    const uint8_t *raw = (const uint8_t *)b;
    uint8_t cksum = 0;
    for (int i = 0; i <= 8; i++) {
        cksum ^= raw[i];
    }
    return cksum;
}
