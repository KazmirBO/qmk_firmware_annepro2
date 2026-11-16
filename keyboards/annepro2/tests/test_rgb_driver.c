/* The rgb_matrix driver (rgb_driver.c) on top of the LED commands (ap2_led.c) and the protocol (protocol.c): what is
 * written to the LED UART (SD0), decoded with the real receiver of protocol.c (proto_decode.h).
 *
 * Where the behaviour is documented:
 *   rgb_driver.c   the long comment above ROW_MESSAGE_SIZE: rows are sent only when a colour changes, a row that ends a
 *                  frame as it was last sent is dropped, a row is sent only when the whole message fits and the reserve
 *                  stays free, the rows that did not fit go out in turn from ap2_rgb_flush_pending(), a row that the
 *                  next frame has begun to change waits, and while the LEDs are on one row is sent again every
 *                  AP2_LED_REFRESH_MS
 *   ap2_led.h      AP2_LED_TX_RESERVE, ap2_rgb_lit(), ap2_rgb_flush_pending(), ap2_rgb_resend_all()
 *   readme.md      the paragraph on RGB_MATRIX_ENABLE
 *
 * The LED layout is the one of info.json (generated into build/led_layout.h by run.sh). The output queue of the fake
 * LED UART holds SERIAL_BUFFERS_SIZE (128) bytes; a row message is 62 bytes and the reserve is 48, so a row needs 110
 * free bytes. */
#include "all_sources.h"
#include "proto_decode.h"

#define ROW_MSG PROTO_TX_MESSAGE_SIZE(1 + NUM_COLUMN * 4)

static void setup(void) {
    reset_firmware();
    fake_rgb_enabled = true;
    rgb_matrix_driver.init();
    rgb_matrix_driver.set_color_all(0, 0, 0); /* a known state: every row sent, all hashes valid */
    fake_sd_tx_clear(&SD0);
    refresh_timer      = timer_read32();
    fake_last_input_ms = fake_ms;
}

static int led_of(int row, int col) {
    return g_led_config.matrix_co[row][col];
}

/* an LED index in a row (the first one that exists) */
static int led_in_row(int row) {
    for (int c = 0; c < NUM_COLUMN; c++) {
        if (led_of(row, c) != NO_LED) return led_of(row, c);
    }
    return -1;
}

static int col_in_row(int row) {
    for (int c = 0; c < NUM_COLUMN; c++) {
        if (led_of(row, c) != NO_LED) return c;
    }
    return -1;
}

/* The rows of the row messages written since the last clear, in order. */
static size_t rows_sent(int *rows, size_t max) {
    size_t n = decode_tx(&SD0, NULL), k = 0;
    for (size_t i = 0; i < n && k < max; i++) {
        if (dec_msgs[i].cmd == CMD_LED_COLOR_SET_ROW) rows[k++] = dec_msgs[i].payload[0];
    }
    return k;
}

static void drain_all(void) {
    fake_sd_drain(&SD0, 100000);
}

static void case_layout(void) {
    CASE("the LED layout of info.json");
    setup();
    CHECK_EQ(RGB_MATRIX_LED_COUNT, 61);
    for (int i = 0; i < RGB_MATRIX_LED_COUNT; i++) {
        bool found = false;
        for (int r = 0; r < NUM_ROW; r++) {
            for (int c = 0; c < NUM_COLUMN; c++) {
                if (led_of(r, c) == i) {
                    found = true;
                    CHECK_EQ(led_row[i], r);
                    CHECK_EQ(led_pos[i], r * NUM_COLUMN + c);
                }
            }
        }
        CHECK(found);
    }
}

