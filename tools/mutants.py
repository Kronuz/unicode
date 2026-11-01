#!/usr/bin/env python3
"""Mutation check for the UTF-8 decoder.

For each mutant: copy unicode.hh with one deliberate bug into a scratch dir, build
test/diff_test.cc against it (with the frozen original) and require that the test FAILS or
crashes. A mutant that survives means the differential test would not catch that bug.

usage: tools/mutants.py TREE_DIR [CXX] [only-name ...]     (scratch dir: $MUT_DIR or a temp dir)
"""
import os
import shutil
import subprocess
import sys
import tempfile

tree = os.path.abspath(sys.argv[1])
cxx = sys.argv[2] if len(sys.argv) > 2 else "c++"
only = set(sys.argv[3:])
src = open(os.path.join(tree, "unicode.hh"), encoding="utf-8").read()

MUTANTS = {
    # a window of ASCII advances two
    "window_advance": ("\t\tif ((window & 0x8080808080808080ull) == 0) {\n\t\t\t++p;", "\t\tif ((window & 0x8080808080808080ull) == 0) {\n\t\t\tp += 2;"),
    # the window test is inverted
    "window_invert": ("if ((window & 0x8080808080808080ull) == 0) {", "if ((window & 0x8080808080808080ull) != 0) {"),
    # the window test ignores the first two bytes (a lead byte followed by a continuation passes as itself)
    "window_skips_two": ("(window & 0x8080808080808080ull)", "(window & 0x8080808080800000ull)"),
    # the window may read one byte past `end`
    "window_end_seven": ("if (!std::is_constant_evaluated() && end - p >= 8) {", "if (!std::is_constant_evaluated() && end - p >= 7) {"),
    # the window may read many bytes past `end`
    "window_end_one": ("if (!std::is_constant_evaluated() && end - p >= 8) {", "if (!std::is_constant_evaluated() && end - p >= 1) {"),
    # an ASCII byte in the last seven bytes advances two
    "tail_advance": ("\t} else if (b0 < 0x80) {\n\t\t++p;", "\t} else if (b0 < 0x80) {\n\t\tp += 2;"),
    # the result of the slow path is dropped (p stays)
    "step_not_stored": ("\tp = step.next;", "\tp = p;"),
    # 0xC1 starts a two-byte sequence
    "lead_c1": ("if (b0 >= 0xC2 && b0 <= 0xDF) {", "if (b0 >= 0xC1 && b0 <= 0xDF) {"),
    # 0xF4 no longer starts a four-byte sequence
    "lead_f4": ("} else if (b0 >= 0xF0 && b0 <= 0xF4) {", "} else if (b0 >= 0xF0 && b0 <= 0xF3) {"),
    # 0xC0 counts as a continuation byte
    "cont_c0": ("if (c1 < 0x40) {", "if (c1 <= 0x40) {"),
    # a two-byte sequence may read the byte at `end`
    "end_two": ("\t\tif (left >= 2) {", "\t\tif (left >= 1) {"),
    # a three-byte sequence may read past `end`
    "end_three": ("\t\tif (left >= 3) {", "\t\tif (left >= 2) {"),
    # a four-byte sequence may read past `end`
    "end_four": ("\t\tif (left >= 4) {", "\t\tif (left >= 3) {"),
    # the second continuation byte of a three-byte sequence is not checked
    "cont_second": ("if ((c1 | c2) < 0x40) {", "if (c1 < 0x40) {"),
    # the third continuation byte of a four-byte sequence is not checked
    "cont_third": ("if ((c1 | c2 | c3) < 0x40) {", "if ((c1 | c2) < 0x40) {"),
    # an overlong three-byte sequence of U+0800 is refused
    "overlong_three": ("if (c >= 0x800) {", "if (c > 0x800) {"),
    # an overlong four-byte sequence of U+10000 is refused
    "overlong_four": ("if (c >= 0x10000 && c <= max_code_point) {", "if (c > 0x10000 && c <= max_code_point) {"),
    # U+10FFFF is refused
    "max_code_point": ("if (c >= 0x10000 && c <= max_code_point) {", "if (c >= 0x10000 && c < max_code_point) {"),
    # the payload of a three-byte sequence is shifted wrongly
    "shift_three": ("char32_t c = char32_t(((b0 & 0x0F) << 12) | (c1 << 6) | c2);", "char32_t c = char32_t(((b0 & 0x0F) << 12) | (c1 << 5) | c2);"),
    # a two-byte sequence advances three
    "advance_two": ("p + 2};", "p + 3};"),
    # a three-byte sequence advances two
    "advance_three": ("p + 3};", "p + 2};"),
    # a four-byte sequence advances three
    "advance_four": ("p + 4};", "p + 3};"),
    # an invalid byte decodes as something else than itself
    "fallback_value": ("\treturn {b0, p + 1};", "\treturn {b0 | 0x100, p + 1};"),
    # an invalid byte advances two
    "fallback_advance": ("\treturn {b0, p + 1};", "\treturn {b0, p + 2};"),
}

if len(MUTANTS) < 20:
    raise SystemExit("mutant table shrank")

work = os.environ.get("MUT_DIR") or tempfile.mkdtemp(prefix="unicode_mutants_")
os.makedirs(work, exist_ok=True)
survivors = []
for name, (old, new) in MUTANTS.items():
    if only and name not in only:
        continue
    if src.count(old) != 1:
        print(f"{name}: pattern matches {src.count(old)} times, fix the table")
        survivors.append(name)
        continue
    d = os.path.join(work, name)
    os.makedirs(d, exist_ok=True)
    shutil.copy(os.path.join(tree, "unicode_tables.hh"), d)
    with open(os.path.join(d, "unicode.hh"), "w", encoding="utf-8") as f:
        f.write(src.replace(old, new))
    exe = os.path.join(d, "diff_test")
    cmd = [cxx, "-std=c++20", "-O1", "-I" + d, "-I" + os.path.join(tree, "test"),
           os.path.join(tree, "test/diff_test.cc"), "-o", exe]
    built = subprocess.run(cmd, capture_output=True, text=True)
    if built.returncode != 0:
        if any(m in built.stderr for m in ("static assertion failed", "non-constant condition for static assertion",
                                           "static assertion expression is not")):
            print(f"{name}: killed (a static_assert in the test fails)")
            shutil.rmtree(d, ignore_errors=True)
            continue
        print(f"{name}: does not compile\n{built.stderr[:400]}")
        survivors.append(name)
        continue
    try:
        ran = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        print(f"{name}: killed (the test does not finish)")
        shutil.rmtree(d, ignore_errors=True)
        continue
    if ran.returncode == 0:
        print(f"{name}: SURVIVED")
        survivors.append(name)
    else:
        print(f"{name}: killed (exit {ran.returncode})")
    shutil.rmtree(d, ignore_errors=True)

if survivors:
    print("survivors:", ", ".join(survivors))
    sys.exit(1)
print("all mutants killed")
