/* The fixes of the findings LED-1 to LED-4 and LED-N1 (2026-11-16), driven through the real functions of annepro2.c,
 * ap2_led.c, rgb_driver.c and annepro2_battery.c; the LED UART is decoded with the real receiver of protocol.c.
 *
 *   LED-1   the intensity key sends the sticky state and every row again (the LED MCU dims what it receives)
 *   LED-2   the first led_check_task() after boot runs the setup once; the later checks send only 30 01
 *   LED-3/4 ap2_led_hold_rx(): after a blink, its cancel and switching the LEDs on, the rgb_matrix rows and the battery
 *           indicator wait (AP2_LED_BLINK_HOLD_MS, AP2_LED_POWER_HOLD_MS, ap2_led.h) and go out after the hold; the end
 *           time is 16 bits
 *   LED-N1  no status request (30 01) in the scan that switches the LEDs on
 * The hold ends (queued bytes / 11 + 1 + ms) after the command, as ap2_led.c computes it. */
#include "all_sources.h"
#include "proto_decode.h"

#define ROW_MSG PROTO_TX_MESSAGE_SIZE(1 + NUM_COLUMN * 4)

static void setup(void) {
    reset_firmware();
    fake_rgb_enabled = true;
    rgb_matrix_driver.init();
    rgb_matrix_driver.set_color_all(0, 0, 0);
    fake_sd_tx_clear(&SD0);
    refresh_timer = timer_read32();
}

static size_t count_cmd(uint8_t cmd) {
    size_t n = decode_tx(&SD0, NULL), k = 0;
    for (size_t i = 0; i < n; i++) k += dec_msgs[i].cmd == cmd;
    return k;
}

static void press(uint16_t kc) {
    keyrecord_t r;
    memset(&r, 0, sizeof(r));
    r.event.pressed = true;
    process_record_kb(kc, &r);
}

static void case_intensity(void) {
    CASE("LED-1: the intensity key: next intensity, mask cleared, sticky state sent again, every row sent again");
    setup();
    fake_rgb_enabled = true;
    press(KC_AP_LED_NEXT_INTENSITY);
    CHECK_EQ(fake_calls.led_update_kb, 1);
    size_t n = decode_tx(&SD0, NULL);
    CHECK(n >= 2);
    CHECK_EQ(dec_msgs[0].cmd, CMD_LED_NEXT_INTENSITY);
    CHECK_EQ(dec_msgs[1].cmd, CMD_LED_MASK_SET_MONO); /* leds_reset_foreground() */
    CHECK_EQ(dirty_rows, 0x1f);
    CHECK_EQ(sent_valid, 0);
    fake_sd_tx_clear(&SD0);
    fake_sd_set_free(&SD0, 4000);
    rgb_matrix_driver.flush();
    CHECK_EQ(count_cmd(CMD_LED_COLOR_SET_ROW), 5); /* even though no colour changed */

    CASE("LED-1: a held layer gets its colour back as before");
    setup();
    layer_state = 2;
    press(KC_AP_LED_NEXT_INTENSITY);
    CHECK_EQ(fake_calls.layer_state_set_kb, 1);
    CHECK_EQ(fake_calls.led_update_kb, 1);

    CASE("LED-1: the other LED keys do not resend the rows");
    setup();
    press(KC_AP_LED_PREV_PROFILE);
    CHECK_EQ(dirty_rows, 0);
    CHECK_EQ(fake_calls.led_update_kb, 0);
}