static void case_changed_only(void) {
    CASE("nothing changed: nothing is sent");
    setup();
    for (int i = 0; i < RGB_MATRIX_LED_COUNT; i++) rgb_matrix_driver.set_color(i, 0, 0, 0);
    CHECK_EQ(dirty_rows, 0); /* "set_color() marks a row only when a colour really changes" */
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, 0);

    CASE("one LED changed: its row is sent with that colour");
    setup();
    const int led = led_of(2, 3);
    CHECK(led != NO_LED);
    rgb_matrix_driver.set_color(led, 0x11, 0x22, 0x33);
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, ROW_MSG);
    unsigned errors;
    CHECK_EQ(decode_tx(&SD0, &errors), 1);
    CHECK_EQ(errors, 0);
    CHECK_EQ(dec_msgs[0].cmd, CMD_LED_COLOR_SET_ROW);
    CHECK_EQ(dec_msgs[0].size, 1 + 56);
    CHECK_EQ(dec_msgs[0].payload[0], 2);
    for (int c = 0; c < NUM_COLUMN; c++) {
        const uint8_t *px = dec_msgs[0].payload + 1 + 4 * c;
        if (c == 3) {
            CHECK_EQ(px[0], 0x33); /* blue, green, red, alpha */
            CHECK_EQ(px[1], 0x22);
            CHECK_EQ(px[2], 0x11);
            CHECK_EQ(px[3], 0xff);
        } else {
            CHECK_EQ(px[0] | px[1] | px[2], 0);
        }
    }

    CASE("the same colour again in the next frame: nothing is sent");
    fake_sd_tx_clear(&SD0);
    rgb_matrix_driver.set_color(led, 0x11, 0x22, 0x33);
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, 0);

    CASE("each of the three components counts");
    setup();
    for (int comp = 0; comp < 3; comp++) {
        fake_sd_tx_clear(&SD0);
        drain_all();
        const uint8_t v[3] = {(uint8_t)(comp == 0 ? 1 : 0), (uint8_t)(comp == 1 ? 1 : 0), (uint8_t)(comp == 2 ? 1 : 0)};
        rgb_matrix_driver.set_color(led, v[0], v[1], v[2]);
        rgb_matrix_driver.flush();
        CHECK_EQ(SD0.tx_len, ROW_MSG);
        rgb_matrix_driver.set_color(led, 0, 0, 0);
        rgb_matrix_driver.flush();
        fake_sd_tx_clear(&SD0);
        drain_all();
    }

    CASE("a row is sent once for several LEDs in it");
    setup();
    for (int c = 0; c < NUM_COLUMN; c++) {
        if (led_of(1, c) != NO_LED) rgb_matrix_driver.set_color(led_of(1, c), 0x40, 0x50, 0x60);
    }
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, ROW_MSG);

    CASE("changed back inside one frame: the row is not sent");
    setup();
    rgb_matrix_driver.set_color(led, 0xaa, 0xbb, 0xcc);
    rgb_matrix_driver.set_color(led, 0, 0, 0);
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, 0);
    CHECK_EQ(dirty_rows, 0);
    CHECK_EQ(pending_rows, 0);

    CASE("an indicator painted over the effect in every frame is sent once");
    setup();
    const int ind = led_of(0, 1);
    for (int frame = 0; frame < 5; frame++) {
        rgb_matrix_driver.set_color(ind, 0x10, 0x20, 0x30); /* the effect */
        rgb_matrix_driver.set_color(ind, 0xff, 0x00, 0x00); /* the indicator on top */
        rgb_matrix_driver.flush();
        drain_all();
        if (frame == 0) {
            CHECK_EQ(SD0.tx_len, ROW_MSG);
            fake_sd_tx_clear(&SD0);
        } else {
            CHECK_EQ(SD0.tx_len, 0);
        }
    }

    CASE("a row that goes back to what the LED MCU has: a change still waiting is void");
    setup();
    fake_sd_set_free(&SD0, 0); /* no room: the row waits */
    rgb_matrix_driver.set_color(led, 0x77, 0x00, 0x00);
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, 0);
    CHECK(pending_rows != 0);
    rgb_matrix_driver.set_color(led, 0, 0, 0); /* back to what was sent */
    rgb_matrix_driver.flush();
    CHECK_EQ(pending_rows, 0);
    fake_sd_set_free(&SD0, -1);
    ap2_rgb_flush_pending();
    CHECK_EQ(SD0.tx_len, 0);
}

