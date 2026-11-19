/* The keys of the keyboard through process_record_kb(), and the Bluetooth switch through matrix_scan_kb(): the typing
 * sweep over all key codes, the four slot keys (tap connects, hold pairs; readme.md AP2_BLE_SLOT_TAP_HOLD, annepro2.c
 * slot_event() and slot_tick()), KC_AP2_BT_UNPAIR (readme.md "KC_AP2_BT_UNPAIR deletes the bonds ...", annepro2.c
 * unpair_event(), unpair_tick(), unpair_now()), KC_AP2_USB, KC_AP2_BATT (the level on the number row) and the switch on
 * PC10 (readme.md AP2_BT_SWITCH, annepro2.c bt_switch_task()). The expectations are the frames on SD1, the LED commands
 * on SD0 and the driver in use, not the statics of the sources. */
#include "all_sources.h"
#include "proto_decode.h"

static void setup(void) {
    reset_firmware();
    fake_pin_c10 = true; /* the Bluetooth switch is off (USB) */
    ble_ready    = true; /* the BLE MCU has answered: no handshake pings in the scans */
}

static bool key(uint16_t kc, bool pressed, uint8_t row, uint8_t col) {
    keyrecord_t r;
    memset(&r, 0, sizeof(r));
    r.event.pressed = pressed;
    r.event.key.row = row;
    r.event.key.col = col;
    return process_record_kb(kc, &r);
}

static void scan(void) { /* one scan; the output queues drain between scans */
    fake_sd_tx_clear(&SD0);
    matrix_scan_kb();
}

static size_t count_cmd(uint8_t cmd) {
    const size_t n = decode_tx(&SD0, NULL);
    size_t       k = 0;
    for (size_t i = 0; i < n; i++) k += dec_msgs[i].cmd == cmd;
    return k;
}
static const dec_msg_t *first_cmd(uint8_t cmd) {
    const size_t n = decode_tx(&SD0, NULL);
    for (size_t i = 0; i < n; i++)
        if (dec_msgs[i].cmd == cmd) return &dec_msgs[i];
    return NULL;
}

#define FRAME(...) ((const uint8_t[]){0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, __VA_ARGS__})

static void case_sweep(void) {
    CASE("typing: no key code outside the keyboard's own block is swallowed or makes the keyboard send anything");
    setup();
    unsigned swallowed = 0;
    for (uint32_t kc = 0; kc <= 0xFFFF; kc++) {
        if (kc >= KC_AP2_BT1 && kc <= KC_AP2_BATT) continue; /* the 18 key codes of annepro2.h */
        if (kc == QK_BOOT) continue;                         /* the keymap takes it (test_keymap_boot.c) */
        fake_sd_tx_clear(&SD0);
        fake_sd_tx_clear(&SD1);
        for (int pressed = 0; pressed < 2; pressed++) {
            if (!key((uint16_t)kc, pressed, 1, 1)) swallowed++;
        }
        if (SD0.tx_len || SD1.tx_len) swallowed += 1000000;
    }
    CHECK_EQ(swallowed, 0);

    CASE("typing: the 18 key codes of the keyboard are handled on the press, none is passed on");
    setup();
    for (uint16_t kc = KC_AP2_BT1; kc <= KC_AP2_BATT; kc++) {
        CHECK(!key(kc, true, 1, 1));
        scan(); /* a held slot or unpair key must not leave a timer behind */
        key(kc, false, 1, 1);
    }
}

