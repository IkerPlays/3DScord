#ifndef ACCOUNT_SETTINGS_H
#define ACCOUNT_SETTINGS_H

#include <string>
#include <vector>

namespace Discord {
namespace AccountSettings {

struct Result {
	bool ok = false;
	long status = 0;
	std::string message; // Discord's own error text when the request is refused
};

// What the server currently has; fields the server does not report keep their defaults.
struct Snapshot {
	bool loaded = false;
	std::string bio;
	std::string pronouns;
	std::string customStatus;
	int accentColor = -1;
	bool compactMessages = false;
	bool renderEmbeds = true;
	bool renderReactions = true;
	bool animateEmoji = true;
	bool gifAutoPlay = true;
	bool inlineEmbedMedia = true;
	bool inlineAttachmentMedia = true;
	bool convertEmoticons = true;
	bool developerMode = false;
	bool showCurrentGame = true;
	bool restrictNewServerDms = false;
	int explicitContentFilter = 1;
	bool lightTheme = false;
};

Snapshot fetch();

Result setDisplayName(const std::string &name);
Result setBio(const std::string &bio);
Result setPronouns(const std::string &pronouns);
Result setAccentColor(int rgb); // -1 clears it
// Profile theme: the two colours of the gradient behind your profile. -1 for both clears the theme.
Result setThemeColors(int primary, int accent);
Result setCustomStatus(const std::string &text); // empty clears it
Result setAvatarFromFile(const std::string &path);
Result setBannerFromFile(const std::string &path);
Result removeAvatar();
Result removeBanner();

// Boolean and integer options of PATCH /users/@me/settings, e.g. "message_display_compact".
Result setBoolSetting(const char *key, bool value);
Result setIntSetting(const char *key, int value);
Result setThemeSetting(bool light);

} // namespace AccountSettings
} // namespace Discord

#endif // ACCOUNT_SETTINGS_H
