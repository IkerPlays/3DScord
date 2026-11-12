#ifndef MVD_TEST_SCREEN_H
#define MVD_TEST_SCREEN_H

#include "screen_manager.h"
#include <3ds.h>
#include <citro3d.h>
#include <string>
#include <vector>

namespace UI {

// EXPERIMENTAL, New 3DS only. Plays a video saved by the video experiment (recordings/video_in_*.264) with the New 3DS
// hardware video decoder, to find out whether it can show the video of a call. The result of every step goes to the log.
class MvdTestScreen : public Screen {
  public:
	static void open();

	MvdTestScreen();
	~MvdTestScreen() override;

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }
	bool isAnimating() const override { return running; }

  private:
	bool loadFile();
	bool parseSps(const unsigned char *nal, size_t len);
	bool startDecoder();
	void stopDecoder();
	void step();

	struct Nal {
		size_t offset; // of the 00 00 00 01 start code
		size_t size;   // including the start code
		int type;
	};

	std::string path;
	std::string status;
	unsigned char *data = nullptr; // linear memory: the decoder reads it directly
	size_t dataLen = 0;
	std::vector<Nal> nals;
	size_t next = 0;
	bool running = false;
	bool decoderOn = false;
	bool paramsDone = false;
	int width = 0;  // coded size, a multiple of 16
	int height = 0;
	u16 *output = nullptr; // the decoded picture
	C3D_Tex texture;
	bool textureReady = false;
	bool haveFrame = false;
	int framesShown = 0;
	double msTotal = 0.0;
	double msMax = 0.0;
	u32 lastStatus = 0;
};

} // namespace UI

#endif // MVD_TEST_SCREEN_H
