/* The handshake with the BLE MCU (annepro2.c: ble_handshake_task()): the wake-up and version request are repeated
 * until the BLE MCU reports its version.
 *
 * Where the behaviour is documented:
 *   annepro2.c   the comment above ble_ready: "five times every 50 ... then every 5 s" of the stock firmware, ten times
 *                faster here (50 ms, then 500 ms), AP2_BLE_HANDSHAKE_TRIES quick tries (24) and then
 *                AP2_BLE_HANDSHAKE_SLOW_MS (5000) between tries, "the pings never stop"
 *   readme.md    AP2_BLE_HANDSHAKE_TRIES, AP2_BLE_HANDSHAKE_SLOW_MS */
#include "all_sources.h"

static const uint8_t ping[22] = {0x7b, 0x10, 0x53, 0x10, 0x03, 0x00, 0x00, 0x7d, 0x02, 0x01, 0x02, /* wake up */
                                 0x7b, 0x12, 0x53, 0x00, 0x03, 0x00, 0x00, 0x7d, 0x01, 0x03, 0x02}; /* version */

int main(void) {
    CASE("the pings come at 50 ms, then 500 ms, then 5000 ms apart, as long as there is no answer");
    reset_firmware();
    /* keyboard_post_init_kb() sends the first ping and starts the count at 1 */
    ble_ping_timer = timer_read32();
    ble_ping_count = 1;
    const uint32_t start = fake_ms;
    uint32_t       expected_at = 0;
    int            n           = 1; /* the ping that has been sent */
    int            sent        = 0;
    for (uint32_t ms = 1; ms <= 1400000; ms++) {
        fake_advance(1);
        fake_sd_drain(&SD1, 11); /* 115200 baud: about 11.5 bytes leave per ms */
        const size_t before = SD1.tx_len;
        ble_handshake_task();
        if (SD1.tx_len != before) {
            expected_at += n < 5 ? 50 : n < AP2_BLE_HANDSHAKE_TRIES ? 500 : AP2_BLE_HANDSHAKE_SLOW_MS;
            CHECK_EQ(SD1.tx_len - before, sizeof(ping));
            CHECK_EQ(fake_ms - start, expected_at);
            CHECK_BYTES(SD1.tx + before, ping, sizeof(ping));
            n++;
            sent++;
        }
    }
    CHECK(sent > 40);
    CHECK_EQ(ble_ping_count, 255); /* the count stops at 255, the pings go on */

    CASE("no more pings once the version is known");
    reset_firmware();
    ble_ping_timer = timer_read32();
    ble_ping_count = 1;
    ble_ready      = true;
    fake_advance(100000);
    ble_handshake_task();
    CHECK_EQ(SD1.tx_len, 0);
    return TEST_RESULT();
}
