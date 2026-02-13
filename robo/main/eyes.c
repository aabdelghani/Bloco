#include "eyes.h"
#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>

static const char *TAG = "eyes";

// ---------------------------------------------------------------------------
// Geometry parameters for one eye
// ---------------------------------------------------------------------------
// Overlay effect drawn on top of eye shapes
typedef enum {
    OVERLAY_NONE = 0,
    OVERLAY_TEARS,
    OVERLAY_SWEAT,
} overlay_type_t;

typedef struct {
    int16_t eye_w;       // half-width of eye rounded rect
    int16_t eye_h;       // half-height of eye rounded rect
    int16_t eye_r;       // corner radius
    int16_t lid_top;     // top eyelid y offset (positive = more closed)
    int16_t lid_bot;     // bottom eyelid y offset (positive = more closed)
    int16_t lid_tilt;    // tilt for top lid (positive = inner edge lower)
    int16_t pupil_w;     // pupil ellipse half-width
    int16_t pupil_h;     // pupil ellipse half-height
} eye_keyframe_t;

// Full expression keyframe (both eyes share same shape, may differ in tilt)
typedef struct {
    eye_keyframe_t eye;
    int16_t lid_tilt_r;  // right eye tilt override (0 = mirror of left)
    overlay_type_t overlay;  // tear drops, sweat, etc.
} expression_keyframe_t;

// ---------------------------------------------------------------------------
// Expression keyframe table
// ---------------------------------------------------------------------------
// Eye center positions
#define EYE_SPACING  38   // half-distance between eye centers
#define EYE_CY       120  // vertical center on 240px display

