#ifndef CONFIG_H
#define CONFIG_H

#define DISCORD_GATEWAY_QUERY "/?v=10&encoding=json&compress=zlib-stream"
#define DISCORD_GATEWAY_URL "wss://gateway.discord.gg" DISCORD_GATEWAY_QUERY
#define DISCORD_REMOTE_AUTH_URL "wss://remote-auth-gateway.discord.gg/?v=2"
#define DISCORD_QR_BASE_URL "https://discord.com/ra/"

#define APP_NAME "3DScord"

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define APP_VERSION                                                                                                    \
	TOSTRING(APP_VERSION_MAJOR)                                                                                        \
	"." TOSTRING(APP_VERSION_MINOR) "." TOSTRING(APP_VERSION_MICRO)
#ifndef APP_DEV_BUILD
#define APP_DEV_BUILD 0
#endif
#define APP_USER_AGENT "3DScord/" APP_VERSION " (Nintendo 3DS)"

#define HTTP_TIMEOUT_SECONDS 30

#define TOP_SCREEN_WIDTH 400
#define TOP_SCREEN_HEIGHT 240
#define BOTTOM_SCREEN_WIDTH 320
#define BOTTOM_SCREEN_HEIGHT 240

#define CONFIG_DIR_PATH "sdmc:/3ds/3DScord"
#define LEGACY_CONFIG_DIR_PATH "sdmc:/3ds/TriCord"
#define UPDATE_REPO "IkerPlays/3DScord"

#include "core/i18n.h"
#include <3ds.h>
#include "utils/key_map.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Theme {
	u32 bg;
	u32 bg_dark;
	u32 bg_light;
	u32 accent;
	u32 selection;
	u32 separator;
	u32 header_border;
	u32 overlay;
	u32 pure_white;

	u32 text;
	u32 text_muted;
	u32 link;

	u32 embed_bg;
	u32 embed_media_bg;
	u32 reaction_bg;
	u32 reaction_me_bg;
	u32 input_bg;

	u32 success;
	u32 error;

	std::string name;
	std::string author;
	std::string description;
};

class Config {
  public:
	struct Account {
		std::string name;
		std::string token;
	};

	static Config &getInstance() {
		static Config instance;
		return instance;
	}

	void load();
	void save();

	void loadSettings();
	void saveSettings();

	std::string getToken() const;
	void setToken(const std::string &newToken);

	const std::vector<Account> &getAccounts() const { return accounts; }
	void addAccount(const std::string &name, const std::string &token);
	void removeAccount(int index);
	void selectAccount(int index);
	int getCurrentAccountIndex() const { return currentAccountIndex; }
	void updateCurrentAccountName(const std::string &name);

	bool hasToken() const { return !accounts.empty(); }

	int getTimezoneOffset() const { return timezoneOffset; }
	void setTimezoneOffset(int offset);

	std::string getLanguage() const { return language; }
	void setLanguage(const std::string &lang) {
		language = lang;
		Core::I18n::getInstance().loadLanguage(lang);
		saveSettings();
	}

	const Theme &getTheme() const;
	void setCustomTheme(const Theme &theme) { customTheme = theme; }

	bool isCustomThemeEnabled() const { return customThemeEnabled; }
	void setCustomThemeEnabled(bool enabled);

	std::string getSelectedThemeName() const { return selectedThemeName; }
	void setSelectedThemeName(const std::string &name);

	int getThemeType() const { return themeType; }
	void setThemeType(int type);
	bool isTypingIndicatorEnabled() const { return typingIndicatorEnabled; }
	void setTypingIndicatorEnabled(bool enabled);
	bool isNotificationSoundEnabled() const { return notificationSound; }
	void setNotificationSoundEnabled(bool enabled);
	bool isFileLoggingEnabled() const { return fileLoggingEnabled; }
	void setFileLoggingEnabled(bool enabled);

	bool isShowAvatarsEnabled() const { return showAvatars; }
	void setShowAvatarsEnabled(bool enabled);
	bool isShowServerIconsEnabled() const { return showServerIcons; }
	void setShowServerIconsEnabled(bool enabled);
	bool isShowHiddenChannelsEnabled() const { return showHiddenChannels; }
	void setShowHiddenChannelsEnabled(bool enabled);

