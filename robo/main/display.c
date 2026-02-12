#include "display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9a01.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "display";

// SPI clock for GC9A01
#define DISPLAY_SPI_FREQ_HZ  (40 * 1000 * 1000)

// Backlight LEDC config (avoids conflict with motor TIMER_0/CH0-1)
#define BL_LEDC_TIMER    LEDC_TIMER_1
#define BL_LEDC_CHANNEL  LEDC_CHANNEL_2
#define BL_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define BL_LEDC_FREQ_HZ  5000
#define BL_LEDC_RES      LEDC_TIMER_8_BIT

static esp_lcd_panel_handle_t s_panel = NULL;
static SemaphoreHandle_t s_flush_sem = NULL;

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_sem, &woken);
    return woken == pdTRUE;
}

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_RES,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = BL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {
        .gpio_num = DISPLAY_PIN_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

esp_err_t display_init(void)
{
    s_flush_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(s_flush_sem);

    // Reset pin — drive high after pulse
    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << DISPLAY_PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_cfg);
    gpio_set_level(DISPLAY_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(DISPLAY_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // SPI bus
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = DISPLAY_PIN_SCLK,
        .mosi_io_num = DISPLAY_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_BAND_PIXELS * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // Panel IO (SPI)
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = DISPLAY_PIN_CS,
        .dc_gpio_num = DISPLAY_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = DISPLAY_SPI_FREQ_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &io));

    // GC9A01 panel driver
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,  // we handled reset manually
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io, &panel_cfg, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    // Backlight
    backlight_init();
    display_set_backlight(80);

    // Clear to black
    display_fill(COLOR_BLACK);

    ESP_LOGI(TAG, "GC9A01 display initialized (240x240, SPI %d MHz)", DISPLAY_SPI_FREQ_HZ / 1000000);
    return ESP_OK;
}

void display_flush(const uint16_t *buf, int y_start, int y_end)
{
    xSemaphoreTake(s_flush_sem, portMAX_DELAY);
    esp_lcd_panel_draw_bitmap(s_panel, 0, y_start, DISPLAY_WIDTH, y_end, buf);
}

void display_fill(uint16_t color)
{
    static uint16_t fill_buf[DISPLAY_BAND_PIXELS];
    for (int i = 0; i < DISPLAY_BAND_PIXELS; i++) {
        fill_buf[i] = color;
    }
    for (int band = 0; band < DISPLAY_NUM_BANDS; band++) {
        int y = band * DISPLAY_BAND_HEIGHT;
        display_flush(fill_buf, y, y + DISPLAY_BAND_HEIGHT);
    }
}

void display_set_backlight(int brightness)
{
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    uint32_t duty = (uint32_t)brightness * 255 / 100;
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}
