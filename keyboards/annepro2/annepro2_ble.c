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
#include <string.h>
#include "ch.h"
#include "hal.h"
#include "host.h"
#include "host_driver.h"
#include "report.h"

/* -------------------- Static Function Prototypes -------------------------- */
static uint8_t ap2_ble_leds(void);
static void    ap2_ble_mouse(report_mouse_t *report);
static void    ap2_ble_extra(report_extra_t *report);
static void    ap2_ble_keyboard(report_keyboard_t *report);

static void ap2_ble_switch_ble_driver(void);
static void ble_send_port_cmd(const uint8_t *header, uint8_t port);

/* -------------------- Static Local Variables ------------------------------ */
static host_driver_t ap2_ble_driver = {
    ap2_ble_leds, ap2_ble_keyboard, NULL, ap2_ble_mouse, ap2_ble_extra
};

/*
 * BLE MCU packet format (UART on SD1):
 *
 *   [0x7b]       sync start
 *   [type]       0x12 single frame, 0x10 frame with a fragment counter (group 0x02, as in the stock firmware)
 *   [routing]    (target << 4) | source  =  0x53 = McuBle(5):McuMain(3)
 *   [seq]        0x00 for type 0x12; 0x10 for type 0x10 (one fragment, index 0)
 *   [len lo]     length, little-endian: 2 (l2_cmd, cmd) + data bytes
 *   [len hi]
 *   [0x00]       always zero; the BLE MCU does not check this byte
 *   [0x7d]       sync end
 *   [l2_cmd]     layer-2 command group: 0x01=GLOBAL, 0x02=FW, 0x10=KEYBOARD, 0x40=BLE
 *   [cmd]        specific command within group
 *   [data...]    optional payload bytes
 *
 * Frames sent back by the BLE MCU use the same 8-byte header, with routing 0x35
 * (target 3 = main MCU, source 5 = BLE MCU), and are parsed in annepro2.c by length:
 *   01 03 <ver>   version, sent once after the BLE MCU starts and in reply to 01 03 02
 *   20 07 <0|1>   host Caps Lock state (BLE_STS_ID_CAPSLOCK)
 *   20 0c 00      a host connected (BLE_STS_ID_CONNECT), sent twice; nothing is sent on a disconnect
 *   40 01 00, 40 04 00, 40 05 00
 *                 one-byte replies to the slot and unpair commands, ignored by the parser
 */

/* 02 01 02 lets the BLE MCU leave its bootloader; the BLE application ignores it. Same frame as the stock firmware. */
static const uint8_t ble_mcu_wakeup[11] = {
    0x7b, 0x10, 0x53, 0x10, 0x03, 0x00, 0x00, 0x7d,
    0x02, 0x01, 0x02,  /* FW, IapMode, flag=normal(0x02) */
};

static const uint8_t ble_mcu_get_version[11] = {
    0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d,
    0x01, 0x03, 0x02,  /* GLOBAL, version request (the stock firmware sends it with 02 01 02) */
};

static const uint8_t ble_mcu_battery[10] = {
    0x7b, 0x12, 0x53, 0x00, 0x04, 0x00, 0x00, 0x7d,
    0x01, 0x05,  /* GLOBAL, battery; followed by the raw 12-bit ADC reading, little-endian */
};

static const uint8_t ble_mcu_start_broadcast[10] = {
    0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d,
    0x40, 0x01,  /* BLE, broadcast (pairing); followed by the port (0-3) */
};

static const uint8_t ble_mcu_connect[10] = {
    0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d,
    0x40, 0x04,  /* BLE, connect; followed by the port (0-3) */
};

static const uint8_t ble_mcu_send_report[10] = {
    0x7b, 0x12, 0x53, 0x00, 0x0A, 0x00, 0x00, 0x7d,
    0x10, 0x04,  /* KEYBOARD, HID report; followed by 8-byte keyboard report */
};

