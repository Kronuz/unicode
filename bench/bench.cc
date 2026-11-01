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
// The "compare" rows time the decoders a reader would otherwise use on the same eight alignments:
// Bjoern Hoehrmann's DFA decoder (written out below), and with -DUNICODE_BENCH_COMPARE=ON utf8proc
// (utf8proc_iterate, utf8proc_category, utf8proc_tolower) and utf8cpp (utf8::next, and the unchecked
// utf8::unchecked::next). They run on the inputs that are valid UTF-8, where each must return the same sum
// as unicode.hh (the bench stops if one does not; --check also compares every code point). The two
// inputs with invalid bytes read as Latin-1 only in unicode.hh, so only it and the frozen original run there.
//
// Build: cmake -B build -DCMAKE_BUILD_TYPE=Release [-DUNICODE_BENCH_COMPARE=ON] && cmake --build build --target unicode_bench
// Run:   ./build/unicode_bench [--compare] [FILE]     (--compare: only the compare rows; FILE: one more corpus, any
//                                                      UTF-8 text)
//        ./build/unicode_bench --check                (the results of every decoder, no timing; a CTest test)
#include "unicode.hh"
#include "../test/unicode_old.hh"

#ifdef UNICODE_BENCH_COMPARE
#include <utf8.h>
#include <utf8proc.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
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
	bool valid = false;         // well-formed UTF-8: the comparators run on it
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

// Bjoern Hoehrmann's UTF-8 decoder (https://bjoern.hoehrmann.de/utf-8/decoder/dfa/), rebuilt from its
// description: a class for each byte, then one table lookup per byte moves between states. State 0 accepts,
// state 12 rejects, the others count the continuation bytes still owed. Only well-formed UTF-8 is expected:
// a rejected or cut sequence reads as U+FFFD.
namespace dfa {

constexpr unsigned reject = 12;

constexpr std::uint8_t byte_class(unsigned b) {
	if (b < 0x80) return 0;
	if (b < 0x90) return 1;
	if (b < 0xA0) return 9;
	if (b < 0xC0) return 7;
	if (b < 0xC2) return 8;
	if (b < 0xE0) return 2;
	if (b == 0xE0) return 10;
	if (b == 0xED) return 4;
	if (b < 0xF0) return 3;
	if (b == 0xF0) return 11;
	if (b < 0xF4) return 6;
	if (b == 0xF4) return 5;
	return 8;
}

struct Table {
	std::uint8_t t[256 + 9 * 12];
};

constexpr Table make_table() {
	Table r{};
	for (unsigned b = 0; b < 256; ++b) {
		r.t[b] = byte_class(b);
	}
	for (unsigned i = 256; i < sizeof r.t; ++i) {
		r.t[i] = reject;
	}
	auto set = [&r](unsigned state, std::initializer_list<unsigned> classes, unsigned next) {
		for (unsigned k : classes) {
			r.t[256 + state + k] = std::uint8_t(next);
		}
	};
	// From the start: one byte, or the lead byte of a sequence (E0, ED, F0 and F4 restrict the next byte).
	set(0, {0}, 0);
	set(0, {2}, 24);   // C2..DF: one continuation
	set(0, {3}, 36);   // E1..EC, EE, EF: two
	set(0, {4}, 60);   // ED: 80..9F, then one
	set(0, {10}, 48);  // E0: A0..BF, then one
	set(0, {11}, 72);  // F0: 90..BF, then two
	set(0, {6}, 84);   // F1..F3: three
	set(0, {5}, 96);   // F4: 80..8F, then two
	set(24, {1, 9, 7}, 0);
	set(36, {1, 9, 7}, 24);
	set(48, {7}, 24);
	set(60, {1, 9}, 24);
	set(72, {9, 7}, 36);
	set(84, {1, 9, 7}, 36);
	set(96, {1}, 36);
	return r;
}

inline constexpr Table table = make_table();

}  // namespace dfa

