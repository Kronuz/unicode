# AGENTS.md — unicode

## Repo map

```
unicode.hh             category_of, tolower, is_* helpers, next_utf8, append_utf8.
unicode_tables.hh      Generated two-stage tables. Never edit by hand.
tools/gen_tables.py    Generator and --check against a reference dump.
test/test.cc           Table digest, spot checks, UTF-8 round trip, optional full dump check.
test/diff_test.cc      Differential test (CTest `unicode_diff`): decoding, encoding and the tables against the frozen original (62M checks; `--full` sweeps every code point). Includes a window sweep and a guard-page run that crashes on any read past `end`.
test/unicode_old.hh    Frozen unicode.hh at d80b6bb, namespace unicode_old. The oracle of diff_test.cc and the baseline of bench/bench.cc. Never edit it.
tools/mutants.py       Breaks the decoder in 24 ways and requires test/diff_test.cc to fail on each.
fuzz/fuzz_utf8.cc      libFuzzer: decoding stays in bounds, advances, equals the frozen decoder, and round-trips.
bench/bench.cc         ns per code point against the frozen copy, over text of several shapes, each decoder in eight loop alignments (optionally one more corpus from a file).
docs/unicode.d2     README diagram; render: d2 --layout elk --sketch --theme 302 docs/unicode.d2 docs/unicode.svg
docs/perf/next-utf8.md  Method, numbers (Apple M4 Pro, Apple M1 Pro, Intel i9-9980HK) and rejected variants of the ASCII window.
CHANGELOG.md           Dated changes, with measurements.
```

## Invariants

- `unicode::category` enumerators keep their order; it equals Xapian's category numbering.
- `next_utf8` never reads at or past `end` and always advances at least one byte. Its
  result equals the frozen original's for every byte string, which `test/diff_test.cc`
  checks (the bytes past `end` hold valid continuations there, so a read past `end`
  changes the answer). After touching the decoder, run `tools/mutants.py`.
- `next_utf8` tests eight bytes at once for ASCII (the last seven bytes of the text: the first
  byte) and sends everything else through `detail::decode_utf8`, which is out of line, starts on
  a 64-byte boundary and returns its result in registers. Do not replace the window with a test of
  the first byte alone: that is faster on English and about 30% slower on arbitrary bytes,
  because one more branch becomes unpredictable (see the README). Keep `decode_utf8` out of
  line and aligned: at an arbitrary address the i9 ran Cyrillic and CJK 20% to 25% slower. The
  window is a speed heuristic: whatever the other seven bytes hold, a window that passes has an
  ASCII first byte, which decodes as itself. Dropping only the first or only the second byte from
  the mask changes no result, so `tools/mutants.py` drops both.
- Measure a decoder change in `bench/bench.cc`, never in one loop, and check the assembly of the
  loop (the call must stay out of the ASCII path): the same code runs up to 30% apart
  depending on where its loop lands. The bench times eight alignments of each loop; the
  alignment of the out-of-line function is one copy, so also build the bench with
  `-falign-functions=64` and without it.
- After regenerating the tables, update the digest in `test/test.cc` only once the
  tables are verified against a reference.

## Verify a change

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```