static void case_check(void) {
    CASE("LED-2: the first check after boot runs the setup once, the later ones send only 30 01");
    setup();
    led_sync(true); /* keyboard_post_init_kb() */
    fake_sd_tx_clear(&SD0);
    fake_sd_drain(&SD0, 1000);
    CHECK(led_mcu_on);
    CHECK(!led_setup_checked);
    fake_advance(AP2_LED_CHECK_MS);
    led_check_task();
    CHECK(led_setup_checked);
    CHECK_EQ(count_cmd(CMD_LED_SET_MANUAL), 1);
    CHECK_EQ(count_cmd(CMD_LED_ON), 1);
    CHECK_EQ(fake_calls.led_update_kb, 1);    /* sticky keys of the keymap */
    CHECK_EQ(fake_calls.layer_state_set_kb, 1); /* the layer colour */
    CHECK_EQ(dirty_rows, 0x1f);                 /* every row follows */
    for (int i = 0; i < 3; i++) {
        fake_sd_tx_clear(&SD0);
        fake_advance(AP2_LED_CHECK_MS - 1);
        led_check_task();
        CHECK_EQ(SD0.tx_len, 0);
        fake_advance(1);
        led_check_task();
        CHECK_EQ(SD0.tx_len, PROTO_TX_MESSAGE_SIZE(1));
        CHECK_EQ(count_cmd(CMD_LED_SET_MANUAL), 1);
        CHECK_EQ(fake_calls.led_update_kb, 1); /* not again */
        CHECK_EQ(fake_calls.layer_state_set_kb, 1);
    }

    CASE("LED-2: with the LEDs off there is no check");
    setup();
    led_mcu_on = false;
    fake_advance(10 * AP2_LED_CHECK_MS);
    led_check_task();
    CHECK_EQ(SD0.tx_len, 0);
    CHECK(!led_setup_checked);
}

static void case_scan_n1(void) {
    CASE("LED-N1: the scan that switches the LEDs on sends no status request besides the one of the switch");
    setup();
    fake_pin_c10     = true; /* the Bluetooth switch is off */
    fake_rgb_enabled = false;
    led_sync(false);
    fake_sd_tx_clear(&SD0);
    led_check_timer  = (uint16_t)(fake_ms - 5 * AP2_LED_CHECK_MS); /* a check is due */
    fake_rgb_enabled = true;                                       /* a key turned the effect on */
    matrix_scan_kb();
    CHECK(led_mcu_on);
    CHECK_EQ(count_cmd(CMD_LED_SET_MANUAL), 1);
    CHECK_EQ(count_cmd(CMD_LED_ON), 1);
    CHECK(!led_setup_checked); /* the first check is still ahead */
    /* and the check follows a full interval later */
    fake_sd_tx_clear(&SD0);
    fake_advance(AP2_LED_CHECK_MS - 1);
    fake_sd_drain(&SD0, 1000);
    matrix_scan_kb();
    fake_advance(1);
    matrix_scan_kb();
    CHECK_EQ(count_cmd(CMD_LED_ON), 1); /* the setup of the first check */
    CHECK(led_setup_checked);
}

