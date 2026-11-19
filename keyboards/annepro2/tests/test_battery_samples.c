/* Trimmed mean of annepro2_battery.c (read_trimmed_mean) with samples that differ; the only firmware seam of the suite
 * is AP2_ADC_REG. The reading is the mean of 8 conversions without the lowest and the highest one (docs/07, the battery
 * row: "srednia z 8 probek bez skrajnych"). The ADC result register hands out one sample of an array per read, so the
 * order of the samples can be chosen; a conversion that never ends (the end flag stays 0) makes the reading fail. */
static unsigned           adc_samples[8];
static unsigned           adc_next;
static volatile uint32_t  adc_cell;
static unsigned           adc_fail_at; /* > 0: the conversion after adc_fail_at samples never ends */
static volatile uint32_t *adc_reg(unsigned off) {
    if (off == 0x088 && adc_fail_at && adc_next >= adc_fail_at) {
        adc_cell = 0; /* the end-of-conversion flag is not set */
        return &adc_cell;
    }
    if (off == 0x030) {
        adc_cell = adc_samples[adc_next++ & 7];
        return &adc_cell;
    }
    return (volatile uint32_t *)(uintptr_t)(0x40010000u + off);
}
#define AP2_ADC_REG(off) (*adc_reg(off))
#include "all_sources.h"

static void setup(void) {
    reset_firmware();
    annepro2_battery_init();
    adc_next            = 0;
    adc_fail_at         = 0;
    FAKE_ADC_REG(0x088) = 1; /* the conversion ends at once */
}

int main(void) {
    CASE("the reading is the mean of 8 samples without the lowest and the highest");
    setup();
    /* two outliers, the rest 2590..2610 */
    const unsigned low_first[8] = {2000, 2610, 2590, 2600, 2605, 2595, 3500, 2600};
    memcpy(adc_samples, low_first, sizeof(low_first));
    uint16_t raw = 0;
    CHECK(annepro2_battery_read(&raw));
    CHECK_EQ(raw, (2610 + 2590 + 2600 + 2605 + 2595 + 2600) / 6);
    CHECK_EQ(adc_next, 8);

    /* The same samples in another order: a trimming that treats the first sample as the start value of both the
     * minimum and the maximum (an else-if) drops it only when it is the lowest. */
    CASE("the same, with the highest sample first and the lowest next to last");
    setup();
    const unsigned high_first[8] = {3500, 2610, 2590, 2600, 2605, 2595, 2000, 2600};
    memcpy(adc_samples, high_first, sizeof(high_first));
    raw = 0;
    CHECK(annepro2_battery_read(&raw));
    CHECK_EQ(raw, 2600);

    CASE("equal samples give that value");
    setup();
    for (int i = 0; i < 8; i++) adc_samples[i] = 2500;
    CHECK(annepro2_battery_read(&raw));
    CHECK_EQ(raw, 2500);

    /* annepro2_battery.c: a conversion that does not end in time fails the whole reading and *raw is left alone. */
    CASE("a conversion that does not end: the reading fails, the value is kept, no more samples are taken");
    setup();
    memcpy(adc_samples, high_first, sizeof(high_first));
    adc_fail_at = 4;
    raw         = 0xBEEF;
    CHECK(!annepro2_battery_read(&raw));
    CHECK_EQ(raw, 0xBEEF);
    CHECK_EQ(adc_next, 4);
    return TEST_RESULT();
}
