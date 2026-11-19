/* Fake hardware and fake QMK for the host-side tests of the Anne Pro 2 sources.
 *
 * The keyboard sources include hal.h, ch.h, quantum.h, usb_main.h, rgb_matrix.h and keyboard.h. Those names are
 * stand-in headers in this directory that all lead here. Where a real QMK header is small and self-contained, the real
 * one is used (report.h, host.h, host_driver.h, led.h, suspend.h, timer.h, keycode.h, util.h, rgb_matrix_types.h and
 * rgb_matrix_drivers.h), so that a clash with QMK's own names is caught by the compiler.
 *
 * What the test can control is declared in the second half of this file. fake_reset() puts everything back. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Real QMK headers. */
#include "keycode.h"
#include "util.h"
#include "report.h"
#include "host_driver.h"
#include "host.h"
#include "led.h"
#include "suspend.h"
#include "timer.h"
#include "rgb_matrix_types.h"
#include "rgb_matrix_drivers.h"

/* ------------------------------------------------------------------------------------------------------------------
 * ChibiOS stand-ins (hal.h, ch.h)
 * ---------------------------------------------------------------------------------------------------------------- */

typedef int32_t msg_t;
#define MSG_OK 0
#define MSG_TIMEOUT (-1)
#define TIME_IMMEDIATE 0
/* A debug build of ChibiOS stops on a failed check; the release build of the keyboard does nothing. The fake does
 * nothing, as the keyboard does, so that the clamp that follows a check in the sources is what is tested. */
#define chDbgCheck(c) ((void)(c))

#define FAKE_SD_RX_MAX 65536
#define FAKE_SD_TX_MAX 65536

typedef struct {
    uint16_t capacity;      /* size of the output queue, SERIAL_BUFFERS_SIZE on the keyboard */
    uint32_t used;          /* bytes written and not drained by fake_sd_drain() yet */
    int32_t  free_override; /* >= 0: the free space the driver reports, whatever was written */
} output_queue_t;

typedef struct {
    uint32_t speed;
} SerialConfig;

typedef struct {
    output_queue_t oqueue;
    uint8_t        rx[FAKE_SD_RX_MAX]; /* bytes the other MCU "sent", read by sdGetTimeout() */
    size_t         rx_head, rx_tail;
    uint8_t        tx[FAKE_SD_TX_MAX]; /* every byte written since the last fake_sd_tx_clear() */
    size_t         tx_len;
    uint32_t       overflows; /* writes that did not fit: on the keyboard sdWrite() would have blocked */
    bool           overflow_ok; /* set by fake_sd_allow_overflow() for the current case */
    uint32_t       starts;
    uint32_t       speed;
} SerialDriver;

extern SerialDriver SD0; /* the LED MCU */
extern SerialDriver SD1; /* the BLE MCU */

void   sdStart(SerialDriver *sd, const SerialConfig *cfg);
size_t sdWrite(SerialDriver *sd, const uint8_t *buf, size_t n);
msg_t  sdGetTimeout(SerialDriver *sd, uint32_t timeout);
msg_t  sdGet(SerialDriver *sd);
bool   sdGetWouldBlock(SerialDriver *sd);
uint16_t oqGetEmptyI(output_queue_t *oq);
#define osalSysLock() ((void)0)
#define osalSysUnlock() ((void)0)

/* Clock control unit registers used by the battery code. The bit values are arbitrary here: the tests do not check
 * which bit does what, which is not known without the Holtek manual. */
typedef struct {
    uint32_t APBCFGR;
    uint32_t APBCCR1;
} CKCU_TypeDef;
extern CKCU_TypeDef fake_ckcu;
#define CKCU (&fake_ckcu)
#define CKCU_APBCFGR_ADCDIV_MASK (7u << 16)
#define CKCU_APBCCR1_ADCEN (1u << 24)

typedef int fake_port_t;
extern fake_port_t fake_ioporta;
#define IOPORTA (&fake_ioporta)
#define PAL_MODE_INPUT_ANALOG 3
void palSetPadMode(fake_port_t *port, unsigned pad, unsigned mode);

#define C10 10
void gpio_set_pin_input_low(int pin);
bool gpio_read_pin(int pin);

void NVIC_SystemReset(void);
#define __disable_irq() ((void)0)

/* The ADC registers sit at 0x40010000 in the sources. The harness maps a page of ordinary memory there, so the
 * battery code reads and writes it as it would the registers. */
#define FAKE_ADC_BASE 0x40010000u
#define FAKE_ADC_REG(off) (*(volatile uint32_t *)(FAKE_ADC_BASE + (off)))

/* USB driver (usb_main.h): only the state the sources look at. The values are the ones of ChibiOS. */
typedef enum {
    USB_UNINIT    = 0,
    USB_STOP      = 1,
    USB_READY     = 2,
    USB_SELECTED  = 3,
    USB_ACTIVE    = 4,
    USB_SUSPENDED = 5,
} usbstate_t;
typedef struct {
    usbstate_t state;
    usbstate_t saved_state;
    uint16_t   status;
} USBDriver;
extern USBDriver USBD1;
#define USB_DRIVER USBD1
void usbWakeupHost(USBDriver *usbp);

/* ------------------------------------------------------------------------------------------------------------------
 * QMK stand-ins (quantum.h, rgb_matrix.h, keyboard.h)
 * ---------------------------------------------------------------------------------------------------------------- */

typedef struct {
    struct {
        struct {
            uint8_t row, col;
        } key;
        bool pressed;
    } event;
} keyrecord_t;

typedef union {
    uint16_t raw;
    struct {
        bool nkro : 1;
    };
} keymap_config_t;
extern keymap_config_t keymap_config;

typedef uint32_t layer_state_t;
extern layer_state_t layer_state;
extern report_keyboard_t *keyboard_report;

