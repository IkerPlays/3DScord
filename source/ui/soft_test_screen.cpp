#include <malloc.h>
#include "ui/soft_test_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "utils/redraw.h"
#include <3ds.h>
#include <algorithm>
#include <citro2d.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

extern "C" {
extern char *fake_heap_start;
extern char *fake_heap_end;
}

namespace UI {

namespace {
constexpr int TEX_W = 512;
constexpr int TEX_H = 512;

// Every step goes to the log and to softtest.txt on the SD card, so it can be sent as a file.
void note(const char *fmt, ...) {
	char line[400];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	Logger::log("%s", line);
	mkdir("sdmc:/3ds", 0777);
	mkdir(CONFIG_DIR_PATH, 0777);
	if (FILE *f = fopen(CONFIG_DIR_PATH "/softtest.txt", "a")) {
		fprintf(f, "%s\n", line);
		fclose(f);
	}
}

double ticksToMs(u64 t) { return (double)t * 1000.0 / (double)SYSCLOCK_ARM11; }
inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
} // namespace

void SoftTestScreen::open() { ScreenManager::getInstance().pushScreen(ScreenType::SOFT_TEST); }

SoftTestScreen::SoftTestScreen() { memset(&texture, 0, sizeof(texture)); }

SoftTestScreen::~SoftTestScreen() {
	decoder.shutdown();
	if (textureReady) {
		C3D_TexDelete(&texture);
	}
	if (picture) {
		linearFree(picture);
	}
}

void SoftTestScreen::onEnter() {
	note("[SoftTest] Opened");
	if (!loadFile()) {
		status = TR("mvdtest.no_file");
		note("[SoftTest] No usable video file");
		return;
	}
	note("[SoftTest] %s: %zu bytes, %zu NAL units", path.c_str(), data.size(), nals.size());
	status = TR("mvdtest.ready");
}

bool SoftTestScreen::loadFile() {
	const std::string dir = std::string(CONFIG_DIR_PATH) + "/recordings";
	std::string best;
	if (DIR *d = opendir(dir.c_str())) {
		while (dirent *e = readdir(d)) {
			const std::string name = e->d_name;
			if (name.rfind("video_in_", 0) == 0 && name > best) {
				best = name;
			}
		}
		closedir(d);
	}
	if (best.empty()) {
		return false;
	}
	path = dir + "/" + best;
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) {
		return false;
	}
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0 || size > 4 * 1024 * 1024) {
		fclose(f);
		return false;
	}
	data.resize((size_t)size);
	if (fread(data.data(), 1, data.size(), f) != data.size()) {
		fclose(f);
		return false;
	}
	fclose(f);

	// The NAL units from the first parameter set on (the start of a key frame).
	std::vector<size_t> starts;
	for (size_t i = 0; i + 4 < data.size(); i++) {
		if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) {
			starts.push_back(i);
			i += 3;
		}
	}
	bool seenSps = false;
	for (size_t k = 0; k < starts.size(); k++) {
		const size_t end = k + 1 < starts.size() ? starts[k + 1] : data.size();
		const int type = data[starts[k] + 4] & 0x1F;
		if (type == 7) {
			seenSps = true;
		}
		if (seenSps) {
			nals.push_back({starts[k], end - starts[k], type});
		}
	}
	return !nals.empty();
}

// Half size, converted from YUV to RGB565, and uploaded to the texture.
void SoftTestScreen::show(const Utils::SoftDecoder::Picture &pic) {
	const int w = (pic.width / 2) & ~7;
	const int h = (pic.height / 2) & ~7;
	if (w <= 0 || h <= 0 || w > TEX_W || h > TEX_H) {
		return;
	}
	if (!picture || picW != w || picH != h) {
		if (picture) {
			linearFree(picture);
		}
		picture = (u16 *)linearAlloc((size_t)w * h * 2);
		picW = w;
		picH = h;
		if (!picture) {
			return;
		}
	}
	if (!textureReady) {
		textureReady = C3D_TexInit(&texture, TEX_W, TEX_H, GPU_RGB565);
		if (textureReady) {
			C3D_TexSetFilter(&texture, GPU_LINEAR, GPU_LINEAR);
		}
	}
	for (int y = 0; y < h; y++) {
		const uint8_t *py = pic.y + (size_t)(y * 2) * pic.strideY;
		const uint8_t *pu = pic.u + (size_t)y * pic.strideUV;
		const uint8_t *pv = pic.v + (size_t)y * pic.strideUV;
		u16 *dst = picture + (size_t)y * w;
		for (int x = 0; x < w; x++) {
			const int c = py[x * 2] - 16;
			const int d = pu[x] - 128;
			const int e = pv[x] - 128;
			const int r = clamp255((298 * c + 409 * e + 128) >> 8);
			const int g = clamp255((298 * c - 100 * d - 208 * e + 128) >> 8);
			const int b = clamp255((298 * c + 516 * d + 128) >> 8);
			dst[x] = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
		}
	}
	if (textureReady) {
		GSPGPU_FlushDataCache(picture, (u32)w * h * 2);
		C3D_SyncDisplayTransfer((u32 *)picture, GX_BUFFER_DIM(w, h), (u32 *)texture.data, GX_BUFFER_DIM(TEX_W, TEX_H),
		                        (GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
		                         GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
		                         GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
		                         GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO)));
		haveFrame = true;
	}
}

