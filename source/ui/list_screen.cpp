#include "ui/list_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "core/local_store.h"
#include "discord/discord_client.h"
#include "log.h"
#include "network/network_manager.h"
#include "ui/message_screen.h"
#include "utils/message_utils.h"
#include "utils/utf8_utils.h"
#include <3ds.h>
#include <algorithm>
#include <rapidjson/document.h>

namespace UI {

ListScreen::Mode ListScreen::pendingMode = ListScreen::Mode::PINS;

namespace {

constexpr float ROW_HEIGHT = 36.0f;
constexpr float LIST_TOP = 30.0f;
constexpr int VISIBLE_ROWS = 5;

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
	case Mode::TOOLS:
		list = {{"pins", channelId, TR("list.pins"), TR("list.tools.pins")},
		        {"search", channelId, TR("list.search"), TR("list.tools.search")},
		        {"quick", channelId, TR("list.quick_replies"), TR("list.tools.quick")},
		        {"drafts", channelId, TR("list.drafts"), TR("list.tools.drafts")},
		        {"format", channelId, TR("list.format"), TR("list.tools.format")},
		        {"read", channelId, TR("list.mark_read"), TR("list.tools.read")},
		        {"readserver", channelId, TR("list.mark_server_read"), TR("list.tools.read_server")}};
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
	auto state = shared;
	Network::NetworkManager::getInstance().enqueue(
	    "https://discord.com/api/v10" + path, "GET", "", Network::RequestPriority::INTERACTIVE,
	    [state, searching](Network::HttpResponse &resp) {
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
	case Mode::TOOLS:
		if (entry.id == "pins") {
			openReplacing(Mode::PINS);
		} else if (entry.id == "search") {
			openReplacing(Mode::SEARCH);
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
	switch (mode) {
	case Mode::SAVED:
		store.removeSaved(entry.id);
		load();
		break;
	case Mode::DRAFTS:
		store.removeDraft((size_t)selected);
		load();
		break;
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
		load();
		break;
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

void ListScreen::update() {
	{
		std::lock_guard<std::mutex> lock(shared->mutex);
		if (shared->version != seenVersion) {
			seenVersion = shared->version;
			entries = shared->entries;
			error = shared->error;
			loading = shared->loading;
		}
	}
	clampSelection();

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
		if (!e.title.empty()) {
			drawText(10.0f, y + 2.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorAccent(), fitLine(e.title, 296.0f, 0.38f));
		}
		drawText(10.0f, y + (e.title.empty() ? 9.0f : 17.0f), 0.5f, 0.42f, 0.42f, ScreenManager::colorText(),
		         fitLine(e.body, 296.0f, 0.42f));
	}

	drawCenteredText(218.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), hintLine(), BOTTOM_SCREEN_WIDTH);
}

} // namespace UI
