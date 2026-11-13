#include "ui/video_test_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "utils/redraw.h"
#include <3ds.h>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>

namespace UI {

namespace {
constexpr int W = 176;
constexpr int H = 144;
constexpr int TEST_SECONDS = 10;
constexpr int TARGET_FPS = 15;
constexpr int TARGET_KBPS = 150;

double ticksToMs(u64 ticks) { return (double)ticks * 1000.0 / (double)SYSCLOCK_ARM11; }
} // namespace

void VideoTestScreen::open() { ScreenManager::getInstance().pushScreen(ScreenType::VIDEO_TEST); }

VideoTestScreen::VideoTestScreen() {
	yPlane.resize((size_t)W * H);
	uPlane.resize((size_t)W * H / 4);
	vPlane.resize((size_t)W * H / 4);
}

VideoTestScreen::~VideoTestScreen() {
	stopCamera();
	if (file) {
		fclose(file);
	}
}

void VideoTestScreen::onEnter() {
	if (!encoder.init(W, H, TARGET_FPS * 2, TARGET_KBPS * 1000 / 8 / TARGET_FPS)) {
		state = State::FAILED;
	}
}

bool VideoTestScreen::startCamera() {
	if (R_FAILED(camInit())) {
		return false;
	}
	packed = (unsigned char *)malloc((size_t)W * H * 2);
	if (!packed) {
		camExit();
		return false;
	}
	bool ok = R_SUCCEEDED(CAMU_SetSize(SELECT_OUT1, SIZE_QCIF, CONTEXT_A));
	ok = ok && R_SUCCEEDED(CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_YUV_422, CONTEXT_A));
	ok = ok && R_SUCCEEDED(CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_15));
	ok = ok && R_SUCCEEDED(CAMU_SetNoiseFilter(SELECT_OUT1, true));
	ok = ok && R_SUCCEEDED(CAMU_SetAutoExposure(SELECT_OUT1, true));
	ok = ok && R_SUCCEEDED(CAMU_SetAutoWhiteBalance(SELECT_OUT1, true));
	ok = ok && R_SUCCEEDED(CAMU_SetTrimming(PORT_CAM1, false));
	ok = ok && R_SUCCEEDED(CAMU_GetMaxBytes(&maxBytes, W, H));
	ok = ok && R_SUCCEEDED(CAMU_SetTransferBytes(PORT_CAM1, maxBytes, W, H));
	ok = ok && R_SUCCEEDED(CAMU_Activate(SELECT_OUT1));
	if (!ok) {
		CAMU_Activate(SELECT_NONE);
		camExit();
		free(packed);
		packed = nullptr;
		return false;
	}
	CAMU_ClearBuffer(PORT_CAM1);
	CAMU_StartCapture(PORT_CAM1);
	cameraOn = true;
	receiving = false;
	return true;
}

void VideoTestScreen::stopCamera() {
	if (!cameraOn) {
		return;
	}
	CAMU_StopCapture(PORT_CAM1);
	if (receiving && event) {
		svcWaitSynchronization(event, 200000000LL);
	}
	if (event) {
		svcCloseHandle(event);
		event = 0;
	}
	CAMU_Activate(SELECT_NONE);
	camExit();
	cameraOn = false;
	receiving = false;
	free(packed);
	packed = nullptr;
}

void VideoTestScreen::start() {
	mkdir("sdmc:/3ds", 0777);
	mkdir(CONFIG_DIR_PATH, 0777);
	const std::string dir = std::string(CONFIG_DIR_PATH) + "/recordings";
	mkdir(dir.c_str(), 0777);
	char name[64];
	const time_t now = time(nullptr);
	strftime(name, sizeof(name), "/video_test_%Y%m%d_%H%M%S.264", gmtime(&now));
	path = dir + name;
	file = fopen(path.c_str(), "wb");
	if (!file || !startCamera()) {
		state = State::FAILED;
		return;
	}
	frames = 0;
	bytes = 0;
	encodeMsTotal = 0.0;
	encodeMsMax = 0.0;
	startedAt = osGetTime();
	state = State::RECORDING;
	Logger::log("[VideoTest] Recording to %s", path.c_str());
}

