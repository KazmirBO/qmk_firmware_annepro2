/* The serial protocol between the main MCU and the LED MCU (protocol.c, shared with the Shine firmware): the receive
 * state machine and the transmitter.
 *
 * Where the behaviour is documented:
 *   protocol.h   the message layout (header 7A 1D cmd id size, then the payload), MAX_PAYLOAD_SIZE, the states,
 *                PROTO_TX_MESSAGE_SIZE
 *   protocol.c   "a robust serial protocol which can handle single dropped characters during transit without locking",
 *                "send count times ... Receiver deduplicates by msg_id", the clamp of an oversized payload
 *   docs/02-protokol.md  the same in Polish
 *
 * There is no checksum in the protocol: a message that lost a byte can be delivered with wrong content. The tests
 * below pin what the code promises (no lock, recovery), not more. */
#include "all_sources.h"
#include "proto_decode.h"

static protocol_t rx;

static void rx_start(void) {
    reset_firmware();
    proto_init(&rx, dec_callback);
    dec_count = 0;
}

static void rx_feed(const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) proto_consume(&rx, b[i]);
}

static size_t make_msg(uint8_t *out, uint8_t cmd, uint8_t id, const uint8_t *payload, uint8_t size) {
    out[0] = 0x7a;
    out[1] = 0x1d;
    out[2] = cmd;
    out[3] = id;
    out[4] = size;
    if (size) memcpy(out + 5, payload, size);
    return 5 + (size_t)size;
}

static void case_receive(void) {
    uint8_t m[80], payload[64];
    for (int i = 0; i < 64; i++) payload[i] = (uint8_t)(0xa0 + i);

    CASE("init");
    rx_start();
    CHECK_EQ(rx.state, STATE_SYNC_1);
    CHECK_EQ(rx.errors, 0);

    CASE("a message of every payload size 0 to 64");
    for (int size = 0; size <= 64; size++) {
        rx_start();
        size_t n = make_msg(m, 0x41, 7, payload, (uint8_t)size);
        rx_feed(m, n);
        CHECK_EQ(dec_count, 1);
        CHECK_EQ(dec_msgs[0].cmd, 0x41);
        CHECK_EQ(dec_msgs[0].id, 7);
        CHECK_EQ(dec_msgs[0].size, size);
        CHECK_BYTES(dec_msgs[0].payload, payload, (size_t)size);
        CHECK_EQ(rx.state, STATE_SYNC_1);
        CHECK_EQ(rx.errors, 0);
    }

    CASE("the callback is not called before the last byte");
    rx_start();
    size_t n = make_msg(m, 0x41, 7, payload, 6);
    rx_feed(m, n - 1);
    CHECK_EQ(dec_count, 0);
    CHECK_EQ(rx.state, STATE_PAYLOAD);
    rx_feed(m + n - 1, 1);
    CHECK_EQ(dec_count, 1);

    CASE("messages with different ids are all delivered");
    rx_start();
    for (int id = 1; id <= 20; id++) {
        n = make_msg(m, 0x10, (uint8_t)id, payload, 3);
        rx_feed(m, n);
    }
    CHECK_EQ(dec_count, 20);
    for (int i = 0; i < 20 && i < (int)dec_count; i++) CHECK_EQ(dec_msgs[i].id, i + 1);

    CASE("the same id again is a duplicate and is dropped, also when the content differs");
    rx_start();
    n = make_msg(m, 0x10, 5, payload, 3);
    rx_feed(m, n);
    rx_feed(m, n);
    rx_feed(m, n);
    CHECK_EQ(dec_count, 1);
    CHECK_EQ(rx.state, STATE_SYNC_1);
    n = make_msg(m, 0x11, 5, payload, 3);
    rx_feed(m, n);
    CHECK_EQ(dec_count, 1);
    n = make_msg(m, 0x10, 6, payload, 3);
    rx_feed(m, n);
    CHECK_EQ(dec_count, 2);
    n = make_msg(m, 0x10, 5, payload, 3); /* only the last id counts: 5 again after 6 is new */
    rx_feed(m, n);
    CHECK_EQ(dec_count, 3);

    CASE("a message without payload is complete after the size byte");
    rx_start();
    n = make_msg(m, 0x01, 9, NULL, 0);
    rx_feed(m, n);
    CHECK_EQ(dec_count, 1);
    CHECK_EQ(dec_msgs[0].size, 0);
}

