# unicode against other decoders

Measured 2026-11-01 with `bench/bench.cc` at `unicode:a022d97`. The README table shows eight of the rows below.

## What is timed

`unicode_bench --compare` decodes 2 MB of each kind of text and reports nanoseconds per code point. "decode" adds up the code points. "pipeline" also adds `category_of` and `tolower` of each code point, the loop of a tokenizer. The rows are the nine kinds of text of `bench/bench.cc`: seven are well-formed UTF-8, and two ("English read as Latin-1", one stray byte `C0` to `FF` in forty; "Random bytes") are not.

## Implementations

| column | what it runs | version |
| --- | --- | --- |
| unicode | `unicode::next_utf8(p, end)`, `category_of`, `tolower` | this repository |
| utf8cpp | `utf8::next(p, end)`: checks the sequence and the end, throws on bad input | [utf8cpp](https://github.com/nemtrif/utfcpp) v4.2.1 |
| utf8cpp unchecked | `utf8::unchecked::next(p)`: trusts that the input is valid UTF-8 and has no end to check | utf8cpp v4.2.1 |
| Hoehrmann | the table-driven DFA decoder of Bjoern Hoehrmann ([description](https://bjoern.hoehrmann.de/utf-8/decoder/dfa/)), written out in `bench/bench.cc` from that description with its tables built at compile time; a rejected sequence reads as U+FFFD | not versioned |
| utf8proc | `utf8proc_iterate`, `utf8proc_category` (mapped to this library's category numbers by a 30-entry table), `utf8proc_tolower` | [utf8proc](https://github.com/JuliaStrings/utf8proc) v2.11.3 (Unicode 17, as `unicode_tables.hh`) |

utf8cpp and Hoehrmann have no category or case tables, so their pipeline columns decode with them and look up `category_of` and `tolower` in this library's tables: the difference from the `unicode` column is the decoder alone. utf8proc's pipeline uses its own tables.

utf8proc and utf8cpp are fetched at those tags by `-DUNICODE_BENCH_COMPARE=ON` and linked into `unicode_bench` only. utf8proc is a C library: it is compiled by the same compiler driver with the same optimization flags as the C++ (`-O3 -DNDEBUG`), as a static library, so `utf8proc_iterate` is a call, as it is for any user of the library. The other three inline into the loop.

## Same results

- The bench stops with an error if the sum of any comparator differs from `unicode`'s on an input, before it times that input.
- `unicode_bench --check` (CTest `unicode_bench_check`) compares each decoder with `unicode.hh` on all 1,114,112 code points: the category and lowercase mapping (surrogates included), and the decoding of the encoded form (surrogates are not UTF-8). It also decodes every well-formed input of the bench code point by code point. All four comparators agree, which includes utf8proc 2.11.3's tables with the tables here.
- The two inputs with invalid bytes run `unicode` alone. The comparators reject invalid UTF-8 (an error, an exception, or U+FFFD) where `next_utf8` reads each bad byte as Latin-1, so their sums would differ by design.

## Method

- Every decoder runs in eight copies of the loop, each shifted by 8 bytes, and a cell is the mean over the eight alignments of the fastest of 24 passes (eight rounds of three). The same loop runs up to 30% apart depending on where the compiler places it (see [next-utf8.md](next-utf8.md)).
- Rounds alternate between the decoders, so a slow stretch of the host lands on all of them. "A/A" is the largest difference, over the decoders of a row, between the mean of the even rounds and the mean of the odd rounds.
- Each machine ran the table once, with the machine to itself. The largest A/A is 22% on the M4 Pro and 5% on the i9: a cell whose row shows a large A/A can move by that much between runs.
- Build: Apple clang 17.0.0, CMake Release (`-O3 -DNDEBUG`), `-std=gnu++20`.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DUNICODE_BENCH_COMPARE=ON
cmake --build build --target unicode_bench
./build/unicode_bench --check      # results only
./build/unicode_bench --compare    # these tables
```

Without `-DUNICODE_BENCH_COMPARE=ON` the bench times `unicode` and Hoehrmann's decoder (and the frozen original of [next-utf8.md](next-utf8.md)). `./build/unicode_bench` alone also runs the loops of that note.

## Apple M4 Pro, macOS 26.7 (arm64)

Apple clang 17.0.0 (clang-1700.3.19.1), 14 cores (10 performance, 4 efficiency).

| text | decode: unicode | utf8cpp | utf8cpp unchecked | Hoehrmann | utf8proc | pipeline: unicode | utf8cpp | utf8cpp unchecked | Hoehrmann | utf8proc |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| English words | 0.75 | 1.31 | 0.27 | 0.57 | 0.75 | 0.64 | 1.63 | 0.59 | 0.89 | 2.62 |
| One accented letter in 33 | 1.08 | 1.52 | 0.47 | 0.83 | 0.93 | 1.07 | 1.74 | 0.77 | 1.30 | 2.77 |
| English with accents and quotes | 0.97 | 1.42 | 0.42 | 0.79 | 0.89 | 0.98 | 1.86 | 0.81 | 1.36 | 2.78 |
| English read as Latin-1 (invalid UTF-8) | 1.10 | - | - | - | - | 1.08 | - | - | - | - |
| Cyrillic words | 1.61 | 2.78 | 1.19 | 2.00 | 1.93 | 1.98 | 2.99 | 1.59 | 2.58 | 3.65 |
| CJK with ASCII digits and spaces | 1.56 | 2.85 | 0.93 | 1.93 | 1.83 | 1.93 | 3.10 | 1.35 | 2.82 | 3.58 |
| Emoji | 2.78 | 5.78 | 1.87 | 3.60 | 3.27 | 3.15 | 4.44 | 2.33 | 4.17 | 4.77 |
| Every length equally likely | 4.64 | 6.73 | 4.43 | 7.52 | 5.47 | 5.06 | 7.29 | 4.57 | 8.00 | 8.27 |
| Random bytes (invalid UTF-8) | 2.55 | - | - | - | - | 2.84 | - | - | - | - |

## Intel Core i9-9980HK, macOS 15.8 (x86-64)

Apple clang 17.0.0 (clang-1700.6.4.2), 8 cores, 16 threads.

| text | decode: unicode | utf8cpp | utf8cpp unchecked | Hoehrmann | utf8proc | pipeline: unicode | utf8cpp | utf8cpp unchecked | Hoehrmann | utf8proc |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| English words | 0.92 | 3.16 | 0.42 | 1.30 | 1.64 | 1.57 | 4.29 | 1.17 | 2.44 | 5.30 |
| One accented letter in 33 | 1.50 | 3.51 | 0.67 | 1.58 | 1.90 | 2.27 | 4.64 | 1.32 | 2.77 | 5.57 |
| English with accents and quotes | 1.28 | 3.47 | 0.59 | 1.53 | 1.83 | 2.02 | 4.50 | 1.27 | 2.69 | 5.47 |
| English read as Latin-1 (invalid UTF-8) | 1.55 | - | - | - | - | 2.27 | - | - | - | - |
| Cyrillic words | 2.66 | 6.40 | 2.06 | 3.86 | 3.92 | 4.24 | 7.56 | 2.39 | 4.66 | 7.71 |
| CJK with ASCII digits and spaces | 2.89 | 7.14 | 2.00 | 3.55 | 4.32 | 4.11 | 8.28 | 2.29 | 5.00 | 7.77 |
| Emoji | 4.98 | 9.07 | 3.38 | 5.60 | 6.96 | 6.44 | 10.2 | 3.66 | 7.12 | 10.4 |
| Every length equally likely | 6.78 | 11.5 | 6.34 | 9.77 | 8.69 | 8.86 | 12.9 | 6.09 | 11.1 | 13.6 |
| Random bytes (invalid UTF-8) | 3.48 | - | - | - | - | 5.04 | - | - | - | - |

## Reading the numbers

In the pipeline `unicode` is faster than the three decoders that check their input (utf8cpp, Hoehrmann, utf8proc) on every row of valid UTF-8, by 1.2× to 4.1× on the M4 Pro and 1.1× to 3.4× on the i9. Decode alone is closer. On the i9 `unicode` still leads those three on every row (1.05× to 3.4×). On the M4 Pro it leads utf8cpp (1.4× to 2.1×) but trails Hoehrmann's DFA on the three mostly-ASCII rows (0.57 against 0.75 ns on English words) and utf8proc on two of them (0.93 against 1.08 ns with one accent in 33; the two are level on English words). utf8cpp's `utf8::unchecked::next`, which neither checks the bytes nor stops at an end, is faster than `unicode` on every row of both machines: by 1.05× to 2.7× in decode and 1.09× to 1.8× in the pipeline.
