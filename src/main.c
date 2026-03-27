#include "buttons.h"
#include "display.h"
#include "ld2450.h"
#include "power.h"
#include "radar_ui.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "main";

static void enter_sleep(void)
{
    display_backlight(false);
    power_sensor(false);
    gpio_hold_en((gpio_num_t)DISP_PIN_BL);       /* hold BL LOW through sleep */
    gpio_hold_en((gpio_num_t)PWR_SENSOR_GPIO);   /* hold GPIO32 LOW through sleep */
    /* ext0 is level-triggered: wait for button release before sleeping,
       otherwise LOW pin wakes the chip immediately */
    while (gpio_get_level((gpio_num_t)BTN_DISP_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(50));  /* debounce margin */
    /* Wake on POWER button (active-LOW, GPIO33 = RTC_GPIO8) */
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_DISP_GPIO, 0);
    ESP_LOGI(TAG, "entering deep sleep");
    esp_deep_sleep_start();
}

static void handle_button(const btn_evt_t *evt)
{
    switch (evt->id) {
    case BTN_ID_MODE:
        radar_ui_toggle_scale();
        break;
    case BTN_ID_POWER:
        /* Power button — sleep now; button press will wake via reset */
        enter_sleep();
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
        while (buttons_get_event(&btn_evt)) {
            handle_button(&btn_evt);
        }

        radar_ui_set_mode(!buttons_sw_back());  /* closed=LOW → front, open=HIGH → back */

        if (ld2450_get_frame(&frame)) {
            radar_ui_update(&frame);
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ld2450-lcd starting (wake cause: %d)",
             esp_sleep_get_wakeup_cause());

    gpio_hold_dis((gpio_num_t)DISP_PIN_BL);      /* release hold before display init */
    gpio_hold_dis((gpio_num_t)PWR_SENSOR_GPIO);  /* release hold before driving HIGH */
    power_init();     /* TPS27081ADDCR → LD2450 on   */
    display_init();   /* SPI + ST7789 + LVGL          */
    radar_ui_init();  /* static radar background      */
    ld2450_init();    /* UART + background parse       */
    buttons_init();   /* GPIO0 + GPIO33 with ISR       */

    xTaskCreate(ui_task, "ui", 8192, NULL, 4, NULL);
}
