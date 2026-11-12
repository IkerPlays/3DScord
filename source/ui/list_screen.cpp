#include "ui/list_screen.h"
#include "utils/screenshot.h"
#include "core/config.h"
#include "core/i18n.h"
#include "core/local_store.h"
#include "discord/discord_client.h"
#include "log.h"
#include "network/http_client.h"
#include "network/network_manager.h"
#include "ui/file_browser_screen.h"
#include "ui/voice_recorder_screen.h"
#include "discord/commands.h"
#include "ui/camera_screen.h"
#include "ui/video_test_screen.h"
#include "ui/mvd_test_screen.h"
#include "utils/call_recorder.h"
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include "ui/image_manager.h"
#include "ui/image_viewer_screen.h"
#include "utils/color_utils.h"
#include "utils/file_utils.h"
#include "utils/system_utils.h"
#include "utils/updater.h"
#include <sys/stat.h>
#include <sys/statvfs.h>
#include "ui/message_screen.h"
#include "ui/profile_screen.h"
#include "discord/social.h"
#include "discord/guild_tools.h"
#include "discord/threads.h"
#include "discord/voice_client.h"
#include "utils/message_utils.h"
#include "utils/redraw.h"
#include "utils/sound_player.h"
#include "utils/transfers.h"
#include "utils/utf8_utils.h"
#include <3ds.h>
#include <algorithm>
#include <map>
#include <cctype>
#include <cstdlib>
#include <strings.h>
#include <rapidjson/document.h>

