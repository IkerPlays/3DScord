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
