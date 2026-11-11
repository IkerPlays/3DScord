#include "discord/threads.h"
#include "discord/api_util.h"
#include "core/config.h"
#include "log.h"
#include "network/http_client.h"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace Discord {
namespace Threads {

Created createFromMessage(const std::string &channelId, const std::string &messageId, const std::string &name) {
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> w(s);
	w.StartObject();
	w.Key("name");
	w.String(name.c_str());
	w.Key("auto_archive_duration");
	w.Int(1440);
	w.EndObject();

	Network::HttpClient http = Api::authorized();
	Network::HttpResponse resp =
	    http.post(std::string(Api::BASE) + "/channels/" + channelId + "/messages/" + messageId + "/threads", s.GetString());

	Created created;
	created.result = Api::toResult(resp, "Threads");
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
	Network::HttpClient http = Api::authorized();
	return Api::toResult(http.put(std::string(Api::BASE) + "/channels/" + threadId + "/thread-members/@me"), "Threads");
}

Result setArchived(const std::string &threadId, bool archived) {
	Network::HttpClient http = Api::authorized();
	return Api::toResult(http.patch(std::string(Api::BASE) + "/channels/" + threadId,
	                           archived ? "{\"archived\":true}" : "{\"archived\":false}"),
	                         "Threads");
}

} // namespace Threads
} // namespace Discord
