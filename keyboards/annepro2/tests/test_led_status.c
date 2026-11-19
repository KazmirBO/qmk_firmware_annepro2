/* Status replies of the LED MCU on SD0 (annepro2.c led_receive(), led_status_received(), led_check_task(); readme.md
 * LED paragraph): the stored status, the reactive forward of key presses, the repair of a restarted LED MCU, its
 * settle time and limit, and the LED check across the 16-bit timer wrap. */
#include "all_sources.h"
#include "proto_decode.h"

static uint8_t status_id;

static void setup(void) {
    reset_firmware();
    proto_init(&proto, led_receive); /* what keyboard_pre_init_kb() does */
    fake_pin_c10 = true;             /* the Bluetooth switch is off: it does not select the driver */
    ble_ready    = true;             /* the BLE MCU has answered: no handshake pings in the scans */
    status_id    = 0;
}

/* The LED MCU answers: 7a 1d 41 <id> <size> profiles current matrix reactive intensity errors */
static void status(uint8_t matrix, uint8_t reactive, uint8_t size) {
    const uint8_t f[] = {0x7a, 0x1d, CMD_LED_STATUS, ++status_id, size, 3, 1, matrix, reactive, 2, 0};
    fake_sd_rx_push(&SD0, f, 5 + size);
}

static size_t count_cmd(uint8_t cmd) {
    const size_t n = decode_tx(&SD0, NULL);
    size_t       k = 0;
    for (size_t i = 0; i < n; i++) k += dec_msgs[i].cmd == cmd;
    return k;
}

static void lit(void) { /* the LEDs are on and the LED MCU was told so, 200 ms ago */
    fake_rgb_enabled = true;
    led_sync(true);
    fake_advance(200);
    fake_sd_tx_clear(&SD0);
    fake_advance(0);
}

static bool key(uint16_t kc, bool pressed, uint8_t row, uint8_t col) {
    keyrecord_t r;
    memset(&r, 0, sizeof(r));
    r.event.pressed = pressed;
    r.event.key.row = row;
    r.event.key.col = col;
    return process_record_kb(kc, &r);
}

static void scan(void) {
    fake_sd_tx_clear(&SD0); /* the queue of a running keyboard drains by itself */
    matrix_scan_kb();
}

static int index_of(uint8_t cmd) {
    const size_t n = decode_tx(&SD0, NULL);
    for (size_t i = 0; i < n; i++)
        if (dec_msgs[i].cmd == cmd) return (int)i;
    return -1;
}

