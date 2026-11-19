/* Copyright 2021 OpenAnnePro community
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  */

#pragma once

#include "protocol.h"

// Struct defining an LED and its RGB color components
// Compatible with Shine firmware.
typedef union {
    struct {
        /* Little endian ordering to match uint32_t */
        uint8_t blue, green, red;
        /* Used in mask; nonzero means - use color from mask. */
        uint8_t alpha;
    } p; /* parts */
    /* Parts vector access: 0 - blue, 1 - green, 2 - red */
    uint8_t pv[4];
    /* 0xrgb in mem is b g r a */
    uint32_t rgb;
} ap2_led_t;

#define ROWCOL2IDX(row, col) (NUM_COLUMN * (row) + (col))
#define NUM_COLUMN 14
#define NUM_ROW 5
#define KEY_COUNT 70

/* Local copy of led_mask, used to override colors on the board */
extern ap2_led_t led_mask[KEY_COUNT];
extern ap2_led_t led_colors[KEY_COUNT];

/* Handle incoming messages */
extern void led_command_callback(const message_t *msg);

void ap2_set_IAP(void);
void ap2_led_disable(void);
void ap2_led_enable(void);
void ap2_led_set_profile(uint8_t prof);
void ap2_led_next_profile(void);
void ap2_led_prev_profile(void);
void ap2_led_next_intensity(void);
void ap2_led_next_animation_speed(void);
void ap2_led_forward_keypress(uint8_t row, uint8_t col);

/* Set single key to a given color; alpha controls which is displayed */
void ap2_led_mask_set_key(uint8_t row, uint8_t col, ap2_led_t color);
/* Push a whole local row to the shine */
void ap2_led_mask_set_row(uint8_t row);
/* Synchronize all rows */
void ap2_led_mask_set_all(void);

/* Set all keys to a given color */
void ap2_led_mask_set_mono(ap2_led_t color);

/* Set single key to a given color; alpha controls which is displayed */
void ap2_led_colors_set_key(uint8_t row, uint8_t col, ap2_led_t color);
/* Push a whole local row to the shine */
void ap2_led_colors_set_row(uint8_t row);
/* Synchronize all rows */
void ap2_led_colors_set_all(void);

/* Set all keys to a given color */
void ap2_led_colors_set_mono(ap2_led_t color);

void ap2_led_set_manual_control(uint8_t manual);

/* Bytes free in the output queue of the LED UART; a message that fits is written without waiting. */
uint16_t ap2_led_tx_space(void);
/* Room that the long or periodic messages (rgb_matrix rows, the battery indicator, the LED MCU check) leave in that
 * queue for the short commands, which are written at once. The longest burst is one key press that switches the LEDs
 * on in a layer: 30 01 (6 B), LED on (3 copies, 15 B), the mask reset and the layer colour (9 B each) and the key
 * itself (6 B), 45 B. A layer key that resolves in the same scan (hold on other key press) can put its 9-byte layer
 * colour in front of that burst, 54 B in all; the write then waits for at most 6 bytes, about half a millisecond. */
#ifndef AP2_LED_TX_RESERVE
#    define AP2_LED_TX_RESERVE 48
#endif
/* The LED MCU (Shine) reads its UART into a 32-byte input queue, which lasts 2.8 ms at 115200 baud, in the thread that
 * also runs the commands. A blink stops that thread for up to 10 ms while it waits for the old blink thread, and the
 * reply to a second status command in a row (LED on right after manual mode) for about 3 ms while its own 32-byte
 * output queue drains. ap2_led_hold_rx() is called after such a command: the rgb_matrix rows and the battery
 * indicator then wait until `ms` after the command has left the queue here, so that they do not overflow the LED MCU's
 * input queue. */
#ifndef AP2_LED_BLINK_HOLD_MS
#    define AP2_LED_BLINK_HOLD_MS 11
#endif
#ifndef AP2_LED_POWER_HOLD_MS
#    define AP2_LED_POWER_HOLD_MS 8
#endif
void ap2_led_hold_rx(uint8_t ms);
bool ap2_led_rx_held(void);
/* rgb_matrix shows something: enabled, not dark while the USB host sleeps, not past RGB_MATRIX_TIMEOUT
 * (rgb_driver.c). */
bool ap2_rgb_lit(void);
/* Send the rgb_matrix rows that did not fit in the LED UART at the last flush (rgb_driver.c); call it every scan. */
void ap2_rgb_flush_pending(void);
/* Send every row again at the next flush, e.g. after the LED MCU was turned on (rgb_driver.c). */
void ap2_rgb_resend_all(void);

/* Blink given key `count` times by masking it with a `color`. Each of the 2 * count phases (on, off) lasts `hundredths`
 * hundredths of a second. */
void ap2_led_blink(uint8_t row, uint8_t col, ap2_led_t color, uint8_t count, uint8_t hundredths);
/* Cancel any running blinking */
void ap2_led_cancel_blink(void);

/* Kept for compatibility, but implemented using masks */
void ap2_led_set_foreground_color(uint8_t red, uint8_t green, uint8_t blue);
void ap2_led_reset_foreground_color(void);

void ap2_led_sticky_set_key(uint8_t row, uint8_t col, ap2_led_t color);
void ap2_led_unset_sticky_key(uint8_t row, uint8_t col);
void ap2_led_unset_sticky_row(uint8_t row);
void ap2_led_unset_sticky_all(void);

typedef struct {
    uint8_t amount_of_profiles;
    uint8_t current_profile;
    uint8_t matrix_enabled;
    uint8_t is_reactive;
    uint8_t led_intensity;
    uint8_t errors;
} ap2_led_status_t;

extern ap2_led_status_t ap2_led_status;

