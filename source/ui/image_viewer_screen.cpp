#include "ui/image_viewer_screen.h"
#include "utils/transfers.h"
#include "core/config.h"
#include "core/i18n.h"
#include "ui/image_manager.h"
#include "ui/screen_manager.h"
#include <3ds.h>
#include <algorithm>
#include <cmath>

namespace UI {

std::string ImageViewerScreen::pendingUrl;
std::string ImageViewerScreen::pendingName;

namespace {
constexpr float MIN_ZOOM = 0.5f;
constexpr float MAX_ZOOM = 6.0f;
} // namespace

void ImageViewerScreen::open(const std::string &url, const std::string &filename) {
	pendingUrl = url;
	pendingName = filename;
	ScreenManager::getInstance().pushScreen(ScreenType::IMAGE_VIEWER);
}

ImageViewerScreen::ImageViewerScreen()
    : url(pendingUrl), filename(pendingName), zoom(1.0f), panX(0.0f), panY(0.0f), dragging(false), lastTouchX(0),
      lastTouchY(0) {
	ImageManager::getInstance().prefetch(url, 0, 0, Network::RequestPriority::INTERACTIVE);
}

void ImageViewerScreen::update() {
	const u32 kDown = hidKeysDown();
	const u32 kHeld = hidKeysHeld();

	if (kDown & KEY_B) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	if (kDown & KEY_Y) {
		// Saves the picture (a GIF too) to the downloads folder on the SD card.
		Utils::Transfers::getInstance().saveAttachment(url, filename.empty() ? "image" : filename, 0);
		ScreenManager::getInstance().showToast(TR("viewer.saving"));
	}
	if (kDown & KEY_X) {
		turns = (turns + 1) % 4;
		panX = 0.0f;
		panY = 0.0f;
	}
	if (kDown & KEY_A) {
		zoom = 1.0f;
		panX = 0.0f;
		panY = 0.0f;
	}

	if (kHeld & KEY_R) {
		zoom = std::min(MAX_ZOOM, zoom * 1.03f);
	}
	if (kHeld & KEY_L) {
		zoom = std::max(MIN_ZOOM, zoom * 0.97f);
	}

	// Panning moves faster the more the image is magnified.
	const float step = 3.0f * std::max(1.0f, zoom);
	if (kHeld & KEY_LEFT) {
		panX += step;
	}
	if (kHeld & KEY_RIGHT) {
		panX -= step;
	}
	if (kHeld & KEY_UP) {
		panY += step;
	}
	if (kHeld & KEY_DOWN) {
		panY -= step;
	}

	circlePosition pad;
	hidCircleRead(&pad);
	if (std::abs(pad.dx) > 15) {
		panX -= (float)pad.dx / 30.0f * std::max(1.0f, zoom);
	}
	if (std::abs(pad.dy) > 15) {
		panY += (float)pad.dy / 30.0f * std::max(1.0f, zoom);
	}

	// Dragging on the touch screen moves the image; the top and bottom edges of the bar zoom.
	touchPosition touch;
	hidTouchRead(&touch);
	if (kDown & KEY_TOUCH) {
		dragging = true;
		lastTouchX = touch.px;
		lastTouchY = touch.py;
	} else if ((kHeld & KEY_TOUCH) && dragging) {
		panX += (float)(touch.px - lastTouchX) * 1.5f;
		panY += (float)(touch.py - lastTouchY) * 1.5f;
		lastTouchX = touch.px;
		lastTouchY = touch.py;
	} else {
		dragging = false;
	}
}

void ImageViewerScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(target);

	const auto info = ImageManager::getInstance().getImageInfo(url);
	if (!info.tex) {
		const std::string text = info.failed ? TR("viewer.failed") : TR("common.loading");
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, info.failed ? ScreenManager::colorError() : C2D_Color32(220, 220, 220, 255),
		                 text, TOP_SCREEN_WIDTH);
		if (!info.failed) {
			ImageManager::getInstance().prefetch(url, 0, 0, Network::RequestPriority::INTERACTIVE);
		}
		return;
	}

	const float uMax = (float)info.originalW / info.tex->width;
	const float vMax = (float)info.originalH / info.tex->height;
	Tex3DS_SubTexture subtex = {(u16)info.originalW, (u16)info.originalH, 0.0f, 1.0f, uMax, 1.0f - vMax};
	const C2D_Image img = {info.tex, &subtex};

	// A picture turned a quarter occupies the screen with its sides swapped.
	const bool sideways = (turns % 2) == 1;
	const float shownW = sideways ? info.originalH : info.originalW;
	const float shownH = sideways ? info.originalW : info.originalH;
	const float fit = std::min((float)TOP_SCREEN_WIDTH / shownW, (float)TOP_SCREEN_HEIGHT / shownH);
	const float scale = fit * zoom;
	const float w = shownW * scale;
	const float h = shownH * scale;

	// Keep some of the image on screen whatever the zoom.
	const float limitX = std::max(0.0f, (w + TOP_SCREEN_WIDTH) / 2.0f - 40.0f);
	const float limitY = std::max(0.0f, (h + TOP_SCREEN_HEIGHT) / 2.0f - 40.0f);
	panX = std::clamp(panX, -limitX, limitX);
	panY = std::clamp(panY, -limitY, limitY);

	if (turns == 0) {
		const float x = (TOP_SCREEN_WIDTH - w) / 2.0f + panX;
		const float y = (TOP_SCREEN_HEIGHT - h) / 2.0f + panY;
		C2D_DrawImageAt(img, x, y, 0.5f, nullptr, scale, scale);
	} else {
		// Turned about its own centre, which sits where the middle of the picture should be.
		const float drawW = info.originalW * scale;
		const float drawH = info.originalH * scale;
		C2D_DrawParams params;
		params.pos = {TOP_SCREEN_WIDTH / 2.0f + panX, TOP_SCREEN_HEIGHT / 2.0f + panY, drawW, drawH};
		params.center = {drawW / 2.0f, drawH / 2.0f};
		params.depth = 0.5f;
		params.angle = turns * 1.57079632679f;
		C2D_DrawImage(img, &params, nullptr);
	}
	ImageManager::getInstance().markDrawn(info.animation);
}

void ImageViewerScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);

	drawCenteredText(20.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), filename, BOTTOM_SCREEN_WIDTH);
	drawCenteredText(48.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(),
	                 std::to_string((int)(zoom * 100.0f)) + "%", BOTTOM_SCREEN_WIDTH);
	drawCenteredText(150.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), TR("viewer.hint_move"),
	                 BOTTOM_SCREEN_WIDTH);
	drawCenteredText(168.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), TR("viewer.hint_zoom"),
	                 BOTTOM_SCREEN_WIDTH);
	drawCenteredText(186.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), TR("viewer.hint_fit"),
	                 BOTTOM_SCREEN_WIDTH);
}

} // namespace UI
