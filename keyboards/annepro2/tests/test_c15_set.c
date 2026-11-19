/* Build variant: the option set of the C15 (c15/config.h defines neither the battery ADC, the Bluetooth switch nor the
 * serial number; readme.md lists them as options of the C18). The shared sources must build and scan without them, and
 * the slot keys, which the C15 has, must still work. */
#undef AP2_HAS_BATTERY_ADC
#undef AP2_HAS_BT_SWITCH
#undef AP2_BT_SWITCH
#undef AP2_BLE_BATTERY
#undef AP2_BLE_SERIAL
#include "all_sources.h"

int main(void) {
    CASE("C15 option set: a slot tap connects and the scan runs without the C18 features");
    reset_firmware();
    ble_ready = true;
    keyrecord_t r;
    memset(&r, 0, sizeof(r));
    r.event.pressed = true;
    process_record_kb(KC_AP2_BT1, &r);
    r.event.pressed = false;
    process_record_kb(KC_AP2_BT1, &r);
    matrix_scan_kb();
    CHECK_EQ(SD1.tx_len, 11);
    CHECK_EQ(SD1.tx[8], 0x40);
    CHECK_EQ(SD1.tx[9], 0x04); /* connect */
    CHECK_EQ(SD1.tx[10], 0x00);
    return TEST_RESULT();
}
