/* The LED MCU power: when it is switched off and on (annepro2.c led_sync(), the comment above led_mcu_on; readme.md
 * "The LED MCU is switched off whenever rgb_matrix shows nothing: disabled, dark while the USB host sleeps or past
 * RGB_MATRIX_TIMEOUT"), the LED keys KC_AP_LED_OFF and KC_AP_RGB_TOG, and the RGB adjustment keys of the owner's FN2
 * layer with Shift and Ctrl (annepro2.c process_record_kb(), readme.md). KC_AP_LED_ON and KC_AP_LED_TOG, which decide
 * by the state the keyboard gave the LED MCU, are in test_led_keys.c. The expectations are the decoded commands on SD0
 * and the rgb_matrix functions the fakes record. */
#include "all_sources.h"
#include "proto_decode.h"

static size_t count_cmd(uint8_t cmd) {
    const size_t n = decode_tx(&SD0, NULL);
    size_t       k = 0;
    for (size_t i = 0; i < n; i++) k += dec_msgs[i].cmd == cmd;
    return k;
}
static void setup(void) {
    reset_firmware();
    fake_pin_c10 = true;
    ble_ready    = true;
}

int main(void) {
    CASE("LEDs: past RGB_MATRIX_TIMEOUT without a key the LED MCU goes off (CMD_LED_OFF), and on again by a key");
    setup();
    fake_rgb_enabled = true;
    led_sync(true);
    fake_sd_tx_clear(&SD0);
    fake_advance(RGB_MATRIX_TIMEOUT + 10);
    fake_last_input_ms = fake_ms - RGB_MATRIX_TIMEOUT; /* exactly the timeout: still lit */
    matrix_scan_kb();
    CHECK_EQ(count_cmd(CMD_LED_OFF), 0);
    CHECK(led_mcu_on);
    fake_last_input_ms = fake_ms - RGB_MATRIX_TIMEOUT - 1;
    matrix_scan_kb();
    CHECK_EQ(count_cmd(CMD_LED_OFF), 1);
    CHECK(!led_mcu_on);
    fake_sd_tx_clear(&SD0);
    fake_last_input_ms = fake_ms; /* a key */
    matrix_scan_kb();
    CHECK_EQ(count_cmd(CMD_LED_ON), 1);
    CHECK_EQ(count_cmd(CMD_LED_SET_MANUAL), 1);

    CASE("LEDs: with the USB host asleep rgb_matrix is suspended and the LED MCU goes off");
    setup();
    fake_rgb_enabled = true;
    led_sync(true);
    fake_sd_tx_clear(&SD0);
    fake_rgb_suspended = true;
    matrix_scan_kb();
    CHECK_EQ(count_cmd(CMD_LED_OFF), 1);

    CASE("KC_AP_LED_OFF: rgb_matrix off and CMD_LED_OFF, also when the state already says off (a key press resends)");
    setup();
    keyrecord_t r = {0};
    r.event.pressed = true;
    fake_rgb_enabled = false;
    led_mcu_on       = false;
    CHECK(!process_record_kb(KC_AP_LED_OFF, &r));
    CHECK_EQ(fake_calls.rgb_disable, 1);
    CHECK_EQ(fake_calls.rgb_enable, 0);
    CHECK_EQ(count_cmd(CMD_LED_OFF), 1);

    CASE("KC_AP_RGB_TOG: off when on; on when off, with manual mode, LED on and the mask reset");
    setup();
    fake_rgb_enabled = true;
    led_sync(true);
    fake_sd_tx_clear(&SD0);
    CHECK(!process_record_kb(KC_AP_RGB_TOG, &r));
    CHECK(!fake_rgb_enabled);
    CHECK_EQ(count_cmd(CMD_LED_OFF), 1);
    fake_sd_tx_clear(&SD0);
    CHECK(!process_record_kb(KC_AP_RGB_TOG, &r));
    CHECK(fake_rgb_enabled);
    CHECK_EQ(count_cmd(CMD_LED_ON), 1);
    CHECK_EQ(count_cmd(CMD_LED_SET_MANUAL), 1);
    CHECK_EQ(count_cmd(CMD_LED_MASK_SET_MONO), 1);

    /* readme.md: "The LED keys (KC_AP_LED_*, KC_AP_RGB_TOG) clear the mask so that their effect shows, then call
     * layer_state_set_kb(), so a layer colour set in layer_state_set_user() stays while its layer is held." The keys
     * also send the command of the LED MCU that their name says (profile forward and back, animation speed). */
    CASE("the LED profile and speed keys send their command and give a held layer its colour back");
    {
        static const struct {
            uint16_t kc;
            uint8_t  cmd;
        } t[] = {
            {KC_AP_LED_NEXT_PROFILE, CMD_LED_NEXT_PROFILE},
            {KC_AP_LED_PREV_PROFILE, CMD_LED_PREV_PROFILE},
            {KC_AP_LED_SPEED, CMD_LED_NEXT_ANIMATION_SPEED},
        };
        for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
            setup();
            layer_state = 1u << 1; /* a layer is held */
            CHECK(!process_record_kb(t[i].kc, &r));
            CHECK_EQ(count_cmd(t[i].cmd), 1);
            CHECK_EQ(fake_calls.layer_state_set_kb, 1);
            layer_state = 0;
        }
    }

    /* The layer hook is called only while a layer is held; with none held it would send a second mask command. */
    CASE("an LED key with no layer held does not call the layer hook");
    setup();
    layer_state = 0;
    CHECK(!process_record_kb(KC_AP_LED_NEXT_PROFILE, &r));
    CHECK_EQ(fake_calls.layer_state_set_kb, 0);
    CHECK_EQ(count_cmd(CMD_LED_MASK_SET_MONO), 1);

    /* Keys of the owner's FN2 layer: brightness (VAI, VAD), effect (MOD) and the LED switch (TOG); with Shift the
     * hue, saturation and speed, with Ctrl the opposite direction. The fakes record the name of the last rgb_matrix
     * function; the key must not be passed on to QMK. */
    CASE("the RGB keys call the rgb_matrix function their modifiers select");
    {
        static const struct {
            uint16_t    kc;
            uint8_t     mods;
            const char *fn;
        } t[] = {
            {KC_AP_RGB_VAI, 0, "rgb_matrix_increase_val"},
            {KC_AP_RGB_VAI, MOD_BIT(KC_LSFT), "rgb_matrix_increase_hue"},
            {KC_AP_RGB_VAI, MOD_BIT(KC_LCTL), "rgb_matrix_decrease_hue"},
            {KC_AP_RGB_VAD, 0, "rgb_matrix_decrease_val"},
            {KC_AP_RGB_VAD, MOD_BIT(KC_LSFT), "rgb_matrix_increase_sat"},
            {KC_AP_RGB_VAD, MOD_BIT(KC_LCTL), "rgb_matrix_decrease_sat"},
            {KC_AP_RGB_TOG, MOD_BIT(KC_LSFT), "rgb_matrix_increase_speed"},
            {KC_AP_RGB_TOG, MOD_BIT(KC_LCTL), "rgb_matrix_decrease_speed"},
            {KC_AP_RGB_MOD, 0, "rgb_matrix_step"},
            {KC_AP_RGB_MOD, MOD_BIT(KC_LCTL), "rgb_matrix_step_reverse"},
        };
        for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
            setup();
            set_mods(t[i].mods);
            CHECK(!process_record_kb(t[i].kc, &r));
            CHECK_EQ(fake_calls.rgb_other, 1);
            CHECK(fake_calls.rgb_last && strcmp(fake_calls.rgb_last, t[i].fn) == 0);
        }
    }
    return TEST_RESULT();
}
