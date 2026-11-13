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
#endif // LED_UART_BAUD_RATE

static const SerialConfig led_uart_runtime_config = {
    .speed = LED_UART_BAUD_RATE,
};

static const uint8_t led_mcu_wakeup[11] = {0x7b, 0x10, 0x43, 0x10, 0x03, 0x00, 0x00, 0x7d, 0x02, 0x01, 0x02};

/* Handshake state with the BLE MCU. The stock firmware pings the BLE MCU (02 01 02 and 01 03 02) five times every
 * 50 ticks of its 10 ms clock (0.5 s) and then every 5 s until the BLE MCU answers with its version. This port pings
 * ten times faster (50 ms, then 500 ms), which only gets the version sooner. */
#ifndef AP2_BLE_HANDSHAKE_TRIES
#    define AP2_BLE_HANDSHAKE_TRIES 24
#endif
#ifndef AP2_BLE_HANDSHAKE_SLOW_MS
#    define AP2_BLE_HANDSHAKE_SLOW_MS 5000 /* the stock interval, once the quick tries are used up */
#endif
static bool     ble_ready;
static uint16_t ble_version;
static uint8_t  ble_ping_count;
static uint32_t ble_ping_timer;

bool annepro2_ble_is_ready(void) {
    return ble_ready;
}
uint16_t annepro2_ble_version(void) {
    return ble_version;
}

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
    /* ble_rx_byte() passes only frames routed from the BLE MCU. A fragmented frame counts only when it is complete in
     * itself (one fragment, index 0). */
    if (f[BLE_RX_TYPE] == BLE_TYPE_FRAGMENT && f[BLE_RX_SEQ] != 0x10) return;
    if (len < 3) return; /* no data byte */
    const uint8_t cmd = f[BLE_RX_CMD], sub = f[BLE_RX_SUB];
    const uint8_t d0 = f[BLE_RX_DATA];

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
                /* The BLE MCU also reports its version after it restarts, which clears the serial number. */
                annepro2_ble_send_serial();
            }
            break;
        default:
            break;
    }
}

static uint16_t ble_rx_len(void) {
    return ble_rx[BLE_RX_LEN_LO] | ((uint16_t)ble_rx[BLE_RX_LEN_HI] << 8);
}

/* Whether the bytes received so far (ble_rx[0] is always the sync byte) can be the start of a frame. The stock
 * firmware accepts only the two frame types, and the header ends with the sync byte and gives a length of at most
 * BLE_RX_SKIP_MAX. A sync byte in the body of a frame that is held here (only short frames from the BLE MCU, which
 * never carry 0x7b) means that a byte of that frame was lost and the next frame has started. */
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
    while (i < ble_rx_pos && ble_rx[i] != BLE_SYNC_START)
        i++;
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
    while (ble_rx_pos > 0 && !ble_rx_valid())
        ble_rx_resync();
    /* A rejected frame leaves at most 7 bytes, so a header can only be complete when the byte appended here ends it. */
    if (ble_rx_pos < BLE_RX_HEADER_SIZE) return;
    const uint16_t len = ble_rx_len();
    if (len > BLE_RX_BODY_MAX || ble_rx[BLE_RX_ROUTE] != BLE_ROUTE_FROM_BLE) {
        /* Longer than any frame handled here, or not from the BLE MCU (it forwards frames from a host unchanged): drop it
         * by its length, so that its body, which may carry 0x7b, is not scanned for frames. */
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
    if (ble_ready) return;
    /* The pings never stop: a BLE MCU that starts late or restarts is still reached, as with the stock firmware, only
     * slowly after AP2_BLE_HANDSHAKE_TRIES quick ones. */
    const uint16_t interval = ble_ping_count < 5 ? 50 : ble_ping_count < AP2_BLE_HANDSHAKE_TRIES ? 500 : AP2_BLE_HANDSHAKE_SLOW_MS;
    if (timer_elapsed32(ble_ping_timer) < interval) return;
    ble_ping_timer = timer_read32();
    if (ble_ping_count < UINT8_MAX) ble_ping_count++;
    annepro2_ble_startup();
    annepro2_ble_request_version();
}

static void led_mcu_power(bool on) {
    if (on) {
        ap2_led_enable();
    } else {
        ap2_led_disable();
    }
}

