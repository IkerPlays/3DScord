#ifndef SOFT_TEST_SCREEN_H
#define SOFT_TEST_SCREEN_H

#include "screen_manager.h"
#include "utils/soft_decoder.h"
#include <3ds.h>
#include <citro3d.h>
#include <string>
#include <vector>

namespace UI {

// EXPERIMENTAL. Decodes the newest video saved by the video experiment (recordings/video_in_*.264) with a software decoder
// (openh264) and shows the pictures at half size, with the time each one takes. A starts, B leaves. The results go to the log
// and to softtest.txt on the SD card.
class SoftTestScreen : public Screen {
  public:
	static void open();

	SoftTestScreen();
	~SoftTestScreen() override;

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }
	bool isAnimating() const override { return running; }

  private:
	bool loadFile();
	void step();
	void show(const Utils::SoftDecoder::Picture &pic);

	struct Nal {
		size_t offset;
		size_t size;
		int type;
	};

	std::string path;
	std::string status;
	std::vector<unsigned char> data;
	std::vector<Nal> nals;
	size_t next = 0;
	bool running = false;
	Utils::SoftDecoder decoder;
	u16 *picture = nullptr; // the picture at half size, RGB565, in linear memory
	int picW = 0;
	int picH = 0;
	C3D_Tex texture;
	bool textureReady = false;
	bool haveFrame = false;
	int frames = 0;
	int lastState = 0;
	double msTotal = 0.0;
	double msMax = 0.0;
	double msFirst = 0.0;
};

} // namespace UI

#endif // SOFT_TEST_SCREEN_H
