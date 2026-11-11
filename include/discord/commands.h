#ifndef DISCORD_COMMANDS_H
#define DISCORD_COMMANDS_H

#include "discord/api_util.h"
#include <string>
#include <utility>
#include <vector>

namespace Discord {
namespace Commands {

using Result = Api::Result;

// One option of a command that the user has to fill in.
struct Option {
	std::string name;
	std::string description;
	int type = 3; // 3 text, 4 integer, 5 yes/no, 10 number
	bool required = false;
};

// The options a command asks for, or false when it uses something the 3DS cannot enter (subcommands, users,
// channels, roles or files). Only the options in `needed` have to be asked.
bool readOptions(const std::string &commandJson, std::vector<Option> &options, bool &supported);

// Runs a slash command in a channel. `values` pairs option names with what the user typed.
Result invoke(const std::string &channelId, const std::string &guildId, const std::string &sessionId,
              const std::string &commandJson, const std::vector<std::pair<std::string, std::string>> &values);

} // namespace Commands
} // namespace Discord

#endif // DISCORD_COMMANDS_H
