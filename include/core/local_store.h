#ifndef LOCAL_STORE_H
#define LOCAL_STORE_H

#include <map>
#include <string>
#include <vector>

namespace Core {

struct SavedMessage {
	std::string id;
	std::string channelId;
	std::string author;
	std::string content;
	std::string timestamp;
};

struct Draft {
	std::string text;
	std::string channelId;
};

// Small lists kept on the SD card (sdmc:/3ds/3DScord): saved messages, drafts and quick replies.
class LocalStore {
  public:
	static LocalStore &getInstance();

	const std::vector<SavedMessage> &savedMessages();
	bool isSaved(const std::string &messageId);
	void saveMessage(const SavedMessage &message);
	void removeSaved(const std::string &messageId);

	const std::vector<Draft> &drafts();
	void addDraft(const Draft &draft);
	void removeDraft(size_t index);

	// Direct-message channels kept at the top of the DM list.
	bool isGuildPinned(const std::string &guildId);
	bool toggleGuildPinned(const std::string &guildId); // returns the new state
	bool isDmPinned(const std::string &channelId);
	bool toggleDmPinned(const std::string &channelId); // returns the new state
	// Favourite channels (any channel or direct message), listed in Quick Access.
	bool isFavoriteChannel(const std::string &channelId);
	bool toggleFavoriteChannel(const std::string &channelId); // returns the new state
	const std::vector<std::string> &favoriteChannels();
	// The emoji used last (their hex codes, newest first): they come first in the picker.
	// The unsent text of each channel, kept between runs of the app.
	const std::map<std::string, std::string> &persistedDrafts();
	void setPersistedDrafts(const std::map<std::string, std::string> &drafts);
	// People whose messages are hidden here (a local mute: nothing is changed on Discord).
	bool isIgnored(const std::string &userId);
	bool toggleIgnored(const std::string &userId, const std::string &name = ""); // returns the new state
	std::vector<std::pair<std::string, std::string>> ignoredPeople(); // id and name
	const std::vector<std::string> &recentEmojis();
	void pushRecentEmoji(const std::string &hex);

	// Notification sound of a server or channel: -1 = the default, 0 = silent, otherwise Utils::Sound + 1.
	int getSoundOverride(const std::string &id);
	void setSoundOverride(const std::string &id, int value); // -1 removes the override

	const std::vector<std::string> &quickReplies();
	void addQuickReply(const std::string &text);
	void setQuickReply(size_t index, const std::string &text); // empty text deletes it

  private:
	LocalStore() = default;
	void ensureLoaded();
	void save();

	bool loaded = false;
	std::vector<SavedMessage> saved;
	std::vector<Draft> draftList;
	std::vector<std::string> replies;
	std::vector<std::string> pinnedDms;
	std::vector<std::string> pinnedGuilds;
	std::vector<std::string> favorites;
	std::vector<std::string> recents;
	std::vector<std::string> ignored;
	std::map<std::string, std::string> ignoredNames;
	std::map<std::string, std::string> channelDraftsSaved;
	std::map<std::string, int> soundOverrides;
};

} // namespace Core

#endif // LOCAL_STORE_H