#ifdef RGB_MATRIX_ENABLE
/* With rgb_matrix the LED MCU is on exactly when rgb_matrix shows something (ap2_rgb_lit(): enabled, not asleep with
 * the USB host, not past RGB_MATRIX_TIMEOUT). Every change (the keys below, QK_RGB_MATRIX_*, the state restored from
 * EEPROM, user code, a key after the timeout) is followed here, so the two cannot drift apart. */
static bool     led_mcu_on;
static uint16_t led_power_timer; /* the last power command; the LED timers are 16 bits, RAM is short on the C15 */

static void led_sync(bool force) {
    const bool on = ap2_rgb_lit();
    if (!force && on == led_mcu_on) return;
    led_mcu_on      = on;
    led_power_timer = timer_read();
    if (on) ap2_led_set_manual_control(1); /* the colours come from rgb_matrix, not from the profiles of the LED MCU */
    led_mcu_power(on);
    if (on) ap2_rgb_resend_all(); /* only changed rows are sent, so make sure the LED MCU has every colour */
}

/*
 * The LED MCU keeps no state over a restart (a brown-out on a low battery, for example): it comes back with its matrix
 * off, out of manual mode and without sticky keys or mask. While it should be on, 30 01 is sent every
 * AP2_LED_CHECK_MS; the LED MCU answers it with its status, and one that reports its matrix off is set up again. A
 * status shortly after a power command can still answer an older command and is not used. If the status still says
 * off after a few repairs in a row, it is not trusted until it reports the matrix on again.
 */
#    ifndef AP2_LED_CHECK_MS
#        define AP2_LED_CHECK_MS 1000
#    endif
#    define AP2_LED_STATUS_SETTLE_MS 100
#    define AP2_LED_MAX_REPAIRS 3

static uint16_t led_check_timer;
static uint8_t  led_repairs;

static void led_check_task(void) {
    if (!led_mcu_on || timer_elapsed(led_check_timer) < AP2_LED_CHECK_MS) return;
    if (ap2_led_tx_space() < PROTO_TX_MESSAGE_SIZE(1) + AP2_LED_TX_RESERVE) return; /* next scan */
    led_check_timer = timer_read();
    ap2_led_set_manual_control(1);
}

static void led_status_received(void) {
    if (!led_mcu_on || timer_elapsed(led_power_timer) < AP2_LED_STATUS_SETTLE_MS) return;
    if (ap2_led_status.matrix_enabled) {
        led_repairs = 0; /* working: a later restart is repaired again */
        return;
    }
    if (led_repairs >= AP2_LED_MAX_REPAIRS) return;
    led_repairs++;
    led_sync(true);
    led_update_kb(host_keyboard_led_state()); /* sticky keys of the keymap, such as a Caps Lock indicator */
    layer_state_set_kb(layer_state);          /* the layer colour of the keymap */
}
#endif

/* LED messages from the LED MCU. */
static void led_receive(const message_t *msg) {
    led_command_callback(msg);
#ifdef RGB_MATRIX_ENABLE
    if (msg->command == CMD_LED_STATUS && msg->payload_size >= 6) led_status_received();
#endif
}

/* The LED keys clear the mask, so that their effect shows, and then let the keymap put its layer colour back: a layer
 * that is still held keeps its indicator. */
static void leds_reset_foreground(void) {
    ap2_led_reset_foreground_color();
    if (layer_state) layer_state_set_kb(layer_state);
}

/* Turn the LEDs on or off: through rgb_matrix when it drives them, otherwise directly. The name is not led_set, which
 * QMK uses for the host LED state. */
static void leds_switch(bool on) {
#ifdef RGB_MATRIX_ENABLE
    if (on) {
        rgb_matrix_enable_noeeprom();
    } else {
        rgb_matrix_disable_noeeprom();
    }
    led_sync(true); /* a key press always resends, even if another path changed the LED MCU */
#else
    led_mcu_power(on);
#endif
}

