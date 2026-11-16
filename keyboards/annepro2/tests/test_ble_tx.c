/* The BLE host driver and the frames the main MCU sends to the BLE MCU (annepro2_ble.c).
 *
 * Where the behaviour is documented:
 *   annepro2_ble.c   the packet format comment above the enum of sub-commands, ble_send(), and the comment of each
 *                    function (pairing, connecting, the timeouts, unpair)
 *   readme.md        AP2_BLE_CONNECT_TIMEOUT_MS, AP2_BLE_PAIR_TIMEOUT_MS, AP2_BLE_SERIAL, KC_AP2_BT_UNPAIR
 *   docs/05-ble.md   the commands of the BLE MCU
 *
 * test_ble_tx_shared_ep.c builds this file with KEYBOARD_SHARED_EP, where a keyboard report starts with a report ID. */
#include "all_sources.h"

static void expect_tx(const char *what, const uint8_t *expected, size_t n) {
    if (SD1.tx_len != n) {
        printf("  (%s: %zu bytes sent, %zu expected)\n", what, SD1.tx_len, n);
    }
    CHECK_EQ(SD1.tx_len, n);
    if (SD1.tx_len == n) CHECK_BYTES(SD1.tx, expected, n);
    fake_sd_tx_clear(&SD1);
}

#define EXPECT_TX(what, ...)                              \
    do {                                                  \
        static const uint8_t exp_[] = {__VA_ARGS__};      \
        expect_tx(what, exp_, sizeof(exp_));              \
    } while (0)

static void setup(void) {
    reset_firmware();
}

static void case_simple_frames(void) {
    CASE("wake up and boot loader frames");
    setup();
    annepro2_ble_startup();
    EXPECT_TX("startup", 0x7b, 0x10, 0x53, 0x10, 0x03, 0x00, 0x00, 0x7d, 0x02, 0x01, 0x02);
    annepro2_ble_bootload();
    EXPECT_TX("bootload", 0x7b, 0x10, 0x51, 0x10, 0x03, 0x00, 0x00, 0x7d, 0x02, 0x01, 0x01);

    CASE("version request");
    setup();
    annepro2_ble_request_version();
    EXPECT_TX("version", 0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x01, 0x03, 0x02);

    CASE("battery, raw reading little endian");
    setup();
    annepro2_ble_send_battery(0x0abc);
    EXPECT_TX("battery", 0x7b, 0x12, 0x53, 0x00, 0x04, 0x00, 0x00, 0x7d, 0x01, 0x05, 0xbc, 0x0a);
    annepro2_ble_send_battery(0x0fff);
    EXPECT_TX("battery max", 0x7b, 0x12, 0x53, 0x00, 0x04, 0x00, 0x00, 0x7d, 0x01, 0x05, 0xff, 0x0f);
    annepro2_ble_send_battery(0);
    EXPECT_TX("battery zero", 0x7b, 0x12, 0x53, 0x00, 0x04, 0x00, 0x00, 0x7d, 0x01, 0x05, 0x00, 0x00);

    CASE("pair");
    setup();
    for (uint8_t slot = 0; slot < 4; slot++) {
        annepro2_ble_pair(slot);
        const uint8_t exp[] = {0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x40, 0x01, slot};
        expect_tx("pair", exp, sizeof(exp));
    }
    annepro2_ble_pair(9); /* a slot above 3 is slot 3 */
    EXPECT_TX("pair 9", 0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x40, 0x01, 0x03);

    CASE("connect");
    setup();
    for (uint8_t slot = 0; slot < 4; slot++) {
        annepro2_ble_connect(slot);
        const uint8_t exp[] = {0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x40, 0x04, slot};
        expect_tx("connect", exp, sizeof(exp));
    }
    annepro2_ble_connect(200);
    EXPECT_TX("connect 200", 0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x40, 0x04, 0x03);

    CASE("unpair: bonds deleted, then the link ended");
    setup();
    annepro2_ble_unpair();
    EXPECT_TX("unpair", 0x7b, 0x12, 0x53, 0x00, 0x02, 0x00, 0x00, 0x7d, 0x40, 0x05, /* 40 05 */
              0x7b, 0x12, 0x53, 0x00, 0x02, 0x00, 0x00, 0x7d, 0x40, 0x02);          /* 40 02 */

    CASE("no frame is larger than its header says");
    setup();
    annepro2_ble_pair(1);
    annepro2_ble_connect(1);
    annepro2_ble_send_battery(1234);
    annepro2_ble_request_version();
    annepro2_ble_unpair();
    size_t pos = 0;
    int    frames = 0;
    while (pos < SD1.tx_len) {
        CHECK_EQ(SD1.tx[pos], 0x7b);
        CHECK_EQ(SD1.tx[pos + 7], 0x7d);
        pos += 8 + (SD1.tx[pos + 4] | (SD1.tx[pos + 5] << 8));
        frames++;
    }
    CHECK_EQ(pos, SD1.tx_len);
    CHECK_EQ(frames, 6);
}

