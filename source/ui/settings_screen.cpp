#include "ui/settings_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "ui/screen_manager.h"
#include "utils/message_utils.h"
#include "discord/account_settings.h"
#include "discord/avatar_cache.h"
#include "discord/discord_client.h"
#include "ui/file_browser_screen.h"
#include "ui/list_screen.h"
#include "ui/pin_lock_screen.h"
#include "ui/updater_screen.h"
#include "utils/updater.h"
#include <cstdlib>
#include <sys/stat.h>
#include <memory>
#include <3ds.h>
#include <algorithm>

namespace UI {

namespace {

using Discord::AccountSettings::Result;
using Discord::AccountSettings::Snapshot;

// What the server reported when the screen was first opened; kept up to date as options change.
Snapshot cachedSnapshot;
bool snapshotFetched = false;
std::string snapshotUserId;

bool promptText(const std::string &hint, const std::string &initial, std::string &out, int maxLen,
                bool multiline = false) {
	SwkbdState swkbd;
	swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, maxLen);
	swkbdSetFeatures(&swkbd, SWKBD_DARKEN_TOP_SCREEN | (multiline ? SWKBD_MULTILINE : 0));
	swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 0, 0);
	swkbdSetHintText(&swkbd, hint.c_str());
	if (!initial.empty()) {
		swkbdSetInitialText(&swkbd, initial.c_str());
	}
	std::vector<char> buffer(maxLen * 4 + 8, 0);
	if (swkbdInputText(&swkbd, buffer.data(), buffer.size()) != SWKBD_BUTTON_CONFIRM) {
		return false;
	}
	out = buffer.data();
	return true;
}

void report(const Result &r) {
	ScreenManager::getInstance().showToast(r.ok ? TR("account.saved") : r.message);
}

} // namespace

SettingsScreen::SettingsScreen()
    : selectedIndex(0), scrollOffset(0.0f), repeatTimer(0), lastKey(0), isDeveloperMode(false), popupActive(false),
      popupSelectedIndex(0), activePopupItem(nullptr), popupScrollOffset(0.0f) {}

SettingsScreen::~SettingsScreen() {}

