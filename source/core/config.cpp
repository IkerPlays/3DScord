#include "config.h"

#include <algorithm>
#include <3ds.h>
#include <citro2d.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/writer.h>

#include "log.h"
#include "utils/color_utils.h"
#include "utils/file_utils.h"
#include "utils/sha256.h"

Config::Config()
    : currentAccountIndex(-1), timezoneOffset(0), language("en_US"), themeType(0), typingIndicatorEnabled(true),
      notificationSound(true), fileLoggingEnabled(false), disclaimerAccepted(false), showAvatars(true),
      showServerIcons(true), showHiddenChannels(false), autoUpdate(true), replyPing(true), quietHours(false), leftHanded(false), batterySaver(false), imageCache(1), pushToTalk(0), noiseGate(5), keepCallsAwake(true), quietStart(22), quietEnd(7), dmUnreadFirst(false), devBuildUpdates(false), micVolume(100), customThemeEnabled(false),
      selectedThemeName("") {
	customTheme = getDarkPreset();
	customTheme.name = "Custom Theme";
}

Theme Config::getDarkPreset() {
	Theme t;
	t.name = "Dark Mode";
	t.author = "3DScord Team";
	t.bg = 0xFF383331;
	t.bg_dark = 0xFF312D2B;
	t.bg_light = 0xFF494240;
	t.accent = 0xFFF26558;
	t.selection = 0xFFF26558;
	t.separator = 0xFFA49B94;
	t.header_border = 0x1EFFFFFF;
	t.overlay = 0x96000000;
	t.pure_white = 0xFFFFFFFF;

	t.text = 0xFFFFFFFF;
	t.text_muted = 0xFFA49B94;
	t.link = 0xFFFEBA49;

	t.embed_bg = 0xFF312D2B;
	t.embed_media_bg = 0xFF383331;
	t.reaction_bg = 0xFF494240;
	t.reaction_me_bg = 0xFF8B6447;
	t.input_bg = 0xFF252220;

	t.success = 0xFF6DB143;
	t.error = 0xFF4D47F0;
	return t;
}

Theme Config::getLightPreset() {
	Theme t;
	t.name = "Light Mode";
	t.author = "3DScord Team";
	t.bg = 0xFFFFFFFF;
	t.bg_dark = 0xFFF5F3F2;
	t.bg_light = 0xFFE5E2E0;
	t.accent = 0xFFF26558;
	t.selection = 0xFFF26558;
	t.separator = 0xFF58504E;
	t.header_border = 0x1EFFFFFF;
	t.overlay = 0x96000000;
	t.pure_white = 0xFFFFFFFF;

	t.text = 0xFF070606;
	t.text_muted = 0xFF58504E;
	t.link = 0xFFFEBA49;

	t.embed_bg = 0xFFFBFBFB;
	t.embed_media_bg = 0xFFF5F3F2;
	t.reaction_bg = 0xFFE0E2E5;
	t.reaction_me_bg = 0xFFFAEAED;
	t.input_bg = 0xFFE5E2E0;

	t.success = 0xFF6DB143;
	t.error = 0xFF4D47F0;
	return t;
}

namespace {
u32 rgb(int r, int g, int b) { return 0xFF000000u | ((u32)b << 16) | ((u32)g << 8) | (u32)r; }
} // namespace

