#include "eeprom.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "eeprom";

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t mux_handle;
static i2c_master_dev_handle_t eeprom_handle;

esp_err_t eeprom_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = EEPROM_I2C_PORT,
        .sda_io_num = EEPROM_I2C_SDA,
        .scl_io_num = EEPROM_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add PCA9548A mux device
    i2c_device_config_t mux_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MUX_ADDR,
        .scl_speed_hz = EEPROM_I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(bus_handle, &mux_cfg, &mux_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add mux device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add AT24C256 EEPROM device
    i2c_device_config_t eeprom_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = EEPROM_ADDR,
        .scl_speed_hz = EEPROM_I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(bus_handle, &eeprom_cfg, &eeprom_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add EEPROM device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Select programming slot channel
    ret = eeprom_select_channel(MUX_CHANNEL_PROG);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select mux channel: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t eeprom_select_channel(uint8_t channel)
{
    uint8_t mask = 1 << channel;
    return i2c_master_transmit(mux_handle, &mask, 1, 100);
}

bool eeprom_is_present(uint8_t channel)
{
    if (eeprom_select_channel(channel) != ESP_OK) {
        return false;
    }
    uint8_t dummy = 0;
    return i2c_master_transmit(eeprom_handle, &dummy, 1, 50) == ESP_OK;
}

esp_err_t eeprom_write(uint16_t addr, const uint8_t *data, size_t len)
{
    // Write in page-aligned chunks to respect AT24C256 page boundaries
    while (len > 0) {
        // Bytes remaining in current page
        size_t page_remain = EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE);
        size_t chunk = (len < page_remain) ? len : page_remain;

        // Build write buffer: 2-byte address + data
        uint8_t buf[2 + EEPROM_PAGE_SIZE];
        buf[0] = (addr >> 8) & 0xFF;
        buf[1] = addr & 0xFF;
        memcpy(&buf[2], data, chunk);

        esp_err_t ret = i2c_master_transmit(eeprom_handle, buf, 2 + chunk, 100);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Write failed at 0x%04X: %s", addr, esp_err_to_name(ret));
            return ret;
        }

        vTaskDelay(pdMS_TO_TICKS(EEPROM_WRITE_TIME_MS));

        addr += chunk;
        data += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

esp_err_t eeprom_read(uint16_t addr, uint8_t *data, size_t len)
{
    uint8_t addr_buf[2] = { (addr >> 8) & 0xFF, addr & 0xFF };
    return i2c_master_transmit_receive(eeprom_handle, addr_buf, 2, data, len, 100);
}

esp_err_t eeprom_erase(uint16_t addr, size_t len)
{
    uint8_t ff_buf[EEPROM_PAGE_SIZE];
    memset(ff_buf, 0xFF, sizeof(ff_buf));

    while (len > 0) {
        size_t page_remain = EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE);
        size_t chunk = (len < page_remain) ? len : page_remain;

        esp_err_t ret = eeprom_write(addr, ff_buf, chunk);
        if (ret != ESP_OK) return ret;

        addr += chunk;
        len -= chunk;
    }
    return ESP_OK;
}