	// Push to talk: 0 off, 1 hold R, 2 hold ZR (New 3DS).
	int getPushToTalk() const { return pushToTalk; }
	void setPushToTalk(int mode);
	// How loud the microphone has to be to count as speaking: 0 (very sensitive) .. 10 (strict).
	int getVoiceTone() const { return voiceTone; }
	void setVoiceTone(int level);
	int getUploadQuality() const { return uploadQuality; }
	void setUploadQuality(int level);
	int getCallVolume() const { return callVolume; }
	void setCallVolume(int percent);
	bool isBusyMode() const { return busyMode; }
	void setBusyMode(bool enabled);
	bool isAutoReconnect() const { return autoReconnect; }
	void setAutoReconnect(bool enabled);
	int getNoiseGate() const { return noiseGate; }
	void setNoiseGate(int level);
	bool isKeepCallsAwake() const { return keepCallsAwake; }
	void setKeepCallsAwake(bool enabled);

	// How many pictures are kept in memory: 0 few (saves memory), 1 normal, 2 many.
	int getImageCacheLevel() const { return imageCache; }
	void setImageCacheLevel(int level);

	// PIN lock: the PIN is never stored, only a salted SHA-256 of it.
	bool hasPin() const { return !pinHash.empty(); }
	bool checkPin(const std::string &pin) const;
	void setPin(const std::string &pin); // empty removes the PIN
	// Copies the settings (not the accounts) to sdmc:/3ds/3DScord/settings_backup.json.
	bool exportSettings();
	// Replaces the settings with the ones in a backup file made by exportSettings.
	bool importSettings(const std::string &path);

	bool isLeftHanded() const { return leftHanded; }
	void setLeftHanded(bool enabled);
	bool isBatterySaver() const { return batterySaver; }
	void setBatterySaver(bool enabled);
	bool isLightMode() const { return lightMode; }
	void setLightMode(bool enabled);
	// Themes built into the app: 0 dark, 1 light, 2 AMOLED, 3 Nintendo, 4 Retro, 5 Midnight.
	static constexpr int THEME_PRESET_COUNT = 6;
	static Theme presetTheme(int type) { return getPreset(type); }
	// Writes a custom theme to sdmc:/3ds/3DScord/themes/<name>.json and returns whether it worked.
	bool saveThemeToFile(const std::string &name, const Theme &theme);

	bool isQuietHoursEnabled() const { return quietHours; }
	void setQuietHoursEnabled(bool enabled);
	int getQuietStart() const { return quietStart; }
	int getQuietEnd() const { return quietEnd; }
	void setQuietHours(int startHour, int endHour);
	bool isDmUnreadFirst() const { return dmUnreadFirst; }
	void setDmUnreadFirst(bool enabled);

	bool isReplyPingEnabled() const { return replyPing; }
	void setReplyPingEnabled(bool enabled);
	bool isAutoUpdateEnabled() const { return autoUpdate; }
	void setAutoUpdateEnabled(bool enabled);
	bool isDevBuildUpdatesEnabled() const { return devBuildUpdates; }
	void setDevBuildUpdatesEnabled(bool enabled);

	bool isDisclaimerAccepted() const { return disclaimerAccepted; }
	void setDisclaimerAccepted(bool accepted);

	int getMicVolume() const { return micVolume; }
	void setMicVolume(int volumePercent);

	int getUserVolume(const std::string &userId) const;
	void setUserVolume(const std::string &userId, int volumePercent);

	void loadTheme();

	std::vector<std::string> getAvailableThemes();
	bool loadThemeFromFile(const std::string &name);
	void deleteTheme(const std::string &name);

  private:
	Config();
	std::vector<Account> accounts;
	int currentAccountIndex;
	int timezoneOffset;
	std::string language;
	int themeType;
	bool typingIndicatorEnabled;
	bool notificationSound;
	bool fileLoggingEnabled;
	bool disclaimerAccepted;
	bool showAvatars;
	bool showServerIcons;
	bool showHiddenChannels;
	bool autoUpdate;
	bool replyPing;
	bool quietHours;
	bool leftHanded;
	std::string pinHash;
	std::string pinSalt;
	bool batterySaver;
	bool lightMode = false;
	int imageCache;
	int pushToTalk;
	int noiseGate;
	int callVolume = 100;
	int uploadQuality = 1; // 0 small, 1 normal, 2 high
	int voiceTone = 0; // -2 warmer ... 2 brighter
	bool busyMode = false;
	bool autoReconnect = true;
	bool keepCallsAwake;
	int quietStart;
	int quietEnd;
	bool dmUnreadFirst;
	bool devBuildUpdates;

	int micVolume;
	std::unordered_map<std::string, int> userVolumes;

	bool customThemeEnabled;
	std::string selectedThemeName;
	Theme customTheme;

	mutable std::recursive_mutex mutex;

	static Theme getDarkPreset();
	static Theme getLightPreset();
	static Theme getPreset(int type);
};

#endif // CONFIG_H
