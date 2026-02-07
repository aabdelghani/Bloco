#include "motor.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "motor";

// LEDC channels for PWM
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_FREQ_HZ        1000
#define LEDC_RESOLUTION     LEDC_TIMER_8_BIT
#define LEDC_CH_LEFT        LEDC_CHANNEL_0
#define LEDC_CH_RIGHT       LEDC_CHANNEL_1

static void set_direction(int in1, int in2, int dir)
{
    // dir: 1 = forward, -1 = backward, 0 = brake
    if (dir > 0) {
        gpio_set_level(in1, 1);
        gpio_set_level(in2, 0);
    } else if (dir < 0) {
        gpio_set_level(in1, 0);
        gpio_set_level(in2, 1);
    } else {
        gpio_set_level(in1, 0);
        gpio_set_level(in2, 0);
    }
}

esp_err_t motor_init(void)
{
    // Configure direction GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR_L_IN1) | (1ULL << MOTOR_L_IN2) |
                        (1ULL << MOTOR_R_IN1) | (1ULL << MOTOR_R_IN2),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    // LEDC timer
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    // Left motor PWM
    ledc_channel_config_t ch_left = {
        .gpio_num = MOTOR_L_EN,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CH_LEFT,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_left));

    // Right motor PWM
    ledc_channel_config_t ch_right = {
        .gpio_num = MOTOR_R_EN,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CH_RIGHT,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_right));

    motor_stop();
    ESP_LOGI(TAG, "Motors initialized (L: GPIO %d/%d EN %d, R: GPIO %d/%d EN %d)",
             MOTOR_L_IN1, MOTOR_L_IN2, MOTOR_L_EN,
             MOTOR_R_IN1, MOTOR_R_IN2, MOTOR_R_EN);
    return ESP_OK;
}

static void set_motors(int left_dir, int right_dir, uint8_t speed)
{
    set_direction(MOTOR_L_IN1, MOTOR_L_IN2, left_dir);
    set_direction(MOTOR_R_IN1, MOTOR_R_IN2, right_dir);
    ledc_set_duty(LEDC_MODE, LEDC_CH_LEFT, speed);
    ledc_set_duty(LEDC_MODE, LEDC_CH_RIGHT, speed);
    ledc_update_duty(LEDC_MODE, LEDC_CH_LEFT);
    ledc_update_duty(LEDC_MODE, LEDC_CH_RIGHT);
}

void motor_forward(uint8_t speed)
{
    ESP_LOGI(TAG, "Forward (speed %d)", speed);
    set_motors(1, 1, speed);
}

void motor_backward(uint8_t speed)
{
    ESP_LOGI(TAG, "Backward (speed %d)", speed);
    set_motors(-1, -1, speed);
}

void motor_turn_right(uint8_t speed)
{
    ESP_LOGI(TAG, "Turn right (speed %d)", speed);
    set_motors(1, -1, speed);
}

void motor_turn_left(uint8_t speed)
{
    ESP_LOGI(TAG, "Turn left (speed %d)", speed);
    set_motors(-1, 1, speed);
}

void motor_spin(uint8_t speed)
{
    ESP_LOGI(TAG, "Spin (speed %d)", speed);
    set_motors(1, -1, speed);
}

void motor_stop(void)
{
    set_direction(MOTOR_L_IN1, MOTOR_L_IN2, 0);
    set_direction(MOTOR_R_IN1, MOTOR_R_IN2, 0);
    ledc_set_duty(LEDC_MODE, LEDC_CH_LEFT, 0);
    ledc_set_duty(LEDC_MODE, LEDC_CH_RIGHT, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CH_LEFT);
    ledc_update_duty(LEDC_MODE, LEDC_CH_RIGHT);
}