static void case_hold(void) {
    CASE("LED-3: a blink holds the rows back for AP2_LED_BLINK_HOLD_MS after it has left the queue");
    setup();
    const ap2_led_t red = {.p.red = 0xff, .p.alpha = 0xff};
    ap2_led_blink(1, 2, red, 8, 50);
    /* 13 bytes queued: 13 / 11 + 1 + 11 = 13 ms */
    CHECK(ap2_led_rx_held());
    fake_sd_tx_clear(&SD0);
    fake_sd_drain(&SD0, 1000);
    rgb_matrix_driver.set_color(g_led_config.matrix_co[0][1], 1, 2, 3);
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, 0);
    fake_advance(12);
    ap2_rgb_flush_pending();
    CHECK_EQ(SD0.tx_len, 0);
    CHECK(ap2_led_rx_held());
    fake_advance(1);
    CHECK(!ap2_led_rx_held());
    ap2_rgb_flush_pending();
    CHECK_EQ(SD0.tx_len, ROW_MSG); /* the row waited and goes out now */
    CHECK_EQ(pending_rows, 0);

    CASE("LED-3: the hold also stops the refresh");
    setup();
    ap2_led_blink(1, 2, red, 8, 50);
    fake_sd_tx_clear(&SD0);
    fake_sd_drain(&SD0, 1000);
    refresh_timer = timer_read32() - AP2_LED_REFRESH_MS;
    ap2_rgb_flush_pending();
    CHECK_EQ(SD0.tx_len, 0);
    fake_advance(13);
    ap2_rgb_flush_pending();
    CHECK_EQ(SD0.tx_len, ROW_MSG);

    CASE("LED-3: cancelling a running blink holds as well");
    setup();
    ap2_led_blink(1, 2, red, 8, 50);
    fake_advance(100);
    fake_sd_tx_clear(&SD0);
    fake_sd_drain(&SD0, 1000);
    ap2_led_cancel_blink();
    CHECK_EQ(count_cmd(CMD_LED_KEY_BLINK), 1);
    CHECK(ap2_led_rx_held());
    fake_advance(AP2_LED_BLINK_HOLD_MS + 2);
    CHECK(!ap2_led_rx_held());

    CASE("LED-3: cancelling a blink that has ended sends nothing and holds nothing");
    setup();
    ap2_led_blink(1, 2, red, 1, 1);
    fake_advance(1000);
    CHECK(!ap2_led_rx_held());
    ap2_led_cancel_blink();
    CHECK(!ap2_led_rx_held());

    CASE("LED-4: switching the LEDs on holds for AP2_LED_POWER_HOLD_MS (two status replies in a row)");
    setup();
    led_mcu_on       = false;
    fake_rgb_enabled = true;
    led_sync(false);
    /* 30 01 (6 bytes) and three copies of LED on (15): 21 / 11 + 1 + 8 = 10 ms */
    CHECK(ap2_led_rx_held());
    fake_advance(9);
    CHECK(ap2_led_rx_held());
    fake_advance(1);
    CHECK(!ap2_led_rx_held());
    CHECK_EQ(dirty_rows, 0x1f); /* all rows follow after the hold */

    CASE("LED-4: the rows wait for the end of the power hold and then all go out");
    setup();
    led_mcu_on = false;
    led_sync(false);
    fake_sd_tx_clear(&SD0);
    fake_sd_drain(&SD0, 1000);
    fake_sd_set_free(&SD0, 4000);
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, 0);
    fake_advance(10);
    ap2_rgb_flush_pending();
    CHECK_EQ(count_cmd(CMD_LED_COLOR_SET_ROW), 5);

    CASE("LED-4: the battery indicator waits during the hold and is written after it");
    setup();
    FAKE_ADC_REG(0x088) = 1;
    FAKE_ADC_REG(0x030) = 2600;
    ap2_led_blink(0, 1, red, 8, 50);
    fake_sd_tx_clear(&SD0);
    fake_sd_drain(&SD0, 1000);
    annepro2_battery_toggle_display();
    CHECK(shown);
    CHECK_EQ(SD0.tx_len, 0);
    annepro2_battery_task();
    CHECK_EQ(SD0.tx_len, 0);
    fake_advance(13);
    for (int i = 0; i < 20; i++) {
        fake_sd_drain(&SD0, 1000);
        annepro2_battery_task();
    }
    CHECK_EQ(count_cmd(CMD_LED_STICKY_SET_KEY), 10);

    CASE("LED-4: a later hold does not shorten an earlier one, a longer one extends it");
    setup();
    ap2_led_hold_rx(50);
    fake_advance(10);
    ap2_led_hold_rx(1);
    fake_advance(30);
    CHECK(ap2_led_rx_held()); /* 40 of about 51 ms */
    fake_advance(15);
    CHECK(!ap2_led_rx_held());
    ap2_led_hold_rx(10);
    ap2_led_hold_rx(40);
    fake_advance(30);
    CHECK(ap2_led_rx_held());

    CASE("LED-3/4: queued bytes lengthen the hold (the command leaves after them, 11 bytes per ms)");
    setup();
    fake_sd_tx_clear(&SD0);
    fake_sd_set_capacity(&SD0, 128);
    SD0.oqueue.used = 110; /* 110 bytes in front: 10 ms */
    ap2_led_hold_rx(8);
    fake_advance(18);
    CHECK(ap2_led_rx_held());
    fake_advance(1);
    CHECK(!ap2_led_rx_held());

    CASE("LED-3/4: the end time is 16 bits: a hold across the wrap");
    for (int back = 0; back <= 14; back++) {
        setup();
        fake_ms = 65536u * 5 - (uint32_t)back;
        ap2_led_blink(1, 2, red, 8, 50);
        fake_sd_drain(&SD0, 1000);
        fake_advance(12);
        CHECK(ap2_led_rx_held());
        fake_advance(1);
        CHECK(!ap2_led_rx_held());
    }
}

int main(void) {
    case_intensity();
    case_check();
    case_scan_n1();
    case_hold();
    return TEST_RESULT();
}
