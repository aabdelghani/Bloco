#pragma once

#include "driver/gpio.h"

// Status LED pin - adjust to your hardware
#define LED_STATUS_PIN  GPIO_NUM_48

typedef enum {
    LED_STATE_IDLE,        // Off
    LED_STATE_PROGRAMMING, // Blue blink
    LED_STATE_SUCCESS,     // Green flash
    LED_STATE_ERROR,       // Red flash
} led_state_t;

void led_init(void);
void led_set_state(led_state_t state);
void led_task(void *arg);
