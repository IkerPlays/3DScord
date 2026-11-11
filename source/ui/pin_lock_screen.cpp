#include "ui/pin_lock_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "ui/screen_manager.h"
#include "utils/redraw.h"
#include <3ds.h>
#include <vector>

namespace UI {

bool PinLockScreen::atStartup = false;

namespace {
constexpr int MAX_TRIES = 5;
constexpr u64 LOCKOUT_MS = 30 * 1000;
} // namespace

bool promptPin(const std::string &hint, std::string &out) {
	SwkbdState swkbd;
	swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, 8);
	swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE);
	swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	swkbdSetFeatures(&swkbd, SWKBD_DARKEN_TOP_SCREEN);
	swkbdSetHintText(&swkbd, hint.c_str());
	char buffer[32] = {0};
	if (swkbdInputText(&swkbd, buffer, sizeof(buffer)) != SWKBD_BUTTON_CONFIRM) {
		return false;
	}
	out = buffer;
	return true;
}

PinLockScreen::PinLockScreen() : asked(false), wrongTries(0), lockedUntil(0) {}

void PinLockScreen::ask() {
	std::string pin;
	if (!promptPin(TR("pin.enter"), pin)) {
		return;
	}
	auto &sm = ScreenManager::getInstance();
	if (Config::getInstance().checkPin(pin)) {
		Logger::log("[PIN] Unlocked");
		if (atStartup) {
			atStartup = false;
			sm.startFlow();
		} else {
			sm.returnToPreviousScreen();
		}
		return;
	}
	wrongTries++;
	message = TR("pin.wrong");
	if (wrongTries >= MAX_TRIES) {
		wrongTries = 0;
		lockedUntil = osGetTime() + LOCKOUT_MS;
	}
	Utils::requestRedraw();
}

void PinLockScreen::update() {
	const u32 kDown = hidKeysDown();
	const bool lockedOut = osGetTime() < lockedUntil;
	if (lockedOut) {
		Utils::requestRedraw();
		return;
	}
	// The keypad opens by itself the first time; afterwards A opens it again.
	if (!asked || (kDown & KEY_A)) {
		asked = true;
		ask();
	}
}

void PinLockScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);
	drawCenteredText(70.0f, 0.5f, 0.8f, 0.8f, ScreenManager::colorText(), "3DScord", TOP_SCREEN_WIDTH);
	drawCenteredText(110.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorTextMuted(), TR("pin.locked"), TOP_SCREEN_WIDTH);
	if (!message.empty()) {
		drawCenteredText(150.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorError(), message, TOP_SCREEN_WIDTH);
	}
}

void PinLockScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackground());
	C2D_SceneBegin(target);
	const u64 now = osGetTime();
	if (now < lockedUntil) {
		drawCenteredText(100.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorError(),
		                 Core::I18n::format(TR("pin.wait"), std::to_string((lockedUntil - now) / 1000 + 1)),
		                 BOTTOM_SCREEN_WIDTH);
		return;
	}
	drawCenteredText(100.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorSuccess(), TR("pin.hint_enter"),
	                 BOTTOM_SCREEN_WIDTH);
	drawCenteredText(130.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("pin.hint_exit"),
	                 BOTTOM_SCREEN_WIDTH);
}

} // namespace UI
