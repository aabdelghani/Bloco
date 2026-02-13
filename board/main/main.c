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
#include "eeprom.h"
#include "block_types.h"
#include "espnow_protocol.h"

// --- Configuration ---
#define NUM_EEPROMS      2            // EEPROM on channel 0 and channel 1
#define EEPROM_SIZE      256          // bytes to read per EEPROM
#define POLL_INTERVAL_MS 1000
#define SEND_BUTTON_GPIO GPIO_NUM_0   // BOOT button
#define LED_GPIO         GPIO_NUM_48  // Onboard WS2812 LED
#define LONG_PRESS_US    4000000      // 4 seconds in microseconds
#define PAIR_TIMEOUT_MS  30000        // 30 second pairing timeout
#define PAIR_BCAST_MS    500          // Broadcast pair request every 500ms

static const char *TAG = "board";
static const uint8_t broadcast_mac[] = ESPNOW_BROADCAST_MAC;

// Per-channel state
static uint8_t eeprom_data[NUM_EEPROMS][EEPROM_SIZE];
static bool    eeprom_present[NUM_EEPROMS];
static bool    data_valid[NUM_EEPROMS];
static volatile bool send_requested = false;

// Target MAC for sending (unicast if paired, broadcast otherwise)
static uint8_t target_mac[6] = ESPNOW_BROADCAST_MAC;
static bool has_paired_mac = false;

// Button long-press tracking
static volatile int64_t button_press_time = 0;
static volatile bool pairing_requested = false;

// Pairing state
static volatile bool pairing_active = false;
static volatile bool pairing_success = false;

// LED strip handle
static led_strip_handle_t led_strip;

static void IRAM_ATTR button_isr(void *arg)
{
    int level = gpio_get_level(SEND_BUTTON_GPIO);
    if (level == 0) {
        // Button pressed (active low) — record time
        button_press_time = esp_timer_get_time();
    } else {
        // Button released — check duration
        int64_t held = esp_timer_get_time() - button_press_time;
        if (held >= LONG_PRESS_US) {
            pairing_requested = true;
        } else {
            send_requested = true;
        }
    }
}

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

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < 1) return;

    if (data[0] == MSG_PAIR_ACK && pairing_active) {
        if (len < (int)sizeof(espnow_pair_ack_t)) return;
        const espnow_pair_ack_t *ack = (const espnow_pair_ack_t *)data;

        // Store the robot's MAC
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
            ESP_LOGI(TAG, "Unpaired by robot");
        }
    }

    if (data[0] == MSG_PROGRAM_ACK) {
        if (len < (int)sizeof(espnow_program_ack_t)) return;
        const espnow_program_ack_t *ack = (const espnow_program_ack_t *)data;
        ESP_LOGI(TAG, "Robot confirmed: received %d blocks successfully", ack->block_count);
    }
}

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

static void wifi_espnow_init(void)
{
    // NVS required by WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Load paired MAC before setting up peers
    load_paired_mac();

    // WiFi init (STA mode, no connect — just needed for ESP-NOW)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // ESP-NOW init
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    // Add broadcast peer (always needed for pairing requests)
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
        ESP_LOGI(TAG, "ESP-NOW initialized (unicast to %02X:%02X:%02X:%02X:%02X:%02X)",
                 target_mac[0], target_mac[1], target_mac[2],
                 target_mac[3], target_mac[4], target_mac[5]);
    } else {
        ESP_LOGI(TAG, "ESP-NOW initialized (broadcast mode)");
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
    ESP_LOGI(TAG, ">>> Sending %d block(s) to robot via ESP-NOW <<<", count);

    // 1. Send PROGRAM_START
    espnow_program_start_t start_msg = {
        .msg_type = MSG_PROGRAM_START,
        .block_count = count,
    };
    esp_now_send(dest, (uint8_t *)&start_msg, sizeof(start_msg));
    vTaskDelay(pdMS_TO_TICKS(20));

    // 2. Send each block
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

    // 3. Send PROGRAM_END
    espnow_program_end_t end_msg = {
        .msg_type = MSG_PROGRAM_END,
    };
    esp_now_send(dest, (uint8_t *)&end_msg, sizeof(end_msg));

    ESP_LOGI(TAG, ">>> Program sent successfully <<<");
}