void SoftTestScreen::step() {
	if (next >= nals.size()) {
		running = false;
		status = TR("mvdtest.done");
		note("[SoftTest] Done: %d pictures, %.1f ms on average (max %.1f, first %.1f)", frames,
		     frames ? msTotal / frames : 0.0, msMax, msFirst);
		return;
	}
	const Nal &n = nals[next++];
	const u64 t0 = svcGetSystemTick();
	Utils::SoftDecoder::Picture pic;
	bool got = false;
	lastState = decoder.decode(data.data() + n.offset, n.size, pic, got);
	const double ms = ticksToMs(svcGetSystemTick() - t0);
	if (next <= 8 || got) {
		note("[SoftTest] NAL %zu type %d (%zu bytes): state %d, %.1f ms%s", next - 1, n.type, n.size, lastState, ms,
		     got ? ", picture" : "");
	}
	if (got) {
		if (frames == 0) {
			msFirst = ms;
		}
		frames++;
		msTotal += ms;
		if (ms > msMax) {
			msMax = ms;
		}
		show(pic);
	}
	Utils::requestRedraw();
}

void SoftTestScreen::update() {
	const u32 kDown = hidKeysDown();
	if (kDown & KEY_B) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	if (!running && (kDown & KEY_A) && !nals.empty() && next == 0) {
		{
			const struct mallinfo mi = mallinfo();
			note("[SoftTest] A pressed: heap %ld KB, in use %d KB, free %d KB; starting openh264",
			     (long)((fake_heap_end - fake_heap_start) / 1024), mi.uordblks / 1024, mi.fordblks / 1024);
		}
		note("[SoftTest] calling init");
		if (!decoder.init()) {
			status = TR("softtest.failed");
			note("[SoftTest] The decoder could not start");
			return;
		}
		note("[SoftTest] init ok");
		running = true;
		status = TR("mvdtest.running");
	}
	if (running) {
		step();
	}
}

void SoftTestScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(target);
	if (haveFrame && textureReady) {
		Tex3DS_SubTexture sub = {(u16)picW, (u16)picH, 0.0f, 1.0f, (float)picW / TEX_W, 1.0f - (float)picH / TEX_H};
		C2D_Image img = {&texture, &sub};
		const float scale = std::min(400.0f / picW, 240.0f / picH);
		C2D_DrawImageAt(img, (400.0f - picW * scale) / 2.0f, (240.0f - picH * scale) / 2.0f, 0.5f, nullptr, scale, scale);
	} else {
		drawCenteredText(100.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), status, 400.0f);
	}
}

void SoftTestScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);
	drawCenteredText(20.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorAccent(), TR("softtest.title"), 320.0f);
	drawCenteredText(48.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), path, 320.0f);
	char line[120];
	snprintf(line, sizeof(line), "NAL %zu/%zu   state %d", next, nals.size(), lastState);
	drawCenteredText(80.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), line, 320.0f);
	snprintf(line, sizeof(line), "%s: %d   %.0f ms (max %.0f)", TR("videotest.frames").c_str(), frames,
	         frames ? msTotal / frames : 0.0, msMax);
	drawCenteredText(104.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), line, 320.0f);
	drawCenteredText(150.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorSuccess(),
	                 TR(running ? "mvdtest.running" : "videotest.hint_start"), 320.0f);
	drawCenteredText(180.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("voice_msg.hint_cancel"), 320.0f);
}

} // namespace UI
