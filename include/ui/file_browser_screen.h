#ifndef FILE_BROWSER_SCREEN_H
#define FILE_BROWSER_SCREEN_H

#include "screen_manager.h"
#include <functional>
#include <string>
#include <vector>

namespace UI {

// Browses the SD card so a file can be picked for an upload (chat attachments, avatar, banner).
// A opens a folder or picks a file, B goes up a folder (and leaves from the top), Y goes to the root.
class FileBrowserScreen : public Screen {
  public:
	enum class Filter { ANY, IMAGES };
	using PickCallback = std::function<void(const std::string &path)>;

	// replaceCurrent skips the back history so that leaving returns to the screen before the current one.
	// maxBytes is the biggest file the destination accepts; the preview refuses anything bigger.
	static void open(const std::string &title, Filter filter, PickCallback onPick, bool replaceCurrent = false,
	                 long long maxBytes = 4LL * 1024 * 1024);

	FileBrowserScreen();

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }

  private:
	struct Entry {
		std::string name;
		bool isDir;
		long long size;
	};

	void readDirectory();
	void activate(const Entry &entry);
	void previewFile(const std::string &path);
	void goUp();
	std::string joined(const std::string &name) const;
	void clampSelection();

	static std::string pendingTitle;
	static Filter pendingFilter;
	static PickCallback pendingCallback;
	static std::string lastDirectory;
	static long long pendingMax;

	std::string title;
	Filter filter;
	PickCallback onPick;
	long long maxBytes;
	std::string directory;
	std::vector<Entry> entries;
	int selected;
	float scroll;
	int repeatTimer;
};

} // namespace UI

#endif // FILE_BROWSER_SCREEN_H
