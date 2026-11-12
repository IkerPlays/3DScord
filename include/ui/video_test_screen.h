#ifndef VIDEO_TEST_SCREEN_H
#define VIDEO_TEST_SCREEN_H

#include "screen_manager.h"
#include "utils/h264_encoder.h"
#include <cstdio>
#include <string>
#include <vector>

namespace UI {

// EXPERIMENTAL. Records ten seconds from the camera as H.264 video, to see how fast the 3DS can encode it. The file is saved
// on the SD card (recordings/video_test_*.264) and plays on a computer with VLC or ffplay. This is the base for video calls.
class VideoTestScreen : public Screen {
  public:
	static void open();

	VideoTestScreen();
	~VideoTestScreen() override;

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }
	bool isAnimating() const override { return state == State::RECORDING; }

  private:
	enum class State { READY, RECORDING, DONE, FAILED };

	bool startCamera();
	void stopCamera();
	void start();
	void pumpFrame();
	void finish();

	State state = State::READY;
	bool cameraOn = false;
	bool receiving = false;
	u32 maxBytes = 0;
	Handle event = 0;
	unsigned char *packed = nullptr; // what the camera delivers: YUYV
	std::vector<unsigned char> yPlane, uPlane, vPlane;
	Utils::H264Encoder encoder;
	FILE *file = nullptr;
	std::string path;

	// Results
	int frames = 0;
	long long bytes = 0;
	double encodeMsTotal = 0.0;
	double encodeMsMax = 0.0;
	u64 startedAt = 0;
	double seconds = 0.0;
	bool useExtra = false;
};

} // namespace UI

#endif // VIDEO_TEST_SCREEN_H
