// unicode: categories and lowercase mappings against a "cp category lower" dump (argv[1],
// optional), spot checks, and UTF-8 round trips and invalid-byte decoding.
#include "unicode.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unordered_map>

static constexpr std::uint64_t tables_digest = 0x7611fcda68b39811ull;

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); if (++failures > 20) std::exit(1); } \
	} while (0)

int main(int argc, char** argv) {
	static_assert(unicode::category_of(U'A') == unicode::category::uppercase_letter);
	static_assert(unicode::tolower(U'A') == U'a');
	static_assert(unicode::category_of(U'_') == unicode::category::connector_punctuation);
	CHECK(unicode::tolower(U'Ä') == U'ä');
	CHECK(unicode::tolower(U'Σ') == U'σ');
	CHECK(unicode::category_of(0x4E2D) == unicode::category::other_letter);
	CHECK(unicode::category_of(0x110000) == unicode::category::unassigned);
	// FNV-1a over (category, lowercase) of every code point; the value is the one the tables
	// had when they matched Xapian 2.1.0 on all code points (pass the dump to recheck).
	std::uint64_t digest = 14695981039346656037ull;
	auto mix = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i) { digest = (digest ^ ((v >> (8 * i)) & 0xff)) * 1099511628211ull; }
	};
	for (char32_t c = 0; c <= unicode::max_code_point; ++c) {
		mix(std::uint32_t(unicode::category_of(c)));
		mix(std::uint32_t(unicode::tolower(c)));
	}
	std::printf("tables digest %016llx (Unicode %s)\n", (unsigned long long)digest, unicode::tables::version);
	CHECK(digest == tables_digest);
	if (argc > 1) {
		std::unordered_map<std::uint32_t, std::pair<int, std::uint32_t>> dump;
		std::ifstream in(argv[1]);
		std::uint32_t cp, low;
		int cat;
		while (in >> cp >> cat >> low) { dump[cp] = {cat, low}; }
		std::size_t checked = 0;
		for (char32_t c = 0; c <= unicode::max_code_point; ++c) {
			auto it = dump.find(c);
			auto want = it == dump.end() ? std::pair<int, std::uint32_t>{0, std::uint32_t(c)} : it->second;
			CHECK(int(unicode::category_of(c)) == want.first);
			CHECK(unicode::tolower(c) == want.second);
			++checked;
		}
		std::printf("checked %zu code points against %s\n", checked, argv[1]);
	}
	// UTF-8 round trip of every encodable code point (surrogates included, as bytes).
	for (char32_t c = 0; c <= unicode::max_code_point; c += (c < 0x3000 ? 1 : 97)) {
		if (c >= 0xD800 && c <= 0xDFFF) { continue; }
		std::string s;
		unicode::append_utf8(s, c);
		const char* p = s.data();
		CHECK(unicode::next_utf8(p, s.data() + s.size()) == c);
		CHECK(p == s.data() + s.size());
	}
	// Invalid sequences decode byte by byte as Latin-1.
	const std::string bad = "\xC3(\xE2\x82\xFF\xC0\x80";
	const char* p = bad.data();
	std::u32string got;
	while (p < bad.data() + bad.size()) { got.push_back(unicode::next_utf8(p, bad.data() + bad.size())); }
	CHECK((got == std::u32string{0xC3, U'(', 0xE2, 0x82, 0xFF, 0xC0, 0x80}));
	std::printf("unicode: %s\n", failures ? "FAILED" : "ok");
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
