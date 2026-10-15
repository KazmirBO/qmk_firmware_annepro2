#include "annepro2_battery.h"

#ifdef AP2_BLE_BATTERY

#    include "quantum.h"
#    include "hal.h"
#    include "ap2_led.h"
#    include "annepro2_ble.h"

/* ADC registers (base 0x40010000), offsets as used by the stock firmware. */
#    define AP2_ADC_REG(off) (*(volatile uint32_t *)(0x40010000u + (off)))
#    define AP2_ADC_CR AP2_ADC_REG(0x000)   /* control */
#    define AP2_ADC_LST0 AP2_ADC_REG(0x004) /* conversion list, 5 bits per entry */
#    define AP2_ADC_DR0 AP2_ADC_REG(0x030)  /* result of the first list entry, 12 bits */
#    define AP2_ADC_TCR AP2_ADC_REG(0x070)
#    define AP2_ADC_TSR AP2_ADC_REG(0x074)  /* trigger source */
#    define AP2_ADC_IER AP2_ADC_REG(0x080)  /* interrupt enable */
#    define AP2_ADC_ISR AP2_ADC_REG(0x088)  /* interrupt status */
#    define AP2_ADC_ICLR AP2_ADC_REG(0x08C) /* interrupt clear */

#    define AP2_ADC_CR_ENABLE (1u << 7)
#    define AP2_ADC_TSR_SOFTWARE (1u << 0)  /* start a conversion */
#    define AP2_ADC_TSR_STOCK 0x10u         /* value written by the stock firmware, trigger control = 1 */
#    define AP2_ADC_TCR_STOCK 1u
#    define AP2_ADC_INT_EOC (1u << 0)       /* end of the single conversion */
#    define AP2_ADC_RESULT_MASK 0x0FFFu
#    define AP2_ADC_LST_ENTRY_MASK 0x1Fu
#    define AP2_ADC_CKCU_DIV_64 6u          /* ADC clock = HCLK / 64 */
#    define AP2_ADC_CKCU_DIV_SHIFT 16

#    define AP2_ADC_CHANNEL 7
#    define AP2_ADC_SAMPLES 8
#    define AP2_ADC_WAIT_LOOP 8000 /* about 1 ms at 48 MHz, a conversion takes tens of microseconds */

/* After this many failed readings in a row the measurement is skipped for AP2_BATTERY_BACKOFF_MS, so that a dead ADC
 * does not stall the matrix scan every time. */
#    define AP2_BATTERY_MAX_FAILS 3
#    define AP2_BATTERY_BACKOFF_MS 600000

static uint8_t  fail_count;
static uint32_t fail_timer;

static bool     send_now = true;
static uint32_t send_timer;

static bool     shown;
static uint32_t shown_timer;

void annepro2_battery_init(void) {
    CKCU->APBCFGR = (CKCU->APBCFGR & ~CKCU_APBCFGR_ADCDIV_MASK) | (AP2_ADC_CKCU_DIV_64 << AP2_ADC_CKCU_DIV_SHIFT);
    CKCU->APBCCR1 |= CKCU_APBCCR1_ADCEN;
    palSetPadMode(IOPORTA, 7, PAL_MODE_INPUT_ANALOG); /* PA7, ADC function */

    AP2_ADC_CR   = AP2_ADC_CR & AP2_ADC_CR_ENABLE; /* one-shot mode, list of one entry */
    AP2_ADC_LST0 = (AP2_ADC_LST0 & ~AP2_ADC_LST_ENTRY_MASK) | AP2_ADC_CHANNEL;
    AP2_ADC_TCR  = AP2_ADC_TCR_STOCK;
    AP2_ADC_TSR  = AP2_ADC_TSR_STOCK;
    AP2_ADC_IER |= AP2_ADC_INT_EOC; /* the status flag is polled, the interrupt is not enabled in the NVIC */
    AP2_ADC_CR |= AP2_ADC_CR_ENABLE;
}

