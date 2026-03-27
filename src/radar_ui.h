#pragma once

#include "ld2450.h"

/* Build the radar screen for the first time.
   Must be called after display_init(). */
void radar_ui_init(void);

/* Reposition target dots and refresh info labels.
   Must be called from the LVGL task only. */
void radar_ui_update(const ld2450_frame_t *frame);

/* Set sensor mount: back=true → origin bottom, X mirrored (MODE_BACK).
   No-op if mode is already correct. Call from the LVGL task only. */
void radar_ui_set_mode(bool back);

/* Toggle display scale between 6 m and 3 m (2× zoom). */
void radar_ui_toggle_scale(void);
