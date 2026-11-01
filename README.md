# unicode

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/unicode/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/unicode/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Header-only Unicode 17 tables for C++20: general categories, simple lowercase and lenient
UTF-8 decoding without ICU. The tables are generated from the Unicode Character Database
as two-stage lookups: a code point's high bits pick a 256-entry block, its low byte picks
the entry, and identical blocks are stored once, so both tables fit in 75.8 KB. Reach for
it to classify and lowercase text the way Xapian does, in a tokenizer or a query parser.

## ✨ Highlights

- **`unicode::category_of(c)`:** the general category as `unicode::category`
  (`uppercase_letter`, `decimal_number`, `connector_punctuation`, ...); code points above
  U+10FFFF are `unassigned`.
- **`unicode::tolower(c)`:** the simple (one-to-one) lowercase mapping from
  `UnicodeData.txt`; code points without one map to themselves.
- **`is_letter`, `is_mark`, `is_number`:** category ranges.
- **Lenient decoding:** `unicode::next_utf8(p, end)` decodes one code point and advances
  `p`. A byte that does not start a valid, shortest-form sequence decodes as itself
  (ISO-8859-1) and advances one byte, so arbitrary bytes never fail and never read past
  `end`.
- **ASCII by eight-byte windows:** `next_utf8` tests eight bytes with one load and one
  mask; everything else goes to an out-of-line decoder that returns in registers.
- **`unicode::append_utf8(out, c)`** encodes `c` onto a `std::string`.
- **`constexpr` lookups,** checked against Xapian 2.1.0's `Unicode::get_category` and
  `Unicode::tolower` on all 1,114,112 code points (the categories are numbered in the
  same order).

## 🚀 Quick start

```cpp
#include "unicode.hh"

#include <cstdio>
#include <string>
#include <string_view>

int main() {
	std::string_view s = "Ünïcode, 2026!";
	std::string lower;
	for (const char *p = s.data(), *end = p + s.size(); p < end;) {
		char32_t c = unicode::next_utf8(p, end);
		if (unicode::is_letter(c) || unicode::is_number(c)) {
			unicode::append_utf8(lower, unicode::tolower(c));
		}
	}
	std::puts(lower.c_str());  // ünïcode2026
}
```

## 📦 Installation

Requires C++20 and CMake 3.14 or newer; no other dependencies. CI builds and tests it on
Linux (x86_64 with GCC and Clang, arm64), macOS (arm64, x86_64) and FreeBSD, plus ASan,
UBSan and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(unicode
  GIT_REPOSITORY https://github.com/Kronuz/unicode.git
  GIT_TAG        e53dd7b18ea79d44826ae85335a0940cc1bda8b9)  # or main, to track the tip
FetchContent_MakeAvailable(unicode)

