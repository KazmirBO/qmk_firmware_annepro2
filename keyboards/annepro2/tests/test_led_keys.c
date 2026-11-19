/* MAIN-7: with rgb_matrix, KC_AP_LED_ON and KC_AP_LED_TOG decide by the state led_sync() last gave the LED MCU, not by
 * its status alone: with a sticky key set (the Caps Lock indicator of kazmir_bo and default_layer_indicators, the
 * battery indicator) the LED MCU keeps its matrix on after CMD_LED_OFF and reports matrix_enabled = 1 (Shine,
 * commands.c: matrixDisable() returns early while sticky keys exist, then sendStatus()). Without the fix KC_AP_LED_TOG
 * cannot switch the LEDs back on until the sticky key goes out, and KC_AP_LED_ON steps the profile instead. A status
 * that says off still counts: a restarted LED MCU that is not set up again is switched on by the key. */
#include "all_sources.h"
#include "proto_decode.h"

static void setup_lit(void) {
    reset_firmware();
    fake_rgb_enabled = true;
    led_sync(true); /* keyboard_post_init_kb(): LEDs on */
    ap2_led_status.matrix_enabled = 1;
    fake_sd_tx_clear(&SD0);
    memset(&fake_calls, 0, sizeof(fake_calls));
}

static void press(uint16_t kc) {
    keyrecord_t r;
    memset(&r, 0, sizeof(r));
    r.event.pressed = true;
    process_record_kb(kc, &r);
}

static size_t count_cmd(uint8_t cmd) {
    size_t n = decode_tx(&SD0, NULL), k = 0;
    for (size_t i = 0; i < n; i++) k += dec_msgs[i].cmd == cmd;
    return k;
}

/* The LEDs switched off by a key while a sticky key is lit: the LED MCU's reply to CMD_LED_OFF says matrix on. */
static void off_with_sticky(uint16_t off_key) {
    setup_lit();
    press(off_key);
    CHECK_EQ(fake_calls.rgb_disable, 1);
    CHECK(!led_mcu_on);
    ap2_led_status.matrix_enabled = 1; /* status after CMD_LED_OFF with a sticky key */
    fake_sd_tx_clear(&SD0);
    memset(&fake_calls, 0, sizeof(fake_calls));
}

int main(void) {
    CASE("TOG with a sticky key lit: the second press switches the LEDs on again");
    off_with_sticky(KC_AP_LED_TOG);
    press(KC_AP_LED_TOG);
    CHECK_EQ(fake_calls.rgb_enable, 1);
    CHECK_EQ(fake_calls.rgb_disable, 0);
    CHECK(fake_rgb_enabled);
    CHECK(led_mcu_on);
    CHECK_EQ(count_cmd(CMD_LED_ON), 1);

    CASE("ON after OFF with a sticky key lit: switches the LEDs on, no profile step");
    off_with_sticky(KC_AP_LED_OFF);
    press(KC_AP_LED_ON);
    CHECK_EQ(fake_calls.rgb_enable, 1);
    CHECK(led_mcu_on);
    CHECK_EQ(count_cmd(CMD_LED_NEXT_PROFILE), 0);
    CHECK_EQ(count_cmd(CMD_LED_ON), 1);

    /* Unchanged behaviour. */
    CASE("TOG with the LEDs lit switches them off; ON with the LEDs lit steps the profile");
    setup_lit();
    press(KC_AP_LED_TOG);
    CHECK_EQ(fake_calls.rgb_disable, 1);
    CHECK(!led_mcu_on);
    setup_lit();
    press(KC_AP_LED_ON);
    CHECK_EQ(count_cmd(CMD_LED_NEXT_PROFILE), 1);
    CHECK_EQ(fake_calls.rgb_enable, 0);

    CASE("TOG and ON with the LEDs dark past RGB_MATRIX_TIMEOUT (rgb_matrix still enabled) switch them on");
    for (int k = 0; k < 2; k++) {
        setup_lit();
        fake_last_input_ms = fake_ms - RGB_MATRIX_TIMEOUT - 1;
        led_sync(false); /* the scan: dark */
        CHECK(!led_mcu_on);
        ap2_led_status.matrix_enabled = 0; /* reply to CMD_LED_OFF, no sticky key */
        fake_last_input_ms = fake_ms;       /* the key press itself */
        memset(&fake_calls, 0, sizeof(fake_calls));
        press(k ? KC_AP_LED_ON : KC_AP_LED_TOG);
        CHECK_EQ(fake_calls.rgb_disable, 0);
        CHECK(fake_rgb_enabled);
        CHECK(led_mcu_on);
    }

    CASE("TOG and ON on a restarted LED MCU that reports its matrix off and is not set up again: switch it on");
    for (int k = 0; k < 2; k++) {
        setup_lit();
        led_repairs                   = AP2_LED_MAX_REPAIRS; /* its status is no longer acted on */
        ap2_led_status.matrix_enabled = 0;
        press(k ? KC_AP_LED_ON : KC_AP_LED_TOG);
        CHECK_EQ(fake_calls.rgb_disable, 0);
        CHECK(fake_rgb_enabled);
        CHECK(led_mcu_on);
        CHECK_EQ(count_cmd(CMD_LED_ON), 1);
        CHECK_EQ(count_cmd(CMD_LED_NEXT_PROFILE), 0);
    }

    CASE("TOG and ON while the USB host sleeps (RGB_MATRIX_SLEEP, LEDs dark) leave rgb_matrix enabled for the wakeup");
    for (int k = 0; k < 2; k++) {
        for (int sticky = 0; sticky < 2; sticky++) {
            setup_lit();
            fake_rgb_suspended = true;
            led_sync(false); /* the scan: dark */
            CHECK(!led_mcu_on);
            ap2_led_status.matrix_enabled = sticky; /* reply to CMD_LED_OFF */
            memset(&fake_calls, 0, sizeof(fake_calls));
            press(k ? KC_AP_LED_ON : KC_AP_LED_TOG);
            CHECK_EQ(fake_calls.rgb_disable, 0);
            CHECK(fake_rgb_enabled);
            CHECK(!led_mcu_on); /* still dark until the host wakes */
        }
    }
    return TEST_RESULT();
}
