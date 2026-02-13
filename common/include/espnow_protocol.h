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
#define MSG_PROGRAM_ACK       0x04
#define MSG_PAIR_REQUEST      0x10
#define MSG_PAIR_ACK          0x11
#define MSG_UNPAIR            0x12

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

// Robot → Board: "I received the complete program"
typedef struct {
    uint8_t msg_type;       // MSG_PROGRAM_ACK
    uint8_t block_count;    // how many blocks were received
} __attribute__((packed)) espnow_program_ack_t;

// Board → Robot: "I want to pair with you"
typedef struct {
    uint8_t msg_type;       // MSG_PAIR_REQUEST
    uint8_t mac[6];         // sender's MAC
} __attribute__((packed)) espnow_pair_request_t;

// Robot → Board: "Pairing accepted"
typedef struct {
    uint8_t msg_type;       // MSG_PAIR_ACK
    uint8_t mac[6];         // sender's MAC
} __attribute__((packed)) espnow_pair_ack_t;

// Either direction: "I'm unpairing from you"
typedef struct {
    uint8_t msg_type;       // MSG_UNPAIR
} __attribute__((packed)) espnow_unpair_t;
