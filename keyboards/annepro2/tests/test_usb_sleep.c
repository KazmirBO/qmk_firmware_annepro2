/* The USB sleep logic of annepro2.c: the keyboard builds with NO_USB_STARTUP_CHECK and handles a sleeping USB host
 * itself (usb_proxy_*(), usb_suspend_task(), annepro2_usb_host_in_use()), and keeps the Bluetooth state over a USB
 * wakeup (housekeeping_task_kb(), suspend_wakeup_init_kb()).
 *
 * Where the behaviour is documented:
 *   annepro2.c   the long comment above USB_GETSTATUS_REMOTE_WAKEUP_ENABLED, and the comment above ble_saved_report
 *   readme.md    the paragraph on NO_USB_STARTUP_CHECK: a report that presses something wakes the host and is lost, a
 *                key that sends nothing wakes nothing, the power down once and a scan every 17 ms,
 *                AP2_USB_WAKE_TIMEOUT_MS (2000) after which the host is taken as gone until the bus changes
 *                state, the wakeup repeated while a key is held
 *   docs/07-port-qmk-i-narzedzia.md
 *
 * The fake USB driver is a state that the test sets (USBD1.state, saved_state, status); the fake senders of QMK's USB
 * driver count what the wrappers pass on. */
#include "all_sources.h"

/* The keymap hook that suspend_wakeup_init_kb() ends with (weak and empty in the fakes). */
static int user_wakeups;
void       suspend_wakeup_init_user(void) {
    user_wakeups++;
}

static void sleep_bus(void) {
    USBD1.state       = USB_SUSPENDED;
    USBD1.saved_state = USB_ACTIVE;
}

static void wake_bus(void) {
    USBD1.state       = USB_ACTIVE;
    USBD1.saved_state = USB_ACTIVE;
}

static void setup(void) {
    reset_firmware();
    usb_wrap_driver();
}

static report_keyboard_t pressing(void) {
    report_keyboard_t r;
    memset(&r, 0, sizeof(r));
    r.keys[0] = 0x04;
    return r;
}

static void send_press(void) {
    report_keyboard_t r = pressing();
    chibios_driver.send_keyboard(&r);
}

static void send_release(void) {
    report_keyboard_t r;
    memset(&r, 0, sizeof(r));
    chibios_driver.send_keyboard(&r);
}

/* One pass of the main loop while the host sleeps: usb_suspend_task() waits 17 ms when it powers down. */
static void scan(void) {
    usb_suspend_task();
    CHECK(!(usb_wake_pending && usb_gone)); /* idle, asked and waiting, or given up (annepro2.c) */
}

static void case_wrapping(void) {
    CASE("the senders of QMK's USB driver are replaced, the rest is kept");
    reset_firmware();
    void (*orig)(report_keyboard_t *) = chibios_driver.send_keyboard;
    uint8_t (*leds)(void)             = chibios_driver.keyboard_leds;
    usb_wrap_driver();
    CHECK(chibios_driver.send_keyboard != orig);
    CHECK(chibios_driver.send_nkro != send_nkro);
    CHECK(chibios_driver.send_mouse != send_mouse);
    CHECK(chibios_driver.send_extra != send_extra);
    CHECK(chibios_driver.keyboard_leds == leds);

    CASE("every report is passed on to QMK's sender, awake or asleep");
    setup();
    send_press();
    CHECK_EQ(fake_calls.usb_keyboard, 1);
    CHECK_EQ(fake_usb_last_keyboard.keys[0], 0x04);
    sleep_bus();
    send_release();
    send_press();
    CHECK_EQ(fake_calls.usb_keyboard, 3);
    report_nkro_t n;
    memset(&n, 0, sizeof(n));
    chibios_driver.send_nkro(&n);
    report_mouse_t m;
    memset(&m, 0, sizeof(m));
    chibios_driver.send_mouse(&m);
    report_extra_t e = {.report_id = REPORT_ID_CONSUMER, .usage = 0};
    chibios_driver.send_extra(&e);
    CHECK_EQ(fake_calls.usb_nkro, 1);
    CHECK_EQ(fake_calls.usb_mouse, 1);
    CHECK_EQ(fake_calls.usb_extra, 1);
}

