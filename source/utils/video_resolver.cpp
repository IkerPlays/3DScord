#include "utils/video_resolver.h"
#include "core/config.h"
#include "log.h"
#include "network/http_client.h"
#include "utils/file_utils.h"
#include <algorithm>
#include <cstdlib>
#include <rapidjson/document.h>
#include <sstream>
#include <vector>

namespace Utils {
namespace VideoResolver {

namespace {

struct Server {
	bool piped;
	std::string base;
};

std::vector<Server> servers() {
	std::vector<Server> list;
	const std::vector<char> file = Utils::File::readFile(std::string(CONFIG_DIR_PATH) + "/video_servers.txt");
	if (!file.empty()) {
		std::istringstream in(std::string(file.begin(), file.end()));
		std::string line;
		while (std::getline(in, line)) {
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
				line.pop_back();
			}
			std::istringstream words(line);
			std::string kind, base;
			words >> kind >> base;
			if ((kind == "piped" || kind == "invidious") && base.rfind("https://", 0) == 0) {
				while (!base.empty() && base.back() == '/') {
					base.pop_back();
				}
				list.push_back({kind == "piped", base});
			}
		}
	}
	// Public servers come and go, so these may stop working: that is what the file above is for.
	list.push_back({true, "https://pipedapi.kavin.rocks"});
	list.push_back({false, "https://inv.nadeko.net"});
	list.push_back({false, "https://yewtu.be"});
	list.push_back({false, "https://invidious.nerdvpn.de"});
	return list;
}

constexpr long long MAX_BYTES = 16LL * 1024 * 1024;

// The height in a quality label such as "360p" or "360p60".
int heightOf(const std::string &label) { return atoi(label.c_str()); }

// Chooses the best stream we can play: MP4 with H.264 (avc1) and sound, up to 360 lines, under the size limit when it is known.
// A candidate is {url, height, bytes}.
struct Candidate {
	std::string url;
	int height = 0;
	long long bytes = 0;
};

void consider(std::vector<Candidate> &out, const std::string &url, int height, long long bytes) {
	if (!url.empty() && height > 0 && height <= 360 && (bytes <= 0 || bytes <= MAX_BYTES)) {
		out.push_back({url, height, bytes});
	}
}

bool fromPiped(const rapidjson::Document &doc, std::vector<Candidate> &out) {
	if (!doc.IsObject() || !doc.HasMember("videoStreams") || !doc["videoStreams"].IsArray()) {
		return false;
	}
	for (const auto &s : doc["videoStreams"].GetArray()) {
		if (!s.IsObject() || !s.HasMember("url") || !s["url"].IsString()) {
			continue;
		}
		const bool videoOnly = s.HasMember("videoOnly") && s["videoOnly"].IsBool() && s["videoOnly"].GetBool();
		const std::string mime = s.HasMember("mimeType") && s["mimeType"].IsString() ? s["mimeType"].GetString() : "";
		if (videoOnly || mime.find("video/mp4") == std::string::npos || mime.find("avc1") == std::string::npos) {
			continue;
		}
		const int height = s.HasMember("height") && s["height"].IsInt() ? s["height"].GetInt()
		                   : s.HasMember("quality") && s["quality"].IsString() ? heightOf(s["quality"].GetString())
		                                                                       : 0;
		const long long bytes = s.HasMember("contentLength") && s["contentLength"].IsInt64() ? s["contentLength"].GetInt64() : 0;
		consider(out, s["url"].GetString(), height, bytes);
	}
	return true;
}

bool fromInvidious(const rapidjson::Document &doc, const std::string &base, std::vector<Candidate> &out) {
	if (!doc.IsObject() || !doc.HasMember("formatStreams") || !doc["formatStreams"].IsArray()) {
		return false;
	}
	for (const auto &s : doc["formatStreams"].GetArray()) {
		if (!s.IsObject() || !s.HasMember("url") || !s["url"].IsString()) {
			continue;
		}
		const std::string type = s.HasMember("type") && s["type"].IsString() ? s["type"].GetString() : "";
		if (type.find("video/mp4") == std::string::npos || type.find("avc1") == std::string::npos) {
			continue;
		}
		std::string url = s["url"].GetString();
		if (!url.empty() && url[0] == '/') {
			url = base + url;
		}
		const int height = s.HasMember("resolution") && s["resolution"].IsString() ? heightOf(s["resolution"].GetString()) : 0;
		const long long bytes = s.HasMember("clen") && s["clen"].IsString() ? atoll(s["clen"].GetString()) : 0;
		consider(out, url, height, bytes);
	}
	return true;
}

} // namespace

bool resolveYoutube(const std::string &videoId, std::string &fileUrl, std::string &error) {
	error.clear();
	int tried = 0;
	for (const Server &server : servers()) {
		if (tried >= 4) {
			break;
		}
		tried++;
		Network::HttpClient http;
		http.setTimeout(8);
		const std::string url = server.piped ? server.base + "/streams/" + videoId
		                                     : server.base + "/api/v1/videos/" + videoId + "?fields=formatStreams";
		const Network::HttpResponse resp = http.get(url);
		if (!resp.success || resp.statusCode != 200 || resp.body.empty()) {
			Logger::log("[VideoLink] %s answered %ld", server.base.c_str(), resp.statusCode);
			error = "server";
			continue;
		}
		rapidjson::Document doc;
		doc.Parse(resp.body.c_str());
		if (doc.HasParseError()) {
			error = "server";
			continue;
		}
		std::vector<Candidate> candidates;
		const bool understood = server.piped ? fromPiped(doc, candidates) : fromInvidious(doc, server.base, candidates);
		if (!understood || candidates.empty()) {
			error = understood ? "format" : "server";
			continue;
		}
		// 360p is the best the console can show; the next one down when there is none.
		std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.height > b.height; });
		fileUrl = candidates.front().url;
		Logger::log("[VideoLink] %s gave a %dp file", server.base.c_str(), candidates.front().height);
		return true;
	}
	if (error.empty()) {
		error = "server";
	}
	return false;
}

} // namespace VideoResolver
} // namespace Utils
