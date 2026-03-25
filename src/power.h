#pragma once

#include <stdbool.h>

/* P-MOSFET gate driver for LD2450 5V rail.
 * HIGH = NPN on = gate pulled low = MOSFET on = sensor powered.
 * LOW / float = NPN off = gate pulled to 5V = sensor off. */
#define PWR_SENSOR_GPIO  32

void power_init(void);
void power_sensor(bool on);