static void case_slots(void) {
    CASE("BT key: a tap connects the slot and blinks the key blue");
    setup();
    CHECK(!key(KC_AP2_BT2, true, 0, 2));
    fake_advance(100);
    CHECK(!key(KC_AP2_BT2, false, 0, 2));
    CHECK_EQ(SD1.tx_len, 11);
    CHECK_BYTES(SD1.tx, FRAME(0x40, 0x04, 0x01), 11);
    CHECK(annepro2_ble_is_active());
    const dec_msg_t *b = first_cmd(CMD_LED_KEY_BLINK);
    CHECK(b != NULL);
    if (b) {
        const uint8_t exp[] = {0, 2, 0xff, 0x00, 0x00, 0xff, 8, 50};
        CHECK_EQ(b->size, 8);
        CHECK_BYTES(b->payload, exp, 8);
    }

    CASE("BT key: every slot key sends its own slot");
    for (uint8_t s = 0; s < 4; s++) {
        setup();
        key(KC_AP2_BT1 + s, true, 0, 1 + s);
        key(KC_AP2_BT1 + s, false, 0, 1 + s);
        const uint8_t exp[] = {0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x40, 0x04, s};
        CHECK_EQ(SD1.tx_len, 11);
        CHECK_BYTES(SD1.tx, exp, 11);
    }

    CASE("BT key: held for AP2_BLE_SLOT_HOLD_MS it pairs (once), on every slot, and the release does nothing more");
    for (uint8_t s = 0; s < 4; s++) {
        setup();
        key(KC_AP2_BT1 + s, true, 0, 1 + s);
        fake_advance(AP2_BLE_SLOT_HOLD_MS - 1);
        scan();
        CHECK_EQ(SD1.tx_len, 0);
        fake_advance(1);
        scan();
        CHECK_EQ(SD1.tx_len, 11);
        CHECK_BYTES(SD1.tx, FRAME(0x40, 0x01, s), 11);
        CHECK_EQ(count_cmd(CMD_LED_KEY_BLINK), 1);
        CHECK(!annepro2_ble_is_active()); /* pairing leaves the reports where they are */
        fake_sd_tx_clear(&SD1);
        fake_advance(2000);
        scan();
        key(KC_AP2_BT1 + s, false, 0, 1 + s);
        CHECK_EQ(SD1.tx_len, 0);
    }

    /* A tap connects on the release; the hold time must not stay pending, or the slot would also pair 500 ms later. */
    CASE("BT key: a tap does not pair later");
    setup();
    key(KC_AP2_BT2, true, 0, 2);
    fake_advance(100);
    key(KC_AP2_BT2, false, 0, 2);
    for (uint32_t t = 0; t < 2 * AP2_BLE_SLOT_HOLD_MS; t += 10) {
        fake_advance(10);
        scan();
    }
    CHECK_EQ(SD1.tx_len, 11);
    CHECK_BYTES(SD1.tx, FRAME(0x40, 0x04, 0x01), 11);

    /* timer_elapsed32() of the hold time must survive the wrap of the 32-bit millisecond counter (49.7 days). */
    CASE("BT key: the hold time is measured across the 32-bit timer wrap");
    setup();
    fake_ms = 0xFFFFFFFFu - 200;
    key(KC_AP2_BT4, true, 0, 4);
    fake_advance(AP2_BLE_SLOT_HOLD_MS - 1);
    scan();
    CHECK_EQ(SD1.tx_len, 0);
    fake_advance(1);
    scan();
    CHECK_EQ(SD1.tx_len, 11);
    CHECK_BYTES(SD1.tx, FRAME(0x40, 0x01, 0x03), 11);

    CASE("BT key: two slot keys down at once are timed separately");
    setup();
    key(KC_AP2_BT1, true, 0, 1);
    fake_advance(300);
    key(KC_AP2_BT2, true, 0, 2);
    fake_advance(200);
    scan();
    CHECK_EQ(SD1.tx_len, 11);
    CHECK_BYTES(SD1.tx, FRAME(0x40, 0x01, 0x00), 11); /* slot 1 (index 0) paired */
    fake_sd_tx_clear(&SD1);
    key(KC_AP2_BT2, false, 0, 2); /* slot 2 released after 200 ms: connect */
    CHECK_BYTES(SD1.tx, FRAME(0x40, 0x04, 0x01), 11);
}

