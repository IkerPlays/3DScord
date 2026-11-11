#include "discord/threads.h"
#include "core/config.h"
#include "log.h"
#include "network/http_client.h"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace Discord {
namespace Threads {

namespace {

constexpr const char *API = "https://discord.com/api/v10";

Result toResult(const Network::HttpResponse &resp) {
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
		Logger::log("[Threads] Request failed: %ld %s", resp.statusCode, r.message.c_str());
	}
	return r;
}

Network::HttpClient authorized() {
	Network::HttpClient http;
	http.setAuthToken(Config::getInstance().getToken());
	http.setTimeout(20);
	return http;
}

} // namespace

Created createFromMessage(const std::string &channelId, const std::string &messageId, const std::string &name) {
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> w(s);
	w.StartObject();
	w.Key("name");
	w.String(name.c_str());
	w.Key("auto_archive_duration");
	w.Int(1440);
	w.EndObject();

	Network::HttpClient http = authorized();
	Network::HttpResponse resp =
	    http.post(std::string(API) + "/channels/" + channelId + "/messages/" + messageId + "/threads", s.GetString());

	Created created;
	created.result = toResult(resp);
	if (resp.success) {
		rapidjson::Document doc;
		doc.Parse(resp.body.c_str());
		if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("id") && doc["id"].IsString()) {
			created.threadId = doc["id"].GetString();
		}
	}
	return created;
}

Result join(const std::string &threadId) {
	Network::HttpClient http = authorized();
	return toResult(http.put(std::string(API) + "/channels/" + threadId + "/thread-members/@me"));
}

Result setArchived(const std::string &threadId, bool archived) {
	Network::HttpClient http = authorized();
	return toResult(http.patch(std::string(API) + "/channels/" + threadId,
	                           archived ? "{\"archived\":true}" : "{\"archived\":false}"));
}

} // namespace Threads
} // namespace Discord