void SettingsScreen::onEnter() {
	Logger::log("[SettingsScreen] Entered");
	activePopupItem = nullptr;
	allItems.clear();

	// GENERAL
	allItems.push_back({TR("settings.section.general"), "", SettingItemType::SECTION_HEADER});

	SettingItem language;
	language.label = TR("settings.language");
	language.description = TR("settings.desc.language");
	language.type = SettingItemType::INTEGER;
	std::string langCode = Config::getInstance().getLanguage();
	language.value = (langCode == "ja_JP")   ? 1
	                 : (langCode == "fr_FR") ? 2
	                 : (langCode == "es_ES") ? 3
	                 : (langCode == "it_IT") ? 4
	                 : (langCode == "de_DE") ? 5
	                 : (langCode == "pl_PL") ? 6
	                 : (langCode == "pt_BR") ? 7
	                 : (langCode == "zh_CN") ? 8
	                                         : 0;
	language.min = 0;
	language.max = 8;
	language.valueFormatter = [](int val) {
		if (val == 0) {
			return "English";
		}
		if (val == 1) {
			return "日本語";
		}
		if (val == 2) {
			return "Français";
		}
		if (val == 3) {
			return "Español";
		}
		if (val == 4) {
			return "Italiano";
		}
		if (val == 5) {
			return "Deutsch";
		}
		if (val == 6) {
			return "Polski";
		}
		if (val == 7) {
			return "Português (Brasil)";
		}
		if (val == 8) {
			return "简体中文";
		}
		return "English";
	};
	language.onUpdate = [this](int val) {
		std::string newLang = (val == 1)   ? "ja_JP"
		                      : (val == 2) ? "fr_FR"
		                      : (val == 3) ? "es_ES"
		                      : (val == 4) ? "it_IT"
		                      : (val == 5) ? "de_DE"
		                      : (val == 6) ? "pl_PL"
		                      : (val == 7) ? "pt_BR"
		                      : (val == 8) ? "zh_CN"
		                                   : "en_US";
		Config::getInstance().setLanguage(newLang);
		ScreenManager::getInstance().getHamburgerMenu().refreshStrings();
		this->scheduleRefresh = true;
	};
	allItems.push_back(language);

	SettingItem timezone;
	timezone.label = TR("settings.timezone");
	timezone.description = TR("settings.desc.timezone");
	timezone.type = SettingItemType::STEPPER;
	timezone.value = Config::getInstance().getTimezoneOffset();
	timezone.min = -12;
	timezone.max = 14;
	timezone.valueFormatter = [](int val) {
		std::string s = (val >= 0 ? "+" : "") + std::to_string(val);
		if (val == 0) {
			s += " (UTC/GMT)";
		} else if (val == 1) {
			s += " (CET)";
		} else if (val == -5) {
			s += " (EST)";
		} else if (val == -6) {
			s += " (CST)";
		} else if (val == -8) {
			s += " (PST)";
		} else if (val == 5) {
			s += " (IST)";
		} else if (val == 9) {
			s += " (JST/KST)";
		} else if (val == 10) {
			s += " (AEST)";
		}
		return s;
	};
	timezone.onUpdate = [](int val) { Config::getInstance().setTimezoneOffset(val); };
	allItems.push_back(timezone);

	// APPEARANCE
	allItems.push_back({TR("settings.section.appearance"), "", SettingItemType::SECTION_HEADER});

	SettingItem theme;
	theme.label = TR("settings.theme");
	theme.description = TR("settings.desc.theme");
	theme.type = SettingItemType::STEPPER;
	theme.value = Config::getInstance().getThemeType();
	theme.min = 0;
	theme.max = Config::THEME_PRESET_COUNT - 1;
	theme.valueFormatter = [](int val) -> std::string {
		static const char *names[Config::THEME_PRESET_COUNT] = {"settings.theme.dark",     "settings.theme.light",
		                                                         "settings.theme.amoled",   "settings.theme.nintendo",
		                                                         "settings.theme.retro",    "settings.theme.midnight"};
		return TR(names[std::clamp(val, 0, Config::THEME_PRESET_COUNT - 1)]);
	};
	theme.onUpdate = [](int val) { Config::getInstance().setThemeType(val); };
	allItems.push_back(theme);

	SettingItem themeEditor;
	themeEditor.label = TR("settings.theme_editor");
	themeEditor.description = TR("settings.desc.theme_editor");
	themeEditor.type = SettingItemType::ACTION;
	themeEditor.action = []() { ListScreen::open(ListScreen::Mode::THEME_EDIT); };
	allItems.push_back(themeEditor);

	SettingItem customThemeManager;
	customThemeManager.label = TR("settings.manage_themes");
	customThemeManager.description = TR("settings.desc.manage_themes");
	customThemeManager.type = SettingItemType::ACTION;
	customThemeManager.action = []() { ScreenManager::getInstance().pushScreen(ScreenType::THEME_MANAGER); };
	allItems.push_back(customThemeManager);

	SettingItem showAvatars;
	showAvatars.label = TR("settings.show_avatars");
	showAvatars.description = TR("settings.desc.show_avatars");
	showAvatars.type = SettingItemType::TOGGLE;
	showAvatars.value = Config::getInstance().isShowAvatarsEnabled() ? 1 : 0;
	showAvatars.min = 0;
	showAvatars.max = 1;
	showAvatars.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	showAvatars.onUpdate = [](int val) {
		Config::getInstance().setShowAvatarsEnabled(val == 1);
		Discord::AvatarCache::getInstance().clear();
	};
	allItems.push_back(showAvatars);

	SettingItem showIcons;
	showIcons.label = TR("settings.show_server_icons");
	showIcons.description = TR("settings.desc.show_server_icons");
	showIcons.type = SettingItemType::TOGGLE;
	showIcons.value = Config::getInstance().isShowServerIconsEnabled() ? 1 : 0;
	showIcons.min = 0;
	showIcons.max = 1;
	showIcons.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	showIcons.onUpdate = [](int val) {
		Config::getInstance().setShowServerIconsEnabled(val == 1);
		Discord::AvatarCache::getInstance().clear();
	};
	allItems.push_back(showIcons);

	SettingItem hiddenChannels;
	hiddenChannels.label = TR("settings.show_hidden_channels");
	hiddenChannels.description = TR("settings.desc.show_hidden_channels");
	hiddenChannels.type = SettingItemType::TOGGLE;
	hiddenChannels.value = Config::getInstance().isShowHiddenChannelsEnabled() ? 1 : 0;
	hiddenChannels.min = 0;
	hiddenChannels.max = 1;
	hiddenChannels.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	hiddenChannels.onUpdate = [](int val) { Config::getInstance().setShowHiddenChannelsEnabled(val == 1); };
	allItems.push_back(hiddenChannels);

	SettingItem imageCache;
	imageCache.label = TR("settings.image_cache");
	imageCache.description = TR("settings.desc.image_cache");
	imageCache.type = SettingItemType::STEPPER;
	imageCache.value = Config::getInstance().getImageCacheLevel();
	imageCache.min = 0;
	imageCache.max = 2;
	imageCache.valueFormatter = [](int val) -> std::string {
		return TR(val == 0 ? "settings.cache.small" : val == 2 ? "settings.cache.large" : "settings.cache.normal");
	};
	imageCache.onUpdate = [](int val) { Config::getInstance().setImageCacheLevel(val); };
	allItems.push_back(imageCache);

	// CHAT
	allItems.push_back({TR("settings.section.chat"), "", SettingItemType::SECTION_HEADER});

	SettingItem typing;
	typing.label = TR("settings.typing_indicator");
	typing.description = TR("settings.desc.typing_indicator");
	typing.type = SettingItemType::TOGGLE;
	typing.value = Config::getInstance().isTypingIndicatorEnabled() ? 1 : 0;
	typing.min = 0;
	typing.max = 1;
	typing.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	typing.onUpdate = [](int val) { Config::getInstance().setTypingIndicatorEnabled(val == 1); };
	allItems.push_back(typing);

	SettingItem notificationSound;
	notificationSound.label = TR("settings.notification_sound");
	notificationSound.description = TR("settings.desc.notification_sound");
	notificationSound.type = SettingItemType::TOGGLE;
	notificationSound.value = Config::getInstance().isNotificationSoundEnabled() ? 1 : 0;
	notificationSound.min = 0;
	notificationSound.max = 1;
	notificationSound.valueFormatter = [](int val) {
		return (val == 1) ? TR("common.enabled") : TR("common.disabled");
	};
	notificationSound.onUpdate = [](int val) { Config::getInstance().setNotificationSoundEnabled(val == 1); };
	allItems.push_back(notificationSound);

	SettingItem replyPing;
	replyPing.label = TR("settings.reply_ping");
	replyPing.description = TR("settings.desc.reply_ping");
	replyPing.type = SettingItemType::TOGGLE;
	replyPing.value = Config::getInstance().isReplyPingEnabled() ? 1 : 0;
	replyPing.min = 0;
	replyPing.max = 1;
	replyPing.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	replyPing.onUpdate = [](int val) { Config::getInstance().setReplyPingEnabled(val == 1); };
	allItems.push_back(replyPing);

	SettingItem dmUnread;
	dmUnread.label = TR("settings.dm_unread_first");
	dmUnread.description = TR("settings.desc.dm_unread_first");
	dmUnread.type = SettingItemType::TOGGLE;
	dmUnread.value = Config::getInstance().isDmUnreadFirst() ? 1 : 0;
	dmUnread.min = 0;
	dmUnread.max = 1;
	dmUnread.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	dmUnread.onUpdate = [](int val) { Config::getInstance().setDmUnreadFirst(val == 1); };
	allItems.push_back(dmUnread);

	// Quiet hours: no message sounds between two hours of the day.
	SettingItem quiet;
	quiet.label = TR("settings.quiet_hours");
	quiet.description = TR("settings.desc.quiet_hours");
	quiet.type = SettingItemType::TOGGLE;
	quiet.value = Config::getInstance().isQuietHoursEnabled() ? 1 : 0;
	quiet.min = 0;
	quiet.max = 1;
	quiet.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	quiet.onUpdate = [](int val) { Config::getInstance().setQuietHoursEnabled(val == 1); };
	allItems.push_back(quiet);

	auto hourFormatter = [](int val) {
		char buf[8];
		snprintf(buf, sizeof(buf), "%02d:00", val);
		return std::string(buf);
	};
	SettingItem quietFrom;
	quietFrom.label = TR("settings.quiet_from");
	quietFrom.description = TR("settings.desc.quiet_from");
	quietFrom.type = SettingItemType::STEPPER;
	quietFrom.value = Config::getInstance().getQuietStart();
	quietFrom.min = 0;
	quietFrom.max = 23;
	quietFrom.valueFormatter = hourFormatter;
	quietFrom.onUpdate = [](int val) {
		Config::getInstance().setQuietHours(val, Config::getInstance().getQuietEnd());
	};
	allItems.push_back(quietFrom);

	SettingItem quietTo;
	quietTo.label = TR("settings.quiet_to");
	quietTo.description = TR("settings.desc.quiet_to");
	quietTo.type = SettingItemType::STEPPER;
	quietTo.value = Config::getInstance().getQuietEnd();
	quietTo.min = 0;
	quietTo.max = 23;
	quietTo.valueFormatter = hourFormatter;
	quietTo.onUpdate = [](int val) {
		Config::getInstance().setQuietHours(Config::getInstance().getQuietStart(), val);
	};
	allItems.push_back(quietTo);

	addAccountItems();
	addUpdateItems();

	// SECURITY AND BACKUP
	allItems.push_back({TR("settings.section.security"), "", SettingItemType::SECTION_HEADER});

	SettingItem pin;
	pin.label = TR("settings.pin");
	pin.description = TR("settings.desc.pin");
	pin.type = SettingItemType::ACTION;
	pin.action = []() {
		auto &cfg = Config::getInstance();
		auto &sm = ScreenManager::getInstance();
		std::string current;
		if (cfg.hasPin()) {
			if (!promptPin(TR("pin.enter_current"), current)) {
				return;
			}
			if (!cfg.checkPin(current)) {
				sm.showToast(TR("pin.wrong"));
				return;
			}
		}
		std::string next;
		// With a PIN set, an empty answer cannot be typed: removing it has its own question.
		if (cfg.hasPin()) {
			SwkbdState ask;
			swkbdInit(&ask, SWKBD_TYPE_NORMAL, 2, 40);
			swkbdSetValidation(&ask, SWKBD_ANYTHING, 0, 0);
			swkbdSetHintText(&ask, TR("pin.remove_hint").c_str());
			char answer[160] = {0};
			if (swkbdInputText(&ask, answer, sizeof(answer)) != SWKBD_BUTTON_CONFIRM) {
				return;
			}
			if (std::string(answer) == "1") {
				cfg.setPin("");
				sm.showToast(TR("pin.removed"));
				return;
			}
		}
		if (!promptPin(TR("pin.enter_new"), next)) {
			return;
		}
		if (next.size() < 4) {
			sm.showToast(TR("pin.too_short"));
			return;
		}
		std::string again;
		if (!promptPin(TR("pin.repeat"), again)) {
			return;
		}
		if (again != next) {
			sm.showToast(TR("pin.mismatch"));
			return;
		}
		cfg.setPin(next);
		sm.showToast(TR("pin.set"));
	};
	allItems.push_back(pin);

	SettingItem sessions;
	sessions.label = TR("settings.sessions");
	sessions.description = TR("settings.desc.sessions");
	sessions.type = SettingItemType::ACTION;
	sessions.action = []() { ListScreen::open(ListScreen::Mode::SESSIONS); };
	allItems.push_back(sessions);

	SettingItem exportSettings;
	exportSettings.label = TR("settings.export");
	exportSettings.description = TR("settings.desc.export");
	exportSettings.type = SettingItemType::ACTION;
	exportSettings.action = []() {
		ScreenManager::getInstance().showToast(Config::getInstance().exportSettings() ? TR("settings.exported")
		                                                                                : TR("settings.export_failed"));
	};
	allItems.push_back(exportSettings);

	SettingItem importSettings;
	importSettings.label = TR("settings.import");
	importSettings.description = TR("settings.desc.import");
	importSettings.type = SettingItemType::ACTION;
	importSettings.action = []() {
		FileBrowserScreen::open(
		    TR("settings.import"), FileBrowserScreen::Filter::ANY,
		    [](const std::string &path) {
			    ScreenManager::getInstance().showToast(Config::getInstance().importSettings(path)
			                                               ? TR("settings.imported")
			                                               : TR("settings.import_failed"));
		    },
		    false, 256LL * 1024);
	};
	allItems.push_back(importSettings);

	// CONTROLS AND BATTERY
	allItems.push_back({TR("settings.section.controls"), "", SettingItemType::SECTION_HEADER});

	SettingItem lefty;
	lefty.label = TR("settings.left_handed");
	lefty.description = TR("settings.desc.left_handed");
	lefty.type = SettingItemType::TOGGLE;
	lefty.value = Config::getInstance().isLeftHanded() ? 1 : 0;
	lefty.min = 0;
	lefty.max = 1;
	lefty.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	lefty.onUpdate = [](int val) { Config::getInstance().setLeftHanded(val == 1); };
	allItems.push_back(lefty);

	SettingItem saver;
	saver.label = TR("settings.battery_saver");
	saver.description = TR("settings.desc.battery_saver");
	saver.type = SettingItemType::TOGGLE;
	saver.value = Config::getInstance().isBatterySaver() ? 1 : 0;
	saver.min = 0;
	saver.max = 1;
	saver.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	saver.onUpdate = [](int val) { Config::getInstance().setBatterySaver(val == 1); };
	allItems.push_back(saver);

	SettingItem light;
	light.label = TR("settings.light_mode");
	light.description = TR("settings.desc.light_mode");
	light.type = SettingItemType::TOGGLE;
	light.value = Config::getInstance().isLightMode() ? 1 : 0;
	light.min = 0;
	light.max = 1;
	light.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	light.onUpdate = [](int val) { Config::getInstance().setLightMode(val == 1); };
	allItems.push_back(light);

	// VOICE
	allItems.push_back({TR("settings.section.voice"), "", SettingItemType::SECTION_HEADER});

	SettingItem ptt;
	ptt.label = TR("settings.ptt");
	ptt.description = TR("settings.desc.ptt");
	ptt.type = SettingItemType::STEPPER;
	ptt.value = Config::getInstance().getPushToTalk();
	ptt.min = 0;
	ptt.max = 2;
	ptt.valueFormatter = [](int val) -> std::string {
		return TR(val == 1 ? "settings.ptt.r" : val == 2 ? "settings.ptt.zr" : "settings.ptt.off");
	};
	ptt.onUpdate = [](int val) { Config::getInstance().setPushToTalk(val); };
	allItems.push_back(ptt);

	SettingItem callVol;
	callVol.label = TR("settings.call_volume");
	callVol.description = TR("settings.desc.call_volume");
	callVol.type = SettingItemType::STEPPER;
	callVol.value = Config::getInstance().getCallVolume() / 10;
	callVol.min = 2;
	callVol.max = 20;
	callVol.valueFormatter = [](int val) -> std::string { return std::to_string(val * 10) + "%"; };
	callVol.onUpdate = [](int val) { Config::getInstance().setCallVolume(val * 10); };
	allItems.push_back(callVol);

	SettingItem busy;
	busy.label = TR("settings.busy_mode");
	busy.description = TR("settings.desc.busy_mode");
	busy.type = SettingItemType::TOGGLE;
	busy.value = Config::getInstance().isBusyMode() ? 1 : 0;
	busy.min = 0;
	busy.max = 1;
	busy.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	busy.onUpdate = [](int val) { Config::getInstance().setBusyMode(val == 1); };
	allItems.push_back(busy);

	SettingItem reconnect;
	reconnect.label = TR("settings.auto_reconnect");
	reconnect.description = TR("settings.desc.auto_reconnect");
	reconnect.type = SettingItemType::TOGGLE;
	reconnect.value = Config::getInstance().isAutoReconnect() ? 1 : 0;
	reconnect.min = 0;
	reconnect.max = 1;
	reconnect.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	reconnect.onUpdate = [](int val) { Config::getInstance().setAutoReconnect(val == 1); };
	allItems.push_back(reconnect);

	SettingItem gate;
	gate.label = TR("settings.noise_gate");
	gate.description = TR("settings.desc.noise_gate");
	gate.type = SettingItemType::STEPPER;
	gate.value = Config::getInstance().getNoiseGate();
	gate.min = 0;
	gate.max = 10;
	gate.valueFormatter = [](int val) -> std::string {
		return std::to_string(val) + (val == 0 ? " (" + TR("settings.gate.sensitive") + ")"
		                              : val == 5 ? " (" + TR("settings.gate.normal") + ")"
		                              : val == 10 ? " (" + TR("settings.gate.strict") + ")"
		                                          : std::string());
	};
	gate.onUpdate = [](int val) { Config::getInstance().setNoiseGate(val); };
	allItems.push_back(gate);

	SettingItem awake;
	awake.label = TR("settings.keep_awake");
	awake.description = TR("settings.desc.keep_awake");
	awake.type = SettingItemType::TOGGLE;
	awake.value = Config::getInstance().isKeepCallsAwake() ? 1 : 0;
	awake.min = 0;
	awake.max = 1;
	awake.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	awake.onUpdate = [](int val) { Config::getInstance().setKeepCallsAwake(val == 1); };
	allItems.push_back(awake);

	SettingItem ringtone;
	ringtone.label = TR("settings.ringtone");
	ringtone.description = TR("settings.desc.ringtone");
	ringtone.type = SettingItemType::ACTION;
	ringtone.action = []() { ListScreen::openSounds("call_ringtone", TR("settings.ringtone")); };
	allItems.push_back(ringtone);

	// DEVELOPER
	SettingItem devSection;
	devSection.label = "Developer Options";
	devSection.type = SettingItemType::SECTION_HEADER;
	devSection.isDeveloper = true;
	allItems.push_back(devSection);

	SettingItem fileLogging;
	fileLogging.label = "Save Logs to File";
	fileLogging.description = "Save debug logs to SD card (sdmc:/3ds/3DScord/3dscord.log)";
	fileLogging.type = SettingItemType::TOGGLE;
	fileLogging.value = Config::getInstance().isFileLoggingEnabled() ? 1 : 0;
	fileLogging.min = 0;
	fileLogging.max = 1;
	fileLogging.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	fileLogging.onUpdate = [](int val) { Config::getInstance().setFileLoggingEnabled(val == 1); };
	fileLogging.isDeveloper = true;
	allItems.push_back(fileLogging);

	refreshVisibleItems();
}

