/*
 * Motor Test — Minimal H-bridge motor test for ESP32-S3
 *
 * Drives two DC motors forward for 3 seconds, then stops.
 * Uses the same pin mapping as the Bloco robot:
 *
 *   Left motor:  IN1=GPIO4, IN2=GPIO5, EN=GPIO15
 *   Right motor: IN1=GPIO6, IN2=GPIO7, EN=GPIO16
 *
 * Change the GPIOs below if your wiring differs.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

/* ── Pin configuration ─────────────────────────────── */
#define L_IN1   4
#define L_IN2   5
#define L_EN    15

#define R_IN1   6
#define R_IN2   7
#define R_EN    16

/* ── Motor speed (0-255) ───────────────────────────── */
#define SPEED   200

/* ── Duration in milliseconds ──────────────────────── */
#define DRIVE_MS  3000

void app_main(void)
{
    printf("Motor Test — starting\n");

    /* Direction GPIOs */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << L_IN1) | (1ULL << L_IN2) |
                        (1ULL << R_IN1) | (1ULL << R_IN2),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);

    /* PWM timer */
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    /* Left motor PWM channel */
    ledc_channel_config_t ch_l = {
        .gpio_num   = L_EN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
    };
    ledc_channel_config(&ch_l);

    /* Right motor PWM channel */
    ledc_channel_config_t ch_r = {
        .gpio_num   = R_EN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_1,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
    };
    ledc_channel_config(&ch_r);

    /* ── FORWARD ──────────────────────────────────── */
    printf("Driving forward (speed %d) for %d ms...\n", SPEED, DRIVE_MS);

    /* Left motor forward */
    gpio_set_level(L_IN1, 1);
    gpio_set_level(L_IN2, 0);

    /* Right motor forward */
    gpio_set_level(R_IN1, 1);
    gpio_set_level(R_IN2, 0);

    /* Set speed */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, SPEED);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, SPEED);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

    vTaskDelay(pdMS_TO_TICKS(DRIVE_MS));

    /* ── STOP ─────────────────────────────────────── */
    printf("Stopping.\n");

    gpio_set_level(L_IN1, 0);
    gpio_set_level(L_IN2, 0);
    gpio_set_level(R_IN1, 0);
    gpio_set_level(R_IN2, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

    printf("Done.\n");
}