static void case_space(void) {
    CASE("the reserve: a row needs its size plus AP2_LED_TX_RESERVE free");
    setup();
    const int led = led_of(0, 1);
    rgb_matrix_driver.set_color(led, 1, 2, 3);
    fake_sd_set_free(&SD0, ROW_MSG + AP2_LED_TX_RESERVE - 1);
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, 0);
    CHECK(pending_rows & 1);
    fake_sd_set_free(&SD0, ROW_MSG + AP2_LED_TX_RESERVE);
    ap2_rgb_flush_pending();
    CHECK_EQ(SD0.tx_len, ROW_MSG);
    CHECK_EQ(pending_rows, 0);
    CHECK_EQ(SD0.overflows, 0);

    CASE("the size of a row message and the reserve");
    CHECK_EQ(ROW_MSG, 62);
    CHECK_EQ(AP2_LED_TX_RESERVE, 48);

    CASE("a fresh queue takes one row of a full frame, the rest follows as the UART drains");
    setup();
    for (int r = 0; r < NUM_ROW; r++) rgb_matrix_driver.set_color(led_in_row(r), 9, 9, (uint8_t)(r + 1));
    rgb_matrix_driver.flush();
    int rows[8];
    CHECK_EQ(rows_sent(rows, 8), 1);
    CHECK_EQ(rows[0], 0);
    for (int r = 1; r < NUM_ROW; r++) {
        ap2_rgb_flush_pending();
        CHECK_EQ(rows_sent(rows, 8), (size_t)r); /* no room: nothing more */
        drain_all();
        ap2_rgb_flush_pending();
        CHECK_EQ(rows_sent(rows, 8), (size_t)r + 1);
    }
    size_t k = rows_sent(rows, 8);
    CHECK_EQ(k, 5);
    for (size_t i = 0; i < k && i < 5; i++) CHECK_EQ(rows[i], (int)i);
    CHECK_EQ(pending_rows, 0);
    CHECK_EQ(SD0.overflows, 0); /* sdWrite() never had to wait */

    CASE("each row has the colours of its LED when it is sent");
    CHECK_EQ(decode_tx(&SD0, NULL), 5);
    for (int r = 0; r < NUM_ROW; r++) {
        CHECK_EQ(dec_msgs[r].payload[0], r);
        CHECK_EQ(dec_msgs[r].payload[1 + 4 * col_in_row(r)], r + 1); /* blue */
    }

    CASE("the rows that did not fit follow in turn: a row that was sent again does not go before the others");
    setup();
    for (int r = 0; r < 3; r++) rgb_matrix_driver.set_color(led_in_row(r), 1, 1, 1);
    rgb_matrix_driver.flush(); /* row 0 goes, rows 1 and 2 wait */
    drain_all();
    rgb_matrix_driver.set_color(led_in_row(0), 2, 2, 2); /* row 0 changes again at once */
    rgb_matrix_driver.flush();                           /* room for one: row 1 first, not row 0 */
    CHECK_EQ(rows_sent(rows, 8), 2);
    CHECK_EQ(rows[0], 0);
    CHECK_EQ(rows[1], 1);
    drain_all();
    ap2_rgb_flush_pending();
    drain_all();
    ap2_rgb_flush_pending();
    k = rows_sent(rows, 8);
    CHECK_EQ(k, 4);
    CHECK_EQ(rows[2], 2);
    CHECK_EQ(rows[3], 0);

    CASE("a row that waits is sent once, with its latest colours");
    setup();
    fake_sd_set_free(&SD0, 0);
    rgb_matrix_driver.set_color(led_in_row(3), 5, 5, 5);
    rgb_matrix_driver.flush();
    rgb_matrix_driver.set_color(led_in_row(3), 6, 6, 6);
    rgb_matrix_driver.flush();
    rgb_matrix_driver.set_color(led_in_row(3), 7, 7, 7);
    rgb_matrix_driver.flush();
    fake_sd_set_free(&SD0, -1);
    ap2_rgb_flush_pending();
    ap2_rgb_flush_pending();
    CHECK_EQ(decode_tx(&SD0, NULL), 1);
    CHECK_EQ(dec_msgs[0].payload[0], 3);
    CHECK_EQ(dec_msgs[0].payload[1 + 4 * col_in_row(3)], 7);

    CASE("a row the next frame has begun to change is not sent half done");
    setup();
    fake_sd_set_free(&SD0, 0);
    rgb_matrix_driver.set_color(led_in_row(1), 5, 5, 5);
    rgb_matrix_driver.flush(); /* waits */
    fake_sd_set_free(&SD0, -1);
    rgb_matrix_driver.set_color(led_in_row(1), 8, 8, 8); /* the next frame starts rendering */
    ap2_rgb_flush_pending();                              /* the scan comes in the middle of the frame */
    CHECK_EQ(SD0.tx_len, 0);
    rgb_matrix_driver.flush();
    CHECK_EQ(decode_tx(&SD0, NULL), 1);
    CHECK_EQ(dec_msgs[0].payload[1 + 4 * col_in_row(1)], 8);
}

