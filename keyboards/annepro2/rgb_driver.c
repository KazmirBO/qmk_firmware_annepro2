/* Copyright 2022 Jose Pablo Ramirez <jp.ramangulo@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef RGB_MATRIX_ENABLE

#    include <string.h>
#    include "hal.h"
#    include "rgb_matrix.h"
#    include "ap2_led.h"
#    include "protocol.h"
#    include "timer.h"

static uint8_t led_pos[RGB_MATRIX_LED_COUNT];
static uint8_t led_row[RGB_MATRIX_LED_COUNT];

/* Rows whose colours changed since the last flush; bit N = row N. */
static uint8_t dirty_rows = 0;

/*
 * The LED UART is slower than the effects: a full frame is 5 rows of 62 bytes, about 27 ms at 115200 baud, and
 * rgb_matrix flushes every 16 ms. Writing it all at once filled the 128-byte output queue and sdWrite() then blocked
 * the matrix scan. Now:
 * - set_color() marks a row only when a colour really changes, so a static effect (solid colour too, which sets every
 *   LED in every frame) sends nothing after its first frame;
 * - a row is sent only when its whole message fits in the queue, leaving room for the short commands (masks, sticky
 *   keys, key presses) so that they never wait either;
 * - the rows that did not fit go out from ap2_rgb_flush_pending() as soon as there is room, in turn, so every row gets
 *   its share when the UART cannot keep up. A row that the next frame has already started to change waits for that
 *   frame's flush, so a half rendered row is never sent;
 * - the protocol has no acknowledgement, so while nothing waits one row is sent again every AP2_LED_REFRESH_MS (each
 *   row once a second by default): a lost message or a reset of the LED MCU heals even with a static effect.
 */
#    define ROW_MESSAGE_SIZE (5 + 1 + NUM_COLUMN * sizeof(ap2_led_t)) /* protocol header, row number, colours */
#    ifndef AP2_LED_TX_RESERVE
#        define AP2_LED_TX_RESERVE 16
#    endif
_Static_assert(SERIAL_BUFFERS_SIZE >= ROW_MESSAGE_SIZE + AP2_LED_TX_RESERVE, "the LED UART queue cannot take a row");

#    ifndef AP2_LED_REFRESH_MS
#        define AP2_LED_REFRESH_MS 200
#    endif

#    define ALL_ROWS ((1 << NUM_ROW) - 1)

static uint8_t  pending_rows; /* flushed but not sent yet */
static uint8_t  next_row;     /* where the round robin goes on */
static uint8_t  refresh_row;
static uint32_t refresh_timer;

void ap2_rgb_flush_pending(void) {
    if (!pending_rows && timer_elapsed32(refresh_timer) >= AP2_LED_REFRESH_MS) {
        refresh_timer = timer_read32();
        pending_rows  = 1 << refresh_row;
        refresh_row   = (refresh_row + 1) % NUM_ROW;
    }
    const uint8_t ready = pending_rows & ~dirty_rows;
    if (!ready) return;
    for (uint8_t i = 0; i < NUM_ROW; i++) {
        const uint8_t row = next_row;
        if (ready & (1 << row)) {
            if (ap2_led_tx_space() < ROW_MESSAGE_SIZE + AP2_LED_TX_RESERVE) return; /* this row first, next time */
            ap2_led_colors_set_row(row);
            pending_rows &= ~(1 << row);
        }
        next_row = (row + 1) % NUM_ROW;
    }
}

void ap2_rgb_resend_all(void) { dirty_rows = ALL_ROWS; }

static void init(void) {
    memset(led_row, 0xff, sizeof(led_row));
    unsigned int i = 0;
    for (unsigned int y = 0; y < NUM_ROW; y++) {
        for (unsigned int x = 0; x < NUM_COLUMN; x++) {
            if (g_led_config.matrix_co[y][x] != NO_LED) {
                led_pos[g_led_config.matrix_co[y][x]] = i;
                led_row[g_led_config.matrix_co[y][x]] = y;
            }
            i++;
        }
    }
}

static void flush(void) {
    pending_rows |= dirty_rows; /* a row still waiting is sent once, with its latest colours */
    dirty_rows = 0;
    ap2_rgb_flush_pending();
}

static inline ap2_led_t pack_color(uint8_t r, uint8_t g, uint8_t b) {
    return (ap2_led_t){.p.blue = b, .p.green = g, .p.red = r, .p.alpha = 0xff};
}

static void set_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (led_row[index] == 0xff) return;
    const ap2_led_t color = pack_color(r, g, b);
    ap2_led_t      *led   = &led_colors[led_pos[index]];
    if (led->rgb == color.rgb) return; /* unchanged: nothing to send */
    *led = color;
    dirty_rows |= (1 << led_row[index]);
}

static void set_color_all(uint8_t r, uint8_t g, uint8_t b) {
    ap2_led_colors_set_mono(pack_color(r, g, b));
    dirty_rows   = 0;
    pending_rows = 0; /* the single colour replaces every row, also the ones still waiting */
}

const rgb_matrix_driver_t rgb_matrix_driver = {
    .init          = init,
    .flush         = flush,
    .set_color     = set_color,
    .set_color_all = set_color_all,
};

#endif
