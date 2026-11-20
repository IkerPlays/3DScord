#ifndef VIDEO_LINKS_H
#define VIDEO_LINKS_H

#include <cctype>
#include <string>

namespace Utils {
namespace VideoLinks {

namespace detail {
inline bool idChar(char c) { return isalnum((unsigned char)c) || c == '-' || c == '_'; }
inline std::string takeId(const std::string &s, size_t from) {
	size_t end = from;
	while (end < s.size() && idChar(s[end])) {
		end++;
	}
	return end - from == 11 ? s.substr(from, 11) : std::string();
}
} // namespace detail

// The 11-character id of a YouTube video in a link (watch?v=, youtu.be/, /shorts/, /embed/, /live/, music.youtube.com), or "".
inline std::string youtubeId(const std::string &url) {
	std::string host;
	size_t pathAt = std::string::npos;
	const size_t scheme = url.find("://");
	const size_t hostFrom = scheme == std::string::npos ? 0 : scheme + 3;
	pathAt = url.find_first_of("/?#", hostFrom);
	host = url.substr(hostFrom, pathAt == std::string::npos ? std::string::npos : pathAt - hostFrom);
	for (char &c : host) {
		c = (char)tolower((unsigned char)c);
	}
	const bool shortHost = host == "youtu.be" || host == "www.youtu.be";
	const bool longHost = host == "youtube.com" || host == "www.youtube.com" || host == "m.youtube.com" ||
	                      host == "music.youtube.com" || host == "youtube-nocookie.com" || host == "www.youtube-nocookie.com";
	if (!shortHost && !longHost) {
		return "";
	}
	if (pathAt == std::string::npos) {
		return "";
	}
	if (shortHost) {
		return detail::takeId(url, pathAt + 1);
	}
	const size_t v = url.find("v=", pathAt);
	if (v != std::string::npos && v > 0 && (url[v - 1] == '?' || url[v - 1] == '&')) {
		return detail::takeId(url, v + 2);
	}
	for (const char *prefix : {"/shorts/", "/embed/", "/live/", "/v/"}) {
		const size_t at = url.find(prefix, pathAt);
		if (at == pathAt) {
			return detail::takeId(url, at + std::string(prefix).size());
		}
	}
	return "";
}

} // namespace VideoLinks
} // namespace Utils

#endif // VIDEO_LINKS_H
