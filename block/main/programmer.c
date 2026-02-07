#include "programmer.h"
#include "eeprom.h"
#include "led.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "programmer";

static uint8_t  mac_bytes[2];
static uint16_t serial_counter;

void programmer_init(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BASE);
    mac_bytes[0] = mac[4];
    mac_bytes[1] = mac[5];
    serial_counter = 0;
    ESP_LOGI(TAG, "Serial prefix: %02X%02X", mac_bytes[0], mac_bytes[1]);
}

static void generate_serial(uint8_t serial[4])
{
    serial[0] = mac_bytes[0];
    serial[1] = mac_bytes[1];
    serial[2] = (serial_counter >> 8) & 0xFF;
    serial[3] = serial_counter & 0xFF;
    serial_counter++;
}

static esp_err_t select_channel(uint8_t channel)
{
    if (channel > PROGRAMMER_MAX_CHANNEL) {
        ESP_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    return eeprom_select_channel(channel);
}

esp_err_t programmer_write_block(uint8_t channel, uint8_t type, uint8_t subtype,
                                 uint8_t param1, uint8_t param2,
                                 const char *name, block_data_t *out)
{
    if (!block_type_valid(type)) {
        ESP_LOGE(TAG, "Invalid block type: 0x%02X", type);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = select_channel(channel);
    if (ret != ESP_OK) return ret;

    led_set_state(LED_STATE_PROGRAMMING);

    block_data_t blk = {0};
    blk.type = type;
    blk.subtype = subtype;
    blk.param1 = param1;
    blk.param2 = param2;
    generate_serial(blk.serial);
    blk.version = BLOCK_VERSION;

    if (name) {
        strncpy(blk.name, name, BLOCK_NAME_MAX_LEN - 1);
        blk.name[BLOCK_NAME_MAX_LEN - 1] = '\0';
    }

    blk.checksum = block_calc_checksum(&blk);

    ret = eeprom_write(0x0000, (const uint8_t *)&blk, BLOCK_DATA_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EEPROM write failed on ch%d", channel);
        led_set_state(LED_STATE_ERROR);
        return ret;
    }

    block_data_t verify = {0};
    ret = eeprom_read(0x0000, (uint8_t *)&verify, BLOCK_DATA_SIZE);
    if (ret != ESP_OK || memcmp(&blk, &verify, BLOCK_DATA_SIZE) != 0) {
        ESP_LOGE(TAG, "Write verification failed on ch%d", channel);
        led_set_state(LED_STATE_ERROR);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "Block written on ch%d: type=0x%02X serial=%02X%02X%02X%02X name=%s",
             channel, blk.type,
             blk.serial[0], blk.serial[1], blk.serial[2], blk.serial[3],
             blk.name);

    led_set_state(LED_STATE_SUCCESS);

    if (out) {
        *out = blk;
    }
    return ESP_OK;
}

esp_err_t programmer_read_block(uint8_t channel, block_data_t *out)
{
    esp_err_t ret = select_channel(channel);
    if (ret != ESP_OK) return ret;

    ret = eeprom_read(0x0000, (uint8_t *)out, BLOCK_DATA_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EEPROM read failed on ch%d", channel);
        return ret;
    }

    uint8_t expected_cksum = block_calc_checksum(out);
    if (out->checksum != expected_cksum) {
        ESP_LOGW(TAG, "Checksum mismatch on ch%d: got 0x%02X expected 0x%02X",
                 channel, out->checksum, expected_cksum);
    }
    return ESP_OK;
}

esp_err_t programmer_erase_block(uint8_t channel)
{
    esp_err_t ret = select_channel(channel);
    if (ret != ESP_OK) return ret;

    led_set_state(LED_STATE_PROGRAMMING);

    ret = eeprom_erase(0x0000, BLOCK_DATA_SIZE);
    if (ret != ESP_OK) {
        led_set_state(LED_STATE_ERROR);
        return ret;
    }

    uint8_t buf[BLOCK_DATA_SIZE];
    ret = eeprom_read(0x0000, buf, BLOCK_DATA_SIZE);
    if (ret != ESP_OK) {
        led_set_state(LED_STATE_ERROR);
        return ret;
    }
    for (int i = 0; i < BLOCK_DATA_SIZE; i++) {
        if (buf[i] != 0xFF) {
            ESP_LOGE(TAG, "Erase verification failed on ch%d at byte %d", channel, i);
            led_set_state(LED_STATE_ERROR);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    ESP_LOGI(TAG, "Block erased on ch%d", channel);
    led_set_state(LED_STATE_SUCCESS);
    return ESP_OK;
}

esp_err_t programmer_verify_block(uint8_t channel, const block_data_t *expected)
{
    esp_err_t ret = select_channel(channel);
    if (ret != ESP_OK) return ret;

    block_data_t actual = {0};
    ret = eeprom_read(0x0000, (uint8_t *)&actual, BLOCK_DATA_SIZE);
    if (ret != ESP_OK) return ret;

    if (memcmp(expected, &actual, BLOCK_DATA_SIZE) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
