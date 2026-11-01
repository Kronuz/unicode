// Benchmark: next_utf8, the decode + classify + lowercase loop, and append_utf8 against the frozen
// original (test/unicode_old.hh), over text of several shapes. Reports ns per code point.
//
// Rows: "decode" sums the code points; "pipeline" decodes, then adds category_of and tolower (what a
// tokenizer does); "encode" appends every code point to a std::string (the same code before and
// after). Each row alternates the frozen original and the current code over several rounds and keeps
// the best pass of each. "A/A" is the spread between the frozen original's best in even and odd
// rounds: it shows how much the host moved during the row.
//
// A decode loop runs up to 30% faster or slower depending on where the compiler places it, for the
// same code. So decode and pipeline rows time each decoder in eight copies of the loop, shifted by
// 8 bytes each, and report the mean over the eight: "x" is old over new by those means, "worst" the
// smallest ratio at any one alignment, "layout" how far the frozen decoder's own eight copies
// differ (the noise floor for a single build).
//
// Build: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target unicode_bench
// Run:   ./build/unicode_bench [FILE]     (FILE: one more corpus, any UTF-8 text)
#include "unicode.hh"
#include "../test/unicode_old.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::mt19937_64 rng(7);

constexpr std::size_t corpus_bytes = 2u << 20;

std::string english_words() {
	static const char* const common[] = {"the", "of", "and", "to", "in", "is", "that", "for", "it", "as", "was", "with",
	                                     "be", "by", "on", "not", "he", "this", "are", "or", "his", "from", "at", "which",
	                                     "but", "have", "an", "had", "they", "you", "were", "their", "one", "all", "we"};
	std::string s;
	while (s.size() < corpus_bytes) {
		if (rng() % 3 == 0) {
			auto len = 3 + rng() % 9;
			for (std::size_t i = 0; i < len; ++i) {
				s += static_cast<char>('a' + rng() % 26);
			}
		} else {
			s += common[rng() % (sizeof(common) / sizeof(*common))];
		}
		auto r = rng() % 40;
		s += r == 0 ? ". " : r == 1 ? ", " : r == 2 ? "; The " : " ";
		if (rng() % 50 == 0) {
			s[s.size() - 1 - (r < 3 ? 1 : 0)] = static_cast<char>('A' + rng() % 26);
		}
	}
	return s;
}

// English with accented letters, typographic punctuation and the odd symbol.
std::string english_plus() {
	std::string s = english_words();
	std::string out;
	for (char c : s) {
		auto r = rng() % 100;
		if (c == ' ' && r < 6) {
			unicode::append_utf8(out, r < 3 ? U'\u2019' : r < 5 ? U'\u2014' : U'\u201c');
		} else if (c == 'e' && r < 8) {
			unicode::append_utf8(out, r < 5 ? U'\u00e9' : U'\u00e8');
		} else if (c == 'o' && r < 3) {
			unicode::append_utf8(out, U'\u00f6');
		} else if (c == '.' && r < 5) {
			unicode::append_utf8(out, U'\U0001F600');
		} else {
			out += c;
		}
	}
	return out;
}

std::string cyrillic() {
	std::string s;
	while (s.size() < corpus_bytes) {
		auto len = 2 + rng() % 9;
		for (std::size_t i = 0; i < len; ++i) {
			unicode::append_utf8(s, static_cast<char32_t>(0x430 + rng() % 32));
		}
		s += rng() % 12 == 0 ? ", " : " ";
	}
	return s;
}

std::string cjk() {
	std::string s;
	while (s.size() < corpus_bytes) {
		auto len = 1 + rng() % 8;
		for (std::size_t i = 0; i < len; ++i) {
			unicode::append_utf8(s, static_cast<char32_t>(0x4E00 + rng() % 0x51FF));
		}
		auto r = rng() % 10;
		if (r == 0) {
			s += std::to_string(rng() % 3000);
		} else if (r == 1) {
			unicode::append_utf8(s, U'\u3002');
		} else if (r == 2) {
			s += ' ';
		}
	}
	return s;
}

std::string emoji() {
	std::string s;
	while (s.size() < corpus_bytes) {
		unicode::append_utf8(s, static_cast<char32_t>(0x1F600 + rng() % 0x50));
		if (rng() % 3 == 0) {
			s += ' ';
		}
	}
	return s;
}

// Every code point's length equally likely: the worst case for a branch predictor.
std::string random_mix() {
	std::string s;
	while (s.size() < corpus_bytes) {
		switch (rng() % 4) {
			case 0: s += static_cast<char>(0x20 + rng() % 0x5f); break;
			case 1: unicode::append_utf8(s, static_cast<char32_t>(0x80 + rng() % 0x780)); break;
			case 2: unicode::append_utf8(s, static_cast<char32_t>(0x800 + rng() % 0xD000)); break;
			default: unicode::append_utf8(s, static_cast<char32_t>(0x10000 + rng() % 0x100000)); break;
		}
	}
	return s;
}

