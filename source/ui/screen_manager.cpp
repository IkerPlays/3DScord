#include "ui/screen_manager.h"
#include "core/config.h"
#include "core/i18n.h"
#include "discord/avatar_cache.h"
#include "discord/voice_client.h"
#include "discord/discord_client.h"
#include "log.h"
#include "ui/about_screen.h"
#include "ui/disclaimer_screen.h"
#include "ui/emoji_manager.h"
#include "ui/forum_screen.h"
#include "ui/file_browser_screen.h"
#include "ui/file_preview_screen.h"
#include "ui/image_manager.h"
#include "ui/image_viewer_screen.h"
#include "ui/list_screen.h"
#include "ui/login_screen.h"
#include "ui/message_screen.h"
#include "ui/pin_lock_screen.h"
#include "ui/profile_screen.h"
#include "ui/server_list_screen.h"
#include "ui/settings_screen.h"
#include "ui/text_measure_cache.h"
#include "ui/theme_manager_screen.h"
#include "ui/updater_screen.h"
#include "ui/voice_recorder_screen.h"
#include "utils/redraw.h"
#include "ui/call_view.h"
#include "ui/camera_screen.h"
#include "ui/video_test_screen.h"
#include "ui/mvd_test_screen.h"
#include "ui/soft_test_screen.h"
#include "ui/video_player_screen.h"
#include "ui/video_orient_screen.h"
#include "utils/voice_player.h"
#include "utils/call_recorder.h"
#include "utils/screenshot.h"
#include "utils/transfers.h"
#include "utils/utf8_utils.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>

