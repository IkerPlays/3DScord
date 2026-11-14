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
std::atomic<uint64_t> ledClearAt{0};
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

// The notification LED is register 0x2D of the MCU: four bytes of animation (ticks per frame, transition delay, repeats of the last
// frame, padding) and 32 steps each of red, green and blue. Written as bytes so it builds with every version of libctru.
static Result writeLed(const u8 *animation, const u8 *red, const u8 *green, const u8 *blue) {
	u8 data[100];
	memcpy(data, animation, 4);
	memcpy(data + 4, red, 32);
	memcpy(data + 36, green, 32);
	memcpy(data + 68, blue, 32);
	return MCUHWC_WriteRegister(0x2D, data, sizeof(data));
}

void BackgroundNotifier::clearLed() {
	if (!haveMcu) {
		return;
	}
	const u8 animation[4] = {0x10, 0, 0, 0};
	u8 zero[32];
	memset(zero, 0, sizeof(zero));
	writeLed(animation, zero, zero, zero);
}

void BackgroundNotifier::tick() {
	// With the app in front the LED pulse stops by itself after a few seconds (in the background it goes on until you come back).
	const uint64_t until = ledClearAt.load();
	if (until != 0 && osGetTime() >= until) {
		ledClearAt = 0;
		clearLed();
	}
}

void BackgroundNotifier::notify(const std::string &title, const std::string &body) {
	const bool inBg = background.load();
	const bool wantNews = inBg && Config::getInstance().isBackgroundNotifications();
	const bool wantLed = Config::getInstance().isNotificationLed();
	if (!wantNews && !wantLed) {
		return;
	}
	const uint64_t now = osGetTime();
	if (now - lastNotificationAt.load() < 4000) {
		return; // a chat can send many messages in a row
	}
	lastNotificationAt = now;

	if (wantNews && haveNews) {
		const std::vector<u16> t = toUtf16(title.empty() ? "3DScord" : title, 31);
		const std::vector<u16> m = toUtf16(body, 96);
		const Result rc = NEWS_AddNotification(t.data(), (u32)t.size(), m.empty() ? nullptr : m.data(), (u32)m.size(), nullptr, 0, false);
		Logger::log("[Notify] Home menu notification rc=%08lX", (unsigned long)rc);
	}
	if (wantLed && haveMcu && (inBg || Config::getInstance().isNotificationLed())) {
		// A slow blue-violet pulse, three times.
		const u8 animation[4] = {0x28, 0x28, 0, 0};
		u8 red[32], green[32], blue[32];
		memset(red, 0, sizeof(red));
		memset(green, 0, sizeof(green));
		memset(blue, 0, sizeof(blue));
		for (int i = 0; i < 24; i += 8) {
			for (int k = 0; k < 4; k++) {
				red[i + k] = 0x58;
				green[i + k] = 0x65;
				blue[i + k] = 0xF2;
			}
		}
		const Result rc = writeLed(animation, red, green, blue);
		Logger::log("[Notify] LED rc=%08lX", (unsigned long)rc);
		ledClearAt = inBg ? 0 : osGetTime() + 5000;
	}
}

} // namespace Utils
