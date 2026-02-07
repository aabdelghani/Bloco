#include "command.h"
#include "programmer.h"
#include "led.h"
#include "block_types.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "command";

#define CMD_BUF_SIZE 512

// Send a JSON response string followed by newline
static void send_response(const char *json_str)
{
    printf("%s\n", json_str);
    fflush(stdout);
}

static void send_error(int code, const char *message)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "response", "ERROR");
    cJSON_AddNumberToObject(resp, "code", code);
    cJSON_AddStringToObject(resp, "message", message);
    char *str = cJSON_PrintUnformatted(resp);
    send_response(str);
    free(str);
    cJSON_Delete(resp);
}

static void serial_to_hex(const uint8_t serial[4], char *out)
{
    snprintf(out, 9, "%02X%02X%02X%02X", serial[0], serial[1], serial[2], serial[3]);
}

static uint8_t parse_channel(cJSON *root)
{
    cJSON *j_ch = cJSON_GetObjectItem(root, "channel");
    if (j_ch && cJSON_IsNumber(j_ch)) {
        int ch = j_ch->valueint;
        if (ch >= 0 && ch <= PROGRAMMER_MAX_CHANNEL) return (uint8_t)ch;
    }
    return 0; // default channel
}

static void handle_write_block(cJSON *root)
{
    uint8_t channel = parse_channel(root);
    cJSON *j_type    = cJSON_GetObjectItem(root, "type");
    cJSON *j_subtype = cJSON_GetObjectItem(root, "subtype");
    cJSON *j_param1  = cJSON_GetObjectItem(root, "param1");
    cJSON *j_param2  = cJSON_GetObjectItem(root, "param2");
    cJSON *j_name    = cJSON_GetObjectItem(root, "name");

    if (!cJSON_IsNumber(j_type)) {
        send_error(1, "Missing or invalid 'type'");
        return;
    }

    uint8_t type    = (uint8_t)j_type->valueint;
    uint8_t subtype = j_subtype && cJSON_IsNumber(j_subtype) ? (uint8_t)j_subtype->valueint : 0;
    uint8_t param1  = j_param1  && cJSON_IsNumber(j_param1)  ? (uint8_t)j_param1->valueint  : 0;
    uint8_t param2  = j_param2  && cJSON_IsNumber(j_param2)  ? (uint8_t)j_param2->valueint  : 0;
    const char *name = (j_name && cJSON_IsString(j_name)) ? j_name->valuestring : "";

    block_data_t blk;
    esp_err_t ret = programmer_write_block(channel, type, subtype, param1, param2, name, &blk);
    if (ret != ESP_OK) {
        send_error(2, "Write failed");
        return;
    }

    char serial_hex[9];
    serial_to_hex(blk.serial, serial_hex);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "response", "WRITE_OK");
    cJSON_AddNumberToObject(resp, "type", blk.type);
    cJSON_AddStringToObject(resp, "serial", serial_hex);
    char *str = cJSON_PrintUnformatted(resp);
    send_response(str);
    free(str);
    cJSON_Delete(resp);
}

static void handle_read_block(cJSON *root)
{
    uint8_t channel = parse_channel(root);
    block_data_t blk;
    esp_err_t ret = programmer_read_block(channel, &blk);
    if (ret != ESP_OK) {
        send_error(3, "Read failed");
        return;
    }

    char serial_hex[9];
    serial_to_hex(blk.serial, serial_hex);

    // Ensure name is null-terminated for printing
    char name_buf[BLOCK_NAME_MAX_LEN + 1] = {0};
    memcpy(name_buf, blk.name, BLOCK_NAME_MAX_LEN);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "response", "READ_DATA");
    cJSON_AddNumberToObject(resp, "type", blk.type);
    cJSON_AddNumberToObject(resp, "subtype", blk.subtype);
    cJSON_AddNumberToObject(resp, "param1", blk.param1);
    cJSON_AddNumberToObject(resp, "param2", blk.param2);
    cJSON_AddStringToObject(resp, "serial", serial_hex);
    cJSON_AddStringToObject(resp, "name", name_buf);
    char *str = cJSON_PrintUnformatted(resp);
    send_response(str);
    free(str);
    cJSON_Delete(resp);
}

static void handle_erase_block(cJSON *root)
{
    uint8_t channel = parse_channel(root);
    esp_err_t ret = programmer_erase_block(channel);
    if (ret != ESP_OK) {
        send_error(4, "Erase failed");
        return;
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "response", "ERASE_OK");
    char *str = cJSON_PrintUnformatted(resp);
    send_response(str);
    free(str);
    cJSON_Delete(resp);
}

