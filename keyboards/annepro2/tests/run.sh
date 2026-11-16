#!/bin/bash
# Build and run every host-side test of the Anne Pro 2 firmware. See README.md.
#   tests/run.sh            all test programs (tests/test_*.c)
#   tests/run.sh ble parser only the programs whose name contains one of the words
# Needs gcc, python3 and bash; no network. Output goes to tests/build/ (ignored by git).
set -e
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
kbd=$(cd "$here/.." && pwd)
root=$(cd "$kbd/../.." && pwd)
build=$here/build
mkdir -p "$build"

CC=${CC:-gcc}
CFLAGS="-std=gnu11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined"
CFLAGS="$CFLAGS -fno-omit-frame-pointer"
# The fakes come first; the real QMK headers that are small and self-contained are used as they are (as system headers,
# so that QMK's own warnings do not show).
INCS="-I$here/fakes -I$build -I$kbd -isystem $root/tmk_core/protocol -isystem $root/quantum"
INCS="$INCS -isystem $root/quantum/rgb_matrix -isystem $root/platforms"
python3 "$here/gen_led_layout.py" "$kbd/info.json" "$build/led_layout.h"

pass=0
fail=0
total_checks=0
total_known=0
for src in "$here"/test_*.c; do
    name=$(basename "$src" .c)
    if [ $# -gt 0 ]; then
        match=no
        for word in "$@"; do
            case $name in *"$word"*) match=yes ;; esac
        done
        [ $match = yes ] || continue
    fi
    log=$build/$name.log
    if ! $CC $CFLAGS $INCS -include "$here/fakes/test_config.h" -o "$build/$name" "$src" \
            "$here/fakes/fake_hw.c" > "$log" 2>&1; then
        printf 'FAIL %-28s does not build, see %s\n' "$name" "${log#$root/}"
        grep -A6 'error' "$log" | sed "s#$root/##g" | head -n 40
        fail=$((fail + 1))
        continue
    fi
    warnings=$(grep -c 'warning:' "$log" || true)
    set +e
    "$build/$name" >> "$log" 2>&1
    status=$?
    set -e
    result=$(grep '^RESULT ' "$log" | tail -n 1 || true)
    checks=$(echo "$result" | sed -n 's/.* checks=\([0-9]*\).*/\1/p')
    known=$(echo "$result" | sed -n 's/.* known=\([0-9]*\).*/\1/p')
    if [ $status -eq 0 ] && [ -n "$result" ]; then
        extra=""
        [ "${known:-0}" -gt 0 ] && extra=", $known known failure(s)"
        [ "$warnings" -gt 0 ] && extra="$extra, $warnings compiler warning(s)"
        printf 'ok   %-28s %4s checks%s\n' "$name" "$checks" "$extra"
        pass=$((pass + 1))
        total_checks=$((total_checks + checks))
        total_known=$((total_known + known))
        grep -E '^  (KNOWN|FIXED)' "$log" | sed 's/^/       /' || true
    else
        printf 'FAIL %-28s (exit status %d), log %s\n' "$name" $status "${log#$root/}"
        grep -v '^RESULT' "$log" | sed "s#$root/##g" | tail -n 40
        fail=$((fail + 1))
    fi
done

echo "----"
echo "$pass test program(s) passed, $fail failed, $total_checks checks, $total_known known failure(s)"
[ $fail -eq 0 ]
