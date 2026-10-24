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

#include <string.h>
#include "hal.h"
#include "annepro2.h"
#include "annepro2_ble.h"
#include "annepro2_battery.h"
#include "ap2_led.h"
#include "protocol.h"
#include "suspend.h"
#include "usb_main.h"

#define RAM_MAGIC_LOCATION 0x20001ffc
#define IAP_MAGIC_VALUE 0x0000fab2

static const SerialConfig uart_115200_config = {
    .speed = 115200,
};

#ifndef LED_UART_BAUD_RATE
#    define LED_UART_BAUD_RATE 115200
#endif  // LED_UART_BAUD_RATE

static const SerialConfig led_uart_runtime_config = {
    .speed = LED_UART_BAUD_RATE,
};

static const uint8_t led_mcu_wakeup[11] = {0x7b, 0x10, 0x43, 0x10, 0x03, 0x00, 0x00, 0x7d, 0x02, 0x01, 0x02};

/* Handshake state with the BLE MCU. The stock firmware pings the BLE MCU (02 01 02 and 01 03 02)
 * every 50 ms five times and then every 500 ms until the BLE MCU answers with its version. */
#ifndef AP2_BLE_HANDSHAKE_TRIES
#    define AP2_BLE_HANDSHAKE_TRIES 24
#endif
static bool     ble_ready;
static uint16_t ble_version;
static uint8_t  ble_ping_count;
static uint32_t ble_ping_timer;

bool     annepro2_ble_is_ready(void) { return ble_ready; }
uint16_t annepro2_ble_version(void) { return ble_version; }

static const ap2_led_t ap2_blue = {
    .p.blue  = 0xff,
    .p.red   = 0x00,
    .p.green = 0x00,
    .p.alpha = 0xff,
};

static void forward_reactive_keypress(const keyrecord_t *record) {
    if (ap2_led_status.matrix_enabled && ap2_led_status.is_reactive) {
        ap2_led_forward_keypress(record->event.key.row, record->event.key.col);
    }
}

/* A frame that stops arriving for this long is dropped. */
#ifndef AP2_BLE_RX_TIMEOUT_MS
#    define AP2_BLE_RX_TIMEOUT_MS 100
#endif

static uint8_t  ble_rx[BLE_RX_HEADER_SIZE + BLE_RX_BODY_MAX];
static uint8_t  ble_rx_pos;
static uint8_t  ble_rx_skip; /* bytes of a long frame still to drop */
static uint32_t ble_rx_timer;

/* The header is validated by ble_rx_byte() before the body is read. */
static void ble_handle_frame(const uint8_t *f, uint16_t len) {
    /* The BLE MCU also forwards frames from a host to the other nodes; only its own frames to this MCU count. A
     * fragmented frame counts only when it is complete in itself (one fragment, index 0). */
    if (f[BLE_RX_ROUTE] != BLE_ROUTE_FROM_BLE) return;
    if (f[BLE_RX_TYPE] == BLE_TYPE_FRAGMENT && f[BLE_RX_SEQ] != 0x10) return;
    if (len < 3) return; /* no data byte */
    const uint8_t cmd = f[BLE_RX_CMD], sub = f[BLE_RX_SUB];
    const uint8_t d0  = f[BLE_RX_DATA];

    switch (cmd) {
        case BLE_CMD_LED:
            switch (sub) {
                case BLE_STS_ID_CONNECT:
                    ap2_led_cancel_blink();
                    annepro2_ble_host_connected();
                    annepro2_battery_request_send();
                    break;
                case BLE_STS_ID_CAPSLOCK:
                    if (d0 <= 1) annepro2_ble_set_capslock(d0); /* the BLE MCU sends 0 or 1 */
                    break;
                default:
                    break;
            }
            break;
        case BLE_CMD_GLOBAL:
            if (sub == BLE_GLOBAL_VERSION) {
                ble_version = ((uint16_t)(d0 >> 4) << 8) | (d0 & 0x0f); /* 0x25 -> 0x0205 (2.05) */
                ble_ready   = true;
            }
            break;
        default:
            break;
    }
}

static uint16_t ble_rx_len(void) { return ble_rx[BLE_RX_LEN_LO] | ((uint16_t)ble_rx[BLE_RX_LEN_HI] << 8); }

/* Whether the bytes received so far (ble_rx[0] is always the sync byte) can be the start of a frame. The stock
 * firmware accepts only the two frame types, and the header ends with the sync byte and gives a length of at most
 * BLE_RX_SKIP_MAX. A sync byte in the body of a frame that is held here means that a byte of that frame was lost and
 * the next frame has started: no frame handled here carries 0x7b. */
