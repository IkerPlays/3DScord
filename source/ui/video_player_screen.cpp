#include "ui/video_player_screen.h"
#include "core/i18n.h"
#include "log.h"
#include "utils/redraw.h"
#include "utils/soft_decoder.h"
#include "utils/video_receiver.h"
#include <algorithm>
#include <citro2d.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace UI {

namespace {

constexpr int TEX_W = 512;
constexpr int TEX_H = 512;
constexpr size_t MAX_FILE = 16 * 1024 * 1024;
std::string pendingPath;

inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

std::string clockText(uint32_t ms) {
	char buf[16];
	const uint32_t s = ms / 1000;
	snprintf(buf, sizeof(buf), "%u:%02u", s / 60, s % 60);
	return buf;
}

} // namespace

void VideoPlayerScreen::open(const std::string &path) {
	pendingPath = path;
	ScreenManager::getInstance().setScreen(ScreenType::VIDEO_PLAYER);
}

VideoPlayerScreen::VideoPlayerScreen() : path(pendingPath) {
	memset(&texture, 0, sizeof(texture));
	LightLock_Init(&lock);
}

VideoPlayerScreen::~VideoPlayerScreen() {
	quit = true;
	worker.join();
	if (textureReady) {
		C3D_TexDelete(&texture);
	}
	if (front) {
		linearFree(front);
	}
	if (ready) {
		linearFree(ready);
	}
	remove(path.c_str());
}

void VideoPlayerScreen::onEnter() {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) {
		status = TR("player.failed");
		return;
	}
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0 || (size_t)size > MAX_FILE) {
		fclose(f);
		status = TR("player.too_big");
		return;
	}
	file.resize((size_t)size);
	const bool read = fread(file.data(), 1, file.size(), f) == file.size();
	fclose(f);
	if (!read) {
		status = TR("player.failed");
		return;
	}
	std::string error;
	if (!video.parse(file.data(), file.size(), error)) {
		Logger::log("[Player] Cannot play %s: %s", path.c_str(), error.c_str());
		status = TR("player.unsupported");
		return;
	}
	if (video.width * video.height > 1280 * 720) {
		Logger::log("[Player] Picture too big: %dx%d", video.width, video.height);
		status = TR("player.too_big");
		return;
	}
	Logger::log("[Player] %dx%d, %zu frames, %u ms", video.width, video.height, video.samples.size(), video.durationMs);
	status = TR("player.loading");
	playing = true;
	worker.start([this] { playLoop(); }, 3, 96 * 1024, true);
}

void VideoPlayerScreen::playLoop() {
	Utils::SoftDecoder decoder;
	if (!decoder.init()) {
		workerFailed = true;
		playing = false;
		return;
	}
	std::vector<uint8_t> annex;
	annex.reserve(256 * 1024);
	const auto &samples = video.samples;
	const size_t n = samples.size();
	size_t i = 0;
	u64 base = osGetTime();
	u64 pausedAt = 0;

	auto toKeyAtOrBefore = [&](uint32_t ms) {
		size_t k = 0;
		for (size_t j = 0; j < n && samples[j].ms <= ms; j++) {
			if (samples[j].key) {
				k = j;
			}
		}
		return k;
	};

	while (!quit) {
		if (paused) {
			if (!pausedAt) {
				pausedAt = osGetTime();
			}
			svcSleepThread(30 * 1000000LL);
			continue;
		}
		if (pausedAt) {
			base += osGetTime() - pausedAt;
			pausedAt = 0;
		}
		if (restart.exchange(false)) {
			i = 0;
			base = osGetTime();
			ended = false;
		}
		const int jump = seekMs.exchange(0);
		if (jump != 0 && n > 0) {
			const int64_t target = std::clamp<int64_t>((int64_t)positionMs.load() + jump, 0, (int64_t)video.durationMs);
			i = toKeyAtOrBefore((uint32_t)target);
			base = osGetTime() - samples[i].ms;
			ended = false;
		}
		if (i >= n) {
			ended = true;
			svcSleepThread(50 * 1000000LL);
			continue;
		}

		const u64 now = osGetTime();
		const u64 due = base + samples[i].ms;
		if (now < due) {
			svcSleepThread(std::min<u64>(due - now, 20) * 1000000LL);
			continue;
		}
		const u64 late = now - due;
		if (late > 1500 && !samples[i].key) {
			// Too far behind: jump to the next key frame and go on from there.
			size_t j = i + 1;
			while (j < n && !samples[j].key) {
				j++;
			}
			skipped += (uint32_t)(j - i);
			if (j >= n) {
				i = n;
			} else {
				i = j;
				base = now - samples[j].ms;
			}
			continue;
		}

		if (video.toAnnexB(file.data(), file.size(), samples[i], annex)) {
			Utils::SoftDecoder::Picture pic;
			bool got = false;
			decoder.decode(annex.data(), annex.size(), pic, got);
			if (got) {
				if (late > 400) {
					skipped++; // decoded to keep the references, but not drawn
				} else {
					convert(pic.y, pic.u, pic.v, pic.width, pic.height, pic.strideY, pic.strideUV);
					shown++;
				}
			}
		}
		positionMs = samples[i].ms;
		i++;
	}
	playing = false;
}

