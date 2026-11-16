/* The battery code (annepro2_battery.c): the conversion of the ADC reading to a percent, the correction of the low end,
 * the history filter and when the level is sent to the BLE MCU, the back-off after failed readings, and the indicator
 * on the number row.
 *
 * Where the behaviour is documented:
 *   annepro2_battery.h   the options and what they do (interval, history, fill time, show time, scale, empty voltage)
 *   annepro2_battery.c   the comments at percent_from_mv() (the curve of the BLE MCU), annepro2_battery_scaled(),
 *                        AP2_BATTERY_MAX_FAILS, show_display()
 *   readme.md            AP2_BLE_BATTERY and KC_AP2_BATT
 *
 * The ADC registers are a block of memory at 0x40010000 (fake_hw.c). A reading is what the test stores in the result
 * register while the end-of-conversion flag is set; all 8 samples of one reading are then equal, so the trimmed mean
 * itself (the highest and lowest sample dropped) cannot be shown here. */
#include "all_sources.h"
#include "proto_decode.h"

#define ADC_ISR FAKE_ADC_REG(0x088)
#define ADC_DR0 FAKE_ADC_REG(0x030)
#define ADC_ICLR FAKE_ADC_REG(0x08C)

static void setup(void) {
    reset_firmware();
    ble_ready = true; /* the BLE MCU has reported its version */
}

static void adc_value(uint16_t raw) {
    ADC_ISR = 1;
    ADC_DR0 = raw;
}

static void adc_dead(void) {
    ADC_ISR = 0;
}

/* The voltage as the BLE MCU computes it: raw * 2 * 3.1 / 4095, in millivolts, cut to an integer. */
static uint32_t mv_of(uint32_t raw) {
    return raw * 6200u / 4095u;
}

/* The curve of the BLE MCU, written down from the table in annepro2_battery.c / docs/05-ble.md, independently of the
 * code: start voltage, percent at the start, mV per 10 %. */
static const struct {
    uint16_t start;
    uint8_t  base;
    uint8_t  width;
} steps[] = {{3400, 10, 100}, {3500, 20, 100}, {3600, 30, 50},  {3650, 40, 50}, {3700, 50, 50},
             {3750, 60, 50},  {3800, 70, 100}, {3900, 80, 100}, {4000, 90, 150}};

static int reference_percent(uint32_t mv) {
    if (mv >= 4150) return 100;
    if (mv < 3400) return 0;
    int i = (int)(sizeof(steps) / sizeof(steps[0])) - 1;
    while (mv < steps[i].start) i--;
    return steps[i].base + (int)((mv - steps[i].start) * 10 / steps[i].width);
}

static void case_percent(void) {
    CASE("percent: every raw reading matches the curve");
    int mismatches = 0;
    for (uint32_t raw = 0; raw < 4096; raw++) {
        if (annepro2_battery_percent((uint16_t)raw) != reference_percent(mv_of(raw))) {
            if (!mismatches) printf("    first mismatch: raw %u (%u mV)\n", raw, mv_of(raw));
            mismatches++;
        }
    }
    CHECK_EQ(mismatches, 0);

    CASE("percent: limits");
    CHECK_EQ(annepro2_battery_percent(0), 0);
    CHECK_EQ(annepro2_battery_percent(4095), 100);

    CASE("percent: monotonic over all raw readings, 0 to 100");
    int prev = 0, bad = 0;
    for (uint32_t raw = 0; raw < 4096; raw++) {
        const int p = annepro2_battery_percent((uint16_t)raw);
        if (p < prev || p > 100) bad++;
        prev = p;
    }
    CHECK_EQ(bad, 0);

    CASE("percent: at each step boundary the percent is the base, one reading lower it is one less");
    for (size_t i = 0; i <= sizeof(steps) / sizeof(steps[0]); i++) {
        const uint32_t start = i < sizeof(steps) / sizeof(steps[0]) ? steps[i].start : 4150;
        const int      base  = i < sizeof(steps) / sizeof(steps[0]) ? steps[i].base : 100;
        uint32_t       raw   = 0;
        while (mv_of(raw) < start) raw++; /* the first reading at or above the boundary */
        CHECK_EQ(annepro2_battery_percent((uint16_t)raw), base);
        CHECK_EQ(annepro2_battery_percent((uint16_t)(raw - 1)), start == 3400 ? 0 : base - 1);
    }

    CASE("percent: the voltage of a reading is truncated, not rounded");
    /* 4095 counts are 6200 mV; the reading 2748 is 4160.6 mV and counts as 4160 */
    CHECK_EQ(mv_of(2748), 4160);
    CHECK_EQ(annepro2_battery_percent(2748), 100);
    /* the first reading at 4150 mV: 2742 gives 4151 mV; 2741 gives 4149 mV (99 %) */
    CHECK_EQ(annepro2_battery_percent(2742), 100);
    CHECK_EQ(annepro2_battery_percent(2741), 99);
}

