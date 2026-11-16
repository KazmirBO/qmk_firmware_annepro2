/* Force-included in every test (-include). The options of the c18 build with keymaps/kazmir_bo, as c18/config.h and the
 * keymap config.h set them, plus what the build system would define for the QMK features the keymap turns on. */
#pragma once
#include <stdint.h>

#define NKRO_ENABLE
#define EXTRAKEY_ENABLE
#define RGB_MATRIX_ENABLE
#define RGB_MATRIX_SLEEP
#define RGB_MATRIX_TIMEOUT 180000
#define SERIAL_BUFFERS_SIZE 128 /* the ChibiOS default that halconf.h of the keyboard leaves in place */

#define AP2_HAS_BATTERY_ADC
#define AP2_HAS_BT_SWITCH
#define AP2_BLE_SLOT_TAP_HOLD
#define AP2_BT_SWITCH
#define AP2_BLE_BATTERY
#define AP2_BLE_SERIAL

/* On the keyboard this is the flash address 0x3E00 (c18/config.h). A host process cannot read that address, so the
 * device record is a block of memory the test fills; the sources read it at AP2_DEVICE_RECORD_ADDR + 8. */
extern uint8_t fake_device_record[8 + 16 + 8];
#define AP2_DEVICE_RECORD_ADDR ((uintptr_t)fake_device_record)

/* generated from info.json: MATRIX_ROWS, MATRIX_COLS, RGB_MATRIX_LED_COUNT, AP2_TEST_MATRIX_CO */
#include "led_layout.h"