static void case_wakeup_signal(void) {
    CASE("a pressing report to a sleeping host signals a wakeup");
    setup();
    sleep_bus();
    send_press();
    CHECK_EQ(fake_calls.usb_wakeup_host, 1);
    CHECK(usb_wake_pending);

    CASE("an empty report does not");
    setup();
    sleep_bus();
    send_release();
    CHECK_EQ(fake_calls.usb_wakeup_host, 0);
    CHECK(!usb_wake_pending);

    CASE("a report to an awake host does not");
    setup();
    send_press();
    CHECK_EQ(fake_calls.usb_wakeup_host, 0);
    CHECK(!usb_wake_pending);

    CASE("a modifier alone presses");
    setup();
    sleep_bus();
    report_keyboard_t r;
    memset(&r, 0, sizeof(r));
    r.mods = 0x02;
    chibios_driver.send_keyboard(&r);
    CHECK_EQ(fake_calls.usb_wakeup_host, 1);

    CASE("every key slot counts");
    for (int i = 0; i < 6; i++) {
        setup();
        sleep_bus();
        memset(&r, 0, sizeof(r));
        r.keys[i] = 0x1d;
        chibios_driver.send_keyboard(&r);
        CHECK_EQ(fake_calls.usb_wakeup_host, 1);
    }

    CASE("NKRO report: modifiers or any bit presses, empty does not");
    setup();
    sleep_bus();
    report_nkro_t n;
    memset(&n, 0, sizeof(n));
    chibios_driver.send_nkro(&n);
    CHECK_EQ(fake_calls.usb_wakeup_host, 0);
    for (size_t i = 0; i < sizeof(n.bits); i++) {
        setup();
        sleep_bus();
        memset(&n, 0, sizeof(n));
        n.bits[i] = 0x80;
        chibios_driver.send_nkro(&n);
        CHECK_EQ(fake_calls.usb_wakeup_host, 1);
    }
    setup();
    sleep_bus();
    memset(&n, 0, sizeof(n));
    n.mods = 1;
    chibios_driver.send_nkro(&n);
    CHECK_EQ(fake_calls.usb_wakeup_host, 1);

    CASE("mouse: a button or a movement presses, a still mouse does not");
    setup();
    sleep_bus();
    report_mouse_t m;
    memset(&m, 0, sizeof(m));
    chibios_driver.send_mouse(&m);
    CHECK_EQ(fake_calls.usb_wakeup_host, 0);
    m.buttons = 1;
    chibios_driver.send_mouse(&m);
    CHECK_EQ(fake_calls.usb_wakeup_host, 1);

    CASE("consumer key: a usage presses, usage 0 does not");
    setup();
    sleep_bus();
    report_extra_t e = {.report_id = REPORT_ID_CONSUMER, .usage = 0};
    chibios_driver.send_extra(&e);
    CHECK_EQ(fake_calls.usb_wakeup_host, 0);
    e.usage = AUDIO_VOL_UP;
    chibios_driver.send_extra(&e);
    CHECK_EQ(fake_calls.usb_wakeup_host, 1);

    CASE("a suspended bus that never had a host (not configured) is not a sleeping host");
    setup();
    USBD1.state       = USB_SUSPENDED;
    USBD1.saved_state = USB_READY;
    send_press();
    CHECK_EQ(fake_calls.usb_wakeup_host, 0);
    CHECK(!usb_wake_pending);
    CHECK(!annepro2_usb_host_in_use());

    CASE("a host that does not allow remote wakeup is not signalled, but the timeout starts");
    setup();
    sleep_bus();
    USBD1.status = 0;
    send_press();
    CHECK_EQ(fake_calls.usb_wakeup_host, 0);
    CHECK(usb_wake_pending);
    fake_advance(AP2_USB_WAKE_TIMEOUT_MS);
    scan();
    CHECK(usb_gone);
    CHECK(!annepro2_usb_host_in_use());
}