int main(void) {
    CASE("status: the reply of the LED MCU is stored");
    setup();
    const uint8_t f[] = {0x7a, 0x1d, CMD_LED_STATUS, 1, 6, 7, 3, 1, 0, 5, 9}; /* all six fields different */
    fake_sd_rx_push(&SD0, f, sizeof(f));
    matrix_scan_kb();
    CHECK_EQ(ap2_led_status.amount_of_profiles, 7);
    CHECK_EQ(ap2_led_status.current_profile, 3);
    CHECK_EQ(ap2_led_status.matrix_enabled, 1);
    CHECK_EQ(ap2_led_status.is_reactive, 0);
    CHECK_EQ(ap2_led_status.led_intensity, 5);
    CHECK_EQ(ap2_led_status.errors, 9);

    CASE("status: a reactive profile forwards every key press, as row << 4 | column");
    setup();
    status(1, 1, 6);
    matrix_scan_kb();
    fake_sd_tx_clear(&SD0);
    keyrecord_t r = {0};
    r.event.pressed = true;
    r.event.key.row = 2;
    r.event.key.col = 3;
    CHECK(process_record_kb(KC_A, &r));
    CHECK_EQ(decode_tx(&SD0, NULL), 1);
    CHECK_EQ(dec_msgs[0].cmd, CMD_LED_KEY_DOWN);
    CHECK_EQ(dec_msgs[0].size, 1);
    CHECK_EQ(dec_msgs[0].payload[0], 0x23);

    CASE("status: a profile that is not reactive, or a matrix that is off, forwards nothing");
    setup();
    status(1, 0, 6);
    matrix_scan_kb();
    fake_sd_tx_clear(&SD0);
    r.event.pressed = true;
    process_record_kb(KC_A, &r);
    CHECK_EQ(SD0.tx_len, 0);
    setup();
    status(0, 1, 6);
    matrix_scan_kb();
    process_record_kb(KC_A, &r);
    CHECK_EQ(SD0.tx_len, 0);

    CASE("status: a reply of 5 bytes is too short and changes nothing");
    setup();
    status(1, 1, 5);
    matrix_scan_kb();
    CHECK_EQ(ap2_led_status.matrix_enabled, 0);

    CASE("status: a LED MCU that reports its matrix off while it should be on is set up again");
    setup();
    lit();
    status(0, 0, 6);
    scan();
    CHECK_EQ(count_cmd(CMD_LED_SET_MANUAL), 1);
    CHECK_EQ(count_cmd(CMD_LED_ON), 1);
    CHECK_EQ(fake_calls.led_update_kb, 1);
    CHECK_EQ(fake_calls.layer_state_set_kb, 1);

    /* annepro2.c "A status shortly after a power command can still answer an older command and is not used": the
     * time runs from a real power command (here the one of KC_AP_RGB_TOG), not from anything else. */
    CASE("status: the repair waits AP2_LED_STATUS_SETTLE_MS after a power command of a key");
    setup();
    lit();
    fake_advance(500);
    key(KC_AP_RGB_TOG, true, 0, 0); /* off */
    key(KC_AP_RGB_TOG, true, 0, 0); /* on: CMD_LED_ON now */
    fake_sd_tx_clear(&SD0);
    fake_advance(AP2_LED_STATUS_SETTLE_MS - 1);
    status(0, 0, 6);
    scan();
    CHECK_EQ(count_cmd(CMD_LED_ON), 0);
    fake_advance(1);
    status(0, 0, 6);
    scan();
    CHECK_EQ(count_cmd(CMD_LED_ON), 1);

    /* The same across the wrap of the 16-bit timer (annepro2.c led_power_timer is a uint16_t). */
    CASE("status: the settle time of a power command is measured across the 16-bit timer wrap");
    for (int k = 1; k <= 2; k++) {
        setup();
        fake_ms            = 65536u * k - 700;
        fake_last_input_ms = fake_ms; /* the clock was set, not advanced: the last key is now */
        lit();                 /* 200 ms later: the periodic check is not due for 800 ms yet */
        fake_advance(450);
        key(KC_AP_RGB_TOG, true, 0, 0); /* off */
        key(KC_AP_RGB_TOG, true, 0, 0); /* on, 50 ms before the wrap */
        fake_sd_tx_clear(&SD0);
        fake_advance(AP2_LED_STATUS_SETTLE_MS - 1);
        status(0, 0, 6);
        scan();
        CHECK_EQ(count_cmd(CMD_LED_ON), 0);
        fake_advance(1);
        status(0, 0, 6);
        scan();
        CHECK_EQ(count_cmd(CMD_LED_ON), 1);
    }

    CASE("status: no repair while the LEDs are off");
    setup();
    fake_rgb_enabled = false;
    status(0, 0, 6);
    scan();
    CHECK_EQ(SD0.tx_len, 0);

    CASE("status: at most 3 repairs in a row; a reply that says on starts counting again");
    setup();
    lit();
    for (int i = 0; i < 3; i++) {
        fake_advance(100);
        status(0, 0, 6);
        scan();
        CHECK_EQ(count_cmd(CMD_LED_ON), 1);
    }
    fake_advance(100);
    status(0, 0, 6);
    scan();
    CHECK_EQ(count_cmd(CMD_LED_ON), 0); /* a 4th is not tried */
    fake_advance(100);
    status(1, 0, 6); /* it reports on again */
    scan();
    fake_advance(100);
    status(0, 0, 6);
    scan();
    CHECK_EQ(count_cmd(CMD_LED_ON), 1); /* a later restart is repaired again */

    CASE("status: the same message twice (a resend) is acted on once");
    setup();
    lit();
    fake_advance(100);
    status(0, 0, 6);
    status_id--; /* the same id again */
    status(0, 0, 6);
    scan();
    CHECK_EQ(count_cmd(CMD_LED_SET_MANUAL), 1);

    /* annepro2.c:300: only CMD_LED_STATUS is taken as a status, whatever the size of the message. */
    CASE("status: a debug or unknown message from the LED MCU is ignored, also one as long as a status");
    setup();
    lit();
    const uint8_t dbg[] = {0x7a, 0x1d, 0x77, 9, 2, 0xaa, 0xbb};
    fake_sd_rx_push(&SD0, dbg, sizeof(dbg));
    scan();
    CHECK_EQ(SD0.tx_len, 0);
    const uint8_t other[] = {0x7a, 0x1d, 0x77, 10, 6, 3, 1, 0, 0, 2, 0}; /* a status payload, another command */
    fake_sd_rx_push(&SD0, other, sizeof(other));
    scan();
    CHECK_EQ(SD0.tx_len, 0);

    /* annepro2.c led_check_task(): the LED check goes on across the 16-bit wrap of timer_read(), so a LED MCU that
     * restarts late in a long session is still found (the LEDs would stay dark otherwise). */
    CASE("check: the LED check keeps running across the 16-bit timer wrap");
    setup();
    fake_ms            = 65536u * 2 - 500;
    fake_last_input_ms = fake_ms;
    lit();
    size_t manual = 0;
    for (int i = 0; i < 350; i++) {
        fake_advance(10);
        scan();
        manual += count_cmd(CMD_LED_SET_MANUAL);
    }
    CHECK(manual >= 3); /* 3.5 s of scans at AP2_LED_CHECK_MS = 1000 */

    /* annepro2.c forward_reactive_keypress(): a reactive profile of the LED MCU lights the key that was pressed; a
     * release is not a key press. Also for the keys of the keyboard itself (here a slot key). */
    CASE("status: a reactive profile gets one key-down for a slot key press and none for its release");
    setup();
    status(1, 1, 6);
    scan();
    fake_sd_tx_clear(&SD0);
    key(KC_AP2_BT1, true, 0, 1);
    CHECK_EQ(count_cmd(CMD_LED_KEY_DOWN), 1);
    fake_sd_tx_clear(&SD0);
    key(KC_AP2_BT1, false, 0, 1);
    CHECK_EQ(count_cmd(CMD_LED_KEY_DOWN), 0);

    /* The unpair key is forwarded to a reactive profile like every other key (process_record_kb()). */
    CASE("status: a reactive profile gets one key-down for the unpair key press and none for its release");
    setup();
    status(1, 1, 6);
    scan();
    fake_sd_tx_clear(&SD0);
    key(KC_AP2_BT_UNPAIR, true, 0, 5);
    CHECK_EQ(count_cmd(CMD_LED_KEY_DOWN), 1);
    fake_sd_tx_clear(&SD0);
    key(KC_AP2_BT_UNPAIR, false, 0, 5);
    CHECK_EQ(count_cmd(CMD_LED_KEY_DOWN), 0);

    /* annepro2.c led_sync(): manual control first, because the colours come from rgb_matrix, not from the profiles; the
     * LED MCU shows its own profile until the manual command arrives. */
    CASE("LED on: manual mode is sent before CMD_LED_ON");
    setup();
    fake_rgb_enabled = true;
    led_sync(true);
    CHECK(index_of(CMD_LED_SET_MANUAL) >= 0);
    CHECK(index_of(CMD_LED_SET_MANUAL) < index_of(CMD_LED_ON));

    /* annepro2.c led_check_task(): the check leaves AP2_LED_TX_RESERVE to the short commands, so it waits while the LED
     * UART has less than its message and the reserve free. The scan helpers empty SD0, so this sets the free space. */
    CASE("check: the LED check waits while the LED UART has less than its message and the reserve free");
    setup();
    fake_rgb_enabled = true;
    led_sync(true);
    fake_advance(AP2_LED_CHECK_MS);
    scan(); /* the first check sets the LED MCU up once more */
    fake_advance(AP2_LED_CHECK_MS);
    fake_sd_tx_clear(&SD0);
    fake_sd_set_free(&SD0, PROTO_TX_MESSAGE_SIZE(1) + AP2_LED_TX_RESERVE - 1);
    matrix_scan_kb();
    CHECK_EQ(count_cmd(CMD_LED_SET_MANUAL), 0);
    fake_sd_set_free(&SD0, PROTO_TX_MESSAGE_SIZE(1) + AP2_LED_TX_RESERVE);
    matrix_scan_kb();
    CHECK_EQ(count_cmd(CMD_LED_SET_MANUAL), 1);
    return TEST_RESULT();
}
