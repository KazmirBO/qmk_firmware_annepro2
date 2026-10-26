# Anne Pro 2 rev. C15 and C18 QMK firmware

An ANSI-layout 60% keyboard featuring Bluetooth support and per-key RGB lighting.

* Keyboard Maintainer: [bwisn](https://github.com/bwisn)
* Hardware Supported: Anne Pro 2, [C15](c15/) and [C18](c18/) versions
* Hardware Availability: [annepro.net](https://www.annepro.net/), [Hexcore](https://www.hexcore.xyz/annepro2)

## How to compile

After setting up your build environment, you can compile the Anne Pro 2 C18 default keymap by using:

    make annepro2/c18:default

If you want to compile the Anne Pro 2 C15 default keymap use:

    make annepro2/c15:default

## Bluetooth and battery options

Define these in the `config.h` of your keymap.

| Option | Effect |
|---|---|
| `AP2_BLE_SLOT_TAP_HOLD` | The Bluetooth keys `KC_AP2_BT1` to `KC_AP2_BT4` work as in the stock firmware: a short press connects to the paired device, holding the key for `AP2_BLE_SLOT_HOLD_MS` starts pairing and sends the keystrokes to Bluetooth, so the new host can be used as soon as it pairs. The stock firmware waits 5 s; the default here is 500 ms, set `AP2_BLE_SLOT_HOLD_MS` to 5000 for the stock timing. Without the option the first press starts pairing and a second press on the same slot connects. |
| `AP2_BT_SWITCH` | Follows the Bluetooth switch on the bottom of the keyboard, as the stock firmware does: `AP2_BT_SWITCH_PIN` (default `C10`, read with a pull-down) high selects USB, low selects Bluetooth. Only a change of the switch, stable for `AP2_BT_SWITCH_DEBOUNCE_MS` (50), selects the output, so `KC_AP2_USB` and the Bluetooth keys still work in between. The pin comes from the analysis of the stock firmware and has not been checked on hardware; on a board without the switch the pin reads low and the keyboard would type over Bluetooth, so leave the option off unless your keyboard has the switch. |
| `AP2_BLE_HANDSHAKE_TRIES` | How many times the wake-up and version request are repeated while the Bluetooth MCU has not reported its version (default 24). `annepro2_ble_is_ready()` and `annepro2_ble_version()` expose the result. A frame from the Bluetooth MCU that stops arriving is dropped after `AP2_BLE_RX_TIMEOUT_MS` (default 100). |
| `AP2_BLE_BATTERY` | C18 only. Reads the battery on PA7 and sends it to the Bluetooth MCU, so that the host can show the battery level; enables the `KC_AP2_BATT` key. Tuning: `AP2_BATTERY_INTERVAL_MS` (60000), `AP2_BATTERY_SHOW_MS` (3000), `AP2_BATTERY_SCALE_NUM` and `AP2_BATTERY_SCALE_DEN` (calibration, default 1:1). `AP2_BATTERY_EMPTY_MV` (3200) is the voltage below which 0 % is shown; readings between it and 3.6 V are stretched, the curve above 3.6 V is unchanged, and 3400 or more turns the correction off. The Bluetooth MCU only lowers the level it reports until it restarts, so the highest of the last `AP2_BATTERY_HISTORY` readings (default 5) is sent, which keeps a short voltage drop off the host. |

`KC_AP2_BATT` shows the battery level on the number row for a few seconds (one key per 10 %, green from 50 %, yellow from 20 %, red below); pressing it again hides the indicator. It does nothing unless `AP2_BLE_BATTERY` is defined.

`KC_AP2_BT_UNPAIR` deletes the bonds of all four slots, not only of the current one. Pairing a slot again replaces only the bond of that slot.

## Installing

### Get AnnePro2 Tools

If you want the executable instead of compiling it yourself, [download it here](https://ci.codetector.org/job/OpenAnnePro/job/AnnePro2-Tools/job/master/).
Windows and Linux versions are available. Otherwise, follow the steps below:

1. Install the latest stable `rust` toolchain using [rustup](https://rustup.rs/)
1. Also install [Visual Studio Community edition](https://visualstudio.microsoft.com/downloads/)
including the C/C++ module to prevent errors while compiling
1. Download or Clone the [AnnePro2-Tools](https://github.com/OpenAnnePro/AnnePro2-Tools) project.
1. Compile the tool using
   ```bash
   cargo build --release
   ```
   The compiled tool should be in `./target/release/annepro2_tools` (In later I will refer to this as `annepro2_tools`)

### Flashing the firmware

1. Put the keyboard into DFU/IAP mode by unplugging the keyboard, then holding ESC while plugging it back in.
1. Run annepro2_tools with the firmware you just built.  
   **Please substitute with the correct paths and correct bin file if you chose another keymap profile.**
   ```bash
   annepro2_tools annepro2_c15_default.bin
   ```

If the tool can't find the keyboard please double check you have the keyboard in IAP mode.


See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).
