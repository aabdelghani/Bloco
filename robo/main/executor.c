#include "executor.h"
#include "motor.h"
#include "eyes.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "executor";

// Default duration for movement actions (ms)
#define DEFAULT_MOVE_MS   1000
#define SPIN_MS           2000
#define SHAKE_CYCLE_MS    300
#define SHAKE_CYCLES      4
#define BEEP_MS           200

// Get parameter value from the block following a action block (if it's a param block)
static int get_param_value(const block_data_t *blocks, uint8_t count, uint8_t *pc)
{
    uint8_t next = *pc + 1;
    if (next >= count) return 1;

    uint8_t t = blocks[next].type;
    int val = 0;

    switch (t) {
    case BLOCK_PARAM_2:       val = 2; break;
    case BLOCK_PARAM_3:       val = 3; break;
    case BLOCK_PARAM_4:       val = 4; break;
    case BLOCK_PARAM_FOREVER: val = -1; break; // sentinel for infinite
    default:
        return 1; // no param block follows — use default
    }

    *pc = next; // consume the param block
    return val;
}

static void do_move(void (*move_fn)(uint8_t), int repeat)
{
    if (repeat < 0) {
        // forever — run for a long time (will be interrupted by next program)
        move_fn(MOTOR_DEFAULT_SPEED);
        vTaskDelay(pdMS_TO_TICKS(30000));
        motor_stop();
        return;
    }
    for (int i = 0; i < repeat; i++) {
        move_fn(MOTOR_DEFAULT_SPEED);
        vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
        motor_stop();
        if (i < repeat - 1) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void executor_run(const block_data_t *blocks, uint8_t count)
{
    ESP_LOGI(TAG, "=== Executing program (%d blocks) ===", count);

    uint8_t pc = 0;
    while (pc < count) {
        uint8_t type = blocks[pc].type;
        ESP_LOGI(TAG, "[%d] type=0x%02X name=%.15s", pc, type, blocks[pc].name);

        switch (type) {
        case BLOCK_BEGIN:
            eyes_set_expression(EYES_FOCUSED);
            break;

        case BLOCK_END:
            ESP_LOGI(TAG, "=== Program END ===");
            motor_stop();
            eyes_set_expression(EYES_NORMAL);
            eyes_set_look_direction(EYES_CENTER);
            return;

        case BLOCK_FORWARD: {
            int reps = get_param_value(blocks, count, &pc);
            ESP_LOGI(TAG, "  Forward x%d", reps);
            eyes_set_expression(EYES_FOCUSED);
            eyes_set_look_direction(EYES_UP);
            do_move(motor_forward, reps);
            break;
        }

        case BLOCK_BACKWARD: {
            int reps = get_param_value(blocks, count, &pc);
            ESP_LOGI(TAG, "  Backward x%d", reps);
            eyes_set_expression(EYES_FOCUSED);
            eyes_set_look_direction(EYES_DOWN);
            do_move(motor_backward, reps);
            break;
        }

        case BLOCK_TURN_RIGHT: {
            int reps = get_param_value(blocks, count, &pc);
            ESP_LOGI(TAG, "  Turn right x%d", reps);
            eyes_set_look_direction(EYES_RIGHT);
            do_move(motor_turn_right, reps);
            break;
        }

        case BLOCK_TURN_LEFT: {
            int reps = get_param_value(blocks, count, &pc);
            ESP_LOGI(TAG, "  Turn left x%d", reps);
            eyes_set_look_direction(EYES_LEFT);
            do_move(motor_turn_left, reps);
            break;
        }

        case BLOCK_SHAKE: {
            ESP_LOGI(TAG, "  Shake!");
            eyes_set_expression(EYES_EXCITED);
            for (int i = 0; i < SHAKE_CYCLES; i++) {
                motor_turn_left(MOTOR_DEFAULT_SPEED);
                vTaskDelay(pdMS_TO_TICKS(SHAKE_CYCLE_MS));
                motor_turn_right(MOTOR_DEFAULT_SPEED);
                vTaskDelay(pdMS_TO_TICKS(SHAKE_CYCLE_MS));
            }
            motor_stop();
            break;
        }

        case BLOCK_SPIN: {
            ESP_LOGI(TAG, "  Spin!");
            eyes_set_expression(EYES_SURPRISED);
            motor_spin(MOTOR_DEFAULT_SPEED);
            vTaskDelay(pdMS_TO_TICKS(SPIN_MS));
            motor_stop();
            break;
        }

        case BLOCK_REPEAT: {
            int reps = get_param_value(blocks, count, &pc);
            ESP_LOGI(TAG, "  Repeat x%d (searching for END_REPEAT)", reps);

            // Find the matching END_REPEAT
            uint8_t start_pc = pc + 1;
            uint8_t end_pc = count; // fallback
            int depth = 1;
            for (uint8_t s = start_pc; s < count; s++) {
                if (blocks[s].type == BLOCK_REPEAT) depth++;
                if (blocks[s].type == BLOCK_END_REPEAT) {
                    depth--;
                    if (depth == 0) { end_pc = s; break; }
                }
            }

            // Execute the body reps times (or a lot if forever)
            int iterations = (reps < 0) ? 1000 : reps;
            for (int r = 0; r < iterations; r++) {
                // Recursively execute the sub-program
                uint8_t body_len = end_pc - start_pc;
                if (body_len > 0) {
                    executor_run(&blocks[start_pc], body_len);
                }
            }

            pc = end_pc; // skip to END_REPEAT
            break;
        }

        case BLOCK_END_REPEAT:
            // Handled by BLOCK_REPEAT — if we hit this standalone, just skip
            break;

        case BLOCK_BEEP:
            ESP_LOGI(TAG, "  Beep! (placeholder — no speaker connected)");
            eyes_set_expression(EYES_HAPPY);
            vTaskDelay(pdMS_TO_TICKS(BEEP_MS));
            break;

        case BLOCK_SING:
        case BLOCK_PLAY_TRIANGLE:
        case BLOCK_PLAY_CIRCLE:
        case BLOCK_PLAY_SQUARE:
            ESP_LOGI(TAG, "  Sound 0x%02X (placeholder)", type);
            eyes_set_expression(EYES_HAPPY);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case BLOCK_WHITE_LIGHT_ON:
        case BLOCK_RED_LIGHT_ON:
        case BLOCK_BLUE_LIGHT_ON:
            ESP_LOGI(TAG, "  Light 0x%02X (placeholder — no LED connected)", type);
            break;

        case BLOCK_WAIT_FOR_CLAP:
            ESP_LOGI(TAG, "  Wait for clap (placeholder — waiting 2s)");
            eyes_set_expression(EYES_SURPRISED);
            vTaskDelay(pdMS_TO_TICKS(2000));
            break;

        case BLOCK_IF:
        case BLOCK_END_IF:
            ESP_LOGI(TAG, "  IF/END_IF (placeholder — skipping)");
            break;

        // Eye expressions
        case BLOCK_EYES_NORMAL:
            ESP_LOGI(TAG, "  Eyes: normal");
            eyes_set_expression(EYES_NORMAL);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_HAPPY:
            ESP_LOGI(TAG, "  Eyes: happy");
            eyes_set_expression(EYES_HAPPY);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_SAD:
            ESP_LOGI(TAG, "  Eyes: sad");
            eyes_set_expression(EYES_SAD);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_ANGRY:
            ESP_LOGI(TAG, "  Eyes: angry");
            eyes_set_expression(EYES_ANGRY);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_SURPRISED:
            ESP_LOGI(TAG, "  Eyes: surprised");
            eyes_set_expression(EYES_SURPRISED);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_SLEEPING:
            ESP_LOGI(TAG, "  Eyes: sleeping");
            eyes_set_expression(EYES_SLEEPING);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_EXCITED:
            ESP_LOGI(TAG, "  Eyes: excited");
            eyes_set_expression(EYES_EXCITED);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_FOCUSED:
            ESP_LOGI(TAG, "  Eyes: focused");
            eyes_set_expression(EYES_FOCUSED);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_SCARED:
            ESP_LOGI(TAG, "  Eyes: scared");
            eyes_set_expression(EYES_SCARED);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_CRYING:
            ESP_LOGI(TAG, "  Eyes: crying");
            eyes_set_expression(EYES_CRYING);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_CRYING_NO_TEARS:
            ESP_LOGI(TAG, "  Eyes: crying (no tears)");
            eyes_set_expression(EYES_CRYING_NO_TEARS);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_SWEATING:
            ESP_LOGI(TAG, "  Eyes: sweating");
            eyes_set_expression(EYES_SWEATING);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_DIZZY:
            ESP_LOGI(TAG, "  Eyes: dizzy");
            eyes_set_expression(EYES_DIZZY);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;

        // Eye look direction
        case BLOCK_EYES_LOOK_CENTER:
            ESP_LOGI(TAG, "  Eyes: look center");
            eyes_set_look_direction(EYES_CENTER);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_LOOK_LEFT:
            ESP_LOGI(TAG, "  Eyes: look left");
            eyes_set_look_direction(EYES_LEFT);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_LOOK_RIGHT:
            ESP_LOGI(TAG, "  Eyes: look right");
            eyes_set_look_direction(EYES_RIGHT);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_LOOK_UP:
            ESP_LOGI(TAG, "  Eyes: look up");
            eyes_set_look_direction(EYES_UP);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;
        case BLOCK_EYES_LOOK_DOWN:
            ESP_LOGI(TAG, "  Eyes: look down");
            eyes_set_look_direction(EYES_DOWN);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MOVE_MS));
            break;

        default:
            // Parameters and sensors consumed by preceding blocks or ignored standalone
            if (type >= 0x60 && type <= 0x6B) {
                ESP_LOGD(TAG, "  Standalone param 0x%02X (ignored)", type);
            } else if (type >= 0x70 && type <= 0x74) {
                ESP_LOGD(TAG, "  Sensor 0x%02X (placeholder)", type);
            } else {
                ESP_LOGW(TAG, "  Unknown block type 0x%02X — skipping", type);
            }
            break;
        }

        pc++;
    }

    motor_stop();
    eyes_set_expression(EYES_NORMAL);
    eyes_set_look_direction(EYES_CENTER);
    ESP_LOGI(TAG, "=== Program finished ===");
}
