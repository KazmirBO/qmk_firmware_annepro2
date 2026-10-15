/*
 * Copyright (c) 2018 Yaotian Feng
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
#include "quantum.h"
#include <stdint.h>
#include "ap2_led.h"

/*
 * Frame sent by the BLE MCU to the main MCU (UART on SD1). The header is the
 * same 8 bytes used between every pair of processors:
 *
 *   [0] 0x7b  [1] 0x12 (single) or 0x10 (fragmented)  [2] (dst << 4) | src
 *   [3] seq   [4..5] length LE = 2 + data bytes        [6] 0  [7] 0x7d
 *   [8] cmd   [9] sub   [10..] data
 *
 * The frame is parsed by length, so frames carrying other payload sizes than
 * the three known ones do not desynchronise the stream.
 */
#define BLE_RX_HEADER_SIZE 8
#define BLE_RX_BODY_MAX    8 /* cmd + sub + up to 6 data bytes */

/* Offsets in a received frame. */
enum {
    BLE_RX_SYNC   = 0,
    BLE_RX_TYPE   = 1,
    BLE_RX_ROUTE  = 2,
    BLE_RX_SEQ    = 3,
    BLE_RX_LEN_LO = 4,
    BLE_RX_LEN_HI = 5,
    BLE_RX_END    = 7,
    BLE_RX_CMD    = 8,
    BLE_RX_SUB    = 9,
    BLE_RX_DATA   = 10,
};

/* Frame types the stock firmware accepts. */
enum {
    BLE_TYPE_SINGLE   = 0x12,
    BLE_TYPE_FRAGMENT = 0x10,
};

extern bool ble_capslock;

#define BLE_SYNC_START 0x7b
#define BLE_SYNC_END   0x7d

/* Command groups (byte 8). */
enum {
    BLE_CMD_GLOBAL = 0x01,
    BLE_CMD_LED    = 0x20, /* the BLE MCU reports host state with this group */
};

/* Sub-commands (byte 9) seen in frames from the BLE MCU. */
enum {
    BLE_GLOBAL_VERSION  = 0x03, /* data[0]: high nibble major, low nibble minor (0x25 = 2.05) */
    BLE_STS_ID_CAPSLOCK = 0x07, /* LED group: data[0] = host Caps Lock state */
    BLE_STS_ID_CONNECT  = 0x0c, /* LED group: GAP state changed */
};

enum AP2KeyCodes {
    KC_AP2_BT1 = QK_KB_0,
    KC_AP2_BT2,
    KC_AP2_BT3,
    KC_AP2_BT4,
    KC_AP2_BT_UNPAIR,
    KC_AP2_USB,
    KC_AP_LED_ON,
    KC_AP_LED_OFF,
    KC_AP_LED_TOG,
    KC_AP_LED_NEXT_PROFILE,
    KC_AP_LED_PREV_PROFILE,
    KC_AP_LED_NEXT_INTENSITY,
    KC_AP_LED_SPEED,
    KC_AP_RGB_VAI,
    KC_AP_RGB_VAD,
    KC_AP_RGB_TOG,
    KC_AP_RGB_MOD,
    KC_AP2_BATT, /* shows the battery level on the number row (needs AP2_BLE_BATTERY) */
};
