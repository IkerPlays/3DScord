#include "utils/video_sender.h"
#include "log.h"
#include "utils/video_receiver.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Utils {

namespace {
constexpr int TARGET_KBPS_CAMERA = 280;
constexpr int TARGET_KBPS_SCREEN = 350;
constexpr size_t MAX_QUEUED = 4;
double ticksToMs(u64 ticks) { return (double)ticks * 1000.0 / (double)SYSCLOCK_ARM11; }

// The top screen as planar YUV 4:2:0. The framebuffer is stored rotated: column by column, bottom to top, three bytes (B, G, R)
// per pixel.
bool screenToYuv(int W, int H, std::vector<uint8_t> &yPlane, std::vector<uint8_t> &uPlane, std::vector<uint8_t> &vPlane) {
	u16 fbW = 0, fbH = 0;
	const u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbW, &fbH);
	if (!fb || fbW < H) {
		return false;
	}
	for (int y = 0; y < H; y += 2) {
		for (int x = 0; x < W; x += 2) {
			int sumU = 0;
			int sumV = 0;
			for (int dy = 0; dy < 2; dy++) {
				for (int dx = 0; dx < 2; dx++) {
					const u8 *p = fb + ((size_t)(x + dx) * fbW + (fbW - 1 - (y + dy))) * 3;
					const int b = p[0];
					const int g = p[1];
					const int r = p[2];
					yPlane[(size_t)(y + dy) * W + x + dx] = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
					sumU += ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
					sumV += ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
				}
			}
			uPlane[(size_t)(y / 2) * (W / 2) + x / 2] = (uint8_t)std::clamp(sumU / 4, 0, 255);
			vPlane[(size_t)(y / 2) * (W / 2) + x / 2] = (uint8_t)std::clamp(sumV / 4, 0, 255);
		}
	}
	return true;
}
} // namespace

VideoSender &VideoSender::getInstance() {
	static VideoSender instance;
	return instance;
}

bool VideoSender::start(Source source) {
	if (active.load() || source == Source::OFF) {
		return active.load();
	}
	current = source;
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
		current = Source::OFF;
		return false;
	}
	return true;
}