/*
 * The keyboard builds with NO_USB_STARTUP_CHECK (c15/c18 rules.mk). Otherwise QMK stops the whole keyboard task while
 * the USB host sleeps, which also stops typing over Bluetooth while the cable only charges. Instead:
 * - every key keeps working; the senders of QMK's USB host driver are wrapped (usb_wrap_driver()), and a report that
 *   presses something while the USB host sleeps wakes the host and is dropped, as with QMK's own loop. A key that sends
 *   nothing (layer keys, the Bluetooth slots) wakes nothing, so Fn and a slot key switch to Bluetooth while the host
 *   sleeps on;
 * - while the reports go to USB and the host sleeps, the keyboard powers down once (with RGB_MATRIX_SLEEP the LEDs go
 *   dark) and scans every 17 ms, as in QMK's loop;
 * - the USB peripheral cannot tell a sleeping host from an unplugged cable. If a host that should wake does not
 *   resume within AP2_USB_WAKE_TIMEOUT_MS, it is taken as gone until the bus changes state, and the power down ends.
 *   The wakeup is still signalled for every key that presses something, and repeated while a key is held, as in QMK's
 *   loop, so a host that missed a signal or resumes slowly can still be woken.
 * The USB wakeup event still runs suspend_wakeup_init().
 */
#ifndef USB_GETSTATUS_REMOTE_WAKEUP_ENABLED
#    define USB_GETSTATUS_REMOTE_WAKEUP_ENABLED (2U)
#endif
#ifndef AP2_USB_WAKE_TIMEOUT_MS
#    define AP2_USB_WAKE_TIMEOUT_MS 2000
#endif
#define AP2_USB_WAKE_RETRY_MS 500

static bool     usb_gone;         /* suspended, and did not resume when asked: no host on the cable */
static bool     usb_wake_pending; /* asked to wake and not resumed yet: the timeout runs */
static bool     usb_keys_down;    /* the last keyboard report to USB holds a key or a modifier */
static uint32_t usb_wake_timer;   /* the first request, the timeout runs from it */
static uint16_t usb_wake_signal;  /* the last wakeup signal; 16 bits time the retry, RAM is short on the C15 */

/* Only a host that had configured the keyboard can be asleep: the USB peripheral may also report a suspend with no
 * host at all (on battery, on a charger). */
static bool usb_bus_asleep(void) {
    return USB_DRIVER.state == USB_SUSPENDED && USB_DRIVER.saved_state == USB_ACTIVE;
}

/* Asleep and not taken as gone: the keyboard powers down for it, and it counts as in use. */
static bool usb_host_asleep(void) {
    return usb_bus_asleep() && !usb_gone;
}

bool annepro2_usb_host_in_use(void) {
    return USB_DRIVER.state == USB_ACTIVE || usb_host_asleep();
}

static void usb_signal_wakeup(void) {
    usb_wake_signal = timer_read();
    if (USB_DRIVER.status & USB_GETSTATUS_REMOTE_WAKEUP_ENABLED) usbWakeupHost(&USB_DRIVER);
}

/* A report that presses something, to a bus that sleeps: signal a wakeup, at most every AP2_USB_WAKE_RETRY_MS, also
 * when the host was taken as gone. The first request starts the timeout. */
static void usb_report_while_asleep(void) {
    const bool first = !usb_wake_pending && !usb_gone;
    if (first) {
        usb_wake_pending = true;
        usb_wake_timer   = timer_read32();
    }
    if (first || timer_elapsed(usb_wake_signal) >= AP2_USB_WAKE_RETRY_MS) usb_signal_wakeup();
}

/* QMK's USB host driver (tmk_core/protocol/chibios/chibios.c) and its senders (usb_main.c). */
extern host_driver_t chibios_driver;
void                 send_keyboard(report_keyboard_t *report);
void                 send_nkro(report_nkro_t *report);
void                 send_mouse(report_mouse_t *report);
void                 send_extra(report_extra_t *report);

static bool usb_report_pressed(const uint8_t *data, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) {
        if (data[i]) return true;
    }
    return false;
}

static void usb_proxy_keyboard(report_keyboard_t *report) {
    usb_keys_down = usb_report_pressed(&report->mods, 8);
    if (usb_keys_down && usb_bus_asleep()) usb_report_while_asleep();
    send_keyboard(report);
}

static void usb_proxy_nkro(report_nkro_t *report) {
    usb_keys_down = report->mods || usb_report_pressed(report->bits, sizeof(report->bits));
    if (usb_keys_down && usb_bus_asleep()) usb_report_while_asleep();
    send_nkro(report);
}

static void usb_proxy_mouse(report_mouse_t *report) {
    if (usb_bus_asleep() && (report->buttons || report->x || report->y || report->v || report->h)) usb_report_while_asleep();
    send_mouse(report);
}

static void usb_proxy_extra(report_extra_t *report) {
    if (usb_bus_asleep() && report->usage) usb_report_while_asleep();
    send_extra(report);
}

