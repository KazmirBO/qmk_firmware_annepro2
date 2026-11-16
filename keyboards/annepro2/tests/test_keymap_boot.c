/* CFG-1: QK_BOOT in keymaps/kazmir_bo/keymap.c acts only after FN2 + Esc has been held for BOOT_HOLD_MS (1000 ms); a
 * short press does nothing. The real keymap is compiled into this program (its matrix tables are not needed: the layout
 * macro is empty here) and driven through the real process_record_kb() and matrix_scan_user(). reset_keyboard() is the
 * fake of fake_hw.c: on the keyboard it runs bootloader_jump(). Documented in the comment above FN2 of keymap.c and in
 * readme.md. */
#define QMK_KEYBOARD_H "annepro2.h"
#define LAYOUT_60_ansi(...) {{0}}
#define get_highest_layer(state) (31 - __builtin_clz((state) | 1u))
#include "all_sources.h"
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

    return TEST_RESULT();
}