void VideoSender::stop() {
	quit = true;
	worker.join();
	active = false;
	current = Source::OFF;
	std::lock_guard<std::mutex> lock(mutex);
	queue.clear();
	previewVersion = 0;
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

void VideoSender::updatePreview() {
	std::lock_guard<std::mutex> lock(mutex);
	if (previewVersion == 0 || previewVersion == shownVersion || previewBuffer.empty()) {
		return;
	}
	if (!previewReady) {
		memset(&previewTex, 0, sizeof(previewTex));
		previewReady = C3D_TexInit(&previewTex, VIDEO_TEX_W, VIDEO_TEX_H, GPU_RGB565);
		if (previewReady) {
			C3D_TexSetFilter(&previewTex, GPU_LINEAR, GPU_LINEAR);
		}
	}
	if (previewReady) {
		memcpy(previewTex.data, previewBuffer.data(), (size_t)VIDEO_TEX_W * VIDEO_TEX_H * 2);
		GSPGPU_FlushDataCache(previewTex.data, (u32)VIDEO_TEX_W * VIDEO_TEX_H * 2);
		shownW = previewW;
		shownH = previewH;
		gotPreview = true;
	}
	shownVersion = previewVersion;
}

void VideoSender::drawPreview(float x, float y, float z, float w, float h) const {
	if (!hasPreview()) {
		return;
	}
	// Written like the pictures of the others, so it is drawn like them (no sender orientation).
	drawVideoTexture(const_cast<C3D_Tex *>(&previewTex), shownW, shownH, x, y, z, w, h, 0);
}

void VideoSender::releasePreview() {
	if (previewReady) {
		C3D_TexDelete(&previewTex);
		previewReady = false;
	}
	gotPreview = false;
	shownVersion = 0;
}

void VideoSender::run() {
	const Source source = current;
	const int W = width();
	const int H = height();
	const int FPS = fps();
	const bool camera = source == Source::CAMERA;
	const int keyEvery = FPS * 2; // a key frame every two seconds
	const int kbps = camera ? TARGET_KBPS_CAMERA : TARGET_KBPS_SCREEN;

	H264Encoder encoder;
	std::vector<uint8_t> yPlane((size_t)W * H), uPlane((size_t)W * H / 4), vPlane((size_t)W * H / 4);
	unsigned char *packed = nullptr;
	u32 maxBytes = 0;
	Handle event = 0;
	bool ok = encoder.init(W, H, keyEvery, kbps * 1000 / 8 / FPS);
	if (camera) {
		if (R_FAILED(camInit())) {
			Logger::log("[VideoTx] camInit failed (is the Camera screen open?)");
			active = false;
			current = Source::OFF;
			return;
		}
		packed = (unsigned char *)malloc((size_t)W * H * 2);
		// The inner camera, YUYV, 15 frames a second.
		ok = ok && packed && R_SUCCEEDED(CAMU_SetSize(SELECT_IN1, SIZE_QVGA, CONTEXT_A));
		ok = ok && R_SUCCEEDED(CAMU_SetOutputFormat(SELECT_IN1, OUTPUT_YUV_422, CONTEXT_A));
		ok = ok && R_SUCCEEDED(CAMU_SetFrameRate(SELECT_IN1, FRAME_RATE_15));
		ok = ok && R_SUCCEEDED(CAMU_SetNoiseFilter(SELECT_IN1, true));
		ok = ok && R_SUCCEEDED(CAMU_SetAutoExposure(SELECT_IN1, true));
		ok = ok && R_SUCCEEDED(CAMU_SetAutoWhiteBalance(SELECT_IN1, true));
		ok = ok && R_SUCCEEDED(CAMU_SetTrimming(PORT_CAM1, false));
		ok = ok && R_SUCCEEDED(CAMU_GetMaxBytes(&maxBytes, W, H));
		ok = ok && R_SUCCEEDED(CAMU_SetTransferBytes(PORT_CAM1, maxBytes, W, H));
		ok = ok && R_SUCCEEDED(CAMU_Activate(SELECT_IN1));
	}
	if (!ok) {
		Logger::log("[VideoTx] The source or the encoder could not start");
		if (camera) {
			CAMU_Activate(SELECT_NONE);
			camExit();
			free(packed);
		}
		active = false;
		current = Source::OFF;
		return;
	}
	if (camera) {
		CAMU_ClearBuffer(PORT_CAM1);
		CAMU_StartCapture(PORT_CAM1);
	}
	{
		std::lock_guard<std::mutex> lock(mutex);
		previewBuffer.assign((size_t)VIDEO_TEX_W * VIDEO_TEX_H, 0);
		previewVersion = 0;
	}
	Logger::log("[VideoTx] Sending the %s: %dx%d, %d fps, %d kbps", camera ? "camera" : "screen", W, H, FPS, kbps);

	uint32_t frames = 0;
	double msTotal = 0;
	bool first = true;
	u64 nextFrameAt = osGetTime();
	bool receiving = false;
	u64 receivingSince = 0;
	uint32_t restarts = 0;
	while (!quit) {
		if (camera) {
			// The request for a frame stays open until the camera fills it; it is never abandoned (a second request while the first
			// is open fails, and then no frame would ever come again).
			if (!receiving) {
				if (R_SUCCEEDED(CAMU_SetReceiving(&event, packed, PORT_CAM1, (u32)W * H * 2, (s16)maxBytes))) {
					receiving = true;
					receivingSince = osGetTime();
				} else {
					svcSleepThread(20 * 1000000LL);
					continue;
				}
			}
			if (svcWaitSynchronization(event, 100 * 1000000LL) != 0) {
				if (osGetTime() - receivingSince > 3000) {
					// Nothing for three seconds: start the capture again.
					Logger::log("[VideoTx] The camera stopped giving frames, restarting it (%u)", ++restarts);
					CAMU_StopCapture(PORT_CAM1);
					svcCloseHandle(event);
					event = 0;
					receiving = false;
					CAMU_ClearBuffer(PORT_CAM1);
					CAMU_StartCapture(PORT_CAM1);
				}
				continue;
			}
			svcCloseHandle(event);
			event = 0;
			receiving = false;
			// The camera writes the frame into memory by itself (DMA), behind the cache of the processor: without this the processor
			// keeps reading the first frame and the picture freezes.
			GSPGPU_InvalidateDataCache(packed, (u32)W * H * 2);

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
		} else {
			const u64 now = osGetTime();
			if (now < nextFrameAt) {
				svcSleepThread(std::min<u64>(nextFrameAt - now, 20) * 1000000LL);
				continue;
			}
			nextFrameAt = now + 1000 / FPS;
			if (!screenToYuv(W, H, yPlane, uPlane, vPlane)) {
				svcSleepThread(50 * 1000000LL);
				continue;
			}
		}

		// A small copy for the preview (every other frame is enough).
		if ((frames & 1) == 0) {
			std::vector<uint16_t> tiled((size_t)VIDEO_TEX_W * VIDEO_TEX_H, 0);
			int pw = 0, ph = 0;
			if (convertToTiledTexture(yPlane.data(), uPlane.data(), vPlane.data(), W, H, W, W / 2, tiled.data(), pw, ph)) {
				std::lock_guard<std::mutex> lock(mutex);
				previewBuffer.swap(tiled);
				previewW = pw;
				previewH = ph;
				previewVersion++;
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

	if (camera) {
		CAMU_StopCapture(PORT_CAM1);
		if (receiving && event) {
			svcWaitSynchronization(event, 300 * 1000000LL);
		}
		if (event) {
			svcCloseHandle(event);
			event = 0;
		}
		CAMU_Activate(SELECT_NONE);
		camExit();
		free(packed);
	}
	active = false;
	Logger::log("[VideoTx] %s off", camera ? "Camera" : "Screen");
}

} // namespace Utils