// clang-format off
static const expression_keyframe_t s_keyframes[EYES_EXPRESSION_COUNT] = {
#ifdef CONFIG_ROBO_EYES_STYLE_PUPIL
    // --- With pupils: classic cartoon eyes ---
    [EYES_NORMAL] = {
        .eye = { .eye_w = 30, .eye_h = 34, .eye_r = 14,
                 .lid_top = 0, .lid_bot = 0, .lid_tilt = 0,
                 .pupil_w = 10, .pupil_h = 12 },
    },
    [EYES_HAPPY] = {
        .eye = { .eye_w = 30, .eye_h = 34, .eye_r = 14,
                 .lid_top = 0, .lid_bot = 18, .lid_tilt = 0,
                 .pupil_w = 10, .pupil_h = 12 },
    },
    [EYES_SAD] = {
        .eye = { .eye_w = 28, .eye_h = 30, .eye_r = 12,
                 .lid_top = 8, .lid_bot = 0, .lid_tilt = -8,
                 .pupil_w = 11, .pupil_h = 13 },
        .lid_tilt_r = 8,
    },
    [EYES_ANGRY] = {
        .eye = { .eye_w = 34, .eye_h = 26, .eye_r = 6,
                 .lid_top = 18, .lid_bot = 0, .lid_tilt = 14,
                 .pupil_w = 7, .pupil_h = 8 },
        .lid_tilt_r = -14,
    },
    [EYES_SURPRISED] = {
        .eye = { .eye_w = 34, .eye_h = 38, .eye_r = 18,
                 .lid_top = 0, .lid_bot = 0, .lid_tilt = 0,
                 .pupil_w = 7, .pupil_h = 8 },
    },
    [EYES_SLEEPING] = {
        .eye = { .eye_w = 28, .eye_h = 34, .eye_r = 14,
                 .lid_top = 30, .lid_bot = 0, .lid_tilt = 0,
                 .pupil_w = 10, .pupil_h = 12 },
    },
    [EYES_EXCITED] = {
        .eye = { .eye_w = 34, .eye_h = 36, .eye_r = 16,
                 .lid_top = 0, .lid_bot = 0, .lid_tilt = 0,
                 .pupil_w = 11, .pupil_h = 13 },
    },
    [EYES_FOCUSED] = {
        .eye = { .eye_w = 28, .eye_h = 28, .eye_r = 12,
                 .lid_top = 4, .lid_bot = 4, .lid_tilt = 0,
                 .pupil_w = 13, .pupil_h = 14 },
    },
    [EYES_SCARED] = {
        .eye = { .eye_w = 32, .eye_h = 36, .eye_r = 16,
                 .lid_top = 0, .lid_bot = 0, .lid_tilt = -4,
                 .pupil_w = 6, .pupil_h = 6 },
        .lid_tilt_r = 4,
    },
    [EYES_CRYING] = {
        .eye = { .eye_w = 28, .eye_h = 30, .eye_r = 12,
                 .lid_top = 10, .lid_bot = 14, .lid_tilt = -6,
                 .pupil_w = 10, .pupil_h = 12 },
        .lid_tilt_r = 6,
        .overlay = OVERLAY_TEARS,
    },
    [EYES_CRYING_NO_TEARS] = {
        .eye = { .eye_w = 28, .eye_h = 30, .eye_r = 12,
                 .lid_top = 10, .lid_bot = 14, .lid_tilt = -6,
                 .pupil_w = 10, .pupil_h = 12 },
        .lid_tilt_r = 6,
    },
    [EYES_SWEATING] = {
        .eye = { .eye_w = 30, .eye_h = 32, .eye_r = 14,
                 .lid_top = 2, .lid_bot = 0, .lid_tilt = -3,
                 .pupil_w = 8, .pupil_h = 9 },
        .lid_tilt_r = 3,
        .overlay = OVERLAY_SWEAT,
    },
    [EYES_DIZZY] = {
        .eye = { .eye_w = 32, .eye_h = 32, .eye_r = 16,
                 .lid_top = 0, .lid_bot = 0, .lid_tilt = 0,
                 .pupil_w = 0, .pupil_h = 0 },
    },
#else
    // --- Solid: no pupils, emotion via shape only ---
    [EYES_NORMAL] = {
        .eye = { .eye_w = 30, .eye_h = 34, .eye_r = 14,
                 .lid_top = 0, .lid_bot = 0, .lid_tilt = 0 },
    },
    [EYES_HAPPY] = {
        .eye = { .eye_w = 32, .eye_h = 34, .eye_r = 16,
                 .lid_top = 0, .lid_bot = 24, .lid_tilt = 0 },
    },
    [EYES_SAD] = {
        .eye = { .eye_w = 26, .eye_h = 28, .eye_r = 12,
                 .lid_top = 12, .lid_bot = 0, .lid_tilt = -10 },
        .lid_tilt_r = 10,
    },
    [EYES_ANGRY] = {
        .eye = { .eye_w = 34, .eye_h = 24, .eye_r = 4,
                 .lid_top = 20, .lid_bot = 4, .lid_tilt = 16 },
        .lid_tilt_r = -16,
    },
    [EYES_SURPRISED] = {
        .eye = { .eye_w = 36, .eye_h = 40, .eye_r = 20,
                 .lid_top = 0, .lid_bot = 0, .lid_tilt = 0 },
    },
    [EYES_SLEEPING] = {
        .eye = { .eye_w = 28, .eye_h = 34, .eye_r = 14,
                 .lid_top = 32, .lid_bot = 0, .lid_tilt = 0 },
    },
    [EYES_EXCITED] = {
        .eye = { .eye_w = 36, .eye_h = 38, .eye_r = 18,
                 .lid_top = 0, .lid_bot = 0, .lid_tilt = 0 },
    },
    [EYES_FOCUSED] = {
        .eye = { .eye_w = 28, .eye_h = 28, .eye_r = 10,
                 .lid_top = 8, .lid_bot = 8, .lid_tilt = 0 },
    },
    [EYES_SCARED] = {
        .eye = { .eye_w = 32, .eye_h = 36, .eye_r = 16,
                 .lid_top = 0, .lid_bot = 0, .lid_tilt = -4 },
        .lid_tilt_r = 4,
    },
    [EYES_CRYING] = {
        .eye = { .eye_w = 28, .eye_h = 30, .eye_r = 12,
                 .lid_top = 10, .lid_bot = 14, .lid_tilt = -6 },
        .lid_tilt_r = 6,
        .overlay = OVERLAY_TEARS,
    },
    [EYES_CRYING_NO_TEARS] = {
        .eye = { .eye_w = 28, .eye_h = 30, .eye_r = 12,
                 .lid_top = 10, .lid_bot = 14, .lid_tilt = -6 },
        .lid_tilt_r = 6,
    },
    [EYES_SWEATING] = {
        .eye = { .eye_w = 30, .eye_h = 32, .eye_r = 14,
                 .lid_top = 2, .lid_bot = 0, .lid_tilt = -3 },
        .lid_tilt_r = 3,
        .overlay = OVERLAY_SWEAT,
    },
    [EYES_DIZZY] = {
        .eye = { .eye_w = 32, .eye_h = 32, .eye_r = 16,
                 .lid_top = 0, .lid_bot = 0, .lid_tilt = 0 },
    },
#endif
};

