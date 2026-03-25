#pragma once

#include "ld2450.h"

/* Build the radar screen for the first time.
   Must be called after display_init(). */
void radar_ui_init(void);

/* Reposition target dots and refresh info labels.
   Must be called from the LVGL task only. */
void radar_ui_update(const ld2450_frame_t *frame);

/* Toggle sensor mount: front (origin top) ↔ back (origin bottom, X mirrored).
   Rebuilds the background; call from the LVGL task only. */
void radar_ui_toggle_mode(void);
