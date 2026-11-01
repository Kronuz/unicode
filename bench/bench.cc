// Benchmark: next_utf8, the decode + classify + lowercase loop, and append_utf8 against the frozen
// original (test/unicode_old.hh), over text of several shapes. Reports ns per code point.
//
// Rows: "decode" sums the code points; "pipeline" decodes, then adds category_of and tolower (what a
// tokenizer does); "encode" appends every code point to a std::string (the same code before and
// after). Each row alternates the frozen original and the current code over several rounds and keeps
// the best pass of each. "A/A" is the spread between the frozen original's best in even and odd
// rounds: it shows how much the host moved during the row.
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

std::uint64_t decode_old(const Corpus& c) {
	const char* p = c.text.data();
	const char* end = p + c.text.size();
	std::uint64_t s = 0;
	while (p < end) {
		s += unicode_old::next_utf8(p, end);
	}
	return s;
}

std::uint64_t decode_new(const Corpus& c) {
	const char* p = c.text.data();
	const char* end = p + c.text.size();
	std::uint64_t s = 0;
	while (p < end) {
		s += unicode::next_utf8(p, end);
	}
	return s;
}

std::uint64_t pipeline_old(const Corpus& c) {
	const char* p = c.text.data();
	const char* end = p + c.text.size();
	std::uint64_t s = 0;
	while (p < end) {
		char32_t ch = unicode_old::next_utf8(p, end);
		s += std::uint64_t(unicode_old::category_of(ch)) + unicode_old::tolower(ch);
	}
	return s;
}

std::uint64_t pipeline_new(const Corpus& c) {
	const char* p = c.text.data();
	const char* end = p + c.text.size();
	std::uint64_t s = 0;
	while (p < end) {
		char32_t ch = unicode::next_utf8(p, end);
		s += std::uint64_t(unicode::category_of(ch)) + unicode::tolower(ch);
	}
	return s;
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
	corpora.push_back(make("english+", english_plus()));
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
		row(c, "decode", decode_old, decode_new);
		row(c, "pipeline", pipeline_old, pipeline_new);
		row(c, "encode", encode_old, encode_new);
	}
	return 0;
}