/* The model of the LED MCU: the colours it holds, built from the messages it received. */
static uint8_t model[NUM_ROW][NUM_COLUMN * 4];

static void model_apply(size_t from, size_t drop_row_message_index, bool drop) {
    size_t n = decode_tx(&SD0, NULL), row_messages = 0;
    for (size_t i = from; i < n; i++) {
        if (dec_msgs[i].cmd == CMD_LED_COLOR_SET_ROW) {
            if (drop && row_messages++ == drop_row_message_index) continue; /* lost on the wire */
            memcpy(model[dec_msgs[i].payload[0]], dec_msgs[i].payload + 1, NUM_COLUMN * 4);
        } else if (dec_msgs[i].cmd == CMD_LED_COLOR_SET_MONO) {
            for (int r = 0; r < NUM_ROW; r++)
                for (int c = 0; c < NUM_COLUMN; c++) memcpy(&model[r][4 * c], dec_msgs[i].payload, 4);
        }
    }
}

static bool model_matches(void) {
    for (int r = 0; r < NUM_ROW; r++) {
        if (memcmp(model[r], &led_colors[ROWCOL2IDX(r, 0)], NUM_COLUMN * 4) != 0) return false;
    }
    return true;
}

static uint32_t rng_state = 0x9e3779b9;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void case_refresh(void) {
    /* rgb_driver.c: "while the LEDs are on and nothing waits one row is sent again every AP2_LED_REFRESH_MS (each row
     * once a second by default)" */
    CASE("one row every 200 ms, in turn, each with the current colours");
    setup();
    rgb_matrix_driver.set_color(led_of(4, 0), 0x31, 0x32, 0x33);
    rgb_matrix_driver.flush();
    fake_sd_tx_clear(&SD0);
    drain_all();
    refresh_timer = timer_read32();
    for (int step = 0; step < 12; step++) {
        fake_advance(AP2_LED_REFRESH_MS - 1);
        ap2_rgb_flush_pending();
        CHECK_EQ(SD0.tx_len, 0);
        fake_advance(1);
        ap2_rgb_flush_pending();
        CHECK_EQ(SD0.tx_len, ROW_MSG);
        CHECK_EQ(decode_tx(&SD0, NULL), 1);
        CHECK_EQ(dec_msgs[0].payload[0], step % NUM_ROW);
        CHECK_EQ(memcmp(dec_msgs[0].payload + 1, &led_colors[ROWCOL2IDX(step % NUM_ROW, 0)], NUM_COLUMN * 4), 0);
        fake_sd_tx_clear(&SD0);
    }

    CASE("each row is sent once a second");
    setup();
    refresh_timer = timer_read32();
    int count[NUM_ROW] = {0};
    for (int ms = 0; ms < 5000; ms++) {
        fake_advance(1);
        ap2_rgb_flush_pending();
        if (SD0.tx_len) {
            int rows[4];
            const size_t k = rows_sent(rows, 4);
            for (size_t i = 0; i < k; i++) count[rows[i]]++;
            fake_sd_tx_clear(&SD0);
        }
    }
    for (int r = 0; r < NUM_ROW; r++) CHECK_EQ(count[r], 5);

    CASE("no refresh while the LEDs are off");
    setup();
    fake_rgb_enabled = false;
    fake_advance(10 * AP2_LED_REFRESH_MS);
    ap2_rgb_flush_pending();
    CHECK_EQ(SD0.tx_len, 0);

    CASE("no refresh while rgb_matrix sleeps with the USB host");
    setup();
    fake_rgb_suspended = true;
    fake_advance(10 * AP2_LED_REFRESH_MS);
    ap2_rgb_flush_pending();
    CHECK_EQ(SD0.tx_len, 0);
    CHECK(!ap2_rgb_lit());

    CASE("no refresh past RGB_MATRIX_TIMEOUT, and it is lit up to it");
    setup();
    fake_last_input_ms = fake_ms - RGB_MATRIX_TIMEOUT;
    CHECK(ap2_rgb_lit());
    fake_advance(1);
    CHECK(!ap2_rgb_lit());
    fake_advance(10 * AP2_LED_REFRESH_MS);
    ap2_rgb_flush_pending();
    CHECK_EQ(SD0.tx_len, 0);
    fake_last_input_ms = fake_ms; /* a key */
    CHECK(ap2_rgb_lit());
    ap2_rgb_flush_pending();
    CHECK_EQ(SD0.tx_len, ROW_MSG);

    CASE("lit: enabled, not asleep, not timed out");
    setup();
    CHECK(ap2_rgb_lit());
    fake_rgb_enabled = false;
    CHECK(!ap2_rgb_lit());

    CASE("no refresh while a row waits");
    setup();
    fake_sd_set_free(&SD0, 0);
    rgb_matrix_driver.set_color(led_in_row(2), 3, 3, 3);
    rgb_matrix_driver.flush();
    const uint8_t waiting = pending_rows;
    fake_advance(10 * AP2_LED_REFRESH_MS);
    ap2_rgb_flush_pending();
    CHECK_EQ(pending_rows, waiting);

    CASE("a lost message is healed within a second, with a static effect");
    setup();
    memset(model, 0, sizeof(model));
    for (int r = 0; r < NUM_ROW; r++) rgb_matrix_driver.set_color(led_in_row(r), (uint8_t)(r + 1), 0, 0);
    /* every scan (1 ms), a frame every 40 ms, the UART sends 11.52 bytes per ms */
    for (int ms = 0; ms < 200; ms++) {
        fake_advance(1);
        if (ms % 40 == 0) {
            for (int r = 0; r < NUM_ROW; r++) rgb_matrix_driver.set_color(led_in_row(r), (uint8_t)(r + 1), 0, 0);
            rgb_matrix_driver.flush();
        }
        ap2_rgb_flush_pending();
        fake_sd_drain(&SD0, 12);
    }
    model_apply(0, 1, true); /* the second row message is lost on the wire */
    CHECK(!model_matches());
    fake_sd_tx_clear(&SD0);
    for (int ms = 0; ms < 1000 + AP2_LED_REFRESH_MS; ms++) {
        fake_advance(1);
        if (ms % 40 == 0) {
            for (int r = 0; r < NUM_ROW; r++) rgb_matrix_driver.set_color(led_in_row(r), (uint8_t)(r + 1), 0, 0);
            rgb_matrix_driver.flush();
        }
        ap2_rgb_flush_pending();
        fake_sd_drain(&SD0, 12);
    }
    model_apply(0, 0, false);
    CHECK(model_matches());
    CHECK_EQ(SD0.overflows, 0);
}

