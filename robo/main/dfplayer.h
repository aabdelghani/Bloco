#pragma once

#include "esp_err.h"
#include <stdint.h>

// SD card folder layout (numbered folders on FAT32 SD card)
#define DF_FOLDER_IDLE     1   // 01/001-004.mp3
#define DF_FOLDER_HAPPY    2   // 02/001-002.mp3
#define DF_FOLDER_EXCITED  3   // 03/001-003.mp3
#define DF_FOLDER_MISC     4   // 04/001-006.mp3

// Track counts per folder
#define DF_IDLE_COUNT      4
#define DF_HAPPY_COUNT     2
#define DF_EXCITED_COUNT   3

// Misc folder track indices
#define DF_MISC_SCARED       1
#define DF_MISC_SAD          2
#define DF_MISC_CONNECTED    3
#define DF_MISC_DISCONNECT   4
#define DF_MISC_END          5
#define DF_MISC_BEEP         6

// Initialize DFPlayer Mini on UART1 (GPIO 17 TX, GPIO 18 RX)
esp_err_t dfplayer_init(void);

// Set playback volume (0-30)
void dfplayer_set_volume(uint8_t vol);

// Play a specific track from a specific folder
void dfplayer_play(uint8_t folder, uint8_t track);

// Stop playback
void dfplayer_stop(void);
