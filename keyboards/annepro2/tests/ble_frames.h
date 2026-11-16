/* Frames from the BLE MCU, fed to the real parser of annepro2.c (ble_poll()). Needs all_sources.h. */
#pragma once

static size_t bf_frame(uint8_t *out, const uint8_t *body, uint16_t body_len) {
    const uint8_t head[8] = {0x7b, 0x12, 0x35, 0x00, (uint8_t)(body_len & 0xff), (uint8_t)(body_len >> 8), 0x00, 0x7d};
    memcpy(out, head, 8);
    memcpy(out + 8, body, body_len);
    return 8u + body_len;
}

/* The BLE MCU sends one frame: body is cmd, sub, data. */
static void bf_send(const uint8_t *body, uint16_t body_len) {
    uint8_t f[32];
    const size_t n = bf_frame(f, body, body_len);
    fake_sd_rx_push(&SD1, f, n);
    ble_poll();
}

static void bf_version(void) {
    static const uint8_t body[] = {0x01, 0x03, 0x25};
    bf_send(body, sizeof(body));
}
