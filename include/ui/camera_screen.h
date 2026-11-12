#ifndef CAMERA_SCREEN_H
#define CAMERA_SCREEN_H

#include "screen_manager.h"
#include <string>

namespace UI {

// Camera mode: the top screen shows what the camera sees. A takes the photo and shows it before it is sent to the chat,
// Y switches between the outer and the inner camera, B leaves.
class CameraScreen : public Screen {
  public:
	static void open(const std::string &channelId);

	CameraScreen();
	~CameraScreen() override;

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }
	bool isAnimating() const override { return cameraOn; }

  private:
	bool startCamera();
	void stopCamera();
	void pumpFrame();

	static std::string pendingChannel;
	static bool pendingInner;

	std::string channelId;
	bool inner;
	bool cameraOn = false;
	bool receiving = false;
	bool haveFrame = false;
	bool textureReady = false;
	unsigned frameCount = 0;
	u32 maxBytes = 0;
	Handle event = 0;
	unsigned short *buffer = nullptr;
	C3D_Tex texture;
};

} // namespace UI

#endif // CAMERA_SCREEN_H
