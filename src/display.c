#include "display.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "display";

#define LVGL_DRAW_BUF_LINES  20
#define LVGL_TICK_PERIOD_MS   1

#define BL_LEDC_TIMER    LEDC_TIMER_0
#define BL_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BL_LEDC_DUTY_RES LEDC_TIMER_10_BIT   /* 0–1023 */
#define BL_LEDC_FREQ_HZ  5000

static lv_disp_drv_t    s_disp_drv;
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t       s_buf1[LCD_H_RES * LVGL_DRAW_BUF_LINES];
static lv_color_t       s_buf2[LCD_H_RES * LVGL_DRAW_BUF_LINES];

static esp_lcd_panel_handle_t s_panel;

/* Called by LVGL when a rendered region is ready to send to the display */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = drv->user_data;
    esp_lcd_panel_draw_bitmap(panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              color_map);
    lv_disp_flush_ready(drv);
}

/* esp_timer callback — advances the LVGL tick counter */
static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

void display_init(void)
{
    /* ---- SPI bus ---- */
    const spi_bus_config_t bus_cfg = {
        .mosi_io_num     = DISP_PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = DISP_PIN_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * sizeof(lv_color_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    /* ---- Panel IO (SPI → LCD command/data) ---- */
    esp_lcd_panel_io_handle_t io_handle;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = DISP_PIN_DC,
        .cs_gpio_num       = DISP_PIN_CS,
        .pclk_hz           = LCD_SPI_CLOCK_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                             &io_cfg, &io_handle));

    /* ---- ST7789 panel ---- */
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = DISP_PIN_RST,
        .color_space    = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &s_panel));

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    /* Landscape: swap X/Y then mirror X.
       If the image is upside-down or mirrored, toggle mirror_x / mirror_y. */
    esp_lcd_panel_swap_xy(s_panel, true);
    esp_lcd_panel_mirror(s_panel, true, false);
    /* ST7789 needs color inversion on most modules */
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_set_gap(s_panel, 0, 0);
    esp_lcd_panel_disp_on_off(s_panel, true);

    /* ---- Backlight via LEDC PWM ---- */
    const ledc_timer_config_t bl_timer = {
        .speed_mode      = BL_LEDC_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_DUTY_RES,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&bl_timer));

    const ledc_channel_config_t bl_channel = {
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = DISP_PIN_BL,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&bl_channel));
    display_set_brightness(DISP_BRIGHTNESS_HIGH);

    /* ---- LVGL init ---- */
    lv_init();
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2,
                          LCD_H_RES * LVGL_DRAW_BUF_LINES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res   = LCD_H_RES;
    s_disp_drv.ver_res   = LCD_V_RES;
    s_disp_drv.flush_cb  = lvgl_flush_cb;
    s_disp_drv.draw_buf  = &s_draw_buf;
    s_disp_drv.user_data = s_panel;
    lv_disp_drv_register(&s_disp_drv);

    /* ---- LVGL tick timer (1 ms) ---- */
    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_cb,
        .name     = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer,
                                             LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "ST7789 %dx%d ready", LCD_H_RES, LCD_V_RES);
}

void display_set_brightness(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = ((1 << BL_LEDC_DUTY_RES) - 1) * percent / 100;
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}