Theme Config::getPreset(int type) {
	Theme t = (type == 1 || type == 3) ? getLightPreset() : getDarkPreset();
	t.author = "3DScord Team";
	switch (type) {
	case 2: // AMOLED: real black, so unlit pixels stay off
		t.name = "AMOLED";
		t.bg = rgb(0, 0, 0);
		t.bg_dark = rgb(0, 0, 0);
		t.bg_light = rgb(26, 26, 26);
		t.separator = rgb(90, 90, 90);
		t.text_muted = rgb(150, 150, 150);
		t.embed_bg = rgb(14, 14, 14);
		t.embed_media_bg = rgb(0, 0, 0);
		t.reaction_bg = rgb(30, 30, 30);
		t.reaction_me_bg = rgb(47, 60, 110);
		t.input_bg = rgb(16, 16, 16);
		break;
	case 3: // Nintendo: white and red
		t.name = "Nintendo";
		t.bg = rgb(255, 255, 255);
		t.bg_dark = rgb(240, 240, 240);
		t.bg_light = rgb(255, 225, 225);
		t.accent = rgb(230, 0, 18);
		t.selection = rgb(230, 0, 18);
		t.separator = rgb(200, 200, 200);
		t.text = rgb(50, 50, 50);
		t.text_muted = rgb(120, 120, 120);
		t.link = rgb(0, 120, 200);
		t.embed_bg = rgb(245, 245, 245);
		t.embed_media_bg = rgb(255, 255, 255);
		t.reaction_bg = rgb(235, 235, 235);
		t.reaction_me_bg = rgb(255, 205, 205);
		t.input_bg = rgb(240, 240, 240);
		t.success = rgb(0, 170, 70);
		t.error = rgb(230, 0, 18);
		break;
	case 4: // Retro: green on black, like a terminal
		t.name = "Retro";
		t.bg = rgb(0, 18, 0);
		t.bg_dark = rgb(0, 10, 0);
		t.bg_light = rgb(0, 40, 0);
		t.accent = rgb(0, 255, 70);
		t.selection = rgb(0, 120, 40);
		t.separator = rgb(0, 140, 50);
		t.text = rgb(120, 255, 140);
		t.text_muted = rgb(0, 160, 60);
		t.link = rgb(0, 255, 255);
		t.embed_bg = rgb(0, 24, 0);
		t.embed_media_bg = rgb(0, 14, 0);
		t.reaction_bg = rgb(0, 50, 10);
		t.reaction_me_bg = rgb(0, 90, 30);
		t.input_bg = rgb(0, 8, 0);
		t.success = rgb(0, 255, 70);
		t.error = rgb(255, 60, 60);
		break;
	case 5: // Midnight: deep blue
		t.name = "Midnight";
		t.bg = rgb(20, 24, 48);
		t.bg_dark = rgb(14, 17, 36);
		t.bg_light = rgb(34, 40, 78);
		t.accent = rgb(120, 140, 255);
		t.selection = rgb(80, 100, 220);
		t.separator = rgb(110, 120, 170);
		t.text = rgb(235, 238, 255);
		t.text_muted = rgb(150, 158, 200);
		t.link = rgb(110, 200, 255);
		t.embed_bg = rgb(14, 17, 36);
		t.embed_media_bg = rgb(20, 24, 48);
		t.reaction_bg = rgb(34, 40, 78);
		t.reaction_me_bg = rgb(60, 80, 160);
		t.input_bg = rgb(10, 12, 28);
		break;
	default:
		break;
	}
	return t;
}

const Theme &Config::getTheme() const {
	if (customThemeEnabled) {
		return customTheme;
	}
	if (themeType == 0) {
		static Theme dark = getDarkPreset();
		return dark;
	} else if (themeType == 1) {
		static Theme light = getLightPreset();
		return light;
	} else if (themeType >= 2 && themeType < THEME_PRESET_COUNT) {
		static Theme presets[THEME_PRESET_COUNT] = {};
		static bool built[THEME_PRESET_COUNT] = {};
		if (!built[themeType]) {
			presets[themeType] = getPreset(themeType);
			built[themeType] = true;
		}
		return presets[themeType];
	}
	return customTheme;
}
static void encrypt_decrypt_data(std::vector<u8> &data) {
	size_t original_size = data.size();
	size_t padded_size = (original_size + 15) & ~15;
	data.resize(padded_size, 0);

	u8 iv[16];
	memset(iv, 0, sizeof(iv));

	PS_EncryptDecryptAes(padded_size, data.data(), data.data(), PS_ALGORITHM_CTR_ENC, PS_KEYSLOT_0D, iv);
}