// ---------------------------------------------------------------------------
// Look-direction pupil offsets
// ---------------------------------------------------------------------------
typedef struct {
    int16_t dx;
    int16_t dy;
} look_offset_t;

static const look_offset_t s_look_offsets[] = {
    [EYES_CENTER] = {  0,  0 },
    [EYES_LEFT]   = { -10,  0 },
    [EYES_RIGHT]  = {  10,  0 },
    [EYES_UP]     = {  0, -8 },
    [EYES_DOWN]   = {  0,  8 },
};

// ---------------------------------------------------------------------------
// Animation state
// ---------------------------------------------------------------------------
#define TRANSITION_MS     250
#define BLINK_CLOSE_MS     80
#define BLINK_OPEN_MS     120
#define FPS                 30
#define FRAME_MS           (1000 / FPS)

// Interpolated state (fixed-point x256 for smooth transitions)
typedef struct {
    int32_t eye_w, eye_h, eye_r;
    int32_t lid_top, lid_bot, lid_tilt_l, lid_tilt_r;
    int32_t pupil_w, pupil_h;
    int32_t pupil_dx, pupil_dy;
    int32_t blink_lid;  // extra top-lid closure for blink
    overlay_type_t overlay;
} anim_state_t;

static anim_state_t s_current;   // current interpolated state (x256)
static anim_state_t s_target;    // target state (x256)
static int32_t s_transition_remaining;  // ms left in transition

static eyes_expression_t s_expr = EYES_NORMAL;
static eyes_look_dir_t s_look = EYES_CENTER;

// Blink state machine
typedef enum { BLINK_IDLE, BLINK_CLOSING, BLINK_OPENING } blink_phase_t;
static blink_phase_t s_blink_phase = BLINK_IDLE;
static int32_t s_blink_timer;
static int32_t s_auto_blink_timer;
static bool s_blink_requested;

// Tear animation
static int32_t s_tear_y_offset;  // animated tear drop y position (0-30)
#define TEAR_SPEED  1   // pixels per frame
#define TEAR_RANGE  30  // tear falls this many pixels then resets

// Dizzy spiral animation
static int32_t s_dizzy_angle;  // 0-359 degrees, advances each frame
#define DIZZY_SPEED  12  // degrees per frame

// Blue color for tears/sweat (RGB565, pre-byte-swapped for SPI)
// Light blue: R=80, G=160, B=255
#define COLOR_BLUE  0xFC54

// Idle sleep timer
#define EYES_IDLE_TIMEOUT_MS  (60 * 1000)   // go to sleep after this much idle
static int32_t s_idle_timer;
static bool s_is_sleeping;

// Band buffer (allocated once)
static uint16_t s_band_buf[DISPLAY_BAND_PIXELS];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int32_t lerp(int32_t a, int32_t b, int32_t t256)
{
    return a + ((b - a) * t256) / 256;
}

static int32_t random_range(int32_t lo, int32_t hi)
{
    return lo + (int32_t)(esp_random() % (uint32_t)(hi - lo + 1));
}

static void set_target_from_expression(void)
{
    const expression_keyframe_t *kf = &s_keyframes[s_expr];
    const look_offset_t *look = &s_look_offsets[s_look];

    s_target.eye_w = kf->eye.eye_w << 8;
    s_target.eye_h = kf->eye.eye_h << 8;
    s_target.eye_r = kf->eye.eye_r << 8;
    s_target.lid_top = kf->eye.lid_top << 8;
    s_target.lid_bot = kf->eye.lid_bot << 8;
    s_target.lid_tilt_l = kf->eye.lid_tilt << 8;
    s_target.lid_tilt_r = (kf->lid_tilt_r ? kf->lid_tilt_r : -kf->eye.lid_tilt) << 8;
    s_target.pupil_w = kf->eye.pupil_w << 8;
    s_target.pupil_h = kf->eye.pupil_h << 8;
    s_target.pupil_dx = look->dx << 8;
    s_target.pupil_dy = look->dy << 8;
    s_target.blink_lid = 0;
    s_target.overlay = kf->overlay;
}

static void snap_current_to_target(void)
{
    s_current = s_target;
    s_transition_remaining = 0;
}

