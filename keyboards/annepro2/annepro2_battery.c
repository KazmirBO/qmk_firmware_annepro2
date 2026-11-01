#include "annepro2_battery.h"

#ifdef AP2_BLE_BATTERY

#    include "quantum.h"
#    include "util.h"
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

_Static_assert(AP2_BATTERY_HISTORY >= 1 && AP2_BATTERY_HISTORY <= 255, "AP2_BATTERY_HISTORY must be 1-255");

static bool     send_now;
static uint32_t sample_timer;

/* Calibrated readings; the slots not used yet are 0, so they never win the maximum. */
static uint16_t history[AP2_BATTERY_HISTORY];
static uint8_t  history_count, history_pos;

static bool     shown;
static uint32_t shown_timer;

/* The indicator is written one key at a time, when the LED UART has room, so that it never blocks the matrix scan
 * (ten keys at once are 110 bytes). */
#    define DISPLAY_KEYS 10
#    define DISPLAY_SET_SIZE (5 + 6)   /* protocol header + row, column, colour */
#    define DISPLAY_UNSET_SIZE (5 + 2) /* protocol header + row, column */
static uint16_t  display_todo; /* bit i: key 1 + i of the number row still to write */
static bool      display_set;  /* write the level, or clear the keys */
static uint8_t   display_lit;
static ap2_led_t display_color;

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

/* Voltage as the BLE MCU computes it from the raw reading: U = raw * 2 * 3.1 / 4095, in millivolts. A step of the curve
 * starts at a whole millivolt value, so this gives the same step as the BLE MCU's floating point. */
#    define AP2_BATTERY_MV(raw) ((uint32_t)(raw) * 6200u / 4095u)

/* The BLE MCU reports its first step (10 %) from 3.4 V and 0 % below it; the correction is applied up to this voltage. */
#    define AP2_BATTERY_BLE_FIRST_STEP_MV 3400u
#    define AP2_BATTERY_CORRECTION_END_MV 3600u

/* The curve of the BLE MCU, so that the indicator shows the level the host shows. The BLE MCU interpolates linearly
 * between its steps: percent = base + (U - start) / width * 10, cut to an integer. It computes in floating point, so a
 * reading that falls exactly on a whole percent can come out 1 % lower there. */
static uint8_t percent_from_mv(uint32_t mv) {
    static const struct {
        uint16_t start; /* mV */
        uint8_t  width; /* mV per 10 %, 0 for the top step */
        uint8_t  base;  /* percent at start */
    } curve[] = {{4150, 0, 100}, {4000, 150, 90}, {3900, 100, 80}, {3800, 100, 70}, {3750, 50, 60},
                 {3700, 50, 50}, {3650, 50, 40},  {3600, 50, 30},  {3500, 100, 20}, {3400, 100, 10}};
    for (uint8_t i = 0; i < sizeof(curve) / sizeof(curve[0]); i++) {
        if (mv < curve[i].start) continue;
        if (curve[i].width == 0) return curve[i].base;
        return (uint8_t)(curve[i].base + (mv - curve[i].start) * 10 / curve[i].width);
    }
    return 0;
}

#    if AP2_BATTERY_EMPTY_MV < AP2_BATTERY_BLE_FIRST_STEP_MV
/* The raw reading for a voltage. One count is about 1.5 mV, so not every voltage has an exact reading: take the rounded
 * down one, and the next one when that gives exactly mv or when the rounded down one falls below the percent of mv.
 * The reading then has the percent of mv: the next count is at most 1 mV above mv, and the percent changes at most
 * every 5 mV. */
static uint16_t raw_from_mv(uint32_t mv) {
    uint32_t raw = mv * 4095u / 6200u;
    if (AP2_BATTERY_MV(raw + 1) == mv || percent_from_mv(AP2_BATTERY_MV(raw)) < percent_from_mv(mv)) raw++;
    return raw;
}
#    endif

