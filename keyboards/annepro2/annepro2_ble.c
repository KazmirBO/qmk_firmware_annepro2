/*
    Copyright (C) 2020 Yaotian Feng, Codetector<codetector@codetector.cn>

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "annepro2_ble.h"
#include <stddef.h>
#include <string.h>
#include "ch.h"
#include "hal.h"
#include "host.h"
#include "host_driver.h"
#include "report.h"
#include "usb_main.h"

/* -------------------- Static Function Prototypes -------------------------- */
static uint8_t ap2_ble_leds(void);
static void    ap2_ble_mouse(report_mouse_t *report);
static void    ap2_ble_extra(report_extra_t *report);
static void    ap2_ble_keyboard(report_keyboard_t *report);

static void ap2_ble_switch_ble_driver(void);
static void ble_send(uint8_t cmd, uint8_t sub, const void *data, uint8_t n);

/* -------------------- Static Local Variables ------------------------------ */
static host_driver_t ap2_ble_driver = {
    ap2_ble_leds, ap2_ble_keyboard, NULL, ap2_ble_mouse, ap2_ble_extra
};

/*
 * BLE MCU packet format (UART on SD1):
 *
 *   [0x7b]       sync start
 *   [type]       0x12 single frame, 0x10 frame with a fragment counter (bootload)
 *   [routing]    (target << 4) | source  =  0x53 = McuBle(5):McuMain(3)
 *   [seq]        sequence byte, 0x00 here (bootload uses 0x10: one fragment, index 0)
 *   [len lo]     length, little-endian: 2 (l2_cmd, cmd) + data bytes
 *   [len hi]
 *   [0x00]       zero in the stock firmware; the QMK wakeup has always sent 0x01,
 *                and the BLE MCU does not check this byte
 *   [0x7d]       sync end
 *   [l2_cmd]     layer-2 command group: 0x01=GLOBAL, 0x02=FW, 0x10=KEYBOARD, 0x40=BLE
 *   [cmd]        specific command within group
 *   [data...]    optional payload bytes
 *
 * ble_send() builds every single frame from cmd, sub and the data, so the length always matches the payload.
 *
 * Frames sent back by the BLE MCU use the same 8-byte header, with routing 0x35
 * (target 3 = main MCU, source 5 = BLE MCU), and are parsed in annepro2.c by length:
 *   01 03 <ver>   version, sent once after the BLE MCU starts and in reply to 01 03 02
 *   20 07 <0|1>   host Caps Lock state (BLE_STS_ID_CAPSLOCK)
 *   20 0c 00      a host connected (BLE_STS_ID_CONNECT), sent twice
 */

/* Sub-commands sent to the BLE MCU. */
enum {
    BLE_GLOBAL_BATTERY  = 0x05, /* raw 12-bit ADC reading, little-endian */
    BLE_KEYBOARD_REPORT = 0x04, /* 8-byte boot keyboard report */
    BLE_KEYBOARD_MEDIA  = 0x08, /* AP2 media bits + 3 reserved bytes */
    BLE_BLE_PAIR        = 0x01, /* advertise for pairing on a slot */
    BLE_BLE_CONNECT     = 0x04, /* connect to the device bonded on a slot */
    BLE_BLE_UNPAIR      = 0x05, /* delete the bonds of all slots */
};

#define BLE_TX_HEADER_SIZE 8
#define BLE_TX_DATA_MAX 8

static const uint8_t ble_mcu_wakeup[11] = {
    0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x01, 0x7d,
    0x02, 0x01, 0x02,  /* FW, IapMode, flag=normal(0x02) */
};

static const uint8_t ble_mcu_bootload[11] = {
    0x7b, 0x10, 0x51, 0x10, 0x03, 0x00, 0x00, 0x7d,
    0x02, 0x01, 0x01,  /* FW, IapMode, flag=IAP(0x01) */
};

static host_driver_t *last_host_driver = NULL;
#ifdef NKRO_ENABLE
static bool lastNkroStatus = false;
#endif  // NKRO_ENABLE

/* Caps Lock state of the BLE host, kept up to date in USB mode too, so that it is right when the reports go back to
 * a host that stayed connected. */
static bool ble_capslock;
/* Slot chosen last (-1: none since start), to tell a different host from the same one. */
static int8_t ble_slot = -1;
/* Pairing was started and no host has connected since. */
static bool pair_pending;

/* -------------------- Public Function Implementation ---------------------- */

void annepro2_ble_bootload(void) { sdWrite(&SD1, ble_mcu_bootload, sizeof(ble_mcu_bootload)); }

void annepro2_ble_startup(void) { sdWrite(&SD1, ble_mcu_wakeup, sizeof(ble_mcu_wakeup)); }

void annepro2_ble_send_battery(uint16_t raw) {
    const uint8_t data[] = {raw & 0xff, raw >> 8};
    ble_send(BLE_CMD_GLOBAL, BLE_GLOBAL_BATTERY, data, sizeof(data));
}

void annepro2_ble_request_version(void) {
    const uint8_t data = 0x02; /* the stock firmware sends it with 02 01 02 */
    ble_send(BLE_CMD_GLOBAL, BLE_GLOBAL_VERSION, &data, 1);
}

bool annepro2_ble_is_active(void) { return host_get_driver() == &ap2_ble_driver; }

void annepro2_ble_set_capslock(bool on) { ble_capslock = on; }