static void case_serial(void) {
    /* annepro2_ble.c annepro2_ble_send_serial(): the UDID of the device record (at +8, 16 bytes) as 32 upper-case hex
     * digits, with id 2 (DIS Serial Number) in front; nothing when the record is blank or erased. */
    CASE("serial number from the device record");
    setup();
    memset(fake_device_record, 0xAA, 8); /* the 8 bytes before the UDID are not part of it */
    for (int i = 0; i < 15; i++) fake_device_record[8 + i] = (uint8_t)(i * 0x11);
    fake_device_record[8 + 15] = 0xfa;
    memset(fake_device_record + 24, 0xBB, 8);
    annepro2_ble_send_serial();
    EXPECT_TX("serial", 0x7b, 0x12, 0x53, 0x00, 0x23, 0x00, 0x00, 0x7d, 0x01, 0x10, 0x02,
              '0', '0', '1', '1', '2', '2', '3', '3', '4', '4', '5', '5', '6', '6', '7', '7',
              '8', '8', '9', '9', 'A', 'A', 'B', 'B', 'C', 'C', 'D', 'D', 'E', 'E', 'F', 'A');

    CASE("serial number: every nibble and upper case");
    setup();
    for (int i = 0; i < 16; i++) fake_device_record[8 + i] = (uint8_t)(0xf0 | i);
    annepro2_ble_send_serial();
    CHECK_EQ(SD1.tx_len, 8 + 2 + 33);
    CHECK_BYTES(SD1.tx + 11, "F0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFF", 32);

    CASE("serial number: blank or erased record sends nothing");
    setup();
    annepro2_ble_send_serial();
    CHECK_EQ(SD1.tx_len, 0);
    memset(fake_device_record + 8, 0xff, 16);
    annepro2_ble_send_serial();
    CHECK_EQ(SD1.tx_len, 0);

    CASE("serial number: one byte that is neither 00 nor ff is a record");
    setup();
    fake_device_record[8 + 7] = 0x5a;
    annepro2_ble_send_serial();
    CHECK_EQ(SD1.tx_len, 8 + 2 + 33);
    CHECK_BYTES(SD1.tx + 11, "00000000000000", 14);
    CHECK_BYTES(SD1.tx + 11 + 14, "5A", 2);

    CASE("serial number: a mix of 00 and ff only is a record");
    setup();
    memset(fake_device_record + 8, 0xff, 16);
    fake_device_record[8] = 0x00;
    annepro2_ble_send_serial();
    CHECK_EQ(SD1.tx_len, 8 + 2 + 33);
    CHECK_BYTES(SD1.tx + 11, "00FFFFFF", 8);
}

