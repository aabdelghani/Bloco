#pragma once

#include "esp_err.h"
#include <stdint.h>

// GC9A01 round LCD — 240x240 RGB565
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240

// SPI GPIO assignments
#define DISPLAY_PIN_SCLK  10
#define DISPLAY_PIN_MOSI  11
#define DISPLAY_PIN_CS    12
#define DISPLAY_PIN_DC    13
#define DISPLAY_PIN_RST   14
#define DISPLAY_PIN_BL    21

// RGB565 color helpers
#define RGB565(r, g, b) ((uint16_t)(((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | ((b) >> 3)))
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF

// Band-buffer rendering: 240 x 30 pixel strips
#define DISPLAY_BAND_HEIGHT  30
#define DISPLAY_BAND_PIXELS  (DISPLAY_WIDTH * DISPLAY_BAND_HEIGHT)
#define DISPLAY_NUM_BANDS    (DISPLAY_HEIGHT / DISPLAY_BAND_HEIGHT)

// Initialize SPI bus, GC9A01 panel, and backlight
esp_err_t display_init(void);

// Flush a band buffer to the display (blocking, waits for DMA)
// y_start: first row of the band (0, 30, 60, ...)
void display_flush(const uint16_t *buf, int y_start, int y_end);

// Fill the entire screen with a single color
void display_fill(uint16_t color);

// Set backlight brightness (0-100)
void display_set_backlight(int brightness);
