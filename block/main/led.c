#include "led.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Use the onboard RGB LED (WS2812 on GPIO48 for most ESP32-S3 dev boards)
static led_strip_handle_t strip;
static volatile led_state_t current_state = LED_STATE_IDLE;

void led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_STATUS_PIN,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip);
    led_strip_clear(strip);
}

void led_set_state(led_state_t state)
{
    current_state = state;
}

void led_task(void *arg)
{
    int flash_count = 0;

    while (1) {
        switch (current_state) {
        case LED_STATE_IDLE:
            led_strip_clear(strip);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;

        case LED_STATE_PROGRAMMING:
            led_strip_set_pixel(strip, 0, 0, 0, 30); // Blue
            led_strip_refresh(strip);
            vTaskDelay(pdMS_TO_TICKS(150));
            led_strip_clear(strip);
            vTaskDelay(pdMS_TO_TICKS(150));
            break;

        case LED_STATE_SUCCESS:
            led_strip_set_pixel(strip, 0, 0, 30, 0); // Green
            led_strip_refresh(strip);
            vTaskDelay(pdMS_TO_TICKS(500));
            led_strip_clear(strip);
            flash_count++;
            if (flash_count >= 3) {
                flash_count = 0;
                current_state = LED_STATE_IDLE;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            break;

        case LED_STATE_ERROR:
            led_strip_set_pixel(strip, 0, 30, 0, 0); // Red
            led_strip_refresh(strip);
            vTaskDelay(pdMS_TO_TICKS(500));
            led_strip_clear(strip);
            flash_count++;
            if (flash_count >= 3) {
                flash_count = 0;
                current_state = LED_STATE_IDLE;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
        }
    }
}
