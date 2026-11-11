#include "discord/account_settings.h"
#include "core/config.h"
#include "discord/discord_client.h"
#include "log.h"
#include "network/http_client.h"
#include "utils/base64_utils.h"
#include "utils/file_utils.h"
#include <algorithm>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace Discord {
namespace AccountSettings {

namespace {

constexpr const char *API = "https://discord.com/api/v10";
constexpr size_t MAX_IMAGE_BYTES = 1536 * 1024;
constexpr long REQUEST_TIMEOUT = 60;

Result fromResponse(const Network::HttpResponse &resp) {
	Result r;
	r.ok = resp.success;
	r.status = resp.statusCode;
	if (!resp.success) {
		rapidjson::Document doc;
		doc.Parse(resp.body.c_str());
		if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("message") && doc["message"].IsString()) {
			r.message = doc["message"].GetString();
		}
		if (doc.IsObject() && doc.HasMember("errors")) {
			// The first nested "message" is the useful one (e.g. "Nitro is required").
			std::string first;
			std::vector<const rapidjson::Value *> stack{&doc["errors"]};
			while (!stack.empty() && first.empty()) {
				const rapidjson::Value *v = stack.back();
				stack.pop_back();
				if (v->IsObject()) {
					if (v->HasMember("message") && (*v)["message"].IsString()) {
						first = (*v)["message"].GetString();
						break;
					}
					for (auto it = v->MemberBegin(); it != v->MemberEnd(); ++it) {
						stack.push_back(&it->value);
					}
				} else if (v->IsArray()) {
					for (auto &e : v->GetArray()) {
						stack.push_back(&e);
					}
				}
			}
			if (!first.empty()) {
				r.message = first;
			}
		}
		if (r.message.empty()) {
			r.message = resp.error.empty() ? "HTTP " + std::to_string(resp.statusCode) : resp.error;
		}
		Logger::log("[Account] Request failed: %ld %s", resp.statusCode, r.message.c_str());
	}
	return r;
}

Result send(const std::string &method, const std::string &path, const std::string &body) {
	const std::string token = Config::getInstance().getToken();
	if (token.empty()) {
		return Result{false, 0, "Not logged in"};
	}
	Network::HttpClient http;
	http.setAuthToken(token);
	http.setTimeout(REQUEST_TIMEOUT);
	const std::string url = std::string(API) + path;
	return fromResponse(method == "GET" ? http.get(url) : http.patch(url, body));
}

template <typename F> std::string buildJson(F fill) {
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> w(s);
	w.StartObject();
	fill(w);
	w.EndObject();
	return s.GetString();
}

using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

std::string mimeFor(const std::string &path) {
	auto dot = path.rfind('.');
	std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	if (ext == "png") {
		return "image/png";
	}
	if (ext == "jpg" || ext == "jpeg") {
		return "image/jpeg";
	}
	if (ext == "gif") {
		return "image/gif";
	}
	return "";
}

bool toDataUri(const std::string &path, std::string &out, std::string &error) {
	const std::string mime = mimeFor(path);
	if (mime.empty()) {
		error = "Use a png, jpg or gif file";
		return false;
	}
	std::vector<unsigned char> data = Utils::File::readFileBinary(path);
	if (data.empty()) {
		error = "Cannot read the file";
		return false;
	}
	if (data.size() > MAX_IMAGE_BYTES) {
		error = "Image is too big (max 1.5 MB)";
		return false;
	}
	out = "data:" + mime + ";base64," + Utils::Base64::encode(data.data(), data.size());
	return true;
}

Result setImage(const char *endpoint, const char *field, const std::string &path) {
	std::string uri, error;
	if (!toDataUri(path, uri, error)) {
		return Result{false, 0, error};
	}
	return send("PATCH", endpoint, buildJson([&](Writer &w) {
		            w.Key(field);
		            w.String(uri.c_str());
	            }));
}

Result clearImage(const char *endpoint, const char *field) {
	return send("PATCH", endpoint, buildJson([&](Writer &w) {
		            w.Key(field);
		            w.Null();
	            }));
}

bool boolOf(const rapidjson::Value &o, const char *key, bool fallback) {
	return o.HasMember(key) && o[key].IsBool() ? o[key].GetBool() : fallback;
}

} // namespace