static void case_unpair_usb(void) {
    CASE("unpair key: only after AP2_BLE_UNPAIR_HOLD_MS; then 40 05, 40 02 and a red blink");
    setup();
    key(KC_AP2_BT_UNPAIR, true, 0, 5);
    fake_advance(AP2_BLE_UNPAIR_HOLD_MS - 1);
    scan();
    CHECK_EQ(SD1.tx_len, 0);
    fake_advance(1);
    scan();
    CHECK_EQ(SD1.tx_len, 20);
    CHECK_BYTES(SD1.tx, ((const uint8_t[]){0x7b, 0x12, 0x53, 0x00, 0x02, 0x00, 0x00, 0x7d, 0x40, 0x05}), 10);
    CHECK_BYTES(SD1.tx + 10, ((const uint8_t[]){0x7b, 0x12, 0x53, 0x00, 0x02, 0x00, 0x00, 0x7d, 0x40, 0x02}), 10);
    const dec_msg_t *b = first_cmd(CMD_LED_KEY_BLINK);
    CHECK(b != NULL);
    if (b) {
        const uint8_t exp[] = {0, 5, 0x00, 0x00, 0xff, 0xff, 8, 50};
        CHECK_BYTES(b->payload, exp, 8);
    }
    fake_sd_tx_clear(&SD1);
    fake_advance(5000);
    scan();
    CHECK_EQ(SD1.tx_len, 0); /* once */

    CASE("unpair key: released before the time, nothing is deleted, and a new press starts the time again");
    setup();
    key(KC_AP2_BT_UNPAIR, true, 0, 5);
    fake_advance(AP2_BLE_UNPAIR_HOLD_MS - 1);
    key(KC_AP2_BT_UNPAIR, false, 0, 5);
    fake_advance(10);
    scan();
    CHECK_EQ(SD1.tx_len, 0);
    key(KC_AP2_BT_UNPAIR, true, 0, 5);
    fake_advance(AP2_BLE_UNPAIR_HOLD_MS - 1);
    scan();
    CHECK_EQ(SD1.tx_len, 0);

    CASE("unpair key: 16-bit timer across the wrap");
    setup();
    fake_ms = 65536u * 3 - 1000;
    key(KC_AP2_BT_UNPAIR, true, 0, 5);
    for (int i = 0; i < 2999; i++) { fake_advance(1); scan(); }
    CHECK_EQ(SD1.tx_len, 0);
    fake_advance(1);
    scan();
    CHECK_EQ(SD1.tx_len, 20);

    /* annepro2.c unpair_now(): the switch back to USB sends an empty report to the BLE MCU when the last report held a
     * key, and a report makes an idle BLE MCU advertise again; sent after the unpair commands it would leave the BLE
     * MCU advertising for a new pairing. So clear_keyboard() comes first, then 40 05, then 40 02. */
    CASE("unpair key while on Bluetooth: the reports go back to USB before 40 05 and 40 02 are sent");
    setup();
    key(KC_AP2_BT1, true, 0, 1);
    key(KC_AP2_BT1, false, 0, 1);
    CHECK(annepro2_ble_is_active());
    fake_sd_tx_clear(&SD1);
    key(KC_AP2_BT_UNPAIR, true, 0, 5);
    fake_advance(AP2_BLE_UNPAIR_HOLD_MS);
    fake_calls.clear_keyboard = 0;
    scan();
    CHECK(!annepro2_ble_is_active());
    CHECK_EQ(fake_calls.clear_keyboard, 1);
    CHECK_EQ(fake_calls.clear_keyboard_sd1_at, 0); /* nothing of the unpair had been sent yet */
    CHECK_EQ(SD1.tx_len, 20);
    CHECK_BYTES(SD1.tx, ((const uint8_t[]){0x7b, 0x12, 0x53, 0x00, 0x02, 0x00, 0x00, 0x7d, 0x40, 0x05}), 10);
    CHECK_BYTES(SD1.tx + 10, ((const uint8_t[]){0x7b, 0x12, 0x53, 0x00, 0x02, 0x00, 0x00, 0x7d, 0x40, 0x02}), 10);

    CASE("USB key: back to the USB driver and the blink is cancelled");
    setup();
    key(KC_AP2_BT1, true, 0, 1);
    key(KC_AP2_BT1, false, 0, 1);
    CHECK(annepro2_ble_is_active());
    fake_sd_tx_clear(&SD0);
    CHECK(!key(KC_AP2_USB, true, 0, 6));
    CHECK(!annepro2_ble_is_active());
    const dec_msg_t *c = first_cmd(CMD_LED_KEY_BLINK);
    CHECK(c != NULL);
    if (c) CHECK_EQ(c->payload[6], 0); /* count 0: the blink is cancelled, not restarted */
    fake_sd_tx_clear(&SD1);
    key(KC_AP2_USB, false, 0, 6); /* the release does nothing */
    CHECK_EQ(SD1.tx_len, 0);
}

