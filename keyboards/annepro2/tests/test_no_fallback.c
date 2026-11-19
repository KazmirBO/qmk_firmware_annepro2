/* Build variant: AP2_BLE_CONNECT_TIMEOUT_MS 0 (readme.md: "AP2_BLE_CONNECT_TIMEOUT_MS (default 6000, 0 turns it off)").
 * A slot tap that moved the keystrokes from a USB host to Bluetooth then stays on Bluetooth, however long no host
 * connects. With the default 6000 the reports would go back to the USB host after 6 s (test_ble_tx.c,
 * test_fix_usbble.c). */
#undef AP2_BLE_CONNECT_TIMEOUT_MS
#define AP2_BLE_CONNECT_TIMEOUT_MS 0
/* annepro2_ble_task() still compares timer_elapsed() with the option, which is then always true; it is never reached
 * with connect_pending set, because annepro2_ble_connect() sets it only under #if AP2_BLE_CONNECT_TIMEOUT_MS > 0. */
#pragma GCC diagnostic ignored "-Wtype-limits"
#include "all_sources.h"

int main(void) {
    CASE("a slot tap from an active USB host stays on Bluetooth for 10 s of scans without a connected host");
    reset_firmware();
    fake_pin_c10      = true;
    ble_ready         = true;
    USBD1.state       = USB_ACTIVE;
    USBD1.saved_state = USB_ACTIVE;
    bt_switch_init(); /* the switch is in the USB position, settled: the scans leave the driver alone */
    fake_advance(AP2_BT_SWITCH_DEBOUNCE_MS);
    matrix_scan_kb();
    CHECK(annepro2_usb_host_in_use()); /* the case where the default would fall back */
    keyrecord_t r;
    memset(&r, 0, sizeof(r));
    r.event.pressed = true;
    r.event.key.col = 1;
    process_record_kb(KC_AP2_BT1, &r);
    r.event.pressed = false;
    process_record_kb(KC_AP2_BT1, &r);
    CHECK(annepro2_ble_is_active());
    for (int i = 0; i < 1000; i++) {
        fake_advance(10);
        fake_sd_tx_clear(&SD0);
        fake_sd_tx_clear(&SD1);
        matrix_scan_kb();
    }
    CHECK(annepro2_ble_is_active());
    CHECK(host_get_driver() != &chibios_driver);
    return TEST_RESULT();
}
