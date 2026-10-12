// Decodes, classifies, and lowercases a UTF-8 file (argv[1]) repeatedly; reports ns per
// code point.
#include "unicode.hh"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
	if (argc != 2) {
		std::fprintf(stderr, "usage: unicode_bench FILE\n");
		return 2;
	}
	std::ifstream in(argv[1], std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string text = ss.str();
	std::uint64_t sink = 0, cps = 0;
	auto t0 = std::chrono::steady_clock::now();
	for (int round = 0; round < 5; ++round) {
		const char* p = text.data();
		const char* end = p + text.size();
		while (p < end) {
			char32_t c = unicode::next_utf8(p, end);
			sink += std::uint64_t(unicode::category_of(c)) + unicode::tolower(c);
			++cps;
		}
	}
	double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
	std::printf("%llu code points, %.2f ns each (sink %llu)\n", (unsigned long long)cps, ns / double(cps), (unsigned long long)sink);
}
