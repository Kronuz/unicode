// Differential test: the decoder, the encoder and the tables against the frozen original
// (unicode_old.hh).
//
// next_utf8 must give the same code point and advance the same number of bytes as the original for
// every input: every string of one to three bytes, every encoded code point (surrogates included)
// with each of its bytes replaced, every cut of a sequence at the end of the buffer (the bytes past
// `end` hold valid continuations, so a read beyond `end` changes the answer), and random text.
//
// Run: unicode_diff_test [--full] [--stress N]   (--full: every code point; N times as many random inputs)
#include "unicode.hh"
#include "unicode_old.hh"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

std::size_t checked = 0;
std::size_t failures = 0;

#define FAIL(...) \
	do { \
		if (++failures <= 20) { \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			std::printf(__VA_ARGS__); \
			std::printf("\n"); \
		} \
	} while (0)

struct Rng {
	std::uint64_t s;
	std::uint64_t operator()() {
		std::uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
		return z ^ (z >> 31);
	}
	std::size_t below(std::size_t n) { return static_cast<std::size_t>((*this)() % n); }
};

// One decode of the bytes [buf, buf + len), compared with the original's.
void check_one(const unsigned char* buf, std::size_t len) {
	const char* base = reinterpret_cast<const char*>(buf);
	const char* po = base;
	const char* pn = base;
	char32_t co = unicode_old::next_utf8(po, base + len);
	char32_t cn = unicode::next_utf8(pn, base + len);
	++checked;
	if (co != cn || po != pn) {
		FAIL("next_utf8 of %02x %02x %02x %02x (cut at %zu): want U+%04x +%td got U+%04x +%td", buf[0], buf[1], buf[2],
		     buf[3], len, static_cast<unsigned>(co), po - base, static_cast<unsigned>(cn), pn - base);
	}
}

// All the cuts of `bytes` (the buffer is padded with continuation bytes, the worst case for a
// read past the end).
void check_cuts(const unsigned char* bytes, std::size_t n) {
	unsigned char buf[16];
	std::memset(buf, 0xBF, sizeof buf);
	std::memcpy(buf, bytes, n);
	for (std::size_t len = 1; len <= n; ++len) {
		check_one(buf, len);
	}
}

void test_all_short_strings() {
	// Every string of one, two and three bytes, each at every cut.
	unsigned char b[4] = {0, 0xBF, 0xBF, 0xBF};
	for (unsigned x = 0; x < 256; ++x) {
		b[0] = static_cast<unsigned char>(x);
		check_cuts(b, 1);
		for (unsigned y = 0; y < 256; ++y) {
			b[1] = static_cast<unsigned char>(y);
			check_cuts(b, 2);
			for (unsigned z = 0; z < 256; ++z) {
				b[2] = static_cast<unsigned char>(z);
				check_cuts(b, 3);
			}
		}
	}
	// Four bytes: every lead byte that can start one (and its neighbors) over the bytes that matter.
	static const unsigned char interesting[] = {0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2,
	                                            0xDF, 0xE0, 0xED, 0xEF, 0xF0, 0xF4, 0xF5, 0xFF};
	for (unsigned lead = 0xEF; lead <= 0xF8; ++lead) {
		for (unsigned char x : interesting) {
			for (unsigned char y : interesting) {
				for (unsigned char z : interesting) {
					unsigned char s[4] = {static_cast<unsigned char>(lead), x, y, z};
					check_cuts(s, 4);
				}
			}
		}
	}
	std::printf("short strings: %zu checks\n", checked);
}

void test_every_code_point(bool full) {
	std::size_t before = checked;
	// The default sweep takes every code point near a boundary of the encoding (all of them below
	// U+0900, the surrogates, the end of the BMP and of Unicode) and every eleventh elsewhere;
	// --full takes them all, with more replacement bytes.
	static const unsigned char repl_full[] = {0x00, 0x41, 0x7F, 0x80, 0xBF, 0xC0, 0xC2, 0xE0, 0xF0, 0xF4, 0xF5, 0xFF};
	static const unsigned char repl_quick[] = {0x00, 0x7F, 0x80, 0xBF, 0xC2, 0xF4};
	const auto* repl = full ? repl_full : repl_quick;
	const std::size_t repl_count = full ? sizeof repl_full : sizeof repl_quick;
	for (char32_t c = 0; c <= unicode::max_code_point; ++c) {
		const bool boundary = c < 0x900 || (c >= 0xD7F0 && c <= 0xE010) || (c >= 0xFFF0 && c <= 0x10010) || c >= 0x10FFF0;
		if (!full && !boundary && c % 11 != 0) {
			continue;
		}
		std::string s;
		unicode::append_utf8(s, c);  // surrogates encode as three bytes
		unsigned char buf[8];
		std::memset(buf, 0xBF, sizeof buf);
		std::memcpy(buf, s.data(), s.size());
		check_cuts(buf, s.size());
		// Replace each byte, in turn, by a few other values.
		for (std::size_t i = 0; i < s.size(); ++i) {
			for (std::size_t k = 0; k < repl_count; ++k) {
				unsigned char t[8];
				std::memcpy(t, buf, sizeof t);
				t[i] = repl[k];
				check_cuts(t, s.size());
			}
		}
	}
	std::printf("every code point: %zu checks\n", checked - before);
}

