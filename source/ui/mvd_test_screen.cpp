#include "ui/mvd_test_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "utils/redraw.h"
#include "utils/system_utils.h"
#include <3ds.h>
#include <citro2d.h>
#include <cstdarg>
#include <cstdio>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <dirent.h>

namespace UI {

namespace {
constexpr int TEX_W = 512;
constexpr int TEX_H = 512;

// Reads bits from an H.264 parameter set (after the emulation prevention bytes are removed).
struct Bits {
	const unsigned char *d;
	size_t n;
	size_t pos = 0;
	bool ok = true;
	unsigned bit() {
		if (pos >= n * 8) {
			ok = false;
			return 0;
		}
		const unsigned v = (d[pos >> 3] >> (7 - (pos & 7))) & 1;
		pos++;
		return v;
	}
	unsigned bits(int count) {
		unsigned v = 0;
		while (count-- > 0) {
			v = (v << 1) | bit();
		}
		return v;
	}
	unsigned ue() {
		int zeros = 0;
		while (ok && bit() == 0 && zeros < 32) {
			zeros++;
		}
		return zeros ? ((1u << zeros) - 1 + bits(zeros)) : 0;
	}
};

// Every step of the test is written to the log and to mvdtest.txt on the SD card, so the result can be sent as a file.
void note(const char *fmt, ...) {
	char line[400];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	Logger::log("%s", line);
	mkdir("sdmc:/3ds", 0777);
	mkdir(CONFIG_DIR_PATH, 0777);
	if (FILE *f = fopen(CONFIG_DIR_PATH "/mvdtest.txt", "a")) {
		fprintf(f, "%s\n", line);
		fclose(f);
	}
}

double ticksToMs(u64 t) { return (double)t * 1000.0 / (double)SYSCLOCK_ARM11; }
} // namespace

void MvdTestScreen::open() { ScreenManager::getInstance().pushScreen(ScreenType::MVD_TEST); }

MvdTestScreen::MvdTestScreen() { memset(&texture, 0, sizeof(texture)); }

MvdTestScreen::~MvdTestScreen() {
	stopDecoder();
	if (textureReady) {
		C3D_TexDelete(&texture);
	}
	if (data) {
		linearFree(data);
	}
}

void MvdTestScreen::onEnter() {
	note("[MvdTest] Opened (New 3DS: %d)", Utils::System::isNew3DS() ? 1 : 0);
	if (!Utils::System::isNew3DS()) {
		status = TR("mvdtest.need_new");
		return;
	}
	if (!loadFile()) {
		note("[MvdTest] No usable video file (%s)", path.c_str());
		status = TR("mvdtest.no_file");
		return;
	}
	note("[MvdTest] %s: %zu bytes, %zu NAL units from the first parameter set, %dx%d", path.c_str(), dataLen, nals.size(),
	            width, height);
	status = TR("mvdtest.ready");
}

bool MvdTestScreen::loadFile() {
	// The newest video_in_* file in recordings/ (the names carry the date).
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
	data = (unsigned char *)linearAlloc((size_t)size + 16);
	if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
		fclose(f);
		return false;
	}
	fclose(f);
	dataLen = (size_t)size;

	// Cut the file into NAL units; the decoding starts at the first parameter set (the start of a key frame).
	std::vector<size_t> starts;
	for (size_t i = 0; i + 4 < dataLen; i++) {
		if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) {
			starts.push_back(i);
			i += 3;
		}
	}
	bool seenSps = false;
	for (size_t k = 0; k < starts.size(); k++) {
		const size_t end = k + 1 < starts.size() ? starts[k + 1] : dataLen;
		const int type = data[starts[k] + 4] & 0x1F;
		if (type == 7) {
			seenSps = true;
		}
		if (seenSps) {
			nals.push_back({starts[k], end - starts[k], type});
		}
	}
	for (const Nal &n : nals) {
		if (n.type == 7) {
			return parseSps(data + n.offset + 5, n.size - 5);
		}
	}
	return false;
}