static void case_repeat(void) {
    /* "signal a wakeup, at most every AP2_USB_WAKE_RETRY_MS (500)" and "repeated while a key is held" */
    CASE("a second press within 500 ms does not signal again, one after 500 ms does");
    setup();
    sleep_bus();
    send_press();
    CHECK_EQ(fake_calls.usb_wakeup_host, 1);
    send_release();
    fake_advance(499);
    send_press();
    CHECK_EQ(fake_calls.usb_wakeup_host, 1);
    send_release();
    fake_advance(1);
    send_press();
    CHECK_EQ(fake_calls.usb_wakeup_host, 2);

    CASE("a key held: the signal is repeated every 500 ms by the scan");
    setup();
    sleep_bus();
    send_press();
    const uint32_t start = fake_ms;
    while (fake_ms - start < 1700) {
        fake_advance(1);
        usb_suspend_task(); /* powered down: each scan also waits 17 ms */
    }
    CHECK_EQ(fake_calls.usb_wakeup_host, 4); /* at 0, about 500, 1000 and 1500 ms */

    CASE("the repeat continues after the host was taken as gone (a key still held)");
    setup();
    sleep_bus();
    send_press();
    fake_advance(AP2_USB_WAKE_TIMEOUT_MS);
    scan();
    CHECK(usb_gone);
    const int before = fake_calls.usb_wakeup_host;
    fake_advance(AP2_USB_WAKE_RETRY_MS);
    usb_suspend_task();
    CHECK_EQ(fake_calls.usb_wakeup_host, before + 1);

    CASE("a released key is not signalled again by the scan");
    setup();
    sleep_bus();
    send_press();
    send_release();
    const int once = fake_calls.usb_wakeup_host;
    fake_advance(3000);
    scan();
    CHECK_EQ(fake_calls.usb_wakeup_host, once);

    CASE("a new press after the host was taken as gone signals again (after the retry time)");
    setup();
    sleep_bus();
    send_press();
    send_release();
    fake_advance(AP2_USB_WAKE_TIMEOUT_MS);
    scan();
    CHECK(usb_gone);
    const int n1 = fake_calls.usb_wakeup_host;
    send_press();
    CHECK_EQ(fake_calls.usb_wakeup_host, n1 + 1);
    send_release();
    send_press(); /* at once again: within the retry time */
    CHECK_EQ(fake_calls.usb_wakeup_host, n1 + 1);

    CASE("the 16-bit signal time wraps");
    setup();
    sleep_bus();
    fake_ms = 65536u * 4 - 100;
    send_press();
    send_release();
    fake_advance(499);
    send_press();
    CHECK_EQ(fake_calls.usb_wakeup_host, 1);
    send_release();
    fake_advance(1);
    send_press();
    CHECK_EQ(fake_calls.usb_wakeup_host, 2);
}

