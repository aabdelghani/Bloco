#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
#include "camera.h"
#include "classifier.h"

// --- Device info ---
#define DEVICE_NAME       "Bloco Tower"
#define FIRMWARE_VERSION  "0.1.0"

// --- Configuration ---
#define SEND_BUTTON_GPIO  GPIO_NUM_0   // BOOT button
#define LED_GPIO          GPIO_NUM_48  // Onboard WS2812 LED
#define LONG_PRESS_US     4000000      // 4 seconds in microseconds
#define PAIR_TIMEOUT_MS   30000        // 30 second pairing timeout
#define PAIR_BCAST_MS     500          // Broadcast pair request every 500ms

// Slot cropping: divide 320×240 frame into NUM_SLOTS equal horizontal strips
#define SLOT_WIDTH        CAPTURE_WIDTH
#define SLOT_HEIGHT       (CAPTURE_HEIGHT / CONFIG_TOWER_NUM_SLOTS)

static const char *TAG = "tower";
static const uint8_t broadcast_mac[] = ESPNOW_BROADCAST_MAC;

// Target MAC for sending (unicast if paired, broadcast otherwise)
static uint8_t target_mac[6] = ESPNOW_BROADCAST_MAC;
static bool has_paired_mac = false;

// Button state
static volatile bool send_requested = false;
static volatile int64_t button_press_time = 0;
static volatile bool pairing_requested = false;

// Pairing state
static volatile bool pairing_active = false;
static volatile bool pairing_success = false;

// LED strip handle
static led_strip_handle_t led_strip;

// ---------------------------------------------------------------------------
// LED helpers
// ---------------------------------------------------------------------------

static void led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
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

// ---------------------------------------------------------------------------
// Button ISR
// ---------------------------------------------------------------------------

static void IRAM_ATTR button_isr(void *arg)
{
    int level = gpio_get_level(SEND_BUTTON_GPIO);
    if (level == 0) {
        button_press_time = esp_timer_get_time();
    } else {
        int64_t held = esp_timer_get_time() - button_press_time;
        if (held >= LONG_PRESS_US) {
            pairing_requested = true;
        } else {
            send_requested = true;
        }
    }
}

static void button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SEND_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(SEND_BUTTON_GPIO, button_isr, NULL);
}

// ---------------------------------------------------------------------------
// NVS pairing storage
// ---------------------------------------------------------------------------

static void load_paired_mac(void)
{
    nvs_handle_t nvs;
    if (nvs_open("bloco", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = 6;
        if (nvs_get_blob(nvs, "paired_mac", target_mac, &len) == ESP_OK && len == 6) {
            has_paired_mac = true;
            ESP_LOGI(TAG, "Loaded paired MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     target_mac[0], target_mac[1], target_mac[2],
                     target_mac[3], target_mac[4], target_mac[5]);
        }
        nvs_close(nvs);
    }
}

static void save_paired_mac(void)
{
    nvs_handle_t nvs;
    if (nvs_open("bloco", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_blob(nvs, "paired_mac", target_mac, 6);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "Saved paired MAC to NVS");
    }
}

// ---------------------------------------------------------------------------
// ESP-NOW receive callback
// ---------------------------------------------------------------------------

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < 1) return;

    if (data[0] == MSG_PAIR_ACK && pairing_active) {
        if (len < (int)sizeof(espnow_pair_ack_t)) return;
        const espnow_pair_ack_t *ack = (const espnow_pair_ack_t *)data;
        memcpy(target_mac, ack->mac, 6);
        has_paired_mac = true;
        pairing_success = true;
        ESP_LOGI(TAG, "Pair ACK from %02X:%02X:%02X:%02X:%02X:%02X",
                 ack->mac[0], ack->mac[1], ack->mac[2],
                 ack->mac[3], ack->mac[4], ack->mac[5]);
    }

    if (data[0] == MSG_UNPAIR && has_paired_mac) {
        if (memcmp(info->src_addr, target_mac, 6) == 0) {
            esp_now_del_peer(target_mac);
            memcpy(target_mac, broadcast_mac, 6);
            has_paired_mac = false;
            nvs_handle_t nvs;
            if (nvs_open("bloco", NVS_READWRITE, &nvs) == ESP_OK) {
                nvs_erase_key(nvs, "paired_mac");
                nvs_commit(nvs);
                nvs_close(nvs);
            }
            ESP_LOGI(TAG, "Unpaired by robot — entering pairing mode");
            pairing_requested = true;
        }
    }

    if (data[0] == MSG_PROGRAM_ACK) {
        if (len < (int)sizeof(espnow_program_ack_t)) return;
        const espnow_program_ack_t *ack = (const espnow_program_ack_t *)data;
        ESP_LOGI(TAG, "Robot confirmed: received %d blocks", ack->block_count);
    }
}