static void case_garbage(void) {
    uint8_t m[80], payload[64] = {1, 2, 3, 4};

    CASE("bytes outside a message are counted as errors and skipped");
    rx_start();
    const uint8_t junk[] = {0x00, 0x1d, 0xff, 0x41};
    rx_feed(junk, sizeof(junk));
    CHECK_EQ(rx.errors, 4);
    CHECK_EQ(dec_count, 0);
    size_t n = make_msg(m, 0x10, 3, payload, 4);
    rx_feed(m, n);
    CHECK_EQ(dec_count, 1);
    CHECK_EQ(rx.errors, 4);

    CASE("a first sync byte not followed by the second drops back");
    rx_start();
    const uint8_t half[] = {0x7a, 0x55};
    rx_feed(half, sizeof(half));
    CHECK_EQ(rx.state, STATE_SYNC_1);
    CHECK_EQ(rx.errors, 1);
    n = make_msg(m, 0x10, 3, payload, 4);
    rx_feed(m, n);
    CHECK_EQ(dec_count, 1);

    CASE("payload size above 64 is cut to 64 and counted");
    for (int size = 65; size < 256; size += 19) {
        rx_start();
        const uint8_t head[] = {0x7a, 0x1d, 0x41, 9, (uint8_t)size};
        rx_feed(head, sizeof(head));
        CHECK_EQ(rx.errors, 1);
        CHECK_EQ(rx.buffer.payload_size, 64);
        for (int i = 0; i < 64; i++) {
            CHECK_EQ(dec_count, 0);
            proto_consume(&rx, (uint8_t)i);
        }
        CHECK_EQ(dec_count, 1);
        CHECK_EQ(dec_msgs[0].size, 64);
        CHECK_EQ(rx.state, STATE_SYNC_1);
    }

    CASE("silence in the middle of a message resets the receiver");
    rx_start();
    n = make_msg(m, 0x10, 3, payload, 4);
    rx_feed(m, n - 2);
    proto_silence(&rx);
    CHECK_EQ(rx.state, STATE_SYNC_1);
    CHECK_EQ(rx.errors, 1);
    rx_feed(m, n);
    CHECK_EQ(dec_count, 1);

    CASE("silence between messages is not an error");
    rx_start();
    proto_silence(&rx);
    CHECK_EQ(rx.errors, 0);
}

static void case_dropped_byte(void) {
    /* protocol.c: "can handle single dropped characters during transit without locking". A message that lost a byte
     * may be delivered damaged or lost, and may take the messages that follow with it (a lost size byte makes the
     * receiver read up to 64 bytes of them as payload), but the receiver finds its way back: after at most one
     * maximum payload of the following messages all arrive. */
    uint8_t payload[4] = {1, 2, 3, 4}, m[16];
    for (int len = 0; len <= 4; len += 4) {
        const size_t size = (size_t)len;
        const size_t nn   = make_msg(m, 0x10, 1, payload, (uint8_t)size);
        for (size_t drop = 0; drop < nn; drop++) {
            CASE("one dropped byte, then more messages");
            rx_start();
            for (size_t i = 0; i < nn; i++) {
                if (i != drop) proto_consume(&rx, m[i]);
            }
            for (int id = 2; id <= 40; id++) {
                uint8_t k[16];
                size_t  kn = make_msg(k, 0x10, (uint8_t)id, payload, (uint8_t)size);
                rx_feed(k, kn);
            }
            bool seen[41] = {false};
            for (size_t i = 0; i < dec_count; i++) {
                if (dec_msgs[i].id <= 40 && dec_msgs[i].cmd == 0x10 && dec_msgs[i].size == size) {
                    seen[dec_msgs[i].id] = true;
                }
            }
            /* 64 bytes are at most 13 messages of 5 bytes: the last 20 of the 39 arrive */
            bool tail_ok = true;
            for (int id = 21; id <= 40; id++) tail_ok &= seen[id];
            if (!tail_ok) printf("    (size %zu, byte %zu dropped)\n", size, drop);
            CHECK(tail_ok);
            CHECK_EQ(rx.state, STATE_SYNC_1);
        }
    }
}

