/* Sweep: usb_suspend_task() and idle_task() never both wait in one scan, and one of them waits whenever the USB host is
 * not active (with AP2_AWAKE_SCAN_MS > 0); both decide with usb_sleep_pace() (annepro2.c). */
#include "all_sources.h"

static uint32_t waited(void) { return fake_calls.wait_ms_total; }

int main(void) {
    const usbstate_t states[] = {USB_UNINIT, USB_STOP, USB_READY, USB_SELECTED, USB_ACTIVE, USB_SUSPENDED};
    int n = 0;
    for (unsigned s = 0; s < sizeof(states) / sizeof(states[0]); s++)
        for (unsigned sv = 0; sv < sizeof(states) / sizeof(states[0]); sv++)
            for (int gone = 0; gone < 2; gone++)
                for (int ble = 0; ble < 2; ble++)
                    for (int recent = 0; recent < 2; recent++) {
                        reset_firmware();
                        USBD1.state       = states[s];
                        USBD1.saved_state = states[sv];
                        usb_gone          = gone;
                        if (ble) annepro2_ble_use_driver();
                        fake_last_input_ms = recent ? fake_ms : fake_ms - 60000;
                        const uint32_t w0 = waited();
                        usb_suspend_task();
                        const uint32_t w1 = waited() - w0;
                        idle_task();
                        const uint32_t w2 = waited() - w0 - w1;
                        n++;
                        CHECK(!(w1 && w2));
                        if (states[s] != USB_ACTIVE) CHECK(w1 || w2);
                        if (states[s] == USB_ACTIVE) CHECK(!w2);
                    }
    CHECK(n == 6 * 6 * 2 * 2 * 2);
    return TEST_RESULT();
}
