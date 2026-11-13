#ifndef VIDEO_SENDER_H
#define VIDEO_SENDER_H

#include "utils/h264_encoder.h"
#include "utils/worker_thread.h"
#include <3ds.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace Utils {

// EXPERIMENTAL. Your own camera in a call: the inner camera is read at 320x240, 15 frames a second, coded as H.264 (baseline, minih264e)
// on a worker thread, and the coded frames wait for the call's media thread to encrypt and send them.
class VideoSender {
  public:
	static VideoSender &getInstance();

	static constexpr int WIDTH = 320;
	static constexpr int HEIGHT = 240;
	static constexpr int FPS = 15;

	// Opens the camera and starts the worker. False when the camera or the encoder cannot start (for example when the
	// Camera screen has it).
	bool start();
	void stop();
	bool running() const { return active.load(); }

	// Media thread: the oldest coded frame (Annex B, with SPS and PPS when it is a key frame).
	bool takeFrame(std::vector<uint8_t> &out, bool &key);
	// Someone asked for a new key frame (RTCP picture loss).
	void requestKeyFrame() { keyRequested = true; }

  private:
	VideoSender() = default;
	void run();

	WorkerThread worker;
	std::atomic<bool> active{false};
	std::atomic<bool> quit{false};
	std::atomic<bool> keyRequested{false};
	std::mutex mutex;
	struct Frame {
		std::vector<uint8_t> data;
		bool key = false;
	};
	std::deque<Frame> queue;
};

} // namespace Utils

#endif // VIDEO_SENDER_H