/* The two keyboard report layouts: 8 bytes for the BLE MCU whichever the layout of QMK's report. */
static void case_reports(void) {
    CASE("keyboard report");
    setup();
    annepro2_ble_use_driver();
    host_driver_t *d = host_get_driver();
    CHECK(d != NULL);
    CHECK(annepro2_ble_is_active());
    report_keyboard_t r;
    memset(&r, 0, sizeof(r));
#ifdef KEYBOARD_SHARED_EP
    CHECK_EQ(sizeof(report_keyboard_t), 9);
    r.report_id = 0xAB; /* must not reach the BLE MCU */
#else
    CHECK_EQ(sizeof(report_keyboard_t), 8);
#endif
    r.mods     = 0x22;
    r.reserved = 0x00;
    for (int i = 0; i < 6; i++) r.keys[i] = (uint8_t)(0x04 + i);
    fake_sd_tx_clear(&SD1);
    d->send_keyboard(&r);
    EXPECT_TX("report", 0x7b, 0x12, 0x53, 0x00, 0x0a, 0x00, 0x00, 0x7d, 0x10, 0x04, /* header, 10 04 */
              0x22, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09);

    CASE("empty keyboard report");
    memset(&r, 0, sizeof(r));
    d->send_keyboard(&r);
    EXPECT_TX("empty report", 0x7b, 0x12, 0x53, 0x00, 0x0a, 0x00, 0x00, 0x7d, 0x10, 0x04, 0, 0, 0, 0, 0, 0, 0, 0);

    CASE("keyboard report: a reserved byte is passed on as it is");
    memset(&r, 0, sizeof(r));
    r.reserved = 0x5a;
    r.keys[5]  = 0xe7;
    d->send_keyboard(&r);
    EXPECT_TX("reserved", 0x7b, 0x12, 0x53, 0x00, 0x0a, 0x00, 0x00, 0x7d, 0x10, 0x04, 0, 0x5a, 0, 0, 0, 0, 0, 0xe7);

    CASE("consumer report");
    static const struct {
        uint16_t usage;
        uint8_t  bit;
    } usages[] = {
        {AUDIO_VOL_DOWN, 0x04}, {AUDIO_VOL_UP, 0x02}, {AUDIO_MUTE, 0x01}, {TRANSPORT_PLAY_PAUSE, 0x08},
        {TRANSPORT_NEXT_TRACK, 0x10}, {TRANSPORT_PREV_TRACK, 0x20}, {BRIGHTNESS_UP, 0x40}, {BRIGHTNESS_DOWN, 0x80},
        {0, 0x00}, {0x1234, 0x00}, {TRANSPORT_STOP, 0x00},
    };
    for (size_t i = 0; i < sizeof(usages) / sizeof(usages[0]); i++) {
        report_extra_t e = {.report_id = REPORT_ID_CONSUMER, .usage = usages[i].usage};
        d->send_extra(&e);
        const uint8_t exp[] = {0x7b, 0x12, 0x53, 0x00, 0x06, 0x00, 0x00, 0x7d, 0x10, 0x08, usages[i].bit, 0, 0, 0};
        expect_tx("consumer", exp, sizeof(exp));
    }

    CASE("system report is not sent");
    report_extra_t sys = {.report_id = REPORT_ID_SYSTEM, .usage = 0x81};
    d->send_extra(&sys);
    CHECK_EQ(SD1.tx_len, 0);

    CASE("mouse report is not sent");
    report_mouse_t m;
    memset(&m, 0, sizeof(m));
    m.x = 5;
    d->send_mouse(&m);
    CHECK_EQ(SD1.tx_len, 0);

    CASE("no NKRO sender: QMK sends the 6 key report");
    CHECK(d->send_nkro == NULL);
}

static void case_driver_switch(void) {
    CASE("connect switches to the BLE driver and turns NKRO off, disconnect puts both back");
    setup();
    keymap_config.nkro = true;
    host_driver_t *usb = host_get_driver();
    CHECK(usb == &chibios_driver);
    annepro2_ble_connect(0);
    CHECK(annepro2_ble_is_active());
    CHECK(host_get_driver() != usb);
    CHECK(!keymap_config.nkro);
    CHECK_EQ(fake_calls.clear_keyboard, 1); /* the keys held on the USB side are released */
    annepro2_ble_disconnect();
    CHECK(!annepro2_ble_is_active());
    CHECK(host_get_driver() == usb);
    CHECK(keymap_config.nkro);
    CHECK_EQ(fake_calls.clear_keyboard, 2);

    CASE("NKRO that was off stays off");
    setup();
    keymap_config.nkro = false;
    annepro2_ble_connect(0);
    annepro2_ble_disconnect();
    CHECK(!keymap_config.nkro);

    CASE("a second switch does not overwrite the saved state");
    setup();
    keymap_config.nkro = true;
    annepro2_ble_connect(0);
    annepro2_ble_connect(1);
    annepro2_ble_use_driver();
    CHECK_EQ(fake_calls.clear_keyboard, 1);
    annepro2_ble_disconnect();
    CHECK(host_get_driver() == &chibios_driver);
    CHECK(keymap_config.nkro);

    CASE("disconnect with the USB driver does nothing to it");
    setup();
    keymap_config.nkro = true;
    annepro2_ble_disconnect();
    CHECK(host_get_driver() == &chibios_driver);
    CHECK(keymap_config.nkro);
    CHECK_EQ(fake_calls.clear_keyboard, 0);
    CHECK_EQ(fake_calls.host_set_driver, 0);

    CASE("two round trips");
    setup();
    for (int i = 0; i < 2; i++) {
        keymap_config.nkro = (i == 0);
        annepro2_ble_use_driver();
        CHECK(!keymap_config.nkro);
        annepro2_ble_disconnect();
        CHECK_EQ(keymap_config.nkro, i == 0);
        CHECK(host_get_driver() == &chibios_driver);
    }
}

