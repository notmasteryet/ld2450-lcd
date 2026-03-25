#pragma once

#include <stdint.h>
#include <stdbool.h>

/* LD2450 reports up to 3 targets simultaneously */
#define LD2450_MAX_TARGETS 3

/* UART wiring — adjust to match your PCB */
#define LD2450_UART_PORT   UART_NUM_2
#define LD2450_PIN_RX      16   /* ESP32 RX ← sensor TX */
#define LD2450_PIN_TX      17   /* ESP32 TX → sensor RX */
#define LD2450_BAUD_RATE   256000

typedef struct {
    int16_t  x;           /* mm, negative=left, positive=right of sensor */
    int16_t  y;           /* mm, distance in front of sensor (always ≥0)  */
    int16_t  speed;       /* cm/s, negative=approaching, positive=moving away */
    uint16_t resolution;  /* mm, distance measurement resolution */
    bool     valid;       /* false when no target in this slot */
} ld2450_target_t;

typedef struct {
    ld2450_target_t targets[LD2450_MAX_TARGETS];
} ld2450_frame_t;

/* Initialise UART and start background parsing task */
void ld2450_init(void);

/* Copy the latest frame into *out.
   Returns true if the frame is newer than the last call.
   Safe to call from any task. */
bool ld2450_get_frame(ld2450_frame_t *out);
