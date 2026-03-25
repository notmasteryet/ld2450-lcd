#include "ld2450.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "ld2450";

/* LD2450 data frame layout (30 bytes total):
 *  [0..3]  header : AA FF 03 00
 *  [4..11] target1: x(2) y(2) speed(2) resolution(2)
 *  [12..19] target2
 *  [20..27] target3
 *  [28..29] tail  : 55 CC
 *
 * Coordinate encoding — sign-magnitude, little-endian:
 *   bit15 = 1 → positive value
 *   bit15 = 0 → negative value
 *   bits[14:0] = magnitude in mm
 * If targets read mirrored or inverted, flip the sign in decode_coord(). */

#define FRAME_LEN      30
#define FRAME_HDR0     0xAA
#define FRAME_HDR1     0xFF
#define FRAME_HDR2     0x03
#define FRAME_HDR3     0x00
#define FRAME_TAIL0    0x55
#define FRAME_TAIL1    0xCC
#define DATA_LEN       24  /* 3 targets × 8 bytes */

static SemaphoreHandle_t s_mutex;
static ld2450_frame_t    s_frame;
static bool              s_new_frame;

static int16_t decode_coord(uint8_t lo, uint8_t hi)
{
    uint16_t raw = (uint16_t)lo | ((uint16_t)hi << 8);
    int16_t  mag = (int16_t)(raw & 0x7FFF);
    return (raw & 0x8000) ? mag : -mag;
}

static int16_t decode_speed(uint8_t lo, uint8_t hi)
{
    /* Speed uses the same sign-magnitude encoding.
       Positive = moving away from sensor, negative = approaching. */
    return decode_coord(lo, hi);
}

static void parse_frame(const uint8_t *data)
{
    ld2450_frame_t frame;

    for (int i = 0; i < LD2450_MAX_TARGETS; i++) {
        const uint8_t *t = data + i * 8;
        frame.targets[i].x          = decode_coord(t[0], t[1]);
        frame.targets[i].y          = decode_coord(t[2], t[3]);
        frame.targets[i].speed      = decode_speed(t[4], t[5]);
        frame.targets[i].resolution = (uint16_t)t[6] | ((uint16_t)t[7] << 8);
        /* A slot with all-zero data means no target */
        frame.targets[i].valid = (frame.targets[i].x != 0 ||
                                  frame.targets[i].y != 0 ||
                                  frame.targets[i].resolution != 0);
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_frame     = frame;
    s_new_frame = true;
    xSemaphoreGive(s_mutex);
}

static void ld2450_task(void *arg)
{
    typedef enum {
        S_HDR0, S_HDR1, S_HDR2, S_HDR3,
        S_DATA,
        S_TAIL0, S_TAIL1
    } state_t;

    state_t state   = S_HDR0;
    uint8_t data[DATA_LEN];
    int     data_idx = 0;
    uint8_t byte;

    while (1) {
        int n = uart_read_bytes(LD2450_UART_PORT, &byte, 1, portMAX_DELAY);
        if (n <= 0) continue;

        switch (state) {
        case S_HDR0: if (byte == FRAME_HDR0) state = S_HDR1; break;
        case S_HDR1: state = (byte == FRAME_HDR1) ? S_HDR2 : S_HDR0; break;
        case S_HDR2: state = (byte == FRAME_HDR2) ? S_HDR3 : S_HDR0; break;
        case S_HDR3:
            if (byte == FRAME_HDR3) {
                data_idx = 0;
                state = S_DATA;
            } else {
                state = S_HDR0;
            }
            break;
        case S_DATA:
            data[data_idx++] = byte;
            if (data_idx == DATA_LEN) state = S_TAIL0;
            break;
        case S_TAIL0:
            state = (byte == FRAME_TAIL0) ? S_TAIL1 : S_HDR0;
            break;
        case S_TAIL1:
            if (byte == FRAME_TAIL1) parse_frame(data);
            state = S_HDR0;
            break;
        }
    }
}

void ld2450_init(void)
{
    s_mutex     = xSemaphoreCreateMutex();
    s_new_frame = false;

    const uart_config_t uart_cfg = {
        .baud_rate  = LD2450_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(LD2450_UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(LD2450_UART_PORT,
                                 LD2450_PIN_TX, LD2450_PIN_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    /* Ring buffer large enough for several frames */
    ESP_ERROR_CHECK(uart_driver_install(LD2450_UART_PORT,
                                        FRAME_LEN * 8, 0, 0, NULL, 0));

    xTaskCreate(ld2450_task, "ld2450", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "initialised on UART%d rx=%d tx=%d",
             LD2450_UART_PORT, LD2450_PIN_RX, LD2450_PIN_TX);
}

bool ld2450_get_frame(ld2450_frame_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool fresh  = s_new_frame;
    *out        = s_frame;
    s_new_frame = false;
    xSemaphoreGive(s_mutex);
    return fresh;
}