void Config::load() {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	accounts.clear();
	struct stat st = {0};
	if (stat(CONFIG_DIR_PATH, &st) == -1) {
		mkdir(CONFIG_DIR_PATH, 0700);
	}

	std::string themesPath = std::string(CONFIG_DIR_PATH) + "/themes";
	if (stat(themesPath.c_str(), &st) == -1) {
		mkdir(themesPath.c_str(), 0700);
	}

	std::string accountsPath = std::string(CONFIG_DIR_PATH) + "/accounts";
	std::vector<unsigned char> buffer = Utils::File::readFileBinary(accountsPath);
	if (!buffer.empty()) {
		encrypt_decrypt_data(buffer);
		buffer.push_back('\0');

		rapidjson::Document doc;
		doc.Parse((char *)buffer.data());

		if (!doc.HasParseError() && doc.IsObject()) {
			if (doc.HasMember("currentIndex") && doc["currentIndex"].IsInt()) {
				currentAccountIndex = doc["currentIndex"].GetInt();
			}

			if (doc.HasMember("accounts") && doc["accounts"].IsArray()) {
				const auto &arr = doc["accounts"];
				for (rapidjson::SizeType i = 0; i < arr.Size(); i++) {
					const auto &obj = arr[i];
					if (obj.IsObject() && obj.HasMember("token") && obj["token"].IsString()) {
						Account acc;
						acc.token = obj["token"].GetString();
						if (obj.HasMember("name") && obj["name"].IsString()) {
							acc.name = obj["name"].GetString();
						} else {
							acc.name = "Account " + std::to_string(i + 1);
						}
						accounts.push_back(acc);
					}
				}
			}
		}
	}

	loadSettings();

	if (accounts.empty()) {
		currentAccountIndex = -1;
	} else if (currentAccountIndex < 0 || currentAccountIndex >= (int)accounts.size()) {
		currentAccountIndex = 0;
	}
}

void Config::save() {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);

	writer.StartObject();
	writer.Key("currentIndex");
	writer.Int(currentAccountIndex);

	writer.Key("accounts");
	writer.StartArray();
	for (const auto &acc : accounts) {
		writer.StartObject();
		writer.Key("name");
		writer.String(acc.name.c_str());
		writer.Key("token");
		writer.String(acc.token.c_str());
		writer.EndObject();
	}
	writer.EndArray();
	writer.EndObject();

	std::string jsonStr = s.GetString();
	std::vector<u8> data(jsonStr.begin(), jsonStr.end());
	encrypt_decrypt_data(data);

	std::string accountsPath = std::string(CONFIG_DIR_PATH) + "/accounts";
	Utils::File::writeFile(accountsPath, data);
}

