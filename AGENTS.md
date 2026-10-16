# AGENTS.md — unicode

## Repo map

```
unicode.hh             category_of, tolower, is_* helpers, next_utf8, append_utf8.
unicode_tables.hh      Generated two-stage tables. Never edit by hand.
tools/gen_tables.py    Generator and --check against a reference dump.
test/test.cc           Table digest, spot checks, UTF-8 round trip, optional full dump check.
fuzz/fuzz_utf8.cc      libFuzzer: decoding stays in bounds, advances, and round-trips.
bench/bench.cc         ns per code point over a UTF-8 file.
docs/unicode.d2     README diagram; render: d2 --layout elk --sketch --theme 302 docs/unicode.d2 docs/unicode.svg
```

## Invariants

- `unicode::category` enumerators keep their order; it equals Xapian's category numbering.
- `next_utf8` never reads at or past `end` and always advances at least one byte.
- After regenerating the tables, update the digest in `test/test.cc` only once the
  tables are verified against a reference.

## Verify a change

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```
