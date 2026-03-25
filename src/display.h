#pragma once

#include "esp_lcd_panel_ops.h"

/* TFT wiring — adjust to match your PCB */
#define DISP_PIN_SCK    18   /* SCL on display label */
#define DISP_PIN_MOSI   23   /* SDA on display label */
#define DISP_PIN_CS      5
#define DISP_PIN_DC     14
#define DISP_PIN_RST     4
#define DISP_PIN_BL     21

#define LCD_H_RES       320
#define LCD_V_RES       240
#define LCD_SPI_CLOCK_HZ (40 * 1000 * 1000)

/* Initialise SPI bus, ST7789 panel and LVGL display driver.
   Must be called before any lv_* calls. */
void display_init(void);

/* Drive the backlight GPIO high or low */
void display_backlight(bool on);
