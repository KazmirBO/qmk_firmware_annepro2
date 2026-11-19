# Host-side tests of the Anne Pro 2 firmware

The keyboard cannot be tested on hardware by the people who change its code, so the logic of the shared sources
(`annepro2.c`, `annepro2_ble.c`, `annepro2_battery.c`, `rgb_driver.c`, `ap2_led.c`, `protocol.c`) is checked on the
host. The tests compile the real source files, unchanged, with gcc for x86-64 (`-std=gnu11 -Wall -Wextra`,
AddressSanitizer and UndefinedBehaviorSanitizer) against fake hardware.

## Run

    keyboards/annepro2/tests/run.sh            # everything
    keyboards/annepro2/tests/run.sh battery    # only the programs whose name contains "battery"
    keyboards/annepro2/tests/coverage.sh       # line and branch coverage of the firmware sources, as a table
    AP2_TEST_WERROR=1 keyboards/annepro2/tests/run.sh   # as CI runs it: a compiler warning fails the build

It needs bash, gcc and python3, uses no network, builds into `tests/build/` (ignored by git) and works from any
directory. It needs Linux on x86-64 (the ADC registers and the RAM word of the bootloader are fixed addresses mapped
with `MAP_FIXED_NOREPLACE`) and gcc with AddressSanitizer; it does not build on macOS. `CC` names the compiler (default
gcc, a command with arguments is allowed), `AP2_TEST_TIMEOUT` the seconds a program may run (default 120). No test
program builds with a compiler warning; a program that does gets ", N compiler warning(s)" on its line, named in its
log, and with `AP2_TEST_WERROR=1` (set by CI) a warning fails the build of that program. `run.sh` first runs
`check_drift.py` (see below). It prints one line per test program and a summary, and exits non-zero when a program fails
to build, fails a check, crashes (a sanitizer report counts) or does not finish in time. The log of each program is
`tests/build/<name>.log`. Exit status 0: all passed; 1: a program failed, did not build or timed out, or
`check_drift.py` found a difference; 2: no program matches the words, or gcc or python3 is missing.

`coverage.sh` builds every program with `gcov` instrumentation, runs it and merges the results by source line
(`merge_cov.py`); the table and the lines and functions that no test reaches are in `tests/build/coverage/report.txt`.
Lines count as covered when any program runs them; the build variants (below) only add lines that the other programs do
not compile.

## What is here

| File | Purpose |
|---|---|
| `run.sh` | builds and runs every `test_*.c`, after `check_drift.py` |
| `check_drift.py` | fails when a `static` variable of a source is not reset in `reset_firmware()`, or when an option of `fakes/test_config.h` differs from the keyboard's `config.h` |
| `coverage.sh`, `merge_cov.py` | line and branch coverage of the six sources and the keymap |
| `check.h` | the check macros (`CHECK`, `CHECK_EQ`, `CHECK_BYTES`, `CHECK_KNOWN`), no framework |
| `all_sources.h` | included by every test: the fakes plus the real sources in one translation unit, `reset_firmware()` and a static assert of the matrix size |
| `ble_frames.h` | `bf_send()` and `bf_version()`: feed a frame of the BLE MCU to the real parser |
| `proto_decode.h` | decodes the bytes written to the LED UART with the real receiver of `protocol.c` |
| `fakes/` | stand-in headers (`hal.h`, `ch.h`, `quantum.h`, ...), `fake_hw.h`/`fake_hw.c`, `test_config.h` |
| `gen_led_layout.py` | turns the LED layout of `info.json` into a header, so the tests use the real layout |
| `test_*.c` | one test program per topic (see the comment at the top of each) |

The fakes in `fakes/fake_hw.h` are what a test controls: the serial ports SD0 (LED MCU) and SD1 (BLE MCU) are byte
queues (`fake_sd_rx_push()` feeds what the other MCU sends, `SD1.tx` holds what the keyboard wrote, the free space of
the output queue is set with `fake_sd_set_capacity()`, `fake_sd_set_free()` and `fake_sd_drain()`, and a write that does
not fit is counted in `overflows`: on the keyboard `sdWrite()` would block, so it fails the program unless the case says
`fake_sd_allow_overflow()` for that port; a test that loops over time drains with `fake_sd_drain()`); the millisecond
clock is `fake_ms` (`fake_advance()`, `wait_ms()` advances it); the USB driver is the variable `USBD1`; the ADC
registers are a page of memory mapped at 0x40010000, and the RAM word that `bootloader_jump()` writes is a page mapped
at 0x20001000; the device record is `fake_device_record`; the QMK functions the code calls are counted in `fake_calls`.
Where a real QMK header is small and self-contained (`report.h`, `host.h`, `host_driver.h`, `led.h`, `suspend.h`,
`timer.h`, `keycode.h`, `util.h`, `rgb_matrix_types.h`, `rgb_matrix_drivers.h`) the real one is used.