static void handle_verify_block(cJSON *root)
{
    uint8_t channel = parse_channel(root);
    block_data_t blk;
    esp_err_t ret = programmer_read_block(channel, &blk);
    if (ret != ESP_OK) {
        send_error(5, "Verify read failed");
        return;
    }

    uint8_t expected_cksum = block_calc_checksum(&blk);
    bool match = (blk.checksum == expected_cksum) && block_type_valid(blk.type);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "response", "VERIFY_OK");
    cJSON_AddBoolToObject(resp, "match", match);
    char *str = cJSON_PrintUnformatted(resp);
    send_response(str);
    free(str);
    cJSON_Delete(resp);
}

static void handle_batch_program(cJSON *root)
{
    cJSON *blocks = cJSON_GetObjectItem(root, "blocks");
    if (!cJSON_IsArray(blocks)) {
        send_error(6, "Missing or invalid 'blocks' array");
        return;
    }

    int count = cJSON_GetArraySize(blocks);
    int success = 0;

    for (int i = 0; i < count; i++) {
        cJSON *entry = cJSON_GetArrayItem(blocks, i);

        cJSON *j_type    = cJSON_GetObjectItem(entry, "type");
        cJSON *j_subtype = cJSON_GetObjectItem(entry, "subtype");
        cJSON *j_param1  = cJSON_GetObjectItem(entry, "param1");
        cJSON *j_param2  = cJSON_GetObjectItem(entry, "param2");
        cJSON *j_name    = cJSON_GetObjectItem(entry, "name");

        if (!cJSON_IsNumber(j_type)) continue;

        uint8_t type    = (uint8_t)j_type->valueint;
        uint8_t subtype = j_subtype && cJSON_IsNumber(j_subtype) ? (uint8_t)j_subtype->valueint : 0;
        uint8_t param1  = j_param1  && cJSON_IsNumber(j_param1)  ? (uint8_t)j_param1->valueint  : 0;
        uint8_t param2  = j_param2  && cJSON_IsNumber(j_param2)  ? (uint8_t)j_param2->valueint  : 0;
        const char *name = (j_name && cJSON_IsString(j_name)) ? j_name->valuestring : "";

        block_data_t blk;
        uint8_t ch = parse_channel(entry);
        esp_err_t ret = programmer_write_block(ch, type, subtype, param1, param2, name, &blk);

        char serial_hex[9];
        serial_to_hex(blk.serial, serial_hex);

        cJSON *item_resp = cJSON_CreateObject();
        if (ret == ESP_OK) {
            cJSON_AddStringToObject(item_resp, "response", "WRITE_OK");
            cJSON_AddNumberToObject(item_resp, "type", blk.type);
            cJSON_AddStringToObject(item_resp, "serial", serial_hex);
            success++;
        } else {
            cJSON_AddStringToObject(item_resp, "response", "ERROR");
            cJSON_AddNumberToObject(item_resp, "index", i);
            cJSON_AddStringToObject(item_resp, "message", "Write failed");
        }
        char *str = cJSON_PrintUnformatted(item_resp);
        send_response(str);
        free(str);
        cJSON_Delete(item_resp);

        // Wait for user to swap in next blank EEPROM
        if (i < count - 1) {
            ESP_LOGI(TAG, "Insert next blank EEPROM and press enter...");
            // Wait for any input char as confirmation
            while (getchar() == EOF) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }

    ESP_LOGI(TAG, "Batch complete: %d/%d succeeded", success, count);
}

static void process_command(char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) {
        send_error(0, "Invalid JSON");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cmd || !cJSON_IsString(cmd)) {
        send_error(0, "Missing 'cmd' field");
        cJSON_Delete(root);
        return;
    }

    const char *cmd_str = cmd->valuestring;

    if (strcmp(cmd_str, "WRITE_BLOCK") == 0) {
        handle_write_block(root);
    } else if (strcmp(cmd_str, "READ_BLOCK") == 0) {
        handle_read_block(root);
    } else if (strcmp(cmd_str, "ERASE_BLOCK") == 0) {
        handle_erase_block(root);
    } else if (strcmp(cmd_str, "VERIFY_BLOCK") == 0) {
        handle_verify_block(root);
    } else if (strcmp(cmd_str, "BATCH_PROGRAM") == 0) {
        handle_batch_program(root);
    } else {
        send_error(0, "Unknown command");
    }

    cJSON_Delete(root);
}

void command_task(void *arg)
{
    static char buf[CMD_BUF_SIZE];
    int pos = 0;

    ESP_LOGI(TAG, "Command handler ready");

    while (1) {
        int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (c == '\n' || c == '\r') {
            if (pos > 0) {
                buf[pos] = '\0';
                process_command(buf);
                pos = 0;
            }
            continue;
        }

        if (pos < CMD_BUF_SIZE - 1) {
            buf[pos++] = (char)c;
        } else {
            // Overflow - discard
            pos = 0;
            send_error(0, "Command too long");
        }
    }
}