// The coded picture size from the sequence parameter set.
bool MvdTestScreen::parseSps(const unsigned char *nal, size_t len) {
	std::vector<unsigned char> rbsp;
	for (size_t i = 0; i < len; i++) {
		if (i >= 2 && nal[i] == 3 && nal[i - 1] == 0 && nal[i - 2] == 0) {
			continue; // emulation prevention byte
		}
		rbsp.push_back(nal[i]);
	}
	Bits b{rbsp.data(), rbsp.size()};
	const unsigned profile = b.bits(8);
	b.bits(8);
	const unsigned level = b.bits(8);
	b.ue(); // sps id
	if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83 ||
	    profile == 86 || profile == 118 || profile == 128) {
		if (b.ue() == 3) {
			b.bit();
		}
		b.ue();
		b.ue();
		b.bit();
		if (b.bit()) {
			return false; // scaling matrices: not handled by this test
		}
	}
	b.ue(); // log2_max_frame_num
	const unsigned pocType = b.ue();
	if (pocType == 0) {
		b.ue();
	} else if (pocType == 1) {
		return false;
	}
	b.ue(); // max_num_ref_frames
	b.bit();
	const unsigned wMbs = b.ue() + 1;
	const unsigned hMapUnits = b.ue() + 1;
	const unsigned frameMbsOnly = b.bit();
	if (!b.ok) {
		return false;
	}
	width = (int)wMbs * 16;
	height = (int)hMapUnits * 16 * (frameMbsOnly ? 1 : 2);
	note("[MvdTest] SPS: profile %u level %u, coded size %dx%d", profile, level, width, height);
	return width > 0 && height > 0 && width <= 1920 && height <= 1088;
}

bool MvdTestScreen::startDecoder() {
	Result rc = mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264, MVD_OUTPUT_RGB565, MVD_DEFAULT_WORKBUF_SIZE, nullptr);
	note("[MvdTest] mvdstdInit = 0x%08lX", (unsigned long)rc);
	initError = (u32)rc;
	if (R_FAILED(rc)) {
		return false;
	}
	decoderOn = true;
	output = (u16 *)linearAlloc((size_t)width * height * 2);
	if (!output) {
		return false;
	}
	memset(output, 0, (size_t)width * height * 2);
	MVDSTD_Config config;
	mvdstdGenerateDefaultConfig(&config, (u32)width, (u32)height, (u32)width, (u32)height, nullptr, (u32 *)output, nullptr);
	rc = MVDSTD_SetConfig(&config);
	note("[MvdTest] SetConfig = 0x%08lX", (unsigned long)rc);
	initError = (u32)rc;
	textureReady = C3D_TexInit(&texture, TEX_W, TEX_H, GPU_RGB565);
	if (textureReady) {
		C3D_TexSetFilter(&texture, GPU_LINEAR, GPU_LINEAR);
	}
	return R_SUCCEEDED(rc);
}

void MvdTestScreen::stopDecoder() {
	if (decoderOn) {
		mvdstdExit();
		decoderOn = false;
	}
	if (output) {
		linearFree(output);
		output = nullptr;
	}
}