namespace UI {

ListScreen::Mode ListScreen::pendingMode = ListScreen::Mode::PINS;
std::string ListScreen::pendingSoundTarget;
std::string ListScreen::pendingSoundName;
size_t ListScreen::preferredGifVariant = 0;
std::string ListScreen::pendingMoveUser;
std::string ListScreen::pendingModUser;
std::string ListScreen::pendingModGuild;

namespace {

constexpr float ROW_HEIGHT = 36.0f;
constexpr float LIST_TOP = 30.0f;
constexpr int VISIBLE_ROWS = 5;

struct ThemeField {
	const char *label;
	u32 Theme::*color;
};

const ThemeField THEME_FIELDS[] = {
    {"theme.bg", &Theme::bg},
    {"theme.bg_dark", &Theme::bg_dark},
    {"theme.bg_light", &Theme::bg_light},
    {"theme.accent", &Theme::accent},
    {"theme.selection", &Theme::selection},
    {"theme.separator", &Theme::separator},
    {"theme.header_border", &Theme::header_border},
    {"theme.overlay", &Theme::overlay},
    {"theme.pure_white", &Theme::pure_white},
    {"theme.text", &Theme::text},
    {"theme.text_muted", &Theme::text_muted},
    {"theme.link", &Theme::link},
    {"theme.embed_bg", &Theme::embed_bg},
    {"theme.embed_media_bg", &Theme::embed_media_bg},
    {"theme.reaction_bg", &Theme::reaction_bg},
    {"theme.reaction_me_bg", &Theme::reaction_me_bg},
    {"theme.input_bg", &Theme::input_bg},
    {"theme.success", &Theme::success},
    {"theme.error", &Theme::error},
};

std::string singleLine(std::string text) {
	if (text.size() > 160) {
		text.resize(160);
	}
	std::replace(text.begin(), text.end(), '\n', ' ');
	std::replace(text.begin(), text.end(), '\r', ' ');
	return text;
}

std::string fitLine(const std::string &input, float maxWidth, float scale) {
	const std::string text = singleLine(input);
	if (measureText(text, scale, scale) <= maxWidth) {
		return text;
	}
	size_t cursor = 0;
	size_t lastGood = 0;
	while (cursor < text.size()) {
		Utils::Utf8::decodeNext(text, cursor);
		if (measureText(text.substr(0, cursor) + "...", scale, scale) > maxWidth) {
			break;
		}
		lastGood = cursor;
	}
	return text.substr(0, lastGood) + "...";
}

std::string urlEncode(const std::string &text) {
	static const char *hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : text) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += (char)c;
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

bool askText(const std::string &hint, const std::string &initial, std::string &out, int maxLen, bool multiline) {
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

std::string str(const rapidjson::Value &o, const char *key) {
	return o.HasMember(key) && o[key].IsString() ? o[key].GetString() : "";
}

// Turns a Discord message object into a list row.
bool messageToEntry(const rapidjson::Value &m, std::string &id, std::string &channelId, std::string &author,
                    std::string &time, std::string &body) {
	if (!m.IsObject()) {
		return false;
	}
	id = str(m, "id");
	if (id.empty()) {
		return false;
	}
	channelId = str(m, "channel_id");
	author = "?";
	if (m.HasMember("author") && m["author"].IsObject()) {
		author = str(m["author"], "global_name");
		if (author.empty()) {
			author = str(m["author"], "username");
		}
	}
	time = UI::MessageUtils::formatTimestamp(str(m, "timestamp"));
	body = str(m, "content");
	auto count = [&](const char *key) { return m.HasMember(key) && m[key].IsArray() ? m[key].Size() : 0u; };
	if (count("attachments")) {
		body += (body.empty() ? "" : " ") + std::string("[") + TR("list.attachment") + "]";
	}
	if (count("sticker_items")) {
		body += (body.empty() ? "" : " ") + std::string("[") + TR("list.sticker") + "]";
	}
	if (body.empty() && count("embeds")) {
		body = "[" + TR("list.embed") + "]";
	}
	return true;
}

} // namespace

void ListScreen::openModeration(const std::string &userId, const std::string &guildId, const std::string &name) {
	pendingModUser = userId;
	pendingModGuild = guildId;
	pendingSoundName = name;
	pendingMode = Mode::MOD;
	ScreenManager::getInstance().pushScreen(ScreenType::LIST);
}

void ListScreen::openMoveVoice(const std::string &userId) {
	pendingMoveUser = userId;
	pendingMode = Mode::VOICE_CHANNELS;
	ScreenManager::getInstance().pushScreen(ScreenType::LIST);
}

void ListScreen::openSounds(const std::string &targetId, const std::string &targetName) {
	pendingSoundTarget = targetId;
	pendingSoundName = targetName;
	pendingMode = Mode::SOUNDS;
	ScreenManager::getInstance().setScreen(ScreenType::LIST);
}

void ListScreen::openReplacing(Mode mode) {
	pendingMode = mode;
	ScreenManager::getInstance().setScreen(ScreenType::LIST);
}

void ListScreen::open(Mode mode) {
	pendingMode = mode;
	ScreenManager::getInstance().pushScreen(ScreenType::LIST);
}

ListScreen::ListScreen()
    : mode(pendingMode), shared(std::make_shared<Shared>()), loading(true), seenVersion(-1), selected(0), scroll(0.0f) {
	channelId = Discord::DiscordClient::getInstance().getSelectedChannelId();
	soundTarget = pendingSoundTarget;
	soundTargetName = pendingSoundName;
	moveUser = pendingMoveUser;
	pendingMoveUser.clear();
	modUser = pendingModUser;
	modGuild = pendingModGuild;
	if (mode == Mode::THEME_EDIT) {
		editTheme = Config::getInstance().getTheme();
	}
}

ListScreen::~ListScreen() {}

void ListScreen::onEnter() {
	Logger::log("[ListScreen] Mode %d", (int)mode);
	load();
}

void ListScreen::load() {
	{
		std::lock_guard<std::mutex> lock(shared->mutex);
		shared->entries.clear();
		shared->error.clear();
		shared->loading = true;
		shared->version++;
	}

	switch (mode) {
	case Mode::PINS:
		fetch("/channels/" + channelId + "/pins");
		break;
	case Mode::MENTIONS:
		fetch("/users/@me/mentions?limit=25&roles=true&everyone=true");
		break;
	case Mode::SEARCH:
		startSearch();
		break;
	case Mode::SOUNDBOARD: {
		const std::string callGuild = Discord::VoiceClient::getInstance().getGuildId();
		if (soundSource == 1 && !callGuild.empty() && callGuild != "DM") {
			fetch("/guilds/" + callGuild + "/soundboard-sounds");
		} else {
			soundSource = 0;
			fetch("/soundboard-default-sounds");
		}
		break;
	}
	case Mode::COMMANDS:
		fetch("/channels/" + channelId + "/application-commands/search?type=1&limit=25&include_applications=false&query=" +
		      urlEncode(query));
		break;
	case Mode::GIFS:
		startGifSearch();
		break;
	case Mode::FIND:
		startFind();
		break;
	case Mode::GALLERY:
		fetch("/channels/" + channelId + "/messages?limit=100");
		break;
	case Mode::SESSIONS:
		fetch("/auth/sessions");
		break;
	case Mode::EVENTS:
	case Mode::AUDIT: {
		const std::string guildId = currentGuild();
		const bool allowed = mode == Mode::EVENTS || (Discord::DiscordClient::getInstance().getGuildPermissions(guildId) & (1ULL << 7));
		if (guildId.empty() || !allowed) {
			std::lock_guard<std::mutex> lock(shared->mutex);
			shared->error = TR(guildId.empty() ? "list.members_need_server" : "audit.no_permission");
			shared->loading = false;
			shared->version++;
		} else if (mode == Mode::EVENTS) {
			fetch("/guilds/" + guildId + "/scheduled-events?with_user_count=true");
		} else {
			fetch("/guilds/" + guildId + "/audit-logs?limit=30");
		}
		break;
	}
	case Mode::FRIENDS:
	case Mode::GROUP_PICK:
		fetch("/users/@me/relationships");
		break;
	case Mode::MEMBERS:
		startMembers();
		break;
	case Mode::THREADS: {
		const std::string guildId = Discord::DiscordClient::getInstance().getGuildIdFromChannel(channelId);
		if (guildId.empty() || guildId == "DM") {
			std::lock_guard<std::mutex> lock(shared->mutex);
			shared->error = TR("list.threads_need_server");
			shared->loading = false;
			shared->version++;
		} else {
			fetch("/channels/" + channelId + "/threads/search?archived=" + (includeArchived ? "true" : "false") +
			      "&sort_by=last_message_time&sort_order=desc&limit=25");
		}
		break;
	}
	case Mode::STICKERS: {
		const std::string guildId = Discord::DiscordClient::getInstance().getGuildIdFromChannel(channelId);
		if (guildId.empty() || guildId == "DM") {
			std::lock_guard<std::mutex> lock(shared->mutex);
			shared->error = TR("list.stickers_need_server");
			shared->loading = false;
			shared->version++;
		} else {
			fetch("/guilds/" + guildId + "/stickers");
		}
		break;
	}
	default:
		loadLocal();
		break;
	}
}

void ListScreen::loadLocal() {
	std::vector<Entry> list;
	auto &store = Core::LocalStore::getInstance();
	switch (mode) {
	case Mode::SAVED:
		for (const auto &m : store.savedMessages()) {
			list.push_back({m.id, m.channelId, m.author + "  " + UI::MessageUtils::formatTimestamp(m.timestamp) + "  " +
			                                       channelLabel(m.channelId),
			                m.content});
		}
		break;
	case Mode::DRAFTS:
		for (const auto &d : store.drafts()) {
			list.push_back({"", d.channelId, channelLabel(d.channelId), d.text});
		}
		break;
	case Mode::QUICK_REPLIES:
		for (const auto &r : store.quickReplies()) {
			list.push_back({"", channelId, "", r});
		}
		break;
	case Mode::PERMISSIONS: {
		struct Perm {
			int bit;
			const char *key;
		};
		static const Perm perms[] = {
		    {0, "perm.invite"},          {1, "perm.kick"},          {2, "perm.ban"},
		    {3, "perm.admin"},           {4, "perm.channels"},      {5, "perm.server"},
		    {6, "perm.reactions"},       {7, "perm.audit"},         {10, "perm.view"},
		    {11, "perm.send"},           {13, "perm.manage_messages"}, {14, "perm.embeds"},
		    {15, "perm.files"},          {16, "perm.history"},      {17, "perm.mention_all"},
		    {18, "perm.ext_emoji"},      {20, "perm.connect"},      {21, "perm.speak"},
		    {22, "perm.mute"},           {23, "perm.deafen"},       {24, "perm.move"},
		    {26, "perm.nick"},           {27, "perm.manage_nicks"}, {28, "perm.roles"},
		    {29, "perm.webhooks"},       {33, "perm.events"},       {34, "perm.threads"},
		    {35, "perm.create_threads"}, {38, "perm.send_in_threads"}, {40, "perm.timeout"},
		    {46, "perm.voice_messages"}};
		const uint64_t have = Discord::DiscordClient::getInstance().getChannelPermissions(channelId);
		for (const auto &p : perms) {
			list.push_back({"", "", TR(p.key), TR((have >> p.bit) & 1ULL ? "perm.yes" : "perm.no")});
		}
		break;
	}
	case Mode::MOD: {
		const uint64_t have = Discord::DiscordClient::getInstance().getGuildPermissions(modGuild);
		if (have & (1ULL << 40)) {
			list.push_back({"timeout:600", "", TR("mod.timeout_10m"), TR("mod.timeout_desc")});
			list.push_back({"timeout:3600", "", TR("mod.timeout_1h"), TR("mod.timeout_desc")});
			list.push_back({"timeout:86400", "", TR("mod.timeout_1d"), TR("mod.timeout_desc")});
			list.push_back({"timeout:604800", "", TR("mod.timeout_1w"), TR("mod.timeout_desc")});
			list.push_back({"timeout:0", "", TR("mod.timeout_off"), TR("mod.timeout_off_desc")});
		}
		if (have & (1ULL << 1)) {
			list.push_back({"kick", "", TR("mod.kick"), TR("mod.kick_desc")});
		}
		if (have & (1ULL << 2)) {
			list.push_back({"ban", "", TR("mod.ban"), TR("mod.ban_desc")});
		}
		break;
	}
	case Mode::FOLLOW: {
		// Channels where this account may create webhooks, which is what following needs.
		auto &client = Discord::DiscordClient::getInstance();
		for (const auto &g : client.getGuilds()) {
			for (const auto &ch : g.channels) {
				if (ch.type == 0 && (client.getChannelPermissions(ch.id) & (1ULL << 29))) {
					list.push_back({ch.id, "", "#" + ch.name, g.name});
				}
			}
		}
		break;
	}
	case Mode::SERVER: {
		list = {{"s_nick", "", TR("server.nick"), TR("server.nick_desc")},
		        {"s_bio", "", TR("server.bio"), TR("server.bio_desc")},
		        {"s_mute", "", TR("server.mute"), TR("server.mute_desc")},
		        {"s_unmute", "", TR("server.unmute"), TR("server.unmute_desc")},
		        {"s_all", "", TR("server.notify_all"), TR("server.notify_desc")},
		        {"s_mentions", "", TR("server.notify_mentions"), TR("server.notify_desc")},
		        {"s_none", "", TR("server.notify_none"), TR("server.notify_desc")},
		        {"s_everyone_off", "", TR("server.everyone_off"), TR("server.everyone_desc")},
		        {"s_everyone_on", "", TR("server.everyone_on"), TR("server.everyone_desc")},
		        {"s_roles_off", "", TR("server.roles_off"), TR("server.roles_desc")},
		        {"s_roles_on", "", TR("server.roles_on"), TR("server.roles_desc")},
		        {"s_push_on", "", TR("server.push_on"), TR("server.push_desc")},
		        {"s_push_off", "", TR("server.push_off"), TR("server.push_desc")},
		        {"s_sound", "", TR("list.sound_guild"), TR("list.tools.sound_guild")}};
		// Editing the server itself needs Manage Server.
		if (Discord::DiscordClient::getInstance().getGuildPermissions(currentGuild()) & (1ULL << 5)) {
			list.push_back({"s_name", "", TR("server.edit_name"), TR("server.edit_desc")});
			list.push_back({"s_description", "", TR("server.edit_description"), TR("server.edit_desc")});
		}
		break;
	}
	case Mode::DIAG: {
		auto add = [&](const std::string &title, const std::string &body) { list.push_back({"", "", title, body}); };
		add(TR("diag.version"), Utils::Updater::currentVersionString() +
		                            (Utils::Updater::isCiaInstall() ? " (CIA)" : " (3DSX)"));
		add(TR("diag.console"), Utils::System::getDeviceModelName());
		add(TR("diag.memory"), std::to_string(Utils::System::heapFreeBytes() / 1024) + " KB heap, " +
		                           std::to_string(linearSpaceFree() / 1024) + " KB linear");
		add(TR("diag.wifi"), std::to_string((int)osGetWifiStrength()) + " / 3");
		u8 battery = 0;
		if (R_SUCCEEDED(ptmuInit())) {
			PTMU_GetBatteryLevel(&battery);
			ptmuExit();
		}
		add(TR("diag.battery"), std::to_string((int)battery) + " / 5");
		struct statvfs sd;
		if (statvfs("sdmc:/", &sd) == 0) {
			add(TR("diag.sd"), std::to_string((unsigned long long)sd.f_bsize * sd.f_bavail / (1024 * 1024)) + " MB");
		}
		add(TR("diag.latency"), diagLatencyMs < 0 ? TR("diag.press_x") : std::to_string(diagLatencyMs) + " ms");
		add(TR("diag.report"), TR("diag.press_y"));
		break;
	}
	case Mode::THEME_EDIT: {
		int index = 0;
		for (const auto &field : THEME_FIELDS) {
			const u32 value = editTheme.*(field.color);
			Entry e{std::to_string(index++), "", TR(field.label), Utils::Color::colorToHex(value)};
			e.kind = (int)value;
			list.push_back(e);
		}
		break;
	}
	case Mode::VOICE_CHANNELS: {
		auto &client = Discord::DiscordClient::getInstance();
		auto &voice = Discord::VoiceClient::getInstance();
		std::string guildId = ScreenManager::getInstance().getSelectedGuildId();
		if (guildId.empty()) {
			guildId = client.getGuildIdFromChannel(channelId);
		}
		const bool inCall = voice.getState() != Discord::VoiceState::DISCONNECTED &&
		                    voice.getState() != Discord::VoiceState::FAILED;
		if (inCall) {
			list.push_back({"leave", "", TR("voice.leave"), TR("voice.leave_desc")});
		}
		if (const Discord::Guild *g = client.getGuildPtr(guildId)) {
			for (const auto &ch : g->channels) {
				if ((ch.type != 2 && ch.type != 13) || !ch.viewable) {
					continue;
				}
				std::string who;
				int count = 0;
				for (const auto &p : client.getVoiceParticipants(ch.id)) {
					if (count++ < 3) {
						who += (who.empty() ? "" : ", ") + p.name;
					}
				}
				if (count > 3) {
					who += " +" + std::to_string(count - 3);
				}
				const bool here = inCall && voice.getChannelId() == ch.id;
				list.push_back({"join:" + guildId + ":" + ch.id, ch.id, (here ? "> " : "") + ch.name,
				                who.empty() ? TR("voice.empty") : who});
			}
		}
		break;
	}
	case Mode::VOICE_PEOPLE: {
		auto &client = Discord::DiscordClient::getInstance();
		auto &voice = Discord::VoiceClient::getInstance();
		if (voice.getState() != Discord::VoiceState::DISCONNECTED && !voice.getChannelId().empty()) {
			const std::string me = client.getCurrentUser().id;
			for (const auto &p : client.getVoiceParticipants(voice.getChannelId())) {
				const int volume = p.userId == me ? voice.getMicVolume() : voice.getUserVolume(p.userId);
				std::string state = (p.userId == me ? TR("voice.mic") : TR("voice.volume")) + " " +
				                    std::to_string(volume) + "%";
				if (p.selfMute || p.mute) {
					state += "  " + TR("voice.is_muted");
				}
				if (p.selfDeaf || p.deaf) {
					state += "  " + TR("voice.is_deaf");
				}
				list.push_back({p.userId, "", (voice.isSpeaking(p.userId) ? "* " : "") + p.name, state});
			}
		}
		break;
	}
	case Mode::SOUNDS: {
		const int current = store.getSoundOverride(soundTarget);
		auto row = [&](const std::string &id, int value, const std::string &name) {
			list.push_back({id, soundTarget, name, current == value ? TR("list.sound_current") : ""});
		};
		row("default", -1, TR("sound.default"));
		row("0", 0, TR("sound.silent"));
		row("1", (int)Utils::Sound::NOTIFICATION + 1, TR("sound.notification"));
		row("2", (int)Utils::Sound::VOICE_JOIN + 1, TR("sound.join"));
		row("3", (int)Utils::Sound::VOICE_LEFT + 1, TR("sound.left"));
		row("4", (int)Utils::Sound::VOICE_PEER_LEFT + 1, TR("sound.left2"));
		row("5", (int)Utils::Sound::MIC_ON + 1, TR("sound.mic_on"));
		row("6", (int)Utils::Sound::HEADPHONE_ON + 1, TR("sound.headphones"));
		row("7", (int)Utils::Sound::CALL_INCOMING + 1, TR("sound.call"));
		break;
	}
	case Mode::HUB:
		list = {{"find", channelId, TR("list.find"), TR("list.hub.find")},
		        {"friends", channelId, TR("list.friends"), TR("list.hub.friends")},
		        {"group", channelId, TR("list.new_group"), TR("list.hub.group")},
		        {"join", channelId, TR("list.join_server"), TR("list.hub.join")},
		        {"voice", channelId, TR("list.voice_channels"), TR("list.hub.voice")},
		        {"people", channelId, TR("list.voice_people"), TR("list.hub.people")},
		        {"saved", channelId, TR("list.saved"), TR("list.hub.saved")},
		        {"mentions", channelId, TR("list.mentions"), TR("list.hub.mentions")}};
		break;
	case Mode::TOOLS:
		list = {{"gallery", channelId, TR("list.gallery"), TR("list.tools.gallery")},
		        {"threads", channelId, TR("list.threads"), TR("list.tools.threads")},
		        {"members", channelId, TR("list.members"), TR("list.tools.members")},
		        {"nick", channelId, TR("list.my_nick"), TR("list.tools.nick")},
		        {"sound_channel", channelId, TR("list.sound_channel"), TR("list.tools.sound_channel")},
		        {"sound_guild", channelId, TR("list.sound_guild"), TR("list.tools.sound_guild")},
		        {"quick", channelId, TR("list.quick_replies"), TR("list.tools.quick")},
		        {"drafts", channelId, TR("list.drafts"), TR("list.tools.drafts")},
		        {"format", channelId, TR("list.format"), TR("list.tools.format")},
		        {"read", channelId, TR("list.mark_read"), TR("list.tools.read")},
		        {"readserver", channelId, TR("list.mark_server_read"), TR("list.tools.read_server")},
		        {"archive", channelId, TR("list.archive_thread"), TR("list.tools.archive")},
		        {"follow", channelId, TR("follow.title"), TR("list.tools.follow")},
		        {"recordcall", channelId, TR("record.title"), TR("list.tools.record")},
		        {"soundboard", channelId, TR("soundboard.title"), TR("list.tools.soundboard")},
		        {"voicemsg", channelId, TR("voice_msg.title"), TR("list.tools.voicemsg")},
		        {"mvdtest", channelId, TR("mvdtest.title"), TR("list.tools.mvdtest")},
		        {"videotest", channelId, TR("videotest.title"), TR("list.tools.videotest")},
		        {"photo", channelId, TR("photo.title"), TR("list.tools.photo")},
		        {"commands", channelId, TR("commands.title"), TR("list.tools.commands")},
		        {"themeshare", channelId, TR("theme.share"), TR("list.tools.theme_share")},
		        {"themeimport", channelId, TR("theme.import"), TR("list.tools.theme_import")},
		        {"shot", channelId, TR("screenshot.title"), TR("list.tools.shot")},
		        {"server", channelId, TR("server.title"), TR("list.tools.server")},
		        {"events", channelId, TR("list.events"), TR("list.tools.events")},
		        {"audit", channelId, TR("list.audit"), TR("list.tools.audit")},
		        {"perms", channelId, TR("list.permissions"), TR("list.tools.permissions")},
		        {"leave", channelId, TR("list.leave_server"), TR("list.tools.leave")}};
		break;
	case Mode::FORMAT:
		list = {{"quote", channelId, TR("format.quote"), "> text"},
		        {"code", channelId, TR("format.code_block"), "```text```"},
		        {"inline", channelId, TR("format.inline_code"), "`text`"},
		        {"spoiler", channelId, TR("format.spoiler"), "||text||"},
		        {"bold", channelId, TR("format.bold"), "**text**"},
		        {"italic", channelId, TR("format.italic"), "*text*"},
		        {"underline", channelId, TR("format.underline"), "__text__"},
		        {"strike", channelId, TR("format.strikethrough"), "~~text~~"}};
		break;
	default:
		break;
	}
	std::lock_guard<std::mutex> lock(shared->mutex);
	shared->entries = std::move(list);
	shared->error = (mode == Mode::VOICE_PEOPLE && shared->entries.empty()) ? TR("voice.not_in_call") : "";
	shared->loading = false;
	shared->version++;
}

void ListScreen::fetch(const std::string &path) {
	const std::string token = Config::getInstance().getToken();
	if (token.empty()) {
		std::lock_guard<std::mutex> lock(shared->mutex);
		shared->error = TR("list.error");
		shared->loading = false;
		shared->version++;
		return;
	}

	const bool searching = (mode == Mode::SEARCH);
	const Mode fetchMode = mode;
	const int filterNow = friendFilter;
	auto state = shared;
	Network::NetworkManager::getInstance().enqueue(
	    "https://discord.com/api/v10" + path, "GET", "", Network::RequestPriority::INTERACTIVE,
	    [state, searching, fetchMode, filterNow](Network::HttpResponse &resp) {
		    std::vector<Entry> list;
		    std::string err;

		    rapidjson::Document doc;
		    doc.Parse(resp.body.c_str());
		    if (!resp.success) {
			    err = TR("list.error");
			    if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("message") && doc["message"].IsString()) {
				    err = doc["message"].GetString();
			    }
		    } else if (doc.HasParseError()) {
			    err = TR("list.error");
		    } else if (fetchMode == Mode::GIFS) {
			    // Search answers with a list; trending answers with an object that holds the list in "gifs".
			    const rapidjson::Value *items = nullptr;
			    if (doc.IsArray()) {
				    items = &doc;
			    } else if (doc.IsObject() && doc.HasMember("gifs") && doc["gifs"].IsArray()) {
				    items = &doc["gifs"];
			    }
			    if (!items) {
				    Logger::log("[GIFs] Unexpected answer: %.200s", resp.body.c_str());
			    }
			    auto usable = [](const std::string &u) {
				    // Videos cannot be shown on the 3DS.
				    const size_t q = u.find('?');
				    const std::string path = q == std::string::npos ? u : u.substr(0, q);
				    const auto ends = [&](const char *ext) {
					    const size_t n = strlen(ext);
					    return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
				    };
				    return !u.empty() && !ends(".mp4") && !ends(".webm") && !ends(".mov");
			    };
			    if (items) {
				    for (const auto &g : items->GetArray()) {
					    if (!g.IsObject()) {
						    continue;
					    }
					    std::string link = str(g, "url");
					    if (link.empty()) {
						    link = str(g, "src");
					    }
					    if (link.empty()) {
						    continue;
					    }
					    std::string title = str(g, "title");
					    Entry e{link, "", title.empty() ? "GIF" : title, link};
					    // preview is a small still, gif_src (or src) the animated picture.
					    const std::string still = str(g, "preview");
					    std::string moving = str(g, "gif_src");
					    if (!usable(moving)) {
						    moving = str(g, "src");
					    }
					    if (!usable(moving)) {
						    moving.clear();
					    }
					    e.preview = usable(still) ? still : moving;
					    e.animated = e.preview == moving ? std::string() : moving;
					    list.push_back(e);
				    }
			    }
		    } else if (fetchMode == Mode::SOUNDBOARD) {
			    // Discord's own sounds come as a list; a server's come in an object with "items".
			    const rapidjson::Value *sounds = nullptr;
			    if (doc.IsArray()) {
				    sounds = &doc;
			    } else if (doc.IsObject() && doc.HasMember("items") && doc["items"].IsArray()) {
				    sounds = &doc["items"];
			    }
			    if (sounds) {
				    for (const auto &snd : sounds->GetArray()) {
					    if (snd.IsObject() && !str(snd, "sound_id").empty()) {
						    // channelId carries the server the sound belongs to (empty for Discord's own).
						    list.push_back({str(snd, "sound_id"), str(snd, "guild_id"), str(snd, "name"),
						                    str(snd, "emoji_name")});
					    }
				    }
			    }
		    } else if (fetchMode == Mode::COMMANDS) {
			    if (doc.IsObject() && doc.HasMember("application_commands") && doc["application_commands"].IsArray()) {
				    for (const auto &c : doc["application_commands"].GetArray()) {
					    if (!c.IsObject() || str(c, "id").empty()) {
						    continue;
					    }
					    rapidjson::StringBuffer sb;
					    rapidjson::Writer<rapidjson::StringBuffer> wr(sb);
					    c.Accept(wr);
					    Entry e{str(c, "id"), "", "/" + str(c, "name"), str(c, "description")};
					    e.data = sb.GetString();
					    list.push_back(e);
				    }
			    }
		    } else if (fetchMode == Mode::FRIENDS || fetchMode == Mode::GROUP_PICK) {
			    // type: 1 friend, 2 blocked, 3 incoming request, 4 outgoing request
			    if (doc.IsArray()) {
				    for (const auto &r : doc.GetArray()) {
					    if (!r.IsObject() || !r.HasMember("user") || !r["user"].IsObject() || !r.HasMember("type") ||
					        !r["type"].IsInt()) {
						    continue;
					    }
					    const int type = r["type"].GetInt();
					    const bool wanted = fetchMode == Mode::GROUP_PICK  ? type == 1
					                        : filterNow == 0 ? type == 1
					                        : filterNow == 1 ? (type == 3 || type == 4)
					                                         : type == 2;
					    if (!wanted) {
						    continue;
					    }
					    const auto &u = r["user"];
					    std::string name = str(r, "nickname");
					    if (name.empty()) {
						    name = str(u, "global_name");
					    }
					    if (name.empty()) {
						    name = str(u, "username");
					    }
					    std::string note = "@" + str(u, "username");
					    if (type == 3) {
						    note += "  - " + TR("friends.incoming");
					    } else if (type == 4) {
						    note += "  - " + TR("friends.outgoing");
					    }
					    Entry e{str(u, "id"), "", fetchMode == Mode::GROUP_PICK ? "[ ] " + name : name, note};
					    e.kind = type;
					    list.push_back(e);
				    }
				    std::sort(list.begin(), list.end(), [](const Entry &a, const Entry &b) {
					    return strcasecmp(a.title.c_str(), b.title.c_str()) < 0;
				    });
			    }
		    } else if (fetchMode == Mode::EVENTS) {
			    if (doc.IsArray()) {
				    for (const auto &ev : doc.GetArray()) {
					    if (!ev.IsObject() || str(ev, "id").empty()) {
						    continue;
					    }
					    std::string title = str(ev, "name");
					    if (ev.HasMember("user_count") && ev["user_count"].IsInt()) {
						    title += "  (" + std::to_string(ev["user_count"].GetInt()) + ")";
					    }
					    std::string when = UI::MessageUtils::formatTimestamp(str(ev, "scheduled_start_time"));
					    const std::string description = str(ev, "description");
					    if (!description.empty()) {
						    when += "  " + description;
					    }
					    Entry e{str(ev, "id"), str(ev, "guild_id"), title, when};
					    list.push_back(e);
				    }
			    }
		    } else if (fetchMode == Mode::AUDIT) {
			    if (doc.IsObject() && doc.HasMember("audit_log_entries") && doc["audit_log_entries"].IsArray()) {
				    std::map<std::string, std::string> names;
				    if (doc.HasMember("users") && doc["users"].IsArray()) {
					    for (const auto &u : doc["users"].GetArray()) {
						    if (u.IsObject()) {
							    names[str(u, "id")] = str(u, "global_name").empty() ? str(u, "username") : str(u, "global_name");
						    }
					    }
				    }
				    for (const auto &en : doc["audit_log_entries"].GetArray()) {
					    if (!en.IsObject()) {
						    continue;
					    }
					    const int type = (en.HasMember("action_type") && en["action_type"].IsInt()) ? en["action_type"].GetInt() : 0;
					    const std::string key = "audit." + std::to_string(type);
					    std::string action = TR(key);
					    if (action == key) {
						    action = TR("audit.other") + " " + std::to_string(type);
					    }
					    const std::string who = names.count(str(en, "user_id")) ? names[str(en, "user_id")] : str(en, "user_id");
					    const std::string target = names.count(str(en, "target_id")) ? names[str(en, "target_id")] : "";
					    std::string info = TR("audit.by") + " " + who;
					    if (!target.empty()) {
						    info += "  > " + target;
					    }
					    const std::string reason = str(en, "reason");
					    if (!reason.empty()) {
						    info += "  \"" + reason + "\"";
					    }
					    list.push_back({str(en, "id"), "", action, info});
				    }
			    }
		    } else if (fetchMode == Mode::SESSIONS) {
			    if (doc.IsObject() && doc.HasMember("user_sessions") && doc["user_sessions"].IsArray()) {
				    for (const auto &u : doc["user_sessions"].GetArray()) {
					    if (!u.IsObject() || str(u, "id_hash").empty()) {
						    continue;
					    }
					    std::string device = "?";
					    std::string place;
					    if (u.HasMember("client_info") && u["client_info"].IsObject()) {
						    const auto &ci = u["client_info"];
						    device = str(ci, "os");
						    const std::string platform = str(ci, "platform");
						    if (!platform.empty()) {
							    device += (device.empty() ? "" : " - ") + platform;
						    }
						    place = str(ci, "location");
					    }
					    std::string info = place;
					    const std::string last = str(u, "approx_last_used_time");
					    if (!last.empty()) {
						    info += (info.empty() ? "" : "  ") + UI::MessageUtils::formatTimestamp(last);
					    }
					    list.push_back({str(u, "id_hash"), "", device.empty() ? "?" : device, info});
				    }
			    }
		    } else if (fetchMode == Mode::GALLERY) {
			    if (doc.IsArray()) {
				    for (const auto &m : doc.GetArray()) {
					    if (!m.IsObject() || !m.HasMember("attachments") || !m["attachments"].IsArray()) {
						    continue;
					    }
					    std::string who;
					    if (m.HasMember("author") && m["author"].IsObject()) {
						    who = str(m["author"], "global_name");
						    if (who.empty()) {
							    who = str(m["author"], "username");
						    }
					    }
					    for (const auto &a : m["attachments"].GetArray()) {
						    if (!a.IsObject() || str(a, "url").empty()) {
							    continue;
						    }
						    const std::string type = str(a, "content_type");
						    const bool image = type.rfind("image/", 0) == 0;
						    std::string info = who;
						    if (a.HasMember("size") && a["size"].IsInt64()) {
							    info += "  " + std::to_string((a["size"].GetInt64() + 1023) / 1024) + " KB";
						    }
						    if (image && a.HasMember("width") && a["width"].IsInt() && a.HasMember("height") &&
						        a["height"].IsInt()) {
							    info += "  " + std::to_string(a["width"].GetInt()) + "x" + std::to_string(a["height"].GetInt());
						    }
						    Entry e{str(a, "url"), "", str(a, "filename"), info};
						    e.kind = image ? 1 : 0;
						    list.push_back(e);
					    }
				    }
			    }
		    } else if (fetchMode == Mode::THREADS) {
			    if (doc.IsObject() && doc.HasMember("threads") && doc["threads"].IsArray()) {
				    for (const auto &t : doc["threads"].GetArray()) {
					    if (!t.IsObject() || str(t, "id").empty()) {
						    continue;
					    }
					    std::string info = (t.HasMember("message_count") && t["message_count"].IsInt())
					                           ? std::to_string(t["message_count"].GetInt()) + " " + TR("list.messages")
					                           : "";
					    if (t.HasMember("thread_metadata") && t["thread_metadata"].IsObject() &&
					        t["thread_metadata"].HasMember("archived") && t["thread_metadata"]["archived"].IsBool() &&
					        t["thread_metadata"]["archived"].GetBool()) {
						    info += (info.empty() ? "" : "  ") + TR("list.archived");
					    }
					    list.push_back({str(t, "id"), str(t, "id"), str(t, "name"), info});
				    }
			    }
		    } else if (fetchMode == Mode::MEMBERS) {
			    if (doc.IsArray()) {
				    for (const auto &m : doc.GetArray()) {
					    if (!m.IsObject() || !m.HasMember("user") || !m["user"].IsObject()) {
						    continue;
					    }
					    const auto &u = m["user"];
					    std::string name = str(m, "nick");
					    if (name.empty()) {
						    name = str(u, "global_name");
					    }
					    if (name.empty()) {
						    name = str(u, "username");
					    }
					    list.push_back({str(u, "id"), "", name, "@" + str(u, "username")});
				    }
			    }
		    } else if (fetchMode == Mode::STICKERS) {
			    if (doc.IsArray()) {
				    for (const auto &st : doc.GetArray()) {
					    if (st.IsObject() && !str(st, "id").empty()) {
						    list.push_back({str(st, "id"), "", str(st, "name"), str(st, "description")});
					    }
				    }
			    }
		    } else if (searching) {
			    if (doc.IsObject() && doc.HasMember("messages") && doc["messages"].IsArray()) {
				    for (const auto &group : doc["messages"].GetArray()) {
					    if (!group.IsArray()) {
						    continue;
					    }
					    for (const auto &m : group.GetArray()) {
						    if (m.IsObject() && m.HasMember("hit") && m["hit"].IsBool() && m["hit"].GetBool()) {
							    std::string id, cid, author, time, body;
							    if (messageToEntry(m, id, cid, author, time, body)) {
								    list.push_back({id, cid, author + "  " + time, body});
							    }
						    }
					    }
				    }
			    } else {
				    // Discord answers 202 with "retry_after" while the search index of a server is being built.
				    err = TR("list.search_not_ready");
			    }
		    } else {
			    const rapidjson::Value *array = nullptr;
			    if (doc.IsArray()) {
				    array = &doc;
			    }
			    if (array) {
				    for (const auto &m : array->GetArray()) {
					    std::string id, cid, author, time, body;
					    if (messageToEntry(m, id, cid, author, time, body)) {
						    list.push_back({id, cid, author + "  " + time, body});
					    }
				    }
			    } else if (doc.IsObject() && doc.HasMember("items") && doc["items"].IsArray()) {
				    for (const auto &item : doc["items"].GetArray()) {
					    std::string id, cid, author, time, body;
					    if (item.IsObject() && item.HasMember("message") &&
					        messageToEntry(item["message"], id, cid, author, time, body)) {
						    list.push_back({id, cid, author + "  " + time, body});
					    }
				    }
			    }
		    }

		    std::lock_guard<std::mutex> lock(state->mutex);
		    state->entries = std::move(list);
		    state->error = err;
		    state->loading = false;
		    state->version++;
	    },
	    {{"Authorization", token}});
}

void ListScreen::startFind() {
	std::string text;
	std::vector<Entry> list;
	if (askText(TR("list.find_hint"), query, text, 60, false) && !text.empty()) {
		query = text;
		auto lower = [](std::string s) {
			std::transform(s.begin(), s.end(), s.begin(), ::tolower);
			return s;
		};
		const std::string needle = lower(query);
		auto &client = Discord::DiscordClient::getInstance();
		for (const auto &g : client.getGuilds()) {
			if (lower(g.name).find(needle) != std::string::npos) {
				list.push_back({"guild:" + g.id, "", TR("list.server"), g.name});
			}
			for (const auto &ch : g.channels) {
				// Text, announcement and forum channels.
				if ((ch.type != 0 && ch.type != 5 && ch.type != 15) || !ch.viewable) {
					continue;
				}
				if (lower(ch.name).find(needle) != std::string::npos) {
					list.push_back({"chan:" + ch.id + ":" + std::to_string(ch.type), ch.id, g.name, "#" + ch.name});
				}
			}
			if (list.size() >= 80) {
				break;
			}
		}
		for (const auto &ch : client.getPrivateChannels()) {
			std::string name = ch.name;
			if (name.empty() && !ch.recipients.empty()) {
				name = ch.recipients[0].global_name.empty() ? ch.recipients[0].username : ch.recipients[0].global_name;
			}
			if (!name.empty() && lower(name).find(needle) != std::string::npos) {
				list.push_back({"chan:" + ch.id + ":" + std::to_string(ch.type), ch.id, TR("list.direct_message"), name});
			}
		}
	}
	std::lock_guard<std::mutex> lock(shared->mutex);
	shared->entries = std::move(list);
	shared->loading = false;
	shared->version++;
}

void ListScreen::startMembers() {
	const std::string guildId = Discord::DiscordClient::getInstance().getGuildIdFromChannel(channelId);
	std::string text;
	if (guildId.empty() || guildId == "DM") {
		std::lock_guard<std::mutex> lock(shared->mutex);
		shared->error = TR("list.members_need_server");
		shared->loading = false;
		shared->version++;
		return;
	}
	if (!askText(TR("list.member_hint"), query, text, 40, false) || text.empty()) {
		std::lock_guard<std::mutex> lock(shared->mutex);
		shared->loading = false;
		shared->version++;
		return;
	}
	query = text;
	fetch("/guilds/" + guildId + "/members/search?query=" + urlEncode(query) + "&limit=25");
}

void ListScreen::startGifSearch() {
	std::string text;
	// An empty search shows what is trending.
	if (!askText(TR("list.gif_hint"), query, text, 60, false)) {
		std::lock_guard<std::mutex> lock(shared->mutex);
		shared->loading = false;
		shared->version++;
		return;
	}
	query = text;
	// Discord has changed these requests over time, so each is tried in a few forms until one gives GIFs.
	gifPaths.clear();
	gifTry = 0;
	std::vector<std::string> forms;
	if (query.empty()) {
		forms = {"/gifs/trending?provider=tenor&locale=en-US&media_format=gif",
		         "/gifs/trending?provider=tenor&locale=en-US&media_format=mp4",
		         "/gifs/trending-gifs?provider=tenor&locale=en-US&media_format=mp4", "/gifs/trending?provider=tenor"};
	} else {
		const std::string q = "q=" + urlEncode(query);
		forms = {"/gifs/search?" + q + "&media_format=gif&provider=tenor&locale=en-US",
		         "/gifs/search?" + q + "&media_format=mp4&provider=tenor&locale=en-US",
		         "/gifs/search?" + q + "&provider=tenor&locale=en-US", "/gifs/search?" + q};
	}
	// The form that worked last time goes first, so the wait of the failed ones is only paid once.
	gifVariants.clear();
	for (size_t i = 0; i < forms.size(); i++) {
		const size_t variant = (i + preferredGifVariant) % forms.size();
		gifPaths.push_back(forms[variant]);
		gifVariants.push_back(variant);
	}
	fetch(gifPaths[0]);
}

void ListScreen::startSearch() {
	std::string text;
	if (!askText(TR("list.search_hint"), query, text, 100, false) || text.empty()) {
		std::lock_guard<std::mutex> lock(shared->mutex);
		shared->loading = false;
		shared->version++;
		return;
	}
	query = text;
	const std::string guildId = Discord::DiscordClient::getInstance().getGuildIdFromChannel(channelId);
	if (!guildId.empty() && guildId != "DM") {
		fetch("/guilds/" + guildId + "/messages/search?channel_id=" + channelId + "&content=" + urlEncode(query));
	} else {
		fetch("/channels/" + channelId + "/messages/search?content=" + urlEncode(query));
	}
}

std::string ListScreen::currentGuild() const {
	const std::string guild = Discord::DiscordClient::getInstance().getGuildIdFromChannel(channelId);
	return guild == "DM" ? "" : guild;
}

std::string ListScreen::channelLabel(const std::string &id) const {
	auto &client = Discord::DiscordClient::getInstance();
	for (const auto &g : client.getGuilds()) {
		for (const auto &ch : g.channels) {
			if (ch.id == id) {
				return "#" + ch.name;
			}
		}
	}
	for (const auto &ch : client.getPrivateChannels()) {
		if (ch.id == id) {
			if (!ch.name.empty()) {
				return ch.name;
			}
			if (!ch.recipients.empty()) {
				return ch.recipients[0].global_name.empty() ? ch.recipients[0].username
				                                            : ch.recipients[0].global_name;
			}
		}
	}
	return "";
}

std::string ListScreen::headerTitle() const {
	switch (mode) {
	case Mode::TOOLS:
		return TR("list.tools");
	case Mode::GIFS:
		return query.empty() ? TR("list.gifs") : TR("list.gifs") + ": " + query;
	case Mode::STICKERS:
		return TR("list.stickers");
	case Mode::FIND:
		return query.empty() ? TR("list.find") : TR("list.find") + ": " + query;
	case Mode::MEMBERS:
		return query.empty() ? TR("list.members") : TR("list.members") + ": " + query;
	case Mode::THREADS:
		return TR("list.threads");
	case Mode::EVENTS:
		return TR("list.events");
	case Mode::AUDIT:
		return TR("list.audit");
	case Mode::PERMISSIONS:
		return TR("list.permissions");
	case Mode::MOD:
		return TR("mod.title") + (soundTargetName.empty() ? "" : ": " + soundTargetName);
	case Mode::SOUNDBOARD:
		return TR("soundboard.title");
	case Mode::FOLLOW:
		return TR("follow.title");
	case Mode::COMMANDS:
		return query.empty() ? TR("commands.title") : TR("commands.title") + ": " + query;
	case Mode::SERVER:
		return TR("server.title");
	case Mode::DIAG:
		return TR("settings.diagnostics");
	case Mode::SESSIONS:
		return TR("sessions.title");
	case Mode::THEME_EDIT:
		return TR("settings.theme_editor");
	case Mode::GALLERY:
		return TR("list.gallery");
	case Mode::VOICE_CHANNELS:
		return TR("list.voice_channels");
	case Mode::VOICE_PEOPLE:
		return TR("list.voice_people");
	case Mode::SOUNDS:
		return TR("list.sounds") + (soundTargetName.empty() ? "" : ": " + soundTargetName);
	case Mode::HUB:
		return TR("menu.quick_access");
	case Mode::FRIENDS:
		return TR(friendFilter == 0 ? "friends.title" : friendFilter == 1 ? "friends.pending" : "friends.blocked");
	case Mode::GROUP_PICK:
		return TR("list.new_group");
	case Mode::PINS:
		return TR("list.pins");
	case Mode::SEARCH:
		return query.empty() ? TR("list.search") : TR("list.search") + ": " + query;
	case Mode::MENTIONS:
		return TR("list.mentions");
	case Mode::SAVED:
		return TR("list.saved");
	case Mode::DRAFTS:
		return TR("list.drafts");
	case Mode::QUICK_REPLIES:
		return TR("list.quick_replies");
	case Mode::FORMAT:
		return TR("list.format");
	}
	return "";
}

std::string ListScreen::hintLine() const {
	switch (mode) {
	case Mode::TOOLS:
		return TR("list.hint.tools");
	case Mode::GIFS:
		return TR("list.hint.gifs");
	case Mode::STICKERS:
		return TR("list.hint.stickers");
	case Mode::FIND:
		return TR("list.hint.find");
	case Mode::MEMBERS:
		return TR("list.hint.members");
	case Mode::THREADS:
		return TR("list.hint.threads");
	case Mode::EVENTS:
		return TR("events.hint");
	case Mode::MOD:
		return TR("mod.hint");
	case Mode::SOUNDBOARD:
		return TR("soundboard.hint");
	case Mode::FOLLOW:
		return TR("follow.hint");
	case Mode::COMMANDS:
		return TR("commands.hint");
	case Mode::SERVER:
		return TR("server.hint");
	case Mode::DIAG:
		return TR("diag.hint");
	case Mode::SESSIONS:
		return TR("sessions.hint");
	case Mode::THEME_EDIT:
		return TR("theme.hint");
	case Mode::GALLERY:
		return TR("list.hint.gallery");
	case Mode::VOICE_CHANNELS:
		return TR("list.hint.voice_channels");
	case Mode::VOICE_PEOPLE:
		return TR("list.hint.voice_people");
	case Mode::SOUNDS:
		return TR("list.hint.sounds");
	case Mode::HUB:
		return TR("list.hint.tools");
	case Mode::FRIENDS:
		return TR("list.hint.friends");
	case Mode::GROUP_PICK:
		return TR("list.hint.group");
	case Mode::SEARCH:
		return TR("list.hint.search");
	case Mode::MENTIONS:
	case Mode::SAVED:
		return TR(mode == Mode::SAVED ? "list.hint.saved" : "list.hint.mentions");
	case Mode::DRAFTS:
		return TR("list.hint.drafts");
	case Mode::QUICK_REPLIES:
		return TR("list.hint.quick");
	case Mode::FORMAT:
		return TR("list.hint.format");
	default:
		return TR("list.hint.back");
	}
}

void ListScreen::clampSelection() {
	const int n = (int)entries.size();
	selected = n == 0 ? 0 : std::clamp(selected, 0, n - 1);
	if (selected < (int)scroll) {
		scroll = (float)selected;
	}
	if (selected >= (int)scroll + VISIBLE_ROWS) {
		scroll = (float)(selected - VISIBLE_ROWS + 1);
	}
}

void ListScreen::primaryAction(const Entry &entry) {
	auto &client = Discord::DiscordClient::getInstance();
	auto &sm = ScreenManager::getInstance();
	switch (mode) {
	case Mode::VOICE_CHANNELS: {
		auto &voice = Discord::VoiceClient::getInstance();
		if (entry.id == "leave") {
			voice.disconnect();
			sm.showToast(TR("voice.left"));
			load();
		} else if (entry.id.rfind("join:", 0) == 0 && !moveUser.empty()) {
			const size_t split = entry.id.rfind(':');
			const auto r = Discord::GuildTools::moveToVoiceChannel(entry.id.substr(5, split - 5), moveUser,
			                                                      entry.id.substr(split + 1));
			sm.showToast(r.ok ? TR("mod.done") : r.message);
			if (r.ok) {
				sm.returnToPreviousScreen();
			}
		} else if (entry.id.rfind("join:", 0) == 0) {
			const size_t split = entry.id.rfind(':');
			const std::string guildId = entry.id.substr(5, split - 5);
			const std::string voiceChannel = entry.id.substr(split + 1);
			const bool idle = voice.getState() == Discord::VoiceState::DISCONNECTED ||
			                  voice.getState() == Discord::VoiceState::FAILED;
			if (!idle && voice.getChannelId() == voiceChannel) {
				voice.disconnect();
				sm.showToast(TR("voice.left"));
			} else {
				voice.connect(guildId, voiceChannel);
				sm.showToast(TR("voice.joining"));
			}
			sm.returnToPreviousScreen();
		}
		break;
	}
	case Mode::VOICE_PEOPLE:
		changePersonVolume(entry.id, +10);
		break;
	case Mode::EVENTS: {
		const auto r = Discord::GuildTools::setEventInterest(entry.channelId.empty() ? currentGuild() : entry.channelId,
		                                                     entry.id, true);
		sm.showToast(r.ok ? TR("events.interested") : r.message);
		break;
	}
	case Mode::MOD: {
		// Every action takes a second press on the same row: these cannot be undone by the app.
		if (confirmId != entry.id) {
			confirmId = entry.id;
			sm.showToast(TR("friends.confirm"));
			break;
		}
		confirmId.clear();
		Discord::GuildTools::Result r;
		if (entry.id == "kick") {
			r = Discord::GuildTools::kick(modGuild, modUser);
		} else if (entry.id == "ban") {
			r = Discord::GuildTools::ban(modGuild, modUser);
		} else if (entry.id.rfind("timeout:", 0) == 0) {
			r = Discord::GuildTools::timeout(modGuild, modUser, atol(entry.id.c_str() + 8));
		}
		sm.showToast(r.ok ? TR("mod.done") : r.message);
		if (r.ok) {
			sm.returnToPreviousScreen();
		}
		break;
	}
	case Mode::SOUNDBOARD: {
		auto &voice = Discord::VoiceClient::getInstance();
		if (voice.getState() != Discord::VoiceState::ESTABLISHED) {
			sm.showToast(TR("soundboard.need_call"));
			break;
		}
		const auto r = Discord::GuildTools::playSoundboard(voice.getChannelId(), entry.id, entry.channelId);
		sm.showToast(r.ok ? TR("soundboard.played") : r.message);
		break;
	}
	case Mode::FOLLOW: {
		const auto r = Discord::GuildTools::followAnnouncements(channelId, entry.id);
		sm.showToast(r.ok ? TR("follow.done") : r.message);
		if (r.ok) {
			sm.returnToPreviousScreen();
		}
		break;
	}
	case Mode::COMMANDS: {
		std::vector<Discord::Commands::Option> options;
		bool supported = true;
		if (!Discord::Commands::readOptions(entry.data, options, supported) || !supported) {
			sm.showToast(TR("commands.unsupported"));
			break;
		}
		std::vector<std::pair<std::string, std::string>> values;
		bool cancelled = false;
		for (const auto &opt : options) {
			std::string value;
			if (!askText(opt.name + (opt.description.empty() ? "" : ": " + opt.description), "", value, 200, false)) {
				cancelled = true;
				break;
			}
			values.push_back({opt.name, value});
		}
		if (cancelled) {
			sm.showToast(TR("commands.cancelled"));
			break;
		}
		const auto r = Discord::Commands::invoke(channelId, currentGuild(), client.getSessionId(), entry.data, values);
		sm.showToast(r.ok ? TR("commands.sent") : r.message);
		if (r.ok) {
			sm.returnToPreviousScreen();
		}
		break;
	}
	case Mode::SERVER: {
		const std::string guildId = currentGuild();
		if (guildId.empty()) {
			break;
		}
		Discord::GuildTools::NotifyChange c;
		const std::string &id = entry.id;
		bool notify = true;
		if (id == "s_mute" || id == "s_unmute") {
			c.muted = id == "s_mute";
		} else if (id == "s_all" || id == "s_mentions" || id == "s_none") {
			c.messageNotifications = id == "s_all" ? 0 : id == "s_mentions" ? 1 : 2;
		} else if (id == "s_everyone_on" || id == "s_everyone_off") {
			c.suppressEveryone = id == "s_everyone_off";
		} else if (id == "s_roles_on" || id == "s_roles_off") {
			c.suppressRoles = id == "s_roles_off";
		} else if (id == "s_push_on" || id == "s_push_off") {
			c.mobilePush = id == "s_push_on";
		} else {
			notify = false;
		}
		Discord::GuildTools::Result r{true, 0, ""};
		if (notify) {
			r = Discord::GuildTools::setNotifications(guildId, c);
		} else if (id == "s_nick") {
			std::string nick;
			if (!askText(TR("list.nick_hint"), "", nick, 32, false)) {
				break;
			}
			r = Discord::Social::setMyNickname(guildId, nick);
		} else if (id == "s_bio") {
			std::string bio;
			if (!askText(TR("server.bio"), "", bio, 190, false)) {
				break;
			}
			r = Discord::GuildTools::setServerBio(guildId, bio);
		} else if (id == "s_sound") {
			const Discord::Guild *g = client.getGuildPtr(guildId);
			openSounds(guildId, g ? g->name : "");
			break;
		} else if (id == "s_name" || id == "s_description") {
			const Discord::Guild *g = client.getGuildPtr(guildId);
			const bool isName = id == "s_name";
			std::string value;
			if (!askText(TR(isName ? "server.name_hint" : "server.description_hint"),
			             g ? (isName ? g->name : g->description) : "", value, isName ? 100 : 120, false) ||
			    (isName && value.empty())) {
				break;
			}
			r = Discord::GuildTools::editGuildText(guildId, isName ? "name" : "description", value);
		}
		sm.showToast(r.ok ? TR("account.saved") : r.message);
		break;
	}
	case Mode::SESSIONS: {
		// Logging a device out takes a second press on the same row.
		if (confirmId != entry.id) {
			confirmId = entry.id;
			sm.showToast(TR("sessions.confirm"));
			break;
		}
		confirmId.clear();
		const auto r = Discord::Social::logoutSession(entry.id);
		sm.showToast(r.ok ? TR("sessions.done") : r.message);
		load();
		break;
	}
	case Mode::THEME_EDIT: {
		const int index = atoi(entry.id.c_str());
		if (index < 0 || index >= (int)(sizeof(THEME_FIELDS) / sizeof(THEME_FIELDS[0]))) {
			break;
		}
		std::string text;
		if (!askText(TR("theme.color_hint"), entry.body, text, 9, false)) {
			break;
		}
		if (!text.empty() && text[0] != '#') {
			text = "#" + text;
		}
		bool valid = text.size() == 7 || text.size() == 9;
		for (size_t i = 1; valid && i < text.size(); i++) {
			valid = isxdigit((unsigned char)text[i]) != 0;
		}
		if (!valid) {
			sm.showToast(TR("theme.invalid_color"));
			break;
		}
		editTheme.*(THEME_FIELDS[index].color) = Utils::Color::hexToColor(text);
		// Shown at once, so the colors can be judged on the real screens.
		Config::getInstance().setCustomTheme(editTheme);
		Config::getInstance().setCustomThemeEnabled(true);
		Utils::requestRedraw();
		load();
		break;
	}
	case Mode::GALLERY:
		if (entry.kind == 1) {
			ImageViewerScreen::open(entry.id, entry.title);
		} else {
			Utils::Transfers::getInstance().saveAttachment(entry.id, entry.title, 0);
		}
		break;
	case Mode::SOUNDS: {
		int value = -1;
		Utils::Sound preview = Utils::Sound::NOTIFICATION;
		bool silent = false;
		if (entry.id != "default") {
			value = atoi(entry.id.c_str());
			silent = (value == 0);
			// Rows 1..7 map to the sounds listed in loadLocal.
			static const Utils::Sound order[] = {Utils::Sound::NOTIFICATION,  Utils::Sound::VOICE_JOIN,
			                                     Utils::Sound::VOICE_LEFT,    Utils::Sound::VOICE_PEER_LEFT,
			                                     Utils::Sound::MIC_ON,        Utils::Sound::HEADPHONE_ON,
			                                     Utils::Sound::CALL_INCOMING};
			if (value >= 1 && value <= 7) {
				preview = order[value - 1];
				value = (int)preview + 1;
			}
		}
		if (entry.id == "default" && soundTarget == "call_ringtone") {
			preview = Utils::Sound::CALL_INCOMING;
		}
		Core::LocalStore::getInstance().setSoundOverride(soundTarget, value);
		if (!silent) {
			Utils::SoundPlayer::getInstance().play(preview);
		}
		load();
		break;
	}
	case Mode::HUB:
		if (entry.id == "find") {
			openReplacing(Mode::FIND);
		} else if (entry.id == "friends") {
			openReplacing(Mode::FRIENDS);
		} else if (entry.id == "group") {
			openReplacing(Mode::GROUP_PICK);
		} else if (entry.id == "join") {
			std::string link;
			if (askText(TR("join.hint"), "", link, 100, false) && !link.empty()) {
				const auto r = Discord::GuildTools::joinInvite(link);
				sm.showToast(r.ok ? TR("join.done") : r.message);
				if (r.ok) {
					sm.setScreen(ScreenType::GUILD_LIST);
				}
			}
		} else if (entry.id == "voice") {
			openReplacing(Mode::VOICE_CHANNELS);
		} else if (entry.id == "people") {
			openReplacing(Mode::VOICE_PEOPLE);
		} else if (entry.id == "saved") {
			openReplacing(Mode::SAVED);
		} else if (entry.id == "mentions") {
			openReplacing(Mode::MENTIONS);
		}
		break;
	case Mode::FRIENDS:
		if (entry.kind == 1) {
			const auto dm = Discord::Social::openDm(entry.id);
			if (dm.result.ok && !dm.channelId.empty()) {
				client.setSelectedChannelId(dm.channelId);
				sm.setScreen(ScreenType::MESSAGES);
			} else {
				sm.showToast(dm.result.message);
			}
		} else if (entry.kind == 3) {
			const auto r = Discord::Social::acceptRequest(entry.id);
			sm.showToast(r.ok ? TR("friends.accepted") : r.message);
			load();
		} else if (entry.kind == 4) {
			sm.showToast(TR("friends.waiting"));
		}
		break;
	case Mode::GROUP_PICK: {
		if (selected >= 0 && selected < (int)entries.size()) {
			Entry &row = entries[selected];
			if (groupIds.erase(row.id)) {
				row.title = "[ ] " + row.title.substr(4);
			} else {
				groupIds.insert(row.id);
				row.title = "[x] " + row.title.substr(4);
			}
		}
		break;
	}
	case Mode::TOOLS:
		if (entry.id == "gallery") {
			openReplacing(Mode::GALLERY);
		} else if (entry.id == "threads") {
			openReplacing(Mode::THREADS);
		} else if (entry.id == "members") {
			openReplacing(Mode::MEMBERS);
		} else if (entry.id == "sound_channel") {
			openSounds(channelId, channelLabel(channelId));
		} else if (entry.id == "sound_guild") {
			const std::string guildId = client.getGuildIdFromChannel(channelId);
			if (guildId.empty() || guildId == "DM") {
				sm.showToast(TR("list.members_need_server"));
			} else {
				const Discord::Guild *g = client.getGuildPtr(guildId);
				openSounds(guildId, g ? g->name : "");
			}
		} else if (entry.id == "follow") {
			bool announcement = false;
			for (const auto &g : client.getGuilds()) {
				for (const auto &ch : g.channels) {
					if (ch.id == channelId && ch.type == 5) {
						announcement = true;
					}
				}
			}
			if (announcement) {
				openReplacing(Mode::FOLLOW);
			} else {
				sm.showToast(TR("follow.not_announcement"));
			}
		} else if (entry.id == "recordcall") {
			auto &recorder = Utils::CallRecorder::getInstance();
			if (recorder.isRecording()) {
				recorder.stop();
				sm.showToast(TR("record.saved"));
			} else if (Discord::VoiceClient::getInstance().getState() != Discord::VoiceState::ESTABLISHED) {
				sm.showToast(TR("soundboard.need_call"));
			} else if (recorder.start()) {
				sm.showToast(TR("record.started"));
				sm.returnToPreviousScreen();
			} else {
				sm.showToast(TR("record.failed"));
			}
		} else if (entry.id == "soundboard") {
			if (Discord::VoiceClient::getInstance().getState() != Discord::VoiceState::ESTABLISHED) {
				sm.showToast(TR("soundboard.need_call"));
			} else {
				openReplacing(Mode::SOUNDBOARD);
			}
		} else if (entry.id == "voicemsg") {
			VoiceRecorderScreen::open(channelId);
		} else if (entry.id == "mvdtest") {
			sm.returnToPreviousScreen();
			MvdTestScreen::open();
		} else if (entry.id == "videotest") {
			sm.returnToPreviousScreen();
			VideoTestScreen::open();
		} else if (entry.id == "photo") {
			// Camera mode: the live view, A takes the photo, and it is shown before it is sent.
			sm.returnToPreviousScreen();
			CameraScreen::open(channelId);
		} else if (entry.id == "commands") {
			openReplacing(Mode::COMMANDS);
		} else if (entry.id == "themeshare") {
			// The theme in use goes to themes/ and from there to the chat as a .json file anyone can import.
			Theme theme = Config::getInstance().getTheme();
			if (theme.name.empty()) {
				theme.name = "Shared Theme";
			}
			if (Config::getInstance().saveThemeToFile(theme.name, theme)) {
				std::string clean;
				for (char ch : theme.name) {
					clean += (std::string("\\/:*?\"<>|").find(ch) != std::string::npos || (unsigned char)ch < 32) ? '_' : ch;
				}
				Utils::Transfers::getInstance().uploadFile(channelId,
				                                           std::string(CONFIG_DIR_PATH) + "/themes/" + clean + ".json", "");
			} else {
				sm.showToast(TR("theme.save_failed"));
			}
		} else if (entry.id == "themeimport") {
			FileBrowserScreen::open(TR("theme.import"), FileBrowserScreen::Filter::ANY, [](const std::string &path) {
				// Copied into themes/ under its own name, then switched on if it reads as a theme.
				std::string name = path.substr(path.find_last_of('/') + 1);
				if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0) {
					name.resize(name.size() - 5);
				}
				const auto bytes = Utils::File::readFile(path);
				const std::string data(bytes.begin(), bytes.end());
				const std::string dir = std::string(CONFIG_DIR_PATH) + "/themes";
				mkdir("sdmc:/3ds", 0777);
				mkdir(CONFIG_DIR_PATH, 0777);
				mkdir(dir.c_str(), 0777);
				auto &sm = ScreenManager::getInstance();
				// Checked under a temporary name first, so a bad file never replaces a theme that works.
				const std::string tempName = "__import_check";
				const std::string tempPath = dir + "/" + tempName + ".json";
				bool good = data.size() > 0 && data.size() < 64 * 1024 && Utils::File::writeFile(tempPath, data) &&
				            Config::getInstance().loadThemeFromFile(tempName);
				remove(tempPath.c_str());
				if (good) {
					std::string finalName = name;
					struct stat st;
					while (stat((dir + "/" + finalName + ".json").c_str(), &st) == 0) {
						finalName += " (imported)";
					}
					good = Utils::File::writeFile(dir + "/" + finalName + ".json", data) &&
					       Config::getInstance().loadThemeFromFile(finalName);
				}
				if (good) {
					Config::getInstance().setCustomThemeEnabled(true);
					sm.showToast(TR("theme.imported"));
				} else {
					sm.showToast(TR("theme.import_failed"));
				}
			});
		} else if (entry.id == "shot") {
			// Close this list first so the picture shows the chat, not the menu.
			sm.returnToPreviousScreen();
			Utils::Screenshot::request(channelId);
		} else if (entry.id == "server") {
			if (currentGuild().empty()) {
				sm.showToast(TR("list.members_need_server"));
			} else {
				openReplacing(Mode::SERVER);
			}
		} else if (entry.id == "events") {
			openReplacing(Mode::EVENTS);
		} else if (entry.id == "audit") {
			openReplacing(Mode::AUDIT);
		} else if (entry.id == "perms") {
			openReplacing(Mode::PERMISSIONS);
		} else if (entry.id == "leave") {
			const std::string guildId = currentGuild();
			if (guildId.empty()) {
				sm.showToast(TR("list.members_need_server"));
			} else if (confirmId != "leave") {
				confirmId = "leave";
				sm.showToast(TR("leave.confirm"));
			} else {
				confirmId.clear();
				const auto r = Discord::GuildTools::leaveGuild(guildId);
				sm.showToast(r.ok ? TR("leave.done") : r.message);
				if (r.ok) {
					sm.setScreen(ScreenType::GUILD_LIST);
				}
			}
		} else if (entry.id == "nick") {
			const std::string guildId = client.getGuildIdFromChannel(channelId);
			if (guildId.empty() || guildId == "DM") {
				sm.showToast(TR("list.members_need_server"));
			} else {
				std::string nick;
				if (askText(TR("list.nick_hint"), "", nick, 32, false)) {
					const auto r = Discord::Social::setMyNickname(guildId, nick);
					sm.showToast(r.ok ? TR("account.saved") : r.message);
				}
			}
		} else if (entry.id == "archive") {
			const auto r = Discord::Threads::setArchived(channelId, true);
			sm.showToast(r.ok ? TR("list.archived_done") : r.message);
			if (r.ok) {
				sm.returnToPreviousScreen();
			}
		} else if (entry.id == "quick") {
			openReplacing(Mode::QUICK_REPLIES);
		} else if (entry.id == "drafts") {
			openReplacing(Mode::DRAFTS);
		} else if (entry.id == "format") {
			openReplacing(Mode::FORMAT);
		} else if (entry.id == "read") {
			client.markChannelReadLatest(channelId);
			sm.returnToPreviousScreen();
			sm.showToast(TR("list.marked_read"));
		} else if (entry.id == "readserver") {
			const std::string guildId = client.getGuildIdFromChannel(channelId);
			if (!guildId.empty() && guildId != "DM") {
				client.markGuildRead(guildId);
				sm.returnToPreviousScreen();
				sm.showToast(TR("list.marked_read"));
			} else {
				sm.showToast(TR("list.not_in_server"));
			}
		}
		break;
	case Mode::GIFS:
		if (!client.canSendMessage(channelId)) {
			sm.showToast(TR("message.no_permission"));
		} else {
			client.sendMessage(channelId, entry.body);
			sm.returnToPreviousScreen();
		}
		break;
	case Mode::FIND: {
		if (entry.id.rfind("guild:", 0) == 0) {
			sm.setSelectedGuildId(entry.id.substr(6));
			sm.setScreen(ScreenType::GUILD_LIST);
		} else if (entry.id.rfind("chan:", 0) == 0) {
			// "chan:<id>:<type>"; forum channels open as a list of posts.
			const std::string rest = entry.id.substr(5);
			const size_t colon = rest.rfind(':');
			const std::string chId = rest.substr(0, colon);
			const int type = colon == std::string::npos ? 0 : atoi(rest.c_str() + colon + 1);
			if (type == 15) {
				sm.setForumChannelId(chId);
				sm.setScreen(ScreenType::FORUM_CHANNEL);
			} else {
				client.setSelectedChannelId(chId);
				sm.setScreen(ScreenType::MESSAGES);
			}
		}
		break;
	}
	case Mode::MEMBERS:
		ProfileScreen::open(entry.id, client.getGuildIdFromChannel(channelId));
		break;
	case Mode::THREADS:
		client.setSelectedChannelId(entry.id);
		sm.setScreen(ScreenType::MESSAGES);
		break;
	case Mode::STICKERS:
		if (!client.canSendMessage(channelId)) {
			sm.showToast(TR("message.no_permission"));
		} else {
			Utils::Transfers::getInstance().sendSticker(channelId, entry.id);
			sm.returnToPreviousScreen();
		}
		break;
	case Mode::MENTIONS:
	case Mode::SAVED:
		if (!entry.channelId.empty()) {
			client.setSelectedChannelId(entry.channelId);
			sm.setScreen(ScreenType::MESSAGES);
		}
		break;
	case Mode::DRAFTS:
		setChannelDraft(entry.channelId, entry.body);
		client.setSelectedChannelId(entry.channelId);
		sm.setScreen(ScreenType::MESSAGES);
		sm.showToast(TR("list.draft_loaded"));
		break;
	case Mode::QUICK_REPLIES:
		if (!client.canSendMessage(channelId)) {
			sm.showToast(TR("message.no_permission"));
		} else {
			client.sendMessage(channelId, entry.body);
			sm.returnToPreviousScreen();
		}
		break;
	case Mode::FORMAT: {
		std::string text;
		if (!askText(entry.title, "", text, 1800, true) || text.empty()) {
			break;
		}
		std::string out;
		if (entry.id == "quote") {
			size_t pos = 0;
			while (pos <= text.size()) {
				size_t end = text.find('\n', pos);
				if (end == std::string::npos) {
					end = text.size();
				}
				out += "> " + text.substr(pos, end - pos) + "\n";
				pos = end + 1;
			}
			if (!out.empty()) {
				out.pop_back();
			}
		} else if (entry.id == "code") {
			out = "```\n" + text + "\n```";
		} else if (entry.id == "inline") {
			out = "`" + text + "`";
		} else if (entry.id == "spoiler") {
			out = "||" + text + "||";
		} else if (entry.id == "bold") {
			out = "**" + text + "**";
		} else if (entry.id == "italic") {
			out = "*" + text + "*";
		} else if (entry.id == "underline") {
			out = "__" + text + "__";
		} else {
			out = "~~" + text + "~~";
		}
		const std::string current = getChannelDraft(channelId);
		setChannelDraft(channelId, current.empty() ? out : current + "\n" + out);
		sm.returnToPreviousScreen();
		sm.showToast(TR("list.format_added"));
		break;
	}
	default:
		break;
	}
}