static bool leds_caps(void) {
    return (host_get_driver()->keyboard_leds() >> 1) & 1;
}

static void case_capslock(void) {
    /* annepro2_ble.c: ble_capslock is kept for the connected host; a new host starts with Caps Lock off */
    CASE("the BLE driver reports the state as bit 1");
    setup();
    annepro2_ble_use_driver();
    CHECK_EQ(host_get_driver()->keyboard_leds(), 0);
    annepro2_ble_set_capslock(true);
    CHECK_EQ(host_get_driver()->keyboard_leds(), 2);
    annepro2_ble_set_capslock(false);
    CHECK_EQ(host_get_driver()->keyboard_leds(), 0);

    CASE("pairing resets it");
    setup();
    annepro2_ble_use_driver();
    annepro2_ble_set_capslock(true);
    annepro2_ble_pair(2);
    CHECK(!leds_caps());

    CASE("connect to the same slot keeps it, to another slot resets it");
    setup();
    annepro2_ble_connect(1); /* the slot is not known before: the state starts false anyway */
    annepro2_ble_set_capslock(true);
    annepro2_ble_connect(1);
    CHECK(leds_caps());
    annepro2_ble_connect(2);
    CHECK(!leds_caps());
    annepro2_ble_set_capslock(true);
    annepro2_ble_connect(2);
    CHECK(leds_caps());

    CASE("pair then connect to the same slot keeps it");
    setup();
    annepro2_ble_pair(3);
    annepro2_ble_set_capslock(true);
    annepro2_ble_connect(3);
    CHECK(leds_caps());

    CASE("a connected host resets it");
    setup();
    annepro2_ble_use_driver();
    annepro2_ble_set_capslock(true);
    annepro2_ble_host_connected();
    CHECK(!leds_caps());

    CASE("unpair resets it and forgets the slot");
    setup();
    annepro2_ble_connect(1);
    annepro2_ble_set_capslock(true);
    annepro2_ble_unpair();
    CHECK(!leds_caps());
    annepro2_ble_set_capslock(true);
    annepro2_ble_connect(1); /* the slot was forgotten: this is another host */
    CHECK(!leds_caps());

    CASE("the state is kept in USB mode");
    setup();
    annepro2_ble_connect(0);
    annepro2_ble_set_capslock(true);
    annepro2_ble_disconnect();
    annepro2_ble_connect(0); /* the same host again */
    CHECK(leds_caps());
}

static void case_broadcast(void) {
    /* annepro2_ble.c: without AP2_BLE_SLOT_TAP_HOLD the first press pairs, a second press on the same slot connects */
    CASE("first press pairs, second press on the same slot pairs and connects");
    setup();
    annepro2_ble_broadcast(1);
    EXPECT_TX("first", 0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x40, 0x01, 0x01);
    CHECK(!annepro2_ble_is_active()); /* pairing leaves the reports where they are */
    annepro2_ble_broadcast(1);
    EXPECT_TX("second", 0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x40, 0x01, 0x01, /* 40 01 1 */
              0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x40, 0x04, 0x01);          /* 40 04 1 */
    CHECK(annepro2_ble_is_active());

    CASE("another slot pairs again");
    setup();
    annepro2_ble_broadcast(1);
    annepro2_ble_broadcast(2);
    fake_sd_tx_clear(&SD1);
    annepro2_ble_broadcast(2);
    CHECK_EQ(SD1.tx_len, 22);

    CASE("unpair forgets the last slot");
    setup();
    annepro2_ble_broadcast(1);
    annepro2_ble_unpair();
    fake_sd_tx_clear(&SD1);
    annepro2_ble_broadcast(1);
    EXPECT_TX("after unpair", 0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x40, 0x01, 0x01);
}

static void set_usb(usbstate_t state, usbstate_t saved) {
    USBD1.state       = state;
    USBD1.saved_state = saved;
}