static void case_scaled(void) {
    CASE("scaled: above 3.6 V the reading is not changed");
    int changed = 0;
    for (uint32_t raw = 0; raw < 4096; raw++) {
        if (mv_of(raw) >= 3600 && annepro2_battery_scaled((uint16_t)raw) != raw) changed++;
    }
    CHECK_EQ(changed, 0);

    CASE("scaled: monotonic over all raw readings, never above 12 bits");
    uint32_t prev = 0, down = 0, big = 0;
    for (uint32_t raw = 0; raw < 4096; raw++) {
        const uint32_t s = annepro2_battery_scaled((uint16_t)raw);
        if (s < prev) down++;
        if (s > 4095) big++;
        prev = s;
    }
    CHECK_EQ(down, 0);
    CHECK_EQ(big, 0);
    CHECK_EQ(annepro2_battery_scaled(0), 0);
    CHECK_EQ(annepro2_battery_scaled(4095), 4095);
    CHECK_EQ(annepro2_battery_scaled(0xffff), 4095); /* a value above 12 bits is cut */

    CASE("scaled: below the empty voltage the level is 0 %, from it 10 % or more");
    int wrong_low = 0, wrong_high = 0;
    uint32_t first_ten = 0;
    for (uint32_t raw = 0; raw < 4096; raw++) {
        const uint32_t mv = mv_of(raw);
        const int      p  = annepro2_battery_percent(annepro2_battery_scaled((uint16_t)raw));
        if (mv < AP2_BATTERY_EMPTY_MV && p != 0) wrong_low++;
        if (mv >= AP2_BATTERY_EMPTY_MV && mv < 3600 && (p < 10 || p > 29)) wrong_high++;
        if (!first_ten && p >= 10) first_ten = mv;
    }
    CHECK_EQ(wrong_low, 0);
    CHECK_EQ(wrong_high, 0);
    /* 10 % starts at the empty voltage */
    CHECK(first_ten >= AP2_BATTERY_EMPTY_MV && first_ten <= AP2_BATTERY_EMPTY_MV + 2);

    CASE("scaled: 3.2 V is the first 10 %, 3.6 V is the first 30 %");
    uint32_t raw3200 = 0;
    while (mv_of(raw3200) < 3200) raw3200++;
    CHECK_EQ(annepro2_battery_percent(annepro2_battery_scaled((uint16_t)raw3200)), 10);
    CHECK_EQ(annepro2_battery_percent(annepro2_battery_scaled((uint16_t)(raw3200 - 1))), 0);
    uint32_t raw3600 = 0;
    while (mv_of(raw3600) < 3600) raw3600++;
    CHECK_EQ(annepro2_battery_percent(annepro2_battery_scaled((uint16_t)raw3600)), 30);
    CHECK(annepro2_battery_percent(annepro2_battery_scaled((uint16_t)(raw3600 - 1))) <= 29);

    CASE("scaled: the stretched part is continuous (no jump of more than a few percent per count)");
    int jump = 0;
    for (uint32_t raw = 1; raw < 4096; raw++) {
        const int a = annepro2_battery_percent(annepro2_battery_scaled((uint16_t)(raw - 1)));
        const int b = annepro2_battery_percent(annepro2_battery_scaled((uint16_t)raw));
        if (b - a > 3 && mv_of(raw) > AP2_BATTERY_EMPTY_MV + 5) jump++;
    }
    CHECK_EQ(jump, 0);
}