void ListScreen::secondaryAction(const Entry &entry) {
	auto &store = Core::LocalStore::getInstance();
	auto &sm = ScreenManager::getInstance();
	switch (mode) {
	case Mode::SAVED:
		store.removeSaved(entry.id);
		load();
		break;
	case Mode::DRAFTS:
		store.removeDraft((size_t)selected);
		load();
		break;
	case Mode::VOICE_PEOPLE:
		changePersonVolume(entry.id, -10);
		break;
	case Mode::EVENTS: {
		const auto r = Discord::GuildTools::setEventInterest(entry.channelId.empty() ? currentGuild() : entry.channelId,
		                                                     entry.id, false);
		sm.showToast(r.ok ? TR("events.not_interested") : r.message);
		break;
	}
	case Mode::DIAG: {
		Network::HttpClient http;
		http.setTimeout(10);
		const u64 start = osGetTime();
		const auto resp = http.get("https://discord.com/api/v10/gateway");
		diagLatencyMs = resp.success ? (long)(osGetTime() - start) : -1;
		if (!resp.success) {
			sm.showToast(TR("diag.no_network"));
		}
		load();
		break;
	}
	case Mode::GALLERY:
		Utils::Transfers::getInstance().saveAttachment(entry.id, entry.title, 0);
		break;
	case Mode::THEME_EDIT:
		editTheme = Config::presetTheme(Config::getInstance().getThemeType());
		Config::getInstance().setCustomTheme(editTheme);
		Config::getInstance().setCustomThemeEnabled(true);
		Utils::requestRedraw();
		load();
		break;
	case Mode::FRIENDS: {
		// Removing someone needs a second press on the same row.
		if (confirmId != entry.id) {
			confirmId = entry.id;
			ScreenManager::getInstance().showToast(TR("friends.confirm"));
			break;
		}
		confirmId.clear();
		const auto r = Discord::Social::removeRelationship(entry.id);
		ScreenManager::getInstance().showToast(r.ok ? TR("friends.done") : r.message);
		load();
		break;
	}
	case Mode::THREADS: {
		const auto r = Discord::Threads::join(entry.id);
		ScreenManager::getInstance().showToast(r.ok ? TR("list.thread_joined") : r.message);
		break;
	}
	case Mode::QUICK_REPLIES: {
		std::string text;
		if (askText(TR("list.edit_reply"), entry.body, text, 200, false)) {
			store.setQuickReply((size_t)selected, text);
			load();
		}
		break;
	}
	default:
		break;
	}
}

