/* The fixes of the findings USB-1 to USB-5, BLE-1 and CFG-4 (2026-11-16), driven through the real functions of
 * annepro2.c, annepro2_ble.c and annepro2_battery.c. Each case names its finding.
 *
 *   USB-1, USB-2, USB-5  idle_task(): the wait of a scan (annepro2.c, comment above AP2_IDLE_SCAN_MS)
 *   USB-3                powered_down is cleared by suspend_wakeup_init_kb(), and the power down is undone when the
 *                        bus is no longer suspended and not active
 *   USB-4                housekeeping_task_kb() does not call housekeeping_task_user() (QMK does)
 *   BLE-1                annepro2_ble_use_driver() cancels the connect fall back
 *   CFG-4                a version report of the BLE MCU asks for the battery level to be sent again
 */
#include "all_sources.h"
#include "ble_frames.h"

static void setup(void) {
    reset_firmware();
}

/* One scan's idle wait: the number of milliseconds idle_task() waited. */
static uint32_t idle_wait(void) {
    const uint32_t before = fake_calls.wait_ms_total;
    idle_task();
    return fake_calls.wait_ms_total - before;
}

static void set_usb(usbstate_t state, usbstate_t saved) {
    USBD1.state       = state;
    USBD1.saved_state = saved;
}

static void case_idle(void) {
    /* The table. "key recent": last key 5 s ago (AP2_IDLE_AFTER_MS is 10 s); "dark and idle": LEDs off, no key for
     * 20 s, no key held. */
    CASE("USB-1/2/5: a USB host that is active gets no wait, whatever else is going on");
    setup();
    set_usb(USB_ACTIVE, USB_ACTIVE);
    CHECK_EQ(idle_wait(), 0);
    fake_last_input_ms = fake_ms - 60000; /* dark and idle */
    CHECK_EQ(idle_wait(), 0);
    annepro2_ble_use_driver(); /* active host, reports on Bluetooth: still the host's rhythm */
    CHECK_EQ(idle_wait(), 0);

    CASE("USB-2: a host that sleeps while the reports go to USB: usb_suspend_task() waits, idle_task() does not");
    setup();
    set_usb(USB_SUSPENDED, USB_ACTIVE);
    fake_last_input_ms = fake_ms - 60000;
    CHECK_EQ(idle_wait(), 0);
    fake_last_input_ms = fake_ms - 1000;
    CHECK_EQ(idle_wait(), 0);

    CASE("USB-1: a host that sleeps while the reports go to Bluetooth: idle is allowed");
    setup();
    set_usb(USB_SUSPENDED, USB_ACTIVE);
    annepro2_ble_use_driver();
    fake_last_input_ms = fake_ms - 60000;
    CHECK_EQ(idle_wait(), AP2_IDLE_SCAN_MS); /* dark and idle */
    fake_last_input_ms = fake_ms - 1000;
    CHECK_EQ(idle_wait(), AP2_AWAKE_SCAN_MS); /* a recent key */

    CASE("USB-1: a host taken as gone: idle is allowed");
    setup();
    set_usb(USB_SUSPENDED, USB_ACTIVE);
    usb_gone           = true;
    fake_last_input_ms = fake_ms - 60000;
    CHECK(!annepro2_usb_host_in_use());
    CHECK_EQ(idle_wait(), AP2_IDLE_SCAN_MS);

    CASE("USB-5: no USB host, dark and idle: AP2_IDLE_SCAN_MS");
    setup();
    set_usb(USB_STOP, USB_STOP);
    fake_last_input_ms = fake_ms - 60000;
    CHECK_EQ(idle_wait(), 10);

    CASE("USB-5: no USB host and a recent key: AP2_AWAKE_SCAN_MS");
    setup();
    set_usb(USB_STOP, USB_STOP);
    fake_last_input_ms = fake_ms - 5000;
    CHECK_EQ(idle_wait(), 1);
    fake_last_input_ms = fake_ms - (AP2_IDLE_AFTER_MS - 1);
    CHECK_EQ(idle_wait(), 1);
    fake_last_input_ms = fake_ms - AP2_IDLE_AFTER_MS;
    CHECK_EQ(idle_wait(), 10); /* exactly 10 s: idle */

    CASE("USB-5: a key held, long after the last event: AP2_AWAKE_SCAN_MS");
    setup();
    set_usb(USB_STOP, USB_STOP);
    fake_last_input_ms = fake_ms - 60000;
    for (int row = 0; row < MATRIX_ROWS; row++) {
        memset(fake_matrix, 0, sizeof(fake_matrix));
        fake_matrix[row] = 1u << 3;
        CHECK_EQ(idle_wait(), 1);
    }
    memset(fake_matrix, 0, sizeof(fake_matrix));
    CHECK_EQ(idle_wait(), 10);

    CASE("USB-5: the LEDs lit: AP2_AWAKE_SCAN_MS");
    setup();
    set_usb(USB_STOP, USB_STOP);
    fake_last_input_ms = fake_ms - 60000;
    fake_rgb_enabled   = true;
    CHECK(ap2_rgb_lit());
    CHECK_EQ(idle_wait(), 1);
    fake_rgb_suspended = true; /* dark with a sleeping host */
    CHECK_EQ(idle_wait(), 10);
    fake_rgb_suspended = false;
    fake_last_input_ms = fake_ms - (RGB_MATRIX_TIMEOUT + 1); /* past the LED timeout */
    CHECK_EQ(idle_wait(), 10);

    CASE("USB-1/5: Bluetooth with no USB at all, the same table");
    setup();
    set_usb(USB_STOP, USB_STOP);
    annepro2_ble_use_driver();
    fake_last_input_ms = fake_ms - 60000;
    CHECK_EQ(idle_wait(), 10);
    fake_last_input_ms = fake_ms;
    CHECK_EQ(idle_wait(), 1);

    CASE("USB-5: the wait goes through matrix_scan_kb()");
    setup();
    set_usb(USB_STOP, USB_STOP);
    fake_last_input_ms = fake_ms - 60000;
    fake_pin_c10 = true; /* the Bluetooth switch is off: it does not select the driver */
    const uint32_t before = fake_calls.wait_ms_total;
    matrix_scan_kb();
    CHECK_EQ(fake_calls.wait_ms_total - before, 10);
}

