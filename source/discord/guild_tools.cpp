#include "discord/guild_tools.h"
#include "utils/message_utils.h"
#include <ctime>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace Discord {
namespace GuildTools {

namespace {
std::string base() { return Api::BASE; }
} // namespace

Result setNotifications(const std::string &guildId, const NotifyChange &c) {
	auto http = Api::authorized();
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> w(s);
	w.StartObject();
	auto flag = [&](const char *key, int v) {
		if (v >= 0) {
			w.Key(key);
			w.Bool(v != 0);
		}
	};
	flag("muted", c.muted);
	if (c.messageNotifications >= 0) {
		w.Key("message_notifications");
		w.Int(c.messageNotifications);
	}
	flag("suppress_everyone", c.suppressEveryone);
	flag("suppress_roles", c.suppressRoles);
	flag("mobile_push", c.mobilePush);
	w.EndObject();
	return Api::toResult(http.patch(base() + "/users/@me/guilds/" + guildId + "/settings", s.GetString()), "Guild");
}

Result editGuildText(const std::string &guildId, const std::string &field, const std::string &value) {
	auto http = Api::authorized();
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> w(s);
	w.StartObject();
	w.Key(field.c_str());
	w.String(value.c_str());
	w.EndObject();
	return Api::toResult(http.patch(base() + "/guilds/" + guildId, s.GetString()), "Guild");
}

Result followAnnouncements(const std::string &announcementChannelId, const std::string &targetChannelId) {
	auto http = Api::authorized();
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> w(s);
	w.StartObject();
	w.Key("webhook_channel_id");
	w.String(targetChannelId.c_str());
	w.EndObject();
	return Api::toResult(http.post(base() + "/channels/" + announcementChannelId + "/followers", s.GetString()),
	                     "Guild");
}

Result setVoiceMute(const std::string &guildId, const std::string &userId, bool muted) {
	auto http = Api::authorized();
	return Api::toResult(http.patch(base() + "/guilds/" + guildId + "/members/" + userId,
	                                muted ? "{\"mute\":true}" : "{\"mute\":false}"),
	                     "Guild");
}

Result disconnectFromVoice(const std::string &guildId, const std::string &userId) {
	auto http = Api::authorized();
	return Api::toResult(http.patch(base() + "/guilds/" + guildId + "/members/" + userId, "{\"channel_id\":null}"),
	                     "Guild");
}

Result setServerBio(const std::string &guildId, const std::string &bio) {
	auto http = Api::authorized();
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> w(s);
	w.StartObject();
	w.Key("bio");
	w.String(bio.c_str());
	w.EndObject();
	return Api::toResult(http.patch(base() + "/guilds/" + guildId + "/profile", s.GetString()), "Guild");
}

Result joinInvite(const std::string &linkOrCode) {
	std::string code = linkOrCode;
	const size_t slash = code.rfind('/');
	if (slash != std::string::npos) {
		code = code.substr(slash + 1);
	}
	const size_t query = code.find('?');
	if (query != std::string::npos) {
		code.resize(query);
	}
	if (code.empty()) {
		return Result{false, 0, "Empty invite"};
	}
	auto http = Api::authorized(30);
	return Api::toResult(http.post(base() + "/invites/" + code, "{}"), "Guild");
}

Result leaveGuild(const std::string &guildId) {
	auto http = Api::authorized();
	return Api::toResult(http.del(base() + "/users/@me/guilds/" + guildId), "Guild");
}

Result kick(const std::string &guildId, const std::string &userId) {
	auto http = Api::authorized();
	return Api::toResult(http.del(base() + "/guilds/" + guildId + "/members/" + userId), "Guild");
}

Result ban(const std::string &guildId, const std::string &userId) {
	auto http = Api::authorized();
	return Api::toResult(http.put(base() + "/guilds/" + guildId + "/bans/" + userId, "{\"delete_message_seconds\":0}"),
	                     "Guild");
}

Result timeout(const std::string &guildId, const std::string &userId, long timeoutSeconds) {
	auto http = Api::authorized();
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> w(s);
	w.StartObject();
	w.Key("communication_disabled_until");
	if (timeoutSeconds <= 0) {
		w.Null();
	} else {
		const time_t until = UI::MessageUtils::getUtcNow() + (time_t)timeoutSeconds;
		w.String((UI::MessageUtils::getISOTimestamp(until) + "Z").c_str());
	}
	w.EndObject();
	return Api::toResult(http.patch(base() + "/guilds/" + guildId + "/members/" + userId, s.GetString()), "Guild");
}

Result setEventInterest(const std::string &guildId, const std::string &eventId, bool interested) {
	auto http = Api::authorized();
	const std::string url = base() + "/guilds/" + guildId + "/scheduled-events/" + eventId + "/users/@me";
	return Api::toResult(interested ? http.put(url, "{}") : http.del(url), "Guild");
}

} // namespace GuildTools
} // namespace Discord
