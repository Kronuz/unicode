# Changelog

Notable changes to unicode, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); entries are dated because the
library has no releases.

## 2026-11-01

### Changed

- `next_utf8` decides ASCII on a window of eight bytes (one 8-byte load and one mask), where the
  entry below decided it on the first byte. Everything else goes to `detail::decode_utf8`:
  multi-byte sequences, invalid bytes, and the last seven bytes of the text (which use the
  first-byte test). `decode_utf8` is out of line, starts on a 64-byte boundary and returns the
  code point and the next address in registers, where `detail::next_utf8_multibyte` took `p` by
  reference. Decoded code points and bytes consumed are identical for every input, invalid
  sequences included.
- Why: with the first-byte test, text that is not mostly ASCII lost to the frozen decoder. About
  half the bytes of random input are ASCII, so the test was mispredicted on every other code
  point (0.67x to 0.72x of the frozen decoder), and Cyrillic read 0.84x on the i9. A window of
  eight ASCII bytes is predictable on text and on random bytes. The price is English: 2.3x of the
  frozen decoder (it was 5.7x) on the M4 Pro.
- Placement matters on x86. With `decode_utf8` at an arbitrary address the i9 decoded Cyrillic
  about 20% and CJK about 25% slower than with it on a 64-byte boundary.

Time per code point from `bench/bench.cc` (Apple clang, `-O3`, strict holds). Before is the frozen
header in `test/unicode_old.hh`; "x" is before over after. Each decoder runs in eight copies of the
loop, shifted by 8 bytes each, and the figures are means over the eight (the same code runs up to
30% apart from where its loop lands on the i9; "A/A" and "layout" in the bench output show it).
"decode" sums the code points; "pipeline" also adds `category_of` and `tolower`.

| text | M4 Pro decode | pipeline | M1 Pro decode | pipeline | i9 decode | pipeline |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| English words | 1.725 to 0.757 ns, 2.28x | 2.81x | 2.095 to 0.932, 2.25x | 2.30x | 1.952 to 0.955, 2.04x | 1.89x |
| European, one accent in 33 | 1.65x | 1.77x | 1.61x | 1.57x | 1.51x | 1.45x |
| English with accents, quotes, a few emoji | 1.84x | 1.89x | 1.79x | 1.76x | 1.65x | 1.58x |
| English read as Latin-1 | 1.57x | 1.73x | 1.60x | 1.57x | 1.46x | 1.42x |
| Cyrillic words | 1.20x | 1.32x | 1.52x | 1.30x | 1.00x | 0.99x |
| CJK with ASCII digits and spaces | 1.11x | 1.16x | 1.25x | 1.20x | 1.37x | 1.23x |
| Emoji | 1.25x | 1.21x | 1.17x | 1.20x | 1.30x | 1.19x |
| Every length equally likely | 1.30x | 1.26x | 1.32x | 1.27x | 1.30x | 1.21x |
| Random bytes (mostly invalid UTF-8) | 1.17x | 1.15x | 1.26x | 1.20x | 1.16x | 1.07x |

No row is below 1.11x on the two Apple hosts. On the i9 the lowest are Cyrillic (1.00x decode,
0.99x pipeline; its worst single alignment is 0.97x and 0.85x) and random bytes in the pipeline
(1.07x, worst alignment 0.92x). Built at `-O2` instead of `-O3` the i9 reads 0.92x and 0.87x on
Cyrillic, where every code point is a call into `decode_utf8` (the frozen decoder is a call too),
and every other row stays at 1.04x or more. The `append_utf8` rows run the same code before and
after: 0.98x to 1.02x on the Apple hosts, 0.69x to 0.94x on the i9, which is the noise floor of
that host for a loop that is not alignment-controlled.

For comparison, the first-byte version (the entry below) measured on the M4 Pro: English 5.74x,
accents 4.26x, Cyrillic 1.01x, CJK 1.09x, emoji 1.25x, every length 1.27x, random bytes 0.72x.

### Added

- `test/diff_test.cc` sweeps one byte of each value at each position of an ASCII buffer, and pairs
  of them, at every length around the window (2.6M checks), and decodes text that ends at the last
  byte before an inaccessible page, so that a read past `end` crashes the test.
- `tools/mutants.py` breaks the decoder in 24 ways (7 of them in the window and the tail).
- `bench/bench.cc` times each decoder at eight loop alignments, and has rows for text with sparse
  accents and for Latin-1 read as bytes.
- Checked on Linux (clang 12, g++ 11, glibc): the differential test under ASan and UBSan at `-O1` and
  `-O3`, and 2.8M libFuzzer runs under ASan and UBSan, all clean.

## 2026-10-31

### Changed

