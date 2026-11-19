#!/bin/bash
# Line and branch coverage of the firmware sources by the host-side tests. See README.md.
#   tests/coverage.sh        builds every tests/test_*.c with gcc --coverage (-O0, same fakes and options as run.sh),
#                            runs it, and merges the gcov data of all programs by source line with merge_cov.py
# Needs gcc with gcov and python3. Output goes to tests/build/coverage/; the table is printed and kept in
# tests/build/coverage/report.txt, which also lists the uncovered functions and lines. Exit status 1 when a program
# fails or does not build, 2 when gcc, gcov or python3 is missing.
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
kbd=$(cd "$here/.." && pwd)
root=$(cd "$kbd/../.." && pwd)
out=$here/build/coverage
for tool in gcc gcov python3; do
    command -v "$tool" > /dev/null || { echo "coverage.sh: $tool not found" >&2; exit 2; }
done
rm -rf "$out"
mkdir -p "$out"
python3 "$here/gen_led_layout.py" "$kbd/info.json" "$out/led_layout.h" || exit 1
INCS=("-I$here/fakes" "-I$out" "-I$kbd" -isystem "$root/tmk_core/protocol" -isystem "$root/quantum"
      -isystem "$root/quantum/rgb_matrix" -isystem "$root/platforms")
failed=0
for src in "$here"/test_*.c; do
    name=$(basename "$src" .c)
    dir=$out/$name
    mkdir -p "$dir"
    if ! (cd "$dir" && gcc -std=gnu11 -w -g -O0 --coverage "${INCS[@]}" -include "$here/fakes/test_config.h" \
            -o "$name" "$src" "$here/fakes/fake_hw.c" && ./"$name" > run.log 2>&1); then
        echo "coverage.sh: $name failed or does not build, see ${dir#"$root"/}/run.log" >&2
        failed=1
    fi
    (cd "$dir" && gcov --json-format -b -o . ./*.gcda > /dev/null 2>&1)
done
python3 "$here/merge_cov.py" "$out" "$root"
exit $failed
