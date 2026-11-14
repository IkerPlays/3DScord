// Host test of Utils::splitText (long messages are sent in several). Build and run: see .github/workflows/tests.yml
#include "utils/text_split.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                                                                                                  \
	do {                                                                                                                                             \
		if (!(cond)) {                                                                                                                               \
			printf("FAILED line %d: %s\n", __LINE__, #cond);                                                                                          \
			failures++;                                                                                                                              \
		}                                                                                                                                            \
	} while (0)

int main() {
	CHECK(Utils::splitText("").empty());
	CHECK(Utils::splitText("hello").size() == 1);
	// Words: every piece fits and nothing is lost.
	std::string words;
	for (int i = 0; i < 1000; i++) {
		words += "word" + std::to_string(i) + " ";
	}
	auto pieces = Utils::splitText(words, 100);
	std::string joined;
	for (const auto &p : pieces) {
		CHECK(p.size() <= 100);
		CHECK(!p.empty());
		joined += p + " ";
	}
	CHECK(pieces.size() > 10);
	CHECK(joined.find("word999") != std::string::npos);
	// Lines are preferred.
	auto lines = Utils::splitText(std::string(60, 'a') + "\n" + std::string(60, 'b'), 100);
	CHECK(lines.size() == 2 && lines[0] == std::string(60, 'a') && lines[1] == std::string(60, 'b'));
	// No blanks: it cuts between characters, never inside a 2-byte one.
	std::string accents;
	for (int i = 0; i < 200; i++) {
		accents += "\xC3\xA9"; // e acute
	}
	for (const auto &p : Utils::splitText(accents, 99)) {
		CHECK(p.size() % 2 == 0);
		CHECK(p.size() <= 99);
	}
	CHECK(Utils::splitText("   ", 100).size() == 1); // only blanks fit in one piece and stay as they are
	printf(failures ? "%d failures\n" : "all good\n", failures);
	return failures ? 1 : 0;
}
