#include "discord/social.h"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace Discord {
namespace Social {

namespace {

using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

template <typename F> std::string json(F fill) {
	rapidjson::StringBuffer s;
	Writer w(s);
	w.StartObject();
	fill(w);
	w.EndObject();
	return s.GetString();
}

DmResult channelFrom(const Network::HttpResponse &resp) {
	DmResult out;
	out.result = Api::toResult(resp, "Social");
	if (resp.success) {
		rapidjson::Document doc;
		doc.Parse(resp.body.c_str());
		if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("id") && doc["id"].IsString()) {
			out.channelId = doc["id"].GetString();
		}
	}
	return out;
}

std::string base() { return Api::BASE; }

} // namespace

DmResult openDm(const std::string &userId) {
	auto http = Api::authorized();
	return channelFrom(http.post(base() + "/users/@me/channels", json([&](Writer &w) {
		                             w.Key("recipient_id");
		                             w.String(userId.c_str());
	                             })));
}

DmResult createGroupDm(const std::vector<std::string> &userIds) {
	auto http = Api::authorized();
	return channelFrom(http.post(base() + "/users/@me/channels", json([&](Writer &w) {
		                             w.Key("recipients");
		                             w.StartArray();
		                             for (const auto &id : userIds) {
			                             w.String(id.c_str());
		                             }
		                             w.EndArray();
	                             })));
}

Result sendFriendRequest(const std::string &username) {
	auto http = Api::authorized();
	return Api::toResult(http.post(base() + "/users/@me/relationships", json([&](Writer &w) {
		                               w.Key("username");
		                               w.String(username.c_str());
		                               w.Key("discriminator");
		                               w.Null();
	                               })),
	                     "Social");
}

Result sendFriendRequestToId(const std::string &userId) {
	auto http = Api::authorized();
	return Api::toResult(http.put(base() + "/users/@me/relationships/" + userId, "{}"), "Social");
}

Result acceptRequest(const std::string &userId) { return sendFriendRequestToId(userId); }

Result removeRelationship(const std::string &userId) {
	auto http = Api::authorized();
	return Api::toResult(http.del(base() + "/users/@me/relationships/" + userId), "Social");
}

Result block(const std::string &userId) {
	auto http = Api::authorized();
	return Api::toResult(http.put(base() + "/users/@me/relationships/" + userId, "{\"type\":2}"), "Social");
}

std::string getNote(const std::string &userId) {
	auto http = Api::authorized(10);
	Network::HttpResponse resp = http.get(base() + "/users/@me/notes/" + userId);
	if (!resp.success) {
		return "";
	}
	rapidjson::Document doc;
	doc.Parse(resp.body.c_str());
	if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("note") && doc["note"].IsString()) {
		return doc["note"].GetString();
	}
	return "";
}

Result setNote(const std::string &userId, const std::string &note) {
	auto http = Api::authorized();
	return Api::toResult(http.put(base() + "/users/@me/notes/" + userId, json([&](Writer &w) {
		                              w.Key("note");
		                              w.String(note.c_str());
	                              })),
	                     "Social");
}

Result setMyNickname(const std::string &guildId, const std::string &nickname) {
	auto http = Api::authorized();
	return Api::toResult(http.patch(base() + "/guilds/" + guildId + "/members/@me", json([&](Writer &w) {
		                                w.Key("nick");
		                                if (nickname.empty()) {
			                                w.Null();
		                                } else {
			                                w.String(nickname.c_str());
		                                }
	                                })),
	                     "Social");
}

} // namespace Social
} // namespace Discord
