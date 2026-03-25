#include "power.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "power";

void power_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PWR_SENSOR_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    /* Start with sensor on */
    gpio_set_level(PWR_SENSOR_GPIO, 1);
    ESP_LOGI(TAG, "sensor power on (GPIO%d)", PWR_SENSOR_GPIO);
}

void power_sensor(bool on)
{
    gpio_set_level(PWR_SENSOR_GPIO, on ? 1 : 0);
    ESP_LOGI(TAG, "sensor power %s", on ? "on" : "off");
}