void ListScreen::tertiaryAction() {
	auto &store = Core::LocalStore::getInstance();
	std::string text;
	switch (mode) {
	case Mode::SEARCH:
	case Mode::GIFS:
	case Mode::FIND:
	case Mode::MEMBERS:
		load();
		break;
	case Mode::THREADS:
		includeArchived = !includeArchived;
		load();
		break;
	case Mode::SOUNDBOARD:
		soundSource = soundSource == 0 ? 1 : 0;
		load();
		break;
	case Mode::COMMANDS: {
		std::string text;
		if (askText(TR("list.search_hint"), query, text, 50, false)) {
			query = text;
			load();
		}
		break;
	}
	case Mode::VOICE_PEOPLE:
		if (selected >= 0 && selected < (int)entries.size()) {
			changePersonVolume(entries[selected].id, 0);
		}
		break;
	case Mode::DIAG:
		writeDiagnosticsReport();
		break;
	case Mode::THEME_EDIT: {
		std::string name;
		if (askText(TR("theme.name_hint"), editTheme.name == "Custom Theme" ? "" : editTheme.name, name, 40, false) &&
		    !name.empty()) {
			editTheme.name = name;
			if (Config::getInstance().saveThemeToFile(name, editTheme)) {
				Config::getInstance().loadThemeFromFile(name);
				Config::getInstance().setCustomThemeEnabled(true);
				ScreenManager::getInstance().showToast(TR("theme.saved"));
			} else {
				ScreenManager::getInstance().showToast(TR("theme.save_failed"));
			}
		}
		break;
	}
	case Mode::FRIENDS:
		friendFilter = (friendFilter + 1) % 3;
		confirmId.clear();
		load();
		break;
	case Mode::GROUP_PICK: {
		if (groupIds.size() < 2) {
			ScreenManager::getInstance().showToast(TR("friends.pick_two"));
			break;
		}
		const std::vector<std::string> ids(groupIds.begin(), groupIds.end());
		const auto dm = Discord::Social::createGroupDm(ids);
		if (dm.result.ok && !dm.channelId.empty()) {
			Discord::DiscordClient::getInstance().setSelectedChannelId(dm.channelId);
			ScreenManager::getInstance().setScreen(ScreenType::MESSAGES);
		} else {
			ScreenManager::getInstance().showToast(dm.result.message);
		}
		break;
	}
	case Mode::QUICK_REPLIES:
		if (askText(TR("list.new_reply"), "", text, 200, false)) {
			store.addQuickReply(text);
			load();
		}
		break;
	case Mode::DRAFTS:
		// New draft for the current channel, starting from what is typed there.
		if (askText(TR("list.new_draft"), getChannelDraft(channelId), text, 1800, true) && !text.empty()) {
			store.addDraft({text, channelId});
			load();
		}
		break;
	default:
		break;
	}
}