static void case_resend_all(void) {
    CASE("resend_all: every row goes again at the next flush, also unchanged ones");
    setup();
    rgb_matrix_driver.set_color(led_of(0, 1), 1, 2, 3);
    rgb_matrix_driver.flush();
    fake_sd_tx_clear(&SD0);
    drain_all();
    ap2_rgb_resend_all();
    fake_sd_set_free(&SD0, 4000);
    rgb_matrix_driver.flush();
    int rows[8];
    CHECK_EQ(rows_sent(rows, 8), 5);
    for (int i = 0; i < 5; i++) CHECK_EQ(rows[i], i);
    CHECK_EQ(SD0.tx_len, 5 * ROW_MSG);

    CASE("resend_all: nothing goes before the flush");
    setup();
    ap2_rgb_resend_all();
    ap2_rgb_flush_pending();
    CHECK_EQ(SD0.tx_len, 0);

    CASE("resend_all: with little room the rows follow in turn");
    setup();
    ap2_rgb_resend_all();
    rgb_matrix_driver.flush();
    for (int i = 0; i < 4; i++) {
        drain_all();
        ap2_rgb_flush_pending();
    }
    CHECK_EQ(rows_sent(rows, 8), 5);
    CHECK_EQ(SD0.overflows, 0);

    CASE("without resend_all unchanged rows are not sent after the LED MCU is switched on again");
    setup();
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, 0);

    CASE("resend_all clears what is known as sent, so a row that equals the last sent one is sent");
    setup();
    ap2_rgb_resend_all();
    CHECK_EQ(sent_valid, 0);
    CHECK_EQ(dirty_rows, 0x1f);
}

