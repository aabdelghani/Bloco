#pragma once

#include "esp_err.h"
#include "block_types.h"

#define BLOCK_UNKNOWN 0x00

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the block classifier (load TFLite Micro model)
esp_err_t classifier_init(void);

// Identify a block from an RGB565 image crop
// Returns the block type, or BLOCK_UNKNOWN (0x00) if unrecognized
uint8_t classifier_identify(const uint8_t *img, int w, int h);

#ifdef __cplusplus
}
#endif
