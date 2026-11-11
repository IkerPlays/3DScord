#include "core/local_store.h"
#include "core/config.h"
#include "log.h"
#include "utils/file_utils.h"
#include <algorithm>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sys/stat.h>

namespace Core {

namespace {

constexpr const char *STORE_FILE = CONFIG_DIR_PATH "/local_store.json";
constexpr size_t MAX_SAVED = 200;
constexpr size_t MAX_DRAFTS = 50;
constexpr size_t MAX_REPLIES = 30;

std::string str(const rapidjson::Value &o, const char *key) {
	return o.HasMember(key) && o[key].IsString() ? o[key].GetString() : "";
}

} // namespace

LocalStore &LocalStore::getInstance() {
	static LocalStore instance;
	return instance;
}

void LocalStore::ensureLoaded() {
	if (loaded) {
		return;
	}
	loaded = true;

	std::vector<char> buffer = Utils::File::readFile(STORE_FILE);
	if (!buffer.empty()) {
		buffer.push_back('\0');
		rapidjson::Document doc;
		doc.Parse(buffer.data());
		if (!doc.HasParseError() && doc.IsObject()) {
			if (doc.HasMember("saved") && doc["saved"].IsArray()) {
				for (const auto &m : doc["saved"].GetArray()) {
					if (m.IsObject()) {
						saved.push_back({str(m, "id"), str(m, "channel_id"), str(m, "author"), str(m, "content"),
						                 str(m, "timestamp")});
					}
				}
			}
			if (doc.HasMember("drafts") && doc["drafts"].IsArray()) {
				for (const auto &d : doc["drafts"].GetArray()) {
					if (d.IsObject()) {
						draftList.push_back({str(d, "text"), str(d, "channel_id")});
					}
				}
			}
			if (doc.HasMember("quick_replies") && doc["quick_replies"].IsArray()) {
				for (const auto &r : doc["quick_replies"].GetArray()) {
					if (r.IsString()) {
						replies.push_back(r.GetString());
					}
				}
			}
			if (!doc.HasMember("quick_replies")) {
				replies = {"On my way!", "Be right back", "Thanks!", "Can't talk now"};
			}
			return;
		}
	}
	replies = {"On my way!", "Be right back", "Thanks!", "Can't talk now"};
}

void LocalStore::save() {
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> w(s);
	w.StartObject();
	w.Key("saved");
	w.StartArray();
	for (const auto &m : saved) {
		w.StartObject();
		w.Key("id");
		w.String(m.id.c_str());
		w.Key("channel_id");
		w.String(m.channelId.c_str());
		w.Key("author");
		w.String(m.author.c_str());
		w.Key("content");
		w.String(m.content.c_str());
		w.Key("timestamp");
		w.String(m.timestamp.c_str());
		w.EndObject();
	}
	w.EndArray();
	w.Key("drafts");
	w.StartArray();
	for (const auto &d : draftList) {
		w.StartObject();
		w.Key("text");
		w.String(d.text.c_str());
		w.Key("channel_id");
		w.String(d.channelId.c_str());
		w.EndObject();
	}
	w.EndArray();
	w.Key("quick_replies");
	w.StartArray();
	for (const auto &r : replies) {
		w.String(r.c_str());
	}
	w.EndArray();
	w.EndObject();

	mkdir("sdmc:/3ds", 0777);
	mkdir(CONFIG_DIR_PATH, 0777);
	if (!Utils::File::writeFile(STORE_FILE, std::string(s.GetString()))) {
		Logger::log("[Store] Could not write %s", STORE_FILE);
	}
}

const std::vector<SavedMessage> &LocalStore::savedMessages() {
	ensureLoaded();
	return saved;
}

bool LocalStore::isSaved(const std::string &messageId) {
	ensureLoaded();
	return std::any_of(saved.begin(), saved.end(), [&](const SavedMessage &m) { return m.id == messageId; });
}

void LocalStore::saveMessage(const SavedMessage &message) {
	ensureLoaded();
	if (isSaved(message.id)) {
		return;
	}
	saved.insert(saved.begin(), message);
	if (saved.size() > MAX_SAVED) {
		saved.resize(MAX_SAVED);
	}
	save();
}

void LocalStore::removeSaved(const std::string &messageId) {
	ensureLoaded();
	saved.erase(std::remove_if(saved.begin(), saved.end(), [&](const SavedMessage &m) { return m.id == messageId; }),
	            saved.end());
	save();
}

const std::vector<Draft> &LocalStore::drafts() {
	ensureLoaded();
	return draftList;
}

void LocalStore::addDraft(const Draft &draft) {
	ensureLoaded();
	if (draft.text.empty()) {
		return;
	}
	draftList.insert(draftList.begin(), draft);
	if (draftList.size() > MAX_DRAFTS) {
		draftList.resize(MAX_DRAFTS);
	}
	save();
}

void LocalStore::removeDraft(size_t index) {
	ensureLoaded();
	if (index < draftList.size()) {
		draftList.erase(draftList.begin() + index);
		save();
	}
}

const std::vector<std::string> &LocalStore::quickReplies() {
	ensureLoaded();
	return replies;
}

void LocalStore::addQuickReply(const std::string &text) {
	ensureLoaded();
	if (!text.empty() && replies.size() < MAX_REPLIES) {
		replies.push_back(text);
		save();
	}
}

void LocalStore::setQuickReply(size_t index, const std::string &text) {
	ensureLoaded();
	if (index >= replies.size()) {
		return;
	}
	if (text.empty()) {
		replies.erase(replies.begin() + index);
	} else {
		replies[index] = text;
	}
	save();
}

} // namespace Core