void ListScreen::changePersonVolume(const std::string &userId, int delta) {
	auto &voice = Discord::VoiceClient::getInstance();
	const bool self = userId == Discord::DiscordClient::getInstance().getCurrentUser().id;
	const int current = self ? voice.getMicVolume() : voice.getUserVolume(userId);
	const int next = delta == 0 ? 100 : std::clamp(current + delta, 0, 200);
	if (self) {
		Config::getInstance().setMicVolume(next);
		voice.setMicVolume(next);
	} else {
		voice.setUserVolume(userId, next);
	}
	load();
}

// A text file with the state of the console and the app, plus the end of the logs, to send when something fails.
void ListScreen::writeDiagnosticsReport() {
	std::string report = "3DScord diagnostics\n";
	for (const auto &e : entries) {
		if (!e.title.empty()) {
			report += e.title + ": " + e.body + "\n";
		}
	}
	auto tail = [](const std::string &path, size_t bytes) {
		std::vector<char> data = Utils::File::readFile(path);
		if (data.empty()) {
			return std::string("(none)\n");
		}
		std::string text(data.data());
		return text.size() > bytes ? text.substr(text.size() - bytes) : text;
	};
	report += "\n--- updater.log ---\n" + tail(std::string(CONFIG_DIR_PATH) + "/updater.log", 3000);
	report += "\n--- 3dscord.log ---\n" + tail(std::string(CONFIG_DIR_PATH) + "/3dscord.log", 3000);
	// The last lines kept in memory are added too, so the report is useful even with the log file turned off.
	{
		report += "\n--- last log lines (memory) ---\n";
		const auto recent = Logger::getRecentLogs();
		const size_t from = recent.size() > 300 ? recent.size() - 300 : 0;
		for (size_t i = from; i < recent.size(); i++) {
			report += recent[i].substr(0, 300) + "\n";
		}
	}
	const std::string path = std::string(CONFIG_DIR_PATH) + "/diagnostics.txt";
	ScreenManager::getInstance().showToast(Utils::File::writeFile(path, report) ? TR("diag.saved") : TR("diag.save_failed"));
}