/* The senders are replaced in QMK's own driver, before it is set: no copy of the driver in the short RAM of the C15,
 * and the Bluetooth driver goes back to the wrapped one. keyboard_leds and the rest are used as they are. */
static void usb_wrap_driver(void) {
    chibios_driver.send_keyboard = usb_proxy_keyboard;
    chibios_driver.send_nkro     = usb_proxy_nkro;
    chibios_driver.send_mouse    = usb_proxy_mouse;
    chibios_driver.send_extra    = usb_proxy_extra;
}

static void usb_suspend_task(void) {
    static bool powered_down;
    if (USB_DRIVER.state != USB_SUSPENDED) {
        usb_gone         = false; /* reset, configured or resumed: a host is there again */
        usb_wake_pending = false;
    } else if (usb_wake_pending && timer_elapsed32(usb_wake_timer) >= AP2_USB_WAKE_TIMEOUT_MS) {
        usb_gone         = true;
        usb_wake_pending = false;
    }
    /* A key held since a request is signalled again, as QMK's loop does while a key is down. */
    if (usb_keys_down && (usb_wake_pending || usb_gone) && usb_bus_asleep() && timer_elapsed(usb_wake_signal) >= AP2_USB_WAKE_RETRY_MS) usb_signal_wakeup();
    if (!usb_host_asleep() || annepro2_ble_is_active()) {
        /* Switched to Bluetooth, or no host after all: undo the power down here, no USB wakeup will do it. */
        if (powered_down && USB_DRIVER.state == USB_SUSPENDED) suspend_wakeup_init_quantum();
        powered_down = false;
        return;
    }
    if (!powered_down) {
        powered_down = true;
        suspend_power_down_quantum(); /* once, as on entering QMK's suspend loop */
    }
    wait_ms(17); /* the rhythm of QMK's suspend loop: no scanning at full speed while the host sleeps */
}

/* A USB host that wakes up runs suspend_wakeup_init(), which clears the held keys and mods even while the reports go to
 * Bluetooth. The Bluetooth state at the end of each loop is kept and put back in that case, so the host on Bluetooth
 * sees no change. It is kept whenever Bluetooth is active: the resume interrupt sets the USB state back before the
 * queued wakeup event runs, so a check of the USB state here would come too late. */
static report_keyboard_t ble_saved_report;
static uint8_t           ble_saved_mods;
static bool              ble_state_saved;

void housekeeping_task_kb(void) {
    ble_state_saved = annepro2_ble_is_active();
    if (ble_state_saved) {
        ble_saved_report = *keyboard_report;
        ble_saved_mods   = get_mods();
    }
    housekeeping_task_user();
}

void suspend_wakeup_init_kb(void) {
    /* Not after a switch to Bluetooth in this loop: then there is no state saved for it yet. */
    if (ble_state_saved && annepro2_ble_is_active()) {
        *keyboard_report = ble_saved_report;
        set_mods(ble_saved_mods);
    }
    suspend_wakeup_init_user();
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
    usb_wrap_driver();
    // Start LED UART
    sdStart(&SD0, &uart_115200_config);
    /* Let the LED chip settle a bit before switching the mode.
     * That helped at least one person. */
    wait_ms(15);
    sdWrite(&SD0, led_mcu_wakeup, sizeof(led_mcu_wakeup));

    // wait to receive response from wakeup
    wait_ms(15);

    proto_init(&proto, led_receive);

    // loop to clear out receive buffer from shine wakeup
    while (!sdGetWouldBlock(&SD0))
        sdGet(&SD0);

    sdStart(&SD0, &led_uart_runtime_config);
    keyboard_pre_init_user();
}

#ifdef AP2_BT_SWITCH
static void bt_switch_init(void);
#endif

void keyboard_post_init_kb(void) {
    annepro2_battery_init();
#ifdef AP2_BT_SWITCH
    bt_switch_init();
#endif
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
    /* A mode saved by a build with more effects can be past the last effect of this one, and rgb_matrix would then
     * render nothing. UINT8_MAX is rgb_matrix's own factory test mode. */
    const uint8_t mode = rgb_matrix_get_mode();
    if (mode >= RGB_MATRIX_EFFECT_MAX && mode != UINT8_MAX) {
        /* Set directly: rgb_matrix_mode() does nothing while rgb_matrix is disabled. */
        rgb_matrix_config.mode = RGB_MATRIX_DEFAULT_MODE;
        eeconfig_force_flush_rgb_matrix();
    }
    led_sync(true);
#endif

    keyboard_post_init_user();
}