uint16_t annepro2_battery_scaled(uint16_t raw) {
    /* the BLE MCU expects a 12-bit reading */
    const uint32_t scaled = MIN((uint32_t)raw * AP2_BATTERY_SCALE_NUM / AP2_BATTERY_SCALE_DEN, AP2_ADC_RESULT_MASK);
#    if AP2_BATTERY_EMPTY_MV < AP2_BATTERY_BLE_FIRST_STEP_MV
    uint32_t mv = AP2_BATTERY_MV(scaled);
    if (mv < AP2_BATTERY_CORRECTION_END_MV) {
        if (mv >= AP2_BATTERY_EMPTY_MV) {
            /* EMPTY_MV..3.6 V becomes 3.4..3.6 V */
            mv = AP2_BATTERY_BLE_FIRST_STEP_MV + (mv - AP2_BATTERY_EMPTY_MV) * (AP2_BATTERY_CORRECTION_END_MV - AP2_BATTERY_BLE_FIRST_STEP_MV) / (AP2_BATTERY_CORRECTION_END_MV - AP2_BATTERY_EMPTY_MV);
        } else {
            /* below EMPTY_MV: just under the first step, so 0 % */
            mv = mv * (AP2_BATTERY_BLE_FIRST_STEP_MV - 1) / AP2_BATTERY_EMPTY_MV;
        }
        return raw_from_mv(mv);
    }
#    endif
    return (uint16_t)scaled; /* above the correction the reading is not converted, so it does not lose precision */
}

uint8_t annepro2_battery_percent(uint16_t raw) { return percent_from_mv(AP2_BATTERY_MV(raw)); }

/* Store a calibrated reading. */
static void history_add(uint16_t value) {
    history[history_pos] = value;
    history_pos          = (history_pos + 1) % AP2_BATTERY_HISTORY;
    if (history_count < AP2_BATTERY_HISTORY) history_count++;
}

/* The level sent to the BLE MCU: the highest stored reading. */
static uint16_t history_max(void) {
    uint16_t max = 0;
    for (uint8_t i = 0; i < AP2_BATTERY_HISTORY; i++) max = MAX(max, history[i]);
    return max;
}

/* The indicator uses sticky keys, so it also works with the LEDs off. */
static void display_task(void) {
    static const ap2_led_t off = {.p.blue = 0x10, .p.green = 0x10, .p.red = 0x10, .p.alpha = 0xff};
    while (display_todo) {
        if (ap2_led_tx_space() < (display_set ? DISPLAY_SET_SIZE : DISPLAY_UNSET_SIZE)) return; /* next scan */
        const uint8_t i = __builtin_ctz(display_todo);
        if (display_set) {
            ap2_led_sticky_set_key(0, 1 + i, i < display_lit ? display_color : off);
        } else {
            ap2_led_unset_sticky_key(0, 1 + i);
        }
        display_todo &= ~(1u << i);
    }
}

static void hide_display(void) {
    display_set  = false;
    display_todo = (1u << DISPLAY_KEYS) - 1;
    shown        = false;
    display_task();
}

static void show_display(void) {
    /* The filtered level sent to the host. Until the history is full and the first level went out (some 8 s after the
     * BLE MCU is ready) it is the highest reading so far, and a fresh reading while there is none. */
    uint16_t level = history_max();
    if (history_count == 0) {
        uint16_t raw;
        if (!annepro2_battery_read(&raw)) return;
        level = annepro2_battery_scaled(raw);
    }
    const uint8_t percent = annepro2_battery_percent(level);
    display_lit           = (percent + 5) / 10; /* keys 1..0, one key per 10 % */
    display_color         = (ap2_led_t){.p.blue = 0x00, .p.green = 0xff, .p.red = 0x00, .p.alpha = 0xff};
    if (percent < 20) {
        display_color.p.green = 0x00;
        display_color.p.red   = 0xff;
    } else if (percent < 50) {
        display_color.p.red = 0xff;
    }
    display_set  = true;
    display_todo = (1u << DISPLAY_KEYS) - 1;
    shown        = true;
    shown_timer  = timer_read32();
    display_task();
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
    display_task();

    if (!annepro2_ble_is_ready()) return;
    const uint32_t interval = history_count < AP2_BATTERY_HISTORY ? AP2_BATTERY_FILL_MS : AP2_BATTERY_INTERVAL_MS;
    if (timer_elapsed32(sample_timer) >= interval) {
        sample_timer = timer_read32();
        uint16_t raw;
        if (annepro2_battery_read(&raw)) {
            history_add(annepro2_battery_scaled(raw));
            send_now = true;
        }
    }
    /* Nothing goes out before the history is full: the BLE MCU would keep a low first level. */
    if (send_now && history_count == AP2_BATTERY_HISTORY) {
        send_now = false;
        annepro2_ble_send_battery(history_max());
    }
}

#else

/* ISO C does not allow an empty translation unit. */
typedef int annepro2_battery_disabled_t;

#endif /* AP2_BLE_BATTERY */
