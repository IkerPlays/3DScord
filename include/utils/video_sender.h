#ifndef VIDEO_SENDER_H
#define VIDEO_SENDER_H

#include "utils/h264_encoder.h"
#include "utils/worker_thread.h"
#include <3ds.h>
#include <atomic>
#include <citro2d.h>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace Utils {

// EXPERIMENTAL. What you send to a call as video: your inner camera (176x144, 10 frames a second) or the top screen of the console
// (reduced to 208x128, 8 frames a second). The picture is coded as H.264 (baseline, minih264e) on a worker thread, and the coded frames wait for
// the call's media thread to encrypt and send them. A small copy is kept to show you what you send.
class VideoSender {
  public:
	enum class Source { OFF, CAMERA, SCREEN };

	static VideoSender &getInstance();

	// Opens the source and starts the worker. False when it cannot start (for example when the Camera screen has the camera).
	bool start(Source source);
	void stop();
	bool running() const { return active.load(); }
	Source source() const { return current; }
	int width() const { return current == Source::SCREEN ? 208 : 176; }
	int height() const { return current == Source::SCREEN ? 128 : 144; }
	int fps() const { return current == Source::SCREEN ? 8 : 10; }

	// Media thread: the oldest coded frame (Annex B, with SPS and PPS when it is a key frame).
	bool takeFrame(std::vector<uint8_t> &out, bool &key);
	// What the encoder is doing, for the screen: frames coded in the last second, the average time of a frame in milliseconds, key frames
	// asked for by the others and times the call could not keep up.
	int statFps() const { return statFpsNow.load(); }
	int statMs() const { return statMsNow.load(); }
	int statKeyAsks() const { return keyAsks.load(); }
	int statDrops() const { return drops.load(); }
	// Someone asked for a new key frame (RTCP picture loss).
	void requestKeyFrame() {
		keyRequested = true;
		keyAsks++;
	}

	// Screen thread: copies the newest preview picture to a texture. Call it once per frame before drawing.
	void updatePreview();
	bool hasPreview() const { return previewReady && gotPreview; }
	// Draws what is being sent inside the box, keeping its proportions.
	void drawPreview(float x, float y, float z, float w, float h) const;
	// Frees the preview texture (call it from the screen thread when nothing is sent).
	void releasePreview();

  private:
	VideoSender() = default;
	void run();

	WorkerThread worker;
	std::atomic<bool> active{false};
	std::atomic<bool> quit{false};
	std::atomic<bool> keyRequested{false};
	std::atomic<int> statFpsNow{0};
	std::atomic<int> statMsNow{0};
	std::atomic<int> keyAsks{0};
	std::atomic<int> drops{0};
	Source current = Source::OFF;
	std::mutex mutex;
	struct Frame {
		std::vector<uint8_t> data;
		bool key = false;
	};
	std::deque<Frame> queue;

	static constexpr int PREVIEW_W = 256; // the preview texture is small: the tile it goes in is small
	static constexpr int PREVIEW_H = 128;
	// The preview: the worker writes a picture in the order of the texture, the screen thread copies it.
	std::vector<uint16_t> previewBuffer;
	uint32_t previewVersion = 0;
	int previewW = 0;
	int previewH = 0;
	C3D_Tex previewTex;
	bool previewReady = false;
	bool gotPreview = false;
	uint32_t shownVersion = 0;
	int shownW = 0;
	int shownH = 0;
};

} // namespace Utils

#endif // VIDEO_SENDER_H