static void case_read(void) {
    CASE("init: the registers of the stock firmware");
    setup();
    annepro2_battery_init();
    CHECK_EQ(FAKE_ADC_REG(0x000), 0x80);     /* control: enabled */
    CHECK_EQ(FAKE_ADC_REG(0x004) & 0x1f, 7); /* the list starts with channel 7 (PA7) */
    CHECK_EQ(FAKE_ADC_REG(0x070), 1);        /* trigger control */
    CHECK_EQ(FAKE_ADC_REG(0x074), 0x10);     /* trigger source */
    CHECK_EQ(FAKE_ADC_REG(0x080) & 1, 1);    /* end of conversion flag enabled (polled, not in the NVIC) */
    CHECK_EQ((fake_ckcu.APBCFGR >> 16) & 7, 6); /* ADC clock HCLK / 64 */
    CHECK(fake_ckcu.APBCCR1 & CKCU_APBCCR1_ADCEN);

    CASE("read: the 12-bit result");
    setup();
    adc_value(1234);
    uint16_t raw = 0;
    CHECK(annepro2_battery_read(&raw));
    CHECK_EQ(raw, 1234);
    ADC_DR0 = 0xF123; /* the bits above 12 are not part of the result */
    CHECK(annepro2_battery_read(&raw));
    CHECK_EQ(raw, 0x123);
    adc_value(4095);
    CHECK(annepro2_battery_read(&raw));
    CHECK_EQ(raw, 4095);
    adc_value(0);
    CHECK(annepro2_battery_read(&raw));
    CHECK_EQ(raw, 0);

    CASE("read: a conversion that does not end fails and leaves the value alone");
    setup();
    adc_dead();
    raw = 777;
    CHECK(!annepro2_battery_read(&raw));
    CHECK_EQ(raw, 777);

    CASE("read: a conversion is started with the software trigger");
    setup();
    adc_value(100);
    FAKE_ADC_REG(0x074) = 0x10;
    CHECK(annepro2_battery_read(&raw));
    CHECK(FAKE_ADC_REG(0x074) & 1);
}

static void case_backoff(void) {
    /* AP2_BATTERY_MAX_FAILS (3) failed readings in a row: the measurement is skipped for AP2_BATTERY_BACKOFF_MS
     * (600000), so that a dead ADC does not stall the matrix scan (annepro2_battery.c). A skipped reading does not
     * touch the ADC: the interrupt clear register, written first by every conversion, stays as the test left it. */
    uint16_t raw;

    CASE("three failures start the back-off");
    setup();
    adc_dead();
    for (int i = 0; i < 3; i++) {
        ADC_ICLR = 0;
        CHECK(!annepro2_battery_read(&raw));
        CHECK_EQ(ADC_ICLR, 1); /* tried */
        fake_advance(1000);
    }
    adc_value(1500); /* the ADC would work now */
    ADC_ICLR = 0;
    CHECK(!annepro2_battery_read(&raw)); /* but it is not asked */
    CHECK_EQ(ADC_ICLR, 0);

    CASE("the back-off is 600000 ms from the last failure");
    setup();
    adc_dead();
    for (int i = 0; i < 3; i++) annepro2_battery_read(&raw);
    adc_value(1500);
    fake_advance(AP2_BATTERY_BACKOFF_MS - 1);
    ADC_ICLR = 0;
    CHECK(!annepro2_battery_read(&raw));
    CHECK_EQ(ADC_ICLR, 0);
    fake_advance(1);
    CHECK(annepro2_battery_read(&raw));
    CHECK_EQ(raw, 1500);

    CASE("two failures do not start it");
    setup();
    adc_dead();
    annepro2_battery_read(&raw);
    annepro2_battery_read(&raw);
    adc_value(1500);
    CHECK(annepro2_battery_read(&raw));

    CASE("a success clears the count");
    setup();
    adc_dead();
    annepro2_battery_read(&raw);
    annepro2_battery_read(&raw);
    adc_value(1500);
    CHECK(annepro2_battery_read(&raw));
    adc_dead();
    annepro2_battery_read(&raw);
    annepro2_battery_read(&raw);
    adc_value(1500);
    CHECK(annepro2_battery_read(&raw)); /* 2 + 2 failures, not 4 in a row */

    CASE("a failure after the back-off starts a new one");
    setup();
    adc_dead();
    for (int i = 0; i < 3; i++) annepro2_battery_read(&raw);
    fake_advance(AP2_BATTERY_BACKOFF_MS);
    CHECK(!annepro2_battery_read(&raw)); /* tried, failed */
    adc_value(1500);
    fake_advance(1000);
    ADC_ICLR = 0;
    CHECK(!annepro2_battery_read(&raw));
    CHECK_EQ(ADC_ICLR, 0);

    CASE("the task keeps away from a dead ADC");
    setup();
    adc_dead();
    for (int i = 0; i < 20; i++) {
        fake_advance(AP2_BATTERY_FILL_MS);
        annepro2_battery_task();
    }
    CHECK_EQ(history_count, 0);
    CHECK_EQ(SD1.tx_len, 0);
    adc_value(2600);
    ADC_ICLR = 0;
    fake_advance(AP2_BATTERY_FILL_MS);
    annepro2_battery_task(); /* 40 s after the last failure: still backed off */
    CHECK_EQ(ADC_ICLR, 0);
    CHECK_EQ(history_count, 0);
}

