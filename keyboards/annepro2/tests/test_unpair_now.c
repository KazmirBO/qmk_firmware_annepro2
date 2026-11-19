/* Build variant: AP2_BLE_UNPAIR_HOLD_MS 0 (readme.md: "KC_AP2_BT_UNPAIR ... after being held for
 * AP2_BLE_UNPAIR_HOLD_MS", annepro2.c: "0 acts on the press"). The unpair key then acts on the press, with no timer,
 * and its release does nothing. */
#undef AP2_BLE_UNPAIR_HOLD_MS
#define AP2_BLE_UNPAIR_HOLD_MS 0
#include "all_sources.h"

int main(void) {
    CASE("unpair on the press: 40 05 then 40 02 at once, nothing on the release");
    reset_firmware();
    keyrecord_t r;
    memset(&r, 0, sizeof(r));
    r.event.pressed = true;
    r.event.key.row = 4;
    r.event.key.col = 5;
    CHECK(!process_record_kb(KC_AP2_BT_UNPAIR, &r));
    CHECK_EQ(SD1.tx_len, 20);
    CHECK_EQ(SD1.tx[8], 0x40);
    CHECK_EQ(SD1.tx[9], 0x05);
    CHECK_EQ(SD1.tx[18], 0x40);
    CHECK_EQ(SD1.tx[19], 0x02);
    fake_sd_tx_clear(&SD1);
    r.event.pressed = false;
    process_record_kb(KC_AP2_BT_UNPAIR, &r);
    CHECK_EQ(SD1.tx_len, 0);
    return TEST_RESULT();
}