struct Dfa {
	static char32_t next(const char*& p, const char* end) {
		std::uint32_t state = 0, cp = 0;
		do {
			std::uint32_t b = static_cast<unsigned char>(*p++);
			std::uint32_t k = dfa::table.t[b];
			cp = state ? (b & 0x3F) | (cp << 6) : (0xFFu >> k) & b;
			state = dfa::table.t[256 + state + k];
		} while (state > dfa::reject && p < end);
		return state == 0 ? cp : 0xFFFD;
	}
	static std::uint64_t category(char32_t c) { return Current::category(c); }
};

#ifdef UNICODE_BENCH_COMPARE
// utf8cpp: utf8::next checks the sequence and the end; utf8::unchecked::next trusts its input. Neither
// has tables, so the pipeline row gives them unicode.hh's.
struct Utf8cpp {
	static char32_t next(const char*& p, const char* end) { return utf8::next(p, end); }
	static std::uint64_t category(char32_t c) { return Current::category(c); }
};

struct Utf8cppUnchecked {
	static char32_t next(const char*& p, const char*) { return utf8::unchecked::next(p); }
	static std::uint64_t category(char32_t c) { return Current::category(c); }
};

// utf8proc numbers its categories in another order than Xapian's, which unicode.hh follows.
constexpr std::array<unicode::category, 30> utf8proc_categories = [] {
	std::array<unicode::category, 30> m{};
	using unicode::category;
	m[UTF8PROC_CATEGORY_CN] = category::unassigned;
	m[UTF8PROC_CATEGORY_LU] = category::uppercase_letter;
	m[UTF8PROC_CATEGORY_LL] = category::lowercase_letter;
	m[UTF8PROC_CATEGORY_LT] = category::titlecase_letter;
	m[UTF8PROC_CATEGORY_LM] = category::modifier_letter;
	m[UTF8PROC_CATEGORY_LO] = category::other_letter;
	m[UTF8PROC_CATEGORY_MN] = category::non_spacing_mark;
	m[UTF8PROC_CATEGORY_MC] = category::spacing_mark;
	m[UTF8PROC_CATEGORY_ME] = category::enclosing_mark;
	m[UTF8PROC_CATEGORY_ND] = category::decimal_number;
	m[UTF8PROC_CATEGORY_NL] = category::letter_number;
	m[UTF8PROC_CATEGORY_NO] = category::other_number;
	m[UTF8PROC_CATEGORY_PC] = category::connector_punctuation;
	m[UTF8PROC_CATEGORY_PD] = category::dash_punctuation;
	m[UTF8PROC_CATEGORY_PS] = category::open_punctuation;
	m[UTF8PROC_CATEGORY_PE] = category::close_punctuation;
	m[UTF8PROC_CATEGORY_PI] = category::initial_punctuation;
	m[UTF8PROC_CATEGORY_PF] = category::final_punctuation;
	m[UTF8PROC_CATEGORY_PO] = category::other_punctuation;
	m[UTF8PROC_CATEGORY_SM] = category::math_symbol;
	m[UTF8PROC_CATEGORY_SC] = category::currency_symbol;
	m[UTF8PROC_CATEGORY_SK] = category::modifier_symbol;
	m[UTF8PROC_CATEGORY_SO] = category::other_symbol;
	m[UTF8PROC_CATEGORY_ZS] = category::space_separator;
	m[UTF8PROC_CATEGORY_ZL] = category::line_separator;
	m[UTF8PROC_CATEGORY_ZP] = category::paragraph_separator;
	m[UTF8PROC_CATEGORY_CC] = category::control;
	m[UTF8PROC_CATEGORY_CF] = category::format;
	m[UTF8PROC_CATEGORY_CS] = category::surrogate;
	m[UTF8PROC_CATEGORY_CO] = category::private_use;
	return m;
}();

