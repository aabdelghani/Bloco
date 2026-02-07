#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "eeprom.h"
#include "led.h"
#include "programmer.h"
#include "command.h"

static const char *TAG = "block_agent";

void app_main(void)
{
    ESP_LOGI(TAG, "Block Agent starting...");

    // Init LED
    led_init();
    xTaskCreate(led_task, "led", 2048, NULL, 2, NULL);

    // Init I2C + EEPROM
    esp_err_t ret = eeprom_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "EEPROM init failed: %s", esp_err_to_name(ret));
        led_set_state(LED_STATE_ERROR);
        return;
    }

    // Init programmer (MAC-based serial generation)
    programmer_init();

    // Start command handler on UART
    xTaskCreate(command_task, "cmd", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Block Agent ready");
}