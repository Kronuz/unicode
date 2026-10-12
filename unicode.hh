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
#include <string>
#include <string_view>

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

// Decodes the code point at p (p < end), advancing p. A byte that does not start a valid,
// shortest-form sequence decodes as itself (ISO-8859-1), one byte at a time.
constexpr char32_t next_utf8(const char*& p, const char* end) noexcept {
	const unsigned char b0 = static_cast<unsigned char>(*p);
	auto cont = [&](std::size_t i) -> int {
		if (p + i >= end) { return -1; }
		const unsigned char b = static_cast<unsigned char>(p[i]);
		return (b & 0xC0) == 0x80 ? (b & 0x3F) : -1;
	};
	if (b0 >= 0xC2 && b0 <= 0xDF) {
		int c1 = cont(1);
		if (c1 >= 0) {
			p += 2;
			return char32_t(((b0 & 0x1F) << 6) | c1);
		}
	} else if (b0 >= 0xE0 && b0 <= 0xEF) {
		int c1 = cont(1), c2 = cont(2);
		if (c1 >= 0 && c2 >= 0) {
			char32_t c = char32_t(((b0 & 0x0F) << 12) | (c1 << 6) | c2);
			if (c >= 0x800) {
				p += 3;
				return c;
			}
		}
	} else if (b0 >= 0xF0 && b0 <= 0xF4) {
		int c1 = cont(1), c2 = cont(2), c3 = cont(3);
		if (c1 >= 0 && c2 >= 0 && c3 >= 0) {
			char32_t c = char32_t(((b0 & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3);
			if (c >= 0x10000 && c <= max_code_point) {
				p += 4;
				return c;
			}
		}
	}
	++p;
	return b0;
}

}  // namespace unicode
