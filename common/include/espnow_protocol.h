#pragma once

#include "block_types.h"
#include <stdint.h>

// ESP-NOW operates on WiFi channel 1 by default
#define ESPNOW_CHANNEL        1

// Broadcast address (sends to all ESP-NOW peers)
#define ESPNOW_BROADCAST_MAC  {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

// Max blocks in a single program
#define ESPNOW_MAX_BLOCKS     8

// --- Message types ---
#define MSG_PROGRAM_START     0x01
#define MSG_BLOCK_DATA        0x02
#define MSG_PROGRAM_END       0x03

// --- Message structs (all packed, fit within ESP-NOW 250-byte limit) ---

// Sent first: announces how many blocks are coming
typedef struct {
    uint8_t msg_type;       // MSG_PROGRAM_START
    uint8_t block_count;    // Number of blocks to follow
} __attribute__((packed)) espnow_program_start_t;

// Sent once per block
typedef struct {
    uint8_t msg_type;       // MSG_BLOCK_DATA
    uint8_t index;          // Block index (0-based)
    block_data_t block;     // 32-byte block data
} __attribute__((packed)) espnow_block_msg_t;

// Sent last: signals the receiver to start executing
typedef struct {
    uint8_t msg_type;       // MSG_PROGRAM_END
} __attribute__((packed)) espnow_program_end_t;
