#ifndef FILE_PREVIEW_SCREEN_H
#define FILE_PREVIEW_SCREEN_H

#include "screen_manager.h"
#include <functional>
#include <string>
#include <vector>

namespace UI {

// Shows what a file is before it is sent: pictures are drawn, text files show their first lines, other files show
// their name, type and size. A confirms, B goes back to choose another file.
class FilePreviewScreen : public Screen {
  public:
	// onConfirm runs when the user presses A; onBack when B is pressed (it normally reopens the file browser).
	static void open(const std::string &path, long long maxBytes, std::function<void()> onConfirm,
	                 std::function<void()> onBack);

	FilePreviewScreen();
	~FilePreviewScreen() override;

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }

  private:
	enum class Kind { IMAGE, TEXT, AUDIO, VIDEO, OTHER };

	static Kind classify(const std::string &name);
	void loadText();

	static std::string pendingPath;
	static long long pendingMax;
	static std::function<void()> pendingConfirm;
	static std::function<void()> pendingBack;

	std::string path;
	std::string name;
	long long size;
	long long maxBytes;
	Kind kind;
	bool tooBig;
	bool imageLoaded;
	bool imageFailed;
	std::vector<std::string> textLines;
	float scroll;
	std::function<void()> onConfirm;
	std::function<void()> onBack;
};

} // namespace UI

#endif // FILE_PREVIEW_SCREEN_H
