#ifndef API_UTIL_H
#define API_UTIL_H

#include "discord/account_settings.h"
#include "network/http_client.h"
#include <string>

namespace Discord {
namespace Api {

using Result = AccountSettings::Result;

constexpr const char *BASE = "https://discord.com/api/v10";

// Turns a response into a Result; on failure the message is Discord's own error text.
Result toResult(const Network::HttpResponse &resp, const char *tag);
// A client that already carries the token of the current account.
Network::HttpClient authorized(long timeoutSeconds = 20);

} // namespace Api
} // namespace Discord

#endif // API_UTIL_H