void SettingsScreen::addAccountItems() {
	using namespace Discord;
	auto &client = DiscordClient::getInstance();
	if (client.getState() != ConnectionState::READY) {
		return;
	}
	const User me = client.getCurrentUser();
	if (!snapshotFetched || snapshotUserId != me.id) {
		cachedSnapshot = AccountSettings::fetch();
		snapshotFetched = true;
		snapshotUserId = me.id;
	}


	auto addAction = [this](const std::string &label, const std::string &desc, std::function<void()> fn) {
		SettingItem item;
		item.label = label;
		item.description = desc;
		item.type = SettingItemType::ACTION;
		item.action = std::move(fn);
		allItems.push_back(item);
	};

	auto addToggle = [this](const std::string &label, const std::string &desc, const char *apiKey,
	                        bool Snapshot::*field) {
		SettingItem item;
		item.label = label;
		item.description = desc;
		item.type = SettingItemType::TOGGLE;
		item.value = (cachedSnapshot.*field) ? 1 : 0;
		item.min = 0;
		item.max = 1;
		item.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
		item.onUpdate = [this, apiKey, field](int val) {
			Result r = AccountSettings::setBoolSetting(apiKey, val == 1);
			if (r.ok) {
				cachedSnapshot.*field = (val == 1);
			} else {
				scheduleRefresh = true;
			}
			report(r);
		};
		allItems.push_back(item);
	};

	// Avatar and banner: pick the picture on the SD card, or remove the current one.
	auto addImageActions = [&addAction](const std::string &label, const std::string &removeLabel,
	                                    const std::string &desc, bool banner) {
		addAction(label, desc, [label, banner]() {
			FileBrowserScreen::open(label, FileBrowserScreen::Filter::IMAGES, [banner](const std::string &path) {
				ScreenManager::getInstance().showToast(TR("account.uploading"));
				report(banner ? Discord::AccountSettings::setBannerFromFile(path)
				              : Discord::AccountSettings::setAvatarFromFile(path));
			}, false, 1536LL * 1024);
		});
		addAction(removeLabel, "", [banner]() {
			report(banner ? Discord::AccountSettings::removeBanner() : Discord::AccountSettings::removeAvatar());
		});
	};

	// PROFILE
	allItems.push_back({TR("settings.section.profile"), "", SettingItemType::SECTION_HEADER});

	addAction(TR("account.display_name"), TR("account.desc.display_name"), [me]() {
		std::string text;
		if (promptText(TR("account.display_name"), me.global_name, text, 32)) {
			report(Discord::AccountSettings::setDisplayName(text));
		}
	});
	addAction(TR("account.bio"), TR("account.desc.bio"), []() {
		std::string text;
		if (promptText(TR("account.bio"), cachedSnapshot.bio, text, 190, true)) {
			Result r = Discord::AccountSettings::setBio(text);
			if (r.ok) {
				cachedSnapshot.bio = text;
			}
			report(r);
		}
	});
	addAction(TR("account.pronouns"), TR("account.desc.pronouns"), []() {
		std::string text;
		if (promptText(TR("account.pronouns"), cachedSnapshot.pronouns, text, 40)) {
			Result r = Discord::AccountSettings::setPronouns(text);
			if (r.ok) {
				cachedSnapshot.pronouns = text;
			}
			report(r);
		}
	});
	addAction(TR("account.accent_color"), TR("account.desc.accent_color"), []() {
		char initial[16] = "";
		if (cachedSnapshot.accentColor >= 0) {
			snprintf(initial, sizeof(initial), "%06X", cachedSnapshot.accentColor & 0xFFFFFF);
		}
		std::string text;
		if (!promptText("RRGGBB", initial, text, 7)) {
			return;
		}
		if (!text.empty() && text[0] == '#') {
			text.erase(0, 1);
		}
		int rgb = -1;
		if (!text.empty()) {
			char *end = nullptr;
			long v = strtol(text.c_str(), &end, 16);
			if (*end != '\0' || text.size() != 6) {
				ScreenManager::getInstance().showToast(TR("account.invalid_color"));
				return;
			}
			rgb = (int)v;
		}
		Result r = Discord::AccountSettings::setAccentColor(rgb);
		if (r.ok) {
			cachedSnapshot.accentColor = rgb;
		}
		report(r);
	});
	addAction(TR("account.theme_colors"), TR("account.desc.theme_colors"), []() {
		// Two colours, asked one after the other. Leaving the first empty removes the theme.
		auto ask = [](const char *hint, int &out) -> bool {
			std::string text;
			if (!promptText(hint, "", text, 7)) {
				return false;
			}
			if (!text.empty() && text[0] == '#') {
				text.erase(0, 1);
			}
			if (text.empty()) {
				out = -1;
				return true;
			}
			char *end = nullptr;
			long v = strtol(text.c_str(), &end, 16);
			if (*end != '\0' || text.size() != 6) {
				ScreenManager::getInstance().showToast(TR("account.invalid_color"));
				return false;
			}
			out = (int)v;
			return true;
		};
		int primary = -1;
		int accent = -1;
		if (!ask("RRGGBB (1/2)", primary)) {
			return;
		}
		if (primary >= 0 && !ask("RRGGBB (2/2)", accent)) {
			return;
		}
		report(Discord::AccountSettings::setThemeColors(primary, accent));
	});
	addImageActions(TR("account.avatar"), TR("account.remove_avatar"), TR("account.desc.images"), false);
	addImageActions(TR("account.banner"), TR("account.remove_banner"), TR("account.desc.images"), true);

	// STATUS
	allItems.push_back({TR("settings.section.status"), "", SettingItemType::SECTION_HEADER});

	SettingItem status;
	status.label = TR("account.status");
	status.description = TR("account.desc.status");
	status.type = SettingItemType::INTEGER;
	status.value = me.status == UserStatus::IDLE ? 1 : me.status == UserStatus::DND ? 2
	               : me.status == UserStatus::INVISIBLE ? 3 : 0;
	status.min = 0;
	status.max = 3;
	status.valueFormatter = [](int val) -> std::string {
		return TR(val == 1 ? "account.status.idle"
		                   : val == 2 ? "account.status.dnd" : val == 3 ? "account.status.invisible" : "account.status.online");
	};
	status.onUpdate = [](int val) {
		DiscordClient::getInstance().updatePresence(val == 1   ? UserStatus::IDLE
		                                            : val == 2 ? UserStatus::DND
		                                            : val == 3 ? UserStatus::INVISIBLE
		                                                       : UserStatus::ONLINE);
	};
	allItems.push_back(status);

	addAction(TR("account.custom_status"), TR("account.desc.custom_status"), []() {
		std::string text;
		if (promptText(TR("account.custom_status"), cachedSnapshot.customStatus, text, 128)) {
			Result r = Discord::AccountSettings::setCustomStatus(text);
			if (r.ok) {
				cachedSnapshot.customStatus = text;
			}
			report(r);
		}
	});
	addAction(TR("account.timed_status"), TR("account.desc.timed_status"), []() {
		std::string text;
		if (!promptText(TR("account.custom_status"), "", text, 128) || text.empty()) {
			return;
		}
		std::string minutes;
		if (!promptText(TR("account.status_minutes"), "60", minutes, 5)) {
			return;
		}
		const int m = std::clamp(atoi(minutes.c_str()), 1, 10080);
		Result r = Discord::AccountSettings::setCustomStatus(text, m);
		if (r.ok) {
			cachedSnapshot.customStatus = text;
		}
		report(r);
	});
	addToggle(TR("account.show_current_game"), TR("account.desc.show_current_game"), "show_current_game",
	          &Snapshot::showCurrentGame);

	// DISCORD CHAT
	allItems.push_back({TR("settings.section.discord_chat"), "", SettingItemType::SECTION_HEADER});
	addToggle(TR("account.compact"), TR("account.desc.compact"), "message_display_compact",
	          &Snapshot::compactMessages);
	addToggle(TR("account.render_embeds"), TR("account.desc.render_embeds"), "render_embeds", &Snapshot::renderEmbeds);
	addToggle(TR("account.render_reactions"), TR("account.desc.render_reactions"), "render_reactions",
	          &Snapshot::renderReactions);
	addToggle(TR("account.animate_emoji"), TR("account.desc.animate_emoji"), "animate_emoji", &Snapshot::animateEmoji);
	addToggle(TR("account.gif_auto_play"), TR("account.desc.gif_auto_play"), "gif_auto_play", &Snapshot::gifAutoPlay);
	addToggle(TR("account.inline_embed_media"), TR("account.desc.inline_embed_media"), "inline_embed_media",
	          &Snapshot::inlineEmbedMedia);
	addToggle(TR("account.inline_attachment_media"), TR("account.desc.inline_attachment_media"),
	          "inline_attachment_media", &Snapshot::inlineAttachmentMedia);
	addToggle(TR("account.convert_emoticons"), TR("account.desc.convert_emoticons"), "convert_emoticons",
	          &Snapshot::convertEmoticons);
	addToggle(TR("account.developer_mode"), TR("account.desc.developer_mode"), "developer_mode",
	          &Snapshot::developerMode);

	// PRIVACY
	allItems.push_back({TR("settings.section.privacy"), "", SettingItemType::SECTION_HEADER});
	addToggle(TR("account.restrict_dms"), TR("account.desc.restrict_dms"), "default_guilds_restricted",
	          &Snapshot::restrictNewServerDms);

	SettingItem filter;
	filter.label = TR("account.explicit_filter");
	filter.description = TR("account.desc.explicit_filter");
	filter.type = SettingItemType::INTEGER;
	filter.value = cachedSnapshot.explicitContentFilter;
	filter.min = 0;
	filter.max = 2;
	filter.valueFormatter = [](int val) -> std::string {
		return TR(val == 0 ? "account.filter.off" : val == 1 ? "account.filter.no_roles" : "account.filter.all");
	};
	filter.onUpdate = [this](int val) {
		Result r = Discord::AccountSettings::setIntSetting("explicit_content_filter", val);
		if (r.ok) {
			cachedSnapshot.explicitContentFilter = val;
		} else {
			scheduleRefresh = true;
		}
		report(r);
	};
	allItems.push_back(filter);

	SettingItem theme;
	theme.label = TR("account.discord_theme");
	theme.description = TR("account.desc.discord_theme");
	theme.type = SettingItemType::STEPPER;
	theme.value = cachedSnapshot.lightTheme ? 1 : 0;
	theme.min = 0;
	theme.max = 1;
	theme.valueFormatter = [](int val) { return val == 1 ? TR("settings.theme.light") : TR("settings.theme.dark"); };
	theme.onUpdate = [this](int val) {
		Result r = Discord::AccountSettings::setThemeSetting(val == 1);
		if (r.ok) {
			cachedSnapshot.lightTheme = (val == 1);
		} else {
			scheduleRefresh = true;
		}
		report(r);
	};
	allItems.push_back(theme);
}