static const uint8_t ble_mcu_send_consumer_report[10] = {
    0x7b, 0x12, 0x53, 0x00, 0x06, 0x00, 0x00, 0x7d,
    0x10, 0x08,  /* KEYBOARD, consumer report; followed by 4-byte consumer report */
};

static const uint8_t ble_mcu_unpair[10] = {
    0x7b, 0x12, 0x53, 0x00, 0x02, 0x00, 0x00, 0x7d,
    0x40, 0x05,  /* BLE, unpair */
};

static const uint8_t ble_mcu_bootload[11] = {
    0x7b, 0x10, 0x51, 0x10, 0x03, 0x00, 0x00, 0x7d,
    0x02, 0x01, 0x01,  /* FW, IapMode, flag=IAP(0x01) */
};

#ifdef AP2_BLE_SERIAL
static const uint8_t ble_mcu_serial[11] = {
    0x7b, 0x12, 0x53, 0x00, 0x23, 0x00, 0x00, 0x7d,
    0x01, 0x10, 0x02,  /* GLOBAL, device info, id 2 (DIS Serial Number); followed by 32 characters */
};
#endif

static host_driver_t *last_host_driver = NULL;
#ifdef NKRO_ENABLE
static bool lastNkroStatus = false;
#endif  // NKRO_ENABLE

/* -------------------- Public Function Implementation ---------------------- */

void annepro2_ble_bootload(void) { sdWrite(&SD1, ble_mcu_bootload, sizeof(ble_mcu_bootload)); }

void annepro2_ble_startup(void) { sdWrite(&SD1, ble_mcu_wakeup, sizeof(ble_mcu_wakeup)); }

void annepro2_ble_send_battery(uint16_t raw) {
    uint8_t buf[sizeof(ble_mcu_battery) + 2];
    memcpy(buf, ble_mcu_battery, sizeof(ble_mcu_battery));
    buf[sizeof(ble_mcu_battery)]     = raw & 0xff;
    buf[sizeof(ble_mcu_battery) + 1] = raw >> 8;
    sdWrite(&SD1, buf, sizeof(buf));
}

void annepro2_ble_request_version(void) { sdWrite(&SD1, ble_mcu_get_version, sizeof(ble_mcu_get_version)); }

#ifdef AP2_BLE_SERIAL
/*
 * Set the DIS Serial Number of the BLE MCU to the UDID of the stock device record, as 32 upper-case hex digits (the
 * text the stock firmware uses as its USB serial number). The stock firmware sends the same text after every version
 * report, but without the id byte, so the BLE MCU rejects it and keeps its default serial. The BLE MCU keeps the
 * value in RAM only, so it has to be sent again after the BLE MCU restarts.
 */
void annepro2_ble_send_serial(void) {
    static const char hex[] = "0123456789ABCDEF";
    const uint8_t    *udid  = (const uint8_t *)(AP2_DEVICE_RECORD_ADDR + 8);
    bool              blank = true, erased = true;
    for (uint8_t i = 0; i < 16; i++) {
        blank &= udid[i] == 0x00;
        erased &= udid[i] == 0xff;
    }
    if (blank || erased) return; /* no device record */

    uint8_t buf[sizeof(ble_mcu_serial) + 32];
    memcpy(buf, ble_mcu_serial, sizeof(ble_mcu_serial));
    for (uint8_t i = 0; i < 16; i++) {
        buf[sizeof(ble_mcu_serial) + 2 * i]     = hex[udid[i] >> 4];
        buf[sizeof(ble_mcu_serial) + 2 * i + 1] = hex[udid[i] & 0x0f];
    }
    sdWrite(&SD1, buf, sizeof(buf));
}
#endif

/* Start advertising on a slot without the "press twice to connect" shortcut. The reports go to the BLE MCU from now
 * on, so that the host can be used as soon as it pairs, without another tap of the key. The stock firmware does not
 * change the output here: only its Bluetooth switch selects between USB and Bluetooth. */
void annepro2_ble_pair(uint8_t port) {
    if (port > 3) port = 3;
    ble_send_port_cmd(ble_mcu_start_broadcast, port);
    ap2_ble_switch_ble_driver();
}

