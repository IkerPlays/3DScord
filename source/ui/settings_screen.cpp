#include "ui/settings_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "ui/screen_manager.h"
#include "utils/message_utils.h"
#include "discord/account_settings.h"
#include "discord/avatar_cache.h"
#include "discord/discord_client.h"
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
	theme.max = 1;
	theme.valueFormatter = [](int val) {
		if (val == 0) {
			return TR("settings.theme.dark");
		}
		return TR("settings.theme.light");
	};
	theme.onUpdate = [](int val) { Config::getInstance().setThemeType(val); };
	allItems.push_back(theme);

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

	addAccountItems();
	addUpdateItems();

	// DEVELOPER
	SettingItem devSection;
	devSection.label = "Developer Options";
	devSection.type = SettingItemType::SECTION_HEADER;
	devSection.isDeveloper = true;
	allItems.push_back(devSection);

	SettingItem fileLogging;
	fileLogging.label = "Save Logs to File";
	fileLogging.description = "Save debug logs to SD card (sdmc:/3ds/TriCord/tricord.log)";
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
	mkdir("sdmc:/3ds", 0777);
	mkdir(CONFIG_DIR_PATH, 0777);
	mkdir(UPLOAD_DIR_PATH, 0777);

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

	// Image pickers list the files of the upload folder; 0 = nothing chosen, 1 = remove the current image.
	auto addImagePicker = [this](const std::string &label, const std::string &desc, bool banner) {
		auto files = std::make_shared<std::vector<std::string>>(AccountSettings::listUploadFiles());
		SettingItem item;
		item.label = label;
		item.description = desc;
		item.type = SettingItemType::INTEGER;
		item.value = 0;
		item.min = 0;
		item.max = 1 + (int)files->size();
		item.valueFormatter = [files](int val) -> std::string {
			if (val == 0) {
				return TR("account.choose_image");
			}
			if (val == 1) {
				return TR("account.remove_image");
			}
			return (*files)[val - 2];
		};
		item.onUpdate = [this, files, banner](int val) {
			if (val == 0) {
				return;
			}
			ScreenManager::getInstance().showToast(TR("account.uploading"));
			Result r;
			if (val == 1) {
				r = banner ? AccountSettings::removeBanner() : AccountSettings::removeAvatar();
			} else {
				const std::string path = std::string(UPLOAD_DIR_PATH) + "/" + (*files)[val - 2];
				r = banner ? AccountSettings::setBannerFromFile(path) : AccountSettings::setAvatarFromFile(path);
			}
			report(r);
			scheduleRefresh = true;
		};
		allItems.push_back(item);
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
	addImagePicker(TR("account.avatar"), TR("account.desc.images"), false);
	addImagePicker(TR("account.banner"), TR("account.desc.images"), true);

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
		UpdaterScreen::manualCheck = true;
		ScreenManager::getInstance().setScreen(ScreenType::UPDATER);
	};
	allItems.push_back(check);

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