void    wait_ms(uint32_t ms);
void    clear_keyboard(void);
uint8_t get_mods(void);
void    set_mods(uint8_t mods);

void keyboard_pre_init_user(void);
void keyboard_post_init_user(void);
void matrix_scan_user(void);
void housekeeping_task_user(void);
bool process_record_user(uint16_t keycode, keyrecord_t *record);
layer_state_t layer_state_set_kb(layer_state_t state);
layer_state_t layer_state_set_user(layer_state_t state);
bool          led_update_user(led_t led_state);

/* rgb_matrix: the features are the ones of the keymap, the state is a variable of the test. */
#define RGB_MATRIX_EFFECT_MAX 40
#define RGB_MATRIX_DEFAULT_MODE 1
extern rgb_config_t rgb_matrix_config;
extern led_config_t g_led_config;
bool    rgb_matrix_is_enabled(void);
bool    rgb_matrix_get_suspend_state(void);
uint8_t rgb_matrix_get_mode(void);
void    rgb_matrix_enable_noeeprom(void);
void    rgb_matrix_disable_noeeprom(void);
void    eeconfig_force_flush_rgb_matrix(void);
void    rgb_matrix_increase_hue(void);
void    rgb_matrix_decrease_hue(void);
void    rgb_matrix_increase_sat(void);
void    rgb_matrix_decrease_sat(void);
void    rgb_matrix_increase_val(void);
void    rgb_matrix_decrease_val(void);
void    rgb_matrix_increase_speed(void);
void    rgb_matrix_decrease_speed(void);
void    rgb_matrix_step(void);
void    rgb_matrix_step_reverse(void);

typedef uint16_t matrix_row_t;
matrix_row_t fake_matrix_row(uint8_t row);
#define matrix_get_row(row) fake_matrix_row(row)
void reset_keyboard(void);

uint32_t last_input_activity_elapsed(void);

/* ------------------------------------------------------------------------------------------------------------------
 * What a test controls and reads
 * ---------------------------------------------------------------------------------------------------------------- */

/* Reset every fake: the clock to FAKE_START_MS, the serial ports (queues empty, output queue SERIAL_BUFFERS_SIZE), the
 * USB driver (active, remote wakeup allowed), the host driver (QMK's USB driver, chibios_driver), the ADC (a
 * conversion that never finishes), the device record (blank), the counters. The static state inside the sources is
 * not touched: a test that includes a source file resets it itself. */
#define FAKE_START_MS 100000u
void fake_reset(void);

/* The millisecond clock. timer_read(), timer_read32() and the elapsed functions read it; wait_ms() advances it. */
extern uint32_t fake_ms;
void            fake_advance(uint32_t ms);
/* The time of the last key, for last_input_activity_elapsed(). */
extern uint32_t fake_last_input_ms;

/* Serial ports. */
void   fake_sd_rx_push(SerialDriver *sd, const void *bytes, size_t n);
size_t fake_sd_rx_pending(const SerialDriver *sd);
void   fake_sd_tx_clear(SerialDriver *sd); /* forget what was written and empty the output queue */
void   fake_sd_tx_forget(SerialDriver *sd); /* forget what was written, the output queue keeps its fill level */
void   fake_sd_set_capacity(SerialDriver *sd, uint16_t capacity);
void   fake_sd_set_free(SerialDriver *sd, int32_t free_bytes); /* fixed free space; -1 goes back to capacity - used */
/* A write that does not fit fails the program (TEST_RESULT()) unless the case allowed it; fake_reset() ends the
 * permission. fake_overflow_failures counts the unexpected ones over the whole program. */
void            fake_sd_allow_overflow(SerialDriver *sd);
extern uint32_t fake_overflow_failures;
void   fake_sd_drain(SerialDriver *sd, uint32_t n);            /* the UART sent n bytes */

/* The device record the BLE serial number is taken from (the UDID is at +8, 16 bytes). Blank (zeros) after reset. */
extern uint8_t fake_device_record[8 + 16 + 8];

/* Which host driver QMK has set, and the state of the host. */
extern host_driver_t chibios_driver; /* QMK's USB driver; the keyboard replaces its senders (usb_wrap_driver()) */
extern led_t         fake_host_led_state;
extern uint8_t       fake_mods;
extern bool          fake_rgb_enabled;
extern bool          fake_rgb_suspended;
extern bool          fake_pin_c10;
extern matrix_row_t  fake_matrix[MATRIX_ROWS]; /* the debounced matrix: a held key is a bit here */

/* What the fake USB senders (the ones the keyboard's wrappers call) last received. */
extern report_keyboard_t fake_usb_last_keyboard;

/* Calls counted by the fakes. */
typedef struct {
    int clear_keyboard;
    int suspend_power_down;
    int suspend_wakeup_init;
    int layer_state_set_kb;
    int led_update_kb;
    int usb_wakeup_host;
    int rgb_enable, rgb_disable, rgb_other;
    int host_set_driver;
    int usb_keyboard, usb_nkro, usb_mouse, usb_extra;
    int wait_ms;
    uint32_t wait_ms_total;
    int nvic_reset;
    int reset_keyboard;
    int housekeeping_user;
    int eeconfig_flush;
    int matrix_scan_user, process_record_user;
    int pre_init_user, post_init_user;   /* the weak defaults count; a keymap's own hooks do not */
    int pin_input_low, pin_input_low_pin; /* gpio_set_pin_input_low(): calls and the last pin */
    size_t clear_keyboard_sd1_at;         /* SD1.tx_len at the last clear_keyboard() */
    const char *rgb_last;                 /* name of the last rgb_matrix_* adjustment called */
} fake_calls_t;
extern fake_calls_t fake_calls;
