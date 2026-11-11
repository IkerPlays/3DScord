#ifndef THREADS_H
#define THREADS_H

#include "discord/account_settings.h"
#include <string>

namespace Discord {
namespace Threads {

using Result = AccountSettings::Result;

struct Created {
	Result result;
	std::string threadId;
};

// Start a thread on a message.
Created createFromMessage(const std::string &channelId, const std::string &messageId, const std::string &name);
// Join a thread so its messages show up for you.
Result join(const std::string &threadId);
// Archive (or reopen) the thread with this id.
Result setArchived(const std::string &threadId, bool archived);

} // namespace Threads
} // namespace Discord

#endif // THREADS_H
