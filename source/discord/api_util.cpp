#include "discord/api_util.h"
#include "core/config.h"
#include "log.h"
#include <rapidjson/document.h>

namespace Discord {
namespace Api {

Result toResult(const Network::HttpResponse &resp, const char *tag) {
	Result r;
	r.ok = resp.success;
	r.status = resp.statusCode;
	if (!resp.success) {
		rapidjson::Document doc;
		doc.Parse(resp.body.c_str());
		if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("message") && doc["message"].IsString()) {
			r.message = doc["message"].GetString();
		}
		if (r.message.empty()) {
			r.message = resp.error.empty() ? "HTTP " + std::to_string(resp.statusCode) : resp.error;
		}
		Logger::log("[%s] Request failed: %ld %s", tag, resp.statusCode, r.message.c_str());
	}
	return r;
}

Network::HttpClient authorized(long timeoutSeconds) {
	Network::HttpClient http;
	http.setAuthToken(Config::getInstance().getToken());
	http.setTimeout(timeoutSeconds);
	return http;
}

} // namespace Api
} // namespace Discord
