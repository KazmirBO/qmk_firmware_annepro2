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
#    include "rgb_matrix.h"
#    include "ap2_led.h"

static uint8_t led_pos[RGB_MATRIX_LED_COUNT];
static uint8_t led_row[RGB_MATRIX_LED_COUNT];

/* Bitmask of rows with pending color changes; bit N = row N dirty */
static uint8_t dirty_rows = 0;

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
    for (uint8_t row = 0; row < NUM_ROW; row++) {
        if (dirty_rows & (1 << row)) {
            ap2_led_colors_set_row(row);
            dirty_rows &= ~(1 << row);
        }
    }
}

static inline ap2_led_t pack_color(uint8_t r, uint8_t g, uint8_t b) {
    return (ap2_led_t){.p.blue = b, .p.green = g, .p.red = r, .p.alpha = 0xff};
}

static void set_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (led_row[index] == 0xff) return;
    led_colors[led_pos[index]] = pack_color(r, g, b);
    dirty_rows |= (1 << led_row[index]);
}

static void set_color_all(uint8_t r, uint8_t g, uint8_t b) {
    ap2_led_colors_set_mono(pack_color(r, g, b));
    dirty_rows = 0;
}

const rgb_matrix_driver_t rgb_matrix_driver = {
    .init          = init,
    .flush         = flush,
    .set_color     = set_color,
    .set_color_all = set_color_all,
};

#endif