static void case_set_color_all(void) {
    CASE("set_color_all: one mono message");
    setup();
    rgb_matrix_driver.set_color_all(0x12, 0x34, 0x56);
    CHECK_EQ(SD0.tx_len, PROTO_TX_MESSAGE_SIZE(4));
    CHECK_EQ(decode_tx(&SD0, NULL), 1);
    CHECK_EQ(dec_msgs[0].cmd, CMD_LED_COLOR_SET_MONO);
    CHECK_EQ(dec_msgs[0].size, 4);
    CHECK_EQ(dec_msgs[0].payload[0], 0x56); /* blue, green, red, alpha */
    CHECK_EQ(dec_msgs[0].payload[1], 0x34);
    CHECK_EQ(dec_msgs[0].payload[2], 0x12);
    CHECK_EQ(dec_msgs[0].payload[3], 0xff);
    for (int i = 0; i < KEY_COUNT; i++) CHECK_EQ(led_colors[i].rgb, 0xff123456u);

    CASE("set_color_all: afterwards nothing is dirty or waiting, and the same colour is not sent");
    CHECK_EQ(dirty_rows, 0);
    CHECK_EQ(pending_rows, 0);
    CHECK_EQ(sent_valid, 0x1f);
    fake_sd_tx_clear(&SD0);
    for (int i = 0; i < RGB_MATRIX_LED_COUNT; i++) rgb_matrix_driver.set_color(i, 0x12, 0x34, 0x56);
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, 0);
    rgb_matrix_driver.set_color(led_of(3, 2), 0, 0, 0);
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, ROW_MSG);

    CASE("set_color_all replaces rows that were waiting");
    setup();
    fake_sd_set_free(&SD0, 0);
    for (int r = 0; r < NUM_ROW; r++) rgb_matrix_driver.set_color(led_in_row(r), 4, 4, 4);
    rgb_matrix_driver.flush();
    CHECK_EQ(pending_rows, 0x1f);
    rgb_matrix_driver.set_color_all(7, 7, 7);
    CHECK_EQ(pending_rows, 0);
    fake_sd_set_free(&SD0, -1);
    fake_sd_tx_clear(&SD0);
    ap2_rgb_flush_pending();
    CHECK_EQ(SD0.tx_len, 0);

    CASE("set_color_all: a row that still holds the colour after a change and back is not sent");
    setup();
    rgb_matrix_driver.set_color_all(7, 7, 7);
    fake_sd_tx_clear(&SD0);
    rgb_matrix_driver.set_color(led_of(1, 1), 1, 1, 1);
    rgb_matrix_driver.set_color(led_of(1, 1), 7, 7, 7);
    rgb_matrix_driver.flush();
    CHECK_EQ(SD0.tx_len, 0);
}

static void case_random_frames(void) {
    /* A long run of random frames at the rates of the keyboard: the LED MCU model ends up with every colour, and the
     * UART never has to wait. */
    CASE("random animation: the model of the LED MCU follows, sdWrite() never waits");
    setup();
    memset(model, 0, sizeof(model));
    for (int ms = 0; ms < 20000; ms++) {
        fake_advance(1);
        if (ms % 40 == 0) {
            const bool burst = (ms / 40) % 7 == 0; /* an effect that changes every LED, now and then */
            for (int i = 0; i < RGB_MATRIX_LED_COUNT; i++) {
                if (burst || rng() % 9 == 0) {
                    rgb_matrix_driver.set_color(i, (uint8_t)rng(), (uint8_t)rng(), (uint8_t)rng());
                }
            }
            rgb_matrix_driver.flush();
        }
        ap2_rgb_flush_pending();
        fake_sd_drain(&SD0, 11); /* 115200 baud: 11.52 bytes per millisecond */
        if (rng() % 3 == 0) fake_sd_drain(&SD0, 1);
        if (ms % 500 == 0) { /* keep the log short */
            model_apply(0, 0, false);
            fake_sd_tx_forget(&SD0); /* the queue keeps its fill level */
        }
    }
    /* quiet: the rest goes out */
    for (int ms = 0; ms < 100; ms++) {
        fake_advance(1);
        ap2_rgb_flush_pending();
        fake_sd_drain(&SD0, 12);
    }
    model_apply(0, 0, false);
    CHECK_EQ(SD0.overflows, 0);
    CHECK_EQ(pending_rows, 0);
    CHECK(model_matches());
}

int main(void) {
    case_layout();
    case_changed_only();
    case_space();
    case_refresh();
    case_resend_all();
    case_set_color_all();
    case_random_frames();
    return TEST_RESULT();
}
