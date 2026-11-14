#include "utils/background_notifier.h"
#include "core/config.h"
#include "log.h"
#include <3ds.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace Utils {

namespace {
std::atomic<bool> background{false};
std::atomic<bool> haveNews{false};
std::atomic<bool> haveMcu{false};
std::atomic<uint64_t> lastNotificationAt{0};
aptHookCookie hookCookie;
bool hookInstalled = false;

void onApt(APT_HookType hook, void *) {
	switch (hook) {
	case APTHOOK_ONSUSPEND:
	case APTHOOK_ONSLEEP:
		background = true;
		break;
	case APTHOOK_ONRESTORE:
	case APTHOOK_ONWAKEUP:
		background = false;
		BackgroundNotifier::getInstance().clearLed();
		break;
	default:
		break;
	}
}

// UTF-8 to UTF-16 (the system wants 16-bit characters), cut to `maxChars`.
std::vector<u16> toUtf16(const std::string &text, size_t maxChars) {
	std::vector<u16> out(maxChars + 1, 0);
	const ssize_t n = utf8_to_utf16(out.data(), (const uint8_t *)text.c_str(), maxChars);
	if (n < 0) {
		out.assign(maxChars + 1, 0);
		return out;
	}
	out.resize((size_t)n);
	return out;
}
} // namespace

BackgroundNotifier &BackgroundNotifier::getInstance() {
	static BackgroundNotifier instance;
	return instance;
}

void BackgroundNotifier::init() {
	if (!hookInstalled) {
		aptHook(&hookCookie, onApt, nullptr);
		hookInstalled = true;
	}
	haveNews = R_SUCCEEDED(newsInit());
	haveMcu = R_SUCCEEDED(mcuHwcInit());
	Logger::log("[Notify] Home menu notifications %s, notification LED %s", haveNews ? "available" : "not available",
	            haveMcu ? "available" : "not available");
}

void BackgroundNotifier::shutdown() {
	if (haveMcu) {
		clearLed();
		mcuHwcExit();
	}
	if (haveNews) {
		newsExit();
	}
	haveMcu = false;
	haveNews = false;
	if (hookInstalled) {
		aptUnhook(&hookCookie);
		hookInstalled = false;
	}
}

bool BackgroundNotifier::inBackground() const { return background.load(); }

void BackgroundNotifier::clearLed() {
	if (!haveMcu) {
		return;
	}
	MCU_InfoLedPattern pattern;
	memset(&pattern, 0, sizeof(pattern));
	pattern.animation.ticksPerFrame = 0x10;
	pattern.animation.frameTransitionDelay = 0;
	pattern.animation.numLastFrameRepeats = 0;
	MCUHWC_SetInfoLedPattern(&pattern);
}

void BackgroundNotifier::notify(const std::string &title, const std::string &body) {
	if (!Config::getInstance().isBackgroundNotifications() || !background.load()) {
		return;
	}
	const uint64_t now = osGetTime();
	if (now - lastNotificationAt.load() < 4000) {
		return; // a chat can send many messages in a row
	}
	lastNotificationAt = now;

	if (haveNews) {
		const std::vector<u16> t = toUtf16(title.empty() ? "3DScord" : title, 31);
		const std::vector<u16> m = toUtf16(body, 96);
		const Result rc = NEWS_AddNotification(t.data(), (u32)t.size(), m.empty() ? nullptr : m.data(), (u32)m.size(), nullptr, 0, false);
		Logger::log("[Notify] Home menu notification rc=%08lX", (unsigned long)rc);
	}
	if (haveMcu) {
		// A slow blue-violet pulse, three times.
		MCU_InfoLedPattern pattern;
		memset(&pattern, 0, sizeof(pattern));
		pattern.animation.ticksPerFrame = 0x28;
		pattern.animation.frameTransitionDelay = 0x28;
		pattern.animation.numLastFrameRepeats = 0;
		for (int i = 0; i < 24; i += 8) {
			for (int k = 0; k < 4; k++) {
				pattern.redPattern[i + k] = 0x58;
				pattern.greenPattern[i + k] = 0x65;
				pattern.bluePattern[i + k] = 0xF2;
			}
		}
		const Result rc = MCUHWC_SetInfoLedPattern(&pattern);
		Logger::log("[Notify] LED rc=%08lX", (unsigned long)rc);
	}
}

} // namespace Utils