#ifdef AP2_BLE_SLOT_TAP_HOLD
/*
 * Stock behaviour of the four Bluetooth keys: a short press connects to the paired device (40 04 slot),
 * holding the key for AP2_BLE_SLOT_HOLD_MS starts pairing (40 01 slot). The stock firmware waits 5 s;
 * the default here is shorter.
 */
#    ifndef AP2_BLE_SLOT_HOLD_MS
#        define AP2_BLE_SLOT_HOLD_MS 500
#    endif

static struct {
    uint32_t timer;
    uint8_t  row, col;
    bool     pending;
} slots[4];

static void slot_feedback(uint8_t row, uint8_t col) {
    ap2_led_blink(row, col, ap2_blue, 8, 50);
}

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

/*
 * KC_AP2_BT_UNPAIR deletes the bonds of all four slots, so it acts only after the key has been held for
 * AP2_BLE_UNPAIR_HOLD_MS; 0 acts on the press.
 */
#ifndef AP2_BLE_UNPAIR_HOLD_MS
#    define AP2_BLE_UNPAIR_HOLD_MS 3000
#endif

static const ap2_led_t ap2_red = {
    .p.blue  = 0x00,
    .p.red   = 0xff,
    .p.green = 0x00,
    .p.alpha = 0xff,
};

static void unpair_now(uint8_t row, uint8_t col) {
    /* Back to the USB driver first: the switch sends an empty report to the BLE MCU, and a report makes a BLE MCU that
     * is neither connected nor advertising advertise again. Sent after the unpair commands, it would leave the BLE MCU
     * advertising for a new pairing. */
    annepro2_ble_disconnect();
    annepro2_ble_unpair();
    ap2_led_cancel_blink();
    ap2_led_blink(row, col, ap2_red, 8, 50);
}

#if AP2_BLE_UNPAIR_HOLD_MS > 0
/* 16-bit timer and no flag (row UNPAIR_IDLE: not held), because RAM is short on the C15. */
#    define UNPAIR_IDLE 0xff
_Static_assert(AP2_BLE_UNPAIR_HOLD_MS < 0x10000, "the unpair hold time is 16-bit");
static struct {
    uint16_t timer;
    uint8_t  row, col;
} unpair_key = {.row = UNPAIR_IDLE};

static void unpair_event(keyrecord_t *record) {
    if (record->event.pressed) {
        unpair_key.timer = timer_read();
        unpair_key.row   = record->event.key.row;
        unpair_key.col   = record->event.key.col;
    } else {
        unpair_key.row = UNPAIR_IDLE; /* released before the hold time: nothing is deleted */
    }
}

static void unpair_tick(void) {
    if (unpair_key.row != UNPAIR_IDLE && timer_elapsed(unpair_key.timer) >= AP2_BLE_UNPAIR_HOLD_MS) {
        const uint8_t row = unpair_key.row, col = unpair_key.col;
        unpair_key.row = UNPAIR_IDLE;
        unpair_now(row, col);
    }
}
#endif

#ifdef AP2_BT_SWITCH
/*
 * Bluetooth switch on the bottom of the keyboard. The stock firmware reads it on PC10 (pull-down): high selects the
 * USB output, low the Bluetooth output. The stock firmware applies the level at every reading, without debouncing;
 * here only a change of the switch, stable for AP2_BT_SWITCH_DEBOUNCE_MS, selects the output, so the Bluetooth keys
 * and KC_AP2_USB still work in between. The first reading is applied by the first matrix scan, because the USB host
 * driver is set only after keyboard_post_init_kb().
 */
#    ifndef AP2_BT_SWITCH_PIN
#        define AP2_BT_SWITCH_PIN C10
#    endif
#    ifndef AP2_BT_SWITCH_DEBOUNCE_MS
#        define AP2_BT_SWITCH_DEBOUNCE_MS 50
#    endif

static int8_t   bt_switch_applied = -1; /* -1: not applied yet, 0: Bluetooth, 1: USB */
static bool     bt_switch_raw;
static uint32_t bt_switch_timer;

static void bt_switch_init(void) {
    gpio_set_pin_input_low(AP2_BT_SWITCH_PIN);
    bt_switch_raw   = gpio_read_pin(AP2_BT_SWITCH_PIN);
    bt_switch_timer = timer_read32();
}