static void task_at(uint16_t raw, uint32_t advance_ms) {
    adc_value(raw);
    fake_advance(advance_ms);
    annepro2_battery_task();
}

static void expect_battery_frame(uint16_t level) {
    const uint8_t exp[] = {0x7b, 0x12, 0x53, 0x00, 0x04, 0x00, 0x00, 0x7d, 0x01, 0x05,
                           (uint8_t)(level & 0xff), (uint8_t)(level >> 8)};
    CHECK_EQ(SD1.tx_len, sizeof(exp));
    if (SD1.tx_len == sizeof(exp)) CHECK_BYTES(SD1.tx, exp, sizeof(exp));
    fake_sd_tx_clear(&SD1);
}

static void case_history(void) {
    /* The BLE MCU keeps the lowest level it was sent until it restarts, so the highest of the last AP2_BATTERY_HISTORY
     * (5) readings is sent; the readings are taken every AP2_BATTERY_FILL_MS (2000) until the history is full and
     * nothing is sent before (annepro2_battery.h). All readings here are above 3.6 V, where scaled() does not change
     * them. */
    CASE("nothing is sent before the history is full, then the highest reading");
    setup();
    static const uint16_t first[] = {2400, 2600, 2500, 2450, 2550};
    for (int i = 0; i < 5; i++) {
        task_at(first[i], i == 0 ? 0 : AP2_BATTERY_FILL_MS);
        CHECK_EQ(history_count, i + 1);
        if (i < 4) CHECK_EQ(SD1.tx_len, 0);
    }
    expect_battery_frame(2600);
    CHECK(!send_now);

    CASE("the first reading is taken at once, then every 2 s");
    setup();
    adc_value(2500);
    annepro2_battery_task();
    CHECK_EQ(history_count, 1);
    fake_advance(AP2_BATTERY_FILL_MS - 1);
    annepro2_battery_task();
    CHECK_EQ(history_count, 1);
    fake_advance(1);
    annepro2_battery_task();
    CHECK_EQ(history_count, 2);

    CASE("the level is held for 5 readings, then it follows a lasting drop");
    setup();
    for (int i = 0; i < 5; i++) task_at(i == 0 ? 2700 : 2600, i == 0 ? 0 : AP2_BATTERY_FILL_MS);
    expect_battery_frame(2700);
    /* from now on the interval is 60 s */
    adc_value(2400);
    fake_advance(AP2_BATTERY_INTERVAL_MS - 1);
    annepro2_battery_task();
    CHECK_EQ(SD1.tx_len, 0); /* no reading yet */
    fake_advance(1);
    annepro2_battery_task();
    expect_battery_frame(2600); /* the 2700 left the history, the dip to 2400 is not seen */
    for (int i = 0; i < 3; i++) {
        task_at(2400, AP2_BATTERY_INTERVAL_MS);
        expect_battery_frame(2600); /* the 2600 readings are still inside */
    }
    task_at(2400, AP2_BATTERY_INTERVAL_MS);
    expect_battery_frame(2400); /* the 5th low reading: now the level drops */

    CASE("one frame per reading");
    setup();
    for (int i = 0; i < 5; i++) task_at(2500, i == 0 ? 0 : AP2_BATTERY_FILL_MS);
    fake_sd_tx_clear(&SD1);
    for (int i = 0; i < 3; i++) annepro2_battery_task();
    CHECK_EQ(SD1.tx_len, 0);

    CASE("a failed reading adds nothing and sends nothing");
    setup();
    for (int i = 0; i < 5; i++) task_at(2500, i == 0 ? 0 : AP2_BATTERY_FILL_MS);
    fake_sd_tx_clear(&SD1);
    adc_dead();
    fake_advance(AP2_BATTERY_INTERVAL_MS);
    annepro2_battery_task();
    CHECK_EQ(SD1.tx_len, 0);
    CHECK_EQ(history_count, 5);

    CASE("a reading with the low end correction goes into the history corrected");
    setup();
    uint32_t raw3300 = 0;
    while (mv_of(raw3300) < 3300) raw3300++;
    for (int i = 0; i < 5; i++) task_at((uint16_t)raw3300, i == 0 ? 0 : AP2_BATTERY_FILL_MS);
    CHECK_EQ(SD1.tx_len, 12);
    CHECK_EQ(SD1.tx[10] | (SD1.tx[11] << 8), annepro2_battery_scaled((uint16_t)raw3300));
    CHECK(annepro2_battery_percent(annepro2_battery_scaled((uint16_t)raw3300)) >= 10);

    CASE("nothing happens until the BLE MCU is ready");
    setup();
    ble_ready = false;
    adc_value(2500);
    for (int i = 0; i < 10; i++) {
        fake_advance(AP2_BATTERY_FILL_MS);
        annepro2_battery_task();
    }
    CHECK_EQ(history_count, 0);
    CHECK_EQ(SD1.tx_len, 0);

    CASE("a connection sends the current level again, without a new reading");
    setup();
    for (int i = 0; i < 5; i++) task_at(2500, i == 0 ? 0 : AP2_BATTERY_FILL_MS);
    fake_sd_tx_clear(&SD1);
    annepro2_battery_request_send();
    ADC_ICLR = 0;
    annepro2_battery_task();
    expect_battery_frame(2500);
    CHECK_EQ(ADC_ICLR, 0); /* no conversion */
    annepro2_battery_task();
    CHECK_EQ(SD1.tx_len, 0);

    CASE("a connection before the history is full waits for it");
    setup();
    task_at(2500, 0);
    annepro2_battery_request_send();
    annepro2_battery_task();
    CHECK_EQ(SD1.tx_len, 0);
    for (int i = 1; i < 5; i++) task_at(2500, AP2_BATTERY_FILL_MS);
    expect_battery_frame(2500);
}

