/* CFG-1: QK_BOOT in keymaps/kazmir_bo/keymap.c acts only after FN2 + Esc has been held for BOOT_HOLD_MS (1000 ms); a
 * short press does nothing. The real keymap is compiled into this program (its matrix tables are not needed: the layout
 * macro is empty here) and driven through the real process_record_kb() and matrix_scan_user(). reset_keyboard() is the
 * fake of fake_hw.c: on the keyboard it runs bootloader_jump(). Documented in the comment above FN2 of keymap.c and in
 * readme.md. The last cases are the hooks QMK calls in the keymap (layer colours, the Caps Lock indicator, NKRO, the
 * tapping term; the comments in keymap.c, readme.md "layer_state_set_user()"). */
#define QMK_KEYBOARD_H "annepro2.h"
#define LAYOUT_60_ansi(...) {{0}}
#define get_highest_layer(state) (31 - __builtin_clz((state) | 1u))
#include "all_sources.h"
#include "proto_decode.h"
/* the layer macros of quantum_keycodes.h, which needs more of QMK than is faked here */
#define MO(layer) (QK_MOMENTARY | ((layer) & 0x1F))
#define LT(layer, kc) (QK_LAYER_TAP | (((layer) & 0xF) << 8) | ((kc) & 0xFF))
#define _______ KC_TRNS
/* the config.h of the keymap, as the build system includes it (TAPPING_TERM and the like) */
#undef RGB_MATRIX_DEFAULT_MODE
#include "../keymaps/kazmir_bo/config.h"
#include "../keymaps/kazmir_bo/keymap.c"

static void press(uint16_t kc, bool pressed) {
    keyrecord_t r;
    memset(&r, 0, sizeof(r));
    r.event.pressed = pressed;
    r.event.key.row = 0;
    r.event.key.col = 0;
    /* process_record_kb() falls through to process_record_user(), which swallows QK_BOOT */
    CHECK(!process_record_kb(kc, &r) || kc != QK_BOOT);
}

static void scan_for(uint32_t ms) { /* scans every 1 ms */
    for (uint32_t i = 0; i < ms; i++) {
        fake_advance(1);
        matrix_scan_user();
    }
}

static void expect_one(uint8_t cmd, const uint8_t *payload, uint8_t n) {
    CHECK_EQ(decode_tx(&SD0, NULL), 1);
    CHECK_EQ(dec_msgs[0].cmd, cmd);
    CHECK_EQ(dec_msgs[0].size, n);
    CHECK_BYTES(dec_msgs[0].payload, payload, n);
    fake_sd_tx_clear(&SD0);
}

static void setup(void) {
    reset_firmware();
    boot_held = false;
}

