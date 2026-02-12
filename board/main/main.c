#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "eeprom.h"
#include "block_types.h"
#include "espnow_protocol.h"

// --- Configuration ---
#define NUM_EEPROMS      2            // EEPROM on channel 0 and channel 1
#define EEPROM_SIZE      256          // bytes to read per EEPROM
#define POLL_INTERVAL_MS 1000
#define SEND_BUTTON_GPIO GPIO_NUM_0   // BOOT button

static const char *TAG = "board";
static const uint8_t broadcast_mac[] = ESPNOW_BROADCAST_MAC;

// Per-channel state
static uint8_t eeprom_data[NUM_EEPROMS][EEPROM_SIZE];
static bool    eeprom_present[NUM_EEPROMS];
static bool    data_valid[NUM_EEPROMS];
static volatile bool send_requested = false;

static void IRAM_ATTR button_isr(void *arg)
{
    send_requested = true;
}

static void wifi_espnow_init(void)
{
    // NVS required by WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // WiFi init (STA mode, no connect — just needed for ESP-NOW)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // ESP-NOW init
    ESP_ERROR_CHECK(esp_now_init());

    // Add broadcast peer
    esp_now_peer_info_t peer = {
        .channel = ESPNOW_CHANNEL,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, broadcast_mac, 6);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    ESP_LOGI(TAG, "ESP-NOW initialized (broadcast mode)");
}

static void button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SEND_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(SEND_BUTTON_GPIO, button_isr, NULL);
}

static void send_program_to_robot(const block_data_t *blocks, uint8_t count)
{
    if (count == 0) {
        ESP_LOGW(TAG, "No blocks to send");
        return;
    }

    ESP_LOGI(TAG, ">>> Sending %d block(s) to robot via ESP-NOW <<<", count);

    // 1. Send PROGRAM_START
    espnow_program_start_t start_msg = {
        .msg_type = MSG_PROGRAM_START,
        .block_count = count,
    };
    esp_now_send(broadcast_mac, (uint8_t *)&start_msg, sizeof(start_msg));
    vTaskDelay(pdMS_TO_TICKS(20));

    // 2. Send each block
    for (uint8_t i = 0; i < count; i++) {
        espnow_block_msg_t block_msg = {
            .msg_type = MSG_BLOCK_DATA,
            .index = i,
        };
        memcpy(&block_msg.block, &blocks[i], sizeof(block_data_t));
        esp_now_send(broadcast_mac, (uint8_t *)&block_msg, sizeof(block_msg));
        ESP_LOGI(TAG, "  Sent block %d: type=0x%02X name=%.15s", i, blocks[i].type, blocks[i].name);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // 3. Send PROGRAM_END
    espnow_program_end_t end_msg = {
        .msg_type = MSG_PROGRAM_END,
    };
    esp_now_send(broadcast_mac, (uint8_t *)&end_msg, sizeof(end_msg));

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

    // --- Init WiFi + ESP-NOW ---
    wifi_espnow_init();

    // --- Init send button (GPIO 0 / BOOT) ---
    button_init();

    // --- Init shared EEPROM/I2C driver ---
    esp_err_t ret = eeprom_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EEPROM init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "I2C bus ready. Polling channels 0,%d every %d ms ...", NUM_EEPROMS - 1, POLL_INTERVAL_MS);
    ESP_LOGI(TAG, "Press BOOT button (GPIO %d) to send program to robot", SEND_BUTTON_GPIO);

#ifdef CONFIG_BOARD_SERIAL_CMD
    // Start UART command listener for GUI tool
    xTaskCreate(uart_cmd_task, "uart_cmd", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Serial command handler enabled (debug build)");
#endif

    memset(eeprom_present, 0, sizeof(eeprom_present));
    memset(data_valid, 0, sizeof(data_valid));

    // --- Main loop: poll channels + check send button ---
    while (1) {
        // Check if send was requested
        if (send_requested) {
            send_requested = false;
            send_eeprom_program_to_robot();
        }

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

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}