static void case_powered_down(void) {
    CASE("USB-3: resume and suspend again within one scan: the power down is entered again");
    setup();
    set_usb(USB_SUSPENDED, USB_ACTIVE);
    usb_suspend_task();
    CHECK_EQ(fake_calls.suspend_power_down, 1);
    /* the host resumes: QMK runs suspend_wakeup_init() (which clears the LEDs state and the keys), and suspends again
     * before the keyboard task has seen the bus awake */
    suspend_wakeup_init_kb();
    usb_suspend_task();
    CHECK_EQ(fake_calls.suspend_power_down, 2);
    usb_suspend_task();
    CHECK_EQ(fake_calls.suspend_power_down, 2); /* and only once */

    CASE("USB-3: a bus reset that is never configured undoes the power down");
    setup();
    set_usb(USB_SUSPENDED, USB_ACTIVE);
    usb_suspend_task();
    CHECK_EQ(fake_calls.suspend_power_down, 1);
    CHECK_EQ(fake_calls.suspend_wakeup_init, 0);
    set_usb(USB_READY, USB_ACTIVE); /* reset by the host (or the cable was moved) and not configured */
    usb_suspend_task();
    CHECK_EQ(fake_calls.suspend_wakeup_init, 1); /* the LEDs come back */
    usb_suspend_task();
    CHECK_EQ(fake_calls.suspend_wakeup_init, 1);
    set_usb(USB_SUSPENDED, USB_ACTIVE); /* a host again that sleeps: powered down again */
    usb_suspend_task();
    CHECK_EQ(fake_calls.suspend_power_down, 2);

    CASE("USB-3: an active bus does not run the undo (QMK's wakeup event does that)");
    setup();
    set_usb(USB_SUSPENDED, USB_ACTIVE);
    usb_suspend_task();
    set_usb(USB_ACTIVE, USB_ACTIVE);
    usb_suspend_task();
    CHECK_EQ(fake_calls.suspend_wakeup_init, 0);
    CHECK(!powered_down);

    CASE("USB-3: suspend_wakeup_init_kb() clears the flag");
    setup();
    powered_down = true;
    suspend_wakeup_init_kb();
    CHECK(!powered_down);
}

