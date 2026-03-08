#pragma once

#include "esp_err.h"
#include "esp_camera.h"

// Capture resolution for classification input
#define CAPTURE_WIDTH   320
#define CAPTURE_HEIGHT  240

// Initialize camera with board-specific pin config
esp_err_t tower_camera_init(void);

// Capture a frame (returns pointer to JPEG or RGB data)
// Caller must return the frame buffer with esp_camera_fb_return()
camera_fb_t *tower_camera_capture(void);

// Turn tower illumination on/off
void tower_light_on(void);
void tower_light_off(void);
