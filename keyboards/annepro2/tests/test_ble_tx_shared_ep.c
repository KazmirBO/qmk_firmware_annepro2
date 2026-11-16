/* test_ble_tx.c with KEYBOARD_SHARED_EP: the keyboard report of QMK starts with a report ID, which must not be sent to
 * the BLE MCU (annepro2_ble.c: "With KEYBOARD_SHARED_EP the report starts with a report ID"). */
#define KEYBOARD_SHARED_EP
#include "test_ble_tx.c"