void MvdTestScreen::step() {
	if (next >= nals.size()) {
		running = false;
		status = TR("mvdtest.done");
		return;
	}
	const Nal &n = nals[next++];
	// The decoder wants the three-byte prefix 00 00 01.
	unsigned char *nal = data + n.offset + 1;
	const size_t size = n.size - 1;
	DSP_FlushDataCache(nal, size);

	const u64 t0 = svcGetSystemTick();
	MVDSTD_ProcessNALUnitOut out;
	memset(&out, 0, sizeof(out));
	Result rc = mvdstdProcessVideoFrame(nal, size, 0, &out);
	lastStatus = (u32)rc;
	if (next <= 12 || (rc != MVD_STATUS_OK && rc != MVD_STATUS_FRAMEREADY)) {
		note("[MvdTest] NAL %zu type %d (%zu bytes): 0x%08lX", next - 1, n.type, n.size, (unsigned long)rc);
	}
	if (rc == MVD_STATUS_PARAMSET) {
		paramsDone = true;
	}
	if ((u32)rc == MVD_STATUS_FRAMEREADY || ((n.type == 5 || n.type == 1) && MVD_CHECKNALUPROC_SUCCESS((u32)rc) && paramsDone)) {
		const Result rr = mvdstdRenderVideoFrame(nullptr, true);
		if (R_FAILED(rr) && framesShown < 3) {
			note("[MvdTest] RenderVideoFrame = 0x%08lX", (unsigned long)rr);
		}
		if (R_SUCCEEDED(rr) || (u32)rr == MVD_STATUS_OK) {
			const double ms = ticksToMs(svcGetSystemTick() - t0);
			msTotal += ms;
			if (ms > msMax) {
				msMax = ms;
			}
			framesShown++;
			haveFrame = true;
			if (textureReady) {
				// Shown at half size: 928x528 becomes 464x264 inside the 512x512 texture.
				GSPGPU_FlushDataCache(output, (u32)width * height * 2);
				C3D_SyncDisplayTransfer((u32 *)output, GX_BUFFER_DIM(width, height), (u32 *)texture.data,
				                        GX_BUFFER_DIM(TEX_W, TEX_H),
				                        (GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
				                         GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
				                         GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
				                         GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_XY)));
			}
			if (framesShown <= 2) {
				note("[MvdTest] First frames decoded (%.1f ms)", ms);
			}
		}
	}
	Utils::requestRedraw();
}

void MvdTestScreen::update() {
	const u32 kDown = hidKeysDown();
	if (kDown & KEY_B) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	if (kDown & KEY_A) {
		note("[MvdTest] A pressed (running %d, decoder %d, NALs %zu, width %d)", running ? 1 : 0, decoderOn ? 1 : 0,
		            nals.size(), width);
	}
	if (!running && !decoderOn && (kDown & KEY_A) && !nals.empty() && width > 0) {
		if (startDecoder()) {
			running = true;
			status = TR("mvdtest.running");
		} else {
			// The error code of the step that failed is shown, to tell what the decoder refused.
			char code[96];
			snprintf(code, sizeof(code), "%s (0x%08lX)", TR("mvdtest.failed").c_str(), (unsigned long)initError);
			status = code;
			stopDecoder();
		}
		Utils::requestRedraw();
	}
	if (running) {
		step();
	}
}

void MvdTestScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(target);
	if (haveFrame && textureReady) {
		const float w = (float)width / 2.0f;
		const float h = (float)height / 2.0f;
		Tex3DS_SubTexture sub = {(u16)(width / 2), (u16)(height / 2), 0.0f, 1.0f, w / TEX_W, 1.0f - h / TEX_H};
		C2D_Image img = {&texture, &sub};
		const float scale = std::min(400.0f / w, 240.0f / h);
		C2D_DrawImageAt(img, (400.0f - w * scale) / 2.0f, (240.0f - h * scale) / 2.0f, 0.5f, nullptr, scale, scale);
	} else {
		drawCenteredText(100.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), status, 400.0f);
	}
}

void MvdTestScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);
	drawCenteredText(20.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorAccent(), TR("mvdtest.title"), 320.0f);
	drawCenteredText(48.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), path, 320.0f);
	char line[120];
	snprintf(line, sizeof(line), "%dx%d   NAL %zu/%zu   0x%05X", width, height, next, nals.size(), (unsigned)lastStatus);
	drawCenteredText(80.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), line, 320.0f);
	snprintf(line, sizeof(line), "%s: %d   %.1f ms (max %.1f)", TR("videotest.frames").c_str(), framesShown,
	         framesShown ? msTotal / framesShown : 0.0, msMax);
	drawCenteredText(104.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), line, 320.0f);
	drawCenteredText(150.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorSuccess(),
	                 TR(running || decoderOn ? "mvdtest.running" : "videotest.hint_start"), 320.0f);
	drawCenteredText(180.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("voice_msg.hint_cancel"), 320.0f);
}

} // namespace UI
