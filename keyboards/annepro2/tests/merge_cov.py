#!/usr/bin/env python3
"""Merge the gcov JSON files of the per-program coverage builds (see coverage.sh) by source line.

usage: merge_cov.py <outdir> <repo-root>
Reads <outdir>/<program>/*.gcov.json.gz, writes <outdir>/report.txt and prints it.
Lines and branches are merged by (file, line): a line counts as covered when any program ran it; a branch counts as
covered when any program took it. The programs that compile another option setting (the build variants in VARIANTS)
are used only for lines the other programs do not compile, so that a line compiled twice is counted once.
"""
import glob, gzip, json, os, sys, collections

out, root = sys.argv[1], sys.argv[2]
FILES = ["annepro2.c", "annepro2_ble.c", "annepro2_battery.c", "rgb_driver.c", "ap2_led.c", "protocol.c",
         "keymaps/kazmir_bo/keymap.c"]
VARIANTS = ("test_idle_scan_off", "test_idle_both_off", "test_idle_awake_off", "test_battery_no_correction",
            "test_unpair_now", "test_slot_broadcast", "test_c15_set", "test_no_fallback")
kbd = os.path.join(root, "keyboards/annepro2") + "/"

def rel(path):
    path = os.path.realpath(path)
    return path[len(os.path.realpath(kbd)) + 1:] if path.startswith(os.path.realpath(kbd) + "/") else None

# data[file] = {"lines": {n: count}, "branches": {n: [count,...]}, "funcs": {name: [start, end, count]}}
data = {f: {"lines": {}, "branches": {}, "funcs": {}, "vlines": set()} for f in FILES}

def load(program):
    for j in glob.glob(os.path.join(out, program, "*.gcov.json.gz")):
        with gzip.open(j, "rt") as fh:
            d = json.load(fh)
        for f in d["files"]:
            name = rel(os.path.join(d["current_working_directory"], f["file"]))
            if name not in data:
                continue
            yield name, f

programs = sorted(p for p in os.listdir(out) if os.path.isdir(os.path.join(out, p)))
base = [p for p in programs if p not in VARIANTS]
variant = [p for p in programs if p in VARIANTS]

for group, is_variant in ((base, False), (variant, True)):
    for p in group:
        for name, f in load(p):
            D = data[name]
            for fn in f["functions"]:
                key = fn["name"]
                cur = D["funcs"].setdefault(key, [fn["start_line"], fn["end_line"], 0])
                cur[2] += fn["execution_count"]
            for l in f["lines"]:
                n = l["line_number"]
                if is_variant and n in D["lines"] and n not in D["vlines"]:
                    # compiled by the base programs: only add counts
                    D["lines"][n] += l["count"]
                    continue
                if is_variant:
                    D["vlines"].add(n)
                D["lines"][n] = D["lines"].get(n, 0) + l["count"]
                br = [b["count"] for b in l["branches"]]
                if br:
                    old = D["branches"].get(n)
                    if old is None or (is_variant and n in D["vlines"] and len(old) != len(br)):
                        D["branches"][n] = br
                    elif len(old) == len(br):
                        D["branches"][n] = [a + b for a, b in zip(old, br)]

lines_out = []
def P(s=""):
    lines_out.append(s)

P("%-28s %9s %7s   %9s %7s   %s" % ("file", "lines", "%", "branches", "%", "functions covered"))
tot = [0, 0, 0, 0, 0, 0]
for f in FILES:
    D = data[f]
    ln = len(D["lines"]); lc = sum(1 for c in D["lines"].values() if c > 0)
    bn = sum(len(b) for b in D["branches"].values()); bc = sum(1 for b in D["branches"].values() for c in b if c > 0)
    fn = len(D["funcs"]); fc = sum(1 for v in D["funcs"].values() if v[2] > 0)
    for i, v in enumerate((ln, lc, bn, bc, fn, fc)):
        tot[i] += v
    P("%-28s %4d/%-4d %6.1f%%   %4d/%-4d %6.1f%%   %d/%d" % (f, lc, ln, 100.0 * lc / max(ln, 1), bc, bn, 100.0 * bc / max(bn, 1), fc, fn))
P("%-28s %4d/%-4d %6.1f%%   %4d/%-4d %6.1f%%   %d/%d" % ("all files", tot[1], tot[0], 100.0 * tot[1] / tot[0], tot[3], tot[2], 100.0 * tot[3] / tot[2], tot[5], tot[4]))

def ranges(nums):
    r = []
    for n in sorted(nums):
        if r and n <= r[-1][1] + 1:
            r[-1][1] = n
        else:
            r.append([n, n])
    return r

for f in FILES:
    D = data[f]
    P()
    P("== %s" % f)
    P("uncovered functions (never entered):")
    for name, (s, e, c) in sorted(D["funcs"].items(), key=lambda kv: kv[1][0]):
        if c == 0:
            P("   %s  lines %d-%d" % (name, s, e))
    P("uncovered lines inside covered functions:")
    for name, (s, e, c) in sorted(D["funcs"].items(), key=lambda kv: kv[1][0]):
        if c == 0:
            continue
        unc = [n for n, cnt in D["lines"].items() if s <= n <= e and cnt == 0]
        if unc:
            P("   %s (%d-%d): %s" % (name, s, e, ", ".join("%d" % a if a == b else "%d-%d" % (a, b) for a, b in ranges(unc))))
    inside = [(v[0], v[1]) for v in D["funcs"].values()]
    orphan = [n for n, cnt in D["lines"].items() if cnt == 0 and not any(a <= n <= b for a, b in inside)]
    if orphan:
        P("uncovered lines in no function gcov lists (gcov drops some functions): " + ", ".join("%d" % a if a == b else "%d-%d" % (a, b) for a, b in ranges(orphan)))
    ub = [(n, b) for n, b in sorted(D["branches"].items()) if any(c == 0 for c in b) and D["lines"].get(n, 0) > 0]
    P("lines run whose branches are not all taken (line: taken/total):")
    P("   " + ", ".join("%d:%d/%d" % (n, sum(1 for c in b if c), len(b)) for n, b in ub))

text = "\n".join(lines_out) + "\n"
open(os.path.join(out, "report.txt"), "w").write(text)
sys.stdout.write(text)
