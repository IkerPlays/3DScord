#ifndef VIDEO_PLAYER_SCREEN_H
#define VIDEO_PLAYER_SCREEN_H

#include "screen_manager.h"
#include "utils/mp4_reader.h"
#include "utils/worker_thread.h"
#include <3ds.h>
#include <atomic>
#include <citro3d.h>
#include <cstdint>
#include <string>
#include <vector>

namespace UI {

// EXPERIMENTAL. Plays an MP4 video (H.264) without sound, decoded in software (openh264) on the extra core of a New 3DS.
// Frames that cannot be decoded in time are skipped. A pauses (or plays again at the end), Left/Right jump 5 seconds,
// B leaves. The file is deleted when the screen closes.
class VideoPlayerScreen : public Screen {
  public:
	static void open(const std::string &path);

	VideoPlayerScreen();
	~VideoPlayerScreen() override;

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }
	bool isAnimating() const override { return playing.load(); }

  private:
	void playLoop();
	void convert(const uint8_t *y, const uint8_t *u, const uint8_t *v, int width, int height, int strideY, int strideUV);

	std::string path;
	std::string status;
	std::vector<uint8_t> file;
	Utils::Mp4Video video;
	Utils::WorkerThread worker;
	std::atomic<bool> quit{false};
	std::atomic<bool> paused{false};
	std::atomic<bool> playing{false};
	std::atomic<bool> ended{false};
	std::atomic<int> seekMs{0}; // requested jump, in milliseconds (negative goes back)
	std::atomic<uint32_t> positionMs{0};
	std::atomic<uint32_t> shown{0};
	std::atomic<uint32_t> skipped{0};
	std::atomic<bool> restart{false};
	std::atomic<bool> workerFailed{false};

	LightLock lock;
	u16 *front = nullptr; // the worker writes here
	u16 *ready = nullptr; // finished, waits for the screen thread
	int readyW = 0;
	int readyH = 0;
	uint32_t version = 0;

	C3D_Tex texture;
	bool textureReady = false;
	bool havePicture = false;
	int picW = 0;
	int picH = 0;
	uint32_t shownVersion = 0;
};

} // namespace UI

#endif // VIDEO_PLAYER_SCREEN_H