struct Utf8proc {
	static char32_t next(const char*& p, const char* end) {
		utf8proc_int32_t cp;
		utf8proc_ssize_t n = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(p), end - p, &cp);
		if (n < 0) {
			++p;
			return 0xFFFD;
		}
		p += n;
		return char32_t(cp);
	}
	static std::uint64_t category(char32_t c) {
		auto k = utf8proc_categories[std::size_t(utf8proc_category(utf8proc_int32_t(c)))];
		return std::uint64_t(k) + char32_t(utf8proc_tolower(utf8proc_int32_t(c)));
	}
};
#endif

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

// A decoder in the compare rows: how to time its loop at the eight alignments, and how to sum a corpus with it.
struct Contender {
	const char* name;
	void (*time_at)(const Corpus&, double (&)[alignments], int);
	std::uint64_t (*sum)(const Corpus&);
};

template <typename D, bool Pipeline>
Contender contender(const char* name) {
	return {name, &time_alignments<D, Pipeline>, &decode_loop<D, 0, Pipeline>};
}

// unicode.hh first, then the comparators, then the frozen original. On input that is not valid UTF-8 the
// comparators are left out: they reject it (an error or U+FFFD) where unicode.hh reads it as Latin-1.
template <bool Pipeline>
std::vector<Contender> contenders(bool valid) {
	std::vector<Contender> v;
	v.push_back(contender<Current, Pipeline>("unicode"));
	if (valid) {
		v.push_back(contender<Dfa, Pipeline>("hoehrmann"));
#ifdef UNICODE_BENCH_COMPARE
		v.push_back(contender<Utf8cpp, Pipeline>("utf8cpp"));
		v.push_back(contender<Utf8cppUnchecked, Pipeline>("utf8cpp-unc"));
		v.push_back(contender<Utf8proc, Pipeline>("utf8proc"));
#endif
	}
	v.push_back(contender<Frozen, Pipeline>("frozen"));
	return v;
}

struct Slots {
	double v[alignments];
};

void compare_header() {
	std::printf("\ncompare (ns per code point: mean over eight loop alignments; sums checked equal to unicode.hh)\n");
	std::printf("%-12s %-9s", "text", "op");
	for (const auto& d : contenders<false>(true)) {
		std::printf(" %11s", d.name);
	}
	std::printf("\n");
}

// Every decoder over eight loop alignments, rounds alternating between them (even and odd rounds are kept
// apart: "A/A" is the largest spread between a decoder's even and odd rounds, how much the host moved).
template <bool Pipeline>
void compare_row(const Corpus& c, const char* op, int rounds = 8, int reps = 3) {
	auto all = contenders<Pipeline>(true);
	auto run = contenders<Pipeline>(c.valid);
	std::uint64_t expect = run[0].sum(c);
	for (const auto& d : run) {
		if (d.sum(c) != expect) {
			std::fprintf(stderr, "%s %s: %s returns a different sum than unicode.hh\n", c.name, op, d.name);
			std::exit(1);
		}
	}
	std::vector<Slots> even(run.size()), odd(run.size());
	for (std::size_t i = 0; i < run.size(); ++i) {
		std::fill_n(even[i].v, alignments, 1e300);
		std::fill_n(odd[i].v, alignments, 1e300);
	}
	for (int r = 0; r < rounds; ++r) {
		for (std::size_t i = 0; i < run.size(); ++i) {
			run[i].time_at(c, (r % 2 ? odd[i] : even[i]).v, reps);
		}
	}
	std::printf("%-12s %-9s", c.name, op);
	double spread = 0;
	for (const auto& d : all) {
		std::size_t i = 0;
		while (i < run.size() && std::strcmp(run[i].name, d.name) != 0) {
			++i;
		}
		if (i == run.size()) {
			std::printf(" %11s", "-");
			continue;
		}
		double best = 0, e = 0, o = 0;
		for (int k = 0; k < alignments; ++k) {
			best += std::min(even[i].v[k], odd[i].v[k]) / alignments;
			e += even[i].v[k] / alignments;
			o += odd[i].v[k] / alignments;
		}
		spread = std::max(spread, std::abs(e - o) / best);
		std::printf(" %11.3f", best);
	}
	std::printf("   A/A %4.1f%%\n", 100.0 * spread);
}