namespace UI {

static C2D_TextBuf textBuf = nullptr;
static C2D_TextBuf debugTextBuf = nullptr;
static C2D_TextBuf layoutTextBuf = nullptr;

static constexpr CFG_Region FALLBACK_REGIONS[] = {CFG_REGION_CHN, CFG_REGION_TWN, CFG_REGION_KOR, CFG_REGION_JPN};
static constexpr int NUM_FALLBACK_FONTS = sizeof(FALLBACK_REGIONS) / sizeof(FALLBACK_REGIONS[0]);

// Visually tuned on hardware
static constexpr float FALLBACK_DESIGN_SCALE = 0.90f;
static constexpr float FALLBACK_BASELINE_TWEAK = -2.4f;

static C2D_Font fallbackFonts[NUM_FALLBACK_FONTS] = {};
static bool fallbackFontTried[NUM_FALLBACK_FONTS] = {};
static int systemFontSlot = -1;
static std::unordered_map<uint32_t, C2D_Font> glyphFontCache;

static bool fontHasGlyph(C2D_Font font, uint32_t cp) {
	return C2D_FontGlyphIndexFromCodePoint(font, cp) != (int)C2D_FontGetInfo(font)->alterCharIndex;
}

static bool isHangul(uint32_t cp) {
	return (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F) || (cp >= 0xAC00 && cp <= 0xD7AF);
}

static int querySystemFontSlot() {
	u8 region = CFG_REGION_JPN;
	if (R_SUCCEEDED(cfguInit())) {
		CFGU_SecureInfoGetRegion(&region);
		cfguExit();
	}
	switch (region) {
	case CFG_REGION_CHN:
		return 0;
	case CFG_REGION_TWN:
		return 1;
	case CFG_REGION_KOR:
		return 2;
	default:
		return 3;
	}
}

static float fallbackFontScale(C2D_Font font) { return font ? FALLBACK_DESIGN_SCALE : 1.0f; }

static float fallbackBaselineOffset(C2D_Font font, float scaleY, float scaleAdj) {
	if (!font) {
		return 0.0f;
	}
	TGLP_s *sysTglp = C2D_FontGetInfo(nullptr)->tglp;
	TGLP_s *fbTglp = C2D_FontGetInfo(font)->tglp;
	if (!sysTglp || !fbTglp) {
		return 0.0f;
	}
	return scaleY * ((float)sysTglp->baselinePos - scaleAdj * (float)fbTglp->baselinePos + FALLBACK_BASELINE_TWEAK);
}

static void loadFallbackFont(int slot) {
	fallbackFontTried[slot] = true;
	// C2D_FontLoadSystem requires cfgu to be initialized
	if (R_SUCCEEDED(cfguInit())) {
		fallbackFonts[slot] = C2D_FontLoadSystem(FALLBACK_REGIONS[slot]);
		cfguExit();
	}
	Logger::log("[UI] Fallback font region %d: %s", (int)FALLBACK_REGIONS[slot],
	            fallbackFonts[slot] ? "loaded" : "unavailable");
}

static C2D_Font glyphFallbackFont(uint32_t cp) {
	if (cp < 0x1100 || cp > 0xFAFF) {
		return nullptr;
	}

	auto cached = glyphFontCache.find(cp);
	if (cached != glyphFontCache.end()) {
		return cached->second;
	}

	C2D_Font result = nullptr;
	if (!fontHasGlyph(nullptr, cp)) {
		if (systemFontSlot < 0) {
			systemFontSlot = querySystemFontSlot();
		}

		static const int hangulOrder[] = {2, 0, 1, 3};
		static const int cjkOrder[] = {0, 1, 3, 2};
		const int *order = isHangul(cp) ? hangulOrder : cjkOrder;

		for (int i = 0; i < NUM_FALLBACK_FONTS; i++) {
			int slot = order[i];
			if (slot == systemFontSlot) {
				continue;
			}
			if (!fallbackFontTried[slot]) {
				loadFallbackFont(slot);
			}
			if (fallbackFonts[slot] && fontHasGlyph(fallbackFonts[slot], cp)) {
				result = fallbackFonts[slot];
				break;
			}
		}
	}

	glyphFontCache[cp] = result;
	return result;
}

static bool needsFontFallback(const std::string &text) {
	size_t cursor = 0;
	while (cursor < text.length() && static_cast<unsigned char>(text[cursor]) < 0x80) {
		cursor++;
	}
	while (cursor < text.length()) {
		if (glyphFallbackFont(Utils::Utf8::decodeNext(text, cursor))) {
			return true;
		}
	}
	return false;
}

template <typename OnRun, typename OnLineBreak>
static void splitFontRuns(const std::string &text, OnRun onRun, OnLineBreak onLineBreak) {
	std::string run;
	C2D_Font runFont = nullptr;

	auto flush = [&]() {
		if (!run.empty()) {
			onRun(run, runFont);
			run.clear();
		}
	};

	size_t cursor = 0;
	while (cursor < text.length()) {
		size_t start = cursor;
		uint32_t cp = Utils::Utf8::decodeNext(text, cursor);
		if (cp == '\n') {
			flush();
			onLineBreak();
			continue;
		}
		C2D_Font font = glyphFallbackFont(cp);
		if (font != runFont) {
			flush();
			runFont = font;
		}
		run.append(text, start, cursor - start);
	}
	flush();
}

Screen::Screen() : exitRequested(false) {}

ScreenManager &ScreenManager::getInstance() {
	static ScreenManager instance;
	return instance;
}

// Set when the console wakes from sleep: the PIN lock (if any) closes over whatever was on screen.
static std::atomic<bool> lockOnWake{false};

void ScreenManager::init() {
	Config::getInstance().loadTheme();
	topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	if (!textBuf) {
		textBuf = C2D_TextBufNew(32768);
	}
	if (!debugTextBuf) {
		debugTextBuf = C2D_TextBufNew(16384);
	}
	if (!layoutTextBuf) {
		layoutTextBuf = C2D_TextBufNew(32768);
	}

	debugOverlayEnabled = false;

	// The framebuffers may no longer hold our last frame after the HOME menu, sleep or an applet.
	aptHook(
	    &aptCookie,
	    [](APT_HookType hook, void *) {
		    if (hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP) {
			    Utils::requestRedraw();
		    }
		    // Closing the lid puts the console to sleep: show as idle meanwhile, and back online on wake-up.
		    static bool wentIdle = false;
		    auto &client = Discord::DiscordClient::getInstance();
		    if (hook == APTHOOK_ONSLEEP && client.getState() == Discord::ConnectionState::READY &&
		        client.getCurrentUser().status == Discord::UserStatus::ONLINE) {
			    wentIdle = true;
			    client.updatePresence(Discord::UserStatus::IDLE);
		    } else if (hook == APTHOOK_ONWAKEUP && wentIdle) {
			    wentIdle = false;
			    client.updatePresence(Discord::UserStatus::ONLINE);
		    }
		    if (hook == APTHOOK_ONWAKEUP) {
			    lockOnWake = true;
		    }
	    },
	    nullptr);

	Logger::log("[UI] Screen manager initialized");

	if (Config::getInstance().hasPin()) {
		PinLockScreen::atStartup = true;
		setScreen(ScreenType::PIN_LOCK);
	} else {
		startFlow();
	}
}

void ScreenManager::startFlow() {
	if (!Config::getInstance().isDisclaimerAccepted()) {
		setScreen(ScreenType::DISCLAIMER);
	} else if (Config::getInstance().isAutoUpdateEnabled()) {
		setScreen(ScreenType::UPDATER);
	} else {
		enterMainScreen();
	}
}

void ScreenManager::enterMainScreen() {
	if (Discord::DiscordClient::getInstance().getState() == Discord::ConnectionState::READY) {
		setScreen(ScreenType::GUILD_LIST);
	} else {
		setScreen(ScreenType::LOGIN);
	}
}

void ScreenManager::shutdown() {
	aptUnhook(&aptCookie);
	if (currentScreen) {
		currentScreen->onExit();
		currentScreen.reset();
	}

	if (textBuf) {
		C2D_TextBufDelete(textBuf);
		textBuf = nullptr;
	}
	if (debugTextBuf) {
		C2D_TextBufDelete(debugTextBuf);
		debugTextBuf = nullptr;
	}
	if (layoutTextBuf) {
		C2D_TextBufDelete(layoutTextBuf);
		layoutTextBuf = nullptr;
	}

	glyphFontCache.clear();
	for (int i = 0; i < NUM_FALLBACK_FONTS; i++) {
		if (fallbackFonts[i]) {
			C2D_FontFree(fallbackFonts[i]);
			fallbackFonts[i] = nullptr;
		}
		fallbackFontTried[i] = false;
	}

	Logger::log("[UI] Screen manager shutdown");
}

void ScreenManager::setScreen(ScreenType type) {
	// Switching inside the current screen's update would destroy that screen while its update still runs.
	if (updatingScreen) {
		pendingScreen = type;
		return;
	}

	if (currentScreen) {
		currentScreen->onExit();
	}

	if (type == ScreenType::LOGIN || type == ScreenType::GUILD_LIST || type == ScreenType::ADD_ACCOUNT ||
	    type == ScreenType::DISCLAIMER || type == ScreenType::UPDATER ||
	    (type == ScreenType::PIN_LOCK && PinLockScreen::atStartup)) {
		screenHistory.clear();
	}

	currentType = type;

	if (type == ScreenType::LOGIN || type == ScreenType::ADD_ACCOUNT || type == ScreenType::DISCLAIMER ||
	    type == ScreenType::UPDATER || type == ScreenType::PIN_LOCK) {
		hamburgerMenu.reset();
	}

	switch (type) {
	case ScreenType::LOGIN:
		expandedFolders.clear();

		currentScreen = std::make_unique<LoginScreen>();
		break;
	case ScreenType::GUILD_LIST: {
		currentScreen = std::make_unique<ServerListScreen>();
		// Once per session: what happened while the app was closed.
		static bool awayShown = false;
		auto &dc = Discord::DiscordClient::getInstance();
		if (!awayShown && dc.getState() == Discord::ConnectionState::READY) {
			awayShown = true;
			int mentions = 0;
			int conversations = 0;
			{
				std::lock_guard<std::recursive_mutex> lock(dc.getMutex());
				for (const auto &kv : dc.getReadStates()) {
					if (kv.second.mentionCount > 0) {
						mentions += kv.second.mentionCount;
						conversations++;
					}
				}
			}
			if (mentions > 0) {
				showToast(Core::I18n::format(TR("away.summary"), std::to_string(mentions),
				                             std::to_string(conversations)));
			}
		}
		break;
	}
	case ScreenType::MESSAGES: {
		auto &client = Discord::DiscordClient::getInstance();
		std::string channelId = client.getSelectedChannelId();
		std::string channelName = TR("common.channel");
		for (const auto &g : client.getGuilds()) {
			for (const auto &ch : g.channels) {
				if (ch.id == channelId) {
					channelName = ch.name;
					goto found;
				}
			}
		}
		for (const auto &ch : client.getPrivateChannels()) {
			if (ch.id == channelId) {
				channelName = ch.name;
				if (channelName.empty() && ch.type == 1 && !ch.recipients.empty()) {
					channelName = ch.recipients[0].global_name;
					if (channelName.empty()) {
						channelName = ch.recipients[0].username;
					}
				}
				break;
			}
		}
	found:
		currentScreen = std::make_unique<MessageScreen>(channelId, channelName);
		break;
	}
	case ScreenType::ADD_ACCOUNT:
		currentScreen = std::make_unique<LoginScreen>();
		break;
	case ScreenType::FORUM_CHANNEL: {
		auto &client = Discord::DiscordClient::getInstance();
		std::string channelId = forumChannelId;
		std::string channelName = TR("common.forum");
		for (const auto &g : client.getGuilds()) {
			for (const auto &ch : g.channels) {
				if (ch.id == channelId) {
					channelName = ch.name;
					break;
				}
			}
		}
		currentScreen = std::make_unique<ForumScreen>(channelId, channelName);
		break;
	}
	case ScreenType::SETTINGS:
		currentScreen = std::make_unique<SettingsScreen>();
		break;
	case ScreenType::ABOUT:
		currentScreen = std::make_unique<AboutScreen>();
		break;
	case ScreenType::DISCLAIMER:
		currentScreen = std::make_unique<DisclaimerScreen>();
		break;
	case ScreenType::THEME_MANAGER:
		currentScreen = std::make_unique<ThemeManagerScreen>();
		break;
	case ScreenType::UPDATER:
		currentScreen = std::make_unique<UpdaterScreen>();
		break;
	case ScreenType::LIST:
		currentScreen = std::make_unique<ListScreen>();
		break;
	case ScreenType::IMAGE_VIEWER:
		currentScreen = std::make_unique<ImageViewerScreen>();
		break;
	case ScreenType::FILE_BROWSER:
		currentScreen = std::make_unique<FileBrowserScreen>();
		break;
	case ScreenType::PROFILE:
		currentScreen = std::make_unique<ProfileScreen>();
		break;
	case ScreenType::FILE_PREVIEW:
		currentScreen = std::make_unique<FilePreviewScreen>();
		break;
	case ScreenType::VOICE_RECORDER:
		currentScreen = std::make_unique<VoiceRecorderScreen>();
		break;
	case ScreenType::PIN_LOCK:
		currentScreen = std::make_unique<PinLockScreen>();
		break;
	case ScreenType::CAMERA:
		currentScreen = std::make_unique<CameraScreen>();
		break;
	case ScreenType::VIDEO_TEST:
		currentScreen = std::make_unique<VideoTestScreen>();
		break;
	case ScreenType::MVD_TEST:
		currentScreen = std::make_unique<MvdTestScreen>();
		break;
	case ScreenType::SOFT_TEST:
		currentScreen = std::make_unique<SoftTestScreen>();
		break;
	case ScreenType::VIDEO_PLAYER:
		currentScreen = std::make_unique<VideoPlayerScreen>();
		break;
	case ScreenType::VIDEO_ORIENT:
		currentScreen = std::make_unique<VideoOrientScreen>();
		break;
	}

	if (currentScreen) {
		currentScreen->onEnter();
	}
	Utils::requestRedraw();
}

void ScreenManager::pushScreen(ScreenType type) {
	if (currentType != type) {
		screenHistory.push_back(currentType);
	}
	setScreen(type);
}

void ScreenManager::returnToPreviousScreen() {
	if (screenHistory.empty()) {
		if (currentType != ScreenType::GUILD_LIST && currentType != ScreenType::LOGIN) {
			setScreen(ScreenType::GUILD_LIST);
		}
		return;
	}

	ScreenType prev = screenHistory.back();
	screenHistory.pop_back();

	setScreen(prev);
}

void ScreenManager::update() {
	ImageManager::getInstance().update();
	EmojiManager::getInstance().update();
	Discord::AvatarCache::getInstance().update();
	Discord::VoiceClient::getInstance().update();

	hamburgerMenu.update();

	if (lockOnWake.exchange(false) && Config::getInstance().hasPin() && currentType != ScreenType::PIN_LOCK &&
	    currentType != ScreenType::UPDATER && currentType != ScreenType::DISCLAIMER) {
		PinLockScreen::atStartup = false;
		pushScreen(ScreenType::PIN_LOCK);
	}

	// A warning when the battery is low and the console is not charging (looked at once a minute).
	{
		static u64 nextBatteryCheck = 0;
		static bool batteryWarned = false;
		const u64 nowMs = osGetTime();
		if (Config::getInstance().isLowBatteryWarning() && nowMs >= nextBatteryCheck) {
			nextBatteryCheck = nowMs + 60000;
			u8 level = 5;
			u8 charging = 0;
			if (R_SUCCEEDED(ptmuInit())) {
				PTMU_GetBatteryLevel(&level);
				PTMU_GetBatteryChargeState(&charging);
				ptmuExit();
			}
			if (!charging && level <= 1) {
				if (!batteryWarned) {
					batteryWarned = true;
					showToast(TR("battery.low"));
				}
			} else if (charging || level >= 3) {
				batteryWarned = false;
			}
		}
	}

	// Results of the background saves and uploads.
	{
		const std::string transferMessage = Utils::Transfers::getInstance().takeMessage();
		if (!transferMessage.empty()) {
			showToast(transferMessage);
		}
	}

	if (layoutTextBuf) {
		C2D_TextBufClear(layoutTextBuf);
	}

	u32 kDown = hidKeysDown();
	u32 kHeld = hidKeysHeld();

	circlePosition circle;
	hidCircleRead(&circle);
	inputActive =
	    kDown || kHeld || hidKeysUp() || std::abs(circle.dx) > CIRCLE_DEADZONE || std::abs(circle.dy) > CIRCLE_DEADZONE;

	if (kDown & KEY_START) {
		appExitRequested = true;
		return;
	}

	Utils::CallRecorder::getInstance().flush();
	{
		const std::string playerError = Utils::VoicePlayer::getInstance().takeError();
		if (!playerError.empty()) {
			showToast(TR("voice_msg.play_failed"));
		}
	}

	// Voice shortcuts that work on every screen while a call is on:
	//   mute: ZL or L+X      deafen: ZR or L+Y      push to talk: the button chosen in Settings > Voice.
	bool voiceKeyHandled = false;
	{
		Discord::VoiceClient &voice = Discord::VoiceClient::getInstance();
		const bool inCall = voice.getState() == Discord::VoiceState::ESTABLISHED;
		const int pttMode = Config::getInstance().getPushToTalk();
		const u32 pttKey = pttMode == 1 ? KEY_R : pttMode == 2 ? KEY_ZR : 0;
		voice.setPushToTalk(inCall && pttKey != 0, inCall && pttKey != 0 && (kHeld & pttKey));

		// A call that drops by itself is joined again after a few seconds, up to three times.
		{
			static std::string wantedGuild, wantedChannel;
			static int attempts = 0;
			static int wait = 0;
			static Discord::VoiceState lastState = Discord::VoiceState::DISCONNECTED;
			const Discord::VoiceState now = voice.getState();
			if (now == Discord::VoiceState::ESTABLISHED) {
				wantedGuild = voice.getGuildId();
				wantedChannel = voice.getChannelId();
				attempts = 0;
				wait = 0;
			} else if (now == Discord::VoiceState::DISCONNECTED) {
				wantedChannel.clear(); // the user hung up
			} else if (now == Discord::VoiceState::FAILED && lastState != Discord::VoiceState::FAILED &&
			           !wantedChannel.empty() && Config::getInstance().isAutoReconnect() && attempts < 3) {
				wait = 180; // three seconds
			}
			if (wait > 0 && --wait == 0 && !wantedChannel.empty()) {
				attempts++;
				showToast(TR("voice.reconnecting"));
				voice.connect(wantedGuild, wantedChannel);
			}
			lastState = now;
		}

		if (inCall) {
			const bool chord = (kHeld & KEY_L) && !(kHeld & KEY_R);
			const bool muteKey = (kDown & KEY_ZL) || (chord && (kDown & KEY_X));
			const bool deafKey = (pttKey != KEY_ZR && (kDown & KEY_ZR)) || (chord && (kDown & KEY_Y));
			// L+R: the top screen shows the speaker in the corner, everyone in the call, or nothing.
			const bool viewKey = ((kDown & KEY_R) && (kHeld & KEY_L)) || ((kDown & KEY_L) && (kHeld & KEY_R));
			if (viewKey) {
				CallView::cycle();
				voiceKeyHandled = true;
			} else if (muteKey && !voice.isServerMuted()) {
				voice.setMuted(!voice.isMuted());
				showToast(TR(voice.isMuted() ? "voice.muted" : "voice.unmuted"));
				voiceKeyHandled = true;
			} else if (deafKey && !voice.isServerDeafened()) {
				voice.setDeafened(!voice.isDeafened());
				showToast(TR(voice.isDeafened() ? "voice.deafened" : "voice.undeafened"));
				voiceKeyHandled = true;
			}
		}
	}

	bool callWasVisible = incomingCall.isVisible();
	incomingCall.update();

	bool shouldBlockScreen = !hamburgerMenu.isClosed() || callWasVisible || incomingCall.isVisible();

	if (debugOverlayEnabled && (kHeld & KEY_L)) {
		if (kHeld & (KEY_UP | KEY_CPAD_UP)) {
			debugScrollOffset += 8.0f;
		}
		if (kHeld & (KEY_DOWN | KEY_CPAD_DOWN)) {
			debugScrollOffset -= 8.0f;
		}
		shouldBlockScreen = true;
	}

	if (!isMenuHidden() && !callWasVisible) {
		touchPosition touch;
		hidTouchRead(&touch);
		if (kDown & KEY_TOUCH) {
			if (touch.px < 40 && touch.py < 40) {
				if (shouldShowBackArrow()) {
					UI::SettingsScreen *ss = (UI::SettingsScreen *)currentScreen.get();
					ss->saveAndExit();
				} else {
					hamburgerMenu.toggle();
				}
				shouldBlockScreen = true;
			}
		} else if (kDown & KEY_SELECT) {
			hamburgerMenu.toggle();
			shouldBlockScreen = !hamburgerMenu.isClosed();
		}
	}

	if (voiceKeyHandled) {
		shouldBlockScreen = true;
	}

	if (!shouldBlockScreen) {
		if (currentScreen) {
			updatingScreen = true;
			currentScreen->update();
			updatingScreen = false;
		}
		if (pendingScreen) {
			ScreenType next = *pendingScreen;
			pendingScreen.reset();
			setScreen(next);
		}
	}

	if ((kHeld & KEY_L) && (kDown & KEY_R)) {
		toggleDebugOverlay();
		Logger::log("Debug overlay toggled: %s", debugOverlayEnabled ? "ON" : "OFF");
	} else if ((kHeld & KEY_R) && (kDown & KEY_L)) {
		toggleStatsOverlay();
	}

	if (toastTimer > 0) {
		toastTimer--;
	}
}

bool ScreenManager::shouldRender() {
	bool requested = Utils::consumeRedrawRequest();
	bool animating = (currentScreen && currentScreen->isAnimating()) || hamburgerMenu.isAnimating() || toastTimer > 0 ||
	                 debugOverlayEnabled || statsOverlayEnabled;
	// The update that ends an animation leaves its final frame still to draw.
	// While the call view is on the top screen it is redrawn about ten times a second, so the speaking ring follows.
	const bool callShown = CallView::mode() != CallView::Mode::HIDDEN &&
	                       Discord::VoiceClient::getInstance().getState() == Discord::VoiceState::ESTABLISHED;
	const int idleLimit = callShown ? 6 : IDLE_REDRAW_FRAMES;
	bool render = requested || inputActive || animating || wasAnimating || ++framesSinceRender >= idleLimit;
	wasAnimating = animating;
	if (render) {
		framesSinceRender = 0;
	}
	return render;
}

void ScreenManager::render() {
	Utils::Screenshot::tick();
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

	if (textBuf) {
		C2D_TextBufClear(textBuf);
	}
	if (debugTextBuf) {
		C2D_TextBufClear(debugTextBuf);
	}

	C2D_TargetClear(topTarget, colorBackground());
	C2D_SceneBegin(topTarget);

	if (currentScreen) {
		currentScreen->renderTop(topTarget);
	}

	CallView::render();

	if (!isMenuHidden()) {
		hamburgerMenu.render();
	}

	if (debugOverlayEnabled) {
		renderDebugOverlay();
	}

	if (statsOverlayEnabled) {
		renderStatsOverlay();
	}

	C2D_TargetClear(bottomTarget, colorBackground());
	C2D_SceneBegin(bottomTarget);

	if (currentScreen) {
		currentScreen->renderBottom(bottomTarget);
	}

	if (!isMenuHidden()) {
		drawHamburgerButton();
	}

	if (toastTimer > 0) {
		drawToast();
	}

	incomingCall.render();

	C3D_FrameEnd(0);
}

void ScreenManager::toggleDebugOverlay() {
	debugOverlayEnabled = !debugOverlayEnabled;
	debugScrollOffset = 0.0f;
}

void ScreenManager::renderDebugOverlay() {
	std::vector<std::string> logs = Logger::getRecentLogs();
	float topMargin = 5.0f;
	float lineHeight = 10.0f;
	float viewHeight = 240.0f - topMargin;
	float maxScroll = std::max(0.0f, logs.size() * lineHeight - viewHeight);
	debugScrollOffset = std::clamp(debugScrollOffset, 0.0f, maxScroll);

	float yStart = topMargin - (maxScroll - debugScrollOffset);

	for (size_t i = 0; i < logs.size(); i++) {
		float y = yStart + i * lineHeight;
		if (y + lineHeight <= 0.0f) {
			continue;
		}
		if (y > 240.0f) {
			break;
		}

		C2D_Text text;
		if (!C2D_TextParse(&text, debugTextBuf, logs[i].c_str())) {
			break;
		}
		C2D_TextOptimize(&text);
		C2D_DrawText(&text, C2D_WithColor, 5.0f, y, 1.0f, 0.4f, 0.4f, C2D_Color32(0, 255, 0, 255));
	}
}

void ScreenManager::toggleStatsOverlay() {
	statsOverlayEnabled = !statsOverlayEnabled;
	statsFrames = 0;
	statsWindowStart = osGetTime();
	statsFps = 0.0f;
	statsFrameMs = 0.0f;
}

void ScreenManager::renderStatsOverlay() {
	statsFrames++;
	uint64_t now = osGetTime();
	uint64_t elapsed = now - statsWindowStart;
	if (elapsed >= 500) {
		statsFps = (float)statsFrames * 1000.0f / (float)elapsed;
		statsFrameMs = (float)elapsed / (float)statsFrames;
		statsFrames = 0;
		statsWindowStart = now;
	}

	auto &images = ImageManager::getInstance();
	auto &emoji = EmojiManager::getInstance();
	auto &config = Config::getInstance();
	auto &client = Discord::DiscordClient::getInstance();
	auto &voice = Discord::VoiceClient::getInstance();

	size_t netR = 0, netI = 0, netB = 0;
	Network::NetworkManager::getInstance().getQueueDepths(netR, netI, netB);

	static const char *const GW_NAMES[] = {"OFF", "CONN", "WS", "IDENT", "AUTH", "READY", "RECON", "ERR"};
	int gwState = (int)client.getState();
	const char *gwName = (gwState >= 0 && gwState < 8) ? GW_NAMES[gwState] : "?";

	static const char *const VC_NAMES[] = {"OFF", "WAIT", "CONN", "IDENT", "READY", "PROTO", "ON", "FAIL"};
	int vcState = (int)voice.getState();
	const char *vcName = (vcState >= 0 && vcState < 8) ? VC_NAMES[vcState] : "?";

	size_t linFree = linearSpaceFree();
	size_t imgBytes = images.getCacheBytes();

	struct Line {
		char text[44];
		u32 color;
	};
	Line lines[12];
	int n = 0;

	const u32 kOk = C2D_Color32(120, 255, 160, 255);
	const u32 kDim = C2D_Color32(150, 150, 150, 255);
	const u32 kWarn = C2D_Color32(255, 200, 80, 255);
	const u32 kBad = C2D_Color32(255, 110, 110, 255);

	auto add = [&](u32 color, const char *fmt, ...) {
		if (n >= (int)(sizeof(lines) / sizeof(lines[0]))) {
			return;
		}
		va_list args;
		va_start(args, fmt);
		vsnprintf(lines[n].text, sizeof(lines[0].text), fmt, args);
		va_end(args);
		lines[n].color = color;
		n++;
	};

	add(statsFps < 50.0f ? kWarn : kOk, "FPS %.1f  GPU %.1f/%.1f", statsFps, C3D_GetProcessingTime(),
	    C3D_GetDrawingTime());
	add(linFree < 4u * 1024 * 1024 ? kBad : (linFree < 8u * 1024 * 1024 ? kWarn : kOk), "LIN %luK  APP %luK",
	    (unsigned long)(linFree / 1024), (unsigned long)(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024));
	add(imgBytes >= ImageManager::getCacheBudget() ? kWarn : kOk, "IMG %luK/%luK x%lu",
	    (unsigned long)(imgBytes / 1024), (unsigned long)(ImageManager::getCacheBudget() / 1024),
	    (unsigned long)images.getCacheCount());
	add(kOk, "EMO %lu/%lu  AVA %lu", (unsigned long)emoji.getTwemojiCount(), (unsigned long)emoji.getCustomCount(),
	    (unsigned long)Discord::AvatarCache::getInstance().getCacheCount());
	add(kOk, "TXT %lu  GLD %lu", (unsigned long)TextMeasureCache::getInstance().getCacheSize(),
	    (unsigned long)client.getGuilds().size());
	add((netR + netI + netB) > 16 ? kWarn : kDim, "NET r%lu i%lu b%lu", (unsigned long)netR, (unsigned long)netI,
	    (unsigned long)netB);

	add(gwState == (int)Discord::ConnectionState::READY ? kOk : kWarn, "GW %s  WIFI %d", gwName,
	    (int)osGetWifiStrength());
	add(vcState == (int)Discord::VoiceState::ESTABLISHED ? kOk : kDim, "VC %s%s%s", vcName, voice.isMuted() ? " M" : "",
	    voice.isDeafened() ? " D" : "");

	int extraThreads = Utils::WorkerThread::extraCoreThreads();
	add(extraThreads > 0 ? kOk : kDim, "N3DS %s  CORE2 %d thr",
	    Utils::WorkerThread::extraCoreAvailable() ? "yes" : "no", extraThreads);
	add(kDim, "AVATAR %s  TYPE %s", config.isShowAvatarsEnabled() ? "on" : "off",
	    config.isTypingIndicatorEnabled() ? "on" : "off");
	add(kDim, "FLOG %s", config.isFileLoggingEnabled() ? "on" : "off");

	const float scale = 0.4f;
	const float lineHeight = 10.0f;
	const float pad = 3.0f;
	const float right = 400.0f;

	float boxW = 0.0f;
	for (int i = 0; i < n; i++) {
		boxW = std::max(boxW, measureText(lines[i].text, scale, scale));
	}
	boxW += pad * 2.0f;
	float boxH = n * lineHeight + pad * 2.0f;
	float boxX = right - boxW;

	C2D_DrawRectSolid(boxX, 0.0f, 1.0f, boxW, boxH, C2D_Color32(0, 0, 0, 200));

	float y = pad;
	for (int i = 0; i < n; i++) {
		drawText(boxX + pad, y, 1.0f, scale, scale, lines[i].color, lines[i].text);
		y += lineHeight;
	}
}

void ScreenManager::drawHamburgerButton() {
	if (shouldShowBackArrow()) {
		C3D_Tex *backTex = ImageManager::getInstance().getLocalImage("romfs:/discord-icons/arrow-large-left.png", true);
		if (backTex) {
			ImageManager::ImageInfo info =
			    ImageManager::getInstance().getImageInfo("romfs:/discord-icons/arrow-large-left.png");
			Tex3DS_SubTexture sub;
			sub.width = (u16)info.originalW;
			sub.height = (u16)info.originalH;
			sub.left = 0.0f;
			sub.top = 0.0f;
			sub.right = (float)info.originalW / backTex->width;
			sub.bottom = (float)info.originalH / backTex->height;
			C2D_Image img = {backTex, &sub};
			float iconSize = 20.0f;
			float scale = iconSize / info.originalW;
			C2D_DrawImageAtRotated(img, 18, 18, 1.0f, -M_PI / 2.0f, nullptr, scale, scale);
			return;
		}
	}

	u32 color = colorText();
	float x = 12.0f;
	float y = 11.0f;
	float w = 18.0f;
	float h = 2.0f;
	float gap = 5.0f;
	float z = 1.0f;
	float r = 1.0f;

	drawRoundedRect(x, y, z, w, h, r, color);
	drawRoundedRect(x, y + gap, z, w, h, r, color);
	drawRoundedRect(x, y + gap * 2, z, w, h, r, color);
}

void ScreenManager::showToast(const std::string &message) {
	toastMessage = message;
	toastTimer = 120;
}

bool ScreenManager::isMenuHidden() const {
	auto &client = Discord::DiscordClient::getInstance();
	bool isConnecting = (client.getState() == Discord::ConnectionState::CONNECTING ||
	                     client.getState() == Discord::ConnectionState::AUTHENTICATING);

	return (currentType == ScreenType::LOGIN) || (currentType == ScreenType::DISCLAIMER) ||
	       (currentType == ScreenType::ADD_ACCOUNT && isConnecting) || (currentScreen && currentScreen->hidesMenu());
}

bool ScreenManager::shouldShowBackArrow() const {
	if (currentType != ScreenType::SETTINGS) {
		return false;
	}
	if (screenHistory.empty()) {
		return false;
	}

	ScreenType prev = screenHistory.back();
	return (prev == ScreenType::LOGIN || prev == ScreenType::DISCLAIMER);
}

void ScreenManager::drawToast() {
	float w = measureText(toastMessage, 0.5f, 0.5f) + 24.0f;
	float h = 32.0f;
	float x = (320.0f - w) / 2.0f;
	float y = 180.0f;
	float z = 0.95f;

	u32 bg = C2D_Color32(40, 40, 45, 235);
	drawRoundedRect(x, y, z, w, h, 8.0f, bg);
	float progressW = (w - 8.0f) * (toastTimer / 120.0f);
	drawRoundedRect(x + 4, y + h - 2.0f, z + 0.01f, progressW, 1.5f, 0.75f, colorSelection());

	C2D_SceneBegin(bottomTarget);
	drawCenteredText(y + 9.0f, z + 0.02f, 0.5f, 0.5f, colorWhite(), toastMessage, 320.0f);
}

void drawText(float x, float y, float z, float scaleX, float scaleY, u32 color, const std::string &rawText) {
	std::string text = Utils::Utf8::sanitizeText(rawText);

	if (!textBuf) {
		return;
	}

	if (!needsFontFallback(text)) {
		C2D_Text c2dText;
		if (!C2D_TextParse(&c2dText, textBuf, text.c_str())) {
			return;
		}
		C2D_TextOptimize(&c2dText);
		C2D_DrawText(&c2dText, C2D_WithColor, x, y, z, scaleX, scaleY, color);
		return;
	}

	float lineFeed = C2D_FontGetInfo(nullptr)->lineFeed * scaleY;
	float curX = x;
	float curY = y;

	splitFontRuns(
	    text,
	    [&](const std::string &run, C2D_Font font) {
		    C2D_Text c2dText;
		    if (!C2D_TextFontParse(&c2dText, font, textBuf, run.c_str())) {
			    return;
		    }
		    C2D_TextOptimize(&c2dText);
		    float scaleAdj = fallbackFontScale(font);
		    float yOff = fallbackBaselineOffset(font, scaleY, scaleAdj);
		    C2D_DrawText(&c2dText, C2D_WithColor, curX, curY + yOff, z, scaleX * scaleAdj, scaleY * scaleAdj, color);
		    float width, height;
		    C2D_TextGetDimensions(&c2dText, scaleX * scaleAdj, scaleY * scaleAdj, &width, &height);
		    curX += width;
	    },
	    [&]() {
		    curX = x;
		    curY += lineFeed;
	    });
}

void drawCenteredText(float y, float z, float scaleX, float scaleY, u32 color, const std::string &rawText,
                      float screenWidth) {
	if (!textBuf) {
		return;
	}

	float width = measureText(rawText, scaleX, scaleY);
	float x = (screenWidth - width) / 2.0f;
	drawText(x, y, z, scaleX, scaleY, color, rawText);
}

float measureTextDirect(const std::string &rawText, float scaleX, float scaleY) {
	std::string text = Utils::Utf8::sanitizeText(rawText);

	if (!layoutTextBuf || text.empty()) {
		return 0.0f;
	}

	if (!needsFontFallback(text)) {
		C2D_Text c2dText;
		if (!C2D_TextParse(&c2dText, layoutTextBuf, text.c_str())) {
			C2D_TextBufClear(layoutTextBuf);
			return 0.0f;
		}

		float width, height;
		C2D_TextGetDimensions(&c2dText, scaleX, scaleY, &width, &height);

		C2D_TextBufClear(layoutTextBuf);
		return width;
	}

	float maxWidth = 0.0f;
	float curX = 0.0f;

	splitFontRuns(
	    text,
	    [&](const std::string &run, C2D_Font font) {
		    C2D_Text c2dText;
		    if (!C2D_TextFontParse(&c2dText, font, layoutTextBuf, run.c_str())) {
			    return;
		    }
		    float scaleAdj = fallbackFontScale(font);
		    float width, height;
		    C2D_TextGetDimensions(&c2dText, scaleX * scaleAdj, scaleY * scaleAdj, &width, &height);
		    curX += width;
	    },
	    [&]() {
		    maxWidth = std::max(maxWidth, curX);
		    curX = 0.0f;
	    });

	C2D_TextBufClear(layoutTextBuf);
	return std::max(maxWidth, curX);
}

float measureText(const std::string &text, float scaleX, float scaleY) {
	return UI::TextMeasureCache::getInstance().measureText(text, scaleX, scaleY);
}

void drawRoundedRect(float x, float y, float z, float w, float h, float radius, u32 color) {
	if (radius <= 0) {
		C2D_DrawRectSolid(x, y, z, w, h, color);
		return;
	}

	if (radius > w / 2) {
		radius = w / 2;
	}
	if (radius > h / 2) {
		radius = h / 2;
	}

	C2D_DrawRectSolid(x + radius, y, z, w - 2 * radius, h, color);
	C2D_DrawRectSolid(x, y + radius, z, radius, h - 2 * radius, color);
	C2D_DrawRectSolid(x + w - radius, y + radius, z, radius, h - 2 * radius, color);

	static float arcCos[33];
	static float arcSin[33];
	static bool arcInit = false;
	if (!arcInit) {
		for (int i = 0; i <= 32; i++) {
			float a = (float)i * ((float)M_PI / 16.0f);
			arcCos[i] = cosf(a);
			arcSin[i] = sinf(a);
		}
		arcInit = true;
	}

	auto drawCorner = [&](float cx, float cy, int base) {
		for (int i = 0; i < 8; i++) {
			C2D_DrawTriangle(cx, cy, color, cx + radius * arcCos[base + i], cy + radius * arcSin[base + i], color,
			                 cx + radius * arcCos[base + i + 1], cy + radius * arcSin[base + i + 1], color, z);
		}
	};

	drawCorner(x + radius, y + radius, 16);
	drawCorner(x + w - radius, y + radius, 24);
	drawCorner(x + w - radius, y + h - radius, 0);
	drawCorner(x + radius, y + h - radius, 8);
}

void drawCircle(float x, float y, float z, float radius, u32 color) { C2D_DrawCircleSolid(x, y, z, radius, color); }

void drawScrollbar(float maxScroll, float currentScroll, float y, float viewHeight) {
	if (maxScroll <= 0.0f) {
		return;
	}
	float barHeight = std::max(10.0f, viewHeight * (viewHeight / (viewHeight + maxScroll)));
	float barY = y + (currentScroll / maxScroll) * (viewHeight - barHeight);
	C2D_DrawRectSolid(314.0f, barY, 0.41f, 3.0f, barHeight, ScreenManager::colorTextMuted());
}

void drawRichText(float x, float y, float z, float scaleX, float scaleY, u32 color, const std::string &text) {
	if (!textBuf || text.empty()) {
		return;
	}

	size_t cursor = 0;
	float currentX = x;

	while (cursor < text.length()) {
		if (text[cursor] == '<') {
			size_t start = cursor;
			if (start + 6 < text.length()) {
				bool isAnimated = (text[start + 1] == 'a');
				if (text[start + 1] == ':' || isAnimated) {
					size_t secondColon = text.find(':', start + (isAnimated ? 3 : 2));
					if (secondColon != std::string::npos) {
						size_t closeBracket = text.find('>', secondColon);
						if (closeBracket != std::string::npos) {
							std::string id = text.substr(secondColon + 1, closeBracket - secondColon - 1);
							EmojiManager::EmojiInfo info = EmojiManager::getInstance().getEmojiInfo(id, isAnimated);
							float emojiSize = 28.0f * scaleY;

							if (info.tex) {
								float uMax = (float)info.originalW / info.tex->width;
								float vMax = (float)info.originalH / info.tex->height;

								Tex3DS_SubTexture subtex = {(u16)info.originalW, (u16)info.originalH, 0.0f, 1.0f, uMax,
								                            1.0f - vMax};

								const C2D_Image img = {info.tex, &subtex};
								C2D_DrawImageAt(img, currentX, y + 1.0f, z, nullptr, emojiSize / info.originalW,
								                emojiSize / info.originalH);
								ImageManager::getInstance().markDrawn(info.animation);
							} else {
								EmojiManager::getInstance().prefetchEmoji(id, isAnimated);

								std::string name = text.substr(start + (isAnimated ? 3 : 2),
								                               secondColon - (start + (isAnimated ? 3 : 2)));
								std::string fallback = ":" + name + ":";
								drawText(currentX, y, z, scaleX, scaleY, color, fallback);

								currentX += measureText(fallback, scaleX, scaleY) - (emojiSize + (2.0f * scaleX));
							}

							currentX += emojiSize + (2.0f * scaleX);
							cursor = closeBracket + 1;
							continue;
						}
					}
				}
			}
		}

		if (static_cast<unsigned char>(text[cursor]) >= 0x80) {
			size_t tempCursor = cursor;
			uint32_t codepoint = Utils::Utf8::decodeNext(text, tempCursor);

			if (Utils::Utf8::isEmoji(codepoint)) {
				size_t seqCursor = cursor;
				std::string sequence = Utils::Utf8::getEmojiSequence(text, seqCursor);
				std::string hex = Utils::Utf8::utf8ToHex(sequence);
				EmojiManager::EmojiInfo info = EmojiManager::getInstance().getTwemojiInfo(hex);
				float emojiSize = 28.0f * scaleY;

				if (info.tex) {
					float uMax = (float)info.originalW / info.tex->width;
					float vMax = (float)info.originalH / info.tex->height;
					Tex3DS_SubTexture subtex = {(u16)info.originalW, (u16)info.originalH, 0.0f, 1.0f, uMax,
					                            1.0f - vMax};
					const C2D_Image img = {info.tex, &subtex};
					C2D_DrawImageAt(img, currentX, y + 1.0f, z, nullptr, emojiSize / info.originalW,
					                emojiSize / info.originalH);
					currentX += emojiSize + (2.0f * scaleX);
					cursor = seqCursor;
					continue;
				} else {
					drawText(currentX, y, z, scaleX, scaleY, color, sequence);
					currentX += measureText(sequence, scaleX, scaleY);
					cursor = seqCursor;
					continue;
				}
			}
		}

		size_t end = cursor;
		while (end < text.length()) {
			if (text[end] == '<') {
				if (end + 6 < text.length()) {
					bool isAnimated = (text[end + 1] == 'a');
					if (text[end + 1] == ':' || isAnimated) {
						size_t secondColon = text.find(':', end + (isAnimated ? 3 : 2));
						if (secondColon != std::string::npos) {
							size_t closeBracket = text.find('>', secondColon);
							if (closeBracket != std::string::npos) {
								break;
							}
						}
					}
				}
			}

			if (static_cast<unsigned char>(text[end]) < 0x80) {
				end++;
				continue;
			}
			size_t nextC = end;
			uint32_t cp = Utils::Utf8::decodeNext(text, nextC);
			if (Utils::Utf8::isEmoji(cp)) {
				break;
			}
			end = nextC;
		}

		if (end > cursor) {
			std::string segment = text.substr(cursor, end - cursor);
			drawText(currentX, y, z, scaleX, scaleY, color, segment);
			currentX += measureText(segment, scaleX, scaleY);
			cursor = end;
		} else if (cursor < text.length()) {
			size_t nextC = cursor;
			Utils::Utf8::decodeNext(text, nextC);
			std::string segment = text.substr(cursor, nextC - cursor);
			drawText(currentX, y, z, scaleX, scaleY, color, segment);
			currentX += measureText(segment, scaleX, scaleY);
			cursor = nextC;
		}
	}
}

void drawCenteredRichText(float y, float z, float scaleX, float scaleY, u32 color, const std::string &rawText,
                          float screenWidth) {
	float width = measureRichText(rawText, scaleX, scaleY);
	float x = (screenWidth - width) / 2.0f;
	drawRichText(x, y, z, scaleX, scaleY, color, rawText);
}

float measureRichTextImpl(const std::string &text, float scaleX, float scaleY, bool unicodeOnly) {
	if (!layoutTextBuf || text.empty()) {
		return 0.0f;
	}

	size_t cursor = 0;
	float currentX = 0;

	while (cursor < text.length()) {
		if (!unicodeOnly && text[cursor] == '<') {
			size_t start = cursor;
			if (start + 6 < text.length()) {
				bool isAnimated = (text[start + 1] == 'a');
				if (text[start + 1] == ':' || isAnimated) {
					size_t secondColon = text.find(':', start + (isAnimated ? 3 : 2));
					if (secondColon != std::string::npos) {
						size_t closeBracket = text.find('>', secondColon);
						if (closeBracket != std::string::npos) {
							float emojiSize = 28.0f * scaleY;
							currentX += emojiSize + (2.0f * scaleX);
							cursor = closeBracket + 1;
							continue;
						}
					}
				}
			}
		}

		if (static_cast<unsigned char>(text[cursor]) >= 0x80) {
			size_t tempCursor = cursor;
			uint32_t codepoint = Utils::Utf8::decodeNext(text, tempCursor);

			if (Utils::Utf8::isEmoji(codepoint)) {
				size_t seqCursor = cursor;
				std::string sequence = Utils::Utf8::getEmojiSequence(text, seqCursor);
				std::string hex = Utils::Utf8::utf8ToHex(sequence);
				EmojiManager::EmojiInfo info = EmojiManager::getInstance().getTwemojiInfo(hex);
				float emojiSize = 28.0f * scaleY;
				if (info.tex) {
					currentX += emojiSize + (2.0f * scaleX);
				} else {
					currentX += measureText(sequence, scaleX, scaleY);
				}
				cursor = seqCursor;
				continue;
			}
		}

		{
			size_t end = cursor;
			while (end < text.length()) {
				if (!unicodeOnly && text[end] == '<') {
					if (end + 6 < text.length()) {
						bool isAnimated = (text[end + 1] == 'a');
						if (text[end + 1] == ':' || isAnimated) {
							size_t secondColon = text.find(':', end + (isAnimated ? 3 : 2));
							if (secondColon != std::string::npos) {
								size_t closeBracket = text.find('>', secondColon);
								if (closeBracket != std::string::npos) {
									break;
								}
							}
						}
					}
				}

				if (static_cast<unsigned char>(text[end]) < 0x80) {
					end++;
					continue;
				}
				size_t nextC = end;
				uint32_t cp = Utils::Utf8::decodeNext(text, nextC);
				if (Utils::Utf8::isEmoji(cp)) {
					break;
				}
				end = nextC;
			}

			if (end > cursor) {
				std::string segment = text.substr(cursor, end - cursor);
				currentX += measureText(segment, scaleX, scaleY);
				cursor = end;
			} else if (cursor < text.length()) {
				size_t nextC = cursor;
				Utils::Utf8::decodeNext(text, nextC);
				std::string segment = text.substr(cursor, nextC - cursor);
				currentX += measureText(segment, scaleX, scaleY);
				cursor = nextC;
			}
		}
	}

	return currentX;
}

float measureRichText(const std::string &rawText, float scaleX, float scaleY) {
	return measureRichTextImpl(rawText, scaleX, scaleY, false);
}

std::string getTruncatedText(const std::string &text, float maxWidth, float scaleX, float scaleY) {
	if (measureText(text, scaleX, scaleY) <= maxWidth) {
		return text;
	}

	std::vector<size_t> offsets;
	for (size_t i = 0; i < text.length();) {
		offsets.push_back(i);
		unsigned char c = (unsigned char)text[i];
		if (c < 0x80) {
			i += 1;
		} else if ((c & 0xE0) == 0xC0) {
			i += 2;
		} else if ((c & 0xF0) == 0xE0) {
			i += 3;
		} else if ((c & 0xF4) == 0xF0) {
			i += 4;
		} else {
			i += 1;
		}
	}

	int low = 0;
	int high = (int)offsets.size() - 1;
	int best = 0;

	while (low <= high) {
		int mid = low + (high - low) / 2;
		std::string test = text.substr(0, offsets[mid]) + "...";
		if (measureText(test, scaleX, scaleY) <= maxWidth) {
			best = mid;
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}

	return text.substr(0, offsets[best]) + "...";
}

std::string getTruncatedRichText(const std::string &rawText, float maxWidth, float scaleX, float scaleY) {
	if (measureRichText(rawText, scaleX, scaleY) <= maxWidth) {
		return rawText;
	}

	std::vector<size_t> offsets;
	for (size_t i = 0; i < rawText.length();) {
		offsets.push_back(i);
		unsigned char c = (unsigned char)rawText[i];
		if (c < 0x80) {
			i += 1;
		} else if ((c & 0xE0) == 0xC0) {
			i += 2;
		} else if ((c & 0xF0) == 0xE0) {
			i += 3;
		} else if ((c & 0xF4) == 0xF0) {
			i += 4;
		} else {
			i += 1;
		}
	}

	int low = 0;
	int high = (int)offsets.size() - 1;
	int best = 0;

	while (low <= high) {
		int mid = low + (high - low) / 2;
		std::string test = rawText.substr(0, offsets[mid]) + "...";
		if (measureRichText(test, scaleX, scaleY) <= maxWidth) {
			best = mid;
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}

	return rawText.substr(0, offsets[best]) + "...";
}

void drawRichTextUnicodeOnly(float x, float y, float z, float scaleX, float scaleY, u32 color,
                             const std::string &rawText) {
	std::string text = Utils::Utf8::sanitizeText(rawText);

	if (!textBuf || text.empty()) {
		return;
	}

	size_t cursor = 0;
	float currentX = x;

	while (cursor < text.length()) {
		if (static_cast<unsigned char>(text[cursor]) >= 0x80) {
			size_t tempCharCursor = cursor;
			uint32_t firstCp = Utils::Utf8::decodeNext(text, tempCharCursor);

			if (Utils::Utf8::isEmoji(firstCp)) {
				size_t seqCursor = cursor;
				std::string sequence = Utils::Utf8::getEmojiSequence(text, seqCursor);
				std::string hex = Utils::Utf8::utf8ToHex(sequence);
				EmojiManager::EmojiInfo info = EmojiManager::getInstance().getTwemojiInfo(hex);
				float emojiSize = 28.0f * scaleY;

				if (info.tex) {
					Tex3DS_SubTexture subtex;
					subtex.width = (u16)info.originalW;
					subtex.height = (u16)info.originalH;
					subtex.left = 0.0f;
					subtex.top = 0.0f;
					subtex.right = (float)info.originalW / info.tex->width;
					subtex.bottom = (float)info.originalH / info.tex->height;

					const C2D_Image img = {info.tex, &subtex};
					C2D_DrawImageAt(img, currentX, y + 1.0f, z, nullptr, emojiSize / info.originalW,
					                emojiSize / info.originalH);
					currentX += emojiSize + (0.0f * scaleX);
				} else {
					std::string clean = Utils::Utf8::sanitizeText(sequence);
					drawText(currentX, y, z, scaleX, scaleY, color, clean);
					currentX += measureText(clean, scaleX, scaleY);
				}
				cursor = seqCursor;
				continue;
			}
		}

		{
			size_t end = cursor;
			while (end < text.length()) {
				if (static_cast<unsigned char>(text[end]) < 0x80) {
					end++;
					continue;
				}
				size_t nextC = end;
				uint32_t cp = Utils::Utf8::decodeNext(text, nextC);
				if (Utils::Utf8::isEmoji(cp)) {
					break;
				}
				end = nextC;
			}

			if (end > cursor) {
				std::string segment = text.substr(cursor, end - cursor);
				drawText(currentX, y, z, scaleX, scaleY, color, segment);
				currentX += measureText(segment, scaleX, scaleY);
				cursor = end;
			} else if (cursor < text.length()) {
				size_t nextC = cursor;
				Utils::Utf8::decodeNext(text, nextC);
				std::string segment = text.substr(cursor, nextC - cursor);
				drawText(currentX, y, z, scaleX, scaleY, color, segment);
				currentX += measureText(segment, scaleX, scaleY);
				cursor = nextC;
			}
		}
	}
}

float measureRichTextUnicodeOnly(const std::string &rawText, float scaleX, float scaleY) {
	return measureRichTextImpl(rawText, scaleX, scaleY, true);
}

u32 ScreenManager::colorBackground() { return Config::getInstance().getTheme().bg; }
u32 ScreenManager::colorBackgroundDark() { return Config::getInstance().getTheme().bg_dark; }
u32 ScreenManager::colorBackgroundLight() { return Config::getInstance().getTheme().bg_light; }
u32 ScreenManager::colorAccent() { return Config::getInstance().getTheme().accent; }
u32 ScreenManager::colorText() { return Config::getInstance().getTheme().text; }
u32 ScreenManager::colorTextMuted() { return Config::getInstance().getTheme().text_muted; }
u32 ScreenManager::colorSuccess() { return Config::getInstance().getTheme().success; }
u32 ScreenManager::colorError() { return Config::getInstance().getTheme().error; }

u32 ScreenManager::colorInput() { return Config::getInstance().getTheme().input_bg; }
u32 ScreenManager::colorLink() { return Config::getInstance().getTheme().link; }

u32 ScreenManager::colorSeparator() { return Config::getInstance().getTheme().separator; }

u32 ScreenManager::colorHeaderGlass() {
	u32 bg = colorBackgroundDark();
	u8 r = (bg >> 0) & 0xFF;
	u8 g = (bg >> 8) & 0xFF;
	u8 b = (bg >> 16) & 0xFF;
	return C2D_Color32(r, g, b, 230);
}

u32 ScreenManager::colorHeaderBorder() { return Config::getInstance().getTheme().header_border; }

u32 ScreenManager::colorSelection() { return Config::getInstance().getTheme().selection; }

u32 ScreenManager::colorOverlay() { return Config::getInstance().getTheme().overlay; }

u32 ScreenManager::colorWhite() { return Config::getInstance().getTheme().pure_white; }

u32 ScreenManager::colorEmbed() { return Config::getInstance().getTheme().embed_bg; }

u32 ScreenManager::colorEmbedMedia() { return Config::getInstance().getTheme().embed_media_bg; }

u32 ScreenManager::colorReaction() { return Config::getInstance().getTheme().reaction_bg; }

u32 ScreenManager::colorReactionMe() { return Config::getInstance().getTheme().reaction_me_bg; }

void ScreenManager::resetSelection() {
	lastServerIndex = 0;
	lastServerScroll = 0;
	lastChannelIndex.clear();
	lastChannelScroll.clear();
	lastForumIndex.clear();
	lastForumScroll.clear();
	selectedGuildId = "";
	expandedFolders.clear();
}

void ScreenManager::clearCaches() {
	Discord::AvatarCache::getInstance().clear();
	ImageManager::getInstance().clear();
}

void drawOverlay(float z) { C2D_DrawRectSolid(0.0f, 0.0f, z, 400.0f, 240.0f, ScreenManager::colorOverlay()); }

void drawPopupBackground(float x, float y, float w, float h, float z, float radius) {
	drawRoundedRect(x, y, z, w, h, radius, ScreenManager::colorBackground());
}

void drawPopupMenuItem(float x, float y, float w, float h, float z, bool isSelected, u32 selectionColor) {
	if (isSelected) {
		drawRoundedRect(x, y, z, w, h, 6.0f, selectionColor);
	}
}

} // namespace UI