static void case_timeout(void) {
    CASE("the host is taken as gone after AP2_USB_WAKE_TIMEOUT_MS from the first request");
    setup();
    sleep_bus();
    send_press();
    send_release();
    fake_advance(AP2_USB_WAKE_TIMEOUT_MS - 1);
    usb_suspend_task();
    CHECK(!usb_gone);
    CHECK(usb_wake_pending);
    CHECK(annepro2_usb_host_in_use());
    fake_advance(1);
    usb_suspend_task();
    CHECK(usb_gone);
    CHECK(!usb_wake_pending);
    CHECK(!annepro2_usb_host_in_use());

    CASE("the timeout runs from the first request, a later press does not extend it");
    setup();
    sleep_bus();
    send_press();
    send_release();
    fake_advance(1500);
    send_press();
    send_release();
    fake_advance(500);
    usb_suspend_task();
    CHECK(usb_gone);

    CASE("no request, no timeout: a sleeping host stays a host");
    setup();
    sleep_bus();
    fake_advance(60000);
    usb_suspend_task();
    CHECK(!usb_gone);
    CHECK(annepro2_usb_host_in_use());

    CASE("a host that resumes before the timeout clears the request");
    setup();
    sleep_bus();
    send_press();
    wake_bus();
    usb_suspend_task();
    CHECK(!usb_wake_pending);
    CHECK(!usb_gone);
    sleep_bus();
    fake_advance(AP2_USB_WAKE_TIMEOUT_MS);
    usb_suspend_task();
    CHECK(!usb_gone); /* the old request does not count any more */

    CASE("gone lasts until the bus changes state");
    setup();
    sleep_bus();
    send_press();
    send_release();
    fake_advance(AP2_USB_WAKE_TIMEOUT_MS);
    usb_suspend_task();
    CHECK(usb_gone);
    fake_advance(100000);
    usb_suspend_task();
    CHECK(usb_gone);
    wake_bus(); /* resumed, reset or configured */
    usb_suspend_task();
    CHECK(!usb_gone);
    CHECK(annepro2_usb_host_in_use());
    sleep_bus();
    CHECK(annepro2_usb_host_in_use()); /* asleep again: a host again */

    CASE("the timeout survives the 32-bit wrap");
    setup();
    sleep_bus();
    fake_ms = 0xFFFFFFF0u;
    send_press();
    send_release();
    fake_advance(AP2_USB_WAKE_TIMEOUT_MS - 1);
    usb_suspend_task();
    CHECK(!usb_gone);
    fake_advance(1);
    usb_suspend_task();
    CHECK(usb_gone);
}

static void case_power_down(void) {
    CASE("the power down is entered once, then a scan takes 17 ms");
    setup();
    sleep_bus();
    for (int i = 0; i < 20; i++) scan();
    CHECK_EQ(fake_calls.suspend_power_down, 1);
    CHECK_EQ(fake_calls.wait_ms, 20);
    CHECK_EQ(fake_calls.wait_ms_total, 20 * 17);
    CHECK_EQ(fake_calls.suspend_wakeup_init, 0);

    CASE("an awake host: no power down, no wait");
    setup();
    for (int i = 0; i < 20; i++) scan();
    CHECK_EQ(fake_calls.suspend_power_down, 0);
    CHECK_EQ(fake_calls.wait_ms, 0);

    CASE("no host at all (cable out, or a suspended bus never configured): no power down");
    setup();
    USBD1.state = USB_STOP;
    scan();
    USBD1.state       = USB_SUSPENDED;
    USBD1.saved_state = USB_READY;
    scan();
    CHECK_EQ(fake_calls.suspend_power_down, 0);
    CHECK_EQ(fake_calls.wait_ms, 0);

    CASE("the host taken as gone ends the power down");
    setup();
    sleep_bus();
    send_press();
    send_release();
    scan();
    CHECK_EQ(fake_calls.suspend_power_down, 1);
    fake_advance(AP2_USB_WAKE_TIMEOUT_MS);
    scan();
    CHECK_EQ(fake_calls.suspend_wakeup_init, 1); /* undone here: no USB wakeup will do it */
    const int waits = fake_calls.wait_ms;
    scan();
    scan();
    CHECK_EQ(fake_calls.wait_ms, waits); /* full speed scanning again */
    CHECK_EQ(fake_calls.suspend_wakeup_init, 1);
    CHECK_EQ(fake_calls.suspend_power_down, 1);

    CASE("a resume and a new sleep power down again, once");
    setup();
    sleep_bus();
    scan();
    wake_bus();
    scan();
    sleep_bus();
    scan();
    scan();
    CHECK_EQ(fake_calls.suspend_power_down, 2);
    CHECK_EQ(fake_calls.suspend_wakeup_init, 0); /* the USB wakeup event does that, not this task */

    CASE("with the reports on Bluetooth the keyboard does not power down for a sleeping USB host");
    setup();
    sleep_bus();
    annepro2_ble_use_driver();
    for (int i = 0; i < 20; i++) scan();
    CHECK_EQ(fake_calls.suspend_power_down, 0);
    CHECK_EQ(fake_calls.wait_ms, 0);

    CASE("a switch to Bluetooth while powered down undoes the power down");
    setup();
    sleep_bus();
    scan();
    CHECK_EQ(fake_calls.suspend_power_down, 1);
    annepro2_ble_use_driver();
    scan();
    CHECK_EQ(fake_calls.suspend_wakeup_init, 1);
    scan();
    CHECK_EQ(fake_calls.suspend_wakeup_init, 1);
    const int w = fake_calls.wait_ms;
    scan();
    CHECK_EQ(fake_calls.wait_ms, w);

    CASE("back to the USB driver while the host still sleeps: powered down again");
    annepro2_ble_disconnect();
    scan();
    CHECK_EQ(fake_calls.suspend_power_down, 2);
}