static void case_connect_timeout(void) {
    /* AP2_BLE_CONNECT_TIMEOUT_MS (6000): a connect that moved the reports from a USB host to Bluetooth falls back to
     * the USB host when no host has connected by then (annepro2_ble.c, readme.md). */
    CASE("the connect falls back after 6000 ms");
    setup();
    keymap_config.nkro = true;
    annepro2_ble_connect(0);
    CHECK(annepro2_ble_is_active());
    fake_advance(AP2_BLE_CONNECT_TIMEOUT_MS - 1);
    CHECK(!annepro2_ble_task());
    CHECK(annepro2_ble_is_active());
    fake_advance(1);
    CHECK(annepro2_ble_task()); /* returns true: the reports went back */
    CHECK(!annepro2_ble_is_active());
    CHECK(host_get_driver() == &chibios_driver);
    CHECK(keymap_config.nkro);
    CHECK(!annepro2_ble_task()); /* only once */

    CASE("a host that connects in time cancels the fall back");
    setup();
    annepro2_ble_connect(0);
    fake_advance(3000);
    annepro2_ble_host_connected();
    fake_advance(10000);
    CHECK(!annepro2_ble_task());
    CHECK(annepro2_ble_is_active());

    CASE("a second tap starts the time again");
    setup();
    annepro2_ble_connect(0);
    fake_advance(4000);
    annepro2_ble_connect(0);
    fake_advance(5999);
    CHECK(!annepro2_ble_task());
    fake_advance(1);
    CHECK(annepro2_ble_task());

    CASE("another slot before the end also starts the time again");
    setup();
    annepro2_ble_connect(0);
    fake_advance(5000);
    annepro2_ble_connect(1);
    fake_advance(5000);
    CHECK(!annepro2_ble_task());
    fake_advance(1000);
    CHECK(annepro2_ble_task());

    CASE("a connect that did not move the reports from a USB host has no fall back");
    setup();
    annepro2_ble_use_driver(); /* already on Bluetooth (the switch, or the end of a pairing) */
    annepro2_ble_connect(0);
    fake_advance(60000);
    CHECK(!annepro2_ble_task());
    CHECK(annepro2_ble_is_active());

    CASE("no USB host in use: the reports stay on Bluetooth");
    setup();
    annepro2_ble_connect(0);
    set_usb(USB_STOP, USB_STOP); /* the cable was unplugged meanwhile */
    fake_advance(6000);
    CHECK(!annepro2_ble_task());
    CHECK(annepro2_ble_is_active());

    CASE("no USB host at the connect: nothing to fall back to, even if one appears later");
    setup();
    set_usb(USB_STOP, USB_STOP);
    annepro2_ble_connect(0);
    set_usb(USB_ACTIVE, USB_ACTIVE);
    fake_advance(60000);
    CHECK(!annepro2_ble_task());
    CHECK(annepro2_ble_is_active());

    CASE("a sleeping USB host counts as in use");
    setup();
    set_usb(USB_SUSPENDED, USB_ACTIVE);
    annepro2_ble_connect(0);
    fake_advance(6000);
    CHECK(annepro2_ble_task());
    CHECK(!annepro2_ble_is_active());

    CASE("a suspended bus that never had a host does not count");
    setup();
    set_usb(USB_SUSPENDED, USB_READY);
    annepro2_ble_connect(0);
    fake_advance(6000);
    CHECK(!annepro2_ble_task());
    CHECK(annepro2_ble_is_active());

    CASE("disconnect before the end clears the pending connect");
    setup();
    annepro2_ble_connect(0);
    annepro2_ble_disconnect();
    annepro2_ble_use_driver();
    fake_advance(6000);
    CHECK(!annepro2_ble_task());
    CHECK(annepro2_ble_is_active());

    CASE("the 16-bit timer wraps");
    setup();
    fake_ms = 65536u * 7 - 3000; /* 3 s before the low 16 bits wrap */
    annepro2_ble_connect(0);
    fake_advance(5999);
    CHECK(!annepro2_ble_task());
    fake_advance(1);
    CHECK(annepro2_ble_task());
}

