#include "utils/video_sender.h"
#include "log.h"
#include <cstdlib>
#include <cstring>

namespace Utils {

namespace {
constexpr int KEY_EVERY = VideoSender::FPS * 2;                       // a key frame every two seconds
constexpr int TARGET_KBPS = 280;
constexpr size_t MAX_QUEUED = 4;
double ticksToMs(u64 ticks) { return (double)ticks * 1000.0 / (double)SYSCLOCK_ARM11; }
} // namespace

VideoSender &VideoSender::getInstance() {
	static VideoSender instance;
	return instance;
}

bool VideoSender::start() {
	if (active.load()) {
		return true;
	}
	quit = false;
	keyRequested = true;
	{
		std::lock_guard<std::mutex> lock(mutex);
		queue.clear();
	}
	active = true;
	worker.start([this] { run(); }, 3, 128 * 1024, true); // the encoder keeps some big arrays on the stack
	if (!worker.joinable()) {
		active = false;
		return false;
	}
	return true;
}

void VideoSender::stop() {
	quit = true;
	worker.join();
	active = false;
	std::lock_guard<std::mutex> lock(mutex);
	queue.clear();
}

bool VideoSender::takeFrame(std::vector<uint8_t> &out, bool &key) {
	std::lock_guard<std::mutex> lock(mutex);
	if (queue.empty()) {
		return false;
	}
	out.swap(queue.front().data);
	key = queue.front().key;
	queue.pop_front();
	return true;
}

void VideoSender::run() {
	constexpr int W = WIDTH;
	constexpr int H = HEIGHT;
	if (R_FAILED(camInit())) {
		Logger::log("[VideoTx] camInit failed (is the Camera screen open?)");
		active = false;
		return;
	}
	H264Encoder encoder;
	std::vector<uint8_t> yPlane((size_t)W * H), uPlane((size_t)W * H / 4), vPlane((size_t)W * H / 4);
	unsigned char *packed = (unsigned char *)malloc((size_t)W * H * 2);
	u32 maxBytes = 0;
	Handle event = 0;
	bool ok = packed && encoder.init(W, H, KEY_EVERY, TARGET_KBPS * 1000 / 8 / FPS);
	// The inner camera, YUYV, 15 frames a second.
	ok = ok && R_SUCCEEDED(CAMU_SetSize(SELECT_IN1, SIZE_QVGA, CONTEXT_A));
	ok = ok && R_SUCCEEDED(CAMU_SetOutputFormat(SELECT_IN1, OUTPUT_YUV_422, CONTEXT_A));
	ok = ok && R_SUCCEEDED(CAMU_SetFrameRate(SELECT_IN1, FRAME_RATE_15));
	ok = ok && R_SUCCEEDED(CAMU_SetNoiseFilter(SELECT_IN1, true));
	ok = ok && R_SUCCEEDED(CAMU_SetAutoExposure(SELECT_IN1, true));
	ok = ok && R_SUCCEEDED(CAMU_SetAutoWhiteBalance(SELECT_IN1, true));
	ok = ok && R_SUCCEEDED(CAMU_SetTrimming(PORT_CAM1, false));
	ok = ok && R_SUCCEEDED(CAMU_GetMaxBytes(&maxBytes, W, H));
	ok = ok && R_SUCCEEDED(CAMU_SetTransferBytes(PORT_CAM1, maxBytes, W, H));
	ok = ok && R_SUCCEEDED(CAMU_Activate(SELECT_IN1));
	if (!ok) {
		Logger::log("[VideoTx] The camera or the encoder could not start");
		CAMU_Activate(SELECT_NONE);
		camExit();
		free(packed);
		active = false;
		return;
	}
	CAMU_ClearBuffer(PORT_CAM1);
	CAMU_StartCapture(PORT_CAM1);
	Logger::log("[VideoTx] Sending the camera: %dx%d, %d fps, %d kbps", W, H, FPS, TARGET_KBPS);

	uint32_t frames = 0;
	double msTotal = 0;
	bool first = true;
	while (!quit) {
		if (R_FAILED(CAMU_SetReceiving(&event, packed, PORT_CAM1, (u32)W * H * 2, (s16)maxBytes))) {
			svcSleepThread(20 * 1000000LL);
			continue;
		}
		if (svcWaitSynchronization(event, 300 * 1000000LL) != 0) {
			svcCloseHandle(event);
			event = 0;
			CAMU_ClearBuffer(PORT_CAM1);
			continue;
		}
		svcCloseHandle(event);
		event = 0;

		// The camera gives YUYV (a pair of pixels shares its colour); the encoder wants separate planes.
		for (int row = 0; row < H; row++) {
			const unsigned char *src = packed + (size_t)row * W * 2;
			unsigned char *yd = &yPlane[(size_t)row * W];
			unsigned char *ud = &uPlane[(size_t)(row / 2) * (W / 2)];
			unsigned char *vd = &vPlane[(size_t)(row / 2) * (W / 2)];
			for (int x = 0; x < W / 2; x++) {
				yd[x * 2] = src[x * 4];
				yd[x * 2 + 1] = src[x * 4 + 2];
				if ((row & 1) == 0) {
					ud[x] = src[x * 4 + 1];
					vd[x] = src[x * 4 + 3];
				} else {
					ud[x] = (unsigned char)((ud[x] + src[x * 4 + 1] + 1) / 2);
					vd[x] = (unsigned char)((vd[x] + src[x * 4 + 3] + 1) / 2);
				}
			}
		}

		const bool forceKey = first || keyRequested.exchange(false);
		first = false;
		const u64 t0 = svcGetSystemTick();
		const unsigned char *coded = nullptr;
		int codedBytes = 0;
		if (encoder.encode(yPlane.data(), uPlane.data(), vPlane.data(), forceKey, &coded, &codedBytes) && coded &&
		    codedBytes > 0) {
			msTotal += ticksToMs(svcGetSystemTick() - t0);
			frames++;
			if (frames <= 3 || frames % 150 == 0) {
				Logger::log("[VideoTx] %u frames, %.1f ms each, last %d bytes%s", frames, msTotal / frames, codedBytes,
				            forceKey ? " (key)" : "");
			}
			std::lock_guard<std::mutex> lock(mutex);
			if (queue.size() >= MAX_QUEUED) {
				queue.clear(); // the call cannot keep up: start again with a key frame
				keyRequested = true;
			} else {
				Frame frame;
				frame.data.assign(coded, coded + codedBytes);
				frame.key = forceKey;
				queue.push_back(std::move(frame));
			}
		}
	}

	CAMU_StopCapture(PORT_CAM1);
	CAMU_Activate(SELECT_NONE);
	camExit();
	free(packed);
	active = false;
	Logger::log("[VideoTx] Camera off");
}

} // namespace Utils