// Build block array from EEPROM data and send to robot
static void send_eeprom_program_to_robot(void)
{
    block_data_t blocks[NUM_EEPROMS];
    uint8_t count = 0;

    for (int ch = 0; ch < NUM_EEPROMS; ch++) {
        if (eeprom_present[ch] && data_valid[ch]) {
            memcpy(&blocks[count], eeprom_data[ch], sizeof(block_data_t));
            count++;
        }
    }

    if (count == 0) {
        ESP_LOGW(TAG, "No blocks to send — insert EEPROMs first");
        return;
    }

    send_program_to_robot(blocks, count);
}

static void print_hex_dump(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i += 16) {
        printf("%04x: ", (unsigned)i);
        for (size_t j = 0; j < 16 && (i + j) < len; j++) {
            printf("%02x ", buf[i + j]);
        }
        printf(" |");
        for (size_t j = 0; j < 16 && (i + j) < len; j++) {
            uint8_t c = buf[i + j];
            printf("%c", (c >= 0x20 && c <= 0x7e) ? c : '.');
        }
        printf("|\n");
    }
}

// --- JSON command handler for GUI tool (debug builds only) ---
#ifdef CONFIG_BOARD_SERIAL_CMD

#include "cJSON.h"

static void print_block_json(int ch, const uint8_t *raw)
{
    const block_data_t *b = (const block_data_t *)raw;
    char name_buf[BLOCK_NAME_MAX_LEN + 1];
    memset(name_buf, 0, sizeof(name_buf));
    memcpy(name_buf, b->name, BLOCK_NAME_MAX_LEN);

    char serial_str[12];
    snprintf(serial_str, sizeof(serial_str), "%02X%02X%02X%02X",
             b->serial[0], b->serial[1], b->serial[2], b->serial[3]);

    uint8_t calc_ck = block_calc_checksum(b);

    printf("{\"response\":\"BLOCK_DATA\",\"channel\":%d,\"present\":true,"
           "\"type\":%d,\"subtype\":%d,\"param1\":%d,\"param2\":%d,"
           "\"serial\":\"%s\",\"version\":%d,\"checksum\":%d,\"checksum_valid\":%s,"
           "\"name\":\"%s\",\"valid\":%s}\n",
           ch, b->type, b->subtype, b->param1, b->param2,
           serial_str, b->version, b->checksum,
           (calc_ck == b->checksum) ? "true" : "false",
           name_buf,
           block_type_valid(b->type) ? "true" : "false");
}

static void handle_serial_command(const char *line)
{
    // Simple parsing — look for "cmd" field
    // SCAN_CHANNELS: read all channels and return block data
    if (strstr(line, "SCAN_CHANNELS")) {
        printf("{\"response\":\"SCAN_START\",\"num_channels\":%d}\n", NUM_EEPROMS);
        fflush(stdout);

        for (int ch = 0; ch < NUM_EEPROMS; ch++) {
            bool present = eeprom_is_present(ch);
            if (present) {
                uint8_t buf[BLOCK_DATA_SIZE];
                esp_err_t err = eeprom_read(0x0000, buf, BLOCK_DATA_SIZE);
                if (err == ESP_OK) {
                    print_block_json(ch, buf);
                } else {
                    printf("{\"response\":\"BLOCK_DATA\",\"channel\":%d,\"present\":true,\"error\":\"read_failed\"}\n", ch);
                }
            } else {
                printf("{\"response\":\"BLOCK_DATA\",\"channel\":%d,\"present\":false}\n", ch);
            }
            fflush(stdout);
        }

        printf("{\"response\":\"SCAN_END\"}\n");
        fflush(stdout);

    } else if (strstr(line, "SEND_TO_ROBOT")) {
        send_eeprom_program_to_robot();
        printf("{\"response\":\"SEND_OK\"}\n");
        fflush(stdout);

    } else if (strstr(line, "SEND_BLOCKS")) {
        // Parse JSON to get blocks array
        cJSON *root = cJSON_Parse(line);
        if (!root) {
            printf("{\"response\":\"ERROR\",\"msg\":\"JSON parse failed\"}\n");
            fflush(stdout);
        } else {
            cJSON *arr = cJSON_GetObjectItem(root, "blocks");
            if (!cJSON_IsArray(arr)) {
                printf("{\"response\":\"ERROR\",\"msg\":\"missing blocks array\"}\n");
                fflush(stdout);
            } else {
                int n = cJSON_GetArraySize(arr);
                if (n <= 0 || n > ESPNOW_MAX_BLOCKS) {
                    printf("{\"response\":\"ERROR\",\"msg\":\"block count out of range\"}\n");
                    fflush(stdout);
                } else {
                    block_data_t blocks[ESPNOW_MAX_BLOCKS];
                    memset(blocks, 0, sizeof(blocks));

                    for (int i = 0; i < n; i++) {
                        cJSON *item = cJSON_GetArrayItem(arr, i);
                        cJSON *j_type = cJSON_GetObjectItem(item, "type");
                        cJSON *j_name = cJSON_GetObjectItem(item, "name");

                        if (cJSON_IsNumber(j_type))
                            blocks[i].type = (uint8_t)j_type->valueint;
                        blocks[i].version = BLOCK_VERSION;
                        if (cJSON_IsString(j_name) && j_name->valuestring)
                            strncpy(blocks[i].name, j_name->valuestring, BLOCK_NAME_MAX_LEN - 1);
                        blocks[i].checksum = block_calc_checksum(&blocks[i]);
                    }

                    send_program_to_robot(blocks, (uint8_t)n);
                    printf("{\"response\":\"SEND_OK\",\"blocks_sent\":%d}\n", n);
                    fflush(stdout);
                }
            }
            cJSON_Delete(root);
        }

    } else if (strstr(line, "GET_STATUS")) {
        int present_count = 0;
        for (int ch = 0; ch < NUM_EEPROMS; ch++) {
            if (eeprom_present[ch]) present_count++;
        }
        printf("{\"response\":\"STATUS\",\"num_channels\":%d,\"blocks_present\":%d,\"i2c_ok\":true}\n",
               NUM_EEPROMS, present_count);
        fflush(stdout);
    }
}

