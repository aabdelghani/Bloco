#pragma once

#include "esp_err.h"
#include <stdint.h>

// Motor GPIO pins (2 DC motors, differential drive)
// Left motor
#define MOTOR_L_IN1  4
#define MOTOR_L_IN2  5
#define MOTOR_L_EN   15

// Right motor
#define MOTOR_R_IN1  6
#define MOTOR_R_IN2  7
#define MOTOR_R_EN   16

// Default speed (0-255 maps to 0-100% duty)
#define MOTOR_DEFAULT_SPEED  200

esp_err_t motor_init(void);
void motor_forward(uint8_t speed);
void motor_backward(uint8_t speed);
void motor_turn_right(uint8_t speed);
void motor_turn_left(uint8_t speed);
void motor_spin(uint8_t speed);
void motor_stop(void);
