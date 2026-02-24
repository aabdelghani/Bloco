#include "dfplayer.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dfplayer";

#define DFPLAYER_UART      UART_NUM_1
#define DFPLAYER_TX_GPIO   17
#define DFPLAYER_RX_GPIO   18
#define DFPLAYER_BAUD      9600
#define DFPLAYER_BUF_SIZE  256

// DFPlayer serial frame bytes
#define DF_START   0x7E
#define DF_VER     0xFF
#define DF_LEN     0x06
#define DF_END     0xEF

// DFPlayer command codes
#define CMD_PLAY_FOLDER  0x0F   // Play folder/track (param: folder << 8 | track)
#define CMD_SET_VOLUME   0x06   // Set volume (param: 0-30)
#define CMD_STOP         0x16   // Stop playback
#define CMD_RESET        0x0C   // Reset module
#define CMD_SELECT_TF    0x09   // Select TF card (param: 0x0002)

static void send_cmd(uint8_t cmd, uint8_t param_h, uint8_t param_l)
{
    // Checksum = -(VER + LEN + CMD + FEEDBACK + PARAM_H + PARAM_L)
    int16_t cksum = -(int16_t)(DF_VER + DF_LEN + cmd + 0x00 + param_h + param_l);

    uint8_t frame[10] = {
        DF_START,
        DF_VER,
        DF_LEN,
        cmd,
        0x00,                   // no feedback
        param_h,
        param_l,
        (uint8_t)(cksum >> 8),  // checksum high
        (uint8_t)(cksum),       // checksum low
        DF_END,
    };

    uart_write_bytes(DFPLAYER_UART, frame, sizeof(frame));
    // DFPlayer needs time between commands
    vTaskDelay(pdMS_TO_TICKS(100));
}

esp_err_t dfplayer_init(void)
{
    uart_config_t uart_cfg = {
        .baud_rate  = DFPLAYER_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(DFPLAYER_UART, DFPLAYER_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(DFPLAYER_UART, &uart_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(DFPLAYER_UART, DFPLAYER_TX_GPIO, DFPLAYER_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for DFPlayer to power up
    vTaskDelay(pdMS_TO_TICKS(500));

    // Reset module
    send_cmd(CMD_RESET, 0x00, 0x00);
    vTaskDelay(pdMS_TO_TICKS(1000));  // reset takes longer

    // Select TF card
    send_cmd(CMD_SELECT_TF, 0x00, 0x02);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Set default volume (20 out of 30)
    send_cmd(CMD_SET_VOLUME, 0x00, 20);

    ESP_LOGI(TAG, "DFPlayer Mini initialized (UART%d, TX=GPIO%d, RX=GPIO%d)",
             DFPLAYER_UART, DFPLAYER_TX_GPIO, DFPLAYER_RX_GPIO);
    return ESP_OK;
}

void dfplayer_set_volume(uint8_t vol)
{
    if (vol > 30) vol = 30;
    send_cmd(CMD_SET_VOLUME, 0x00, vol);
    ESP_LOGD(TAG, "Volume set to %d", vol);
}

void dfplayer_play(uint8_t folder, uint8_t track)
{
    send_cmd(CMD_PLAY_FOLDER, folder, track);
    ESP_LOGI(TAG, "Play folder=%d track=%d", folder, track);
}

void dfplayer_stop(void)
{
    send_cmd(CMD_STOP, 0x00, 0x00);
    ESP_LOGD(TAG, "Stop");
}
