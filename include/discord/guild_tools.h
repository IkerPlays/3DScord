#ifndef GUILD_TOOLS_H
#define GUILD_TOOLS_H

#include "discord/api_util.h"
#include <string>

namespace Discord {
namespace GuildTools {

using Result = Api::Result;

// Joins a server from an invite link or code (Discord may refuse and ask for a captcha, which the 3DS cannot show).
Result joinInvite(const std::string &linkOrCode);
Result leaveGuild(const std::string &guildId);

// Moderation. timeoutSeconds 0 removes a timeout.
Result kick(const std::string &guildId, const std::string &userId);
Result ban(const std::string &guildId, const std::string &userId);
Result timeout(const std::string &guildId, const std::string &userId, long timeoutSeconds);

// Your own notification settings for a server. Pass only the fields to change: -1 leaves an integer alone.
struct NotifyChange {
	int muted = -1;            // 0/1
	int messageNotifications = -1; // 0 all, 1 only mentions, 2 nothing
	int suppressEveryone = -1; // 0/1
	int suppressRoles = -1;    // 0/1
	int mobilePush = -1;       // 0/1
};
Result setNotifications(const std::string &guildId, const NotifyChange &change);

// Server profile (needs Manage Server). Only the field named by `field` ("name" or "description") is sent.
Result editGuildText(const std::string &guildId, const std::string &field, const std::string &value);

// Posts of an announcement channel are copied into another channel by a webhook (needs Manage Webhooks there).
Result followAnnouncements(const std::string &announcementChannelId, const std::string &targetChannelId);

// Voice moderation (needs Mute Members / Move Members). Muting is for everyone in the server.
Result setVoiceMute(const std::string &guildId, const std::string &userId, bool muted);
Result disconnectFromVoice(const std::string &guildId, const std::string &userId);

// Your "About Me" inside one server.
Result setServerBio(const std::string &guildId, const std::string &bio);

// Tell Discord whether you are interested in a scheduled event.
Result setEventInterest(const std::string &guildId, const std::string &eventId, bool interested);

} // namespace GuildTools
} // namespace Discord

#endif // GUILD_TOOLS_H
