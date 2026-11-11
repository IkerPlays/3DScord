#include "ui/profile_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "discord/avatar_cache.h"
#include "discord/discord_client.h"
#include "discord/social.h"
#include "log.h"
#include "network/network_manager.h"
#include "ui/image_manager.h"
#include "ui/list_screen.h"
#include "utils/message_utils.h"
#include "utils/redraw.h"
#include "utils/utf8_utils.h"
#include <3ds.h>
#include <algorithm>
#include <rapidjson/document.h>

namespace UI {

std::string ProfileScreen::pendingUser;
std::string ProfileScreen::pendingGuild;

namespace {

constexpr float BANNER_HEIGHT = 100.0f;
constexpr int BODY_ROWS = 12;

std::string str(const rapidjson::Value &o, const char *key) {
	return o.HasMember(key) && o[key].IsString() ? o[key].GetString() : "";
}

std::string datePart(const std::string &iso) {
	return iso.empty() ? "" : UI::MessageUtils::getLocalDateString(iso);
}

} // namespace

void ProfileScreen::open(const std::string &userId, const std::string &guildId) {
	pendingUser = userId;
	pendingGuild = guildId == "DM" ? "" : guildId;
	ScreenManager::getInstance().pushScreen(ScreenType::PROFILE);
}

ProfileScreen::ProfileScreen()
    : userId(pendingUser), guildId(pendingGuild), shared(std::make_shared<Shared>()), loading(true), scroll(0.0f) {}

void ProfileScreen::onEnter() {
	profile.id = userId;
	const std::string token = Config::getInstance().getToken();
	std::string url = "https://discord.com/api/v10/users/" + userId + "/profile?with_mutual_guilds=false";
	if (!guildId.empty()) {
		url += "&guild_id=" + guildId;
	}

	auto state = shared;
	Network::NetworkManager::getInstance().enqueue(
	    url, "GET", "", Network::RequestPriority::INTERACTIVE,
	    [state, userId = userId](Network::HttpResponse &resp) {
		    Profile p;
		    p.id = userId;
		    std::string err;

		    rapidjson::Document doc;
		    doc.Parse(resp.body.c_str());
		    if (!resp.success || doc.HasParseError() || !doc.IsObject()) {
			    err = TR("profile.error");
			    if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("message") && doc["message"].IsString()) {
				    err = doc["message"].GetString();
			    }
		    } else {
			    if (doc.HasMember("user") && doc["user"].IsObject()) {
				    const auto &u = doc["user"];
				    p.username = str(u, "username");
				    p.globalName = str(u, "global_name");
				    p.avatar = str(u, "avatar");
				    p.banner = str(u, "banner");
				    p.discriminator = str(u, "discriminator");
				    p.bot = u.HasMember("bot") && u["bot"].IsBool() && u["bot"].GetBool();
				    if (u.HasMember("accent_color") && u["accent_color"].IsInt()) {
					    p.accentColor = u["accent_color"].GetInt();
				    }
			    }
			    if (doc.HasMember("user_profile") && doc["user_profile"].IsObject()) {
				    p.bio = str(doc["user_profile"], "bio");
				    p.pronouns = str(doc["user_profile"], "pronouns");
			    }
			    if (doc.HasMember("connected_accounts") && doc["connected_accounts"].IsArray()) {
				    for (const auto &c : doc["connected_accounts"].GetArray()) {
					    if (c.IsObject() && !str(c, "type").empty() && !str(c, "name").empty()) {
						    std::string type = str(c, "type");
						    type[0] = (char)toupper((unsigned char)type[0]);
						    p.connections.push_back(type + ": " + str(c, "name"));
					    }
				    }
			    }
			    if (doc.HasMember("guild_member") && doc["guild_member"].IsObject()) {
				    const auto &m = doc["guild_member"];
				    p.nickname = str(m, "nick");
				    p.joinedAt = str(m, "joined_at");
				    if (m.HasMember("roles") && m["roles"].IsArray()) {
					    for (const auto &r : m["roles"].GetArray()) {
						    if (r.IsString()) {
							    p.roleIds.push_back(r.GetString());
						    }
					    }
				    }
			    }
		    }

		    std::lock_guard<std::mutex> lock(state->mutex);
		    state->profile = std::move(p);
		    state->error = err;
		    state->loading = false;
		    state->changed = true;
	    },
	    {{"Authorization", token}});

	// The private note you keep about this person.
	auto noteState = shared;
	Network::NetworkManager::getInstance().enqueue(
	    "https://discord.com/api/v10/users/@me/notes/" + userId, "GET", "", Network::RequestPriority::INTERACTIVE,
	    [noteState](Network::HttpResponse &resp) {
		    std::string text;
		    if (resp.success) {
			    rapidjson::Document doc;
			    doc.Parse(resp.body.c_str());
			    if (!doc.HasParseError() && doc.IsObject()) {
				    text = str(doc, "note");
			    }
		    }
		    std::lock_guard<std::mutex> lock(noteState->mutex);
		    noteState->note = text;
		    noteState->noteChanged = true;
	    },
	    {{"Authorization", token}});
}