// Every decoder and every table the bench times gives unicode.hh's results: category and lowercase of every
// code point, the decoding of every encoded code point (surrogates are not UTF-8), and each input that is
// valid UTF-8, code point by code point.
template <typename D>
int check_decoder(const char* name, const std::vector<Corpus>& corpora) {
	int bad = 0;
	auto fail = [&](const char* what, unsigned long where) {
		if (++bad <= 5) {
			std::fprintf(stderr, "%s: %s differs at %lu\n", name, what, where);
		}
	};
	std::string s;
	for (char32_t c = 0; c <= unicode::max_code_point; ++c) {
		if (D::category(c) != Current::category(c)) {
			fail("category or lowercase", c);
		}
		if (c >= 0xD800 && c <= 0xDFFF) {
			continue;
		}
		s.clear();
		unicode::append_utf8(s, c);
		const char* p = s.data();
		const char* end = p + s.size();
		if (D::next(p, end) != c || p != end) {
			fail("decoding", c);
		}
	}
	for (const auto& k : corpora) {
		if (!k.valid) {
			continue;
		}
		const char* p = k.text.data();
		const char* end = p + k.text.size();
		std::size_t i = 0;
		while (p < end && i < k.cps.size() && D::next(p, end) == k.cps[i]) {
			++i;
		}
		if (p != end || i != k.cps.size()) {
			fail(k.name, i);
		}
	}
	std::printf("%-12s %s\n", name, bad ? "FAIL" : "ok");
	return bad;
}

int run_checks(const std::vector<Corpus>& corpora) {
	int bad = check_decoder<Dfa>("hoehrmann", corpora);
#ifdef UNICODE_BENCH_COMPARE
	bad += check_decoder<Utf8cpp>("utf8cpp", corpora);
	bad += check_decoder<Utf8cppUnchecked>("utf8cpp-unc", corpora);
	bad += check_decoder<Utf8proc>("utf8proc", corpora);
#endif
	return bad ? 1 : 0;
}

// Well-formed UTF-8 by the DFA's rules.
bool is_valid_utf8(const std::string& text) {
	std::uint32_t state = 0;
	for (char ch : text) {
		state = dfa::table.t[256 + state + dfa::table.t[static_cast<unsigned char>(ch)]];
		if (state == dfa::reject) {
			return false;
		}
	}
	return state == 0;
}

Corpus make(const char* name, std::string text) {
	Corpus c{name, std::move(text), {}};
	c.valid = is_valid_utf8(c.text);
	const char* p = c.text.data();
	const char* end = p + c.text.size();
	while (p < end) {
		c.cps.push_back(unicode_old::next_utf8(p, end));
	}
	return c;
}

}  // namespace

int main(int argc, char** argv) {
	bool check = false, compare_only = false;
	const char* file = nullptr;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--check") == 0) {
			check = true;
		} else if (std::strcmp(argv[i], "--compare") == 0) {
			compare_only = true;
		} else {
			file = argv[i];
		}
	}
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
	if (file) {
		std::ifstream in(file, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		corpora.push_back(make("file", ss.str()));
	}
	if (check) {
		return run_checks(corpora);
	}
	if (!compare_only) {
		std::printf("unicode bench (ns per code point)\n");
		for (const auto& c : corpora) {
			decode_row<false>(c, "decode");
			decode_row<true>(c, "pipeline");
			row(c, "encode", encode_old, encode_new);
		}
	}
	compare_header();
	for (const auto& c : corpora) {
		compare_row<false>(c, "decode");
		compare_row<true>(c, "pipeline");
	}
	return 0;
}
