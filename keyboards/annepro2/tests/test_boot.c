/* Start-up of the keyboard: keyboard_pre_init_kb() (LED MCU wake-up, UART restart), keyboard_post_init_kb() (BLE UART,
 * wake-up frame, saved effect, LED power, the Bluetooth switch pin, the ADC, the keymap's hook) and bootloader_jump()
 * (annepro2.c, the comments on those functions; readme.md "QK_BOOT ... puts all three MCUs into IAP"). The expectations
 * are the bytes written to SD0 and SD1, the QMK calls counted by the fakes and the word left in RAM for the
 * bootloader. */
#include "all_sources.h"
#include "proto_decode.h"

static void setup(void) {
    reset_firmware();
    fake_pin_c10 = true; /* the Bluetooth switch is off (USB) */
    ble_ready    = true; /* the BLE MCU has answered: no handshake pings in the scans */
}

static void scan(void) { /* one scan; the output queues drain between scans */
    fake_sd_tx_clear(&SD0);
    matrix_scan_kb();
}

static size_t count_cmd(uint8_t cmd) {
    const size_t n = decode_tx(&SD0, NULL);
    size_t       k = 0;
    for (size_t i = 0; i < n; i++) k += dec_msgs[i].cmd == cmd;
    return k;
}

static void case_init(void) {
    CASE("keyboard_pre_init_kb: LED MCU wake-up bytes, UART restart at the run speed, replies drained, 30 ms");
    setup();
    static const uint8_t reply[] = {0x7a, 0x1d, 0x41, 1, 1, 0};
    fake_sd_rx_push(&SD0, reply, sizeof(reply));
    keyboard_pre_init_kb();
    static const uint8_t wake[] = {0x7b, 0x10, 0x43, 0x10, 0x03, 0x00, 0x00, 0x7d, 0x02, 0x01, 0x02};
    CHECK_EQ(SD0.tx_len, sizeof(wake));
    CHECK_BYTES(SD0.tx, wake, sizeof(wake));
    CHECK_EQ(SD0.starts, 2);
    CHECK_EQ(SD0.speed, LED_UART_BAUD_RATE);
    CHECK_EQ(fake_sd_rx_pending(&SD0), 0);
    CHECK_EQ(fake_calls.wait_ms_total, 30);
    CHECK(chibios_driver.send_keyboard != send_keyboard); /* the USB senders are wrapped */
    /* the receiver now acts on status replies */
    fake_rgb_enabled = true;
    led_sync(true);
    fake_advance(200);
    fake_sd_tx_clear(&SD0);
    const uint8_t st[] = {0x7a, 0x1d, CMD_LED_STATUS, 7, 6, 3, 1, 0, 0, 2, 0};
    fake_sd_rx_push(&SD0, st, sizeof(st));
    matrix_scan_kb();
    CHECK_EQ(count_cmd(CMD_LED_ON), 1);

    CASE("keyboard_post_init_kb: BLE UART, wake-up frame, the reply is parsed, 100 ms");
    setup();
    static const uint8_t ver[] = {0x7b, 0x12, 0x35, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x01, 0x03, 0x25};
    fake_sd_rx_push(&SD1, ver, sizeof(ver));
    keyboard_post_init_kb();
    CHECK_EQ(SD1.starts, 1);
    CHECK_BYTES(SD1.tx, ((const uint8_t[]){0x7b, 0x10, 0x53, 0x10, 0x03, 0x00, 0x00, 0x7d, 0x02, 0x01, 0x02}), 11);
    CHECK(annepro2_ble_is_ready());
    CHECK_EQ(annepro2_ble_version(), 0x0205);
    CHECK_EQ(fake_calls.wait_ms_total, 100);

    CASE("keyboard_post_init_kb: no reply: the wake-up counts as the first ping, the next follows after 50 ms");
    setup();
    ble_ready = false;
    keyboard_post_init_kb();
    CHECK(!annepro2_ble_is_ready());
    fake_sd_tx_clear(&SD1);
    fake_advance(49);
    matrix_scan_kb();
    CHECK_EQ(SD1.tx_len, 0);
    fake_advance(1);
    matrix_scan_kb();
    CHECK_EQ(SD1.tx_len, 22); /* wake-up and version request */

    CASE("keyboard_post_init_kb: a saved effect past the last one is replaced; the factory test mode is kept");
    setup();
    rgb_matrix_config.mode = RGB_MATRIX_EFFECT_MAX;
    keyboard_post_init_kb();
    CHECK_EQ(rgb_matrix_config.mode, RGB_MATRIX_DEFAULT_MODE);
    CHECK_EQ(fake_calls.eeconfig_flush, 1);
    setup();
    rgb_matrix_config.mode = RGB_MATRIX_EFFECT_MAX - 1;
    keyboard_post_init_kb();
    CHECK_EQ(rgb_matrix_config.mode, RGB_MATRIX_EFFECT_MAX - 1);
    setup();
    rgb_matrix_config.mode = 0; /* the mode is a 6-bit field: UINT8_MAX cannot be stored in it */
    keyboard_post_init_kb();
    CHECK_EQ(rgb_matrix_config.mode, 0);
    CHECK_EQ(fake_calls.eeconfig_flush, 0);

    CASE("keyboard_post_init_kb: LEDs enabled in the EEPROM: the LED MCU is switched on and set to manual; else off");
    setup();
    fake_rgb_enabled = true;
    keyboard_post_init_kb();
    CHECK_EQ(count_cmd(CMD_LED_SET_MANUAL), 1);
    CHECK_EQ(count_cmd(CMD_LED_ON), 1);
    setup();
    fake_rgb_enabled = false;
    keyboard_post_init_kb();
    CHECK_EQ(count_cmd(CMD_LED_OFF), 1);

    CASE("bootloader_jump: LED MCU to IAP, BLE MCU to IAP, the magic value, then the reset");
    setup();
    *(volatile uint32_t *)RAM_MAGIC_LOCATION = 0;
    bootloader_jump();
    CHECK_EQ(count_cmd(CMD_LED_IAP), 1);
    CHECK_EQ(SD1.tx_len, 11);
    CHECK_BYTES(SD1.tx, ((const uint8_t[]){0x7b, 0x10, 0x51, 0x10, 0x03, 0x00, 0x00, 0x7d, 0x02, 0x01, 0x01}), 11);
    CHECK_EQ(fake_calls.wait_ms_total, 30);
    CHECK_EQ(*(volatile uint32_t *)RAM_MAGIC_LOCATION, 0xfab2);
    CHECK_EQ(fake_calls.nvic_reset, 1);
}


int main(void) {
    case_init();

    /* The code comment at bt_switch_init(): PC10 is read with a pull-down (a floating pin must read as USB); the first
     * reading is applied by the first matrix scan. keyboard_post_init_kb() also starts the battery ADC and calls the
     * keymap's keyboard_post_init_user() (kazmir_bo switches NKRO on there). */
    CASE("keyboard_post_init_kb: sets up the switch pin, the ADC and calls the keymap hook");
    setup();
    fake_pin_c10 = false; /* the switch is in the Bluetooth position at power on */
    keyboard_post_init_kb();
    CHECK_EQ(fake_calls.pin_input_low, 1);
    CHECK_EQ(fake_calls.pin_input_low_pin, C10);
    CHECK(FAKE_ADC_REG(0x000) & (1u << 7)); /* AP2_ADC_CR_ENABLE: annepro2_battery_init() ran */
    CHECK_EQ(fake_calls.post_init_user, 1);
    scan();
    CHECK(annepro2_ble_is_active()); /* the first scan applies the switch position */
    return TEST_RESULT();
}
