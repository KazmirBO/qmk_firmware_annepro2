#include QMK_KEYBOARD_H

enum anne_pro_layers {
    BASE,
    FN1,
    FN2,
};

const uint16_t keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [BASE] = LAYOUT_60_ansi(
        KC_ESC,           KC_1,    KC_2,    KC_3, KC_4, KC_5, KC_6,   KC_7, KC_8, KC_9,    KC_0,             KC_MINS,          KC_EQL,        KC_BSPC,
        KC_TAB,           KC_Q,    KC_W,    KC_E, KC_R, KC_T, KC_Y,   KC_U, KC_I, KC_O,    KC_P,             KC_LBRC,          KC_RBRC,       KC_BSLS,
        LT(FN1, KC_CAPS), KC_A,    KC_S,    KC_D, KC_F, KC_G, KC_H,   KC_J, KC_K, KC_L,    KC_SCLN,          KC_QUOT,          KC_ENT,
        KC_LSFT,                   KC_Z,    KC_X, KC_C, KC_V, KC_B,   KC_N, KC_M, KC_COMM, KC_DOT,           KC_SLSH,          KC_RSFT,
        KC_LCTL,          KC_LGUI, KC_LALT,                   KC_SPC,             KC_RALT, MO(FN1), LT(FN2, KC_APP), KC_RCTL
    ),

    [FN1] = LAYOUT_60_ansi(
        KC_GRV,  KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  _______,
        _______, _______, KC_UP,   _______, _______, _______, _______, _______, _______, _______, KC_PSCR, KC_HOME, KC_END,  _______,
        _______, KC_LEFT, KC_DOWN, KC_RGHT, _______, _______, _______, _______, _______, _______, KC_PGUP, KC_PGDN, _______,
        _______, KC_MUTE, KC_VOLD, KC_VOLU, KC_MPRV, KC_MPLY, KC_MNXT, KC_BRID, KC_BRIU, KC_INS,  KC_DEL,  _______,
        _______, _______, _______,                            KC_ENT,                    _______, _______, MO(FN2), _______
    ),

    /* KC_AP2_BT_UNPAIR (40 05) deletes the bonds of all four slots, so it is not bound next to the slot keys.
     * Pairing a slot again replaces only the bond of that slot. */
    [FN2] = LAYOUT_60_ansi(
        QK_BOOT, KC_AP2_BT1, KC_AP2_BT2, KC_AP2_BT3, KC_AP2_BT4, XXXXXXX,          KC_AP2_USB, KC_AP_LED_NEXT_INTENSITY, KC_AP2_BATT, KC_AP_RGB_MOD, KC_AP_RGB_TOG, KC_AP_RGB_VAD, KC_AP_RGB_VAI, KC_AP_LED_SPEED,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______,    _______,    _______,    _______,    _______,          _______,    _______, _______, _______,       _______,       _______,       _______,
        _______,             _______,    _______,    _______,    _______,          _______,    _______, _______, _______,       _______,       _______,       _______,
        _______, _______,    _______,                                              _______,                      _______,       MO(FN1),       MO(FN2),       _______
    ),
};

uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    if (keycode == LT(FN1, KC_CAPS)) {
        return CAPS_FN1_TAPPING_TERM;
    }
    return TAPPING_TERM;
}

void keyboard_post_init_user(void) {
    /* NKRO_ENABLE builds NKRO in, but this keymap has no key that turns it on (no magic or command keys) and EEPROM
     * keeps the old value, so turn it on for USB here. The Bluetooth driver turns it off and back on when it switches. */
    keymap_config.nkro = true;
}

layer_state_t layer_state_set_user(layer_state_t state) {
    switch (get_highest_layer(state)) {
        case FN1: ap2_led_set_foreground_color(0x00, 0xff, 0x00); break;
        case FN2: ap2_led_set_foreground_color(0x00, 0xff, 0xdd); break;
        default: ap2_led_reset_foreground_color(); break;
    }
    return state;
}

bool led_update_user(led_t leds) {
    static const ap2_led_t caps_on = {.p.red = 0x00, .p.green = 0x00, .p.blue = 0xff, .p.alpha = 0xff};

    /* leds comes from the active host driver, so in Bluetooth mode it already holds the Caps Lock state of the
     * Bluetooth host, and in USB mode it is not mixed with the state of a Bluetooth host left connected. */
    if (leds.caps_lock) {
        ap2_led_sticky_set_key(2, 0, caps_on);
    } else {
        ap2_led_unset_sticky_key(2, 0);
    }
    return true;
}
