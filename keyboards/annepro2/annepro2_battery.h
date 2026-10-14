/*
 * Battery voltage measurement for the Anne Pro 2 (C18).
 *
 * The battery is sampled by ADC channel 7 on PA7. The values below follow the stock firmware:
 * ADC clock HCLK/64, one conversion triggered by software, 12-bit result. The stock firmware
 * averages 8 samples without the smallest and the largest one.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Interval between two readings sent to the BLE MCU. */
#ifndef AP2_BATTERY_INTERVAL_MS
#    define AP2_BATTERY_INTERVAL_MS 60000
#endif

/* The BLE MCU converts the raw reading with U = raw * 2 * 3.1 / 4095. If the real reference voltage
 * or divider differ, scale the reading with NUM/DEN before it is sent (default: unchanged). */
#ifndef AP2_BATTERY_SCALE_NUM
#    define AP2_BATTERY_SCALE_NUM 1
#endif
#ifndef AP2_BATTERY_SCALE_DEN
#    define AP2_BATTERY_SCALE_DEN 1
#endif

void    annepro2_battery_init(void);
bool    annepro2_battery_read(uint16_t *raw);       /* trimmed mean of 8 samples, false on timeout */
uint8_t annepro2_battery_percent(uint16_t raw);     /* 0-100, the curve used by the stock firmware */
