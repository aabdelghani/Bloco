#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "led_strip.h"
#include "block_types.h"
#include "espnow_protocol.h"
#include "motor.h"
#include "display.h"
#include "eyes.h"
#include "executor.h"

#define PAIR_BUTTON_GPIO  GPIO_NUM_0   // BOOT button
#define LED_GPIO          GPIO_NUM_48  // Onboard WS2812 LED
#define LONG_PRESS_US     4000000      // 4 seconds
#define PAIR_TIMEOUT_MS   30000        // 30 second pairing timeout

static const char *TAG = "robo";

// Paired board MAC (if set, only accept messages from this MAC)
static uint8_t paired_mac[6];
static bool has_paired_mac = false;

// Button long-press tracking
static volatile int64_t button_press_time = 0;
static volatile bool pairing_requested = false;

// Pairing state (shared between recv callback and main loop)
static volatile bool pairing_active = false;
static volatile bool pairing_success = false;

// Received program buffer
static block_data_t rx_blocks[ESPNOW_MAX_BLOCKS];
static uint8_t rx_count = 0;
static uint8_t rx_expected = 0;
static bool rx_in_progress = false;

// Signal to executor task
static SemaphoreHandle_t program_ready_sem;
static block_data_t exec_blocks[ESPNOW_MAX_BLOCKS];
static uint8_t exec_count = 0;

// LED strip handle
static led_strip_handle_t led_strip;

static void led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

static void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
}

static void led_off(void)
{
    led_strip_clear(led_strip);
}

static void IRAM_ATTR button_isr(void *arg)
{
    int level = gpio_get_level(PAIR_BUTTON_GPIO);
    if (level == 0) {
        button_press_time = esp_timer_get_time();
    } else {
        int64_t held = esp_timer_get_time() - button_press_time;
        if (held >= LONG_PRESS_US) {
            pairing_requested = true;
        }
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < 1) return;

    uint8_t msg_type = data[0];

    // Handle pair requests regardless of MAC filtering
    if (msg_type == MSG_PAIR_REQUEST) {
        if (!pairing_active) return;
        if (len < (int)sizeof(espnow_pair_request_t)) return;
        const espnow_pair_request_t *req = (const espnow_pair_request_t *)data;

        ESP_LOGI(TAG, "Pair request from %02X:%02X:%02X:%02X:%02X:%02X",
                 req->mac[0], req->mac[1], req->mac[2],
                 req->mac[3], req->mac[4], req->mac[5]);

        // Store the board's MAC as our paired MAC
        memcpy(paired_mac, req->mac, 6);
        has_paired_mac = true;

        // Add the board as a peer so we can send the ACK
        esp_now_peer_info_t peer = {
            .channel = ESPNOW_CHANNEL,
            .encrypt = false,
        };
        memcpy(peer.peer_addr, req->mac, 6);
        esp_now_add_peer(&peer);  // OK if already exists

        // Send ACK with our own MAC
        uint8_t my_mac[6];
        esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
        espnow_pair_ack_t ack = {
            .msg_type = MSG_PAIR_ACK,
        };
        memcpy(ack.mac, my_mac, 6);
        esp_now_send(req->mac, (uint8_t *)&ack, sizeof(ack));

        pairing_success = true;
        return;
    }

    // Handle unpair notification from paired board
    if (msg_type == MSG_UNPAIR && has_paired_mac) {
        if (memcmp(info->src_addr, paired_mac, 6) == 0) {
            memset(paired_mac, 0, 6);
            has_paired_mac = false;
            nvs_handle_t nvs;
            if (nvs_open("bloco", NVS_READWRITE, &nvs) == ESP_OK) {
                nvs_erase_key(nvs, "paired_mac");
                nvs_commit(nvs);
                nvs_close(nvs);
            }
            ESP_LOGI(TAG, "Unpaired by board");
        }
        return;
    }

    // MAC filtering: must be paired and from correct board (except during pairing)
    if (!pairing_active) {
        if (!has_paired_mac) {
            return;  // Not paired — ignore all program messages
        }
        if (memcmp(info->src_addr, paired_mac, 6) != 0) {
            return;  // Ignore messages from other devices
        }
    }

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
            // Send ACK back to the board
            esp_now_peer_info_t peer_info;
            if (esp_now_get_peer(info->src_addr, &peer_info) != ESP_OK) {
                esp_now_peer_info_t new_peer = {
                    .channel = ESPNOW_CHANNEL,
                    .encrypt = false,
                };
                memcpy(new_peer.peer_addr, info->src_addr, 6);
                esp_now_add_peer(&new_peer);
            }
            espnow_program_ack_t ack = {
                .msg_type = MSG_PROGRAM_ACK,
                .block_count = rx_expected,
            };
            esp_now_send(info->src_addr, (uint8_t *)&ack, sizeof(ack));
            ESP_LOGI(TAG, "Program received successfully (%d blocks), ACK sent", rx_expected);

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

static void load_paired_mac(void)
{
    nvs_handle_t nvs;
    if (nvs_open("bloco", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = 6;
        if (nvs_get_blob(nvs, "paired_mac", paired_mac, &len) == ESP_OK && len == 6) {
            has_paired_mac = true;
            ESP_LOGI(TAG, "Loaded paired MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     paired_mac[0], paired_mac[1], paired_mac[2],
                     paired_mac[3], paired_mac[4], paired_mac[5]);
        }
        nvs_close(nvs);
    }
}

static void save_paired_mac(void)
{
    nvs_handle_t nvs;
    if (nvs_open("bloco", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_blob(nvs, "paired_mac", paired_mac, 6);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "Saved paired MAC to NVS");
    }
}

static void button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PAIR_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PAIR_BUTTON_GPIO, button_isr, NULL);
}

