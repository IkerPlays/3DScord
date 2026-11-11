#ifndef LIST_SCREEN_H
#define LIST_SCREEN_H

#include "core/config.h"
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
	enum class Mode { TOOLS, PINS, SEARCH, MENTIONS, SAVED, DRAFTS, QUICK_REPLIES, FORMAT, GIFS, STICKERS, FIND, MEMBERS, THREADS, HUB, FRIENDS, GROUP_PICK, SOUNDS, VOICE_CHANNELS, VOICE_PEOPLE, GALLERY, THEME_EDIT, SESSIONS, EVENTS, AUDIT, PERMISSIONS, MOD, DIAG, SERVER, COMMANDS };

	static void open(Mode mode);
	// Switches to another list without adding the current one to the back history.
	static void openReplacing(Mode mode);
	// Notification sound of a server or channel (the id of the server or channel and a name to show).
	static void openSounds(const std::string &targetId, const std::string &targetName);
	// Kick, ban or time out one member of a server.
	static void openModeration(const std::string &userId, const std::string &guildId, const std::string &name);

	ListScreen();
	~ListScreen() override;

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return false; }
	// The people in a call change all the time (speaking, volume), so that list keeps redrawing.
	bool isAnimating() const override { return mode == Mode::VOICE_PEOPLE || mode == Mode::GIFS; }

  private:
	struct Entry {
		std::string id;
		std::string channelId;
		std::string title;
		std::string body;
		int kind = 0; // relationship type for the friends list
		std::string animated; // animated version of the picture, loaded once the selection rests (GIFs)
		std::string data; // anything else a mode needs to keep with the row (a command's JSON)
		std::string preview; // picture shown on the top screen while the row is selected (GIFs)
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
	std::string currentGuild() const;
	void writeDiagnosticsReport();
	// Volume of one person in the call (the microphone volume for yourself); delta 0 resets it to 100%.
	void changePersonVolume(const std::string &userId, int delta);
	std::string channelLabel(const std::string &channelId) const;
	std::string headerTitle() const;
	std::string hintLine() const;
	void clampSelection();

	static Mode pendingMode;
	static std::string pendingSoundTarget;
	static std::string pendingSoundName;
	static std::string pendingModUser;
	static std::string pendingModGuild;

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
	Theme editTheme; // the theme being made in the theme editor
	std::string soundTarget;
	std::string soundTargetName;
	std::string modUser;
	std::string modGuild;
	long diagLatencyMs = -1;
	bool includeArchived = false;
	int refreshTick = 0;
	int stableFrames = 0; // frames the same row has been selected: the GIF is only fetched once you stop on it
	int lastSelected = -1;
	int friendFilter = 0; // 0 friends, 1 pending, 2 blocked
	std::set<std::string> groupIds;
	std::string confirmId; // entry waiting for a second press to confirm removing it
};

} // namespace UI

#endif // LIST_SCREEN_H
