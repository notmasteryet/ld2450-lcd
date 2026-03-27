#pragma once

#include <stdbool.h>
#include <stdint.h>

/* GPIO assignments — adjust to PCB layout */
#define BTN_BOOT_GPIO    0    /* BOOT/USER button — also flashing strapping pin */
#define BTN_DISP_GPIO   33    /* Display-control button                         */
#define SW_BACK_GPIO    27    /* Reed switch — active-LOW, internal pull-up      */

typedef enum {
    BTN_ID_MODE = 0,   /* BOOT/GPIO0  — toggle front/back sensor mode */
    BTN_ID_POWER,      /* GPIO33      — toggle display + sensor on/off */
    BTN_ID_COUNT,
} btn_id_t;

typedef enum {
    BTN_EVT_PRESS,     /* rising edge (button released, active-LOW) */
} btn_evt_type_t;

typedef struct {
    btn_id_t       id;
    btn_evt_type_t type;
} btn_evt_t;

/* Initialise both buttons and the event queue.
   Must be called before buttons_get_event(). */
void buttons_init(void);

/* Non-blocking poll: copies next event into *evt, returns true if one was waiting.
   Call from the UI task on every loop iteration. */
bool buttons_get_event(btn_evt_t *evt);

/* Returns true when the reed switch is closed (GPIO27 pulled LOW). */
bool buttons_sw_back(void);
