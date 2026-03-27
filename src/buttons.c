#include "buttons.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

static const char *TAG = "buttons";

/* Both buttons are active-LOW (pressing pulls the pin to GND).
 * GPIO0 has an on-board pull-up on DevKit boards.
 * GPIO33 uses the internal pull-up. */

#define DEBOUNCE_MS   50
#define LONGPRESS_MS  5000

static QueueHandle_t s_evt_queue;

typedef struct {
    btn_id_t      id;
    gpio_num_t    gpio;
    TimerHandle_t debounce_timer;
    TimerHandle_t longpress_timer;  /* NULL if no long-press on this button */
} btn_ctx_t;

static btn_ctx_t s_buttons[BTN_ID_COUNT] = {
    [BTN_ID_MODE]  = { .id = BTN_ID_MODE,  .gpio = BTN_BOOT_GPIO },
    [BTN_ID_POWER] = { .id = BTN_ID_POWER, .gpio = BTN_DISP_GPIO },
};

/* Called from the debounce timer (timer task context, not ISR).
   Confirms the pin is still LOW before sending a PRESS event. */
static void debounce_expired(TimerHandle_t timer)
{
    btn_ctx_t *ctx = pvTimerGetTimerID(timer);
    if (gpio_get_level(ctx->gpio) == 0) {
        btn_evt_t evt = { .id = ctx->id, .type = BTN_EVT_PRESS };
        xQueueSend(s_evt_queue, &evt, 0);
    }
}

/* Called from the long-press timer after LONGPRESS_MS with button still held */
static void longpress_expired(TimerHandle_t timer)
{
    btn_ctx_t *ctx = pvTimerGetTimerID(timer);
    btn_evt_t evt = { .id = ctx->id, .type = BTN_EVT_LONG_PRESS };
    xQueueSend(s_evt_queue, &evt, 0);
}

/* ISR — fires on falling or rising edge depending on button config */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    btn_ctx_t *ctx = (btn_ctx_t *)arg;
    BaseType_t woken = pdFALSE;

    if (gpio_get_level(ctx->gpio) == 0) {
        /* Falling edge — button pressed: start debounce and long-press timers */
        xTimerResetFromISR(ctx->debounce_timer, &woken);
        if (ctx->longpress_timer) {
            xTimerResetFromISR(ctx->longpress_timer, &woken);
        }
    } else {
        /* Rising edge — button released: cancel long-press if not yet fired */
        if (ctx->longpress_timer) {
            xTimerStopFromISR(ctx->longpress_timer, &woken);
        }
    }
    portYIELD_FROM_ISR(woken);
}

void buttons_init(void)
{
    s_evt_queue = xQueueCreate(8, sizeof(btn_evt_t));

    /* Reed switch — input only, no ISR */
    gpio_config_t sw_cfg = {
        .pin_bit_mask = 1ULL << SW_BACK_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&sw_cfg);

    /* Long-press timer for power button only */
    s_buttons[BTN_ID_POWER].longpress_timer = xTimerCreate(
        "btn_lp",
        pdMS_TO_TICKS(LONGPRESS_MS),
        pdFALSE,   /* one-shot */
        &s_buttons[BTN_ID_POWER],
        longpress_expired
    );

    gpio_install_isr_service(0);

    for (int i = 0; i < BTN_ID_COUNT; i++) {
        btn_ctx_t *ctx = &s_buttons[i];

        /* Power button needs ANYEDGE to detect release for long-press cancel */
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << ctx->gpio,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = ctx->longpress_timer ? GPIO_INTR_ANYEDGE
                                                 : GPIO_INTR_NEGEDGE,
        };
        gpio_config(&cfg);

        ctx->debounce_timer = xTimerCreate(
            "btn_db",
            pdMS_TO_TICKS(DEBOUNCE_MS),
            pdFALSE,   /* one-shot */
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

bool buttons_sw_back(void)
{
    return gpio_get_level(SW_BACK_GPIO) == 0;  /* active-LOW */
}