void test_streams(std::size_t scale) {
	Rng rng{0x7f8};
	std::size_t before = checked;
	for (std::size_t n = 0; n < 30000 * scale; ++n) {
		// A buffer of random bytes, ASCII, and well-formed sequences, decoded to the end.
		std::string s;
		auto parts = rng.below(40);
		for (std::size_t k = 0; k < parts; ++k) {
			switch (rng.below(4)) {
				case 0:
					s.push_back(static_cast<char>(rng()));
					break;
				case 1:
					s.push_back(static_cast<char>(0x20 + rng.below(0x5f)));
					break;
				case 2:
					unicode::append_utf8(s, static_cast<char32_t>(rng.below(0x110000)));
					break;
				default:
					unicode::append_utf8(s, static_cast<char32_t>(0x80 + rng.below(0x700)));
					break;
			}
		}
		const char* po = s.data();
		const char* pn = s.data();
		const char* end = s.data() + s.size();
		while (po < end) {
			char32_t co = unicode_old::next_utf8(po, end);
			char32_t cn = unicode::next_utf8(pn, end);
			++checked;
			if (co != cn || po != pn) {
				FAIL("stream decode diverges at byte %td: want U+%04x got U+%04x", po - s.data(), static_cast<unsigned>(co),
				     static_cast<unsigned>(cn));
				break;
			}
		}
	}
	std::printf("streams: %zu checks\n", checked - before);
}

// The window of eight ASCII bytes: one byte of each value at each position of an ASCII buffer, and
// pairs of them, decoded to the end at every length around the window (the last bytes of the buffer
// take the path without a window).
void test_windows() {
	std::size_t before = checked;
	auto same_stream = [](const unsigned char* buf, std::size_t len) {
		const char* base = reinterpret_cast<const char*>(buf);
		const char* po = base;
		const char* pn = base;
		const char* end = base + len;
		while (po < end) {
			char32_t co = unicode_old::next_utf8(po, end);
			char32_t cn = unicode::next_utf8(pn, end);
			++checked;
			if (co != cn || po != pn) {
				FAIL("window stream differs at byte %td of %zu: want U+%04x +%td got U+%04x +%td", po - base, len,
				     static_cast<unsigned>(co), po - base, static_cast<unsigned>(cn), pn - base);
				return;
			}
		}
	};
	unsigned char buf[24];
	for (std::size_t pos = 0; pos < 14; ++pos) {
		for (unsigned v = 0; v < 256; ++v) {
			std::memset(buf, 'a', sizeof buf);
			buf[pos] = static_cast<unsigned char>(v);
			for (std::size_t len = 1; len <= 20; ++len) {
				same_stream(buf, len);
			}
		}
	}
	static const unsigned char vals[] = {0x00, 0x7F, 0x80, 0xA9, 0xBF, 0xC2, 0xC3, 0xE2, 0xF0, 0xF4, 0xFF};
	for (std::size_t i = 0; i < 12; ++i) {
		for (std::size_t j = i + 1; j < 14; ++j) {
			for (unsigned char x : vals) {
				for (unsigned char y : vals) {
					std::memset(buf, 'a', sizeof buf);
					buf[i] = x;
					buf[j] = y;
					for (std::size_t len = 1; len <= 18; ++len) {
						same_stream(buf, len);
					}
				}
			}
		}
	}
	std::printf("windows: %zu checks\n", checked - before);
}