// Half size (a quarter when the picture is very big), YUV to RGB565.
void VideoPlayerScreen::convert(const uint8_t *py0, const uint8_t *pu0, const uint8_t *pv0, int width, int height,
                                int strideY, int strideUV) {
	const int div = width > 1024 ? 4 : 2;
	const int w = (width / div) & ~7;
	const int h = (height / div) & ~7;
	if (w <= 0 || h <= 0 || w > TEX_W || h > TEX_H) {
		return;
	}
	if (!front || readyW != w || readyH != h) {
		LightLock_Lock(&lock);
		if (front) {
			linearFree(front);
		}
		if (ready) {
			linearFree(ready);
		}
		// Rows as wide as the texture, or the display transfer shears the picture.
		front = (u16 *)linearAlloc((size_t)TEX_W * TEX_H * 2);
		ready = (u16 *)linearAlloc((size_t)TEX_W * TEX_H * 2);
		if (front) {
			memset(front, 0, (size_t)TEX_W * TEX_H * 2);
		}
		if (ready) {
			memset(ready, 0, (size_t)TEX_W * TEX_H * 2);
		}
		readyW = w;
		readyH = h;
		LightLock_Unlock(&lock);
		if (!front || !ready) {
			return;
		}
	}
	for (int y = 0; y < h; y++) {
		const uint8_t *py = py0 + (size_t)(y * div) * strideY;
		const uint8_t *pu = pu0 + (size_t)(y * div / 2) * strideUV;
		const uint8_t *pv = pv0 + (size_t)(y * div / 2) * strideUV;
		u16 *dst = front + (size_t)y * TEX_W;
		for (int x = 0; x < w; x++) {
			const int c = ((py[x * div] + py[x * div + 1] + py[x * div + strideY] + py[x * div + 1 + strideY]) >> 2) - 16; // 2x2 average, no stripes
			const int d = pu[x * div / 2] - 128;
			const int e = pv[x * div / 2] - 128;
			const int r = clamp255((298 * c + 409 * e + 128) >> 8);
			const int g = clamp255((298 * c - 100 * d - 208 * e + 128) >> 8);
			const int b = clamp255((298 * c + 516 * d + 128) >> 8);
			dst[x] = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
		}
	}
	LightLock_Lock(&lock);
	std::swap(front, ready);
	version++;
	LightLock_Unlock(&lock);
	Utils::requestRedraw();
}

void VideoPlayerScreen::update() {
	const u32 kDown = hidKeysDown();
	if (kDown & KEY_B) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	if (kDown & KEY_A) {
		if (ended) {
			restart = true;
		} else {
			paused = !paused.load();
		}
	}
	if (kDown & KEY_LEFT) {
		seekMs = -5000;
	}
	if (kDown & KEY_RIGHT) {
		seekMs = 5000;
	}

	if (workerFailed) {
		status = TR("player.failed");
	}
	LightLock_Lock(&lock);
	if (version != shownVersion && ready) {
		if (!textureReady) {
			textureReady = C3D_TexInit(&texture, TEX_W, TEX_H, GPU_RGB565);
			if (textureReady) {
				C3D_TexSetFilter(&texture, GPU_LINEAR, GPU_LINEAR);
			}
		}
		if (textureReady) {
			picW = readyW;
			picH = readyH;
			GSPGPU_FlushDataCache(ready, (u32)TEX_W * TEX_H * 2);
			C3D_SyncDisplayTransfer((u32 *)ready, GX_BUFFER_DIM(TEX_W, TEX_H), (u32 *)texture.data,
			                        GX_BUFFER_DIM(TEX_W, TEX_H),
			                        (GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
			                         GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
			                         GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
			                         GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO)));
			havePicture = true;
		}
		shownVersion = version;
	}
	LightLock_Unlock(&lock);
	if (playing || ended) {
		Utils::requestRedraw();
	}
}

void VideoPlayerScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(target);
	if (havePicture && textureReady) {
		Utils::drawVideoTexture(&texture, picW, picH, 0.0f, 0.0f, 0.5f, 400.0f, 240.0f);
	} else {
		drawCenteredText(100.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), status, 400.0f);
	}
}

void VideoPlayerScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);
	drawCenteredText(20.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorAccent(), TR("player.title"), 320.0f);
	const size_t slash = path.rfind('/');
	drawCenteredText(48.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(),
	                 slash == std::string::npos ? path : path.substr(slash + 1), 320.0f);
	if (video.durationMs > 0) {
		const float frac = std::min(1.0f, (float)positionMs.load() / (float)video.durationMs);
		drawRoundedRect(30.0f, 90.0f, 0.5f, 260.0f, 8.0f, 4.0f, ScreenManager::colorBackgroundLight());
		drawRoundedRect(30.0f, 90.0f, 0.51f, std::max(8.0f, 260.0f * frac), 8.0f, 4.0f, ScreenManager::colorSelection());
		drawCenteredText(106.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(),
		                 clockText(positionMs.load()) + " / " + clockText(video.durationMs), 320.0f);
		char line[80];
		snprintf(line, sizeof(line), "%u %s, %u %s", shown.load(), TR("player.shown").c_str(), skipped.load(),
		         TR("player.skipped").c_str());
		drawCenteredText(130.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), line, 320.0f);
	}
	drawCenteredText(160.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), TR("player.hint"), 320.0f);
	drawCenteredText(180.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), TR("player.no_sound"), 320.0f);
}

} // namespace UI