static uint32_t raw_for_percent(int percent) {
    for (uint32_t raw = 0; raw < 4096; raw++) {
        if (annepro2_battery_percent(annepro2_battery_scaled((uint16_t)raw)) == percent) return raw;
    }
    return 0;
}

/* The indicator is written as the LED UART has room (up to the reserve), so the last keys follow in later scans: let
 * the UART send everything and scan until the writer is done. */
static void drain_display(void) {
    for (int i = 0; i < 20; i++) {
        fake_sd_drain(&SD0, 1000);
        annepro2_battery_task();
    }
}

/* The keys 1..0 of the number row are row 0, columns 1..10 (annepro2_battery.c display_task()). */
static void expect_display(int lit, uint8_t r, uint8_t g, uint8_t b) {
    unsigned errors;
    const size_t n = decode_tx(&SD0, &errors);
    CHECK_EQ(errors, 0);
    CHECK_EQ(n, 10);
    for (size_t i = 0; i < n && i < 10; i++) {
        CHECK_EQ(dec_msgs[i].cmd, CMD_LED_STICKY_SET_KEY);
        CHECK_EQ(dec_msgs[i].size, 6);
        CHECK_EQ(dec_msgs[i].payload[0], 0);
        CHECK_EQ(dec_msgs[i].payload[1], 1 + i);
        const bool on = (int)i < lit;
        CHECK_EQ(dec_msgs[i].payload[2], on ? b : 0x10); /* blue, green, red, alpha */
        CHECK_EQ(dec_msgs[i].payload[3], on ? g : 0x10);
        CHECK_EQ(dec_msgs[i].payload[4], on ? r : 0x10);
        CHECK_EQ(dec_msgs[i].payload[5], 0xff);
    }
    fake_sd_tx_clear(&SD0);
}