void SettingsScreen::addUpdateItems() {
	allItems.push_back({TR("settings.section.updates"), "", SettingItemType::SECTION_HEADER});

	SettingItem version;
	version.label = TR("updater.version") + ": " + Utils::Updater::currentVersionString();
	version.description = TR("updater.desc.version");
	version.type = SettingItemType::ACTION;
	allItems.push_back(version);

	SettingItem check;
	check.label = TR("updater.check_now");
	check.description = TR("updater.desc.check_now");
	check.type = SettingItemType::ACTION;
	check.action = []() {
		UpdaterScreen::downgrade = false;
		UpdaterScreen::manualCheck = true;
		ScreenManager::getInstance().setScreen(ScreenType::UPDATER);
	};
	allItems.push_back(check);

	SettingItem previous;
	previous.label = TR("updater.previous");
	previous.description = TR("updater.desc.previous");
	previous.type = SettingItemType::ACTION;
	previous.action = []() {
		UpdaterScreen::manualCheck = true;
		UpdaterScreen::downgrade = true;
		ScreenManager::getInstance().setScreen(ScreenType::UPDATER);
	};
	allItems.push_back(previous);

	SettingItem diagnostics;
	diagnostics.label = TR("settings.diagnostics");
	diagnostics.description = TR("settings.desc.diagnostics");
	diagnostics.type = SettingItemType::ACTION;
	diagnostics.action = []() { ListScreen::open(ListScreen::Mode::DIAG); };
	allItems.push_back(diagnostics);

	SettingItem autoUpdate;
	autoUpdate.label = TR("updater.auto");
	autoUpdate.description = TR("updater.desc.auto");
	autoUpdate.type = SettingItemType::TOGGLE;
	autoUpdate.value = Config::getInstance().isAutoUpdateEnabled() ? 1 : 0;
	autoUpdate.min = 0;
	autoUpdate.max = 1;
	autoUpdate.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	autoUpdate.onUpdate = [](int val) { Config::getInstance().setAutoUpdateEnabled(val == 1); };
	allItems.push_back(autoUpdate);

	SettingItem dev;
	dev.label = TR("updater.devbuilds");
	dev.description = TR("updater.desc.devbuilds");
	dev.type = SettingItemType::TOGGLE;
	dev.value = Config::getInstance().isDevBuildUpdatesEnabled() ? 1 : 0;
	dev.min = 0;
	dev.max = 1;
	dev.valueFormatter = [](int val) { return (val == 1) ? TR("common.enabled") : TR("common.disabled"); };
	dev.onUpdate = [](int val) { Config::getInstance().setDevBuildUpdatesEnabled(val == 1); };
	allItems.push_back(dev);
}