// ---------------------------------------------------------------------------
// WiFi + ESP-NOW init
// ---------------------------------------------------------------------------

static void wifi_espnow_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    load_paired_mac();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    // Add broadcast peer (always needed for pairing)
    esp_now_peer_info_t bcast_peer = {
        .channel = ESPNOW_CHANNEL,
        .encrypt = false,
    };
    memcpy(bcast_peer.peer_addr, broadcast_mac, 6);
    esp_now_add_peer(&bcast_peer);

    // If paired, also add unicast peer
    if (has_paired_mac) {
        esp_now_peer_info_t uni_peer = {
            .channel = ESPNOW_CHANNEL,
            .encrypt = false,
        };
        memcpy(uni_peer.peer_addr, target_mac, 6);
        esp_now_add_peer(&uni_peer);
        ESP_LOGI(TAG, "ESP-NOW ready (unicast to %02X:%02X:%02X:%02X:%02X:%02X)",
                 target_mac[0], target_mac[1], target_mac[2],
                 target_mac[3], target_mac[4], target_mac[5]);
    } else {
        ESP_LOGI(TAG, "ESP-NOW ready (broadcast mode)");
    }
}

// ---------------------------------------------------------------------------
// Send program to robot via ESP-NOW
// ---------------------------------------------------------------------------