/* every scan the output queue drains; count the messages of one kind over a few scans after an event */
static size_t cmds_over_scans(uint8_t cmd, uint8_t *cols) {
    size_t total = 0;
    for (int i = 0; i < 12; i++) {
        const size_t n = decode_tx(&SD0, NULL);
        for (size_t j = 0; j < n; j++) {
            if (dec_msgs[j].cmd == cmd && total < 10) cols[total++] = dec_msgs[j].payload[1];
        }
        scan();
    }
    return total;
}

static void case_battery_key(void) {
    CASE("KC_AP2_BATT: the first press shows the level on the number row (keys 1 to 0), the second hides it");
    setup();
    annepro2_battery_init();
    FAKE_ADC_REG(0x088) = 1;
    FAKE_ADC_REG(0x030) = 2600;
    uint8_t cols[10];
    CHECK(!key(KC_AP2_BATT, true, 0, 9));
    CHECK_EQ(cmds_over_scans(CMD_LED_STICKY_SET_KEY, cols), 10);
    for (int i = 0; i < 10; i++) CHECK_EQ(cols[i], 1 + i);
    fake_sd_tx_clear(&SD0);
    key(KC_AP2_BATT, false, 0, 9); /* the release does nothing */
    CHECK_EQ(SD0.tx_len, 0);
    CHECK(!key(KC_AP2_BATT, true, 0, 9));
    CHECK_EQ(cmds_over_scans(CMD_LED_STICKY_UNSET_KEY, cols), 10);
    for (int i = 0; i < 10; i++) CHECK_EQ(cols[i], 1 + i);
}

static void case_switch(void) {
    CASE("BT switch: Bluetooth position selects the Bluetooth driver after the debounce time");
    setup();
    fake_pin_c10 = false;
    bt_switch_init();
    fake_advance(AP2_BT_SWITCH_DEBOUNCE_MS - 1);
    scan();
    CHECK(!annepro2_ble_is_active());
    fake_advance(1);
    scan();
    CHECK(annepro2_ble_is_active());

    CASE("BT switch: moving it to USB goes back to the USB driver");
    fake_pin_c10 = true;
    scan();
    fake_advance(AP2_BT_SWITCH_DEBOUNCE_MS - 1);
    scan();
    CHECK(annepro2_ble_is_active());
    fake_advance(1);
    scan();
    CHECK(!annepro2_ble_is_active());

    /* annepro2.c bt_switch_task(): the USB position cancels a running slot blink, like KC_AP2_USB does. */
    CASE("BT switch: moved to USB it cancels a running slot blink");
    setup();
    fake_pin_c10 = false;
    bt_switch_init();
    fake_advance(AP2_BT_SWITCH_DEBOUNCE_MS);
    scan();
    key(KC_AP2_BT1, true, 0, 1);
    key(KC_AP2_BT1, false, 0, 1); /* a tap: the key blinks */
    fake_pin_c10 = true;
    scan();
    fake_advance(AP2_BT_SWITCH_DEBOUNCE_MS);
    scan();
    CHECK(!annepro2_ble_is_active());
    const dec_msg_t *cb = first_cmd(CMD_LED_KEY_BLINK);
    CHECK(cb != NULL);
    if (cb) CHECK_EQ(cb->payload[6], 0); /* count 0: cancelled */

    /* bt_switch_timer is 32 bits and read with timer_elapsed32(): the debounce across the wrap of the counter. */
    CASE("BT switch: the debounce time is measured across the 32-bit timer wrap");
    setup();
    fake_ms      = 0xFFFFFFFFu - 20;
    fake_pin_c10 = true;
    bt_switch_init();
    fake_pin_c10 = false;
    scan(); /* the change is seen here */
    fake_advance(AP2_BT_SWITCH_DEBOUNCE_MS - 1);
    scan();
    CHECK(!annepro2_ble_is_active());
    fake_advance(1);
    scan();
    CHECK(annepro2_ble_is_active());

    CASE("BT switch: a contact that bounces every 10 ms selects nothing");
    setup();
    fake_pin_c10 = true;
    bt_switch_init();
    for (int i = 0; i < 100; i++) {
        fake_pin_c10 = !fake_pin_c10;
        fake_advance(10);
        scan();
    }
    CHECK(!annepro2_ble_is_active());
    CHECK_EQ(bt_switch_applied, -1);

    CASE("BT switch: it acts on a change only; KC_AP2_USB stays in force while the switch does not move");
    setup();
    fake_pin_c10 = false;
    bt_switch_init();
    fake_advance(100);
    scan();
    CHECK(annepro2_ble_is_active());
    key(KC_AP2_USB, true, 0, 6);
    CHECK(!annepro2_ble_is_active());
    for (int i = 0; i < 20; i++) { fake_advance(100); scan(); }
    CHECK(!annepro2_ble_is_active());

    CASE("BT switch: at power on the first reading is applied by the first scan after the debounce");
    setup();
    fake_pin_c10 = true;
    bt_switch_init();
    fake_advance(AP2_BT_SWITCH_DEBOUNCE_MS);
    scan();
    CHECK_EQ(bt_switch_applied, 1);
    CHECK(!annepro2_ble_is_active());
}

