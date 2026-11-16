/* The parser of the frames the BLE MCU sends to the main MCU (annepro2.c: ble_poll(), ble_rx_byte(),
 * ble_handle_frame()).
 *
 * Where the behaviour is documented:
 *   annepro2.h            the frame layout, the two frame types, the routing byte, BLE_RX_BODY_MAX and BLE_RX_SKIP_MAX
 *   annepro2.c            comments at ble_handle_frame(), ble_rx_valid(), ble_rx_byte(), ble_poll()
 *   annepro2_ble.c        "Frames sent back by the BLE MCU" (version, Caps Lock, connect)
 *   readme.md             AP2_BLE_RX_TIMEOUT_MS: "a frame that lost a byte is recognised by the sync byte of the next
 *                         one, and frames the Bluetooth MCU only forwards are skipped"
 *   docs/05-ble.md        the frames of the BLE MCU
 *
 * How the effects of a frame are seen: the version frame sets annepro2_ble_is_ready() and annepro2_ble_version(); the
 * Caps Lock frame sets the state the BLE host driver reports to QMK (host_get_driver()->keyboard_leds(), bit 1); the
 * connect frame clears that state and asks the battery code to send the level (send_now). */
#include "all_sources.h"

static const uint8_t body_version[] = {0x01, 0x03, 0x25};
static const uint8_t body_caps_on[] = {0x20, 0x07, 0x01};
static const uint8_t body_caps_off[] = {0x20, 0x07, 0x00};
static const uint8_t body_connect[] = {0x20, 0x0c, 0x00};
static const uint8_t body_eight[]   = {0x20, 0x07, 0x00, 1, 2, 3, 4, 5};

/* A frame from the BLE MCU: 7b type route seq len-lo len-hi 00 7d, then the body (cmd sub data). */
static size_t make_frame(uint8_t *out, uint8_t type, uint8_t route, uint8_t seq, const uint8_t *body,
                         uint16_t body_len) {
    const uint8_t head[8] = {0x7b, type, route, seq, (uint8_t)(body_len & 0xff), (uint8_t)(body_len >> 8), 0x00, 0x7d};
    memcpy(out, head, 8);
    if (body_len) memcpy(out + 8, body, body_len);
    return 8 + body_len;
}

static size_t ble_frame(uint8_t *out, const uint8_t *body, uint16_t body_len) {
    return make_frame(out, 0x12, 0x35, 0x00, body, body_len);
}

/* The bytes arrive and the main loop polls once. */
static void feed(const uint8_t *bytes, size_t n) {
    fake_sd_rx_push(&SD1, bytes, n);
    ble_poll();
}

static bool host_caps(void) {
    return (host_get_driver()->keyboard_leds() >> 1) & 1;
}

static void setup(void) {
    reset_firmware();
    annepro2_ble_use_driver(); /* the BLE host driver is the one that reports the Caps Lock state of the BLE host */
    CHECK(host_get_driver() != NULL);
    annepro2_ble_set_capslock(false);
}

