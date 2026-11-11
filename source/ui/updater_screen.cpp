#include "ui/updater_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "ui/image_manager.h"
#include "ui/screen_manager.h"
#include "utils/updater.h"
#include <3ds.h>
#include <cmath>

namespace UI {

using Utils::Updater;

bool UpdaterScreen::manualCheck = false;

UpdaterScreen::UpdaterScreen() : angle(0.0f), installStarted(false), finished(false), graceFrames(0) {}

UpdaterScreen::~UpdaterScreen() {
	// A finished or abandoned worker must not outlive the screen that reads its state.
	Updater::getInstance().cancel();
	Updater::getInstance().join();
}

void UpdaterScreen::onEnter() {
	Logger::log("[UpdaterScreen] Checking for updates (current %s)", Updater::currentVersionString().c_str());
	Updater::getInstance().startCheck(Config::getInstance().isDevBuildUpdatesEnabled());
}

void UpdaterScreen::update() {
	angle += 6.0f;
	if (angle >= 360.0f) {
		angle -= 360.0f;
	}

	Updater &updater = Updater::getInstance();
	const u32 kDown = hidKeysDown();

	switch (updater.getState()) {
	case Updater::State::CHECKING:
		if (kDown & KEY_B) {
			updater.cancel();
			ScreenManager::getInstance().enterMainScreen();
		}
		break;
	case Updater::State::UP_TO_DATE:
		if (manualCheck) {
			manualCheck = false;
			ScreenManager::getInstance().showToast(TR("updater.up_to_date"));
		}
		ScreenManager::getInstance().enterMainScreen();
		break;
	case Updater::State::AVAILABLE:
		if (!installStarted) {
			installStarted = true;
			updater.startInstall();
		}
		break;
	case Updater::State::DOWNLOADING:
		if (kDown & KEY_B) {
			updater.cancel();
		}
		break;
	case Updater::State::RESTART:
		// Let the user see "done" for a moment, then close: the system relaunches the app (chainload to self).
		if (++graceFrames > 60 && !finished) {
			finished = true;
			updater.join();
			aptSetChainloaderToSelf();
			ScreenManager::getInstance().requestAppExit();
		}
		break;
	case Updater::State::EXIT_MANUAL:
		if (kDown & (KEY_A | KEY_B)) {
			updater.join();
			ScreenManager::getInstance().requestAppExit();
		}
		break;
	case Updater::State::FAILED:
		if (!installStarted) {
			// The check itself failed (offline, rate limit...): never block the app for that.
			if (manualCheck) {
				manualCheck = false;
				ScreenManager::getInstance().showToast(updater.getError());
			}
			ScreenManager::getInstance().enterMainScreen();
		} else if (kDown & (KEY_A | KEY_B)) {
			ScreenManager::getInstance().enterMainScreen();
		}
		break;
	default:
		break;
	}
}

void UpdaterScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);

	Updater &updater = Updater::getInstance();
	const float centerX = TOP_SCREEN_WIDTH / 2.0f;
	const float centerY = TOP_SCREEN_HEIGHT / 2.0f - 10.0f;

	C3D_Tex *tex = ImageManager::getInstance().getLocalImage("romfs:/discord.png", true);
	if (tex) {
		ImageManager::ImageInfo info = ImageManager::getInstance().getImageInfo("romfs:/discord.png");
		Tex3DS_SubTexture subtex;
		subtex.width = (u16)info.originalW;
		subtex.height = (u16)info.originalH;
		subtex.left = 0.0f;
		subtex.top = 0.0f;
		subtex.right = (float)info.originalW / tex->width;
		subtex.bottom = (float)info.originalH / tex->height;
		C2D_Image img = {tex, &subtex};
		float scale = 1.0f;
		if (info.originalW > 0 && info.originalH > 0) {
			const float maxSize = 90.0f;
			scale = std::fmin(maxSize / (float)info.originalW, maxSize / (float)info.originalH);
		}
		const bool spinning = updater.getState() == Updater::State::CHECKING;
		const float rad = spinning ? (angle - 90.0f) * M_PI / 180.0f : -M_PI / 2.0f;
		C2D_DrawImageAtRotated(img, centerX, centerY, 0.6f, rad, nullptr, scale, scale);
	}

	std::string line;
	switch (updater.getState()) {
	case Updater::State::CHECKING:
		line = TR("updater.checking");
		break;
	case Updater::State::AVAILABLE:
	case Updater::State::DOWNLOADING:
		line = Core::I18n::format(TR("updater.downloading"), updater.getRelease().tag);
		break;
	case Updater::State::INSTALLING:
		line = Core::I18n::format(TR("updater.installing"), updater.getRelease().tag);
		break;
	case Updater::State::RESTART:
		line = TR("updater.restarting");
		break;
	case Updater::State::EXIT_MANUAL:
		line = TR("updater.done_manual");
		break;
	case Updater::State::FAILED:
		line = updater.getError();
		break;
	default:
		break;
	}
	drawCenteredText(centerY + 60.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), line, TOP_SCREEN_WIDTH);

	const auto state = updater.getState();
	if (state == Updater::State::DOWNLOADING || state == Updater::State::INSTALLING) {
		const float w = 200.0f;
		const float h = 8.0f;
		const float x = centerX - w / 2.0f;
		const float y = centerY + 85.0f;
		C2D_DrawRectSolid(x, y, 0.5f, w, h, ScreenManager::colorSeparator());
		const float p = state == Updater::State::INSTALLING ? 1.0f : updater.getProgress();
		C2D_DrawRectSolid(x, y, 0.55f, w * p, h, ScreenManager::colorAccent());
	}
}

void UpdaterScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackground());
	C2D_SceneBegin(target);

	Updater &updater = Updater::getInstance();
	drawCenteredText(20.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorText(),
	                 std::string("3DScord ") + Updater::currentVersionString(), BOTTOM_SCREEN_WIDTH);

	const auto state = updater.getState();
	if (state == Updater::State::CHECKING) {
		drawCenteredText(200.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("updater.skip_hint"),
		                 BOTTOM_SCREEN_WIDTH);
	} else if (state == Updater::State::DOWNLOADING) {
		drawCenteredText(200.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("updater.cancel_hint"),
		                 BOTTOM_SCREEN_WIDTH);
	} else if (state == Updater::State::EXIT_MANUAL || state == Updater::State::FAILED) {
		drawCenteredText(200.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("updater.continue_hint"),
		                 BOTTOM_SCREEN_WIDTH);
	}
}

} // namespace UI
