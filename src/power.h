#pragma once

#include <stdbool.h>

/* TPS27081ADDCR load switch for LD2450 5V rail.
 * EN pin is active-HIGH: HIGH = sensor powered, LOW/float = sensor off. */
#define PWR_SENSOR_GPIO  32

void power_init(void);
void power_sensor(bool on);