std::string ProfileScreen::displayName() const {
	if (!profile.nickname.empty()) {
		return profile.nickname;
	}
	if (!profile.globalName.empty()) {
		return profile.globalName;
	}
	return profile.username.empty() ? "?" : profile.username;
}

void ProfileScreen::buildBodyLines() {
	bodyLines.clear();
	auto add = [this](const std::string &text, float width = 300.0f) {
		for (const auto &line : MessageUtils::wrapText(text, width, 0.42f)) {
			bodyLines.push_back(line);
		}
	};

	const std::string activity = Discord::DiscordClient::getInstance().getUserActivity(profile.id);
	if (!activity.empty()) {
		bodyLines.push_back("# " + TR("profile.activity"));
		add(activity);
		bodyLines.push_back("");
	}

	if (!profile.bio.empty()) {
		bodyLines.push_back("# " + TR("profile.about"));
		add(profile.bio);
		bodyLines.push_back("");
	}

	const time_t created = MessageUtils::snowflakeToTimestamp(profile.id);
	bodyLines.push_back("# " + TR("profile.member_since"));
	add(datePart(MessageUtils::getISOTimestamp(created)));
	if (!profile.joinedAt.empty()) {
		bodyLines.push_back("");
		bodyLines.push_back("# " + TR("profile.joined_server"));
		add(datePart(profile.joinedAt));
	}

	if (!profile.connections.empty()) {
		bodyLines.push_back("");
		bodyLines.push_back("# " + TR("profile.connections"));
		for (const auto &c : profile.connections) {
			add(c);
		}
	}

	if (!note.empty()) {
		bodyLines.push_back("");
		bodyLines.push_back("# " + TR("profile.note"));
		add(note);
	}

	if (!profile.roleIds.empty() && !guildId.empty()) {
		std::string names;
		const Discord::Guild *guild = Discord::DiscordClient::getInstance().getGuildPtr(guildId);
		if (guild) {
			for (const auto &id : profile.roleIds) {
				for (const auto &role : guild->roles) {
					if (role.id == id && role.name != "@everyone") {
						names += (names.empty() ? "" : ", ") + role.name;
					}
				}
			}
		}
		if (!names.empty()) {
			bodyLines.push_back("");
			bodyLines.push_back("# " + TR("profile.roles"));
			add(names);
		}
	}
}

void ProfileScreen::update() {
	{
		std::lock_guard<std::mutex> lock(shared->mutex);
		if (shared->changed) {
			shared->changed = false;
			Utils::requestRedraw();
			profile = shared->profile;
			error = shared->error;
			loading = shared->loading;
		}
	}
	{
		std::lock_guard<std::mutex> lock(shared->mutex);
		if (shared->noteChanged) {
			shared->noteChanged = false;
			note = shared->note;
			bodyLines.clear();
		}
	}
	if (!loading && bodyLines.empty() && error.empty()) {
		buildBodyLines();
		Utils::requestRedraw();
	}

	const u32 kDown = hidKeysDown();
	const u32 kHeld = hidKeysHeld();
	if (kDown & KEY_B) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	if (!loading && error.empty()) {
		auto &sm = ScreenManager::getInstance();
		if (kDown & KEY_A) {
			const auto dm = Discord::Social::openDm(userId);
			if (dm.result.ok && !dm.channelId.empty()) {
				Discord::DiscordClient::getInstance().setSelectedChannelId(dm.channelId);
				sm.setScreen(ScreenType::MESSAGES);
				return;
			}
			sm.showToast(dm.result.message);
		} else if (kDown & KEY_Y) {
			const auto r = Discord::Social::sendFriendRequestToId(userId);
			sm.showToast(r.ok ? TR("friends.sent") : r.message);
		} else if (kDown & KEY_X) {
			// Blocking takes a second press.
			if (!confirmBlock) {
				confirmBlock = true;
				sm.showToast(TR("profile.confirm_block"));
			} else {
				confirmBlock = false;
				const auto r = Discord::Social::block(userId);
				sm.showToast(r.ok ? TR("profile.blocked") : r.message);
			}
		} else if ((kDown & KEY_L) && !guildId.empty()) {
			ListScreen::openModeration(userId, guildId, displayName());
		} else if (kDown & KEY_R) {
			SwkbdState swkbd;
			swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 256);
			swkbdSetFeatures(&swkbd, SWKBD_DARKEN_TOP_SCREEN | SWKBD_MULTILINE);
			swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 0, 0);
			swkbdSetHintText(&swkbd, TR("profile.note").c_str());
			if (!note.empty()) {
				swkbdSetInitialText(&swkbd, note.c_str());
			}
			std::vector<char> buffer(256 * 4 + 8, 0);
			if (swkbdInputText(&swkbd, buffer.data(), buffer.size()) == SWKBD_BUTTON_CONFIRM) {
				const auto r = Discord::Social::setNote(userId, buffer.data());
				if (r.ok) {
					note = buffer.data();
					bodyLines.clear();
				}
				sm.showToast(r.ok ? TR("account.saved") : r.message);
			}
		}
	}

	const float maxScroll = std::max(0.0f, (float)bodyLines.size() - BODY_ROWS);
	if (kHeld & KEY_DOWN) {
		scroll = std::min(maxScroll, scroll + 0.25f);
	}
	if (kHeld & KEY_UP) {
		scroll = std::max(0.0f, scroll - 0.25f);
	}
}

void ProfileScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackground());
	C2D_SceneBegin(target);

	if (loading) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), TR("common.loading"),
		                 TOP_SCREEN_WIDTH);
		return;
	}
	if (!error.empty()) {
		drawCenteredText(110.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorError(), error, TOP_SCREEN_WIDTH);
		return;
	}

	// Banner: the picture at its own aspect ratio over the profile color.
	u32 band = profile.accentColor >= 0
	               ? C2D_Color32((profile.accentColor >> 16) & 0xFF, (profile.accentColor >> 8) & 0xFF,
	                             profile.accentColor & 0xFF, 255)
	               : ScreenManager::colorAccent();
	C2D_DrawRectSolid(0.0f, 0.0f, 0.4f, TOP_SCREEN_WIDTH, BANNER_HEIGHT, band);
	if (!profile.banner.empty()) {
		const std::string url =
		    "https://cdn.discordapp.com/banners/" + profile.id + "/" + profile.banner + ".png?size=480";
		const auto info = ImageManager::getInstance().getImageInfo(url);
		if (info.tex && info.originalW > 0 && info.originalH > 0) {
			const float uMax = (float)info.originalW / info.tex->width;
			const float vMax = (float)info.originalH / info.tex->height;
			Tex3DS_SubTexture subtex = {(u16)info.originalW, (u16)info.originalH, 0.0f, 1.0f, uMax, 1.0f - vMax};
			const float scale = BANNER_HEIGHT / info.originalH;
			const float w = info.originalW * scale;
			C2D_DrawImageAt({info.tex, &subtex}, (TOP_SCREEN_WIDTH - w) / 2.0f, 0.0f, 0.45f, nullptr, scale, scale);
		} else if (!info.failed) {
			ImageManager::getInstance().prefetch(url, 0, 0, Network::RequestPriority::INTERACTIVE);
		}
	}

	// Avatar over the lower edge of the banner.
	const float avatarSize = 56.0f;
	const float ax = 16.0f;
	const float ay = BANNER_HEIGHT - avatarSize / 2.0f;
	C2D_DrawRectSolid(ax - 3.0f, ay - 3.0f, 0.5f, avatarSize + 6.0f, avatarSize + 6.0f, ScreenManager::colorBackground());
	C3D_Tex *avatar = Discord::AvatarCache::getInstance().getAvatar(profile.id, profile.avatar, profile.discriminator);
	if (avatar) {
		Tex3DS_SubTexture subtex = {(u16)avatar->width, (u16)avatar->height, 0.0f, 1.0f, 1.0f, 0.0f};
		C2D_DrawImageAt({avatar, &subtex}, ax, ay, 0.55f, nullptr, avatarSize / avatar->width,
		                avatarSize / avatar->height);
	} else {
		Discord::AvatarCache::getInstance().prefetchAvatar(profile.id, profile.avatar, profile.discriminator);
		C2D_DrawRectSolid(ax, ay, 0.55f, avatarSize, avatarSize, ScreenManager::colorBackgroundLight());
	}

	const float tx = ax + avatarSize + 12.0f;
	drawText(tx, BANNER_HEIGHT + 6.0f, 0.5f, 0.65f, 0.65f, ScreenManager::colorText(), displayName());
	std::string sub = "@" + profile.username;
	if (profile.bot) {
		sub += "  [BOT]";
	}
	drawText(tx, BANNER_HEIGHT + 30.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), sub);
	if (!profile.pronouns.empty()) {
		drawText(tx, BANNER_HEIGHT + 48.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), profile.pronouns);
	}
}

void ProfileScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);

	if (loading || !error.empty()) {
		return;
	}
	const int first = (int)scroll;
	float y = 8.0f;
	for (int i = 0; i < BODY_ROWS + 1 && first + i < (int)bodyLines.size(); i++) {
		const std::string &line = bodyLines[first + i];
		if (!line.empty() && line[0] == '#') {
			drawText(10.0f, y, 0.5f, 0.38f, 0.38f, ScreenManager::colorAccent(), line.substr(2));
		} else {
			drawText(10.0f, y, 0.5f, 0.42f, 0.42f, ScreenManager::colorText(), line);
		}
		y += 16.0f;
	}
	drawCenteredText(222.0f, 0.5f, 0.36f, 0.36f, ScreenManager::colorTextMuted(), TR("profile.hint"),
	                 BOTTOM_SCREEN_WIDTH);
}

} // namespace UI
