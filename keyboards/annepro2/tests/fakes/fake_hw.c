/* Implementation of the fakes declared in fake_hw.h. Linked into every test program. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "fake_hw.h"

/* ---- state ------------------------------------------------------------------------------------------------------ */

SerialDriver      SD0, SD1;
CKCU_TypeDef      fake_ckcu;
fake_port_t       fake_ioporta;
USBDriver         USBD1;
keymap_config_t   keymap_config;
layer_state_t     layer_state;
report_keyboard_t fake_keyboard_report_storage;
report_keyboard_t *keyboard_report = &fake_keyboard_report_storage;
rgb_config_t      rgb_matrix_config;
led_config_t      g_led_config = {.matrix_co = AP2_TEST_MATRIX_CO};
led_t             fake_host_led_state;
uint8_t           fake_mods;
bool              fake_rgb_enabled;
bool              fake_rgb_suspended;
bool              fake_pin_c10;
matrix_row_t      fake_matrix[MATRIX_ROWS];
uint32_t          fake_ms;
uint32_t          fake_last_input_ms;
uint8_t           fake_device_record[8 + 16 + 8];
report_keyboard_t fake_usb_last_keyboard;
fake_calls_t      fake_calls;

static host_driver_t *current_driver;

/* ---- the ADC page ----------------------------------------------------------------------------------------------- */

__attribute__((constructor)) static void map_adc(void) {
    void *p = mmap((void *)(uintptr_t)FAKE_ADC_BASE, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != (void *)(uintptr_t)FAKE_ADC_BASE) {
        fprintf(stderr, "fake_hw: cannot map the ADC registers at 0x%x\n", FAKE_ADC_BASE);
        exit(2);
    }
}

/* ---- serial ports ----------------------------------------------------------------------------------------------- */

void sdStart(SerialDriver *sd, const SerialConfig *cfg) {
    sd->starts++;
    sd->speed = cfg->speed;
}

size_t sdWrite(SerialDriver *sd, const uint8_t *buf, size_t n) {
    if (n > oqGetEmptyI(&sd->oqueue)) sd->overflows++; /* the real driver would wait here until the UART made room */
    if (sd->tx_len + n > FAKE_SD_TX_MAX) {
        fprintf(stderr, "fake_hw: serial output log full\n");
        exit(2);
    }
    memcpy(sd->tx + sd->tx_len, buf, n);
    sd->tx_len += n;
    sd->oqueue.used += n;
    return n;
}

msg_t sdGetTimeout(SerialDriver *sd, uint32_t timeout) {
    (void)timeout; /* only TIME_IMMEDIATE is used: the byte is there or it is not */
    if (sd->rx_head == sd->rx_tail) return MSG_TIMEOUT;
    return sd->rx[sd->rx_head++];
}

msg_t sdGet(SerialDriver *sd) {
    return sdGetTimeout(sd, 0);
}

bool sdGetWouldBlock(SerialDriver *sd) {
    return sd->rx_head == sd->rx_tail;
}

uint16_t oqGetEmptyI(output_queue_t *oq) {
    if (oq->free_override >= 0) return (uint16_t)oq->free_override;
    return oq->used >= oq->capacity ? 0 : (uint16_t)(oq->capacity - oq->used);
}

void fake_sd_rx_push(SerialDriver *sd, const void *bytes, size_t n) {
    if (sd->rx_head == sd->rx_tail) sd->rx_head = sd->rx_tail = 0; /* everything was read: start at the front */
    if (sd->rx_tail + n > FAKE_SD_RX_MAX) {
        fprintf(stderr, "fake_hw: serial input queue full\n");
        exit(2);
    }
    memcpy(sd->rx + sd->rx_tail, bytes, n);
    sd->rx_tail += n;
}

size_t fake_sd_rx_pending(const SerialDriver *sd) {
    return sd->rx_tail - sd->rx_head;
}

void fake_sd_tx_clear(SerialDriver *sd) {
    sd->tx_len      = 0;
    sd->oqueue.used = 0;
}

void fake_sd_tx_forget(SerialDriver *sd) {
    sd->tx_len = 0;
}

