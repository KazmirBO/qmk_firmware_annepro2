#include "annepro2_battery.h"
#include "hal.h"

/* ADC registers (base 0x40010000), offsets as used by the stock firmware. */
#define AP2_ADC_REG(off) (*(volatile uint32_t *)(0x40010000u + (off)))
#define AP2_ADC_CR   AP2_ADC_REG(0x000) /* bit 7 enables the converter */
#define AP2_ADC_LST0 AP2_ADC_REG(0x004) /* conversion list, 5 bits per entry */
#define AP2_ADC_DR0  AP2_ADC_REG(0x030) /* result of the first list entry, 12 bits */
#define AP2_ADC_TCR  AP2_ADC_REG(0x070)
#define AP2_ADC_TSR  AP2_ADC_REG(0x074) /* bit 0 starts a conversion by software */
#define AP2_ADC_IER  AP2_ADC_REG(0x080)
#define AP2_ADC_ISR  AP2_ADC_REG(0x088) /* bit 0: end of conversion */
#define AP2_ADC_ICLR AP2_ADC_REG(0x08C)

#define AP2_ADC_CHANNEL   7
#define AP2_ADC_SAMPLES   8
#define AP2_ADC_WAIT_LOOP 20000

void annepro2_battery_init(void) {
    CKCU->APBCFGR = (CKCU->APBCFGR & ~CKCU_APBCFGR_ADCDIV_MASK) | (6U << 16); /* HCLK / 64 */
    CKCU->APBCCR1 |= CKCU_APBCCR1_ADCEN;
    palSetPadMode(IOPORTA, 7, PAL_MODE_INPUT_ANALOG); /* PA7, ADC function */

    AP2_ADC_CR   = AP2_ADC_CR & 0x80; /* one-shot mode, list of one entry */
    AP2_ADC_LST0 = (AP2_ADC_LST0 & ~0x1Fu) | AP2_ADC_CHANNEL;
    AP2_ADC_TCR  = 1;
    AP2_ADC_TSR  = 0x10;
    AP2_ADC_IER |= 1; /* the status flag is polled, the interrupt is not enabled in the NVIC */
    AP2_ADC_CR |= 0x80;
}

static bool battery_convert(uint16_t *value) {
    AP2_ADC_ICLR = 1;
    AP2_ADC_TSR |= 1;
    for (uint32_t i = 0; i < AP2_ADC_WAIT_LOOP; i++) {
        if (AP2_ADC_ISR & 1) {
            *value       = AP2_ADC_DR0 & 0x0FFF;
            AP2_ADC_ICLR = 1;
            return true;
        }
    }
    return false;
}

bool annepro2_battery_read(uint16_t *raw) {
    uint32_t sum = 0;
    uint16_t min = 0xFFFF, max = 0;
    for (uint8_t i = 0; i < AP2_ADC_SAMPLES; i++) {
        uint16_t v;
        if (!battery_convert(&v)) return false;
        sum += v;
        if (v < min) min = v;
        if (v > max) max = v;
    }
    *raw = (uint16_t)((sum - min - max) / (AP2_ADC_SAMPLES - 2));
    return true;
}

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
