#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "block_types.h"
#include "espnow_protocol.h"
#include "motor.h"
#include "display.h"
#include "eyes.h"
#include "executor.h"

static const char *TAG = "robo";

// Received program buffer
static block_data_t rx_blocks[ESPNOW_MAX_BLOCKS];
static uint8_t rx_count = 0;
static uint8_t rx_expected = 0;
static bool rx_in_progress = false;

// Signal to executor task
static SemaphoreHandle_t program_ready_sem;
static block_data_t exec_blocks[ESPNOW_MAX_BLOCKS];
static uint8_t exec_count = 0;

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < 1) return;

    uint8_t msg_type = data[0];

    switch (msg_type) {
    case MSG_PROGRAM_START: {
        if (len < (int)sizeof(espnow_program_start_t)) return;
        const espnow_program_start_t *msg = (const espnow_program_start_t *)data;

        rx_expected = msg->block_count;
        rx_count = 0;
        rx_in_progress = true;

        if (rx_expected > ESPNOW_MAX_BLOCKS) {
            ESP_LOGW(TAG, "Program too large (%d blocks), capping to %d", rx_expected, ESPNOW_MAX_BLOCKS);
            rx_expected = ESPNOW_MAX_BLOCKS;
        }

        ESP_LOGI(TAG, "<<< Program start: expecting %d blocks >>>", rx_expected);
        break;
    }

    case MSG_BLOCK_DATA: {
        if (!rx_in_progress) return;
        if (len < (int)sizeof(espnow_block_msg_t)) return;
        const espnow_block_msg_t *msg = (const espnow_block_msg_t *)data;

        if (msg->index < ESPNOW_MAX_BLOCKS) {
            memcpy(&rx_blocks[msg->index], &msg->block, sizeof(block_data_t));
            rx_count++;
            ESP_LOGI(TAG, "  Received block %d: type=0x%02X name=%.15s",
                     msg->index, msg->block.type, msg->block.name);
        }
        break;
    }

    case MSG_PROGRAM_END: {
        if (!rx_in_progress) return;
        rx_in_progress = false;

        ESP_LOGI(TAG, "<<< Program end: got %d/%d blocks >>>", rx_count, rx_expected);

        if (rx_count >= rx_expected) {
            // Copy to execution buffer and signal
            memcpy(exec_blocks, rx_blocks, sizeof(block_data_t) * rx_expected);
            exec_count = rx_expected;
            xSemaphoreGive(program_ready_sem);
        } else {
            ESP_LOGW(TAG, "Incomplete program — discarding");
        }
        break;
    }

    default:
        ESP_LOGW(TAG, "Unknown ESP-NOW message type: 0x%02X", msg_type);
        break;
    }
}

static void wifi_espnow_init(void)
{
    // NVS required by WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // WiFi init (STA mode, no connect — just for ESP-NOW)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // ESP-NOW init
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    ESP_LOGI(TAG, "ESP-NOW receiver initialized");
}

static void executor_task(void *arg)
{
    while (1) {
        // Wait for a complete program
        if (xSemaphoreTake(program_ready_sem, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, ">>> Executing received program (%d blocks) <<<", exec_count);
            executor_run(exec_blocks, exec_count);
            ESP_LOGI(TAG, ">>> Execution complete <<<");
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Bloco Robot ===");

    // Create semaphore
    program_ready_sem = xSemaphoreCreateBinary();

    // Init motors
    ESP_ERROR_CHECK(motor_init());

    // Init display + eyes
    ESP_ERROR_CHECK(display_init());
    eyes_init();

    // Init WiFi + ESP-NOW receiver
    wifi_espnow_init();

    // Start executor task
    xTaskCreate(executor_task, "executor", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Ready — waiting for program via ESP-NOW...");

    // Main task has nothing else to do
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