void Config::loadSettings() {
	std::string settingsPath = std::string(CONFIG_DIR_PATH) + "/settings.json";
	std::vector<char> buffer = Utils::File::readFile(settingsPath);

	if (!buffer.empty()) {
		rapidjson::Document doc;
		doc.Parse(buffer.data());

		if (!doc.HasParseError() && doc.IsObject()) {
			if (doc.HasMember("timezone_offset") && doc["timezone_offset"].IsInt()) {
				timezoneOffset = doc["timezone_offset"].GetInt();
			}
			if (doc.HasMember("theme_type") && doc["theme_type"].IsInt()) {
				themeType = doc["theme_type"].GetInt();
			}
			if (doc.HasMember("language") && doc["language"].IsString()) {
				language = doc["language"].GetString();
			}
			if (doc.HasMember("typing_indicator") && doc["typing_indicator"].IsBool()) {
				typingIndicatorEnabled = doc["typing_indicator"].GetBool();
			}
			if (doc.HasMember("notification_sound") && doc["notification_sound"].IsBool()) {
				notificationSound = doc["notification_sound"].GetBool();
			}
			if (doc.HasMember("file_logging") && doc["file_logging"].IsBool()) {
				fileLoggingEnabled = doc["file_logging"].GetBool();
			}
			if (doc.HasMember("disclaimer_accepted") && doc["disclaimer_accepted"].IsBool()) {
				disclaimerAccepted = doc["disclaimer_accepted"].GetBool();
			}
			if (doc.HasMember("custom_theme_enabled") && doc["custom_theme_enabled"].IsBool()) {
				customThemeEnabled = doc["custom_theme_enabled"].GetBool();
			}
			if (doc.HasMember("selected_theme_name") && doc["selected_theme_name"].IsString()) {
				selectedThemeName = doc["selected_theme_name"].GetString();
			}
			if (doc.HasMember("show_avatars") && doc["show_avatars"].IsBool()) {
				showAvatars = doc["show_avatars"].GetBool();
			}
			if (doc.HasMember("show_server_icons") && doc["show_server_icons"].IsBool()) {
				showServerIcons = doc["show_server_icons"].GetBool();
			}
			if (doc.HasMember("show_hidden_channels") && doc["show_hidden_channels"].IsBool()) {
				showHiddenChannels = doc["show_hidden_channels"].GetBool();
			}
			if (doc.HasMember("pin_hash") && doc["pin_hash"].IsString()) {
				pinHash = doc["pin_hash"].GetString();
			}
			if (doc.HasMember("pin_salt") && doc["pin_salt"].IsString()) {
				pinSalt = doc["pin_salt"].GetString();
			}
			if (doc.HasMember("left_handed") && doc["left_handed"].IsBool()) {
				leftHanded = doc["left_handed"].GetBool();
				Utils::KeyMap::leftHanded = leftHanded;
			}
			if (doc.HasMember("battery_saver") && doc["battery_saver"].IsBool()) {
				batterySaver = doc["battery_saver"].GetBool();
			}
			if (doc.HasMember("light_mode") && doc["light_mode"].IsBool()) {
				lightMode = doc["light_mode"].GetBool();
				Utils::System::setLightMode(lightMode);
			}
			if (doc.HasMember("image_cache") && doc["image_cache"].IsInt()) {
				imageCache = std::clamp(doc["image_cache"].GetInt(), 0, 2);
			}
			if (doc.HasMember("push_to_talk") && doc["push_to_talk"].IsInt()) {
				pushToTalk = std::clamp(doc["push_to_talk"].GetInt(), 0, 2);
			}
			if (doc.HasMember("noise_gate") && doc["noise_gate"].IsInt()) {
				noiseGate = std::clamp(doc["noise_gate"].GetInt(), 0, 10);
			}
			if (doc.HasMember("keep_calls_awake") && doc["keep_calls_awake"].IsBool()) {
				keepCallsAwake = doc["keep_calls_awake"].GetBool();
			}
			if (doc.HasMember("quiet_hours") && doc["quiet_hours"].IsBool()) {
				quietHours = doc["quiet_hours"].GetBool();
			}
			if (doc.HasMember("quiet_start") && doc["quiet_start"].IsInt()) {
				quietStart = std::clamp(doc["quiet_start"].GetInt(), 0, 23);
			}
			if (doc.HasMember("quiet_end") && doc["quiet_end"].IsInt()) {
				quietEnd = std::clamp(doc["quiet_end"].GetInt(), 0, 23);
			}
			if (doc.HasMember("dm_unread_first") && doc["dm_unread_first"].IsBool()) {
				dmUnreadFirst = doc["dm_unread_first"].GetBool();
			}
			if (doc.HasMember("reply_ping") && doc["reply_ping"].IsBool()) {
				replyPing = doc["reply_ping"].GetBool();
			}
			if (doc.HasMember("auto_update") && doc["auto_update"].IsBool()) {
				autoUpdate = doc["auto_update"].GetBool();
			}
			if (doc.HasMember("dev_build_updates") && doc["dev_build_updates"].IsBool()) {
				devBuildUpdates = doc["dev_build_updates"].GetBool();
			}
			if (doc.HasMember("mic_volume") && doc["mic_volume"].IsInt()) {
				micVolume = std::clamp(doc["mic_volume"].GetInt(), 0, 200);
			}
			if (doc.HasMember("user_volumes") && doc["user_volumes"].IsObject()) {
				userVolumes.clear();
				for (auto it = doc["user_volumes"].MemberBegin(); it != doc["user_volumes"].MemberEnd(); ++it) {
					if (it->name.IsString() && it->value.IsInt()) {
						userVolumes[it->name.GetString()] = std::clamp(it->value.GetInt(), 0, 200);
					}
				}
			}
		} else {
			saveSettings();
		}
	} else {
		saveSettings();
	}

	if (!Core::I18n::getInstance().loadLanguage(language.empty() ? "en_US" : language)) {
		Core::I18n::getInstance().loadLanguage("en_US");
	}
	Logger::setFileLoggingEnabled(fileLoggingEnabled);
}

