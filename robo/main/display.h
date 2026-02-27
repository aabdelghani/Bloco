#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef CONFIG_ROBO_DISPLAY_SSD1309
// ---- SSD1309 2.42" OLED — 128x64 monochrome, I2C ----
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64

// I2C GPIO assignments (reuses SPI clock/data pins)
#define DISPLAY_PIN_SCL  10
#define DISPLAY_PIN_SDA  11

// I2C address (typical for SSD1309 modules)
#define DISPLAY_I2C_ADDR  0x3C

// Band-buffer: one SSD1309 page = 8 pixel rows
// Each byte = 1 column of 8 vertical pixels (bit 0 = top)
#define DISPLAY_BAND_HEIGHT  8
#define DISPLAY_NUM_BANDS    (DISPLAY_HEIGHT / DISPLAY_BAND_HEIGHT)

// Initialize I2C bus and SSD1309 panel
esp_err_t display_init(void);

// Flush a page buffer (128 bytes) to the display
// y_start must be page-aligned (0, 8, 16, ...)
void display_flush(const uint8_t *buf, int y_start, int y_end);

// Fill the entire screen (0x00 = black, 0xFF = white)
void display_fill(uint8_t val);

// No-op for OLED (self-emitting)
static inline void display_set_backlight(int brightness) { (void)brightness; }

#else
// ---- GC9A01 round LCD — 240x240 RGB565, SPI ----
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

#endif
