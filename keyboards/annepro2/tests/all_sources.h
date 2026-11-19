/* Every test includes this: the check macros, the fakes, and the real source files of the keyboard, unchanged, in one
 * translation unit. The static functions and variables of the sources are then reachable from the test, which lets a
 * test pin down a static parser and put the state of a module back (reset_firmware() below). */
#pragma once

#include "check.h"
#include "fake_hw.h"

#include "../annepro2.c"
#include "../annepro2_ble.c"
#include "../annepro2_battery.c"
#include "../ap2_led.c"
#include "../protocol.c"
#include "../rgb_driver.c"

/* The tests assume the layout of the keyboard: the matrix of the keymap's LAYOUT and the key count of the LED code. */
_Static_assert(MATRIX_ROWS == NUM_ROW && MATRIX_COLS == NUM_COLUMN && KEY_COUNT == NUM_ROW * NUM_COLUMN,
               "the tests assume the 5 x 14 matrix of the Anne Pro 2");

/* Put the static state of every module back to its value at power on, and reset the fakes (fake_reset()). The statics
 * are listed by hand: a new static variable in a source has to be added here, or a state can leak from one test case to
 * the next (the tests then fail in an order dependent way, which is the sign to look here). */
static void reset_firmware(void) {
    fake_reset();

    /* annepro2.c */
    ble_ready      = false;
    ble_version    = 0;
    ble_ping_count = 0;
    ble_ping_timer = 0;
    memset(ble_rx, 0, sizeof(ble_rx));
    ble_rx_pos   = 0;
    ble_rx_skip  = 0;
    ble_rx_timer = 0;
#ifdef RGB_MATRIX_ENABLE
    led_mcu_on = false;
    led_power_timer = 0;
    led_repairs     = 0;
#endif
    usb_gone         = false;
    usb_wake_pending = false;
    usb_keys_down    = false;
    usb_wake_timer   = 0;
    usb_wake_signal  = 0;
    memset(&ble_saved_report, 0, sizeof(ble_saved_report));
    ble_saved_mods  = 0;
    ble_state_saved = false;
#ifdef AP2_BLE_SLOT_TAP_HOLD
    memset(slots, 0, sizeof(slots));
#endif
#if AP2_BLE_UNPAIR_HOLD_MS > 0
    unpair_key.timer = 0;
    unpair_key.row   = UNPAIR_IDLE;
    unpair_key.col   = 0;
#endif
#ifdef AP2_BT_SWITCH
    bt_switch_applied = -1;
    bt_switch_raw     = false;
    bt_switch_timer   = 0;
#endif
    powered_down = false;
#ifdef RGB_MATRIX_ENABLE
    led_setup_checked = false;
    led_check_timer   = 0;
#endif
    fake_reset();

    /* annepro2_ble.c */
    last_host_driver = NULL;
#ifdef NKRO_ENABLE
    lastNkroStatus   = false;
#endif
    ble_capslock     = false;
    ble_slot         = -1;
    pair_pending     = false;
    connect_pending  = false;
    ble_timer        = 0;
    broadcast_last   = -1;

#ifdef AP2_BLE_BATTERY
    /* annepro2_battery.c */
    fail_count = 0;
    fail_timer = 0;
    send_now   = false;
    sample_timer = 0;
    memset(history, 0, sizeof(history));
    history_count = 0;
    history_pos   = 0;
    shown         = false;
    shown_timer   = 0;
    display_todo  = 0;
    display_set   = false;
    display_lit   = 0;
    memset(&display_color, 0, sizeof(display_color));
#endif

    /* ap2_led.c */
    memset(led_mask, 0, sizeof(led_mask));
    memset(led_colors, 0, sizeof(led_colors));
    memset(&ap2_led_status, 0, sizeof(ap2_led_status));
    blink_key = BLINK_NONE;
    blink_end = 0;
    rx_held     = false;
    rx_hold_end = 0;

    /* protocol.c */
    msg_id = 0;
    memset(&proto, 0, sizeof(proto));
    proto_init(&proto, led_receive); /* as keyboard_pre_init_kb() leaves it */

#ifdef RGB_MATRIX_ENABLE
    /* rgb_driver.c */
    dirty_rows    = 0;
    pending_rows  = 0;
    next_row      = 0;
    refresh_row   = 0;
    refresh_timer = 0;
    memset(sent_hash, 0, sizeof(sent_hash));
    sent_valid = 0;
#endif
}
