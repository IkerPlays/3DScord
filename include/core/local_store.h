#ifndef LOCAL_STORE_H
#define LOCAL_STORE_H

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
	bool isDmPinned(const std::string &channelId);
	bool toggleDmPinned(const std::string &channelId); // returns the new state

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
};

} // namespace Core

#endif // LOCAL_STORE_H
