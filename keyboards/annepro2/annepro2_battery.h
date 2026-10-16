/*
 * Battery level of the Anne Pro 2 (C18).
 *
 * The battery is sampled by ADC channel 7 on PA7. The settings follow the stock firmware:
 * ADC clock HCLK/64, one conversion triggered by software, 12-bit result, trimmed mean of 8 samples.
 * The raw reading is sent to the BLE MCU (01 05), which turns it into the Battery Service level,
 * and KC_AP2_BATT shows it on the number row.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Interval between two readings sent to the BLE MCU. */
#ifndef AP2_BATTERY_INTERVAL_MS
#    define AP2_BATTERY_INTERVAL_MS 60000
#endif

/* The BLE MCU keeps the lowest level it was sent until it restarts, so a single low reading (for example while the LEDs
 * draw a lot of current) would stay on the host. The highest of the last AP2_BATTERY_HISTORY readings is sent instead,
 * so a lasting drop reaches the host after that many readings (5 minutes with the default interval). */
#ifndef AP2_BATTERY_HISTORY
#    define AP2_BATTERY_HISTORY 5
#endif

/* How long KC_AP2_BATT keeps the level on the number row. */
#ifndef AP2_BATTERY_SHOW_MS
#    define AP2_BATTERY_SHOW_MS 3000
#endif

/* The BLE MCU converts the raw reading with U = raw * 2 * 3.1 / 4095. If the real reference voltage
 * or divider differ, scale the reading with NUM/DEN before it is used (default: unchanged). */
#ifndef AP2_BATTERY_SCALE_NUM
#    define AP2_BATTERY_SCALE_NUM 1
#endif
#ifndef AP2_BATTERY_SCALE_DEN
#    define AP2_BATTERY_SCALE_DEN 1
#endif

/* The BLE MCU shows 0 % below 3.4 V, but the cell can be used longer. Readings between this voltage and 3.6 V are
 * stretched so that the host and the indicator keep showing a low level down to AP2_BATTERY_EMPTY_MV and show 0 % only
 * below it. The curve above 3.6 V does not change. Set it to 3400 or more to turn the correction off. */
#ifndef AP2_BATTERY_EMPTY_MV
#    define AP2_BATTERY_EMPTY_MV 3200
#endif

#ifdef AP2_BLE_BATTERY

#    ifndef AP2_HAS_BATTERY_ADC
#        error "AP2_BLE_BATTERY needs the battery ADC on PA7, which is known for the C18 only"
#    endif

void     annepro2_battery_init(void);
bool     annepro2_battery_read(uint16_t *raw);   /* trimmed mean of 8 samples, false on timeout */
uint16_t annepro2_battery_scaled(uint16_t raw);  /* calibrated 12-bit reading: the scale and the low end correction */
uint8_t  annepro2_battery_percent(uint16_t raw); /* 0-100 for a calibrated reading, the curve of the stock firmware */

void annepro2_battery_task(void);           /* periodic send to the BLE MCU and hiding of the indicator */
void annepro2_battery_request_send(void);   /* send the next reading right away, e.g. after a connection */
void annepro2_battery_toggle_display(void); /* KC_AP2_BATT */

#else /* the option is off: the callers compile to nothing */

static inline void annepro2_battery_init(void) {}
static inline void annepro2_battery_task(void) {}
static inline void annepro2_battery_request_send(void) {}
static inline void annepro2_battery_toggle_display(void) {}

#endif