static void uart_cmd_task(void *arg)
{
    char line_buf[1024];
    int pos = 0;

    while (1) {
        int c = fgetc(stdin);
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (c == '\n' || c == '\r') {
            if (pos > 0) {
                line_buf[pos] = '\0';
                if (line_buf[0] == '{') {
                    handle_serial_command(line_buf);
                }
                pos = 0;
            }
        } else if (pos < (int)sizeof(line_buf) - 1) {
            line_buf[pos++] = (char)c;
        }
    }
}

#endif // CONFIG_BOARD_SERIAL_CMD

void app_main(void)
{
    ESP_LOGI(TAG, "=== Bloco Board Reader ===");

    // Store device role in NVS for identification
    {
        nvs_handle_t nvs;
        if (nvs_open("bloco", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "role", "board");
            nvs_commit(nvs);
            nvs_close(nvs);
        }
    }
    printf("DEVICE_ROLE=board\n");
    fflush(stdout);

    // --- Init LED ---
    led_init();

    // --- Init WiFi + ESP-NOW ---
    wifi_espnow_init();

    // Log paired status prominently
    if (has_paired_mac) {
        ESP_LOGI(TAG, "=== Current target: paired to %02X:%02X:%02X:%02X:%02X:%02X ===",
                 target_mac[0], target_mac[1], target_mac[2],
                 target_mac[3], target_mac[4], target_mac[5]);
    } else {
        ESP_LOGI(TAG, "=== Current target: BROADCAST (not paired) ===");
    }

    // --- Init send button (GPIO 0 / BOOT) ---
    button_init();

    // --- Init shared EEPROM/I2C driver ---
    esp_err_t ret = eeprom_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EEPROM init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "I2C bus ready. Polling channels 0,%d every %d ms ...", NUM_EEPROMS - 1, POLL_INTERVAL_MS);
    ESP_LOGI(TAG, "Press BOOT button (GPIO %d) to send, hold 4s to pair", SEND_BUTTON_GPIO);