static bool convert(uint16_t *value) {
    AP2_ADC_ICLR = AP2_ADC_INT_EOC;
    AP2_ADC_TSR |= AP2_ADC_TSR_SOFTWARE;
    for (uint32_t i = 0; i < AP2_ADC_WAIT_LOOP; i++) {
        if (AP2_ADC_ISR & AP2_ADC_INT_EOC) {
            *value       = AP2_ADC_DR0 & AP2_ADC_RESULT_MASK;
            AP2_ADC_ICLR = AP2_ADC_INT_EOC;
            return true;
        }
    }
    return false;
}

static bool read_trimmed_mean(uint16_t *raw) {
    uint32_t sum = 0;
    uint16_t min = 0xFFFF, max = 0;
    for (uint8_t i = 0; i < AP2_ADC_SAMPLES; i++) {
        uint16_t v;
        if (!convert(&v)) return false;
        sum += v;
        if (v < min) min = v;
        if (v > max) max = v;
    }
    *raw = (uint16_t)((sum - min - max) / (AP2_ADC_SAMPLES - 2));
    return true;
}

bool annepro2_battery_read(uint16_t *raw) {
    if (fail_count >= AP2_BATTERY_MAX_FAILS && timer_elapsed32(fail_timer) < AP2_BATTERY_BACKOFF_MS) return false;
    if (read_trimmed_mean(raw)) {
        fail_count = 0;
        return true;
    }
    if (fail_count < 0xFF) fail_count++;
    fail_timer = timer_read32();
    return false;
}

uint16_t annepro2_battery_scaled(uint16_t raw) { return (uint16_t)((uint32_t)raw * AP2_BATTERY_SCALE_NUM / AP2_BATTERY_SCALE_DEN); }

uint8_t annepro2_battery_percent(uint16_t raw) {
    /* Stock formula: U = raw * 3.1 * 2.02 / 4094, here in millivolts. */
    const uint32_t mv = (uint32_t)raw * 6262u / 4094u;
    static const struct {
        uint16_t mv;
        uint8_t  percent;
    } curve[] = {{4100, 100}, {4000, 90}, {3900, 80}, {3800, 70}, {3750, 60}, {3700, 50}, {3650, 40}, {3600, 30}, {3500, 20}, {3400, 10}};
    for (uint8_t i = 0; i < sizeof(curve) / sizeof(curve[0]); i++) {
        if (mv >= curve[i].mv) return curve[i].percent;
    }
    return 0;
}

/* The indicator uses sticky keys, so it also works with the LEDs off. */
static void hide_display(void) {
    for (uint8_t col = 1; col <= 10; col++) ap2_led_unset_sticky_key(0, col);
    shown = false;
}

static void show_display(void) {
    uint16_t raw;
    if (!annepro2_battery_read(&raw)) return;
    const uint8_t percent = annepro2_battery_percent(annepro2_battery_scaled(raw));
    const uint8_t lit     = (percent + 5) / 10; /* keys 1..0, one key per 10 % */
    ap2_led_t     color   = {.p.blue = 0x00, .p.green = 0xff, .p.red = 0x00, .p.alpha = 0xff};
    if (percent < 20) {
        color.p.green = 0x00;
        color.p.red   = 0xff;
    } else if (percent < 50) {
        color.p.red = 0xff;
    }
    const ap2_led_t off = {.p.blue = 0x10, .p.green = 0x10, .p.red = 0x10, .p.alpha = 0xff};
    for (uint8_t i = 0; i < 10; i++) ap2_led_sticky_set_key(0, 1 + i, i < lit ? color : off);
    shown       = true;
    shown_timer = timer_read32();
}

void annepro2_battery_toggle_display(void) {
    if (shown) {
        hide_display();
    } else {
        show_display();
    }
}

void annepro2_battery_request_send(void) { send_now = true; }

void annepro2_battery_task(void) {
    if (shown && timer_elapsed32(shown_timer) >= AP2_BATTERY_SHOW_MS) hide_display();

    if (!annepro2_ble_is_ready()) return;
    if (!send_now && timer_elapsed32(send_timer) < AP2_BATTERY_INTERVAL_MS) return;
    send_now   = false;
    send_timer = timer_read32();
    uint16_t raw;
    if (annepro2_battery_read(&raw)) annepro2_ble_send_battery(annepro2_battery_scaled(raw));
}

#else

/* ISO C does not allow an empty translation unit. */
typedef int annepro2_battery_disabled_t;

#endif /* AP2_BLE_BATTERY */