// ---------------------------------------------------------------------------
// Per-pixel rendering
// ---------------------------------------------------------------------------

// Check if pixel (px, py) is inside a rounded rectangle centered at (cx, cy)
// with half-size (hw, hh) and corner radius r.  All values in pixels.
static inline bool in_rounded_rect(int px, int py, int cx, int cy,
                                   int hw, int hh, int r)
{
    int dx = px - cx;
    int dy = py - cy;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    if (dx > hw || dy > hh) return false;

    // Inside the non-rounded region?
    if (dx <= hw - r || dy <= hh - r) return true;

    // Corner check: (dx - (hw-r))^2 + (dy - (hh-r))^2 <= r^2
    int cx2 = dx - (hw - r);
    int cy2 = dy - (hh - r);
    return (cx2 * cx2 + cy2 * cy2) <= (r * r);
}

// Check if pixel is inside an axis-aligned ellipse centered at (cx, cy)
// with semi-axes (a, b).  Uses integer multiply to avoid division.
static inline bool in_ellipse(int px, int py, int cx, int cy, int a, int b)
{
    int dx = px - cx;
    int dy = py - cy;
    // dx^2 * b^2 + dy^2 * a^2 <= a^2 * b^2
    return (dx * dx * b * b + dy * dy * a * a) <= (a * a * b * b);
}