// The camera gives YUYV (a pair of pixels shares its colour); the encoder wants separate planes with the colour of each
// 2x2 block.
void VideoTestScreen::pumpFrame() {
	if (!receiving) {
		if (R_SUCCEEDED(CAMU_SetReceiving(&event, packed, PORT_CAM1, (u32)W * H * 2, (s16)maxBytes))) {
			receiving = true;
		}
		return;
	}
	if (svcWaitSynchronization(event, 0) != 0) {
		return;
	}
	svcCloseHandle(event);
	event = 0;
	receiving = false;
	GSPGPU_InvalidateDataCache(packed, (u32)W * H * 2); // the camera wrote the frame behind the cache of the processor

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

	const u64 t0 = svcGetSystemTick();
	const unsigned char *coded = nullptr;
	int codedBytes = 0;
	const bool ok = encoder.encode(yPlane.data(), uPlane.data(), vPlane.data(), frames == 0, &coded, &codedBytes);
	const double ms = ticksToMs(svcGetSystemTick() - t0);
	if (ok && coded && codedBytes > 0) {
		fwrite(coded, 1, (size_t)codedBytes, file);
		bytes += codedBytes;
		frames++;
		encodeMsTotal += ms;
		if (ms > encodeMsMax) {
			encodeMsMax = ms;
		}
	}
	Utils::requestRedraw();
}

void VideoTestScreen::finish() {
	seconds = (osGetTime() - startedAt) / 1000.0;
	stopCamera();
	if (file) {
		fclose(file);
		file = nullptr;
	}
	state = State::DONE;
	Logger::log("[VideoTest] %d frames in %.1f s, %lld bytes, %.1f ms/frame (max %.1f)", frames, seconds, bytes,
	            frames ? encodeMsTotal / frames : 0.0, encodeMsMax);
}

void VideoTestScreen::update() {
	const u32 kDown = hidKeysDown();
	if (kDown & KEY_B) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	if (state == State::READY && (kDown & KEY_A)) {
		start();
	} else if (state == State::DONE && (kDown & KEY_A)) {
		state = State::READY;
	}
	if (state == State::RECORDING) {
		pumpFrame();
		if (osGetTime() - startedAt >= (u64)TEST_SECONDS * 1000ULL) {
			finish();
		}
		Utils::requestRedraw();
	}
}

void VideoTestScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackground());
	C2D_SceneBegin(target);
	drawCenteredText(16.0f, 0.5f, 0.6f, 0.6f, ScreenManager::colorAccent(), TR("videotest.title"), 400.0f);
	drawCenteredText(40.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), TR("videotest.experimental"), 400.0f);

	char line[160];
	float y = 80.0f;
	auto put = [&](const char *text) {
		drawCenteredText(y, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), text, 400.0f);
		y += 22.0f;
	};
	if (state == State::RECORDING) {
		snprintf(line, sizeof(line), "%d / %d s", (int)((osGetTime() - startedAt) / 1000), TEST_SECONDS);
		put(line);
		snprintf(line, sizeof(line), "%s: %d", TR("videotest.frames").c_str(), frames);
		put(line);
		if (frames > 0) {
			snprintf(line, sizeof(line), "%.1f ms / %s", encodeMsTotal / frames, TR("videotest.frame").c_str());
			put(line);
		}
	} else if (state == State::DONE) {
		const double avg = frames ? encodeMsTotal / frames : 0.0;
		snprintf(line, sizeof(line), "%s: %d  (%.1f %s)", TR("videotest.frames").c_str(), frames,
		         seconds > 0 ? frames / seconds : 0.0, "fps");
		put(line);
		snprintf(line, sizeof(line), "%s: %.1f ms   max %.1f ms", TR("videotest.encode").c_str(), avg, encodeMsMax);
		put(line);
		snprintf(line, sizeof(line), "%lld KB  (%.0f kbps)", bytes / 1024, seconds > 0 ? bytes * 8.0 / seconds / 1000.0 : 0.0);
		put(line);
		drawCenteredText(y + 4.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), path, 400.0f);
	} else if (state == State::FAILED) {
		put(TR("videotest.failed").c_str());
	} else {
		put(TR("videotest.ready").c_str());
	}
}

void VideoTestScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);
	drawCenteredText(90.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorSuccess(),
	                 TR(state == State::RECORDING ? "videotest.recording" : "videotest.hint_start"), 320.0f);
	drawCenteredText(150.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("voice_msg.hint_cancel"), 320.0f);
}

} // namespace UI