#ifdef CONFIG_BOARD_SERIAL_CMD
    // Start UART command listener for GUI tool
    xTaskCreate(uart_cmd_task, "uart_cmd", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Serial command handler enabled (debug build)");
#endif

    memset(eeprom_present, 0, sizeof(eeprom_present));
    memset(data_valid, 0, sizeof(data_valid));

    // Pairing state machine variables
    int64_t pair_start_time = 0;
    int64_t last_pair_bcast = 0;
    int64_t last_pair_log = 0;
    bool led_on = false;
    int poll_counter = 0;

    // Polling-based long press detection (backup for ISR)
    int64_t button_hold_start = 0;
    bool button_was_pressed = false;

    // Paired status LED state (-1 = unset, 0 = red/unpaired, 1 = green/paired)
    int led_paired_state = -1;

    // --- Main loop ---
    while (1) {
        // --- Poll button for long press (ISR may miss release edge) ---
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
            last_pair_log = 0;
            led_on = false;

            // Clear old pairing — notify robot and go back to broadcast mode
            if (has_paired_mac) {
                espnow_unpair_t unpair = { .msg_type = MSG_UNPAIR };
                esp_now_send(target_mac, (uint8_t *)&unpair, sizeof(unpair));
                vTaskDelay(pdMS_TO_TICKS(50));  // Let message send
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
                ESP_LOGI(TAG, "Cleared previous pairing");
            }

            led_paired_state = -1;  // Force LED update after pairing ends
            ESP_LOGI(TAG, "*** PAIRING MODE — broadcasting pair requests ***");
        }

        if (pairing_active) {
            int64_t now = esp_timer_get_time();
            int64_t elapsed_ms = (now - pair_start_time) / 1000;

            if (pairing_success) {
                // Pairing succeeded
                pairing_active = false;

                // Save to NVS
                save_paired_mac();

                // Update ESP-NOW peer: remove old broadcast-only, add unicast
                esp_now_del_peer(broadcast_mac);
                esp_now_peer_info_t uni_peer = {
                    .channel = ESPNOW_CHANNEL,
                    .encrypt = false,
                };
                memcpy(uni_peer.peer_addr, target_mac, 6);
                esp_now_add_peer(&uni_peer);
                // Re-add broadcast for future pairing requests
                esp_now_peer_info_t bcast_peer = {
                    .channel = ESPNOW_CHANNEL,
                    .encrypt = false,
                };
                memcpy(bcast_peer.peer_addr, broadcast_mac, 6);
                esp_now_add_peer(&bcast_peer);

                ESP_LOGI(TAG, "*** PAIRED SUCCESSFULLY ***");

            } else if (elapsed_ms >= PAIR_TIMEOUT_MS) {
                // Timeout
                pairing_active = false;
                ESP_LOGW(TAG, "Pairing timed out");

            } else {
                // Periodic log every 5 seconds
                int64_t elapsed_s = elapsed_ms / 1000;
                if (elapsed_s > 0 && (now - last_pair_log) / 1000 >= 5000) {
                    last_pair_log = now;
                    ESP_LOGI(TAG, "Pairing... %d seconds elapsed", (int)elapsed_s);
                }

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
            continue;  // Skip EEPROM polling during pairing
        }

        // --- Normal operation ---
        if (send_requested) {
            send_requested = false;
            send_eeprom_program_to_robot();
        }

        // Poll EEPROMs every POLL_INTERVAL_MS (using 100ms ticks)
        poll_counter++;
        if (poll_counter >= (POLL_INTERVAL_MS / 100)) {
            poll_counter = 0;

            for (int ch = 0; ch < NUM_EEPROMS; ch++) {
                bool present_now = eeprom_is_present(ch);

                if (present_now && !eeprom_present[ch]) {
                    ESP_LOGI(TAG, ">>> EEPROM DETECTED on channel %d <<<", ch);

                    esp_err_t err = eeprom_read(0x0000, eeprom_data[ch], EEPROM_SIZE);
                    if (err == ESP_OK) {
                        data_valid[ch] = true;
                        ESP_LOGI(TAG, "Channel %d — read %d bytes:", ch, EEPROM_SIZE);
                        print_hex_dump(eeprom_data[ch], EEPROM_SIZE);

                        bool all_ff = true;
                        for (int i = 0; i < EEPROM_SIZE; i++) {
                            if (eeprom_data[ch][i] != 0xFF) { all_ff = false; break; }
                        }
                        if (all_ff) {
                            ESP_LOGW(TAG, "Channel %d — EEPROM is blank (all 0xFF)", ch);
                        }
                    } else {
                        ESP_LOGE(TAG, "Channel %d — read failed: %s", ch, esp_err_to_name(err));
                    }

                } else if (!present_now && eeprom_present[ch]) {
                    ESP_LOGW(TAG, ">>> EEPROM REMOVED from channel %d <<<", ch);
                    if (data_valid[ch]) {
                        memset(eeprom_data[ch], 0, EEPROM_SIZE);
                        data_valid[ch] = false;
                    }
                }

                eeprom_present[ch] = present_now;
            }
        }

        // Paired status LED: solid green if paired, solid red if not
        int want_paired = has_paired_mac ? 1 : 0;
        if (want_paired != led_paired_state) {
            led_paired_state = want_paired;
            if (has_paired_mac) led_set(0, 16, 0);
            else led_set(16, 0, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