static void case_display(void) {
    /* KC_AP2_BATT (readme.md): one key per 10 %, green from 50 %, yellow from 20 %, red below; pressing it again hides
     * the indicator; it hides itself after AP2_BATTERY_SHOW_MS (3000). Before the first level is sent it shows the
     * highest reading so far, and a fresh reading while there is none. */
    static const struct {
        int     percent, lit;
        uint8_t r, g, b;
    } cases[] = {
        {0, 0, 0xff, 0x00, 0}, {10, 1, 0xff, 0x00, 0}, {19, 2, 0xff, 0x00, 0}, {20, 2, 0xff, 0xff, 0},
        {49, 5, 0xff, 0xff, 0},  {50, 5, 0x00, 0xff, 0}, {95, 10, 0x00, 0xff, 0}, {100, 10, 0x00, 0xff, 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CASE("indicator from a fresh reading");
        setup();
        const uint32_t raw = raw_for_percent(cases[i].percent);
        adc_value((uint16_t)raw);
        annepro2_battery_toggle_display();
        drain_display();
        expect_display(cases[i].lit, cases[i].r, cases[i].g, cases[i].b);
        CHECK(shown);
        CHECK_EQ(SD0.overflows, 0);
    }

    CASE("no reading and no history: nothing is shown");
    setup();
    adc_dead();
    annepro2_battery_toggle_display();
    CHECK(!shown);
    CHECK_EQ(SD0.tx_len, 0);

    CASE("the history is used when there is one, not a new reading");
    setup();
    history_add((uint16_t)raw_for_percent(80)); /* a level from the history ... */
    adc_value((uint16_t)raw_for_percent(30));   /* ... a different one now at the ADC */
    annepro2_battery_toggle_display();
    drain_display();
    expect_display(8, 0x00, 0xff, 0x00);

    CASE("the second press hides it, the keys are unset one by one");
    setup();
    adc_value((uint16_t)raw_for_percent(60));
    annepro2_battery_toggle_display();
    drain_display();
    fake_sd_tx_clear(&SD0);
    annepro2_battery_toggle_display();
    drain_display();
    CHECK(!shown);
    unsigned errors;
    size_t   n = decode_tx(&SD0, &errors);
    CHECK_EQ(n, 10);
    for (size_t i = 0; i < n && i < 10; i++) {
        CHECK_EQ(dec_msgs[i].cmd, CMD_LED_STICKY_UNSET_KEY);
        CHECK_EQ(dec_msgs[i].size, 2);
        CHECK_EQ(dec_msgs[i].payload[0], 0);
        CHECK_EQ(dec_msgs[i].payload[1], 1 + i);
    }

    CASE("the indicator hides itself after 3 s");
    setup();
    adc_value((uint16_t)raw_for_percent(60));
    annepro2_battery_toggle_display();
    drain_display();
    fake_sd_tx_clear(&SD0);
    fake_advance(AP2_BATTERY_SHOW_MS - 1);
    annepro2_battery_task();
    CHECK(shown);
    CHECK_EQ(SD0.tx_len, 0);
    fake_advance(1);
    annepro2_battery_task();
    CHECK(!shown);
    drain_display();
    CHECK_EQ(decode_tx(&SD0, NULL), 10);
    CHECK_EQ(dec_msgs[0].cmd, CMD_LED_STICKY_UNSET_KEY);

    CASE("with little room the keys follow, one per scan, and the reserve stays free");
    setup();
    adc_value((uint16_t)raw_for_percent(70));
    fake_sd_set_capacity(&SD0, DISPLAY_SET_SIZE + AP2_LED_TX_RESERVE); /* room for one key and the reserve */
    annepro2_battery_toggle_display();
    CHECK_EQ(SD0.tx_len, DISPLAY_SET_SIZE);
    for (int i = 2; i <= 10; i++) {
        annepro2_battery_task(); /* no room: nothing more */
        CHECK_EQ(SD0.tx_len, (size_t)(i - 1) * DISPLAY_SET_SIZE);
        fake_sd_drain(&SD0, DISPLAY_SET_SIZE); /* the UART sends */
        annepro2_battery_task();
        CHECK_EQ(SD0.tx_len, (size_t)i * DISPLAY_SET_SIZE);
    }
    CHECK_EQ(SD0.overflows, 0);
    CHECK_EQ(decode_tx(&SD0, NULL), 10);
    for (size_t i = 0; i < 10; i++) CHECK_EQ(dec_msgs[i].payload[1], 1 + i);

    CASE("hiding with little room: the unset commands are shorter and follow in turn too");
    setup();
    adc_value((uint16_t)raw_for_percent(70));
    annepro2_battery_toggle_display();
    drain_display();
    fake_sd_tx_clear(&SD0);
    fake_sd_set_capacity(&SD0, DISPLAY_UNSET_SIZE + AP2_LED_TX_RESERVE);
    annepro2_battery_toggle_display();
    CHECK_EQ(SD0.tx_len, DISPLAY_UNSET_SIZE);
    CHECK(display_todo != 0);
    for (int i = 0; i < 20; i++) {
        fake_sd_drain(&SD0, DISPLAY_UNSET_SIZE);
        annepro2_battery_task();
    }
    CHECK_EQ(display_todo, 0);
    CHECK_EQ(decode_tx(&SD0, NULL), 10);
    CHECK_EQ(SD0.overflows, 0);
}

int main(void) {
    case_percent();
    case_scaled();
    case_read();
    case_backoff();
    case_history();
    case_display();
    return TEST_RESULT();
}