void fake_sd_set_capacity(SerialDriver *sd, uint16_t capacity) {
    sd->oqueue.capacity = capacity;
}

void fake_sd_set_free(SerialDriver *sd, int32_t free_bytes) {
    sd->oqueue.free_override = free_bytes;
}

void fake_sd_drain(SerialDriver *sd, uint32_t n) {
    sd->oqueue.used = n >= sd->oqueue.used ? 0 : sd->oqueue.used - n;
}

/* ---- pins, registers -------------------------------------------------------------------------------------------- */

void palSetPadMode(fake_port_t *port, unsigned pad, unsigned mode) {
    (void)port;
    (void)pad;
    (void)mode;
}
void gpio_set_pin_input_low(int pin) {
    (void)pin;
}
bool gpio_read_pin(int pin) {
    (void)pin;
    return fake_pin_c10;
}
void NVIC_SystemReset(void) {
    fake_calls.nvic_reset++;
}

/* ---- USB -------------------------------------------------------------------------------------------------------- */

void usbWakeupHost(USBDriver *usbp) {
    (void)usbp;
    fake_calls.usb_wakeup_host++;
}

/* QMK's USB driver: the leds and the four senders. The keyboard replaces the senders by its wrappers, which call the
 * functions named send_keyboard, send_nkro, send_mouse and send_extra as they are declared in annepro2.c. */
static uint8_t usb_leds(void) {
    return fake_host_led_state.raw;
}
void send_keyboard(report_keyboard_t *report) {
    fake_calls.usb_keyboard++;
    fake_usb_last_keyboard = *report;
}
void send_nkro(report_nkro_t *report) {
    (void)report;
    fake_calls.usb_nkro++;
}
void send_mouse(report_mouse_t *report) {
    (void)report;
    fake_calls.usb_mouse++;
}
void send_extra(report_extra_t *report) {
    (void)report;
    fake_calls.usb_extra++;
}
host_driver_t chibios_driver = {usb_leds, send_keyboard, send_nkro, send_mouse, send_extra};

/* ---- QMK -------------------------------------------------------------------------------------------------------- */

void host_set_driver(host_driver_t *driver) {
    fake_calls.host_set_driver++;
    current_driver = driver;
}
host_driver_t *host_get_driver(void) {
    return current_driver;
}
led_t host_keyboard_led_state(void) {
    return fake_host_led_state;
}
void wait_ms(uint32_t ms) {
    fake_calls.wait_ms++;
    fake_calls.wait_ms_total += ms;
    fake_ms += ms;
}
void clear_keyboard(void) {
    fake_calls.clear_keyboard++;
}
uint8_t get_mods(void) {
    return fake_mods;
}
void set_mods(uint8_t mods) {
    fake_mods = mods;
}
__attribute__((weak)) void keyboard_pre_init_user(void) {}
__attribute__((weak)) void keyboard_post_init_user(void) {}
__attribute__((weak)) void matrix_scan_user(void) {
    fake_calls.matrix_scan_user++;
}
__attribute__((weak)) void housekeeping_task_user(void) {
    fake_calls.housekeeping_user++;
}
__attribute__((weak)) void suspend_wakeup_init_user(void) {}
void suspend_power_down_quantum(void) {
    fake_calls.suspend_power_down++;
}
void suspend_wakeup_init_quantum(void) {
    fake_calls.suspend_wakeup_init++;
}
__attribute__((weak)) bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;
    (void)record;
    fake_calls.process_record_user++;
    return true;
}
layer_state_t layer_state_set_kb(layer_state_t state) {
    fake_calls.layer_state_set_kb++;
    return state;
}
bool led_update_kb(led_t led_state) {
    (void)led_state;
    fake_calls.led_update_kb++;
    return true;
}