void SettingsScreen::refreshVisibleItems() {
	visibleItems.clear();
	std::string lowerQuery = searchQuery;
	std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

	for (auto &item : allItems) {
		if (item.isDeveloper && !isDeveloperMode) {
			continue;
		}

		if (item.type == SettingItemType::SECTION_HEADER) {
			if (lowerQuery.empty()) {
				visibleItems.push_back(&item);
			}
			continue;
		}

		if (!lowerQuery.empty()) {
			std::string lowerLabel = item.label;
			std::transform(lowerLabel.begin(), lowerLabel.end(), lowerLabel.begin(), ::tolower);
			std::string lowerDesc = item.description;
			std::transform(lowerDesc.begin(), lowerDesc.end(), lowerDesc.begin(), ::tolower);

			if (lowerLabel.find(lowerQuery) == std::string::npos && lowerDesc.find(lowerQuery) == std::string::npos) {
				continue;
			}
		}

		visibleItems.push_back(&item);
	}

	if (visibleItems.empty()) {
		selectedIndex = -1;
	} else {
		if (selectedIndex >= (int)visibleItems.size()) {
			selectedIndex = (int)visibleItems.size() - 1;
		}
		if (selectedIndex >= 0 && visibleItems[selectedIndex]->type == SettingItemType::SECTION_HEADER) {
			int next = selectedIndex + 1;
			while (next < (int)visibleItems.size() && visibleItems[next]->type == SettingItemType::SECTION_HEADER) {
				next++;
			}
			if (next < (int)visibleItems.size()) {
				selectedIndex = next;
			} else {
				int prev = selectedIndex - 1;
				while (prev >= 0 && visibleItems[prev]->type == SettingItemType::SECTION_HEADER) {
					prev--;
				}
				selectedIndex = prev;
			}
		}
	}
}

