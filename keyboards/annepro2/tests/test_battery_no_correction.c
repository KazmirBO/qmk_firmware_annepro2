/* The battery conversion with the low end correction turned off: AP2_BATTERY_EMPTY_MV of 3400 or more turns it off
 * (annepro2_battery.h: "Set it to 3400 or more to turn the correction off"). The reading is then passed on unchanged
 * and the percent follows the curve of the BLE MCU down to 3.4 V, below which it is 0. */
#define AP2_BATTERY_EMPTY_MV 3400
#include "all_sources.h"

int main(void) {
    reset_firmware();

    CASE("scaled is the identity, cut at 12 bits");
    int changed = 0;
    for (uint32_t raw = 0; raw < 4096; raw++) {
        if (annepro2_battery_scaled((uint16_t)raw) != raw) changed++;
    }
    CHECK_EQ(changed, 0);
    CHECK_EQ(annepro2_battery_scaled(0xffff), 4095);

    CASE("0 % below 3.4 V, 10 % from it");
    uint32_t raw = 0;
    while (raw * 6200u / 4095u < 3400) raw++;
    CHECK_EQ(annepro2_battery_percent(annepro2_battery_scaled((uint16_t)(raw - 1))), 0);
    CHECK_EQ(annepro2_battery_percent(annepro2_battery_scaled((uint16_t)raw)), 10);

    CASE("the low level reaches the BLE MCU as it is");
    ble_ready = true;
    FAKE_ADC_REG(0x088) = 1;
    FAKE_ADC_REG(0x030) = 2000; /* 3.03 V */
    for (int i = 0; i < 5; i++) {
        fake_advance(AP2_BATTERY_FILL_MS);
        annepro2_battery_task();
    }
    CHECK_EQ(SD1.tx_len, 12);
    CHECK_EQ(SD1.tx[10] | (SD1.tx[11] << 8), 2000);
    return TEST_RESULT();
}