target_link_libraries(my_app PRIVATE unicode::unicode)
```

Link `unicode::unicode` and include `unicode.hh`.

## 🎯 When to use it

- Classifying and lowercasing text exactly as Xapian does, with no ICU dependency.
- Decoding untrusted bytes that may not be UTF-8: invalid input reads as Latin-1.

Not for:

- Full case folding, normalization or word breaking: use ICU.
- Rejecting invalid UTF-8: `next_utf8` never reports an error.
- Locale-dependent mappings (Turkish dotless i and the like): `tolower` is the simple,
  locale-free mapping.

## ⚡ Performance

Nanoseconds per code point to decode 2 MB of each kind of text and add up the code points
("decode"), and to decode and also add `category_of` and `tolower` of each code point, as
a tokenizer does ("pipeline"). The rows are the text; the columns are implementations:
`unicode` is `next_utf8` here, [utf8cpp](https://github.com/nemtrif/utfcpp) 4.2.1 is
`utf8::next` (which checks its input) and `utf8::unchecked::next` (which trusts it),
Hoehrmann is Bjoern Hoehrmann's [DFA
decoder](https://bjoern.hoehrmann.de/utf-8/decoder/dfa/) written out from its description,
and [utf8proc](https://github.com/JuliaStrings/utf8proc) 2.11.3 is `utf8proc_iterate`,
`utf8proc_category` and `utf8proc_tolower` (Unicode 17, as here). utf8cpp and Hoehrmann
have no tables, so their pipeline uses this library's. All are built with the same
compiler and flags and return the same sums as `unicode` on the rows of valid UTF-8 (the
bench stops when one differs); the row of invalid UTF-8 shows `unicode` alone (`-`),
because the others reject bad bytes where `next_utf8` reads them as Latin-1.

Apple M4 Pro, macOS 26.7 (arm64):

| text | decode: unicode | utf8cpp | utf8cpp unchecked | Hoehrmann | utf8proc | pipeline: unicode | utf8cpp | utf8cpp unchecked | Hoehrmann | utf8proc |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| English words | 0.75 | 1.31 | 0.27 | 0.57 | 0.75 | 0.64 | 1.63 | 0.59 | 0.89 | 2.62 |
| One accented letter in 33 | 1.08 | 1.52 | 0.47 | 0.83 | 0.93 | 1.07 | 1.74 | 0.77 | 1.30 | 2.77 |
| English with accents and quotes | 0.97 | 1.42 | 0.42 | 0.79 | 0.89 | 0.98 | 1.86 | 0.81 | 1.36 | 2.78 |
| Cyrillic words | 1.61 | 2.78 | 1.19 | 2.00 | 1.93 | 1.98 | 2.99 | 1.59 | 2.58 | 3.65 |
| CJK with ASCII digits and spaces | 1.56 | 2.85 | 0.93 | 1.93 | 1.83 | 1.93 | 3.10 | 1.35 | 2.82 | 3.58 |
| Emoji | 2.78 | 5.32 | 1.87 | 3.60 | 3.26 | 3.15 | 4.44 | 2.33 | 4.17 | 4.65 |
| Every length equally likely | 4.64 | 6.73 | 4.43 | 7.52 | 5.47 | 5.06 | 7.29 | 4.57 | 8.00 | 8.27 |
| Random bytes (invalid UTF-8) | 2.55 | - | - | - | - | 2.84 | - | - | - | - |

Intel Core i9-9980HK, macOS 15.8 (x86-64):

| text | decode: unicode | utf8cpp | utf8cpp unchecked | Hoehrmann | utf8proc | pipeline: unicode | utf8cpp | utf8cpp unchecked | Hoehrmann | utf8proc |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| English words | 0.92 | 3.15 | 0.42 | 1.28 | 1.61 | 1.54 | 4.28 | 1.16 | 2.42 | 5.25 |
| One accented letter in 33 | 1.48 | 3.46 | 0.65 | 1.56 | 1.86 | 2.21 | 4.52 | 1.27 | 2.70 | 5.45 |
| English with accents and quotes | 1.26 | 3.35 | 0.59 | 1.49 | 1.78 | 1.97 | 4.45 | 1.25 | 2.63 | 5.43 |
| Cyrillic words | 2.65 | 6.30 | 2.03 | 3.78 | 3.89 | 4.15 | 7.50 | 2.36 | 4.59 | 7.66 |
| CJK with ASCII digits and spaces | 2.83 | 7.12 | 1.99 | 3.46 | 4.26 | 4.09 | 8.22 | 2.29 | 4.93 | 7.69 |
| Emoji | 4.92 | 9.00 | 3.38 | 5.60 | 6.92 | 6.35 | 10.1 | 3.60 | 7.06 | 10.3 |
| Every length equally likely | 6.65 | 11.5 | 6.34 | 9.66 | 8.62 | 8.65 | 12.8 | 6.05 | 10.9 | 13.5 |
| Random bytes (invalid UTF-8) | 3.44 | - | - | - | - | 4.99 | - | - | - | - |

In the pipeline `unicode` is faster than the three decoders that check their input
(utf8cpp, Hoehrmann, utf8proc) on every row of valid UTF-8, by 1.2× to 4.1× on the M4 Pro
and 1.1× to 3.4× on the i9. Decode alone is closer. On the i9 `unicode` still leads those
three on every row (1.05× to 3.4×). On the M4 Pro it leads utf8cpp (1.4× to 1.9×) but
trails Hoehrmann's DFA on the three mostly-ASCII rows (0.57 against 0.75 ns on English
words) and utf8proc on two of them (0.93 against 1.08 ns with one accent in 33; the two
are level on English words). utf8cpp's `utf8::unchecked::next`, which neither checks the
bytes nor stops at an end, is faster than `unicode` on every row of both machines: by
1.05× to 2.7× in decode and 1.09× to 1.8× in the pipeline.

Apple clang 17.0.0 at `-O3 -DNDEBUG` (CMake Release), each machine to itself, cells are
means over eight loop alignments. To rebuild and rerun (the build fetches utf8cpp and
utf8proc at those tags):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DUNICODE_BENCH_COMPARE=ON
cmake --build build --target unicode_bench
./build/unicode_bench --compare
```

[docs/perf/comparison.md](docs/perf/comparison.md) has every row, the method and the
spread between runs. [docs/perf/next-utf8.md](docs/perf/next-utf8.md) has the method for
the ASCII window and the numbers on an Apple M1 Pro.

## 📚 Documentation

- [docs/perf/comparison.md](docs/perf/comparison.md): `next_utf8` against utf8proc, utf8cpp and
  Hoehrmann's decoder, every row on two machines.
- [docs/perf/next-utf8.md](docs/perf/next-utf8.md): how `next_utf8` is measured.
- [CHANGELOG.md](CHANGELOG.md): notable changes, with their measurements.
- [AGENTS.md](AGENTS.md): repository map and invariants.
- [Catalog entry](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#unicode) in
  Kronuz C++ Libraries.

## 🛠️ Building and testing

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The build includes `unicode_bench`; `-DUNICODE_FUZZ=ON` adds the libFuzzer target
`fuzz/fuzz_utf8.cc`, which needs a Clang with libFuzzer. To regenerate the tables from a
new Unicode release:

```sh
curl -O https://www.unicode.org/Public/17.0.0/ucd/UnicodeData.txt
tools/gen_tables.py UnicodeData.txt 17.0.0 > unicode_tables.hh
```

## 🤝 Contributing

Issues and pull requests are welcome; read [AGENTS.md](AGENTS.md) and the catalog's
[rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
first.

## 📄 License

MIT, see [LICENSE](LICENSE). The generated tables derive from the Unicode Character
Database under the Unicode License v3; see [LICENSE-UNICODE](LICENSE-UNICODE).
