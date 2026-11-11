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

// Tell Discord whether you are interested in a scheduled event.
Result setEventInterest(const std::string &guildId, const std::string &eventId, bool interested);

} // namespace GuildTools
} // namespace Discord

#endif // GUILD_TOOLS_H