static void case_housekeeping(void) {
    CASE("USB-4: housekeeping_task_kb() does not call housekeeping_task_user()");
    setup();
    housekeeping_task_kb();
    CHECK_EQ(fake_calls.housekeeping_user, 0);
    annepro2_ble_use_driver();
    housekeeping_task_kb();
    CHECK_EQ(fake_calls.housekeeping_user, 0);
}

static void case_ble1(void) {
    CASE("BLE-1: the switch turned to Bluetooth during a slot tap: the reports stay on Bluetooth");
    setup();
    annepro2_ble_connect(0); /* the tap: Bluetooth, with a fall back to the USB host pending */
    CHECK(annepro2_ble_is_active());
    annepro2_ble_use_driver(); /* the switch selects Bluetooth */
    fake_advance(AP2_BLE_CONNECT_TIMEOUT_MS);
    CHECK(!annepro2_ble_task());
    CHECK(annepro2_ble_is_active());
    fake_advance(60000);
    CHECK(!annepro2_ble_task());
    CHECK(annepro2_ble_is_active());

    CASE("BLE-1: a tap alone still falls back (the fix does not remove the fall back)");
    setup();
    annepro2_ble_connect(0);
    fake_advance(AP2_BLE_CONNECT_TIMEOUT_MS);
    CHECK(annepro2_ble_task());
    CHECK(!annepro2_ble_is_active());
}

static void fill_history(uint16_t raw) {
    for (int i = 0; i < AP2_BATTERY_HISTORY; i++) {
        FAKE_ADC_REG(0x088) = 1;
        FAKE_ADC_REG(0x030) = raw;
        fake_advance(AP2_BATTERY_FILL_MS);
        annepro2_battery_task();
    }
}

static void case_cfg4(void) {
    CASE("CFG-4: a version report asks for the battery level to be sent");
    setup();
    send_now = false;
    bf_version();
    CHECK(send_now);
    CHECK(annepro2_ble_is_ready());

    CASE("CFG-4: after a restart of the BLE MCU the level goes out at once, without a new reading");
    setup();
    bf_version(); /* the start */
    fill_history(2600);
    fake_sd_tx_clear(&SD1);
    CHECK(!send_now);
    FAKE_ADC_REG(0x08C) = 0;
    bf_version(); /* the BLE MCU restarted */
    CHECK(send_now);
    annepro2_battery_task();
    CHECK_EQ(SD1.tx_len, 12);
    CHECK_EQ(SD1.tx[8], 0x01);
    CHECK_EQ(SD1.tx[9], 0x05);
    CHECK_EQ(SD1.tx[10] | (SD1.tx[11] << 8), 2600);
    CHECK_EQ(FAKE_ADC_REG(0x08C), 0); /* no conversion */
    annepro2_battery_task();
    CHECK_EQ(SD1.tx_len, 12);

    CASE("CFG-4: the first send after boot still waits for the full history");
    setup();
    bf_version();
    for (int i = 0; i < AP2_BATTERY_HISTORY - 1; i++) {
        FAKE_ADC_REG(0x088) = 1;
        FAKE_ADC_REG(0x030) = 2600;
        fake_advance(AP2_BATTERY_FILL_MS);
        annepro2_battery_task();
        CHECK_EQ(SD1.tx_len, 0);
    }
    bf_version(); /* a second version report meanwhile does not break the wait */
    annepro2_battery_task();
    CHECK_EQ(SD1.tx_len, 0);
    FAKE_ADC_REG(0x030) = 2600;
    fake_advance(AP2_BATTERY_FILL_MS);
    annepro2_battery_task();
    CHECK_EQ(SD1.tx_len, 12);
}

int main(void) {
    case_idle();
    case_powered_down();
    case_housekeeping();
    case_ble1();
    case_cfg4();
    return TEST_RESULT();
}
