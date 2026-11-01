# MCU
MCU = cortex-m0plus
ARMV = 6
USE_FPU = no
MCU_FAMILY = HT32
MCU_SERIES = HT32F523xx
MCU_LDSCRIPT = HT32F52342_ANNEPRO2_C15
MCU_STARTUP = ht32f523xx

BOARD = ANNEPRO2_C15

# Bootloader selection
BOOTLOADER = custom
PROGRAM_CMD = annepro2_tools --boot $(BUILD_DIR)/$(TARGET).bin

# Keep the keyboard task running while the USB host sleeps, so that Bluetooth keeps working; annepro2.c powers
# down and wakes the USB host itself (usb_suspend_task).
NO_USB_STARTUP_CHECK = yes

# Anne Pro 2
SRC = \
	annepro2_ble.c \
	ap2_led.c \
	protocol.c \
	rgb_driver.c \
