#include "ui/video_orient_screen.h"
#include "core/i18n.h"
#include "log.h"
#include "utils/redraw.h"
#include "utils/video_receiver.h"
#include <algorithm>
#include <citro2d.h>
#include <cstdio>
#include <cstring>

namespace UI {

namespace {
constexpr int PW = 320;
constexpr int PH = 192;

struct Rgb {
	uint8_t r, g, b;
};

void fill(std::vector<Rgb> &img, int x0, int y0, int x1, int y1, Rgb c) {
	for (int y = std::max(0, y0); y < std::min(PH, y1); y++) {
		for (int x = std::max(0, x0); x < std::min(PW, x1); x++) {
			img[(size_t)y * PW + x] = c;
		}
	}
}
} // namespace

void VideoOrientScreen::open() { ScreenManager::getInstance().pushScreen(ScreenType::VIDEO_ORIENT); }

VideoOrientScreen::~VideoOrientScreen() {
	if (textureReady) {
		C3D_TexDelete(&texture);
	}
}

void VideoOrientScreen::onEnter() {
	std::vector<Rgb> img((size_t)PW * PH, Rgb{40, 40, 40});
	fill(img, 0, 0, PW, 12, Rgb{255, 0, 0});          // top: red
	fill(img, 0, PH - 12, PW, PH, Rgb{0, 80, 255});   // bottom: blue
	fill(img, 0, 0, 12, PH, Rgb{0, 200, 0});          // left: green
	fill(img, PW - 12, 0, PW, PH, Rgb{255, 230, 0});  // right: yellow
	fill(img, 20, 20, 50, 50, Rgb{255, 130, 0});      // orange square, top left
	fill(img, 130, 40, 160, 152, Rgb{255, 255, 255}); // the F
	fill(img, 130, 40, 230, 68, Rgb{255, 255, 255});
	fill(img, 130, 88, 200, 112, Rgb{255, 255, 255});

	// Planar YUV 4:2:0, like what the decoder gives.
	std::vector<uint8_t> y((size_t)PW * PH), u((size_t)PW * PH / 4), v((size_t)PW * PH / 4);
	for (int row = 0; row < PH; row++) {
		for (int x = 0; x < PW; x++) {
			const Rgb c = img[(size_t)row * PW + x];
			y[(size_t)row * PW + x] = (uint8_t)(((66 * c.r + 129 * c.g + 25 * c.b + 128) >> 8) + 16);
		}
	}
	for (int row = 0; row < PH; row += 2) {
		for (int x = 0; x < PW; x += 2) {
			int su = 0;
			int sv = 0;
			for (int dy = 0; dy < 2; dy++) {
				for (int dx = 0; dx < 2; dx++) {
					const Rgb c = img[(size_t)(row + dy) * PW + x + dx];
					su += ((-38 * c.r - 74 * c.g + 112 * c.b + 128) >> 8) + 128;
					sv += ((112 * c.r - 94 * c.g - 18 * c.b + 128) >> 8) + 128;
				}
			}
			u[(size_t)(row / 2) * (PW / 2) + x / 2] = (uint8_t)std::clamp(su / 4, 0, 255);
			v[(size_t)(row / 2) * (PW / 2) + x / 2] = (uint8_t)std::clamp(sv / 4, 0, 255);
		}
	}
	tiled.assign((size_t)Utils::VIDEO_TEX_W * Utils::VIDEO_TEX_H, 0);
	int w = 0;
	int h = 0;
	Utils::convertToTiledTexture(y.data(), u.data(), v.data(), PW, PH, PW, PW / 2, tiled.data(), w, h);
	memset(&texture, 0, sizeof(texture));
	textureReady = C3D_TexInit(&texture, Utils::VIDEO_TEX_W, Utils::VIDEO_TEX_H, GPU_RGB565);
	if (textureReady) {
		C3D_TexSetFilter(&texture, GPU_LINEAR, GPU_LINEAR);
		memcpy(texture.data, tiled.data(), tiled.size() * 2);
		GSPGPU_FlushDataCache(texture.data, (u32)(tiled.size() * 2));
	}
}

void VideoOrientScreen::update() {
	const u32 kDown = hidKeysDown();
	if (kDown & KEY_B) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	if (kDown & (KEY_A | KEY_RIGHT)) {
		option = (option + 1) % 8;
		Logger::log("[VideoOrient] Option %d (turns %d, mirror %d)", option, option & 3, option >> 2);
		Utils::requestRedraw();
	}
	if (kDown & KEY_LEFT) {
		option = (option + 7) % 8;
		Utils::requestRedraw();
	}
}

void VideoOrientScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(target);
	if (textureReady) {
		Utils::drawVideoTextureOriented(&texture, PW, PH, 0.0f, 0.0f, 0.5f, 400.0f, 240.0f, option & 3, (option >> 2) != 0);
	}
}

void VideoOrientScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);
	drawCenteredText(16.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorAccent(), TR("videoorient.title"), 320.0f);
	char line[96];
	snprintf(line, sizeof(line), "%s %d / 8   (%d x 90, %s)", TR("videoorient.option").c_str(), option + 1, option & 3,
	         TR((option >> 2) ? "videoorient.mirror" : "videoorient.nomirror").c_str());
	drawCenteredText(52.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), line, 320.0f);
	drawCenteredText(90.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), TR("videoorient.help1"), 300.0f);
	drawCenteredText(116.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), TR("videoorient.help2"), 300.0f);
	drawCenteredText(160.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorSuccess(), TR("videoorient.hint"), 320.0f);
}

} // namespace UI
