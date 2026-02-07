#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Hardware config
#define EEPROM_I2C_PORT       0
#define EEPROM_I2C_SDA        8
#define EEPROM_I2C_SCL        9
#define EEPROM_I2C_FREQ_HZ    100000

#define MUX_ADDR              0x70   // PCA9548A address
#define EEPROM_ADDR           0x50   // AT24C256 address
#define MUX_CHANNEL_PROG      0      // Programming slot channel

#define EEPROM_PAGE_SIZE      64     // AT24C256 page size
#define EEPROM_WRITE_TIME_MS  10     // Write cycle time

esp_err_t eeprom_init(void);
esp_err_t eeprom_select_channel(uint8_t channel);
bool      eeprom_is_present(uint8_t channel);
esp_err_t eeprom_write(uint16_t addr, const uint8_t *data, size_t len);
esp_err_t eeprom_read(uint16_t addr, uint8_t *data, size_t len);
esp_err_t eeprom_erase(uint16_t addr, size_t len);