static void case_valid_frames(void) {
    uint8_t f[16];
    size_t  n;

    CASE("version frame");
    setup();
    n = ble_frame(f, body_version, sizeof(body_version));
    CHECK_EQ(n, 11);
    CHECK(!annepro2_ble_is_ready());
    feed(f, n);
    CHECK(annepro2_ble_is_ready());
    CHECK_EQ(annepro2_ble_version(), 0x0205); /* 0x25 is 2.05 (annepro2.c) */
    CHECK_EQ(ble_rx_pos, 0);

    CASE("version nibbles");
    setup();
    const uint8_t v31[] = {0x01, 0x03, 0x31};
    n = ble_frame(f, v31, sizeof(v31));
    feed(f, n);
    CHECK_EQ(annepro2_ble_version(), 0x0301);

    CASE("Caps Lock frames");
    setup();
    n = ble_frame(f, body_caps_on, sizeof(body_caps_on));
    feed(f, n);
    CHECK(host_caps());
    n = ble_frame(f, body_caps_off, sizeof(body_caps_off));
    feed(f, n);
    CHECK(!host_caps());

    CASE("Caps Lock value above 1 is ignored");
    setup();
    n = ble_frame(f, body_caps_on, sizeof(body_caps_on));
    feed(f, n);
    const uint8_t caps_two[] = {0x20, 0x07, 0x02};
    n = ble_frame(f, caps_two, sizeof(caps_two));
    feed(f, n);
    CHECK(host_caps()); /* "if (d0 <= 1)": the state stays */

    CASE("connect frame");
    setup();
    n = ble_frame(f, body_caps_on, sizeof(body_caps_on));
    feed(f, n);
    CHECK(host_caps());
    send_now = false;
    n        = ble_frame(f, body_connect, sizeof(body_connect));
    feed(f, n);
    CHECK(!host_caps());  /* a new connection starts with Caps Lock off (annepro2_ble_host_connected()) */
    CHECK(send_now);      /* and the battery code is asked to send the level again */
    CHECK_EQ(SD1.tx_len, 0);

    CASE("unknown commands and short bodies do nothing");
    setup();
    const uint8_t other[]   = {0x40, 0x01, 0x00}; /* one-byte reply to a slot command */
    const uint8_t no_data[] = {0x01, 0x03};       /* len 2: no data byte */
    const uint8_t unknown[] = {0x20, 0x55, 0x01};
    n = ble_frame(f, other, sizeof(other));
    feed(f, n);
    n = ble_frame(f, no_data, sizeof(no_data));
    feed(f, n);
    n = ble_frame(f, unknown, sizeof(unknown));
    feed(f, n);
    CHECK(!annepro2_ble_is_ready());
    CHECK(!host_caps());
    CHECK_EQ(ble_rx_pos, 0);

    CASE("longest handled body (8 bytes) is handled");
    setup();
    const uint8_t body8[] = {0x20, 0x07, 0x01, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    n = ble_frame(f, body8, sizeof(body8));
    feed(f, n);
    CHECK(host_caps());
}

static void case_fragment_types(void) {
    uint8_t f[16];
    size_t  n;

    /* annepro2.c ble_handle_frame(): a fragmented frame (type 0x10) counts only when it is complete in itself, one
     * fragment with index 0, which is seq 0x10 (annepro2_ble.c header comment). */
    CASE("fragment type with seq 0x10 is handled");
    setup();
    n = make_frame(f, 0x10, 0x35, 0x10, body_caps_on, sizeof(body_caps_on));
    feed(f, n);
    CHECK(host_caps());

    CASE("fragment type with another seq is ignored");
    for (int seq = 0; seq < 256; seq++) {
        if (seq == 0x10) continue;
        setup();
        n = make_frame(f, 0x10, 0x35, (uint8_t)seq, body_caps_on, sizeof(body_caps_on));
        feed(f, n);
        CHECK(!host_caps());
        CHECK_EQ(ble_rx_pos, 0);
    }

    CASE("single frame type ignores seq");
    setup();
    n = make_frame(f, 0x12, 0x35, 0x77, body_caps_on, sizeof(body_caps_on));
    feed(f, n);
    CHECK(host_caps());

    CASE("a type that is neither 0x12 nor 0x10 is not a frame");
    for (int type = 0; type < 256; type++) {
        if (type == 0x12 || type == 0x10) continue;
        setup();
        n = make_frame(f, (uint8_t)type, 0x35, 0x00, body_caps_on, sizeof(body_caps_on));
        feed(f, n);
        CHECK(!host_caps());
        CHECK_EQ(ble_rx_pos, 0);
        /* and the frame after it is read */
        n = ble_frame(f, body_caps_on, sizeof(body_caps_on));
        feed(f, n);
        CHECK(host_caps());
    }
}

static void case_arrival_in_pieces(void) {
    uint8_t f[16];
    size_t  n = ble_frame(f, body_caps_on, sizeof(body_caps_on));

    /* The rest of a frame normally follows within a few milliseconds (annepro2.c, ble_poll()): any split of the bytes
     * over several polls gives the same result as one piece. */
    CASE("every split of a frame over two polls");
    for (size_t cut = 1; cut < n; cut++) {
        setup();
        feed(f, cut);
        CHECK(!host_caps());
        fake_advance(3);
        feed(f + cut, n - cut);
        CHECK(host_caps());
        CHECK_EQ(ble_rx_pos, 0);
    }

    CASE("one byte per poll");
    setup();
    for (size_t i = 0; i < n; i++) {
        CHECK(!host_caps());
        feed(f + i, 1);
        fake_advance(1);
    }
    CHECK(host_caps());

    CASE("two frames in one piece");
    setup();
    uint8_t two[32];
    size_t  m = ble_frame(two, body_caps_on, sizeof(body_caps_on));
    m += ble_frame(two + m, body_version, sizeof(body_version));
    feed(two, m);
    CHECK(host_caps());
    CHECK(annepro2_ble_is_ready());
}

/* One byte of the first frame is lost; the second frame follows at once. The second must be read, and the damaged one
 * must not take effect (annepro2.c: "A sync byte in the body of a frame that is held here ... means that a byte of
 * that frame was lost and the next frame has started"). */
static void case_dropped_byte(void) {
    static const struct {
        const char    *name;
        const uint8_t *body;
        uint16_t       len;
    } firsts[] = {
        {"version", body_version, sizeof(body_version)},
        {"connect", body_connect, sizeof(body_connect)},
        {"caps off", body_caps_off, sizeof(body_caps_off)},
        {"8 byte body", body_eight, 8},
    };
    uint8_t first[16], second[16], all[40];
    size_t  n2 = ble_frame(second, body_caps_on, sizeof(body_caps_on));

    for (size_t k = 0; k < sizeof(firsts) / sizeof(firsts[0]); k++) {
        const size_t n1 = ble_frame(first, firsts[k].body, firsts[k].len);
        for (size_t drop = 0; drop < n1; drop++) {
            CASE(firsts[k].name);
            setup();
            /* the damaged frame must have none of its effects */
            size_t m = 0;
            for (size_t i = 0; i < n1; i++) {
                if (i != drop) all[m++] = first[i];
            }
            memcpy(all + m, second, n2);
            m += n2;
            feed(all, m);
            if (!host_caps()) printf("    (first frame %s, byte %zu dropped)\n", firsts[k].name, drop);
            CHECK(host_caps());               /* the second frame was read */
            CHECK(!annepro2_ble_is_ready());  /* the damaged one was not applied (version frame) */
            CHECK_EQ(ble_rx_pos, 0);
        }
    }

    /* The same with a different arrival: the damaged frame, a pause shorter than the timeout, then the next frame. */
    CASE("dropped byte, next frame after a short pause");
    for (size_t drop = 0; drop < 11; drop++) {
        setup();
        uint8_t v[16];
        size_t  nv = ble_frame(v, body_version, sizeof(body_version));
        size_t  m  = 0;
        for (size_t i = 0; i < nv; i++) {
            if (i != drop) all[m++] = v[i];
        }
        feed(all, m);
        fake_advance(20);
        feed(second, n2);
        CHECK(host_caps());
    }
}

static void case_skipped_frames(void) {
    uint8_t f[96], g[16], all[200];
    size_t  n;

    /* annepro2.h: "a longer frame (for example one the BLE MCU forwards from a host) is skipped by its length, and only
     * frames routed from the BLE MCU to the main MCU are handled" */
    CASE("frame routed to the BLE MCU (0x53) is skipped by its length, its body is not scanned");
    setup();
    uint8_t inner[16], body[40];
    size_t  ni = ble_frame(inner, body_version, sizeof(body_version)); /* a whole frame inside the body */
    memcpy(body, inner, ni);
    n = make_frame(f, 0x12, 0x53, 0x00, body, (uint16_t)ni);
    size_t ng = ble_frame(g, body_caps_on, sizeof(body_caps_on));
    memcpy(all, f, n);
    memcpy(all + n, g, ng);
    feed(all, n + ng);
    CHECK(!annepro2_ble_is_ready()); /* the frame inside the body was not read */
    CHECK(host_caps());              /* the frame after it was */

    CASE("any other route is skipped");
    for (int route = 0; route < 256; route++) {
        if (route == 0x35) continue;
        setup();
        n = make_frame(f, 0x12, (uint8_t)route, 0x00, body_caps_on, sizeof(body_caps_on));
        feed(f, n);
        CHECK(!host_caps());
        CHECK_EQ(ble_rx_pos, 0);
        ng = ble_frame(g, body_connect, sizeof(body_connect));
        annepro2_ble_set_capslock(true);
        feed(g, ng);
        CHECK(!host_caps()); /* the connect frame after it was read (it clears the state) */
    }

    CASE("longer than the handled body, from the BLE MCU: skipped by length");
    for (int len = 9; len <= BLE_RX_SKIP_MAX; len++) {
        setup();
        uint8_t big[8 + 64 + 16], longbody[64];
        memset(longbody, 0x7b, sizeof(longbody)); /* a body full of sync bytes */
        memcpy(longbody, body_caps_on, 3);        /* with a Caps Lock frame body at its start */
        n = make_frame(big, 0x12, 0x35, 0x00, longbody, (uint16_t)len);
        ng = ble_frame(g, body_version, sizeof(body_version));
        memcpy(big + n, g, ng);
        feed(big, n + ng);
        CHECK(!host_caps());            /* the caps frame at the start of the long body was not handled */
        CHECK(annepro2_ble_is_ready()); /* the frame after it was */
        CHECK_EQ(ble_rx_skip, 0);
        CHECK_EQ(ble_rx_pos, 0);
    }

    CASE("length above BLE_RX_SKIP_MAX is not skipped: a frame right after the header is read");
    static const uint16_t lens[] = {65, 66, 100, 255, 256, 0x7b7b, 0x7d00, 0xffff};
    for (size_t i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        setup();
        const uint8_t head[8] = {0x7b, 0x12, 0x35, 0x00, (uint8_t)(lens[i] & 0xff),
                                 (uint8_t)(lens[i] >> 8), 0x00, 0x7d};
        memcpy(all, head, 8);
        ng = ble_frame(g, body_caps_on, sizeof(body_caps_on));
        memcpy(all + 8, g, ng);
        feed(all, 8 + ng);
        CHECK(host_caps());
        CHECK_EQ(ble_rx_skip, 0);
    }

    CASE("length below 2 is not a frame");
    for (int len = 0; len < 2; len++) {
        setup();
        const uint8_t head[8] = {0x7b, 0x12, 0x35, 0x00, (uint8_t)len, 0x00, 0x00, 0x7d};
        memcpy(all, head, 8);
        ng = ble_frame(g, body_caps_on, sizeof(body_caps_on));
        memcpy(all + 8, g, ng);
        feed(all, 8 + ng);
        CHECK(host_caps());
    }

    CASE("garbage before a frame is ignored");
    setup();
    const uint8_t junk[] = {0x00, 0xff, 0x12, 0x35, 0x7d, 0x03, 0x00};
    memcpy(all, junk, sizeof(junk));
    ng = ble_frame(g, body_caps_on, sizeof(body_caps_on));
    memcpy(all + sizeof(junk), g, ng);
    feed(all, sizeof(junk) + ng);
    CHECK(host_caps());
}

static void case_timeout(void) {
    uint8_t f[16], g[16];
    size_t  n  = ble_frame(f, body_caps_on, sizeof(body_caps_on));
    size_t  ng = ble_frame(g, body_version, sizeof(body_version));

    /* AP2_BLE_RX_TIMEOUT_MS (100): a frame that stops arriving is dropped (annepro2.c, readme.md) */
    CASE("an unfinished frame is dropped after 100 ms");
    setup();
    feed(f, 6);
    CHECK_EQ(ble_rx_pos, 6);
    fake_advance(AP2_BLE_RX_TIMEOUT_MS - 1);
    ble_poll();
    CHECK_EQ(ble_rx_pos, 6); /* not yet */
    fake_advance(1);
    ble_poll();
    CHECK_EQ(ble_rx_pos, 0); /* now */
    /* the rest of the old frame is noise, the next frame is read */
    feed(f + 6, n - 6);
    CHECK(!host_caps());
    feed(g, ng);
    CHECK(annepro2_ble_is_ready());

    CASE("a frame that arrives in time is not dropped");
    setup();
    feed(f, 6);
    fake_advance(AP2_BLE_RX_TIMEOUT_MS - 1);
    feed(f + 6, n - 6);
    CHECK(host_caps());

    CASE("the timeout runs from the last byte, not from the first");
    setup();
    feed(f, 3);
    fake_advance(80);
    feed(f + 3, 3);
    fake_advance(80); /* 160 ms since the first byte, 80 since the last */
    ble_poll();
    CHECK_EQ(ble_rx_pos, 6);
    feed(f + 6, n - 6);
    CHECK(host_caps());

    CASE("the timeout ends a skip");
    setup();
    uint8_t big[8 + 40], filler[40];
    memset(filler, 0x55, sizeof(filler));
    make_frame(big, 0x12, 0x53, 0x00, filler, 40);
    feed(big, 8 + 10); /* the header and 10 of 40 bytes */
    CHECK_EQ(ble_rx_skip, 30);
    fake_advance(AP2_BLE_RX_TIMEOUT_MS);
    ble_poll();
    CHECK_EQ(ble_rx_skip, 0);
    feed(f, n);
    CHECK(host_caps()); /* without the timeout these 11 bytes would have been swallowed */

    CASE("a frame left over from a long stall does not swallow the next one");
    setup();
    feed(f, 6);
    fake_advance(5000);
    /* no poll in between: the old and the new frame arrive together */
    feed(g, ng);
    CHECK(annepro2_ble_is_ready());
    CHECK_EQ(ble_rx_pos, 0);

    CASE("the timer survives the 32-bit wrap");
    setup();
    fake_ms = 0xFFFFFFF0u;
    feed(f, 6);
    fake_advance(AP2_BLE_RX_TIMEOUT_MS - 1);
    ble_poll();
    CHECK_EQ(ble_rx_pos, 6);
    fake_advance(1);
    ble_poll();
    CHECK_EQ(ble_rx_pos, 0);
}

/* A small xorshift generator, so that the noise is the same on every run. */
static uint32_t rng_state;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void case_noise(void) {
    /* The parser must never overrun its buffers (AddressSanitizer is on), never hold more than a header and a body, and
     * be able to read a frame afterwards (annepro2.c: ble_rx_byte(), the timeout in ble_poll()). */
    static const uint8_t alphabet[] = {0x7b, 0x7d, 0x12, 0x10, 0x35, 0x53, 0x00, 0x01,
                                       0x03, 0x07, 0x0c, 0x20, 0x40, 0x02, 0x08, 0x40};
    uint8_t              f[16], g[16];
    size_t               n  = ble_frame(f, body_caps_on, sizeof(body_caps_on));
    size_t               ng = ble_frame(g, body_version, sizeof(body_version));

    for (int mode = 0; mode < 3; mode++) {
        CASE(mode == 0 ? "uniform noise" : mode == 1 ? "noise of header bytes" : "noise with frame heads");
        setup();
        rng_state = 0x2545F491u + (uint32_t)mode;
        for (long i = 0; i < 200000; i++) {
            uint8_t b;
            if (mode == 0) {
                b = (uint8_t)rng();
            } else if (mode == 1) {
                b = alphabet[rng() % sizeof(alphabet)];
            } else if (rng() % 16 == 0) {
                /* a plausible head: sync, type, route, seq, a random length, zero, end */
                const uint8_t type = rng() & 1 ? 0x12 : 0x10, route = rng() & 1 ? 0x35 : 0x53;
                const uint8_t head[8] = {0x7b, type, route, (uint8_t)rng(), (uint8_t)(rng() % 80), 0x00, 0x00, 0x7d};
                for (int k = 0; k < 8; k++) ble_rx_byte(head[k]);
                b = (uint8_t)rng();
            } else {
                b = alphabet[rng() % sizeof(alphabet)];
            }
            ble_rx_byte(b);
            if (ble_rx_pos > sizeof(ble_rx) || ble_rx_skip > BLE_RX_SKIP_MAX) {
                CHECK(ble_rx_pos <= sizeof(ble_rx));
                CHECK(ble_rx_skip <= BLE_RX_SKIP_MAX);
                break;
            }
        }
        CHECK(ble_rx_pos <= sizeof(ble_rx));
        CHECK(ble_rx_skip <= BLE_RX_SKIP_MAX);

        /* After the noise, once the line has been quiet for the timeout, a frame is read. */
        ble_ready = false;
        annepro2_ble_set_capslock(false);
        ble_rx_timer = timer_read32();
        fake_advance(AP2_BLE_RX_TIMEOUT_MS);
        ble_poll();
        CHECK_EQ(ble_rx_pos, 0);
        CHECK_EQ(ble_rx_skip, 0);
        feed(f, n);
        CHECK(host_caps());
        feed(g, ng);
        CHECK(annepro2_ble_is_ready());
    }

    /* Without a quiet period: a frame skipped by its length holds at most BLE_RX_SKIP_MAX bytes, so among a few frames
     * sent right after the noise the last one is always read. */
    CASE("frames right after noise");
    setup();
    rng_state = 12345;
    for (int i = 0; i < 5000; i++) ble_rx_byte(alphabet[rng() % sizeof(alphabet)]);
    for (int i = 0; i < 8; i++) {
        feed(f, n);
    }
    CHECK(host_caps());
    CHECK_EQ(ble_rx_pos, 0);
}

static void case_serial_after_version(void) {
    uint8_t f[16];
    size_t  n = ble_frame(f, body_version, sizeof(body_version));

    /* annepro2.c ble_handle_frame(): "The BLE MCU also reports its version after it restarts, which clears the serial
     * number": the serial number is sent after every version frame (annepro2_ble.c has the bytes). */
    CASE("a version frame sends the serial number when there is a device record");
    setup();
    for (int i = 0; i < 16; i++) fake_device_record[8 + i] = (uint8_t)(0x10 + i);
    feed(f, n);
    CHECK_EQ(SD1.tx_len, 8 + 2 + 33);
    CHECK_EQ(SD1.tx[8], 0x01);
    CHECK_EQ(SD1.tx[9], 0x10);
    fake_sd_tx_clear(&SD1);
    feed(f, n);
    CHECK_EQ(SD1.tx_len, 8 + 2 + 33); /* again after the next version frame */

    CASE("no serial number without a device record");
    setup();
    feed(f, n);
    CHECK_EQ(SD1.tx_len, 0);
}

int main(void) {
    case_valid_frames();
    case_fragment_types();
    case_arrival_in_pieces();
    case_dropped_byte();
    case_skipped_frames();
    case_timeout();
    case_noise();
    case_serial_after_version();
    return TEST_RESULT();
}