int main(void) {
    CASE("held for BOOT_HOLD_MS: the bootloader is entered, once");
    setup();
    press(QK_BOOT, true);
    scan_for(BOOT_HOLD_MS - 1);
    CHECK_EQ(fake_calls.reset_keyboard, 0);
    scan_for(1);
    CHECK_EQ(fake_calls.reset_keyboard, 1);
    scan_for(3000);
    CHECK_EQ(fake_calls.reset_keyboard, 1); /* the key is still down, but it acts once */

    CASE("a short press does nothing, also when scanned long after");
    setup();
    press(QK_BOOT, true);
    scan_for(500);
    press(QK_BOOT, false);
    scan_for(5000);
    CHECK_EQ(fake_calls.reset_keyboard, 0);

    CASE("a roll from FN2 onto Esc that ends within the time does nothing");
    setup();
    press(QK_BOOT, true);
    scan_for(BOOT_HOLD_MS - 1);
    press(QK_BOOT, false);
    scan_for(10);
    CHECK_EQ(fake_calls.reset_keyboard, 0);

    CASE("released and pressed again: the time starts again");
    setup();
    press(QK_BOOT, true);
    scan_for(900);
    press(QK_BOOT, false);
    scan_for(100);
    press(QK_BOOT, true);
    scan_for(BOOT_HOLD_MS - 1);
    CHECK_EQ(fake_calls.reset_keyboard, 0);
    scan_for(1);
    CHECK_EQ(fake_calls.reset_keyboard, 1);

    CASE("other keys are passed on");
    setup();
    keyrecord_t r;
    memset(&r, 0, sizeof(r));
    r.event.pressed = true;
    CHECK(process_record_user(KC_A, &r));
    scan_for(BOOT_HOLD_MS + 10);
    CHECK_EQ(fake_calls.reset_keyboard, 0);

    CASE("the 16-bit timer wraps");
    setup();
    fake_ms = 65536u * 3 - 400;
    press(QK_BOOT, true);
    scan_for(BOOT_HOLD_MS - 1);
    CHECK_EQ(fake_calls.reset_keyboard, 0);
    scan_for(1);
    CHECK_EQ(fake_calls.reset_keyboard, 1);

    /* The hooks of the keymap that QMK calls (the keymap's own colours: keymap.c, readme.md "layer colours"): the
     * fakes of fake_hw.c call layer_state_set_user() and led_update_user() from the _kb hooks, as QMK does. */
    CASE("layer_state_set_user: FN1 green, FN2 cyan, the base layer takes the colour off (payload b g r alpha)");
    reset_firmware();
    CHECK_EQ(layer_state_set_user(1u << FN1), 1u << FN1);
    expect_one(CMD_LED_MASK_SET_MONO, (const uint8_t[]){0x00, 0xff, 0x00, 0xff}, 4);
    layer_state_set_user(1u << FN2);
    expect_one(CMD_LED_MASK_SET_MONO, (const uint8_t[]){0xdd, 0xff, 0x00, 0xff}, 4);
    layer_state_set_user((1u << FN2) | (1u << FN1)); /* the highest layer wins */
    expect_one(CMD_LED_MASK_SET_MONO, (const uint8_t[]){0xdd, 0xff, 0x00, 0xff}, 4);
    layer_state_set_user(1u << BASE);
    expect_one(CMD_LED_MASK_SET_MONO, (const uint8_t[]){0, 0, 0, 0}, 4);

    CASE("led_update_user: Caps Lock lights the Caps key blue (row 2, column 0); off puts it out");
    reset_firmware();
    led_t on = {.caps_lock = true}, off = {0};
    CHECK(led_update_user(on));
    expect_one(CMD_LED_STICKY_SET_KEY, (const uint8_t[]){2, 0, 0xff, 0x00, 0x00, 0xff}, 6);
    CHECK(led_update_user(off));
    expect_one(CMD_LED_STICKY_UNSET_KEY, (const uint8_t[]){2, 0}, 2);
    led_t num = {.num_lock = true}; /* another host LED does not light it */
    led_update_user(num);
    expect_one(CMD_LED_STICKY_UNSET_KEY, (const uint8_t[]){2, 0}, 2);

    CASE("a LED MCU that restarted is set up again with the Caps Lock indicator and the layer colour of the keymap");
    reset_firmware();
    proto_init(&proto, led_receive);
    fake_pin_c10         = true;
    fake_rgb_enabled     = true;
    fake_host_led_state.caps_lock = true;
    layer_state          = 1u << FN1;
    led_sync(true);
    fake_advance(200);
    fake_sd_tx_clear(&SD0);
    const uint8_t st[] = {0x7a, 0x1d, CMD_LED_STATUS, 5, 6, 3, 1, 0, 0, 2, 0}; /* matrix off: it restarted */
    fake_sd_rx_push(&SD0, st, sizeof(st));
    matrix_scan_kb();
    {
        const size_t n = decode_tx(&SD0, NULL);
        bool         caps = false, green = false;
        for (size_t i = 0; i < n; i++) {
            const dec_msg_t *m = &dec_msgs[i];
            if (m->cmd == CMD_LED_STICKY_SET_KEY && m->payload[0] == 2 && m->payload[1] == 0 && m->payload[2] == 0xff) {
                caps = true; /* blue Caps key at row 2, column 0 */
            }
            if (m->cmd == CMD_LED_MASK_SET_MONO && m->payload[0] == 0 && m->payload[1] == 0xff) green = true;
        }
        CHECK(caps);
        CHECK(green);
    }

    CASE("keyboard_post_init_user: NKRO is on for USB");
    reset_firmware();
    keymap_config.nkro = false;
    keyboard_post_init_user();
    CHECK(keymap_config.nkro);

    CASE("get_tapping_term: the Caps key (LT FN1) is held 250 ms, every other key 100 ms");
    CHECK_EQ(get_tapping_term(LT(FN1, KC_CAPS), &r), CAPS_FN1_TAPPING_TERM);
    CHECK_EQ(get_tapping_term(KC_A, &r), TAPPING_TERM);
    CHECK_EQ(get_tapping_term(LT(FN2, KC_APP), &r), TAPPING_TERM);
    return TEST_RESULT();
}