- `next_utf8` decides ASCII with one inline comparison. Before, it ran three range comparisons
  on the lead byte before it fell through to the one-byte case. The multi-byte part moved to
  `detail::next_utf8_multibyte`, which checks the bounds once per sequence and the continuation
  bytes together, where it called a lambda per byte. Decoded code points and bytes consumed are
  identical for every input, invalid sequences included.

Time per code point on an Apple M4 Pro (Apple clang 17, `-O3`), from `bench/bench.cc` under an
exclusive hold. Before is the frozen header in `test/unicode_old.hh`; each figure is the best of
several alternating passes over 2 MB of text. "decode" sums the code points; "pipeline" also adds
`category_of` and `tolower`, the loop of a tokenizer. "A/A" is how far the frozen code differs
from itself between rounds:

| text | bytes per code point | decode before | decode after | speedup | pipeline before | pipeline after | speedup |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| English words | 1.00 | 1.567 ns | 0.273 ns | 5.74x (A/A 17%) | 1.818 ns | 0.625 ns | 2.91x |
| English with accents, quotes, a few emoji | 1.03 | 1.836 ns | 0.431 ns | 4.26x | 1.886 ns | 0.847 ns | 2.23x |
| Cyrillic words | 1.85 | 1.815 ns | 1.800 ns | 1.01x | 2.159 ns | 2.284 ns | 0.95x (A/A 34%) |
| CJK with ASCII digits and spaces | 2.82 | 2.223 ns | 2.037 ns | 1.09x | 2.013 ns | 2.222 ns | 0.91x (A/A 21%) |
| Emoji | 3.25 | 3.442 ns | 2.762 ns | 1.25x | 3.857 ns | 3.165 ns | 1.22x |
| Every length equally likely | 2.50 | 5.861 ns | 4.600 ns | 1.27x | 6.361 ns | 5.020 ns | 1.27x |
| Random bytes (mostly invalid UTF-8) | 1.04 | 2.998 ns | 4.137 ns | 0.72x | 3.122 ns | 4.651 ns | 0.67x |

`append_utf8` and the table lookups are unchanged (1.00x on every text on the M4 Pro).

On an Intel i9-9980HK (Apple clang 17, `-O3`, exclusive hold), in ns per code point:

| text | decode before | decode after | speedup | pipeline before | pipeline after | speedup |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| English words | 2.303 | 0.476 | 4.83x | 3.225 | 1.507 | 2.14x |
| English with accents, quotes, a few emoji | 2.384 | 0.670 | 3.56x | 3.359 | 1.757 | 1.91x |
| Cyrillic words | 2.714 | 3.229 | 0.84x | 4.241 | 4.178 | 1.02x |
| CJK with ASCII digits and spaces | 4.019 | 3.037 | 1.32x | 5.539 | 4.033 | 1.37x |
| Emoji | 6.226 | 5.415 | 1.15x | 7.852 | 6.011 | 1.31x |
| Every length equally likely | 8.720 | 8.203 | 1.06x | 10.599 | 9.171 | 1.16x |
| Random bytes (mostly invalid UTF-8) | 4.305 | 6.564 | 0.66x | 5.280 | 7.671 | 0.69x |

The `append_utf8` rows, which run the same code before and after, read 0.91x to 0.99x on this host
(the frozen code run against itself differs by up to 8.3%), so the Cyrillic decode row is inside
that noise as well: the same loop built into a second program measured 1.06x.

Text that is mostly ASCII decodes 3.5 to 6 times faster. On the M4 Pro, Cyrillic and CJK text is
within the noise of this benchmark: the same decode loop built into a second program measured
1.27 ns (before) on the Cyrillic text instead of 1.82 ns, so differences of up to 10% there are not
distinguishable from code layout. On the i9, CJK text gains 1.3x and Cyrillic reads 0.84x in this
program and 1.06x in the second. Random bytes are a real loss on both hosts: with half the bytes
ASCII, the extra comparison at the front is mispredicted about as often as it is taken. A caller
that decodes arbitrary binary data and not text pays 0.7x.

### Added

- `test/diff_test.cc` compares `next_utf8` with the frozen decoder on every string of one to three
  bytes, every encoded code point (surrogates included) with each of its bytes replaced and at
  every cut of the buffer (the bytes past `end` hold valid continuations, so a read past `end`
  changes the answer), and random text; and `append_utf8`, `category_of`, `tolower` and the
  predicates on every code point (62M checks, CTest `unicode_diff`; `--full` runs 265M).
- `tools/mutants.py` breaks the decoder in 17 ways; the differential test has to fail on each.
- The libFuzzer target compares the decoder with the frozen one as well.
- `bench/bench.cc` times the decoder, the pipeline and `append_utf8` against the frozen header
  over text of several shapes, and over a file when one is given.
