#!/usr/bin/env python3
"""Two drift checks, run by run.sh before the tests are built (see README.md).

1. Every file-scope, non-const static variable of the firmware sources is reset in reset_firmware() (all_sources.h).
   A static that is not reset leaks state from one test case to the next. Two names are tables that are set once
   and never change, so they are not state (ALLOWED).
2. Every AP2_* option and RGB_MATRIX_TIMEOUT / RGB_MATRIX_SLEEP that fakes/test_config.h defines is defined with the
   same value in c18/config.h or keymaps/kazmir_bo/config.h, the build the tests stand for. Otherwise the tests would
   check a configuration that nobody builds. And every such option of those two files is in test_config.h, otherwise
   the tests would keep checking the default of an option that the build has changed.

Exit status 0 when both hold, 1 with the names when not."""
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
kbd = os.path.dirname(here)
SOURCES = ["annepro2.c", "annepro2_ble.c", "annepro2_battery.c", "ap2_led.c", "protocol.c", "rgb_driver.c"]
# set once and never written afterwards: a driver table and the lookup table that init() of rgb_driver.c builds
ALLOWED = {"ap2_ble_driver", "led_pos"}
# differs on purpose: the keyboard reads flash, the host test reads a block of memory
OPTION_EXCEPTIONS = {"AP2_DEVICE_RECORD_ADDR"}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def statics():
    names = set()
    for f in SOURCES:
        text = strip_comments(open(os.path.join(kbd, f), encoding="utf-8").read())
        for m in re.finditer(r"^static\s+struct\s*\{[^}]*\}\s*(\w+)", text, re.M):
            names.add((f, m.group(1)))
        for line in text.splitlines():
            head = line.split("=")[0]
            m = re.match(r"static\s+(?!const\b)(?!struct\s*\{)[\w\s\*]+?\b(\w+)\s*(\[[^\]]*\])?\s*(=[^;]*)?;\s*$", line)
            if m and "(" not in head:
                names.add((f, m.group(1)))
            # several names on one line: static uint8_t history_count, history_pos;
            m = re.match(r"static\s+(?!const\b)\w+\s+(\w+(\s*,\s*\w+)+)\s*;", line)
            if m:
                names.update((f, n.strip()) for n in m.group(1).split(","))
    return names


def defines(path):
    out = {}
    for line in strip_comments(open(path, encoding="utf-8").read()).splitlines():
        m = re.match(r"\s*#\s*define\s+(\w+)(?:[ \t]+(.*))?$", line)
        if m:
            out[m.group(1)] = " ".join((m.group(2) or "").split())
    return out


def main():
    problems = []
    all_sources = open(os.path.join(here, "all_sources.h"), encoding="utf-8").read()
    body = strip_comments(all_sources[all_sources.index("static void reset_firmware(void)"):])
    found = statics()
    for f, n in sorted(found):
        if n not in ALLOWED and not re.search(r"\b%s\b" % re.escape(n), body):
            problems.append("%s: static %s is not reset in reset_firmware() (tests/all_sources.h)" % (f, n))

    test = defines(os.path.join(here, "fakes", "test_config.h"))
    real = defines(os.path.join(kbd, "c18", "config.h"))
    real.update(defines(os.path.join(kbd, "keymaps", "kazmir_bo", "config.h")))
    for name, value in sorted(test.items()):
        if not (name.startswith("AP2_") or name in ("RGB_MATRIX_TIMEOUT", "RGB_MATRIX_SLEEP")):
            continue
        if name in OPTION_EXCEPTIONS:
            continue
        if name not in real:
            problems.append("tests/fakes/test_config.h defines %s, c18/config.h and keymaps/kazmir_bo/config.h do not"
                            % name)
        elif real[name] != value:
            problems.append("%s is '%s' in tests/fakes/test_config.h but '%s' in the keyboard's config.h"
                            % (name, value, real[name]))

    for name, value in sorted(real.items()):
        if not (name.startswith("AP2_") or name in ("RGB_MATRIX_TIMEOUT", "RGB_MATRIX_SLEEP")):
            continue
        if name not in OPTION_EXCEPTIONS and name not in test:
            problems.append("%s is defined in the keyboard's config.h but not in tests/fakes/test_config.h" % name)

    for p in problems:
        print("check_drift: " + p, file=sys.stderr)
    return 1 if problems else 0


sys.exit(main())
