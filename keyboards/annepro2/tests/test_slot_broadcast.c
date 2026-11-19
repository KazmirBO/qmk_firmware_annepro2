/* Build variant: without AP2_BLE_SLOT_TAP_HOLD (readme.md: "Without the option the first press starts pairing and a
 * second press on the same slot connects"). The slot keys then call annepro2_ble_broadcast(): 40 01 <slot> for the
 * first press, and 40 01 <slot> followed by 40 04 <slot> for a second press on the same slot, as the stock firmware
 * does. */
#undef AP2_BLE_SLOT_TAP_HOLD
#include "all_sources.h"

static void press(uint16_t kc) {
    keyrecord_t r;
    memset(&r, 0, sizeof(r));
    r.event.pressed = true;
    r.event.key.col = 1;
    CHECK(!process_record_kb(kc, &r));
}

int main(void) {
    CASE("broadcast: first press 40 01 <slot>, second press on the same slot 40 01 <slot> and 40 04 <slot>");
    reset_firmware();
    press(KC_AP2_BT2);
    CHECK_EQ(SD1.tx_len, 11);
    CHECK_EQ(SD1.tx[8], 0x40);
    CHECK_EQ(SD1.tx[9], 0x01);
    CHECK_EQ(SD1.tx[10], 0x01);
    fake_sd_tx_clear(&SD1);
    press(KC_AP2_BT2);
    CHECK_EQ(SD1.tx_len, 22); /* the stock firmware's pair-then-connect pair of frames */
    CHECK_EQ(SD1.tx[9], 0x01);
    CHECK_EQ(SD1.tx[10], 0x01);
    CHECK_EQ(SD1.tx[20], 0x04);
    CHECK_EQ(SD1.tx[21], 0x01);

    CASE("broadcast: a press on another slot starts pairing there");
    fake_sd_tx_clear(&SD1);
    press(KC_AP2_BT3);
    CHECK_EQ(SD1.tx_len, 11);
    CHECK_EQ(SD1.tx[9], 0x01);
    CHECK_EQ(SD1.tx[10], 0x02);
    return TEST_RESULT();
}