static void case_bluetooth_state(void) {
    /* annepro2.c: "A USB host that wakes up runs suspend_wakeup_init(), which clears the held keys and mods even while
     * the reports go to Bluetooth. The Bluetooth state at the end of each loop is kept and put back in that case". */
    CASE("on Bluetooth the held keys and mods survive a USB wakeup");
    setup();
    annepro2_ble_use_driver();
    keyboard_report->mods    = 0x02;
    keyboard_report->keys[0] = 0x04;
    keyboard_report->keys[1] = 0x05;
    set_mods(0x02);
    housekeeping_task_kb(); /* the end of the loop */
    /* the USB wakeup: QMK clears the keyboard */
    memset(keyboard_report, 0, sizeof(*keyboard_report));
    set_mods(0);
    suspend_wakeup_init_kb();
    CHECK_EQ(keyboard_report->mods, 0x02);
    CHECK_EQ(keyboard_report->keys[0], 0x04);
    CHECK_EQ(keyboard_report->keys[1], 0x05);
    CHECK_EQ(get_mods(), 0x02);

    CASE("on USB the state is not kept: the wakeup clears it as QMK does");
    setup();
    keyboard_report->keys[0] = 0x04;
    set_mods(0x02);
    housekeeping_task_kb();
    memset(keyboard_report, 0, sizeof(*keyboard_report));
    set_mods(0);
    suspend_wakeup_init_kb();
    CHECK_EQ(keyboard_report->keys[0], 0);
    CHECK_EQ(get_mods(), 0);

    CASE("switched to Bluetooth in the loop after the state was taken: nothing is restored");
    setup();
    keyboard_report->keys[0] = 0x04;
    set_mods(0x02);
    housekeeping_task_kb(); /* USB at the end of the loop: nothing saved */
    annepro2_ble_use_driver();
    memset(keyboard_report, 0, sizeof(*keyboard_report));
    set_mods(0);
    suspend_wakeup_init_kb();
    CHECK_EQ(keyboard_report->keys[0], 0);
    CHECK_EQ(get_mods(), 0);

    CASE("back on USB when the wakeup arrives: nothing is restored");
    setup();
    annepro2_ble_use_driver();
    keyboard_report->keys[0] = 0x04;
    set_mods(0x02);
    housekeeping_task_kb();
    annepro2_ble_disconnect();
    memset(keyboard_report, 0, sizeof(*keyboard_report));
    set_mods(0);
    suspend_wakeup_init_kb();
    CHECK_EQ(keyboard_report->keys[0], 0);
    CHECK_EQ(get_mods(), 0);

    CASE("the saved state is the one of the last loop");
    setup();
    annepro2_ble_use_driver();
    keyboard_report->keys[0] = 0x04;
    housekeeping_task_kb();
    keyboard_report->keys[0] = 0x09;
    housekeeping_task_kb();
    memset(keyboard_report, 0, sizeof(*keyboard_report));
    suspend_wakeup_init_kb();
    CHECK_EQ(keyboard_report->keys[0], 0x09);

    /* annepro2.c housekeeping_task_kb(): the state is saved only in a loop that ends on Bluetooth, and a loop that ends
     * on USB drops the copy of an earlier one; otherwise a wakeup after a switch back to Bluetooth would press a key
     * that was released long ago (readme.md: "A USB host that wakes up while the keystrokes go to Bluetooth no longer
     * drops the keys and modifiers held on Bluetooth", but only the ones held now). */
    CASE("USB wakeup over Bluetooth: a report saved in an earlier loop is not put back after a switch to Bluetooth");
    setup();
    fake_pin_c10 = true;
    ble_ready    = true;
    annepro2_ble_use_driver();
    keyboard_report->keys[0] = 0x04;
    housekeeping_task_kb(); /* saved on Bluetooth */
    annepro2_ble_disconnect();
    housekeeping_task_kb(); /* the next loop is on USB: nothing is saved now, the old copy is stale */
    annepro2_ble_use_driver();
    memset(keyboard_report, 0, sizeof(*keyboard_report));
    suspend_wakeup_init_kb();
    CHECK_EQ(keyboard_report->keys[0], 0);
}

