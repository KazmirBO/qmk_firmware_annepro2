#pragma once

#define USB_POLLING_INTERVAL_MS 1

#define MATRIX_IO_DELAY 1
#define DEBOUNCE 10

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

/* LEDs off while the USB host sleeps (annepro2.c powers down) and after 3 minutes without a key press. */
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

/* Mode after the first flash or an EEPROM reset (RGB_MATRIX_STARTUP_MODE is the old name, QMK no longer reads it). */
#define RGB_MATRIX_DEFAULT_MODE RGB_MATRIX_SOLID_COLOR

/* Bluetooth slot keys as in the stock firmware: tap connects, hold (500 ms) starts pairing. */
#define AP2_BLE_SLOT_TAP_HOLD

/* Send the battery level (ADC on PA7) to the Bluetooth MCU, so that the host can show it. The ADC is known for the
 * C18 only, so the option stays off on the C15. */
#ifdef AP2_HAS_BATTERY_ADC
#    define AP2_BLE_BATTERY
#endif