static bool ble_rx_valid(void) {
    if (ble_rx_pos > BLE_RX_TYPE && ble_rx[BLE_RX_TYPE] != BLE_TYPE_SINGLE && ble_rx[BLE_RX_TYPE] != BLE_TYPE_FRAGMENT) return false;
    if (ble_rx_pos < BLE_RX_HEADER_SIZE) return true;
    const uint16_t len = ble_rx_len();
    if (ble_rx[BLE_RX_END] != BLE_SYNC_END || len < 2 || len > BLE_RX_SKIP_MAX) return false;
    return ble_rx_pos == BLE_RX_HEADER_SIZE || ble_rx[ble_rx_pos - 1] != BLE_SYNC_START;
}

/* Drop the bytes before the next sync byte, so that a frame starting inside a rejected one is not lost. */
static void ble_rx_resync(void) {
    uint8_t i = 1;
    while (i < ble_rx_pos && ble_rx[i] != BLE_SYNC_START) i++;
    memmove(ble_rx, ble_rx + i, ble_rx_pos - i);
    ble_rx_pos -= i;
}

/* Feed one byte from the BLE MCU into the frame parser. */
static void ble_rx_byte(uint8_t byte) {
    if (ble_rx_skip) {
        ble_rx_skip--;
        return;
    }
    if (ble_rx_pos == 0 && byte != BLE_SYNC_START) return;
    ble_rx[ble_rx_pos++] = byte;
    while (ble_rx_pos > 0 && !ble_rx_valid()) ble_rx_resync();
    /* A rejected frame leaves at most 7 bytes, so a header can only be complete when the byte appended here ends it. */
    if (ble_rx_pos < BLE_RX_HEADER_SIZE) return;
    const uint16_t len = ble_rx_len();
    if (len > BLE_RX_BODY_MAX) {
        /* Longer than any frame handled here: drop it by its length, so that its body is not scanned for frames. */
        ble_rx_skip = len;
        ble_rx_pos  = 0;
        return;
    }
    if (ble_rx_pos < BLE_RX_HEADER_SIZE + len) return;
    ble_handle_frame(ble_rx, len);
    ble_rx_pos = 0;
}

static void ble_poll(void) {
    msg_t c = sdGetTimeout(&SD1, TIME_IMMEDIATE);
    if (c < MSG_OK) {
        /* The rest of a frame normally follows within a few milliseconds. A frame left over from before a long stall
         * does not swallow the next one: its sync byte rejects the old frame. */
        if ((ble_rx_pos || ble_rx_skip) && timer_elapsed32(ble_rx_timer) >= AP2_BLE_RX_TIMEOUT_MS) {
            ble_rx_pos  = 0;
            ble_rx_skip = 0;
        }
        return;
    }
    do {
        ble_rx_byte((uint8_t)c);
    } while ((c = sdGetTimeout(&SD1, TIME_IMMEDIATE)) >= MSG_OK);
    ble_rx_timer = timer_read32();
}

static void ble_handshake_task(void) {
    if (ble_ready || ble_ping_count >= AP2_BLE_HANDSHAKE_TRIES) return;
    const uint16_t interval = ble_ping_count < 5 ? 50 : 500;
    if (timer_elapsed32(ble_ping_timer) < interval) return;
    ble_ping_timer = timer_read32();
    ble_ping_count++;
    annepro2_ble_startup();
    annepro2_ble_request_version();
}

#ifdef RGB_MATRIX_ENABLE
/* With rgb_matrix the LED MCU is on exactly when rgb_matrix is enabled. Every change of that flag (the keys below,
 * QK_RGB_MATRIX_*, the state restored from EEPROM, user code) is followed here, so the two cannot drift apart. */
static bool led_mcu_on;

static void led_sync(bool force) {
    const bool on = rgb_matrix_is_enabled();
    if (!force && on == led_mcu_on) return;
    led_mcu_on = on;
    if (on) {
        ap2_led_enable();
    } else {
        ap2_led_disable();
    }
}
#endif

/* Turn the LEDs on or off: through rgb_matrix when it drives them, otherwise directly. */
static void led_set(bool on) {
#ifdef RGB_MATRIX_ENABLE
    if (on) {
        rgb_matrix_enable_noeeprom();
    } else {
        rgb_matrix_disable_noeeprom();
    }
    led_sync(true); /* a key press always resends, even if another path changed the LED MCU */
#else
    if (on) {
        ap2_led_enable();
    } else {
        ap2_led_disable();
    }
#endif
}

/*
 * The keyboard builds with NO_USB_STARTUP_CHECK (c15/c18 rules.mk). Otherwise QMK stops the whole keyboard task while
 * the USB host sleeps, which also stops typing over Bluetooth while the cable only charges. This does what that loop
 * did, but only while the reports go to USB: power down once (with RGB_MATRIX_SLEEP the LEDs go dark) and wake the
 * host with a new key press. The USB wakeup event still runs suspend_wakeup_init().
 */
