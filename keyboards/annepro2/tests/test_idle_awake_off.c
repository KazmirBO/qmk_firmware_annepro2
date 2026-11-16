/* USB-5, build variant: AP2_AWAKE_SCAN_MS 0 turns off only the short wait; the idle wait stays. */
#define AP2_AWAKE_SCAN_MS 0
#include "all_sources.h"

static uint32_t idle_wait(void) {
    const uint32_t before = fake_calls.wait_ms_total;
    idle_task();
    return fake_calls.wait_ms_total - before;
}

int main(void) {
    CASE("recent key, held key, lit LEDs: no wait");
    reset_firmware();
    USBD1.state        = USB_STOP;
    fake_last_input_ms = fake_ms - 1000;
    CHECK_EQ(idle_wait(), 0);
    fake_last_input_ms = fake_ms - 60000;
    fake_matrix[1]     = 4;
    CHECK_EQ(idle_wait(), 0);
    fake_matrix[1]   = 0;
    fake_rgb_enabled = true;
    CHECK_EQ(idle_wait(), 0);

    CASE("dark and idle: AP2_IDLE_SCAN_MS");
    fake_rgb_enabled = false;
    CHECK_EQ(idle_wait(), AP2_IDLE_SCAN_MS);

    CASE("USB active: no wait");
    USBD1.state = USB_ACTIVE;
    CHECK_EQ(idle_wait(), 0);
    return TEST_RESULT();
}