void Config::saveSettings() {
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);

	writer.StartObject();
	writer.Key("timezone_offset");
	writer.Int(timezoneOffset);
	writer.Key("theme_type");
	writer.Int(themeType);
	writer.Key("language");
	writer.String(language.c_str());
	writer.Key("typing_indicator");
	writer.Bool(typingIndicatorEnabled);
	writer.Key("notification_sound");
	writer.Bool(notificationSound);
	writer.Key("file_logging");
	writer.Bool(fileLoggingEnabled);
	writer.Key("disclaimer_accepted");
	writer.Bool(disclaimerAccepted);
	writer.Key("custom_theme_enabled");
	writer.Bool(customThemeEnabled);
	writer.Key("selected_theme_name");
	writer.String(selectedThemeName.c_str());
	writer.Key("show_avatars");
	writer.Bool(showAvatars);
	writer.Key("show_server_icons");
	writer.Bool(showServerIcons);
	writer.Key("show_hidden_channels");
	writer.Bool(showHiddenChannels);
	writer.Key("pin_hash");
	writer.String(pinHash.c_str());
	writer.Key("pin_salt");
	writer.String(pinSalt.c_str());
	writer.Key("left_handed");
	writer.Bool(leftHanded);
	writer.Key("battery_saver");
	writer.Bool(batterySaver);
	writer.Key("light_mode");
	writer.Bool(lightMode);
	writer.Key("image_cache");
	writer.Int(imageCache);
	writer.Key("push_to_talk");
	writer.Int(pushToTalk);
	writer.Key("noise_gate");
	writer.Int(noiseGate);
	writer.Key("keep_calls_awake");
	writer.Bool(keepCallsAwake);
	writer.Key("quiet_hours");
	writer.Bool(quietHours);
	writer.Key("quiet_start");
	writer.Int(quietStart);
	writer.Key("quiet_end");
	writer.Int(quietEnd);
	writer.Key("dm_unread_first");
	writer.Bool(dmUnreadFirst);
	writer.Key("reply_ping");
	writer.Bool(replyPing);
	writer.Key("auto_update");
	writer.Bool(autoUpdate);
	writer.Key("dev_build_updates");
	writer.Bool(devBuildUpdates);
	writer.Key("mic_volume");
	writer.Int(micVolume);
	writer.Key("user_volumes");
	writer.StartObject();
	for (const auto &pair : userVolumes) {
		writer.Key(pair.first.c_str());
		writer.Int(pair.second);
	}
	writer.EndObject();
	writer.EndObject();

	std::string settingsPath = std::string(CONFIG_DIR_PATH) + "/settings.json";
	Utils::File::writeFile(settingsPath, s.GetString());
}

void Config::setFileLoggingEnabled(bool enabled) {
	fileLoggingEnabled = enabled;
	Logger::setFileLoggingEnabled(enabled);
	saveSettings();
}

void Config::setDisclaimerAccepted(bool accepted) {
	disclaimerAccepted = accepted;
	saveSettings();
}

void Config::setMicVolume(int volumePercent) {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	micVolume = std::clamp(volumePercent, 0, 200);
	saveSettings();
}

int Config::getUserVolume(const std::string &userId) const {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	auto it = userVolumes.find(userId);
	return (it != userVolumes.end()) ? it->second : 100;
}

void Config::setUserVolume(const std::string &userId, int volumePercent) {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	userVolumes[userId] = std::clamp(volumePercent, 0, 200);
	saveSettings();
}

void Config::setTimezoneOffset(int offset) {
	timezoneOffset = offset;
	saveSettings();
}

void Config::setThemeType(int type) {
	themeType = std::clamp(type, 0, THEME_PRESET_COUNT - 1);
	if (customThemeEnabled && !selectedThemeName.empty()) {
		loadThemeFromFile(selectedThemeName);
	}
	saveSettings();
}

void Config::setTypingIndicatorEnabled(bool enabled) {
	typingIndicatorEnabled = enabled;
	saveSettings();
}

