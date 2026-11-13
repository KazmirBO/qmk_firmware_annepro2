/* Copyright 2026 KazmirBO
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

#define USB_POLLING_INTERVAL_MS 1

#define MATRIX_IO_DELAY 1
/* A key change counts once it has been stable for 20 ms (DEBOUNCE_TYPE in rules.mk); this hides the W switch
 * chatter, at the cost of 20 ms more latency on press and release. */
#define DEBOUNCE 20

#define TAPPING_TERM 100
/* Caps Lock (hold = FN1) uses a longer term, see get_tapping_term in keymap.c. */
#define TAPPING_TERM_PER_KEY
#define CAPS_FN1_TAPPING_TERM 250
/* A tap followed by a hold of the same dual-role key always activates the hold action. */
#define QUICK_TAP_TERM 0
#define HOLD_ON_OTHER_KEY_PRESS

#define NO_ACTION_ONESHOT
#define NO_MUSIC_MODE

#define RGB_MATRIX_MAXIMUM_BRIGHTNESS 255

/* LEDs off while the USB host sleeps (annepro2.c powers down) and after 3 minutes without a key press; the LED MCU is
 * switched off then too. */
#define RGB_MATRIX_SLEEP
#define RGB_MATRIX_TIMEOUT 180000

/* The keyboard info.json enables these effects; this keymap does not use them. This file is included after the
 * generated info_config.h, so #undef removes them from the build. */
#undef ENABLE_RGB_MATRIX_MULTISPLASH
#undef ENABLE_RGB_MATRIX_SPLASH
#undef ENABLE_RGB_MATRIX_SOLID_SPLASH
#undef ENABLE_RGB_MATRIX_SOLID_MULTISPLASH
#undef ENABLE_RGB_MATRIX_DIGITAL_RAIN
#undef ENABLE_RGB_MATRIX_TYPING_HEATMAP
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS

/* The C15 has 8 KB of RAM. Since QMK's USB control buffer grew to a whole packet (62 bytes more), this keymap is 36 bytes
 * over there. Pixel Flow keeps 183 bytes of state, so the C15 goes without it. The C18 keeps the effect, and with it the
 * numbers of the modes saved in EEPROM. */
#ifdef KEYBOARD_annepro2_c15
#    undef ENABLE_RGB_MATRIX_PIXEL_FLOW
#endif

/* Mode after the first flash or an EEPROM reset (RGB_MATRIX_STARTUP_MODE is the old name, QMK no longer reads it). */
#define RGB_MATRIX_DEFAULT_MODE RGB_MATRIX_SOLID_COLOR

/* Bluetooth slot keys as in the stock firmware: tap connects, hold starts pairing. The stock firmware waits 5 s;
 * this keymap keeps the default AP2_BLE_SLOT_HOLD_MS of 500 ms. */
#define AP2_BLE_SLOT_TAP_HOLD

/* Follow the Bluetooth switch on the bottom of the keyboard (PC10): on selects the Bluetooth output, off the USB
 * output. Only a change of the switch selects the output, so KC_AP2_USB and the slot keys still work in between. The
 * C15 has no PC10, so the option stays off there. */
#ifdef AP2_HAS_BT_SWITCH
#    define AP2_BT_SWITCH
#endif

/* Send the battery level (ADC on PA7) to the Bluetooth MCU, so that the host can show it. The ADC is known for the
 * C18 only, so the option stays off on the C15. */
#ifdef AP2_HAS_BATTERY_ADC
#    define AP2_BLE_BATTERY
#endif

/* Give the Bluetooth MCU the UDID of the stock device record as its serial number. The record is known for the C18
 * only, so the option stays off on the C15. */
#ifdef AP2_DEVICE_RECORD_ADDR
#    define AP2_BLE_SERIAL
#endif