Snapshot fetch() {
	Snapshot snap;

	const Network::HttpResponse settings = [] {
		const std::string token = Config::getInstance().getToken();
		Network::HttpClient http;
		http.setAuthToken(token);
		http.setTimeout(10);
		return http.get(std::string(API) + "/users/@me/settings");
	}();

	if (settings.success) {
		rapidjson::Document doc;
		doc.Parse(settings.body.c_str());
		if (!doc.HasParseError() && doc.IsObject()) {
			snap.loaded = true;
			snap.compactMessages = boolOf(doc, "message_display_compact", snap.compactMessages);
			snap.renderEmbeds = boolOf(doc, "render_embeds", snap.renderEmbeds);
			snap.renderReactions = boolOf(doc, "render_reactions", snap.renderReactions);
			snap.animateEmoji = boolOf(doc, "animate_emoji", snap.animateEmoji);
			snap.gifAutoPlay = boolOf(doc, "gif_auto_play", snap.gifAutoPlay);
			snap.inlineEmbedMedia = boolOf(doc, "inline_embed_media", snap.inlineEmbedMedia);
			snap.inlineAttachmentMedia = boolOf(doc, "inline_attachment_media", snap.inlineAttachmentMedia);
			snap.convertEmoticons = boolOf(doc, "convert_emoticons", snap.convertEmoticons);
			snap.developerMode = boolOf(doc, "developer_mode", snap.developerMode);
			snap.showCurrentGame = boolOf(doc, "show_current_game", snap.showCurrentGame);
			snap.restrictNewServerDms = boolOf(doc, "default_guilds_restricted", snap.restrictNewServerDms);
			if (doc.HasMember("explicit_content_filter") && doc["explicit_content_filter"].IsInt()) {
				snap.explicitContentFilter = std::clamp(doc["explicit_content_filter"].GetInt(), 0, 2);
			}
			if (doc.HasMember("theme") && doc["theme"].IsString()) {
				snap.lightTheme = std::string(doc["theme"].GetString()) == "light";
			}
			if (doc.HasMember("custom_status") && doc["custom_status"].IsObject()) {
				const auto &cs = doc["custom_status"];
				if (cs.HasMember("text") && cs["text"].IsString()) {
					snap.customStatus = cs["text"].GetString();
				}
			}
		}
	}

	const std::string userId = DiscordClient::getInstance().getCurrentUser().id;
	if (!userId.empty()) {
		Network::HttpClient http;
		http.setAuthToken(Config::getInstance().getToken());
		http.setTimeout(10);
		const Network::HttpResponse profile =
		    http.get(std::string(API) + "/users/" + userId + "/profile?with_mutual_guilds=false");
		if (profile.success) {
			rapidjson::Document doc;
			doc.Parse(profile.body.c_str());
			if (!doc.HasParseError() && doc.IsObject()) {
				if (doc.HasMember("user_profile") && doc["user_profile"].IsObject()) {
					const auto &up = doc["user_profile"];
					if (up.HasMember("bio") && up["bio"].IsString()) {
						snap.bio = up["bio"].GetString();
					}
					if (up.HasMember("pronouns") && up["pronouns"].IsString()) {
						snap.pronouns = up["pronouns"].GetString();
					}
				}
				if (doc.HasMember("user") && doc["user"].IsObject() && doc["user"].HasMember("accent_color") &&
				    doc["user"]["accent_color"].IsInt()) {
					snap.accentColor = doc["user"]["accent_color"].GetInt();
				}
			}
		}
	}
	return snap;
}

Result setDisplayName(const std::string &name) {
	return send("PATCH", "/users/@me", buildJson([&](Writer &w) {
		            w.Key("global_name");
		            if (name.empty()) {
			            w.Null();
		            } else {
			            w.String(name.c_str());
		            }
	            }));
}

Result setBio(const std::string &bio) {
	return send("PATCH", "/users/@me/profile", buildJson([&](Writer &w) {
		            w.Key("bio");
		            w.String(bio.c_str());
	            }));
}

Result setPronouns(const std::string &pronouns) {
	return send("PATCH", "/users/@me/profile", buildJson([&](Writer &w) {
		            w.Key("pronouns");
		            w.String(pronouns.c_str());
	            }));
}

Result setAccentColor(int rgb) {
	return send("PATCH", "/users/@me/profile", buildJson([&](Writer &w) {
		            w.Key("accent_color");
		            if (rgb < 0) {
			            w.Null();
		            } else {
			            w.Int(rgb & 0xFFFFFF);
		            }
	            }));
}

Result setCustomStatus(const std::string &text) {
	return send("PATCH", "/users/@me/settings", buildJson([&](Writer &w) {
		            w.Key("custom_status");
		            if (text.empty()) {
			            w.Null();
		            } else {
			            w.StartObject();
			            w.Key("text");
			            w.String(text.c_str());
			            w.Key("expires_at");
			            w.Null();
			            w.EndObject();
		            }
	            }));
}

Result setAvatarFromFile(const std::string &path) { return setImage("/users/@me", "avatar", path); }
Result setBannerFromFile(const std::string &path) { return setImage("/users/@me/profile", "banner", path); }
Result removeAvatar() { return clearImage("/users/@me", "avatar"); }
Result removeBanner() { return clearImage("/users/@me/profile", "banner"); }

Result setBoolSetting(const char *key, bool value) {
	return send("PATCH", "/users/@me/settings", buildJson([&](Writer &w) {
		            w.Key(key);
		            w.Bool(value);
	            }));
}

Result setIntSetting(const char *key, int value) {
	return send("PATCH", "/users/@me/settings", buildJson([&](Writer &w) {
		            w.Key(key);
		            w.Int(value);
	            }));
}

Result setThemeSetting(bool light) {
	return send("PATCH", "/users/@me/settings", buildJson([&](Writer &w) {
		            w.Key("theme");
		            w.String(light ? "light" : "dark");
	            }));
}

} // namespace AccountSettings
} // namespace Discord
