#include "buttons.h"
#include "display.h"
#include "ld2450.h"
#include "power.h"
#include "radar_ui.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "main";

static bool s_backlight_on = true;

static void handle_button(const btn_evt_t *evt)
{
    switch (evt->id) {
    case BTN_ID_BOOT:
        /* BOOT button — toggle front/back sensor mount mode */
        radar_ui_toggle_mode();
        break;
    case BTN_ID_DISP:
        /* Display button — toggle backlight */
        s_backlight_on = !s_backlight_on;
        display_backlight(s_backlight_on);
        break;
    default:
        break;
    }
}

/* Single task that owns all LVGL calls, polls sensor data and button events */
static void ui_task(void *arg)
{
    ld2450_frame_t frame;
    btn_evt_t      btn_evt;

    while (1) {
        /* Drain all pending button events */
        while (buttons_get_event(&btn_evt)) {
            handle_button(&btn_evt);
        }

        if (ld2450_get_frame(&frame)) {
            radar_ui_update(&frame);
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ld2450-lcd starting");

    power_init();     /* GPIO32 → P-MOSFET → LD2450 */
    display_init();   /* SPI + ST7789 + LVGL        */
    radar_ui_init();  /* static radar background     */
    ld2450_init();    /* UART + background parse     */
    buttons_init();   /* GPIO0 + GPIO33 with ISR     */

    xTaskCreate(ui_task, "ui", 8192, NULL, 4, NULL);
}
