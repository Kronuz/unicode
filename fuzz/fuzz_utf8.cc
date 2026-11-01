// libFuzzer target: decoding arbitrary bytes stays in bounds, always advances, equals the frozen
// original's decoder, and re-encoding a decoded multi-byte sequence reproduces its bytes.
#include "unicode.hh"
#include "../test/unicode_old.hh"

#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const char* p = reinterpret_cast<const char*>(data);
	const char* po = p;
	const char* end = p + size;
	std::string out;
	while (p < end) {
		const char* start = p;
		char32_t c = unicode::next_utf8(p, end);
		if (p <= start || p > end || c > unicode::max_code_point) { std::abort(); }
		if (unicode_old::next_utf8(po, end) != c || po != p) { std::abort(); }
		if (p - start > 1) {
			out.clear();
			unicode::append_utf8(out, c);
			if (out.size() != std::size_t(p - start) || std::memcmp(out.data(), start, out.size()) != 0) { std::abort(); }
		}
		(void)unicode::category_of(c);
		(void)unicode::tolower(c);
	}
	return 0;
}
