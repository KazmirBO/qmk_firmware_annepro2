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

#include "hal.h"
#include "annepro2.h"
#include "annepro2_ble.h"
#include "ap2_led.h"
#include "protocol.h"

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

bool ble_capslock = false;

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

static uint8_t ble_rx[BLE_RX_HEADER_SIZE + BLE_RX_BODY_MAX];
static uint8_t ble_rx_pos;

static void ble_handle_frame(const uint8_t *f, uint16_t len) {
    if (f[7] != BLE_SYNC_END) return; /* misframed */
    const uint8_t cmd = f[8], sub = f[9];
    const uint8_t n   = len - 2; /* data bytes */
    if (n < 1) return;
    const uint8_t d0 = f[10];

    switch (cmd) {
        case BLE_CMD_LED:
            switch (sub) {
                case BLE_STS_ID_CONNECT:
                    ap2_led_cancel_blink();
                    break;
                case BLE_STS_ID_CAPSLOCK:
                    ble_capslock = (bool)d0;
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

/* Feed one byte from the BLE MCU into the frame parser. */
static void ble_rx_byte(uint8_t byte) {
    if (ble_rx_pos == 0 && byte != BLE_SYNC_START) return;
    ble_rx[ble_rx_pos++] = byte;
    if (ble_rx_pos == 2 && byte != 0x12 && byte != 0x10) {
        ble_rx_pos = 0; /* the stock firmware accepts only these two frame types */
        return;
    }
    if (ble_rx_pos < BLE_RX_HEADER_SIZE) return;
    const uint16_t len = ble_rx[4] | ((uint16_t)ble_rx[5] << 8);
    if (len < 2 || len > BLE_RX_BODY_MAX) {
        ble_rx_pos = 0; /* implausible length: wait for the next sync byte */
        return;
    }
    if (ble_rx_pos < BLE_RX_HEADER_SIZE + len) return;
    ble_handle_frame(ble_rx, len);
    ble_rx_pos = 0;
}

static void ble_poll(void) {
    while (!sdGetWouldBlock(&SD1)) ble_rx_byte(sdGet(&SD1));
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
    ap2_led_enable();
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
} ap2_slot[4];

static void ap2_slot_feedback(uint8_t row, uint8_t col) {
    const ap2_led_t blue = {
        .p.blue  = 0xff,
        .p.red   = 0x00,
        .p.green = 0x00,
        .p.alpha = 0xff,
    };
    ap2_led_blink(row, col, blue, 8, 50);
}

static void ap2_slot_event(uint8_t slot, keyrecord_t *record) {
    if (record->event.pressed) {
        ap2_slot[slot].timer   = timer_read32();
        ap2_slot[slot].row     = record->event.key.row;
        ap2_slot[slot].col     = record->event.key.col;
        ap2_slot[slot].pending = true;
    } else if (ap2_slot[slot].pending) {
        ap2_slot[slot].pending = false; /* released before the hold time: connect */
        annepro2_ble_connect(slot);
        ap2_slot_feedback(ap2_slot[slot].row, ap2_slot[slot].col);
    }
}

static void ap2_slot_tick(void) {
    for (uint8_t i = 0; i < 4; i++) {
        if (ap2_slot[i].pending && timer_elapsed32(ap2_slot[i].timer) >= AP2_BLE_SLOT_HOLD_MS) {
            ap2_slot[i].pending = false; /* held: pair */
            annepro2_ble_pair(i);
            ap2_slot_feedback(ap2_slot[i].row, ap2_slot[i].col);
        }
    }
}
#endif /* AP2_BLE_SLOT_TAP_HOLD */

void matrix_scan_kb(void) {
    ble_poll();
    ble_handshake_task();
#ifdef AP2_BLE_SLOT_TAP_HOLD
    ap2_slot_tick();
#endif

    /* While there's data from LED keyboard sent - read it. */
    while (!sdGetWouldBlock(&SD0)) {
        uint8_t byte = sdGet(&SD0);
        proto_consume(&proto, byte);
    }


    matrix_scan_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
#ifdef AP2_BLE_SLOT_TAP_HOLD
    if (keycode >= KC_AP2_BT1 && keycode <= KC_AP2_BT4) {
        if (record->event.pressed && ap2_led_status.matrix_enabled && ap2_led_status.is_reactive) {
            ap2_led_forward_keypress(record->event.key.row, record->event.key.col);
        }
        ap2_slot_event(keycode - KC_AP2_BT1, record);
        return false;
    }
#endif
    if (record->event.pressed) {
        if (ap2_led_status.matrix_enabled && ap2_led_status.is_reactive) {
            ap2_led_forward_keypress(record->event.key.row, record->event.key.col);
        }

        switch (keycode) {
#ifndef AP2_BLE_SLOT_TAP_HOLD
            case KC_AP2_BT1 ... KC_AP2_BT4: {
                const ap2_led_t blue = {
                    .p.blue  = 0xff,
                    .p.red   = 0x00,
                    .p.green = 0x00,
                    .p.alpha = 0xff,
                };
                annepro2_ble_broadcast(keycode - KC_AP2_BT1);
                ap2_led_blink(record->event.key.row, record->event.key.col, blue, 8, 50);
                return false;
            }
#endif

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
                ap2_led_disable();
#ifdef RGB_MATRIX_ENABLE
                /* Keep rgb_matrix_is_enabled() (the source of truth for
                 * KC_AP_RGB_TOG) in sync with the hardware LED state. */
                rgb_matrix_disable_noeeprom();
#endif
                return false;

            case KC_AP_LED_ON:
                if (ap2_led_status.matrix_enabled) {
                    ap2_led_next_profile();
                } else {
                    ap2_led_enable();
#ifdef RGB_MATRIX_ENABLE
                    rgb_matrix_enable_noeeprom();
#endif
                }
                ap2_led_reset_foreground_color();
                return false;

            case KC_AP_LED_TOG:
                if (ap2_led_status.matrix_enabled) {
                    ap2_led_disable();
#ifdef RGB_MATRIX_ENABLE
                    rgb_matrix_disable_noeeprom();
#endif
                } else {
                    ap2_led_enable();
#ifdef RGB_MATRIX_ENABLE
                    rgb_matrix_enable_noeeprom();
#endif
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
            case QK_RGB_MATRIX_TOGGLE:
                if(rgb_matrix_is_enabled()) ap2_led_disable();
                else ap2_led_enable();
                return process_record_user(keycode, record);

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
                    ap2_led_disable();
                    rgb_matrix_disable_noeeprom();
                } else {
                    ap2_led_enable();
                    rgb_matrix_enable_noeeprom();
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
