/*
 * Copyright (c) 2026 Germán Méndez Bravo (Kronuz)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Unicode general categories and simple lowercase mappings (Unicode 17.0.0, generated
// from UnicodeData.txt by tools/gen_tables.py), and UTF-8 decoding and encoding.

#pragma once

#include "unicode_tables.hh"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

#if defined(__GNUC__) || defined(__clang__)
#define UNICODE_OUT_OF_LINE __attribute__((noinline, aligned(64)))
#elif defined(_MSC_VER)
#define UNICODE_OUT_OF_LINE __declspec(noinline)
#else
#define UNICODE_OUT_OF_LINE
#endif

namespace unicode {

// General categories, in this order.
enum class category : std::uint8_t {
	unassigned, uppercase_letter, lowercase_letter, titlecase_letter, modifier_letter, other_letter,
	non_spacing_mark, enclosing_mark, spacing_mark, decimal_number, letter_number, other_number,
	space_separator, line_separator, paragraph_separator, control, format, private_use, surrogate,
	connector_punctuation, dash_punctuation, open_punctuation, close_punctuation, initial_punctuation,
	final_punctuation, other_punctuation, math_symbol, currency_symbol, modifier_symbol, other_symbol,
};

inline constexpr char32_t max_code_point = 0x10FFFF;

constexpr category category_of(char32_t c) noexcept {
	if (c > max_code_point) { return category::unassigned; }
	return category(tables::category_stage2[tables::category_stage1[c >> 8]][c & 0xff]);
}

constexpr char32_t tolower(char32_t c) noexcept {
	if (c > max_code_point) { return c; }
	return char32_t(std::int32_t(c) + tables::lower_stage2[tables::lower_stage1[c >> 8]][c & 0xff]);
}

constexpr bool is_letter(char32_t c) noexcept {
	auto k = category_of(c);
	return k >= category::uppercase_letter && k <= category::other_letter;
}
constexpr bool is_mark(char32_t c) noexcept {
	auto k = category_of(c);
	return k >= category::non_spacing_mark && k <= category::spacing_mark;
}
constexpr bool is_number(char32_t c) noexcept {
	auto k = category_of(c);
	return k >= category::decimal_number && k <= category::other_number;
}

// Appends c as UTF-8 (code points above max_code_point are not encodable and are skipped).
inline void append_utf8(std::string& out, char32_t c) {
	if (c < 0x80) {
		out.push_back(char(c));
	} else if (c < 0x800) {
		out.push_back(char(0xC0 | (c >> 6)));
		out.push_back(char(0x80 | (c & 0x3F)));
	} else if (c < 0x10000) {
		out.push_back(char(0xE0 | (c >> 12)));
		out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
		out.push_back(char(0x80 | (c & 0x3F)));
	} else if (c <= max_code_point) {
		out.push_back(char(0xF0 | (c >> 18)));
		out.push_back(char(0x80 | ((c >> 12) & 0x3F)));
		out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
		out.push_back(char(0x80 | (c & 0x3F)));
	}
}

namespace detail {

// A decoded code point and the address of the byte after it.
struct utf8_step {
	char32_t cp;
	const char* next;
};

// Decodes the code point at p (p < end) whose first byte is b0 = *p. A continuation byte b is valid
// when (b ^ 0x80) < 0x40, and that value is its six payload bits. This is every case but ASCII
// inside a window of ASCII; next_utf8 calls it for the rest. It is not inlined, it returns its
// two results in registers (so a caller's loop keeps `p` out of memory) and it starts on a 64-byte
// boundary, so its hot path sits in one cache line. On an Intel i9 the same code at an arbitrary
// address decoded Cyrillic about 20% and CJK about 25% slower.
UNICODE_OUT_OF_LINE constexpr utf8_step decode_utf8(const char* p, const char* end, unsigned b0) noexcept {
	const auto left = end - p;
	auto cont = [&](int i) -> unsigned { return static_cast<unsigned char>(p[i]) ^ 0x80u; };
	if (b0 >= 0xC2 && b0 <= 0xDF) {
		if (left >= 2) {
			const unsigned c1 = cont(1);
			if (c1 < 0x40) {
				return {char32_t(((b0 & 0x1F) << 6) | c1), p + 2};
			}
		}
	} else if (b0 >= 0xE0 && b0 <= 0xEF) {
		if (left >= 3) {
			const unsigned c1 = cont(1), c2 = cont(2);
			if ((c1 | c2) < 0x40) {
				char32_t c = char32_t(((b0 & 0x0F) << 12) | (c1 << 6) | c2);
				if (c >= 0x800) {
					return {c, p + 3};
				}
			}
		}
	} else if (b0 >= 0xF0 && b0 <= 0xF4) {
		if (left >= 4) {
			const unsigned c1 = cont(1), c2 = cont(2), c3 = cont(3);
			if ((c1 | c2 | c3) < 0x40) {
				char32_t c = char32_t(((b0 & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3);
				if (c >= 0x10000 && c <= max_code_point) {
					return {c, p + 4};
				}
			}
		}
	}
	return {b0, p + 1};
}

}  // namespace detail

// Decodes the code point at p (p < end), advancing p. A byte that does not start a valid,
// shortest-form sequence decodes as itself (ISO-8859-1), one byte at a time.
//
// Eight bytes that are all ASCII (one load and one mask) decide the common case. Anything else, a
// multi-byte sequence, an invalid byte, or the last seven bytes of the text, goes through
// detail::decode_utf8, whose branches are those of a plain range test on the lead byte. Testing the
// first byte alone for ASCII is faster on English text but adds an unpredictable branch on every
// other kind of input.
constexpr char32_t next_utf8(const char*& p, const char* end) noexcept {
	const unsigned b0 = static_cast<unsigned char>(*p);
	if (!std::is_constant_evaluated() && end - p >= 8) {
		std::uint64_t window;
		std::memcpy(&window, p, sizeof window);
		if ((window & 0x8080808080808080ull) == 0) {
			++p;
			return b0;
		}
	} else if (b0 < 0x80) {
		++p;
		return b0;
	}
	const auto step = detail::decode_utf8(p, end, b0);
	p = step.next;
	return step.cp;
}


}  // namespace unicode

#undef UNICODE_OUT_OF_LINE
