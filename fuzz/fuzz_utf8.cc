// libFuzzer target: decoding arbitrary bytes stays in bounds, always advances, and
// re-encoding a decoded multi-byte sequence reproduces its bytes.
#include "unicode.hh"

#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const char* p = reinterpret_cast<const char*>(data);
	const char* end = p + size;
	std::string out;
	while (p < end) {
		const char* start = p;
		char32_t c = unicode::next_utf8(p, end);
		if (p <= start || p > end || c > unicode::max_code_point) { std::abort(); }
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