/* A host connected (20 0c, sent twice per connection). After pairing, the reports follow it to Bluetooth unless a USB
 * host is in use, so that a keyboard on battery can be used as soon as the new host pairs. */
void annepro2_ble_host_connected(void) {
    if (pair_pending && USB_DRIVER.state != USB_ACTIVE) ap2_ble_switch_ble_driver();
    pair_pending = false;
}

/* Start advertising on a slot without the "press twice to connect" shortcut. The reports stay where they are (USB keeps
 * working while nothing has paired), as in the stock firmware; annepro2_ble_host_connected() moves them later. */
void annepro2_ble_pair(uint8_t port) {
    if (port > 3) port = 3;
    ble_send(BLE_CMD_BLE, BLE_BLE_PAIR, &port, 1);
    ble_capslock = false; /* a new host: its state is unknown until it writes its LED report */
    ble_slot     = port;
    pair_pending = true;
}

void annepro2_ble_broadcast(uint8_t port) {
    static int8_t lastBroadcast = -1;
    if (port > 3) port = 3;
    ble_send(BLE_CMD_BLE, BLE_BLE_PAIR, &port, 1);
    if (lastBroadcast == port) {
        annepro2_ble_connect(port);
    }
    lastBroadcast = port;
}

void annepro2_ble_connect(uint8_t port) {
    if (port > 3) port = 3;
    ble_send(BLE_CMD_BLE, BLE_BLE_CONNECT, &port, 1);
    if (ble_slot != port) ble_capslock = false; /* another host; the same one keeps its known state */
    ble_slot     = port;
    pair_pending = false;
    ap2_ble_switch_ble_driver();
}

void annepro2_ble_disconnect(void) {
    pair_pending = false;
    if (host_get_driver() != &ap2_ble_driver) {
        return;
    }

    clear_keyboard();
#ifdef NKRO_ENABLE
    keymap_config.nkro = lastNkroStatus;
#endif
    host_set_driver(last_host_driver);
}

void annepro2_ble_unpair(void) {
    ble_send(BLE_CMD_BLE, BLE_BLE_UNPAIR, NULL, 0);
    ble_capslock = false;
    ble_slot     = -1;
}

/* ------------------- Static Function Implementation ----------------------- */

/* Single frame from the main MCU to the BLE MCU; the length is derived from the data. */
static void ble_send(uint8_t cmd, uint8_t sub, const void *data, uint8_t n) {
    if (n > BLE_TX_DATA_MAX) n = BLE_TX_DATA_MAX;
    uint8_t buf[BLE_TX_HEADER_SIZE + 2 + BLE_TX_DATA_MAX] = {
        BLE_SYNC_START, BLE_TYPE_SINGLE, BLE_ROUTE_TO_BLE, 0x00, 2 + n, 0x00, 0x00, BLE_SYNC_END, cmd, sub,
    };
    if (n) memcpy(buf + BLE_TX_HEADER_SIZE + 2, data, n);
    sdWrite(&SD1, buf, BLE_TX_HEADER_SIZE + 2 + n);
}

static void ap2_ble_switch_ble_driver(void) {
    if (host_get_driver() == &ap2_ble_driver) {
        return;
    }
    clear_keyboard();
    last_host_driver = host_get_driver();
#ifdef NKRO_ENABLE
    lastNkroStatus = keymap_config.nkro;
#endif
    keymap_config.nkro = false;
    host_set_driver(&ap2_ble_driver);
}

static uint8_t ap2_ble_leds(void) {
    return ble_capslock << 1;
}

static void ap2_ble_mouse(report_mouse_t *report) {}

static inline uint8_t CONSUMER2AP2(uint16_t usage) {
    switch (usage) {
        case AUDIO_VOL_DOWN:
            return 0x04;
        case AUDIO_VOL_UP:
            return 0x02;
        case AUDIO_MUTE:
            return 0x01;
        case TRANSPORT_PLAY_PAUSE:
            return 0x08;
        case TRANSPORT_NEXT_TRACK:
            return 0x10;
        case TRANSPORT_PREV_TRACK:
            return 0x20;
        /* Bits 6 and 7 of the BLE consumer report map (usages 0x6F and 0x70). */
        case BRIGHTNESS_UP:
            return 0x40;
        case BRIGHTNESS_DOWN:
            return 0x80;
        default:
            return 0x00;
    }
}

static void ap2_ble_extra(report_extra_t *report) {
    if (report->report_id == REPORT_ID_CONSUMER) {
        const uint8_t data[4] = {CONSUMER2AP2(report->usage)}; /* AP2 usage byte + 3 reserved */
        ble_send(BLE_CMD_KEYBOARD, BLE_KEYBOARD_MEDIA, data, sizeof(data));
    }
}

/* The BLE MCU takes the 8-byte boot layout: mods, reserved, keys[6]. With KEYBOARD_SHARED_EP the report starts with
 * a report ID, so the copy starts at mods instead of at the start of the report. */
_Static_assert(offsetof(report_keyboard_t, keys) + KEYBOARD_REPORT_KEYS - offsetof(report_keyboard_t, mods) == 8, "the BLE keyboard report is 8 bytes");

/*!
 * @brief  Send keyboard HID report for Bluetooth driver
 */
static void ap2_ble_keyboard(report_keyboard_t *report) {
    ble_send(BLE_CMD_KEYBOARD, BLE_KEYBOARD_REPORT, &report->mods, 8);
}