#if defined(__unix__) || defined(__APPLE__)
// Decodes text that ends at the last byte before an inaccessible page: a read past `end` (by the
// window or by a sequence) crashes the test.
void test_guard_page() {
	const std::size_t page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
	void* region = mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (region == MAP_FAILED || mprotect(static_cast<char*>(region) + page, page, PROT_NONE) != 0) {
		std::printf("guard page: skipped (no mmap)\n");
		return;
	}
	std::size_t before = checked;
	Rng rng{0x6a7d};
	for (std::size_t len = 1; len <= 40; ++len) {
		for (std::size_t round = 0; round < 400; ++round) {
			std::string s;
			while (s.size() < len) {
				switch (rng.below(5)) {
					case 0:
					case 1:
						s.push_back(static_cast<char>(0x20 + rng.below(0x5f)));
						break;
					case 2:
						s.push_back(static_cast<char>(rng()));
						break;
					case 3:
						unicode::append_utf8(s, static_cast<char32_t>(0x80 + rng.below(0x700)));
						break;
					default:
						unicode::append_utf8(s, static_cast<char32_t>(rng.below(0x110000)));
						break;
				}
			}
			s.resize(len);  // a sequence may be cut: the byte after it is the inaccessible page
			char* at = static_cast<char*>(region) + page - len;
			std::memcpy(at, s.data(), len);
			const char* po = at;
			const char* pn = at;
			const char* end = at + len;
			while (po < end) {
				char32_t co = unicode_old::next_utf8(po, end);
				char32_t cn = unicode::next_utf8(pn, end);
				++checked;
				if (co != cn || po != pn) {
					FAIL("guard page stream differs at byte %td of %zu", po - at, len);
					break;
				}
			}
		}
	}
	munmap(region, 2 * page);
	std::printf("guard page: %zu checks\n", checked - before);
}
#endif

void test_encode_and_tables() {
	std::size_t before = checked;
	for (char32_t c = 0; c <= unicode::max_code_point + 3; ++c) {
		std::string a = "x", b = "x";
		unicode_old::append_utf8(a, c);
		unicode::append_utf8(b, c);
		++checked;
		if (a != b) {
			FAIL("append_utf8(U+%04x)", static_cast<unsigned>(c));
		}
		if (static_cast<int>(unicode_old::category_of(c)) != static_cast<int>(unicode::category_of(c)) ||
		    unicode_old::tolower(c) != unicode::tolower(c) || unicode_old::is_letter(c) != unicode::is_letter(c) ||
		    unicode_old::is_mark(c) != unicode::is_mark(c) || unicode_old::is_number(c) != unicode::is_number(c)) {
			FAIL("tables at U+%04x", static_cast<unsigned>(c));
		}
	}
	// Large code points and the top of the range.
	for (char32_t c : {char32_t(0x110000), char32_t(0x7fffffff), char32_t(0x80000000), char32_t(0xffffffff)}) {
		std::string a, b;
		unicode_old::append_utf8(a, c);
		unicode::append_utf8(b, c);
		++checked;
		if (a != b || unicode_old::tolower(c) != unicode::tolower(c)) {
			FAIL("above the range: U+%08x", static_cast<unsigned>(c));
		}
	}
	std::printf("encode and tables: %zu checks\n", checked - before);
}

// The decoder in constant evaluation.
constexpr char32_t decode_at_compile_time(const char* s, std::size_t n) {
	const char* p = s;
	char32_t last = 0;
	while (p < s + n) {
		last = unicode::next_utf8(p, s + n);
	}
	return last;
}
static_assert(decode_at_compile_time("a\xC3\xA9", 3) == 0xE9);
static_assert(decode_at_compile_time("a\xE2\x82\xAC", 4) == 0x20AC);
static_assert(decode_at_compile_time("\xF0\x9F\x98\x80", 4) == 0x1F600);
static_assert(decode_at_compile_time("\xF0\x9F\x98", 3) == 0x98);

}  // namespace

int main(int argc, char** argv) {
	std::size_t scale = 1;
	bool full = false;
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--full") == 0) {
			full = true;
		} else if (std::strcmp(argv[i], "--stress") == 0 && i + 1 < argc) {
			scale = static_cast<std::size_t>(std::strtoull(argv[i + 1], nullptr, 10));
		}
	}
	test_all_short_strings();
	test_every_code_point(full);
	test_streams(scale);
	test_windows();
#if defined(__unix__) || defined(__APPLE__)
	test_guard_page();
#endif
	test_encode_and_tables();
	if (failures != 0) {
		std::printf("%zu of %zu checks DIFFER from the frozen original\n", failures, checked);
		return 1;
	}
	std::printf("all unicode differential tests passed (%zu checks)\n", checked);
	return 0;
}