bool rgb_matrix_is_enabled(void) {
    return fake_rgb_enabled;
}
bool rgb_matrix_get_suspend_state(void) {
    return fake_rgb_suspended;
}
uint8_t rgb_matrix_get_mode(void) {
    return rgb_matrix_config.mode;
}
void rgb_matrix_enable_noeeprom(void) {
    fake_calls.rgb_enable++;
    fake_rgb_enabled = true;
}
void rgb_matrix_disable_noeeprom(void) {
    fake_calls.rgb_disable++;
    fake_rgb_enabled = false;
}
void eeconfig_force_flush_rgb_matrix(void) {
    fake_calls.eeconfig_flush++;
}
void rgb_matrix_increase_hue(void) { fake_calls.rgb_other++; }
void rgb_matrix_decrease_hue(void) { fake_calls.rgb_other++; }
void rgb_matrix_increase_sat(void) { fake_calls.rgb_other++; }
void rgb_matrix_decrease_sat(void) { fake_calls.rgb_other++; }
void rgb_matrix_increase_val(void) { fake_calls.rgb_other++; }
void rgb_matrix_decrease_val(void) { fake_calls.rgb_other++; }
void rgb_matrix_increase_speed(void) { fake_calls.rgb_other++; }
void rgb_matrix_decrease_speed(void) { fake_calls.rgb_other++; }
void rgb_matrix_step(void) { fake_calls.rgb_other++; }
void rgb_matrix_step_reverse(void) { fake_calls.rgb_other++; }

matrix_row_t fake_matrix_row(uint8_t row) {
    return fake_matrix[row];
}
void reset_keyboard(void) {
    fake_calls.reset_keyboard++;
}

uint32_t last_input_activity_elapsed(void) {
    return fake_ms - fake_last_input_ms;
}

/* ---- timer ------------------------------------------------------------------------------------------------------ */

volatile uint32_t timer_count;

uint16_t timer_read(void) {
    return (uint16_t)fake_ms;
}
uint32_t timer_read32(void) {
    return fake_ms;
}
uint16_t timer_elapsed(uint16_t last) {
    return TIMER_DIFF_16(timer_read(), last);
}
uint32_t timer_elapsed32(uint32_t last) {
    return TIMER_DIFF_32(timer_read32(), last);
}
void fake_advance(uint32_t ms) {
    fake_ms += ms;
}

/* ---- reset ------------------------------------------------------------------------------------------------------ */

static void sd_reset(SerialDriver *sd) {
    memset(sd, 0, sizeof(*sd));
    sd->oqueue.capacity      = SERIAL_BUFFERS_SIZE;
    sd->oqueue.free_override = -1;
}

void fake_reset(void) {
    fake_ms            = FAKE_START_MS;
    fake_last_input_ms = FAKE_START_MS;
    sd_reset(&SD0);
    sd_reset(&SD1);
    memset(fake_device_record, 0, sizeof(fake_device_record));
    memset(&fake_calls, 0, sizeof(fake_calls));
    memset(&fake_usb_last_keyboard, 0, sizeof(fake_usb_last_keyboard));
    memset(&fake_keyboard_report_storage, 0, sizeof(fake_keyboard_report_storage));
    memset(&keymap_config, 0, sizeof(keymap_config));
    memset(&fake_ckcu, 0, sizeof(fake_ckcu));
    memset((void *)(uintptr_t)FAKE_ADC_BASE, 0, 4096); /* ISR bit 0 clear: a conversion never ends */
    memset(&rgb_matrix_config, 0, sizeof(rgb_matrix_config));
    memset(&fake_host_led_state, 0, sizeof(fake_host_led_state));
    layer_state        = 0;
    fake_mods          = 0;
    fake_rgb_enabled   = false;
    fake_rgb_suspended = false;
    fake_pin_c10       = false;
    memset(fake_matrix, 0, sizeof(fake_matrix));
    USBD1.state        = USB_ACTIVE;
    USBD1.saved_state  = USB_ACTIVE;
    USBD1.status       = 2; /* remote wakeup allowed (USB_GETSTATUS_REMOTE_WAKEUP_ENABLED) */
    /* QMK's own senders: annepro2.c replaces them in usb_wrap_driver(); a test that wants the wrapped driver calls
     * it. */
    chibios_driver.send_keyboard = send_keyboard;
    chibios_driver.send_nkro     = send_nkro;
    chibios_driver.send_mouse    = send_mouse;
    chibios_driver.send_extra    = send_extra;
    current_driver               = &chibios_driver;
}
