#ifndef SOCIAL_H
#define SOCIAL_H

#include "discord/api_util.h"
#include <string>
#include <vector>

namespace Discord {
namespace Social {

using Result = Api::Result;

struct DmResult {
	Result result;
	std::string channelId;
};

// Direct message with one person, created if it does not exist yet.
DmResult openDm(const std::string &userId);
// Group DM with two or more people.
DmResult createGroupDm(const std::vector<std::string> &userIds);

// Friends: sending a request to someone who already asked you accepts it.
Result sendFriendRequest(const std::string &username);
Result sendFriendRequestToId(const std::string &userId);
Result acceptRequest(const std::string &userId);
// Removes a friend, declines or cancels a request, or unblocks, depending on the current relationship.
Result removeRelationship(const std::string &userId);
Result block(const std::string &userId);

// The private note you keep about a user.
std::string getNote(const std::string &userId);
Result setNote(const std::string &userId, const std::string &note);

// Logs another device out of your account (by the hash Discord gives each session).
Result logoutSession(const std::string &sessionHash);

// Your own nickname in one server (empty clears it).
Result setMyNickname(const std::string &guildId, const std::string &nickname);

} // namespace Social
} // namespace Discord

#endif // SOCIAL_H