#ifndef USB_GETSTATUS_REMOTE_WAKEUP_ENABLED
#    define USB_GETSTATUS_REMOTE_WAKEUP_ENABLED (2U)
#endif

static void usb_suspend_task(void) {
    static bool powered_down, key_down;
    const bool  usb_asleep = USB_DRIVER.state == USB_SUSPENDED;
    if (!usb_asleep || annepro2_ble_is_active()) {
        /* Switched to Bluetooth while the USB host sleeps: undo the power down here, no USB wakeup will do it. */
        if (powered_down && usb_asleep) suspend_wakeup_init_quantum();
        powered_down = false;
        return;
    }
    bool down = false;
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) down |= matrix_get_row(row) != 0;
    if (!powered_down) {
        powered_down = true;
        key_down     = down; /* keys still held from before the host slept do not wake it */
        suspend_power_down_quantum();
    }
    if (down && !key_down && (USB_DRIVER.status & USB_GETSTATUS_REMOTE_WAKEUP_ENABLED)) usbWakeupHost(&USB_DRIVER);
    key_down = down;
}

void bootloader_jump(void) {
    // Send msg to shine to boot into IAP
    ap2_set_IAP();

    // wait for shine to boot into IAP
    wait_ms(15);

    // Load ble into IAP
    annepro2_ble_bootload();
    wait_ms(15);

    // Magic key to set keyboard to IAP
    // It’s from reversing original boot loader
    // If value is that it stays in boot loader aka IAP
    *((uint32_t *)RAM_MAGIC_LOCATION) = IAP_MAGIC_VALUE;

    // Load the main MCU into IAP
    __disable_irq();
    NVIC_SystemReset();
}

void keyboard_pre_init_kb(void) {
    // Start LED UART
    sdStart(&SD0, &uart_115200_config);
    /* Let the LED chip settle a bit before switching the mode.
     * That helped at least one person. */
    wait_ms(15);
    sdWrite(&SD0, led_mcu_wakeup, sizeof(led_mcu_wakeup));

    // wait to receive response from wakeup
    wait_ms(15);

    proto_init(&proto, led_command_callback);

    // loop to clear out receive buffer from shine wakeup
    while (!sdGetWouldBlock(&SD0)) sdGet(&SD0);

    sdStart(&SD0, &led_uart_runtime_config);
    keyboard_pre_init_user();
}

void keyboard_post_init_kb(void) {
    annepro2_battery_init();
    // Start BLE UART
    sdStart(&SD1, &uart_115200_config);
    annepro2_ble_startup();

    // Give the send uart thread some time to
    // send out the queue before we read back
    wait_ms(100);

    // Parse the reply to the wake-up instead of discarding it. The wake-up above counts as the
    // first handshake ping; ble_handshake_task() repeats it until the BLE MCU reports its version.
    ble_poll();
    ble_ping_timer = timer_read32();
    ble_ping_count = 1;

#ifdef RGB_MATRIX_ENABLE
    ap2_led_set_manual_control(1);
    /* A mode saved by a build with more effects can be past the last effect of this one, and rgb_matrix would then
     * render nothing. UINT8_MAX is rgb_matrix's own factory test mode. */
    const uint8_t mode = rgb_matrix_get_mode();
    if (mode >= RGB_MATRIX_EFFECT_MAX && mode != UINT8_MAX) rgb_matrix_mode(RGB_MATRIX_DEFAULT_MODE);
    led_sync(true);
#endif

    keyboard_post_init_user();
}

#ifdef AP2_BLE_SLOT_TAP_HOLD
/*
 * Stock behaviour of the four Bluetooth keys: a short press connects to the paired device (40 04 slot),
 * holding the key for AP2_BLE_SLOT_HOLD_MS starts pairing (40 01 slot).
 */
#    ifndef AP2_BLE_SLOT_HOLD_MS
#        define AP2_BLE_SLOT_HOLD_MS 500
#    endif

static struct {
    uint32_t timer;
    uint8_t  row, col;
    bool     pending;
} slots[4];

static void slot_feedback(uint8_t row, uint8_t col) { ap2_led_blink(row, col, ap2_blue, 8, 50); }

static void slot_event(uint8_t slot, keyrecord_t *record) {
    if (record->event.pressed) {
        slots[slot].timer   = timer_read32();
        slots[slot].row     = record->event.key.row;
        slots[slot].col     = record->event.key.col;
        slots[slot].pending = true;
    } else if (slots[slot].pending) {
        slots[slot].pending = false; /* released before the hold time: connect */
        annepro2_ble_connect(slot);
        slot_feedback(slots[slot].row, slots[slot].col);
    }
}

