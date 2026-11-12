#ifndef CAMERA_SCREEN_H
#define CAMERA_SCREEN_H

#include "screen_manager.h"
#include <functional>
#include <string>

namespace UI {

// Camera mode: the top screen shows what the camera sees. A takes the photo and shows it before it is sent to the chat,
// Y switches between the outer and the inner camera, B leaves.
class CameraScreen : public Screen {
  public:
	// Opens the camera to send a photo to a channel.
	static void open(const std::string &channelId);
	// Opens the camera for something else that wants a photo (the file browser): onPhoto gets the saved file.
	static void openForPick(std::function<void(const std::string &)> onPhoto, long long maxBytes);

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
	void takePhoto();

	static std::string pendingChannel;
	static bool pendingInner;
	static std::function<void(const std::string &)> pendingPick;
	static long long pendingMax;
	static bool leaveOnReturn; // set once the photo has gone: coming back to the camera closes it
	static int timerSeconds;   // self-timer, kept between visits: 0 off

	std::string channelId;
	std::function<void(const std::string &)> onPhoto;
	long long maxBytes_ = 0;
	bool leaving = false;
	u64 countdownEnd = 0; // when the self-timer ends, 0 when it is not running
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
