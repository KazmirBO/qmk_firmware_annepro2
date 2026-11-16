/* USB-1/2/5, build variant: AP2_IDLE_SCAN_MS 0 turns both waits off, the short one too, whatever AP2_AWAKE_SCAN_MS
 * says (annepro2.c, comment above AP2_IDLE_SCAN_MS). test_idle_both_off.c builds it with AP2_AWAKE_SCAN_MS 0 as well:
 * both options at 0 compile and never wait. */
#define AP2_IDLE_SCAN_MS 0
#include "all_sources.h"

static uint32_t idle_wait(void) {
    const uint32_t before = fake_calls.wait_ms_total;
    idle_task();
    return fake_calls.wait_ms_total - before;
}

int main(void) {
    static const usbstate_t states[] = {USB_ACTIVE, USB_SUSPENDED, USB_STOP};
    for (size_t s = 0; s < 3; s++) {
        for (int ble = 0; ble < 2; ble++) {
            for (int key = 0; key < 2; key++) {
                CASE("both waits off: never a wait");
                reset_firmware();
                USBD1.state       = states[s];
                USBD1.saved_state = USB_ACTIVE;
                if (ble) annepro2_ble_use_driver();
                fake_last_input_ms = key ? fake_ms - 100 : fake_ms - 60000;
                fake_rgb_enabled   = key;
                CHECK_EQ(idle_wait(), 0);
                fake_pin_c10 = true; /* the Bluetooth switch is off (USB): it does not select the driver */
                matrix_scan_kb();
                CHECK_EQ(fake_calls.wait_ms_total, 17u * (states[s] == USB_SUSPENDED && !ble));
            }
        }
    }
    return TEST_RESULT();
}