static void case_pair_timeout(void) {
    /* AP2_BLE_PAIR_TIMEOUT_MS (60000): the pairing state ends, so that a later unrelated connection does not move the
     * reports (annepro2_ble.c comment at pair_pending, readme.md). A host that connects after pairing moves the
     * reports to Bluetooth unless a USB host is in use. */
    CASE("a host that connects after pairing moves the reports when no USB host is in use");
    setup();
    set_usb(USB_STOP, USB_STOP);
    annepro2_ble_pair(0);
    CHECK(!annepro2_ble_is_active());
    annepro2_ble_host_connected();
    CHECK(annepro2_ble_is_active());

    CASE("with a USB host in use the reports stay");
    setup();
    annepro2_ble_pair(0);
    annepro2_ble_host_connected();
    CHECK(!annepro2_ble_is_active());

    CASE("with a sleeping USB host in use the reports stay");
    setup();
    set_usb(USB_SUSPENDED, USB_ACTIVE);
    annepro2_ble_pair(0);
    annepro2_ble_host_connected();
    CHECK(!annepro2_ble_is_active());

    CASE("a connection without pairing does not move the reports");
    setup();
    set_usb(USB_STOP, USB_STOP);
    annepro2_ble_host_connected();
    CHECK(!annepro2_ble_is_active());

    CASE("the pairing state ends after 60000 ms");
    setup();
    set_usb(USB_STOP, USB_STOP);
    annepro2_ble_pair(0);
    fake_advance(AP2_BLE_PAIR_TIMEOUT_MS - 1);
    CHECK(!annepro2_ble_task());
    annepro2_ble_host_connected();
    CHECK(annepro2_ble_is_active()); /* 59999 ms: still pairing */

    setup();
    set_usb(USB_STOP, USB_STOP);
    annepro2_ble_pair(0);
    fake_advance(AP2_BLE_PAIR_TIMEOUT_MS);
    CHECK(!annepro2_ble_task());
    annepro2_ble_host_connected();
    CHECK(!annepro2_ble_is_active()); /* 60000 ms: over */

    CASE("pairing again starts the time again");
    setup();
    set_usb(USB_STOP, USB_STOP);
    annepro2_ble_pair(0);
    fake_advance(50000);
    annepro2_ble_pair(1);
    fake_advance(50000);
    annepro2_ble_task();
    annepro2_ble_host_connected();
    CHECK(annepro2_ble_is_active());

    CASE("a connect ends the pairing state");
    setup();
    set_usb(USB_STOP, USB_STOP);
    annepro2_ble_pair(0);
    annepro2_ble_connect(0);
    annepro2_ble_disconnect(); /* back to USB; the pairing flag must be gone */
    annepro2_ble_host_connected();
    CHECK(!annepro2_ble_is_active());

    CASE("unpair and disconnect end the pairing state");
    setup();
    set_usb(USB_STOP, USB_STOP);
    annepro2_ble_pair(0);
    annepro2_ble_unpair();
    annepro2_ble_host_connected();
    CHECK(!annepro2_ble_is_active());
    setup();
    set_usb(USB_STOP, USB_STOP);
    annepro2_ble_pair(0);
    annepro2_ble_disconnect();
    annepro2_ble_host_connected();
    CHECK(!annepro2_ble_is_active());

    CASE("the 16-bit timer wraps");
    setup();
    set_usb(USB_STOP, USB_STOP);
    fake_ms = 65536u * 9 - 20000;
    annepro2_ble_pair(0);
    fake_advance(59999);
    annepro2_ble_task();
    annepro2_ble_host_connected();
    CHECK(annepro2_ble_is_active());
    setup();
    set_usb(USB_STOP, USB_STOP);
    fake_ms = 65536u * 9 - 20000;
    annepro2_ble_pair(0);
    fake_advance(60000);
    annepro2_ble_task();
    annepro2_ble_host_connected();
    CHECK(!annepro2_ble_is_active());

    CASE("pair replaces a connect attempt: the reports go back to the USB host first");
    setup();
    keymap_config.nkro = true;
    annepro2_ble_connect(0);
    CHECK(annepro2_ble_is_active());
    fake_sd_tx_clear(&SD1);
    annepro2_ble_pair(1);
    CHECK(!annepro2_ble_is_active());
    CHECK(keymap_config.nkro);
    EXPECT_TX("pair after connect", 0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x40, 0x01, 0x01);
    fake_advance(60000);
    CHECK(!annepro2_ble_task()); /* the connect fall back is not pending any more */
    CHECK(!annepro2_ble_is_active());
}

int main(void) {
    case_simple_frames();
    case_serial();
    case_reports();
    case_driver_switch();
    case_capslock();
    case_broadcast();
    case_connect_timeout();
    case_pair_timeout();
    return TEST_RESULT();
}