void Config::setNotificationSoundEnabled(bool enabled) {
	notificationSound = enabled;
	saveSettings();
}

void Config::setShowAvatarsEnabled(bool enabled) {
	showAvatars = enabled;
	saveSettings();
}

void Config::setShowServerIconsEnabled(bool enabled) {
	showServerIcons = enabled;
	saveSettings();
}

void Config::setShowHiddenChannelsEnabled(bool enabled) {
	showHiddenChannels = enabled;
	saveSettings();
}

bool Utils::KeyMap::leftHanded = false;

bool Config::checkPin(const std::string &pin) const {
	return !pinHash.empty() && Utils::sha256Hex(pinSalt + pin) == pinHash;
}

void Config::setPin(const std::string &pin) {
	if (pin.empty()) {
		pinHash.clear();
		pinSalt.clear();
	} else {
		char salt[17];
		snprintf(salt, sizeof(salt), "%08x%08x", (unsigned)rand(), (unsigned)osGetTime());
		pinSalt = salt;
		pinHash = Utils::sha256Hex(pinSalt + pin);
	}
	saveSettings();
}

bool Config::exportSettings() {
	saveSettings();
	std::vector<char> data = Utils::File::readFile(std::string(CONFIG_DIR_PATH) + "/settings.json");
	if (data.empty()) {
		return false;
	}
	// The PIN stays on this console: a backup restored elsewhere must not lock it with a PIN it does not know.
	rapidjson::Document doc;
	doc.Parse(data.data());
	if (doc.HasParseError() || !doc.IsObject()) {
		return false;
	}
	doc.RemoveMember("pin_hash");
	doc.RemoveMember("pin_salt");
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> w(s);
	doc.Accept(w);
	return Utils::File::writeFile(std::string(CONFIG_DIR_PATH) + "/settings_backup.json", std::string(s.GetString()));
}

bool Config::importSettings(const std::string &path) {
	std::vector<char> data = Utils::File::readFile(path);
	if (data.empty()) {
		return false;
	}
	rapidjson::Document doc;
	doc.Parse(data.data());
	// A settings file always has these; anything else is some other JSON.
	if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("theme_type")) {
		return false;
	}
	const std::string keepHash = pinHash;
	const std::string keepSalt = pinSalt;
	doc.RemoveMember("pin_hash");
	doc.RemoveMember("pin_salt");
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> w(s);
	doc.Accept(w);
	if (!Utils::File::writeFile(std::string(CONFIG_DIR_PATH) + "/settings.json", std::string(s.GetString()))) {
		return false;
	}
	loadSettings();
	pinHash = keepHash;
	pinSalt = keepSalt;
	saveSettings();
	return true;
}

void Config::setLeftHanded(bool enabled) {
	leftHanded = enabled;
	Utils::KeyMap::leftHanded = enabled;
	saveSettings();
}

void Config::setLightMode(bool enabled) {
	lightMode = enabled;
	Utils::System::setLightMode(enabled);
	saveSettings();
}

void Config::setBatterySaver(bool enabled) {
	batterySaver = enabled;
	// The New 3DS has a faster mode; the saver turns it off.
	osSetSpeedupEnable(!enabled);
	saveSettings();
}

void Config::setImageCacheLevel(int level) {
	imageCache = std::clamp(level, 0, 2);
	saveSettings();
}

void Config::setPushToTalk(int mode) {
	pushToTalk = std::clamp(mode, 0, 2);
	saveSettings();
}

void Config::setNoiseGate(int level) {
	noiseGate = std::clamp(level, 0, 10);
	saveSettings();
}

void Config::setKeepCallsAwake(bool enabled) {
	keepCallsAwake = enabled;
	saveSettings();
}

void Config::setQuietHoursEnabled(bool enabled) {
	quietHours = enabled;
	saveSettings();
}

void Config::setQuietHours(int startHour, int endHour) {
	quietStart = std::clamp(startHour, 0, 23);
	quietEnd = std::clamp(endHour, 0, 23);
	saveSettings();
}

void Config::setDmUnreadFirst(bool enabled) {
	dmUnreadFirst = enabled;
	saveSettings();
}