/* annepro2.c matrix_scan_kb() and annepro2_ble_pair(): a connect from a running USB host falls back to USB when it does
 * not complete in AP2_BLE_CONNECT_TIMEOUT_MS, and the slot blink is cancelled with it; a pairing replaces a connect
 * attempt, so the time-out of the connect does not end it. The switch is settled on USB first, otherwise the first
 * application of the switch position undoes the tap. */
static void settle_switch_usb(void) {
    bt_switch_init();
    fake_advance(AP2_BT_SWITCH_DEBOUNCE_MS);
    scan();
}

static void case_connect_timeout(void) {
    CASE("a connect that times out goes back to USB and cancels the slot blink");
    setup();
    settle_switch_usb();
    key(KC_AP2_BT1, true, 0, 1);
    key(KC_AP2_BT1, false, 0, 1);
    CHECK(annepro2_ble_is_active());
    size_t cancels = 0;
    for (uint32_t t = 0; t <= AP2_BLE_CONNECT_TIMEOUT_MS; t += 10) {
        fake_advance(10);
        fake_sd_tx_clear(&SD1);
        scan();
        const size_t n = decode_tx(&SD0, NULL);
        for (size_t i = 0; i < n; i++) cancels += dec_msgs[i].cmd == CMD_LED_KEY_BLINK && dec_msgs[i].payload[6] == 0;
    }
    CHECK(!annepro2_ble_is_active());
    CHECK_EQ(cancels, 1);

    CASE("a pairing started after a connect from USB is not ended by the connect time-out");
    setup();
    settle_switch_usb();
    key(KC_AP2_BT1, true, 0, 1);
    key(KC_AP2_BT1, false, 0, 1); /* connect from an active USB host: the fall back is armed */
    USBD1.state = USB_STOP;       /* the cable is pulled */
    key(KC_AP2_BT2, true, 0, 2);
    fake_advance(AP2_BLE_SLOT_HOLD_MS);
    scan(); /* held: pair slot 2 */
    USBD1.state = USB_ACTIVE; /* the cable is back */
    key(KC_AP2_BT2, false, 0, 2);
    for (uint32_t t = 0; t <= AP2_BLE_CONNECT_TIMEOUT_MS + 100; t += 10) {
        fake_advance(10);
        fake_sd_tx_clear(&SD1);
        scan();
    }
    CHECK(annepro2_ble_is_active());
}

int main(void) {
    case_sweep();
    case_slots();
    case_unpair_usb();
    case_battery_key();
    case_switch();
    case_connect_timeout();
    return TEST_RESULT();
}