The options are those of the c18 build with `keymaps/kazmir_bo` (`fakes/test_config.h`); `check_drift.py` fails the run
when an `AP2_*` option, `RGB_MATRIX_TIMEOUT` or `RGB_MATRIX_SLEEP` there differs from `c18/config.h` or the keymap's
`config.h`, or one of those files defines such an option that `fakes/test_config.h` does not (the device record address
differs on purpose). If a keymap option changes what the shared code does, add a
variant (see `test_ble_tx_shared_ep.c` and `test_battery_no_correction.c`: a small file that defines the option and
includes or repeats the test).

## Add a test

1. Make `tests/test_<topic>.c`, start it with a comment saying what behaviour it pins and where that behaviour is
   documented (a code comment, `readme.md`, `docs/`).
2. `#include "all_sources.h"` (and `"proto_decode.h"` if you decode the LED UART). Everything static in the sources is
   visible, so a test may call `ble_rx_byte()` or read `dirty_rows` directly.
3. Start every test case with `reset_firmware()` (it also calls `fake_reset()`), then set up the fakes, call the code,
   check. Name the case with `CASE("...")`.
4. End `main()` with `return TEST_RESULT();`. `run.sh` finds the file by its name.
5. Prove that the test can fail: change the firmware source (flip a condition), see the test fail, undo the change.

A new `static` variable in a source file has to be added to `reset_firmware()` in `all_sources.h`, under the same `#if`
as its declaration, or state can leak between test cases; `check_drift.py` fails the run when one is missing (it knows
two tables that are set once and are not state).

The only change of a firmware file for the tests is `#ifndef AP2_ADC_REG` in `annepro2_battery.c`: a test that defines
the macro before `all_sources.h` replaces the access to the ADC registers (`test_battery_samples.c` hands out samples of
its own choice). The seam does not change the images of the keyboard.

A check that is expected to fail on the current code, because the code looks wrong, is written as `CHECK_KNOWN(cond,
"what was seen")`: it does not fail the run, it is listed as KNOWN in the output, and when the code is fixed it is
listed as FIXED, which is the signal to turn it into a plain `CHECK`.

## What this cannot show

- There is no hardware. Timing is the fake clock: nothing here measures real speed, stack use, interrupts, DMA or the
  behaviour of the real USB peripheral and its suspend and resume interrupts.
- There are no real MCUs on the other end of the UARTs. The BLE MCU (TI CC254x, stock firmware) and the LED MCU (Shine)
  are replaced by bytes the test writes and reads. The tests show that the keyboard sends the frames the notes describe
  and reads the frames the notes describe; whether the real MCUs behave as the notes say is not tested.
- The register bit meanings of the Holtek ADC are not checked (the values the code writes are pinned to the stock
  firmware's values); the trimmed mean is tested with sample sequences through the `AP2_ADC_REG` seam
  (`test_battery_samples.c`).
- The sources are compiled for x86-64: an error that depends on the 32-bit Cortex-M0+ (alignment, `int` size, the linker
  script, RAM use) is not seen. Build the firmware (`make annepro2/c18:kazmir_bo`) for that.
- ChibiOS, the QMK matrix, debounce, `rgb_matrix.c` effects and the real USB stack are not part of it; they are faked
  (`fakes/`) or left out. The keys of the keymaps are tested through `process_record_kb()`, not through the QMK code
  that calls it (tapping, layers).

## Tests of a keymap and of the build options

A test may include a keymap after `all_sources.h` (see `test_keymap_boot.c`, which compiles `keymaps/kazmir_bo/keymap.c`
and drives its `process_record_user()` and `matrix_scan_user()`): define `QMK_KEYBOARD_H`, an empty `LAYOUT_60_ansi()`
and the layer macros first, and include the `config.h` of the keymap. The user callbacks of the fakes are weak, as in
QMK, so the keymap's own replace them. The debounced matrix is `fake_matrix[]`, `reset_keyboard()` is counted in
`fake_calls.reset_keyboard`. Build options that change the code are tested as small variant files that define the option
and then include a test (`test_idle_scan_off.c`, `test_idle_both_off.c`, `test_idle_awake_off.c`) or carry their own
cases (`test_unpair_now.c`, `test_slot_broadcast.c`, `test_no_fallback.c` and `test_c15_set.c`, which `#undef` or
`#define` the option before `all_sources.h`).

The tests of the 2026-11-16 fixes are `test_fix_usbble.c` and `test_fix_led.c`; each case names its finding (USB-1 to
USB-5, BLE-1, CFG-4, LED-1 to LED-4, LED-N1) in its `CASE()` text. CFG-1 (`QK_BOOT`) is tested in `test_keymap_boot.c`,
and the `test_idle_*.c` files are build variants for USB-1, USB-2 and USB-5 (each names its findings in its first line).

