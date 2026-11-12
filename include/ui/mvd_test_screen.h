#ifndef MVD_TEST_SCREEN_H
#define MVD_TEST_SCREEN_H

#include "screen_manager.h"
#include <3ds.h>
#include <citro3d.h>
#include <3ds/services/mvd.h>
#include "utils/worker_thread.h"
#include <atomic>
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
	bool startDecoder(u32 workBytes);
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
	u32 initError = 0;
	u32 workBytes_ = 0; // size of the decoder's work buffer for this try
	MVDSTD_Config config;               // given to the decoder with every picture it renders
	Utils::WorkerThread starter;        // starts the decoder away from the screen: the call may never answer
	std::atomic<int> startState{0};     // 0 not started, 1 starting, 2 started, 3 failed
	u64 startRequestedAt = 0;
	bool startTimedOut = false;
};

} // namespace UI

#endif // MVD_TEST_SCREEN_H
