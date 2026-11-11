#ifndef LIST_SCREEN_H
#define LIST_SCREEN_H

#include "screen_manager.h"
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace UI {

// One screen for the lists of the channel tools: pinned messages, search results, mentions, saved messages, drafts,
// quick replies and the formatting helpers. The mode is chosen with open() before the screen is created.
class ListScreen : public Screen {
  public:
	enum class Mode { TOOLS, PINS, SEARCH, MENTIONS, SAVED, DRAFTS, QUICK_REPLIES, FORMAT, GIFS, STICKERS, FIND, MEMBERS, THREADS, HUB, FRIENDS, GROUP_PICK };

	static void open(Mode mode);
	// Switches to another list without adding the current one to the back history.
	static void openReplacing(Mode mode);

	ListScreen();
	~ListScreen() override;

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return false; }

  private:
	struct Entry {
		std::string id;
		std::string channelId;
		std::string title;
		std::string body;
		int kind = 0; // relationship type for the friends list
	};

	// Filled by network callbacks, which may outlive the screen.
	struct Shared {
		std::mutex mutex;
		std::vector<Entry> entries;
		std::string error;
		bool loading = true;
		int version = 0;
	};

	void load();
	void loadLocal();
	void fetch(const std::string &path);
	void startSearch();
	void startGifSearch();
	void startFind();
	void startMembers();
	void primaryAction(const Entry &entry);
	void secondaryAction(const Entry &entry);
	void tertiaryAction();
	void quaternaryAction();
	std::string channelLabel(const std::string &channelId) const;
	std::string headerTitle() const;
	std::string hintLine() const;
	void clampSelection();

	static Mode pendingMode;

	Mode mode;
	std::string channelId;
	std::shared_ptr<Shared> shared;
	std::vector<Entry> entries;
	std::string error;
	bool loading;
	int seenVersion;
	int selected;
	float scroll;
	std::string query;
	bool includeArchived = false;
	int friendFilter = 0; // 0 friends, 1 pending, 2 blocked
	std::set<std::string> groupIds;
	std::string confirmId; // entry waiting for a second press to confirm removing it
};

} // namespace UI

#endif // LIST_SCREEN_H