std::string random_bytes() {
	std::string s(corpus_bytes, '\0');
	for (auto& c : s) {
		c = static_cast<char>(rng());
	}
	return s;
}

// Mostly ASCII with one accented letter in about thirty code points (Spanish, German).
std::string european() {
	std::string s = english_words();
	std::string out;
	static const char32_t accented[] = {U'\u00e9', U'\u00f1', U'\u00fc', U'\u00e1', U'\u00f3', U'\u00df', U'\u00e4'};
	for (char c : s) {
		if (rng() % 33 == 0) {
			unicode::append_utf8(out, accented[rng() % (sizeof(accented) / sizeof(*accented))]);
		} else {
			out += c;
		}
	}
	return out;
}

// English read as Latin-1: one stray byte 0xC0..0xFF in forty, none of them valid UTF-8.
std::string latin1() {
	std::string s = english_words();
	for (auto& c : s) {
		if (rng() % 40 == 0) {
			c = static_cast<char>(0xC0 + rng() % 0x40);
		}
	}
	return s;
}

struct Corpus {
	const char* name;
	std::string text;
	std::vector<char32_t> cps;  // for the encode rows
};

volatile std::uint64_t sink = 0;

template <typename Fn>
double best_ns_per_cp(const Corpus& c, Fn&& fn, int reps) {
	double best = 1e300;
	for (int rep = 0; rep < reps; ++rep) {
		auto t0 = std::chrono::steady_clock::now();
		std::uint64_t s = fn(c);
		auto t1 = std::chrono::steady_clock::now();
		sink = sink + s;
		best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(c.cps.size()));
	}
	return best;
}

template <typename Old, typename New>
void row(const Corpus& c, const char* op, Old&& old_fn, New&& new_fn, int rounds = 8, int reps = 4) {
	double a_even = 1e300, a_odd = 1e300, b = 1e300;
	for (int r = 0; r < rounds; ++r) {
		double a = best_ns_per_cp(c, old_fn, reps);
		b = std::min(b, best_ns_per_cp(c, new_fn, reps));
		double& slot = r % 2 ? a_odd : a_even;
		slot = std::min(slot, a);
	}
	double a = std::min(a_even, a_odd);
	std::printf("%-12s %-9s %5.2f B/cp  old %6.3f ns  new %6.3f ns  x%5.2f   A/A %4.1f%%\n", c.name, op,
	            static_cast<double>(c.text.size()) / static_cast<double>(c.cps.size()), a, b, a / b,
	            100.0 * std::abs(a_even - a_odd) / a);
}

// Shifts the code after it by N bytes (a jump over N bytes of padding), to move the loop below it.
#if defined(__GNUC__) && defined(__aarch64__)
#define SHIFT_CODE(N) asm volatile("b 1f\n .skip " #N "\n1:")
#elif defined(__GNUC__) && defined(__x86_64__)
#define SHIFT_CODE(N) asm volatile("jmp 1f\n .skip " #N "\n1:")
#else
#define SHIFT_CODE(N) static_cast<void>(0)
#endif

constexpr int alignments = 8;

struct Frozen {
	static char32_t next(const char*& p, const char* end) { return unicode_old::next_utf8(p, end); }
	static std::uint64_t category(char32_t c) { return std::uint64_t(unicode_old::category_of(c)) + unicode_old::tolower(c); }
};

struct Current {
	static char32_t next(const char*& p, const char* end) { return unicode::next_utf8(p, end); }
	static std::uint64_t category(char32_t c) { return std::uint64_t(unicode::category_of(c)) + unicode::tolower(c); }
};

// One copy of the decode loop (Pipeline: also classify and lowercase), moved by Shift * 8 bytes.
template <typename D, int Shift, bool Pipeline>
__attribute__((noinline)) std::uint64_t decode_loop(const Corpus& c) {
	if constexpr (Shift == 1) SHIFT_CODE(8);
	if constexpr (Shift == 2) SHIFT_CODE(16);
	if constexpr (Shift == 3) SHIFT_CODE(24);
	if constexpr (Shift == 4) SHIFT_CODE(32);
	if constexpr (Shift == 5) SHIFT_CODE(40);
	if constexpr (Shift == 6) SHIFT_CODE(48);
	if constexpr (Shift == 7) SHIFT_CODE(56);
	const char* p = c.text.data();
	const char* end = p + c.text.size();
	std::uint64_t s = 0;
	while (p < end) {
		char32_t ch = D::next(p, end);
		if constexpr (Pipeline) {
			s += D::category(ch);
		} else {
			s += ch;
		}
	}
	return s;
}