static void case_transmit(void) {
    uint8_t payload[80];
    for (int i = 0; i < 80; i++) payload[i] = (uint8_t)(i * 3 + 1);

    CASE("one message: header and payload");
    reset_firmware();
    fake_sd_allow_overflow(&SD0); /* the bytes are checked here, not the flow: nothing drains the queue */
    proto_tx(0x32, payload, 4, 1);
    const uint8_t exp1[] = {0x7a, 0x1d, 0x32, 1, 4, 1, 4, 7, 10};
    CHECK_EQ(SD0.tx_len, sizeof(exp1));
    CHECK_BYTES(SD0.tx, exp1, sizeof(exp1));
    CHECK_EQ(PROTO_TX_MESSAGE_SIZE(4), sizeof(exp1));

    CASE("the id counts up by one per call, not per copy");
    reset_firmware();
    fake_sd_allow_overflow(&SD0); /* the bytes are checked here, not the flow: nothing drains the queue */
    proto_tx(0x01, NULL, 0, 3);
    proto_tx(0x02, NULL, 0, 1);
    const uint8_t exp2[] = {0x7a, 0x1d, 0x01, 1, 0, 0x7a, 0x1d, 0x01, 1, 0,
                            0x7a, 0x1d, 0x01, 1, 0, 0x7a, 0x1d, 0x02, 2, 0};
    CHECK_EQ(SD0.tx_len, sizeof(exp2));
    CHECK_BYTES(SD0.tx, exp2, sizeof(exp2));

    CASE("count copies; the receiver delivers one");
    reset_firmware();
    fake_sd_allow_overflow(&SD0); /* the bytes are checked here, not the flow: nothing drains the queue */
    proto_tx(0x32, payload, 10, 3);
    CHECK_EQ(SD0.tx_len, 3 * PROTO_TX_MESSAGE_SIZE(10));
    rx_start();
    fake_sd_tx_clear(&SD0);
    proto_tx(0x32, payload, 10, 3);
    CHECK_EQ(decode_tx(&SD0, NULL), 1);
    CHECK_EQ(dec_msgs[0].size, 10);

    CASE("count 0 sends nothing");
    reset_firmware();
    fake_sd_allow_overflow(&SD0); /* the bytes are checked here, not the flow: nothing drains the queue */
    proto_tx(0x32, payload, 10, 0);
    CHECK_EQ(SD0.tx_len, 0);

    CASE("a payload above 64 bytes is cut to 64");
    reset_firmware();
    fake_sd_allow_overflow(&SD0); /* the bytes are checked here, not the flow: nothing drains the queue */
    proto_tx(0x32, payload, 80, 1);
    CHECK_EQ(SD0.tx_len, PROTO_TX_MESSAGE_SIZE(64));
    CHECK_EQ(SD0.tx[4], 64);
    CHECK_BYTES(SD0.tx + 5, payload, 64);
    CHECK_EQ(decode_tx(&SD0, NULL), 1);

    CASE("the id wraps from 255 to 0 and the receiver still delivers every message");
    reset_firmware();
    fake_sd_allow_overflow(&SD0); /* the bytes are checked here, not the flow: nothing drains the queue */
    for (int i = 0; i < 600; i++) proto_tx(0x10, payload, 1, 1);
    CHECK_EQ(SD0.tx_len, 600 * PROTO_TX_MESSAGE_SIZE(1));
    CHECK_EQ(decode_tx(&SD0, NULL), 600);
    CHECK_EQ(dec_msgs[254].id, 255);
    CHECK_EQ(dec_msgs[255].id, 0);
    CHECK_EQ(dec_msgs[256].id, 1);
}

static uint32_t rng_state = 0x1badcafe;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void case_noise(void) {
    uint8_t m[80], payload[64] = {9, 9, 9, 9};

    CASE("random bytes never get the receiver stuck");
    rx_start();
    for (long i = 0; i < 100000; i++) {
        uint8_t b = (uint8_t)rng();
        if (i % 7 == 0) b = 0x7a;
        if (i % 11 == 0) b = 0x1d;
        proto_consume(&rx, b);
        if (rx.state > STATE_PAYLOAD || rx.payload_position > MAX_PAYLOAD_SIZE) {
            CHECK(rx.state <= STATE_PAYLOAD);
            CHECK(rx.payload_position <= MAX_PAYLOAD_SIZE);
            break;
        }
    }
    CHECK(rx.state <= STATE_PAYLOAD);
    /* a message that has to be read to the end can take up to 64 + 5 bytes of the following ones; after that a message
     * is received */
    dec_count = 0;
    for (int k = 0; k < 20; k++) {
        size_t n = make_msg(m, 0x10, (uint8_t)(100 + k), payload, 4);
        rx_feed(m, n);
    }
    CHECK(dec_count >= 10);
    CHECK_EQ(dec_msgs[dec_count - 1].id, 119);
    CHECK_EQ(rx.state, STATE_SYNC_1);

    CASE("silence after noise: the next message arrives");
    rx_start();
    for (long i = 0; i < 1000; i++) proto_consume(&rx, (uint8_t)rng());
    proto_silence(&rx);
    dec_count = 0;
    size_t n = make_msg(m, 0x10, 77, payload, 4);
    rx_feed(m, n);
    CHECK_EQ(dec_count, 1);
}

int main(void) {
    case_receive();
    case_garbage();
    case_dropped_byte();
    case_transmit();
    case_noise();
    return TEST_RESULT();
}