void ListScreen::quaternaryAction() {
	if (mode != Mode::FRIENDS) {
		return;
	}
	std::string name;
	if (askText(TR("friends.add_hint"), "", name, 40, false) && !name.empty()) {
		const auto r = Discord::Social::sendFriendRequest(name);
		ScreenManager::getInstance().showToast(r.ok ? TR("friends.sent") : r.message);
		if (r.ok) {
			load();
		}
	}
}

void ListScreen::update() {
	if (mode == Mode::VOICE_PEOPLE && ++refreshTick >= 20) {
		refreshTick = 0;
		loadLocal();
	}
	{
		std::lock_guard<std::mutex> lock(shared->mutex);
		if (shared->version != seenVersion) {
			seenVersion = shared->version;
			Utils::requestRedraw();
			entries = shared->entries;
			error = shared->error;
			loading = shared->loading;
		}
	}
	clampSelection();

	if (mode == Mode::GIFS && !loading && !entries.empty() && gifTry < gifVariants.size()) {
		preferredGifVariant = gifVariants[gifTry];
	}

	// The request gave nothing: the next form of it is tried.
	if (mode == Mode::GIFS && !loading && entries.empty() && gifTry + 1 < gifPaths.size()) {
		gifTry++;
		Logger::log("[GIFs] Trying %s", gifPaths[gifTry].c_str());
		{
			std::lock_guard<std::mutex> lock(shared->mutex);
			shared->loading = true;
			shared->error.clear();
			shared->version++;
		}
		loading = true;
		error.clear();
		fetch(gifPaths[gifTry]);
	}

	if (selected != lastSelected) {
		lastSelected = selected;
		stableFrames = 0;
	} else if (stableFrames < 1000) {
		stableFrames++;
	}

	const u32 kDown = hidKeysDown();
	if (kDown & KEY_B) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	if ((kDown & KEY_DOWN) && selected < (int)entries.size() - 1) {
		selected++;
	}
	if ((kDown & KEY_UP) && selected > 0) {
		selected--;
	}

	// Touch: tap a row to select it, tap it again to act.
	if (kDown & KEY_TOUCH) {
		touchPosition touch;
		hidTouchRead(&touch);
		if (touch.py >= LIST_TOP) {
			const int row = (int)scroll + (int)((touch.py - LIST_TOP) / ROW_HEIGHT);
			if (row >= 0 && row < (int)entries.size()) {
				if (row == selected) {
					primaryAction(entries[row]);
					return;
				}
				selected = row;
			}
		}
	}

	if (!entries.empty() && selected < (int)entries.size()) {
		const Entry entry = entries[selected];
		if (kDown & KEY_A) {
			primaryAction(entry);
			return;
		}
		if (kDown & KEY_X) {
			secondaryAction(entry);
			return;
		}
	}
	if (kDown & KEY_Y) {
		tertiaryAction();
	}
	if (kDown & KEY_R) {
		quaternaryAction();
	}
	clampSelection();
}

void ListScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackground());
	C2D_SceneBegin(target);

	drawText(10.0f, 6.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorText(), fitLine(headerTitle(), 380.0f, 0.55f));
	C2D_DrawRectSolid(10.0f, 28.0f, 0.5f, 380.0f, 1.0f, ScreenManager::colorSeparator());

	if (loading) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), TR("common.loading"), 400.0f);
		return;
	}
	if (!error.empty()) {
		drawCenteredText(110.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorError(), fitLine(error, 380.0f, 0.45f),
		                 400.0f);
		return;
	}
	if (entries.empty() || selected >= (int)entries.size()) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), TR("list.empty"), 400.0f);
		return;
	}

	const Entry &e = entries[selected];
	if (mode == Mode::GIFS && !e.preview.empty()) {
		// The picture of the selected GIF, animated, once the selection has rested for a moment.
		drawText(10.0f, 34.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorAccent(), fitLine(e.title, 380.0f, 0.42f));
		auto &images = ImageManager::getInstance();
		auto info = images.getImageInfo(e.preview);
		// The still shows at once, the heavier animated picture replaces it when it has loaded.
		if (!e.animated.empty()) {
			const auto anim = images.getImageInfo(e.animated);
			if (anim.tex && anim.originalW > 0 && anim.originalH > 0) {
				info = anim;
			} else if (stableFrames > 15 && !anim.failed) {
				images.prefetch(e.animated, 0, 0, Network::RequestPriority::INTERACTIVE);
			}
		}
		if (info.tex && info.originalW > 0 && info.originalH > 0) {
			const float areaW = 380.0f;
			const float areaH = 170.0f;
			const float scale = std::min(areaW / info.originalW, areaH / info.originalH);
			const float w = info.originalW * scale;
			const float h = info.originalH * scale;
			const float uMax = (float)info.originalW / info.tex->width;
			const float vMax = (float)info.originalH / info.tex->height;
			Tex3DS_SubTexture subtex = {(u16)info.originalW, (u16)info.originalH, 0.0f, 1.0f, uMax, 1.0f - vMax};
			C2D_DrawImageAt({info.tex, &subtex}, (TOP_SCREEN_WIDTH - w) / 2.0f, 56.0f + (areaH - h) / 2.0f, 0.5f,
			                nullptr, scale, scale);
			images.markDrawn(info.animation);
		} else if (info.failed) {
			drawCenteredText(130.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), TR("gif.no_preview"),
			                 TOP_SCREEN_WIDTH);
		} else {
			drawCenteredText(130.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), TR("common.loading"),
			                 TOP_SCREEN_WIDTH);
			images.prefetch(e.preview, 0, 0, Network::RequestPriority::INTERACTIVE);
		}
		return;
	}
	if (!e.title.empty()) {
		drawText(10.0f, 36.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorAccent(), fitLine(e.title, 380.0f, 0.42f));
	}
	float y = 56.0f;
	int shown = 0;
	for (const auto &line : MessageUtils::wrapText(e.body, 380.0f, 0.45f)) {
		if (shown++ >= 11) {
			break;
		}
		drawText(10.0f, y, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), line);
		y += 15.0f;
	}
}

void ListScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);

	drawText(10.0f, 6.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), fitLine(headerTitle(), 300.0f, 0.5f));
	C2D_DrawRectSolid(10.0f, 28.0f, 0.5f, 300.0f, 1.0f, ScreenManager::colorSeparator());

	for (int i = 0; i < VISIBLE_ROWS + 1; i++) {
		const int index = (int)scroll + i;
		if (index >= (int)entries.size()) {
			break;
		}
		const Entry &e = entries[index];
		const float y = LIST_TOP + i * ROW_HEIGHT;
		if (y + ROW_HEIGHT > 214.0f) {
			break;
		}
		if (index == selected) {
			C2D_DrawRectSolid(4.0f, y, 0.4f, 312.0f, ROW_HEIGHT - 2.0f, ScreenManager::colorSelection());
		}
		if (mode == Mode::THEME_EDIT) {
			C2D_DrawRectSolid(280.0f, y + 6.0f, 0.5f, 30.0f, ROW_HEIGHT - 12.0f, (u32)e.kind);
		}
		if (!e.title.empty()) {
			drawText(10.0f, y + 2.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorAccent(), fitLine(e.title, 296.0f, 0.38f));
		}
		drawText(10.0f, y + (e.title.empty() ? 9.0f : 17.0f), 0.5f, 0.42f, 0.42f, ScreenManager::colorText(),
		         fitLine(e.body, 296.0f, 0.42f));
	}

	drawCenteredText(218.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), hintLine(), BOTTOM_SCREEN_WIDTH);
}

} // namespace UI
