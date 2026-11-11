#ifndef IMAGE_VIEWER_SCREEN_H
#define IMAGE_VIEWER_SCREEN_H

#include "screen_manager.h"
#include <string>

namespace UI {

// Full-screen image on the top screen. D-Pad / Circle Pad pan, L and R zoom, A fits the image, B goes back.
class ImageViewerScreen : public Screen {
  public:
	static void open(const std::string &url, const std::string &filename);

	ImageViewerScreen();

	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }
	bool isAnimating() const override { return true; }

  private:
	static std::string pendingUrl;
	static std::string pendingName;

	std::string url;
	std::string filename;
	float zoom;
	float panX;
	float panY;
	bool dragging;
	int lastTouchX;
	int lastTouchY;
};

} // namespace UI

#endif // IMAGE_VIEWER_SCREEN_H