void Config::setReplyPingEnabled(bool enabled) {
	replyPing = enabled;
	saveSettings();
}

void Config::setAutoUpdateEnabled(bool enabled) {
	autoUpdate = enabled;
	saveSettings();
}

void Config::setDevBuildUpdatesEnabled(bool enabled) {
	devBuildUpdates = enabled;
	saveSettings();
}

std::string Config::getToken() const {
	std::lock_guard<std::recursive_mutex> lock(const_cast<std::recursive_mutex &>(mutex));
	if (currentAccountIndex >= 0 && currentAccountIndex < (int)accounts.size()) {
		return accounts[currentAccountIndex].token;
	}
	return "";
}

void Config::setToken(const std::string &newToken) { addAccount("New Account", newToken); }

void Config::addAccount(const std::string &name, const std::string &token) {
	std::lock_guard<std::recursive_mutex> lock(mutex);

	for (size_t i = 0; i < accounts.size(); i++) {
		if (accounts[i].token == token) {
			currentAccountIndex = (int)i;
			save();
			return;
		}
	}

	Account acc;
	acc.name = name;
	acc.token = token;
	accounts.push_back(acc);
	currentAccountIndex = (int)accounts.size() - 1;
	save();
}

void Config::removeAccount(int index) {
	if (index >= 0 && index < (int)accounts.size()) {
		accounts.erase(accounts.begin() + index);
		if (accounts.empty()) {
			currentAccountIndex = -1;
		} else {
			if (currentAccountIndex == index) {
				if (currentAccountIndex >= (int)accounts.size()) {
					currentAccountIndex = (int)accounts.size() - 1;
				}
			} else if (currentAccountIndex > index) {
				currentAccountIndex--;
			}
		}
		save();
	}
}

void Config::selectAccount(int index) {
	if (index >= -1 && index < (int)accounts.size()) {
		currentAccountIndex = index;
		save();
	}
}

void Config::updateCurrentAccountName(const std::string &name) {
	if (currentAccountIndex >= 0 && currentAccountIndex < (int)accounts.size()) {
		accounts[currentAccountIndex].name = name;
		save();
	}
}

void Config::loadTheme() {
	customTheme = getPreset(themeType);
	if (customThemeEnabled && !selectedThemeName.empty()) {
		loadThemeFromFile(selectedThemeName);
	}
}

void Config::setCustomThemeEnabled(bool enabled) {
	customThemeEnabled = enabled;
	saveSettings();
}

void Config::setSelectedThemeName(const std::string &name) {
	selectedThemeName = name;
	saveSettings();
}

std::vector<std::string> Config::getAvailableThemes() {
	std::vector<std::string> themes;
	std::string themesPath = std::string(CONFIG_DIR_PATH) + "/themes";
	struct dirent *ent;
	DIR *dir = opendir(themesPath.c_str());
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			std::string name = ent->d_name;
			if (name.length() > 5 && name.substr(name.length() - 5) == ".json") {
				themes.push_back(name.substr(0, name.length() - 5));
			}
		}
		closedir(dir);
	}
	return themes;
}

