#ifndef TEXT_SPLIT_H
#define TEXT_SPLIT_H

#include <string>
#include <vector>

namespace Utils {

// Cuts a long text into pieces of at most `limit` bytes for Discord (2000 characters per message). It cuts at the last line break, or the last
// space, in the second half of the piece; if there is none it cuts between two characters (never inside a UTF-8 sequence). The blanks at the
// cut are dropped. Nothing but blanks gives no piece.
inline std::vector<std::string> splitText(std::string rest, size_t limit = 1900) {
	std::vector<std::string> pieces;
	while (!rest.empty()) {
		std::string piece;
		if (rest.size() <= limit) {
			piece = rest;
			rest.clear();
		} else {
			size_t cut = rest.find_last_of('\n', limit);
			if (cut == std::string::npos || cut < limit / 2) {
				cut = rest.find_last_of(' ', limit);
			}
			if (cut == std::string::npos || cut < limit / 2) {
				cut = limit;
				while (cut > 0 && (static_cast<unsigned char>(rest[cut]) & 0xC0) == 0x80) {
					cut--;
				}
			}
			piece = rest.substr(0, cut);
			rest.erase(0, cut);
			const size_t first = rest.find_first_not_of(" \n");
			rest.erase(0, first == std::string::npos ? rest.size() : first);
		}
		if (!piece.empty()) {
			pieces.push_back(piece);
		}
	}
	return pieces;
}

} // namespace Utils

#endif // TEXT_SPLIT_H