static void render_band(int band_y)
{
    // Extract pixel-level values from fixed-point state
    int ew  = (int)(s_current.eye_w >> 8);
    int eh  = (int)(s_current.eye_h >> 8);
    int er  = (int)(s_current.eye_r >> 8);
    int lt  = (int)(s_current.lid_top >> 8);
    int lb  = (int)(s_current.lid_bot >> 8);
    int tilt_l = (int)(s_current.lid_tilt_l >> 8);
    int tilt_r = (int)(s_current.lid_tilt_r >> 8);
    int pw  = (int)(s_current.pupil_w >> 8);
    int ph  = (int)(s_current.pupil_h >> 8);
    int pdx = (int)(s_current.pupil_dx >> 8);
    int pdy = (int)(s_current.pupil_dy >> 8);
    int blink = (int)(s_current.blink_lid >> 8);

    // Total top lid = expression lid + blink
    int total_lid_top = lt + blink;

    // Eye centers
    int left_cx  = DISPLAY_WIDTH / 2 - EYE_SPACING;
    int right_cx = DISPLAY_WIDTH / 2 + EYE_SPACING;
    int cy = EYE_CY;

    uint16_t *p = s_band_buf;

    for (int row = 0; row < DISPLAY_BAND_HEIGHT; row++) {
        int y = band_y + row;

        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            uint16_t color = COLOR_BLACK;

            // Test both eyes
            for (int side = 0; side < 2; side++) {
                int ecx = (side == 0) ? left_cx : right_cx;
                int tilt = (side == 0) ? tilt_l : tilt_r;

                if (!in_rounded_rect(x, y, ecx, cy, ew, eh, er))
                    continue;

                // Eyelid clipping: top lid
                // lid_y = cy - eh + total_lid_top + tilt * (x - ecx) / ew
                int lid_top_y = cy - eh + total_lid_top;
                if (ew > 0) {
                    lid_top_y += tilt * (x - ecx) / ew;
                }
                if (y < lid_top_y) continue;

                // Bottom lid
                int lid_bot_y = cy + eh - lb;
                if (y > lid_bot_y) continue;

#ifdef CONFIG_ROBO_EYES_STYLE_PUPIL
                // Check if inside pupil
                {
                    int pcx = ecx + pdx;
                    int pcy = cy + pdy;
                    color = (pw > 0 && ph > 0 && in_ellipse(x, y, pcx, pcy, pw, ph))
                            ? COLOR_BLACK : COLOR_WHITE;
                }
#else
                color = COLOR_WHITE;
#endif
            }

            *p++ = color;
        }
    }

    // --- Overlay effects ---
    overlay_type_t ov = s_current.overlay;

    if (ov == OVERLAY_TEARS) {
        // Two tear drops below each eye, animated falling
        int tear_centers[2] = { left_cx, right_cx };
        int tear_base_y = cy + eh + 4;
        int ty = tear_base_y + (int)s_tear_y_offset;

        p = s_band_buf;
        for (int row = 0; row < DISPLAY_BAND_HEIGHT; row++) {
            int y = band_y + row;
            for (int x = 0; x < DISPLAY_WIDTH; x++) {
                for (int t = 0; t < 2; t++) {
                    int tcx = tear_centers[t];
                    // Tear drop: small ellipse (3x5 pixels)
                    if (in_ellipse(x, y, tcx, ty, 3, 5)) {
                        *p = COLOR_BLUE;
                    }
                    // Second tear slightly offset
                    if (in_ellipse(x, y, tcx + 8, ty + 8, 2, 4)) {
                        *p = COLOR_BLUE;
                    }
                }
                p++;
            }
        }
    }

    if (ov == OVERLAY_SWEAT) {
        // Single sweat drop on right side of face
        int sx = right_cx + ew + 6;
        int sy = cy - eh + 10;

        p = s_band_buf;
        for (int row = 0; row < DISPLAY_BAND_HEIGHT; row++) {
            int y = band_y + row;
            for (int x = 0; x < DISPLAY_WIDTH; x++) {
                // Tear-drop shape: ellipse bottom + triangle top
                if (in_ellipse(x, y, sx, sy + 4, 4, 5)) {
                    *p = COLOR_BLUE;
                } else if (x >= sx - 1 && x <= sx + 1 && y >= sy - 4 && y <= sy) {
                    *p = COLOR_BLUE;
                }
                p++;
            }
        }
    }

    if (s_expr == EYES_DIZZY) {
        // Draw X shapes over each eye instead of normal content
        int centers[2] = { left_cx, right_cx };
        int xsize = ew > 16 ? 16 : ew;

        p = s_band_buf;
        for (int row = 0; row < DISPLAY_BAND_HEIGHT; row++) {
            int y = band_y + row;
            for (int x = 0; x < DISPLAY_WIDTH; x++) {
                for (int e = 0; e < 2; e++) {
                    int ecx = centers[e];
                    int dx = x - ecx;
                    int dy = y - cy;
                    // Two diagonal lines forming an X, thickness ~3px
                    if (dx >= -xsize && dx <= xsize && dy >= -xsize && dy <= xsize) {
                        int d1 = dx - dy;  // diagonal 1
                        int d2 = dx + dy;  // diagonal 2
                        if (d1 < 0) d1 = -d1;
                        if (d2 < 0) d2 = -d2;
                        if (d1 <= 2 || d2 <= 2) {
                            *p = COLOR_WHITE;
                        }
                    }
                }
                p++;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Animation tick (one frame)
// ---------------------------------------------------------------------------

static void advance_transition(int32_t dt_ms)
{
    if (s_transition_remaining <= 0) {
        s_current = s_target;
        return;
    }

    int32_t t256;
    if (s_transition_remaining <= dt_ms) {
        t256 = 256;
        s_transition_remaining = 0;
    } else {
        t256 = (dt_ms << 8) / s_transition_remaining;
        s_transition_remaining -= dt_ms;
    }

    s_current.eye_w    = lerp(s_current.eye_w,    s_target.eye_w,    t256);
    s_current.eye_h    = lerp(s_current.eye_h,    s_target.eye_h,    t256);
    s_current.eye_r    = lerp(s_current.eye_r,    s_target.eye_r,    t256);
    s_current.lid_top  = lerp(s_current.lid_top,  s_target.lid_top,  t256);
    s_current.lid_bot  = lerp(s_current.lid_bot,  s_target.lid_bot,  t256);
    s_current.lid_tilt_l = lerp(s_current.lid_tilt_l, s_target.lid_tilt_l, t256);
    s_current.lid_tilt_r = lerp(s_current.lid_tilt_r, s_target.lid_tilt_r, t256);
    s_current.pupil_w  = lerp(s_current.pupil_w,  s_target.pupil_w,  t256);
    s_current.pupil_h  = lerp(s_current.pupil_h,  s_target.pupil_h,  t256);
    s_current.pupil_dx = lerp(s_current.pupil_dx, s_target.pupil_dx, t256);
    s_current.pupil_dy = lerp(s_current.pupil_dy, s_target.pupil_dy, t256);
    s_current.blink_lid = lerp(s_current.blink_lid, s_target.blink_lid, t256);
    s_current.overlay = s_target.overlay;
}

static void advance_blink(int32_t dt_ms)
{
    // Auto-blink timer
    s_auto_blink_timer -= dt_ms;
    if (s_auto_blink_timer <= 0 || s_blink_requested) {
        if (s_blink_phase == BLINK_IDLE) {
            s_blink_phase = BLINK_CLOSING;
            s_blink_timer = BLINK_CLOSE_MS;
        }
        s_blink_requested = false;
        s_auto_blink_timer = random_range(2000, 6000);
    }

    switch (s_blink_phase) {
    case BLINK_IDLE:
        s_current.blink_lid = 0;
        break;
    case BLINK_CLOSING:
        s_blink_timer -= dt_ms;
        // Full closure = 2 * eye_h (enough to cover entire eye)
        s_current.blink_lid = (int32_t)(70 << 8) *
            (BLINK_CLOSE_MS - s_blink_timer) / BLINK_CLOSE_MS;
        if (s_blink_timer <= 0) {
            s_blink_phase = BLINK_OPENING;
            s_blink_timer = BLINK_OPEN_MS;
        }
        break;
    case BLINK_OPENING:
        s_blink_timer -= dt_ms;
        s_current.blink_lid = (int32_t)(70 << 8) *
            s_blink_timer / BLINK_OPEN_MS;
        if (s_blink_timer <= 0) {
            s_blink_phase = BLINK_IDLE;
            s_current.blink_lid = 0;
        }
        break;
    }
}

static void advance_idle(int32_t dt_ms)
{
    if (s_is_sleeping) return;

    s_idle_timer += dt_ms;
    if (s_idle_timer >= EYES_IDLE_TIMEOUT_MS) {
        s_is_sleeping = true;
        s_expr = EYES_SLEEPING;
        s_look = EYES_CENTER;
        set_target_from_expression();
        s_transition_remaining = 500;  // slow drowsy transition
        ESP_LOGI(TAG, "Idle timeout — falling asleep");
    }
}

static void eyes_tick(void)
{
    advance_idle(FRAME_MS);
    advance_transition(FRAME_MS);
    advance_blink(FRAME_MS);

    // Advance tear drop animation
    if (s_current.overlay == OVERLAY_TEARS) {
        s_tear_y_offset += TEAR_SPEED;
        if (s_tear_y_offset >= TEAR_RANGE) s_tear_y_offset = 0;
    }
    // Advance dizzy angle
    if (s_expr == EYES_DIZZY) {
        s_dizzy_angle = (s_dizzy_angle + DIZZY_SPEED) % 360;
    }

    for (int band = 0; band < DISPLAY_NUM_BANDS; band++) {
        int y = band * DISPLAY_BAND_HEIGHT;
        render_band(y);
        display_flush(s_band_buf, y, y + DISPLAY_BAND_HEIGHT);
    }
}

// ---------------------------------------------------------------------------
// FreeRTOS task
// ---------------------------------------------------------------------------

static void eyes_task(void *arg)
{
    while (1) {
        TickType_t start = xTaskGetTickCount();
        eyes_tick();
        TickType_t elapsed = xTaskGetTickCount() - start;
        TickType_t target = pdMS_TO_TICKS(FRAME_MS);
        // Always yield at least 1 tick so IDLE can feed the watchdog
        vTaskDelay((elapsed < target) ? (target - elapsed) : 1);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void eyes_init(void)
{
    s_expr = EYES_NORMAL;
    s_look = EYES_CENTER;
    set_target_from_expression();
    snap_current_to_target();
    s_auto_blink_timer = random_range(1000, 3000);

    xTaskCreate(eyes_task, "eyes", 4096, NULL, 1, NULL);
    ESP_LOGI(TAG, "Eyes animation started (30 fps, band-buffer rendering)");
}

void eyes_set_expression(eyes_expression_t expr)
{
    if (expr >= EYES_EXPRESSION_COUNT) return;
    s_idle_timer = 0;
    s_is_sleeping = false;
    s_expr = expr;
    set_target_from_expression();
    s_transition_remaining = TRANSITION_MS;
    ESP_LOGD(TAG, "Expression -> %d", expr);
}

void eyes_set_look_direction(eyes_look_dir_t dir)
{
#ifdef CONFIG_ROBO_EYES_STYLE_PUPIL
    s_look = dir;
    set_target_from_expression();
    s_transition_remaining = TRANSITION_MS;
    ESP_LOGD(TAG, "Look -> %d", dir);
#else
    (void)dir;
#endif
}

void eyes_blink(void)
{
    s_blink_requested = true;
}