bool Config::loadThemeFromFile(const std::string &name) {
	std::string path = std::string(CONFIG_DIR_PATH) + "/themes/" + name + ".json";
	std::vector<char> buffer = Utils::File::readFile(path);
	if (buffer.empty()) {
		return false;
	}

	rapidjson::Document doc;
	doc.Parse(buffer.data());
	if (doc.HasParseError() || !doc.IsObject()) {
		return false;
	}

	Theme newTheme = getPreset(themeType);
	if (doc.HasMember("name") && doc["name"].IsString()) {
		newTheme.name = doc["name"].GetString();
	} else {
		newTheme.name = name;
	}

	if (doc.HasMember("author") && doc["author"].IsString()) {
		newTheme.author = doc["author"].GetString();
	} else {
		newTheme.author = "";
	}

	if (doc.HasMember("description") && doc["description"].IsString()) {
		newTheme.description = doc["description"].GetString();
	} else {
		newTheme.description = "";
	}

	auto loadCol = [&](const char *category, const char *key, u32 &target) {
		if (doc.HasMember("colors") && doc["colors"].IsObject()) {
			const auto &colors = doc["colors"];
			if (colors.HasMember(category) && colors[category].IsObject()) {
				const auto &cat = colors[category];
				if (cat.HasMember(key) && cat[key].IsString()) {
					target = Utils::Color::hexToColor(cat[key].GetString());
				}
			}
		}
	};

	loadCol("ui", "background", newTheme.bg);
	loadCol("ui", "background_dark", newTheme.bg_dark);
	loadCol("ui", "background_light", newTheme.bg_light);
	loadCol("ui", "accent", newTheme.accent);
	loadCol("ui", "selection", newTheme.selection);
	loadCol("ui", "separator", newTheme.separator);
	loadCol("ui", "header_border", newTheme.header_border);
	loadCol("ui", "overlay", newTheme.overlay);
	loadCol("ui", "pure_white", newTheme.pure_white);

	loadCol("text", "main", newTheme.text);
	loadCol("text", "muted", newTheme.text_muted);
	loadCol("text", "link", newTheme.link);

	loadCol("discord", "embed_bg", newTheme.embed_bg);
	loadCol("discord", "embed_media_bg", newTheme.embed_media_bg);
	loadCol("discord", "reaction_bg", newTheme.reaction_bg);
	loadCol("discord", "reaction_me_bg", newTheme.reaction_me_bg);
	loadCol("discord", "input_bg", newTheme.input_bg);

	loadCol("status", "success", newTheme.success);
	loadCol("status", "error", newTheme.error);

	customTheme = newTheme;
	selectedThemeName = name;
	saveSettings();
	return true;
}

bool Config::saveThemeToFile(const std::string &name, const Theme &theme) {
	std::string clean;
	for (char ch : name) {
		clean += (std::string("\\/:*?\"<>|").find(ch) != std::string::npos || (unsigned char)ch < 32) ? '_' : ch;
	}
	if (clean.empty()) {
		return false;
	}
	mkdir("sdmc:/3ds", 0777);
	mkdir(CONFIG_DIR_PATH, 0777);
	const std::string dir = std::string(CONFIG_DIR_PATH) + "/themes";
	mkdir(dir.c_str(), 0777);

	rapidjson::StringBuffer s;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> w(s);
	auto color = [&](const char *key, u32 value) {
		w.Key(key);
		w.String(Utils::Color::colorToHex(value).c_str());
	};
	w.StartObject();
	w.Key("name");
	w.String(clean.c_str());
	w.Key("author");
	w.String(theme.author.empty() ? "3DScord" : theme.author.c_str());
	w.Key("description");
	w.String("Made with the theme editor of 3DScord");
	w.Key("colors");
	w.StartObject();
	w.Key("ui");
	w.StartObject();
	color("background", theme.bg);
	color("background_dark", theme.bg_dark);
	color("background_light", theme.bg_light);
	color("accent", theme.accent);
	color("selection", theme.selection);
	color("separator", theme.separator);
	color("header_border", theme.header_border);
	color("overlay", theme.overlay);
	color("pure_white", theme.pure_white);
	w.EndObject();
	w.Key("text");
	w.StartObject();
	color("main", theme.text);
	color("muted", theme.text_muted);
	color("link", theme.link);
	w.EndObject();
	w.Key("discord");
	w.StartObject();
	color("embed_bg", theme.embed_bg);
	color("embed_media_bg", theme.embed_media_bg);
	color("reaction_bg", theme.reaction_bg);
	color("reaction_me_bg", theme.reaction_me_bg);
	color("input_bg", theme.input_bg);
	w.EndObject();
	w.Key("status");
	w.StartObject();
	color("success", theme.success);
	color("error", theme.error);
	w.EndObject();
	w.EndObject();
	w.EndObject();

	return Utils::File::writeFile(dir + "/" + clean + ".json", std::string(s.GetString()));
}

void Config::deleteTheme(const std::string &name) {
	std::string path = std::string(CONFIG_DIR_PATH) + "/themes/" + name + ".json";
	remove(path.c_str());
	if (selectedThemeName == name) {
		selectedThemeName = "";
		customThemeEnabled = false;
		saveSettings();
	}
}