static void case_in_use(void) {
    CASE("annepro2_usb_host_in_use(): active, or asleep after it had configured the keyboard and not taken as gone");
    setup();
    static const struct {
        usbstate_t state, saved;
        bool       in_use;
    } table[] = {
        {USB_UNINIT, USB_UNINIT, false}, {USB_STOP, USB_STOP, false},  {USB_READY, USB_READY, false},
        {USB_SELECTED, USB_READY, false}, {USB_ACTIVE, USB_ACTIVE, true}, {USB_SUSPENDED, USB_ACTIVE, true},
        {USB_SUSPENDED, USB_READY, false}, {USB_SUSPENDED, USB_SELECTED, false}, {USB_SUSPENDED, USB_STOP, false},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        USBD1.state       = table[i].state;
        USBD1.saved_state = table[i].saved;
        CHECK_EQ(annepro2_usb_host_in_use(), table[i].in_use);
    }
    usb_gone = true;
    USBD1.state       = USB_SUSPENDED;
    USBD1.saved_state = USB_ACTIVE;
    CHECK(!annepro2_usb_host_in_use());
    USBD1.state = USB_ACTIVE; /* gone is cleared by the next scan, but an active host is in use anyway */
    CHECK(annepro2_usb_host_in_use());
}

/* annepro2.c usb_proxy_mouse(): any motion, also a horizontal scroll alone, wakes a sleeping host; and
 * suspend_wakeup_init_kb() ends with the keymap's hook, as QMK's _kb hooks do. */
static void case_mouse_and_hook(void) {
    CASE("a horizontal scroll alone wakes a sleeping USB host");
    setup();
    sleep_bus();
    report_mouse_t m;
    memset(&m, 0, sizeof(m));
    m.h = 1;
    chibios_driver.send_mouse(&m);
    CHECK_EQ(fake_calls.usb_wakeup_host, 1);

    CASE("suspend_wakeup_init_kb() calls suspend_wakeup_init_user()");
    setup();
    user_wakeups = 0;
    suspend_wakeup_init_kb();
    CHECK_EQ(user_wakeups, 1);
}

int main(void) {
    case_wrapping();
    case_wakeup_signal();
    case_repeat();
    case_timeout();
    case_power_down();
    case_bluetooth_state();
    case_in_use();
    case_mouse_and_hook();
    return TEST_RESULT();
}