static void send_program_to_robot(const block_data_t *blocks, uint8_t count)
{
    if (!has_paired_mac) {
        ESP_LOGW(TAG, "Not paired — cannot send program");
        return;
    }

    if (count == 0) {
        ESP_LOGW(TAG, "No blocks to send");
        return;
    }

    const uint8_t *dest = target_mac;
    ESP_LOGI(TAG, ">>> Sending %d block(s) to robot <<<", count);

    // 1. PROGRAM_START
    espnow_program_start_t start_msg = {
        .msg_type = MSG_PROGRAM_START,
        .block_count = count,
    };
    esp_now_send(dest, (uint8_t *)&start_msg, sizeof(start_msg));
    vTaskDelay(pdMS_TO_TICKS(20));

    // 2. Each block
    for (uint8_t i = 0; i < count; i++) {
        espnow_block_msg_t block_msg = {
            .msg_type = MSG_BLOCK_DATA,
            .index = i,
        };
        memcpy(&block_msg.block, &blocks[i], sizeof(block_data_t));
        esp_now_send(dest, (uint8_t *)&block_msg, sizeof(block_msg));
        ESP_LOGI(TAG, "  Sent block %d: type=0x%02X name=%.15s", i, blocks[i].type, blocks[i].name);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // 3. PROGRAM_END
    espnow_program_end_t end_msg = {
        .msg_type = MSG_PROGRAM_END,
    };
    esp_now_send(dest, (uint8_t *)&end_msg, sizeof(end_msg));

    ESP_LOGI(TAG, ">>> Program sent <<<");
}

// ---------------------------------------------------------------------------
// Camera capture → classify → send
// ---------------------------------------------------------------------------

static void capture_and_send(void)
{
    ESP_LOGI(TAG, "Capturing frame...");

    // White flash while capturing
    led_set(32, 32, 32);

    camera_fb_t *fb = tower_camera_capture();
    if (!fb) {
        ESP_LOGE(TAG, "Capture failed");
        led_off();
        return;
    }

    ESP_LOGI(TAG, "Frame: %dx%d, %d bytes", fb->width, fb->height, fb->len);

    // Classify each slot
    block_data_t blocks[ESPNOW_MAX_BLOCKS];
    uint8_t count = 0;
    int num_slots = CONFIG_TOWER_NUM_SLOTS;
    if (num_slots > ESPNOW_MAX_BLOCKS) num_slots = ESPNOW_MAX_BLOCKS;

    int slot_h = fb->height / num_slots;

    for (int s = 0; s < num_slots; s++) {
        int y_offset = s * slot_h;
        const uint8_t *crop = fb->buf + (y_offset * fb->width * 2);  // RGB565 = 2 bytes/pixel

        uint8_t type = classifier_identify(crop, fb->width, slot_h);
        if (type == BLOCK_UNKNOWN) {
            ESP_LOGI(TAG, "Slot %d: empty/unknown — skipping", s);
            continue;
        }

        memset(&blocks[count], 0, sizeof(block_data_t));
        blocks[count].type = type;
        blocks[count].version = BLOCK_VERSION;
        blocks[count].checksum = block_calc_checksum(&blocks[count]);
        count++;
    }

    esp_camera_fb_return(fb);
    led_off();

    if (count == 0) {
        ESP_LOGW(TAG, "No blocks recognized");
        return;
    }

    send_program_to_robot(blocks, count);
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "=== %s v%s ===", DEVICE_NAME, FIRMWARE_VERSION);

    // Store device role for tool identification
    {
        nvs_handle_t nvs;
        if (nvs_open("bloco", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "role", "tower");
            nvs_commit(nvs);
            nvs_close(nvs);
        }
    }
    printf("DEVICE_ROLE=tower\n");
    fflush(stdout);

    // Init subsystems
    led_init();
    wifi_espnow_init();

    if (has_paired_mac) {
        ESP_LOGI(TAG, "=== Paired to %02X:%02X:%02X:%02X:%02X:%02X ===",
                 target_mac[0], target_mac[1], target_mac[2],
                 target_mac[3], target_mac[4], target_mac[5]);
    } else {
        ESP_LOGI(TAG, "=== Not paired (hold BOOT 4s to pair) ===");
    }

    // Init camera
    esp_err_t cam_ret = tower_camera_init();
    if (cam_ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed — tower cannot operate");
    }

    // Init classifier (stub for now)
    classifier_init();

    // Init button
    button_init();
    ESP_LOGI(TAG, "Press BOOT to scan & send, hold 4s to pair");

    // Pairing state variables
    int64_t pair_start_time = 0;
    int64_t last_pair_bcast = 0;
    bool led_on = false;

    // Polling-based long press detection
    int64_t button_hold_start = 0;
    bool button_was_pressed = false;

    // Paired status LED state
    int led_paired_state = -1;

    // --- Main loop ---
    while (1) {
        // --- Poll button for long press ---
        bool button_down = (gpio_get_level(SEND_BUTTON_GPIO) == 0);
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

        // --- Pairing state machine ---
        if (pairing_requested && !pairing_active) {
            pairing_requested = false;
            pairing_active = true;
            pairing_success = false;
            pair_start_time = esp_timer_get_time();
            last_pair_bcast = 0;
            led_on = false;

            // Clear old pairing
            if (has_paired_mac) {
                espnow_unpair_t unpair = { .msg_type = MSG_UNPAIR };
                esp_now_send(target_mac, (uint8_t *)&unpair, sizeof(unpair));
                vTaskDelay(pdMS_TO_TICKS(50));
                ESP_LOGI(TAG, "Sent unpair notification to robot");
                esp_now_del_peer(target_mac);
                memcpy(target_mac, broadcast_mac, 6);
                has_paired_mac = false;
                nvs_handle_t nvs;
                if (nvs_open("bloco", NVS_READWRITE, &nvs) == ESP_OK) {
                    nvs_erase_key(nvs, "paired_mac");
                    nvs_commit(nvs);
                    nvs_close(nvs);
                }
            }

            led_paired_state = -1;
            ESP_LOGI(TAG, "*** PAIRING MODE ***");
        }

        if (pairing_active) {
            int64_t now = esp_timer_get_time();
            int64_t elapsed_ms = (now - pair_start_time) / 1000;

            if (pairing_success) {
                pairing_active = false;
                save_paired_mac();

                // Update ESP-NOW peers
                esp_now_del_peer(broadcast_mac);
                esp_now_peer_info_t uni_peer = {
                    .channel = ESPNOW_CHANNEL,
                    .encrypt = false,
                };
                memcpy(uni_peer.peer_addr, target_mac, 6);
                esp_now_add_peer(&uni_peer);
                esp_now_peer_info_t bcast_peer = {
                    .channel = ESPNOW_CHANNEL,
                    .encrypt = false,
                };
                memcpy(bcast_peer.peer_addr, broadcast_mac, 6);
                esp_now_add_peer(&bcast_peer);

                ESP_LOGI(TAG, "*** PAIRED SUCCESSFULLY ***");

            } else if (elapsed_ms >= PAIR_TIMEOUT_MS) {
                pairing_active = false;
                ESP_LOGW(TAG, "Pairing timed out");

            } else {
                // Blink blue LED
                int64_t blink_phase = (now / 1000) % 500;
                if (blink_phase < 250) {
                    if (!led_on) { led_set(0, 0, 32); led_on = true; }
                } else {
                    if (led_on) { led_off(); led_on = false; }
                }

                // Broadcast pair request every 500ms
                if ((now - last_pair_bcast) / 1000 >= PAIR_BCAST_MS) {
                    last_pair_bcast = now;
                    uint8_t my_mac[6];
                    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
                    espnow_pair_request_t req = {
                        .msg_type = MSG_PAIR_REQUEST,
                    };
                    memcpy(req.mac, my_mac, 6);
                    esp_now_send(broadcast_mac, (uint8_t *)&req, sizeof(req));
                }
            }

            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // --- Normal operation: button press triggers capture & send ---
        if (send_requested) {
            send_requested = false;
            capture_and_send();
        }

        // Paired status LED: green if paired, red if not
        int want_paired = has_paired_mac ? 1 : 0;
        if (want_paired != led_paired_state) {
            led_paired_state = want_paired;
            if (has_paired_mac) led_set(0, 16, 0);
            else led_set(16, 0, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
