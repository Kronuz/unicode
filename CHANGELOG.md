# Changelog

Notable changes to unicode, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); entries are dated because the
library has no releases.

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

`append_utf8` and the table lookups are unchanged (1.00x on every text).

Text that is mostly ASCII decodes 4 to 6 times faster. Cyrillic and CJK text is within the noise
of this benchmark: the same decode loop built into a second program measured 1.27 ns (before) on
the Cyrillic text instead of 1.82 ns, so differences of up to 10% there are not distinguishable
from code layout. Random bytes are a real loss: with half the bytes ASCII, the extra comparison at
the front is mispredicted about as often as it is taken. A caller that decodes arbitrary binary
data and not text pays 0.7x.

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