void SettingsScreen::onExit() { Logger::log("[SettingsScreen] Exited"); }

void SettingsScreen::update() {
	u32 kDown = hidKeysDown();
	u32 kHeld = hidKeysHeld();

	if (kDown & (KEY_DOWN | KEY_UP)) {
		repeatTimer = 30;
		lastKey = (kDown & KEY_DOWN) ? KEY_DOWN : KEY_UP;
	}
	if (!(kHeld & (KEY_DOWN | KEY_UP))) {
		lastKey = 0;
	}

	u32 moveDir = 0;
	if (kDown & KEY_DOWN) {
		moveDir = KEY_DOWN;
	} else if (kDown & KEY_UP) {
		moveDir = KEY_UP;
	} else if (kHeld & KEY_DOWN && (--repeatTimer <= 0)) {
		moveDir = KEY_DOWN;
		repeatTimer = 6;
	} else if (kHeld & KEY_UP && (--repeatTimer <= 0)) {
		moveDir = KEY_UP;
		repeatTimer = 6;
	}

	if (popupActive && activePopupItem) {
		if (moveDir & KEY_UP) {
			if (popupSelectedIndex > 0) {
				popupSelectedIndex--;
				if (popupSelectedIndex < (int)popupScrollOffset) {
					popupScrollOffset = (float)popupSelectedIndex;
				}
			}
		} else if (moveDir & KEY_DOWN) {
			int maxVisible = 5;
			int totalOpts = activePopupItem->max - activePopupItem->min + 1;
			if (popupSelectedIndex < totalOpts - 1) {
				popupSelectedIndex++;
				if (popupSelectedIndex >= (int)popupScrollOffset + maxVisible) {
					popupScrollOffset = (float)(popupSelectedIndex - maxVisible + 1);
				}
			}
		} else if (kDown & KEY_A) {
			activePopupItem->value = popupSelectedIndex + activePopupItem->min;
			if (activePopupItem->onUpdate) {
				activePopupItem->onUpdate(activePopupItem->value);
			}
			popupActive = false;
		} else if (kDown & KEY_B) {
			popupActive = false;
		}
		return;
	}

	if (kDown & KEY_Y) {
		SwkbdState swkbd;
		swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 1, -1);
		swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT | SWKBD_DARKEN_TOP_SCREEN);
		swkbdSetHintText(&swkbd, "Search Settings");
		char resultText[256] = {0};
		SwkbdButton button = swkbdInputText(&swkbd, resultText, sizeof(resultText));
		if (button == SWKBD_BUTTON_CONFIRM) {
			std::string query(resultText);
			if (query == "devmode" && !isDeveloperMode) {
				isDeveloperMode = true;
				searchQuery = "";
				ScreenManager::getInstance().showToast("!!! Avoid use without a specific reason !!!");
			} else {
				searchQuery = query;
			}
			selectedIndex = 0;
			scrollOffset = 0.0f;
			refreshVisibleItems();
		}
		return;
	}

	if (kDown & KEY_X) {
		if (!searchQuery.empty()) {
			searchQuery = "";
			selectedIndex = 0;
			scrollOffset = 0.0f;
			refreshVisibleItems();
		}
	}

	if (visibleItems.empty()) {
		if (kDown & KEY_B) {
			if (!searchQuery.empty()) {
				searchQuery = "";
				selectedIndex = 0;
				scrollOffset = 0.0f;
				refreshVisibleItems();
			} else {
				saveAndExit();
			}
		}
		return;
	}

	if (moveDir & KEY_UP) {
		int next = selectedIndex - 1;
		while (next >= 0 && visibleItems[next]->type == SettingItemType::SECTION_HEADER) {
			next--;
		}
		if (next >= 0) {
			selectedIndex = next;
			if (selectedIndex < (float)scrollOffset) {
				scrollOffset = (float)selectedIndex;
				// look back to include header if possible
				if (selectedIndex > 0 && visibleItems[selectedIndex - 1]->type == SettingItemType::SECTION_HEADER) {
					scrollOffset = (float)(selectedIndex - 1);
				}
			}
		}
	} else if (moveDir & KEY_DOWN) {
		int next = selectedIndex + 1;
		while (next < (int)visibleItems.size() && visibleItems[next]->type == SettingItemType::SECTION_HEADER) {
			next++;
		}
		if (next < (int)visibleItems.size()) {
			selectedIndex = next;
			if (selectedIndex >= (int)scrollOffset + 5) {
				scrollOffset = (float)(selectedIndex - 4);
			}
		}
	}

	if (kDown & KEY_A && selectedIndex >= 0) {
		SettingItem *selected = visibleItems[selectedIndex];
		if (selected->type == SettingItemType::INTEGER) {
			popupActive = true;
			popupSelectedIndex = selected->value - selected->min;
			activePopupItem = selected;
			popupScrollOffset = 0.0f;
			if (popupSelectedIndex > 3) {
				popupScrollOffset = (float)(popupSelectedIndex - 3);
			}
		} else if (selected->type == SettingItemType::TOGGLE) {
			selected->value = (selected->value == 1) ? 0 : 1;
			if (selected->onUpdate) {
				selected->onUpdate(selected->value);
			}
		} else if (selected->type == SettingItemType::ACTION) {
			if (selected->action) {
				selected->action();
				return;
			}
		}
	}

	if (kDown & (KEY_LEFT | KEY_RIGHT) && selectedIndex >= 0) {
		SettingItem *selected = visibleItems[selectedIndex];
		if (selected->type == SettingItemType::STEPPER) {
			if (kDown & KEY_RIGHT) {
				if (selected->value < selected->max) {
					selected->value++;
					if (selected->onUpdate) {
						selected->onUpdate(selected->value);
					}
				}
			} else if (kDown & KEY_LEFT) {
				if (selected->value > selected->min) {
					selected->value--;
					if (selected->onUpdate) {
						selected->onUpdate(selected->value);
					}
				}
			}
		}
	}

	if (kDown & KEY_B) {
		if (!searchQuery.empty()) {
			searchQuery = "";
			selectedIndex = 0;
			scrollOffset = 0.0f;
			refreshVisibleItems();
		} else {
			saveAndExit();
			return;
		}
	}

	if (scheduleRefresh) {
		scheduleRefresh = false;
		this->onEnter();
	}
}

void SettingsScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackground());
	C2D_SceneBegin(target);

	float headerH = 26.0f;
	C2D_DrawRectSolid(0, 0, 0.9f, TOP_SCREEN_WIDTH, headerH, ScreenManager::colorHeaderGlass());
	C2D_DrawRectSolid(0, headerH - 1.0f, 0.91f, TOP_SCREEN_WIDTH, 1.0f, ScreenManager::colorHeaderBorder());

	std::string titleText = TR("settings.title");
	if (!searchQuery.empty()) {
		titleText += " - Search: " + searchQuery;
	}
	drawCenteredText(4.0f, 0.95f, 0.52f, 0.52f, ScreenManager::colorText(), titleText, TOP_SCREEN_WIDTH);

	float y = headerH + 6.0f;

	for (int i = (int)scrollOffset; i < (int)visibleItems.size(); i++) {
		const auto *item = visibleItems[i];
		bool isSelected = (i == selectedIndex);

		if (item->type == SettingItemType::SECTION_HEADER) {
			drawText(20.0f, y + 4.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), item->label);
			C2D_DrawRectSolid(15.0f, y + 18.0f, 0.5f, TOP_SCREEN_WIDTH - 30.0f, 1.0f, ScreenManager::colorSeparator());
			y += 26.0f;
		} else {
			float itemHeight = 36.0f;
			if (isSelected) {
				drawRoundedRect(10, y, 0.5f, TOP_SCREEN_WIDTH - 20, itemHeight, 8.0f,
				                ScreenManager::colorBackgroundLight());
				drawRoundedRect(10, y + 4, 0.55f, 4, itemHeight - 8.0f, 2.0f, ScreenManager::colorSelection());
			} else {
				drawRoundedRect(10, y, 0.5f, TOP_SCREEN_WIDTH - 20, itemHeight, 8.0f,
				                ScreenManager::colorBackgroundDark());
			}

			u32 textColor = isSelected ? ScreenManager::colorText() : ScreenManager::colorTextMuted();
			drawText(25.0f, y + 9.0f, 0.6f, 0.45f, 0.45f, textColor, item->label);

			float centerX = TOP_SCREEN_WIDTH - 80.0f;
			std::string valStr = item->valueFormatter ? item->valueFormatter(item->value) : "";

			if (item->type == SettingItemType::TOGGLE) {
				bool isOn = (item->value == 1);
				float toggleW = 34.0f;
				float toggleH = 18.0f;
				float toggleX = TOP_SCREEN_WIDTH - 30.0f - toggleW;
				float toggleY = y + (itemHeight - toggleH) / 2.0f;
				u32 tBg = isOn ? ScreenManager::colorSelection() : ScreenManager::colorSeparator();
				drawRoundedRect(toggleX, toggleY, 0.6f, toggleW, toggleH, toggleH / 2.0f, tBg);
				float circSize = 14.0f;
				float circX = isOn ? (toggleX + toggleW - circSize - 2.0f) : (toggleX + 2.0f);
				drawCircle(circX + circSize / 2.0f, toggleY + circSize / 2.0f + 2.0f, 0.65f, circSize / 2.0f,
				           ScreenManager::colorWhite());
			} else if (item->type == SettingItemType::INTEGER || item->type == SettingItemType::STEPPER) {
				float valWidth = measureText(valStr, 0.45f, 0.45f);

				if (isSelected && item->type == SettingItemType::STEPPER) {
					u32 arrowColor = ScreenManager::colorSelection();
					if (item->value > item->min) {
						drawText(centerX - 55.0f, y + 9.0f, 0.6f, 0.45f, 0.45f, arrowColor, "<");
					}
					if (item->value < item->max) {
						drawText(centerX + 45.0f, y + 9.0f, 0.6f, 0.45f, 0.45f, arrowColor, ">");
					}
				}

				drawText(centerX - (valWidth / 2.0f), y + 9.0f, 0.6f, 0.45f, 0.45f, textColor, valStr);
			} else if (item->type == SettingItemType::ACTION) {
				if (!valStr.empty()) {
					float valWidth = measureText(valStr, 0.45f, 0.45f);
					drawText(centerX - (valWidth / 2.0f), y + 9.0f, 0.6f, 0.45f, 0.45f, textColor, valStr);
				} else {
					drawText(centerX, y + 9.0f, 0.6f, 0.45f, 0.45f, textColor, ">");
				}
			}

			y += itemHeight + 6.0f;
		}
		if (y > TOP_SCREEN_HEIGHT) {
			break;
		}
	}

	if (popupActive && activePopupItem) {
		C2D_DrawRectSolid(0, 0, 0.8f, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT, C2D_Color32(0, 0, 0, 160));

		float pWidth = 240.0f;
		float pHeight = 180.0f;
		float pX = (TOP_SCREEN_WIDTH - pWidth) / 2.0f;
		float pY = (TOP_SCREEN_HEIGHT - pHeight) / 2.0f;

		drawRoundedRect(pX, pY, 0.85f, pWidth, pHeight, 10.0f, ScreenManager::colorBackground());

		drawCenteredText(pY + 10.0f, 0.9f, 0.5f, 0.5f, ScreenManager::colorText(), activePopupItem->label,
		                 TOP_SCREEN_WIDTH);
		C2D_DrawRectSolid(pX + 10.0f, pY + 30.0f, 0.9f, pWidth - 20.0f, 1.0f, ScreenManager::colorSeparator());

		float optY = pY + 35.0f;
		int totalOpts = activePopupItem->max - activePopupItem->min + 1;
		for (int i = (int)popupScrollOffset; i < totalOpts; i++) {
			bool isOptSelected = (i == popupSelectedIndex);
			if (isOptSelected) {
				drawRoundedRect(pX + 5.0f, optY, 0.88f, pWidth - 10.0f, 26.0f, 4.0f, ScreenManager::colorSelection());
			}
			std::string label = activePopupItem->valueFormatter
			                        ? activePopupItem->valueFormatter(i + activePopupItem->min)
			                        : std::to_string(i + activePopupItem->min);
			u32 tc = isOptSelected ? ScreenManager::colorWhite() : ScreenManager::colorText();
			drawCenteredText(optY + 5.0f, 0.9f, 0.45f, 0.45f, tc, label, TOP_SCREEN_WIDTH);
			optY += 28.0f;
			if (optY > pY + pHeight - 20.0f) {
				break;
			}
		}

		if (totalOpts > 5) {
			float barArea = pHeight - 40.0f;
			float thumbH = std::max(10.0f, barArea * (5.0f / totalOpts));
			float maxScroll = totalOpts - 5.0f;
			float thumbY = pY + 35.0f + (barArea - thumbH) * (std::min(popupScrollOffset, maxScroll) / maxScroll);
			drawRoundedRect(pX + pWidth - 8.0f, thumbY, 0.91f, 4.0f, thumbH, 2.0f, ScreenManager::colorTextMuted());
		}
	}
}

void SettingsScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);

	if (!visibleItems.empty() && selectedIndex >= 0 && selectedIndex < (int)visibleItems.size()) {
		const auto *item = visibleItems[selectedIndex];

		if (item->type != SettingItemType::SECTION_HEADER) {
			drawText(35.0f, 10.0f, 0.6f, 0.5f, 0.5f, ScreenManager::colorText(), item->label);

			C2D_DrawRectSolid(10, 32, 0.5f, BOTTOM_SCREEN_WIDTH - 20, 1, ScreenManager::colorSeparator());

			if (!item->description.empty()) {
				float descY = 40.0f;
				auto lines = MessageUtils::wrapText(item->description, BOTTOM_SCREEN_WIDTH - 20, 0.5f);
				for (const auto &line : lines) {
					drawText(10.0f, descY, 0.6f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), line);
					descY += 15.0f;
				}
			}
		}
	} else if (!searchQuery.empty() && visibleItems.empty()) {
		drawCenteredText(BOTTOM_SCREEN_HEIGHT / 2.0f - 10.0f, 0.6f, 0.5f, 0.5f, ScreenManager::colorTextMuted(),
		                 "No results found.", BOTTOM_SCREEN_WIDTH);
	}

	float keysY = BOTTOM_SCREEN_HEIGHT - 25.0f;

	if (popupActive) {
		drawText(10.0f, keysY, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(),
		         ": " + TR("common.navigate") + "  : " + TR("common.confirm") +
		             "  : " + TR("common.cancel"));
	} else {
		drawText(10.0f, keysY, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(),
		         ": " + TR("common.navigate") + "  : " + TR("common.select") + "  : " + TR("common.back") +
		             "  : " + TR("common.search"));
	}
}

void SettingsScreen::saveAndExit() {
	Config::getInstance().saveSettings();
	ScreenManager::getInstance().returnToPreviousScreen();
}

} // namespace UI
