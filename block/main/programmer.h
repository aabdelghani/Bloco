#pragma once

#include "block_types.h"
#include "esp_err.h"

#define PROGRAMMER_MAX_CHANNEL 7

void      programmer_init(void);
esp_err_t programmer_write_block(uint8_t channel, uint8_t type, uint8_t subtype,
                                 uint8_t param1, uint8_t param2,
                                 const char *name, block_data_t *out);
esp_err_t programmer_read_block(uint8_t channel, block_data_t *out);
esp_err_t programmer_erase_block(uint8_t channel);
esp_err_t programmer_verify_block(uint8_t channel, const block_data_t *expected);
