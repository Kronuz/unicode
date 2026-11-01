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

Nanoseconds per code point from `bench/bench.cc`, on an Apple M4 Pro with Apple clang 17
at `-O3`. "decode" sums the code points; "pipeline" decodes, then adds `category_of` and
`tolower`, as a tokenizer does. Each decoder runs in eight copies of the loop, shifted by
8 bytes each, and the figures are means over the eight.

| text | decode | pipeline |
| --- | ---: | ---: |
| English words | 0.76 | 0.65 |
| One accented letter in 33 | 1.09 | 1.08 |
| English with accents and quotes | 0.98 | 1.00 |
| English read as Latin-1 | 1.10 | 1.07 |
| Cyrillic words | 1.6 | 2.0 |
| CJK with ASCII digits and spaces | 1.6 | 1.9 |
| Emoji | 2.8 | 3.1 |
| Every length equally likely | 4.7 | 5.1 |
| Random bytes (mostly invalid UTF-8) | 2.5 | 2.8 |

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target unicode_bench
./build/unicode_bench [FILE]
```

[docs/perf/next-utf8.md](docs/perf/next-utf8.md) has the method and the numbers on an
Apple M1 Pro and an Intel Core i9-9980HK.

## 📚 Documentation

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