void annepro2_ble_broadcast(uint8_t port) {
    static int8_t lastBroadcast = -1;
    if (port > 3) port = 3;
    ble_send_port_cmd(ble_mcu_start_broadcast, port);
    if (lastBroadcast == port) {
        annepro2_ble_connect(port);
    }
    lastBroadcast = port;
}

void annepro2_ble_connect(uint8_t port) {
    if (port > 3) port = 3;
    ble_send_port_cmd(ble_mcu_connect, port);
    ap2_ble_switch_ble_driver();
}

/* Send the reports to the BLE MCU without a slot command, as the stock firmware does when its Bluetooth switch is
 * turned on. A host that is still connected gets them at once. After a disconnect the BLE MCU does not advertise on its
 * own; it starts advertising when the next report arrives (5 s for its bonded hosts, 60 s without bonds). */
void annepro2_ble_use_driver(void) { ap2_ble_switch_ble_driver(); }

void annepro2_ble_disconnect(void) {
    if (host_get_driver() != &ap2_ble_driver) {
        return;
    }
    ble_capslock = false;

    clear_keyboard();
#ifdef NKRO_ENABLE
    keymap_config.nkro = lastNkroStatus;
#endif
    host_set_driver(last_host_driver);
}

void annepro2_ble_unpair(void) {
    sdWrite(&SD1, ble_mcu_unpair, sizeof(ble_mcu_unpair));
}

/* ------------------- Static Function Implementation ----------------------- */
#define BLE_CMD_HEADER_SIZE 10

/* The length in the header (3) covers one data byte, the port, as in the frames of the stock firmware. */
static void ble_send_port_cmd(const uint8_t *header, uint8_t port) {
    uint8_t buf[BLE_CMD_HEADER_SIZE + 1];
    memcpy(buf, header, BLE_CMD_HEADER_SIZE);
    buf[BLE_CMD_HEADER_SIZE] = port;
    sdWrite(&SD1, buf, sizeof(buf));
}

static void ap2_ble_switch_ble_driver(void) {
    if (host_get_driver() == &ap2_ble_driver) {
        return;
    }
    /* The new host's CapsLock state is unknown until it reports it. This is
     * the single point where the host driver identity switches to BLE. */
    ble_capslock = false;
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
        enum { CONSUMER_PAYLOAD_SIZE = 4 };  /* AP2 usage byte + 3 reserved */
        uint8_t buf[sizeof(ble_mcu_send_consumer_report) + CONSUMER_PAYLOAD_SIZE] = {0};
        memcpy(buf, ble_mcu_send_consumer_report, sizeof(ble_mcu_send_consumer_report));
        /* Derive the size byte (header index 4) like ap2_ble_keyboard, so it can
         * never drift from the payload: l2_cmd (2 bytes) + the consumer payload. */
        buf[4]                                     = 2 + CONSUMER_PAYLOAD_SIZE;
        buf[sizeof(ble_mcu_send_consumer_report)]   = CONSUMER2AP2(report->usage);
        sdWrite(&SD1, buf, sizeof(buf));
    }
}

/*!
 * @brief  Send keyboard HID report for Bluetooth driver
 */
static void ap2_ble_keyboard(report_keyboard_t *report) {
    uint8_t buf[sizeof(ble_mcu_send_report) + KEYBOARD_REPORT_SIZE];
    memcpy(buf, ble_mcu_send_report, sizeof(ble_mcu_send_report));
    /* Payload-size byte (header index 4) = l2_cmd (0x10,0x04 = 2 bytes) + the
     * HID report. Derive it so a non-8-byte KEYBOARD_REPORT_SIZE stays correct. */
    buf[4] = 2 + KEYBOARD_REPORT_SIZE;
    memcpy(buf + sizeof(ble_mcu_send_report), report, KEYBOARD_REPORT_SIZE);
    sdWrite(&SD1, buf, sizeof(buf));
}
