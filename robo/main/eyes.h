#pragma once

typedef enum {
    EYES_NORMAL = 0,
    EYES_HAPPY,
    EYES_SAD,
    EYES_ANGRY,
    EYES_SURPRISED,
    EYES_SLEEPING,
    EYES_EXCITED,
    EYES_FOCUSED,
    EYES_SCARED,
    EYES_CRYING,
    EYES_CRYING_NO_TEARS,
    EYES_SWEATING,
    EYES_DIZZY,
    EYES_EXPRESSION_COUNT
} eyes_expression_t;

typedef enum {
    EYES_CENTER = 0,
    EYES_LEFT,
    EYES_RIGHT,
    EYES_UP,
    EYES_DOWN,
} eyes_look_dir_t;

// Start the eyes rendering task (call after display_init)
void eyes_init(void);

// Set target expression (smooth transition over ~250ms)
void eyes_set_expression(eyes_expression_t expr);

// Set look direction (pupil movement — only effective with CONFIG_ROBO_EYES_STYLE_PUPIL)
void eyes_set_look_direction(eyes_look_dir_t dir);

// Get current expression (for polling from other tasks)
eyes_expression_t eyes_get_expression(void);

// Trigger a single blink
void eyes_blink(void);
