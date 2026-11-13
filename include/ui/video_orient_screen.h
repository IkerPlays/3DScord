#ifndef VIDEO_ORIENT_SCREEN_H
#define VIDEO_ORIENT_SCREEN_H

#include "screen_manager.h"
#include <3ds.h>
#include <citro3d.h>
#include <cstdint>
#include <vector>

namespace UI {

// A test picture that is not symmetric (an F, a red edge on top, blue at the bottom, green on the left, yellow on the right and an
// orange square in the top left corner) goes through the same path as the pictures of calls. A cycles through the eight ways of
// turning and mirroring it, so the one that shows it right (red on top, blue at the bottom, the F readable) can be found.
class VideoOrientScreen : public Screen {
  public:
	static void open();

	VideoOrientScreen() = default;
	~VideoOrientScreen() override;

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }

  private:
	C3D_Tex texture;
	bool textureReady = false;
	std::vector<uint16_t> tiled;
	int option = 0; // 0-7: quarter turns (option & 3) and a mirror (option >> 2)
};

} // namespace UI

#endif // VIDEO_ORIENT_SCREEN_H
