# next_utf8: ASCII by eight-byte windows

Measured 2026-11-01 at `unicode:2e5d92a`. The first-byte version below was measured 2026-10-31 at `unicode:7c7d6ad`.

## Change

`next_utf8` decodes one code point and advances the cursor. It decides ASCII on a window of eight bytes: when `end - p >= 8` and the window has no byte with the high bit set, the first byte is the code point and `p` advances by one. Everything else goes to `detail::decode_utf8`: multi-byte sequences, invalid bytes, and the last seven bytes of the text (which use a first-byte test).

`decode_utf8` is out of line, starts on a 64-byte boundary (`aligned(64)`) and returns the code point and the next address as a two-word struct, which comes back in registers (`rax:rdx`, `x0:x1`), so the caller's loop keeps `p` in a register. It checks the bounds once per sequence and tests the continuation bytes together.

Decoded code points and bytes consumed are identical to the frozen decoder (`test/unicode_old.hh`) for every input, invalid sequences included. `append_utf8` and the tables are untouched.

## Method

- Bench: `bench/bench.cc`, target `unicode_bench`, `./build/unicode_bench [FILE]`. It times each decoder over 2 MB of text of nine shapes, the rows of the table below. "decode" sums the code points. "pipeline" also calls `category_of` and `tolower`, the loop of a tokenizer.
- Arms: the frozen header (before) and the current one (after), alternated over rounds.
- Layout control: the same decode loop runs up to 30% apart on the Intel host depending on where the compiler places it. Each decoder is timed in eight copies of the loop, shifted by 8 bytes each, and the figure is the mean of the eight. The bench prints the worst alignment and the spread of the frozen decoder against itself ("A/A" and "layout").
- Machines: Apple M4 Pro, Apple M1 Pro, Intel Core i9-9980HK (macOS 15, x86_64). Apple clang 17, `-O3`, with the machine to itself.
- Linux (clang 12, g++ 11, glibc): correctness only, never timing.

## Results

Speedup of the decode loop over the frozen decoder, mean over eight alignments. The first row also gives nanoseconds per code point before and after.

| text | Apple M4 Pro | Apple M1 Pro | Intel i9-9980HK |
|---|---:|---:|---:|
| English words | 1.725 to 0.757 ns, **2.28x** | 2.095 to 0.932 ns, 2.25x | 1.952 to 0.955 ns, 2.04x |
| European, one accent in 33 | 1.65x | 1.61x | 1.51x |
| English with accents, quotes, a few emoji | 1.84x | 1.79x | 1.65x |
| English read as Latin-1 | 1.57x | 1.60x | 1.46x |
| Cyrillic words | 1.20x | 1.52x | 1.00x |
| CJK with ASCII digits and spaces | 1.11x | 1.25x | 1.37x |
| Emoji | 1.25x | 1.17x | 1.30x |
| Every length equally likely | 1.30x | 1.32x | 1.30x |
| Random bytes (mostly invalid UTF-8) | 1.17x | 1.26x | 1.16x |

The pipeline loop (decode, `category_of`, `tolower`) reads 1.15x to 2.81x on the Apple hosts and 0.99x to 1.89x on the i9. The lowest i9 rows are Cyrillic (1.00x decode and 0.99x pipeline, worst single alignment 0.97x and 0.85x) and random bytes in the pipeline (1.07x, worst alignment 0.92x). Built at `-O2`, the i9 reads 0.92x and 0.87x on Cyrillic, where every code point is a call into `decode_utf8` (the frozen decoder is a call too); every other row stays at 1.04x or more.

The `append_utf8` rows run the same code before and after: 0.98x to 1.02x on the Apple hosts and 0.69x to 0.94x on the i9. That range is the noise floor of the i9 for a loop that is not alignment-controlled. The M1 Pro with nothing else running shows A/A 0.0% and layout 0.1% on every row, so it is the host to time arm64 changes on.

### The first-byte version

A version that decided ASCII with one comparison on the first byte read, on the M4 Pro:

| text | decode | pipeline |
|---|---:|---:|
| English words | 5.74x | 2.91x |
| English with accents, quotes, a few emoji | 4.26x | 2.23x |
| Cyrillic words | 1.01x | 0.95x |
| CJK with ASCII digits and spaces | 1.09x | 0.91x |
| Emoji | 1.25x | 1.22x |
| Every length equally likely | 1.27x | 1.27x |
| Random bytes | **0.72x** | 0.67x |

On the i9 it read 4.83x on English, 0.84x on Cyrillic and 0.66x on random bytes. About half the bytes of random input are ASCII, so a test on the first byte is mispredicted on every other code point. The frozen decoder tests the minority classes first (`C2..DF`, `E0..EF`, `F0..F4`) and lets ASCII and stray bytes fall out of the last `else`, which is already the cheapest order for random input. A window of eight bytes is predictable on text (almost always all ASCII) and on random bytes (almost never), so it keeps 2.3x on English on the M4 Pro and no row on the Apple hosts falls below 1.11x.

## Proof

- `test/diff_test.cc` (CTest `unicode_diff`, 62M checks; `--full` runs 265M) compares `next_utf8` with the frozen decoder on every string of one to three bytes; every encoded code point (surrogates included) with each byte replaced and at every cut of the buffer; one byte of each value at each position of an ASCII buffer, and pairs of them, at every length around the window; and random text. The bytes past `end` hold valid continuations, so a read past `end` changes the answer. It also decodes text that ends at the last byte before an inaccessible page, which turns a read past `end` into a crash in a plain build. `append_utf8`, `category_of`, `tolower` and the predicates are compared on every code point.
- `tools/mutants.py` breaks the decoder in 24 ways (7 in the window and the tail) and requires the differential test to fail or crash on each. A mutant that stops the cursor makes a `static_assert` loop forever, which GCC reports as "non-constant condition" and Clang as a failed assertion; both count as killed at compile time.
- Equivalent mutants are not in the list: a lead-byte mask that the lead byte's range already implies (`b0 & 0x3F` for `0xC2..0xDF`), and lead bytes `0xF5..0xF7`, whose result the maximum-code-point check rejects. With the first byte in the window mask, the other seven bytes only decide how often the fast path is taken: dropping the second byte, or only the first, changes no result, and the mutant that matters drops both.
- `fuzz/fuzz_utf8.cc` compares with the frozen decoder under libFuzzer. On Linux, 2.8M runs under ASan and UBSan and the differential test at `-O1` and `-O3` under both were clean.

## Decision and open items

- The window version is the one in the library. English falls from 5.7x to 2.3x of the frozen decoder on the M4 Pro, and the loss on random bytes (0.72x) and on i9 Cyrillic (0.84x) disappears.
- Placement matters on x86. With `decode_utf8` at an arbitrary address the i9 decodes Cyrillic about 20% and CJK about 25% slower than with it on a 64-byte boundary, and shifting the loop does not cover it, because one copy of the callee serves all eight loop alignments. Build the bench with `-falign-functions=64` and without it, and decide on the mean. Measure the library's header, not a copy of its algorithm: a scratch variant and the header read 1.10x and 0.82x on the same row until the callee's placement was equalized.
- Variants measured that did not win: the first byte with `b0 < 0xC2` (ASCII and stray bytes together, 0.71x on random bytes); a lead-byte jump table; a window of four bytes (0.93x on random bytes); `likely` and `unlikely` hints (worse on x86); decoding the two-, three- and four-byte sequences inline from the window word (mixed, worse on CJK, emoji and random bytes).
- A bulk helper (`ascii_prefix`: the number of leading ASCII bytes, eight at a time) is not in the library. In a loop that does a table lookup or two per code point it is at most 8% faster on English and 0.7x to 0.8x on Cyrillic and CJK, where the ASCII runs are one byte long. It earns a place when the loop it serves gets 1.3x.
- Open: Cyrillic on the i9 built at `-O2` reads 0.92x decode and 0.87x pipeline, because each code point is a call into `decode_utf8`. At `-O3` the same rows read 1.00x and 0.99x, and every other row is above 1.0x.