static void bt_switch_task(void) {
    const bool raw = gpio_read_pin(AP2_BT_SWITCH_PIN);
    if (raw != bt_switch_raw) {
        bt_switch_raw   = raw;
        bt_switch_timer = timer_read32();
        return;
    }
    if (bt_switch_applied == (int8_t)raw || timer_elapsed32(bt_switch_timer) < AP2_BT_SWITCH_DEBOUNCE_MS) return;

    bt_switch_applied = raw;
    if (raw) {
        annepro2_ble_disconnect();
        ap2_led_cancel_blink();
    } else {
        annepro2_ble_use_driver();
    }
}
#endif /* AP2_BT_SWITCH */

/*
 * With no USB host to serve, no key for AP2_IDLE_AFTER_MS and the LEDs dark, the keyboard scans every AP2_IDLE_SCAN_MS
 * and the MCU sleeps in between (ChibiOS idles with WFI) instead of scanning flat out on the battery. The first key
 * after the pause is seen at most AP2_IDLE_SCAN_MS late; 0 turns this off.
 */
#ifndef AP2_IDLE_SCAN_MS
#    define AP2_IDLE_SCAN_MS 10
#endif
#ifndef AP2_IDLE_AFTER_MS
#    define AP2_IDLE_AFTER_MS 10000
#endif

static void idle_task(void) {
#if AP2_IDLE_SCAN_MS > 0
    if (annepro2_usb_host_in_use() || last_input_activity_elapsed() < AP2_IDLE_AFTER_MS) return;
#    ifdef RGB_MATRIX_ENABLE
    if (ap2_rgb_lit()) return; /* a lit effect keeps its frame rate */
#    endif
    wait_ms(AP2_IDLE_SCAN_MS);
#endif
}

void matrix_scan_kb(void) {
    ble_poll();
    ble_handshake_task();
    if (annepro2_ble_task()) ap2_led_cancel_blink(); /* a connect timed out: back on the USB host */
    annepro2_battery_task();
#ifdef AP2_BLE_SLOT_TAP_HOLD
    slot_tick();
#endif
#if AP2_BLE_UNPAIR_HOLD_MS > 0
    unpair_tick();
#endif
#ifdef AP2_BT_SWITCH
    bt_switch_task();
#endif
    usb_suspend_task();
#ifdef RGB_MATRIX_ENABLE
    led_sync(false);
    led_check_task();
    ap2_rgb_flush_pending();
#endif
    idle_task();

    /* While there's data from LED keyboard sent - read it. */
    msg_t c;
    while ((c = sdGetTimeout(&SD0, TIME_IMMEDIATE)) >= MSG_OK)
        proto_consume(&proto, (uint8_t)c);

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
#if AP2_BLE_UNPAIR_HOLD_MS > 0
    if (keycode == KC_AP2_BT_UNPAIR) {
        if (record->event.pressed) forward_reactive_keypress(record);
        unpair_event(record);
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

#if AP2_BLE_UNPAIR_HOLD_MS <= 0
            case KC_AP2_BT_UNPAIR:
                unpair_now(record->event.key.row, record->event.key.col);
                return false;
#endif

            case KC_AP_LED_OFF:
                leds_switch(false);
                return false;

            case KC_AP_LED_ON:
                if (ap2_led_status.matrix_enabled) {
                    ap2_led_next_profile();
                } else {
                    leds_switch(true);
                }
                leds_reset_foreground();
                return false;

            case KC_AP_LED_TOG:
                if (ap2_led_status.matrix_enabled) {
                    leds_switch(false);
                } else {
                    leds_switch(true);
                    leds_reset_foreground();
                }
                return false;

            case KC_AP_LED_NEXT_PROFILE:
                ap2_led_next_profile();
                leds_reset_foreground();
                return false;

            case KC_AP_LED_PREV_PROFILE:
                ap2_led_prev_profile();
                leds_reset_foreground();
                return false;

            case KC_AP_LED_NEXT_INTENSITY:
                ap2_led_next_intensity();
                leds_reset_foreground();
                return false;

            case KC_AP_LED_SPEED:
                ap2_led_next_animation_speed();
                leds_reset_foreground();
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
                    leds_switch(false);
                } else {
                    leds_switch(true);
                    leds_reset_foreground();
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