template <typename D, bool Pipeline>
void time_alignments(const Corpus& c, double (&best)[alignments], int reps) {
	best[0] = std::min(best[0], best_ns_per_cp(c, decode_loop<D, 0, Pipeline>, reps));
	best[1] = std::min(best[1], best_ns_per_cp(c, decode_loop<D, 1, Pipeline>, reps));
	best[2] = std::min(best[2], best_ns_per_cp(c, decode_loop<D, 2, Pipeline>, reps));
	best[3] = std::min(best[3], best_ns_per_cp(c, decode_loop<D, 3, Pipeline>, reps));
	best[4] = std::min(best[4], best_ns_per_cp(c, decode_loop<D, 4, Pipeline>, reps));
	best[5] = std::min(best[5], best_ns_per_cp(c, decode_loop<D, 5, Pipeline>, reps));
	best[6] = std::min(best[6], best_ns_per_cp(c, decode_loop<D, 6, Pipeline>, reps));
	best[7] = std::min(best[7], best_ns_per_cp(c, decode_loop<D, 7, Pipeline>, reps));
}

// Frozen and current decoder over eight loop alignments each; rounds alternate them. Prints the mean
// ns per code point of each, old over new by those means, the worst single alignment, A/A (the
// frozen decoder's mean in even against odd rounds) and the spread of its own eight alignments.
template <bool Pipeline>
void decode_row(const Corpus& c, const char* op, int rounds = 6, int reps = 2) {
	double old_even[alignments], old_odd[alignments], fresh[alignments];
	std::fill_n(old_even, alignments, 1e300);
	std::fill_n(old_odd, alignments, 1e300);
	std::fill_n(fresh, alignments, 1e300);
	for (int r = 0; r < rounds; ++r) {
		if (r % 2) {
			time_alignments<Frozen, Pipeline>(c, old_odd, reps);
		} else {
			time_alignments<Frozen, Pipeline>(c, old_even, reps);
		}
		time_alignments<Current, Pipeline>(c, fresh, reps);
	}
	double a = 0, b = 0, ae = 0, ao = 0, worst = 1e300, lo = 1e300, hi = 0;
	for (int k = 0; k < alignments; ++k) {
		double old_k = std::min(old_even[k], old_odd[k]);
		a += old_k / alignments;
		b += fresh[k] / alignments;
		ae += old_even[k] / alignments;
		ao += old_odd[k] / alignments;
		worst = std::min(worst, old_k / fresh[k]);
		lo = std::min(lo, old_k);
		hi = std::max(hi, old_k);
	}
	std::printf("%-12s %-9s %5.2f B/cp  old %6.3f ns  new %6.3f ns  x%5.2f  worst x%5.2f   A/A %4.1f%%  layout %4.1f%%\n",
	            c.name, op, static_cast<double>(c.text.size()) / static_cast<double>(c.cps.size()), a, b, a / b, worst,
	            100.0 * std::abs(ae - ao) / a, 100.0 * (hi - lo) / lo);
}

std::uint64_t encode_old(const Corpus& c) {
	std::string out;
	for (char32_t ch : c.cps) {
		unicode_old::append_utf8(out, ch);
	}
	return out.size();
}

std::uint64_t encode_new(const Corpus& c) {
	std::string out;
	for (char32_t ch : c.cps) {
		unicode::append_utf8(out, ch);
	}
	return out.size();
}

Corpus make(const char* name, std::string text) {
	Corpus c{name, std::move(text), {}};
	const char* p = c.text.data();
	const char* end = p + c.text.size();
	while (p < end) {
		c.cps.push_back(unicode_old::next_utf8(p, end));
	}
	return c;
}

}  // namespace

int main(int argc, char** argv) {
	std::vector<Corpus> corpora;
	corpora.push_back(make("english", english_words()));
	corpora.push_back(make("european", european()));
	corpora.push_back(make("english+", english_plus()));
	corpora.push_back(make("latin1", latin1()));
	corpora.push_back(make("cyrillic", cyrillic()));
	corpora.push_back(make("cjk", cjk()));
	corpora.push_back(make("emoji", emoji()));
	corpora.push_back(make("random mix", random_mix()));
	corpora.push_back(make("random bytes", random_bytes()));
	if (argc > 1) {
		std::ifstream in(argv[1], std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		corpora.push_back(make("file", ss.str()));
	}
	std::printf("unicode bench (ns per code point)\n");
	for (const auto& c : corpora) {
		decode_row<false>(c, "decode");
		decode_row<true>(c, "pipeline");
		row(c, "encode", encode_old, encode_new);
	}
	return 0;
}
