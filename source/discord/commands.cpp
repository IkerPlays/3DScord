#include "discord/commands.h"
#include <cstdlib>
#include <ctime>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace Discord {
namespace Commands {

namespace {
std::string str(const rapidjson::Value &v, const char *key) {
	return v.IsObject() && v.HasMember(key) && v[key].IsString() ? v[key].GetString() : "";
}
} // namespace

bool readOptions(const std::string &commandJson, std::vector<Option> &options, bool &supported) {
	rapidjson::Document doc;
	doc.Parse(commandJson.c_str());
	if (doc.HasParseError() || !doc.IsObject()) {
		return false;
	}
	supported = true;
	options.clear();
	if (doc.HasMember("options") && doc["options"].IsArray()) {
		for (const auto &o : doc["options"].GetArray()) {
			if (!o.IsObject() || !o.HasMember("type") || !o["type"].IsInt()) {
				continue;
			}
			const int type = o["type"].GetInt();
			const bool required = o.HasMember("required") && o["required"].IsBool() && o["required"].GetBool();
			const bool simple = type == 3 || type == 4 || type == 5 || type == 10;
			if (!simple) {
				// A subcommand or a user/channel/role/file picker. Optional ones can simply be left out.
				if (required || type == 1 || type == 2) {
					supported = false;
				}
				continue;
			}
			if (required) {
				Option opt;
				opt.name = str(o, "name");
				opt.description = str(o, "description");
				opt.type = type;
				opt.required = true;
				options.push_back(opt);
			}
		}
	}
	return true;
}

Result invoke(const std::string &channelId, const std::string &guildId, const std::string &sessionId,
              const std::string &commandJson, const std::vector<std::pair<std::string, std::string>> &values) {
	rapidjson::Document cmd;
	cmd.Parse(commandJson.c_str());
	if (cmd.HasParseError() || !cmd.IsObject()) {
		return Result{false, 0, "Bad command"};
	}
	std::vector<Option> options;
	bool supported = true;
	readOptions(commandJson, options, supported);

	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> w(s);
	w.StartObject();
	w.Key("type");
	w.Int(2); // application command
	// Discord wants a unique number-like string per interaction.
	const std::string nonce = std::to_string((long long)time(nullptr) * 1000LL + rand() % 1000) + "000000";
	w.Key("nonce");
	w.String(nonce.c_str());
	if (!guildId.empty()) {
		w.Key("guild_id");
		w.String(guildId.c_str());
	}
	w.Key("channel_id");
	w.String(channelId.c_str());
	w.Key("application_id");
	w.String(str(cmd, "application_id").c_str());
	w.Key("session_id");
	w.String(sessionId.c_str());
	w.Key("data");
	w.StartObject();
	w.Key("version");
	w.String(str(cmd, "version").c_str());
	w.Key("id");
	w.String(str(cmd, "id").c_str());
	w.Key("name");
	w.String(str(cmd, "name").c_str());
	w.Key("type");
	w.Int(1);
	w.Key("options");
	w.StartArray();
	for (const auto &opt : options) {
		for (const auto &v : values) {
			if (v.first != opt.name) {
				continue;
			}
			w.StartObject();
			w.Key("type");
			w.Int(opt.type);
			w.Key("name");
			w.String(opt.name.c_str());
			w.Key("value");
			if (opt.type == 4) {
				w.Int64(atoll(v.second.c_str()));
			} else if (opt.type == 10) {
				w.Double(atof(v.second.c_str()));
			} else if (opt.type == 5) {
				const char c = v.second.empty() ? 'n' : (char)tolower((unsigned char)v.second[0]);
				w.Bool(c == 'y' || c == 's' || c == 't' || c == '1');
			} else {
				w.String(v.second.c_str());
			}
			w.EndObject();
		}
	}
	w.EndArray();
	w.Key("application_command");
	cmd.Accept(w);
	w.Key("attachments");
	w.StartArray();
	w.EndArray();
	w.EndObject();
	w.EndObject();

	auto http = Api::authorized(20);
	return Api::toResult(http.post(std::string(Api::BASE) + "/interactions", s.GetString()), "Commands");
}

} // namespace Commands
} // namespace Discord
