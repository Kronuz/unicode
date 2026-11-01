# AGENTS.md — unicode

## Repo map

```
unicode.hh             category_of, tolower, is_* helpers, next_utf8, append_utf8.
unicode_tables.hh      Generated two-stage tables. Never edit by hand.
tools/gen_tables.py    Generator and --check against a reference dump.
test/test.cc           Table digest, spot checks, UTF-8 round trip, optional full dump check.
test/diff_test.cc      Differential test (CTest `unicode_diff`): decoding, encoding and the tables against the frozen original (62M checks; `--full` sweeps every code point).
test/unicode_old.hh    Frozen unicode.hh at d80b6bb, namespace unicode_old. The oracle of diff_test.cc and the baseline of bench/bench.cc. Never edit it.
tools/mutants.py       Breaks the decoder in 17 ways and requires test/diff_test.cc to fail on each.
fuzz/fuzz_utf8.cc      libFuzzer: decoding stays in bounds, advances, equals the frozen decoder, and round-trips.
bench/bench.cc         ns per code point against the frozen copy, over text of several shapes (optionally one more corpus from a file).
docs/unicode.d2     README diagram; render: d2 --layout elk --sketch --theme 302 docs/unicode.d2 docs/unicode.svg
CHANGELOG.md           Dated changes, with measurements.
```

## Invariants

- `unicode::category` enumerators keep their order; it equals Xapian's category numbering.
- `next_utf8` never reads at or past `end` and always advances at least one byte. Its
  result equals the frozen original's for every byte string, which `test/diff_test.cc`
  checks (the bytes past `end` hold valid continuations there, so a read past `end`
  changes the answer). After touching the decoder, run `tools/mutants.py`.
- The ASCII test comes first in `next_utf8` and is inline; the multi-byte part is
  `detail::next_utf8_multibyte`. Text that is mostly ASCII decodes several times faster
  that way; arbitrary non-UTF-8 bytes decode a little slower (see the README).
- After regenerating the tables, update the digest in `test/test.cc` only once the
  tables are verified against a reference.

## Verify a change

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```