static void wifi_espnow_init(void)
{
    // NVS required by WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Load paired MAC before registering callback
    load_paired_mac();

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

    if (has_paired_mac) {
        ESP_LOGI(TAG, "ESP-NOW receiver initialized (paired to %02X:%02X:%02X:%02X:%02X:%02X)",
                 paired_mac[0], paired_mac[1], paired_mac[2],
                 paired_mac[3], paired_mac[4], paired_mac[5]);
    } else {
        ESP_LOGI(TAG, "ESP-NOW receiver initialized (accepting all)");
    }
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

    // Store device role in NVS for identification
    {
        nvs_handle_t nvs;
        if (nvs_open("bloco", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "role", "robo");
            nvs_commit(nvs);
            nvs_close(nvs);
        }
    }
    printf("DEVICE_ROLE=robo\n");
    fflush(stdout);

    // Create semaphore
    program_ready_sem = xSemaphoreCreateBinary();

    // Init LED
    led_init();

    // Init motors
    ESP_ERROR_CHECK(motor_init());

    // Init display + eyes
    ESP_ERROR_CHECK(display_init());
    eyes_init();

    // Init WiFi + ESP-NOW receiver
    wifi_espnow_init();

    // Log paired status prominently
    if (has_paired_mac) {
        ESP_LOGI(TAG, "=== Paired to board: %02X:%02X:%02X:%02X:%02X:%02X ===",
                 paired_mac[0], paired_mac[1], paired_mac[2],
                 paired_mac[3], paired_mac[4], paired_mac[5]);
    } else {
        ESP_LOGI(TAG, "=== Not paired (accepting from any board) ===");
    }

    // Init BOOT button for pairing
    button_init();

    // Start executor task
    xTaskCreate(executor_task, "executor", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Ready — waiting for program via ESP-NOW...");
    ESP_LOGI(TAG, "Hold BOOT button 4s to enter pairing mode");

    // Pairing state machine variables
    int64_t pair_start_time = 0;
    bool pair_eye_toggle = false;
    int64_t last_eye_toggle = 0;
    int64_t last_pair_log = 0;
    bool pair_led_on = false;
    int pair_success_countdown = 0;  // ticks remaining for success display

    // Polling-based long press detection
    int64_t button_hold_start = 0;
    bool button_was_pressed = false;

    // Paired status LED state (-1 = unset, 0 = red/unpaired, 1 = green/paired)
    int led_paired_state = -1;

    // --- Main loop (100ms tick for responsive pairing) ---
    while (1) {
        // --- Poll button for long press (ISR may miss release edge) ---
        bool button_down = (gpio_get_level(PAIR_BUTTON_GPIO) == 0);
        if (button_down) {
            if (!button_was_pressed) {
                button_hold_start = esp_timer_get_time();
                button_was_pressed = true;
            } else if (!pairing_active && !pairing_requested) {
                int64_t held = esp_timer_get_time() - button_hold_start;
                if (held >= LONG_PRESS_US) {
                    pairing_requested = true;
                    button_was_pressed = false;
                }
            }
        } else {
            button_was_pressed = false;
        }

        // --- Enter pairing mode ---
        if (pairing_requested && !pairing_active) {
            pairing_requested = false;
            pairing_active = true;
            pairing_success = false;
            pair_start_time = esp_timer_get_time();
            last_eye_toggle = 0;
            pair_eye_toggle = false;
            last_pair_log = 0;

            // Clear old pairing — notify board and stop accepting
            if (has_paired_mac) {
                espnow_unpair_t unpair = { .msg_type = MSG_UNPAIR };
                esp_now_send(paired_mac, (uint8_t *)&unpair, sizeof(unpair));
                vTaskDelay(pdMS_TO_TICKS(50));  // Let message send
                ESP_LOGI(TAG, "Sent unpair notification to board");
                memset(paired_mac, 0, 6);
                has_paired_mac = false;
                nvs_handle_t nvs;
                if (nvs_open("bloco", NVS_READWRITE, &nvs) == ESP_OK) {
                    nvs_erase_key(nvs, "paired_mac");
                    nvs_commit(nvs);
                    nvs_close(nvs);
                }
                ESP_LOGI(TAG, "Cleared previous pairing");
            }

            led_paired_state = -1;  // Force LED update after pairing ends
            eyes_set_expression(EYES_SURPRISED);
            ESP_LOGI(TAG, "*** PAIRING MODE — waiting for pair request ***");
        }

        // --- Pairing active: animate eyes and check state ---
        if (pairing_active) {
            int64_t now = esp_timer_get_time();
            int64_t elapsed_ms = (now - pair_start_time) / 1000;

            if (pairing_success) {
                // Pairing succeeded
                pairing_active = false;
                save_paired_mac();

                ESP_LOGI(TAG, "*** PAIRED with %02X:%02X:%02X:%02X:%02X:%02X ***",
                         paired_mac[0], paired_mac[1], paired_mac[2],
                         paired_mac[3], paired_mac[4], paired_mac[5]);

                eyes_set_expression(EYES_HAPPY);
                pair_success_countdown = 20;  // 20 * 100ms = 2 seconds (eyes only)

            } else if (elapsed_ms >= PAIR_TIMEOUT_MS) {
                // Timeout
                pairing_active = false;
                ESP_LOGW(TAG, "Pairing timed out");
                eyes_set_expression(EYES_SAD);
                vTaskDelay(pdMS_TO_TICKS(2000));
                eyes_set_expression(EYES_NORMAL);

            } else {
                // Periodic log every 5 seconds
                int64_t elapsed_s = elapsed_ms / 1000;
                if (elapsed_s > 0 && (now - last_pair_log) / 1000 >= 5000) {
                    last_pair_log = now;
                    ESP_LOGI(TAG, "Waiting for pair request... %d seconds elapsed", (int)elapsed_s);
                }

                // Blink blue LED during pairing
                int64_t blink_phase = (now / 1000) % 500;
                if (blink_phase < 250) {
                    if (!pair_led_on) { led_set(0, 0, 32); pair_led_on = true; }
                } else {
                    if (pair_led_on) { led_off(); pair_led_on = false; }
                }

                // Alternate eyes between NORMAL and SURPRISED every 500ms
                if ((now - last_eye_toggle) / 1000 >= 500) {
                    last_eye_toggle = now;
                    pair_eye_toggle = !pair_eye_toggle;
                    eyes_set_expression(pair_eye_toggle ? EYES_NORMAL : EYES_SURPRISED);
                }
            }
        }

        // --- Count down success display ---
        if (pair_success_countdown > 0) {
            pair_success_countdown--;
            if (pair_success_countdown == 0) {
                eyes_set_expression(EYES_NORMAL);
            }
        }

        // --- Paired status LED: solid green if paired, solid red if not ---
        if (!pairing_active) {
            int want_paired = has_paired_mac ? 1 : 0;
            if (want_paired != led_paired_state) {
                led_paired_state = want_paired;
                if (has_paired_mac) led_set(0, 16, 0);
                else led_set(16, 0, 0);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
