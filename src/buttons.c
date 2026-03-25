#include "buttons.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

static const char *TAG = "buttons";

/* Both buttons are active-LOW (pressing pulls the pin to GND).
 * GPIO0 has an on-board pull-up on DevKit boards.
 * GPIO33 uses the internal pull-up (no external resistor needed on PCB,
 * though a 10k external is good practice for noise immunity). */

#define DEBOUNCE_MS  50

static QueueHandle_t s_evt_queue;

typedef struct {
    btn_id_t     id;
    gpio_num_t   gpio;
    TimerHandle_t debounce_timer;
} btn_ctx_t;

static btn_ctx_t s_buttons[BTN_ID_COUNT] = {
    [BTN_ID_BOOT] = { .id = BTN_ID_BOOT, .gpio = BTN_BOOT_GPIO },
    [BTN_ID_DISP] = { .id = BTN_ID_DISP, .gpio = BTN_DISP_GPIO },
};

/* Called from the debounce timer (timer task context, not ISR).
   Checks the pin level is still LOW before firing the event. */
static void debounce_expired(TimerHandle_t timer)
{
    btn_ctx_t *ctx = pvTimerGetTimerID(timer);

    /* Confirm pin is still pressed (LOW) after debounce window */
    if (gpio_get_level(ctx->gpio) == 0) {
        btn_evt_t evt = { .id = ctx->id, .type = BTN_EVT_PRESS };
        xQueueSend(s_evt_queue, &evt, 0);
    }
}

/* ISR — fires on falling edge (button press) */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    btn_ctx_t *ctx = (btn_ctx_t *)arg;
    /* Reset and start the debounce one-shot timer from ISR */
    BaseType_t woken = pdFALSE;
    xTimerResetFromISR(ctx->debounce_timer, &woken);
    portYIELD_FROM_ISR(woken);
}

void buttons_init(void)
{
    s_evt_queue = xQueueCreate(8, sizeof(btn_evt_t));

    gpio_install_isr_service(0);

    for (int i = 0; i < BTN_ID_COUNT; i++) {
        btn_ctx_t *ctx = &s_buttons[i];

        /* GPIO0 already has an external pull-up on DevKit; still safe to
         * enable the internal one on custom PCBs without an external resistor. */
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << ctx->gpio,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_NEGEDGE,  /* falling edge = button press */
        };
        gpio_config(&cfg);

        ctx->debounce_timer = xTimerCreate(
            "btn_db",
            pdMS_TO_TICKS(DEBOUNCE_MS),
            pdFALSE,        /* one-shot */
            ctx,
            debounce_expired
        );

        gpio_isr_handler_add(ctx->gpio, gpio_isr_handler, ctx);

        ESP_LOGI(TAG, "button %d on GPIO%d ready", i, ctx->gpio);
    }
}

bool buttons_get_event(btn_evt_t *evt)
{
    return xQueueReceive(s_evt_queue, evt, 0) == pdTRUE;
}