static void slot_tick(void) {
    for (uint8_t i = 0; i < 4; i++) {
        if (slots[i].pending && timer_elapsed32(slots[i].timer) >= AP2_BLE_SLOT_HOLD_MS) {
            slots[i].pending = false; /* held: pair */
            annepro2_ble_pair(i);
            slot_feedback(slots[i].row, slots[i].col);
        }
    }
}
#endif /* AP2_BLE_SLOT_TAP_HOLD */

void matrix_scan_kb(void) {
    ble_poll();
    ble_handshake_task();
    annepro2_battery_task();
#ifdef AP2_BLE_SLOT_TAP_HOLD
    slot_tick();
#endif
    usb_suspend_task();
#ifdef RGB_MATRIX_ENABLE
    led_sync(false);
#endif

    /* While there's data from LED keyboard sent - read it. */
    msg_t c;
    while ((c = sdGetTimeout(&SD0, TIME_IMMEDIATE)) >= MSG_OK) proto_consume(&proto, (uint8_t)c);

    matrix_scan_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
#ifdef AP2_BLE_SLOT_TAP_HOLD
    if (keycode >= KC_AP2_BT1 && keycode <= KC_AP2_BT4) {
        if (record->event.pressed) forward_reactive_keypress(record);
        slot_event(keycode - KC_AP2_BT1, record);
        return false;
    }
#endif
    if (record->event.pressed) {
        forward_reactive_keypress(record);

        switch (keycode) {
#ifndef AP2_BLE_SLOT_TAP_HOLD
            case KC_AP2_BT1 ... KC_AP2_BT4:
                annepro2_ble_broadcast(keycode - KC_AP2_BT1);
                ap2_led_blink(record->event.key.row, record->event.key.col, ap2_blue, 8, 50);
                return false;
#endif

            case KC_AP2_BATT:
                annepro2_battery_toggle_display();
                return false;

            case KC_AP2_USB:
                annepro2_ble_disconnect();
                ap2_led_cancel_blink();
                return false;

            case KC_AP2_BT_UNPAIR:
                annepro2_ble_unpair();
                annepro2_ble_disconnect();
                ap2_led_cancel_blink();
                return false;

            case KC_AP_LED_OFF:
                led_set(false);
                return false;

            case KC_AP_LED_ON:
                if (ap2_led_status.matrix_enabled) {
                    ap2_led_next_profile();
                } else {
                    led_set(true);
                }
                ap2_led_reset_foreground_color();
                return false;

            case KC_AP_LED_TOG:
                if (ap2_led_status.matrix_enabled) {
                    led_set(false);
                } else {
                    led_set(true);
                    ap2_led_reset_foreground_color();
                }
                return false;

            case KC_AP_LED_NEXT_PROFILE:
                ap2_led_next_profile();
                ap2_led_reset_foreground_color();
                return false;

            case KC_AP_LED_PREV_PROFILE:
                ap2_led_prev_profile();
                ap2_led_reset_foreground_color();
                return false;

            case KC_AP_LED_NEXT_INTENSITY:
                ap2_led_next_intensity();
                ap2_led_reset_foreground_color();
                return false;

            case KC_AP_LED_SPEED:
                ap2_led_next_animation_speed();
                ap2_led_reset_foreground_color();
                return false;
#ifdef RGB_MATRIX_ENABLE
            /* QK_RGB_MATRIX_TOGGLE and the other rgb_matrix keys need nothing here: led_sync() follows them. */
            case KC_AP_RGB_VAI:
                if (get_mods() & MOD_MASK_SHIFT) {
                    rgb_matrix_increase_hue();
                } else if (get_mods() & MOD_MASK_CTRL) {
                    rgb_matrix_decrease_hue();
                } else {
                    rgb_matrix_increase_val();
                }
                return false;

            case KC_AP_RGB_VAD:
                if (get_mods() & MOD_MASK_SHIFT) {
                    rgb_matrix_increase_sat();
                } else if (get_mods() & MOD_MASK_CTRL) {
                    rgb_matrix_decrease_sat();
                } else {
                    rgb_matrix_decrease_val();
                }
                return false;

            case KC_AP_RGB_TOG:
                if (get_mods() & MOD_MASK_SHIFT) {
                    rgb_matrix_increase_speed();
                } else if (get_mods() & MOD_MASK_CTRL) {
                    rgb_matrix_decrease_speed();
                } else if (rgb_matrix_is_enabled()) {
                    led_set(false);
                } else {
                    led_set(true);
                    ap2_led_reset_foreground_color();
                }
                return false;

            case KC_AP_RGB_MOD:
                if (get_mods() & MOD_MASK_CTRL) {
                    rgb_matrix_step_reverse();
                } else {
                    rgb_matrix_step();
                }
                return false;
#endif

            default:
                break;
        }
    }
    return process_record_user(keycode, record);
}
