#include "utils/video_sender.h"
#include "log.h"
#include "utils/video_receiver.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Utils {

namespace {
constexpr int TARGET_KBPS_SCREEN = 350;
constexpr size_t MAX_QUEUED = 4;
double ticksToMs(u64 ticks) { return (double)ticks * 1000.0 / (double)SYSCLOCK_ARM11; }

// The top screen (400x240), reduced to W x H, as planar YUV 4:2:0 (each pixel is the average of 2x2 pixels of the screen).
// The framebuffer is stored rotated: column by column, bottom to top, three bytes (B, G, R) per pixel.
bool screenToYuv(int W, int H, std::vector<uint8_t> &yPlane, std::vector<uint8_t> &uPlane, std::vector<uint8_t> &vPlane) {
	u16 fbW = 0, fbH = 0;
	const u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbW, &fbH);
	if (!fb || fbW < 240) {
		return false;
	}
	auto pixel = [&](int sx, int sy, int &r, int &g, int &b) {
		sx = std::min(sx, 399);
		sy = std::min(sy, 239);
		const u8 *p = fb + ((size_t)sx * fbW + (fbW - 1 - sy)) * 3;
		b = p[0];
		g = p[1];
		r = p[2];
	};
	for (int y = 0; y < H; y += 2) {
		for (int x = 0; x < W; x += 2) {
			int sumU = 0;
			int sumV = 0;
			for (int dy = 0; dy < 2; dy++) {
				for (int dx = 0; dx < 2; dx++) {
					// Each pixel of the stream is the average of 2x2 pixels of the screen.
					int r = 0, g = 0, b = 0;
					for (int k = 0; k < 4; k++) {
						int pr, pg, pb;
						pixel((x + dx) * 400 / W + (k & 1), (y + dy) * 240 / H + (k >> 1), pr, pg, pb);
						r += pr;
						g += pg;
						b += pb;
					}
					r /= 4;
					g /= 4;
					b /= 4;
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
	mirrorPreview = source == Source::CAMERA;
	curW = source == Source::SCREEN ? 208 : 192;
	curH = source == Source::SCREEN ? 128 : 144;
	resChanged = false;
	quit = false;
	keyRequested = true;
	statFpsNow = 0;
	statMsNow = 0;
	keyAsks = 0;
	drops = 0;
	{
		std::lock_guard<std::mutex> lock(mutex);
		queue.clear();
	}
	ready = false;
	active = true;
	worker.start([this] { run(); }, 3, 128 * 1024, true); // the encoder keeps some big arrays on the stack
	if (!worker.joinable()) {
		active = false;
		current = Source::OFF;
		return false;
	}
	// The camera or the encoder may fail to start (the camera is in use, for example): wait to know, so the call is not told
	// about a video that never comes.
	const u64 until = osGetTime() + 2500;
	while (!ready.load() && active.load() && osGetTime() < until) {
		svcSleepThread(20 * 1000000LL);
	}
	if (!ready.load()) {
		quit = true;
		worker.join();
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
		previewReady = C3D_TexInit(&previewTex, PREVIEW_W, PREVIEW_H, GPU_RGB565);
		if (previewReady) {
			C3D_TexSetFilter(&previewTex, GPU_LINEAR, GPU_LINEAR);
		}
	}
	if (previewReady) {
		memcpy(previewTex.data, previewBuffer.data(), (size_t)PREVIEW_W * PREVIEW_H * 2);
		GSPGPU_FlushDataCache(previewTex.data, (u32)PREVIEW_W * PREVIEW_H * 2);
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
	drawVideoTextureOriented(const_cast<C3D_Tex *>(&previewTex), shownW, shownH, x, y, z, w, h, 0, mirrorPreview);
}

void VideoSender::releasePreview() {
	if (previewReady) {
		C3D_TexDelete(&previewTex);
		previewReady = false;
	}
	gotPreview = false;
	shownVersion = 0;
}

namespace {
struct Level {
	int w;
	int h;
};
// The sizes tried, from the best to the lightest (all multiples of 16 in width and height or at least of 16 for the encoder).
const Level CAMERA_LEVELS[4] = {{256, 192}, {192, 144}, {128, 96}, {96, 80}};
const Level SCREEN_LEVELS[3] = {{208, 128}, {160, 96}, {112, 64}};
constexpr int CAM_START_LEVEL = 1; // the camera starts at 192x144 and goes up when the encoder has room
constexpr int CAM_W = 256; // what the camera gives (its "DS LCD" size)
constexpr int CAM_H = 192;

// Reduces planar YUV 4:2:0 (srcW x srcH) to dstW x dstH by taking the nearest pixel.
void scalePlanes(const std::vector<uint8_t> &sy, const std::vector<uint8_t> &su, const std::vector<uint8_t> &sv, int srcW, int srcH,
                 std::vector<uint8_t> &dy, std::vector<uint8_t> &du, std::vector<uint8_t> &dv, int dstW, int dstH) {
	for (int y = 0; y < dstH; y++) {
		const int syy = y * srcH / dstH;
		for (int x = 0; x < dstW; x++) {
			dy[(size_t)y * dstW + x] = sy[(size_t)syy * srcW + x * srcW / dstW];
		}
	}
	for (int y = 0; y < dstH / 2; y++) {
		const int syy = y * (srcH / 2) / (dstH / 2);
		for (int x = 0; x < dstW / 2; x++) {
			const size_t si = (size_t)syy * (srcW / 2) + x * (srcW / 2) / (dstW / 2);
			du[(size_t)y * (dstW / 2) + x] = su[si];
			dv[(size_t)y * (dstW / 2) + x] = sv[si];
		}
	}
}
} // namespace

void VideoSender::run() {
	const Source source = current;
	const bool camera = source == Source::CAMERA || source == Source::OUTER;
	const u32 select = source == Source::OUTER ? SELECT_OUT1 : SELECT_IN1;
	const Level *levels = camera ? CAMERA_LEVELS : SCREEN_LEVELS;
	const int levelCount = camera ? 4 : 3;
	int level = camera ? CAM_START_LEVEL : 0;
	int W = levels[level].w;
	int H = levels[level].h;
	const int FPS = fps();
	const int keyEvery = FPS * 2; // a key frame every two seconds
	// A bigger picture gets more bits.
	auto kbpsFor = [&](int lv) { return camera ? (lv == 0 ? 380 : lv == 1 ? 300 : lv == 2 ? 220 : 160) : TARGET_KBPS_SCREEN; };
	int kbps = kbpsFor(level);

	H264Encoder encoder;
	// What the encoder gets (W x H), and, for the camera, what the camera gave (CAM_W x CAM_H).
	std::vector<uint8_t> yPlane((size_t)W * H), uPlane((size_t)W * H / 4), vPlane((size_t)W * H / 4);
	std::vector<uint8_t> camY, camU, camV;
	unsigned char *packed = nullptr;
	u32 maxBytes = 0;
	Handle event = 0;
	bool ok = encoder.init(W, H, keyEvery, kbps * 1000 / 8 / FPS);
	if (camera) {
		camY.resize((size_t)CAM_W * CAM_H);
		camU.resize((size_t)CAM_W * CAM_H / 4);
		camV.resize((size_t)CAM_W * CAM_H / 4);
		if (R_FAILED(camInit())) {
			Logger::log("[VideoTx] camInit failed (is the Camera screen open?)");
			active = false;
			current = Source::OFF;
			return;
		}
		packed = (unsigned char *)malloc((size_t)CAM_W * CAM_H * 2);
		// The camera (inner or outer), YUYV, 10 frames a second (15 left the encoder no room next to the audio and the decoder).
		ok = ok && packed && R_SUCCEEDED(CAMU_SetSize(select, SIZE_DS_LCD, CONTEXT_A));
		ok = ok && R_SUCCEEDED(CAMU_SetOutputFormat(select, OUTPUT_YUV_422, CONTEXT_A));
		ok = ok && R_SUCCEEDED(CAMU_SetFrameRate(select, FRAME_RATE_10));
		ok = ok && R_SUCCEEDED(CAMU_SetNoiseFilter(select, true));
		ok = ok && R_SUCCEEDED(CAMU_SetAutoExposure(select, true));
		ok = ok && R_SUCCEEDED(CAMU_SetAutoWhiteBalance(select, true));
		ok = ok && R_SUCCEEDED(CAMU_SetTrimming(PORT_CAM1, false));
		ok = ok && R_SUCCEEDED(CAMU_GetMaxBytes(&maxBytes, CAM_W, CAM_H));
		ok = ok && R_SUCCEEDED(CAMU_SetTransferBytes(PORT_CAM1, maxBytes, CAM_W, CAM_H));
		ok = ok && R_SUCCEEDED(CAMU_Activate(select));
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
		previewBuffer.assign((size_t)PREVIEW_W * PREVIEW_H, 0);
		previewVersion = 0;
	}
	ready = true;
	Logger::log("[VideoTx] Sending the %s: %dx%d, %d fps, %d kbps", camera ? (source == Source::OUTER ? "outer camera" : "inner camera") : "screen", W, H, FPS, kbps);

	uint32_t frames = 0;
	double msTotal = 0;
	bool first = true;
	u64 nextFrameAt = osGetTime();
	bool receiving = false;
	u64 receivingSince = 0;
	uint32_t restarts = 0;
	u64 windowStart = osGetTime();
	int windowFrames = 0;
	double windowMs = 0;
	// For choosing the size: the average over about two seconds.
	u64 judgeStart = osGetTime();
	int judgeFrames = 0;
	double judgeMs = 0;
	u64 lastLevelChange = osGetTime();
	while (!quit) {
		if (camera) {
			// The request for a frame stays open until the camera fills it; it is never abandoned (a second request while the first
			// is open fails, and then no frame would ever come again).
			if (!receiving) {
				if (R_SUCCEEDED(CAMU_SetReceiving(&event, packed, PORT_CAM1, (u32)CAM_W * CAM_H * 2, (s16)maxBytes))) {
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
			GSPGPU_InvalidateDataCache(packed, (u32)CAM_W * CAM_H * 2);

			// The camera gives YUYV (a pair of pixels shares its colour); the encoder wants separate planes.
			for (int row = 0; row < CAM_H; row++) {
				const unsigned char *src = packed + (size_t)row * CAM_W * 2;
				unsigned char *yd = &camY[(size_t)row * CAM_W];
				unsigned char *ud = &camU[(size_t)(row / 2) * (CAM_W / 2)];
				unsigned char *vd = &camV[(size_t)(row / 2) * (CAM_W / 2)];
				for (int x = 0; x < CAM_W / 2; x++) {
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
			if (W == CAM_W && H == CAM_H) {
				yPlane = camY;
				uPlane = camU;
				vPlane = camV;
			} else {
				scalePlanes(camY, camU, camV, CAM_W, CAM_H, yPlane, uPlane, vPlane, W, H);
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

		// A copy for the preview.
		{
			std::vector<uint16_t> tiled((size_t)PREVIEW_W * PREVIEW_H, 0);
			int pw = 0, ph = 0;
			if (convertToTiledTexture(yPlane.data(), uPlane.data(), vPlane.data(), W, H, W, W / 2, tiled.data(), pw, ph, PREVIEW_W,
			                          PREVIEW_H)) {
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
			const double frameMs = ticksToMs(svcGetSystemTick() - t0);
			msTotal += frameMs;
			windowMs += frameMs;
			windowFrames++;
			judgeMs += frameMs;
			judgeFrames++;
			frames++;
			if (osGetTime() - windowStart >= 1000) {
				statFpsNow = windowFrames;
				statMsNow = (int)(windowMs / std::max(1, windowFrames));
				windowStart = osGetTime();
				windowFrames = 0;
				windowMs = 0;
			}
			if (frames <= 3 || frames % 150 == 0) {
				Logger::log("[VideoTx] %u frames, %.1f ms each, last %d bytes%s (%dx%d)", frames, msTotal / frames, codedBytes,
				            forceKey ? " (key)" : "", W, H);
			}
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (queue.size() >= MAX_QUEUED) {
					queue.clear(); // the call cannot keep up: start again with a key frame
					keyRequested = true;
					drops++;
				} else {
					Frame frame;
					frame.data.assign(coded, coded + codedBytes);
					frame.key = forceKey;
					queue.push_back(std::move(frame));
				}
			}

			// Every two seconds: too slow (more than 90 ms a frame) goes to a lighter size; with lots of room (under 35 ms for
			// eight seconds) goes to a bigger one.
			const u64 nowMs = osGetTime();
			if (nowMs - judgeStart >= 2000 && judgeFrames >= 3) {
				const double avg = judgeMs / judgeFrames;
				int target = level;
				if (avg > 90.0 && level < levelCount - 1) {
					target = level + 1;
				} else if (avg < 35.0 && level > 0 && nowMs - lastLevelChange > 8000) {
					target = level - 1;
				}
				if (target != level) {
					level = target;
					W = levels[level].w;
					H = levels[level].h;
					kbps = kbpsFor(level);
					yPlane.assign((size_t)W * H, 0);
					uPlane.assign((size_t)W * H / 4, 128);
					vPlane.assign((size_t)W * H / 4, 128);
					if (encoder.init(W, H, keyEvery, kbps * 1000 / 8 / FPS)) {
						first = true;
						curW = W;
						curH = H;
						resChanged = true;
						lastLevelChange = nowMs;
						std::lock_guard<std::mutex> lock(mutex);
						queue.clear();
						Logger::log("[VideoTx] %.0f ms a frame: the size is now %dx%d", avg, W, H);
					} else {
						Logger::log("[VideoTx] The encoder could not change to %dx%d", W, H);
						quit = true;
					}
				}
				judgeStart = nowMs;
				judgeFrames = 0;
				judgeMs = 0;
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
