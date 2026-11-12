#include "ui/call_view.h"
#include "core/i18n.h"
#include "discord/avatar_cache.h"
#include "discord/discord_client.h"
#include "discord/voice_client.h"
#include "ui/image_manager.h"
#include "ui/screen_manager.h"
#include "ui/voice_controls.h"
#include "utils/redraw.h"
#include <algorithm>
#include <cstdio>

namespace UI {
namespace CallView {

namespace {

Mode currentMode = Mode::COMPACT;
std::string lastSpeaker;
u64 lastSpokeAt = 0;

const u32 GREEN = C2D_Color32(35, 165, 90, 255);
const u32 RED = C2D_Color32(237, 66, 69, 255);

void drawAvatar(const Discord::VoiceParticipant &p, float x, float y, float size, bool speaking) {
	if (speaking) {
		drawCircle(x + size / 2.0f, y + size / 2.0f, 0.5f, size / 2.0f + 3.0f, GREEN);
	}
	C3D_Tex *avatar = Discord::AvatarCache::getInstance().getAvatar(p.userId, p.avatar, "0");
	if (avatar) {
		Tex3DS_SubTexture sub = {(u16)avatar->width, (u16)avatar->height, 0.0f, 1.0f, 1.0f, 0.0f};
		C2D_Image img = {avatar, &sub};
		C2D_DrawImageAt(img, x, y, 0.52f, nullptr, size / avatar->width, size / avatar->height);
	} else {
		drawRoundedRect(x, y, 0.52f, size, size, 6.0f, ScreenManager::colorBackgroundLight());
	}
}

void drawStateIcon(const char *path, float x, float y, float size, u32 color) {
	C3D_Tex *tex = ImageManager::getInstance().getLocalImage(path);
	if (!tex) {
		return;
	}
	Tex3DS_SubTexture sub = {(u16)tex->width, (u16)tex->height, 0.0f, 1.0f, 1.0f, 0.0f};
	C2D_Image img = {tex, &sub};
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, color, 1.0f);
	C2D_DrawImageAt(img, x, y, 0.56f, &tint, size / tex->width, size / tex->height);
}

std::string clock(int seconds) {
	char buf[16];
	if (seconds >= 3600) {
		snprintf(buf, sizeof(buf), "%d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60);
	} else {
		snprintf(buf, sizeof(buf), "%d:%02d", seconds / 60, seconds % 60);
	}
	return buf;
}

// The camera or the shared screen of someone cannot be shown, so it is marked with a badge.
void drawBadges(const Discord::VoiceParticipant &p, float x, float y) {
	if (p.selfStream) {
		drawRoundedRect(x, y, 0.57f, 28.0f, 11.0f, 3.0f, RED);
		drawText(x + 4.0f, y, 0.58f, 0.38f, 0.38f, ScreenManager::colorText(), "LIVE");
		x += 31.0f;
	}
	if (p.selfVideo) {
		drawRoundedRect(x, y, 0.57f, 28.0f, 11.0f, 3.0f, ScreenManager::colorSelection());
		drawText(x + 5.0f, y, 0.58f, 0.38f, 0.38f, ScreenManager::colorText(), "CAM");
	}
}

void drawCompact(const std::vector<Discord::VoiceParticipant> &people, Discord::VoiceClient &voice) {
	// The one speaking now, or the last one for a moment so the corner does not flicker.
	const Discord::VoiceParticipant *shown = nullptr;
	for (const auto &p : people) {
		if (voice.isSpeaking(p.userId)) {
			shown = &p;
			lastSpeaker = p.userId;
			lastSpokeAt = osGetTime();
			break;
		}
	}
	if (!shown && osGetTime() - lastSpokeAt < 1500) {
		for (const auto &p : people) {
			if (p.userId == lastSpeaker) {
				shown = &p;
			}
		}
	}
	const float w = 128.0f;
	const float h = 48.0f;
	const float x = 400.0f - w - 4.0f;
	const float y = 4.0f;
	drawRoundedRect(x, y, 0.5f, w, h, 8.0f, C2D_Color32(0, 0, 0, 170));
	if (shown) {
		drawAvatar(*shown, x + 6.0f, y + 6.0f, 36.0f, voice.isSpeaking(shown->userId));
		drawRichText(x + 48.0f, y + 6.0f, 0.55f, 0.42f, 0.42f, ScreenManager::colorText(),
		             getTruncatedRichText(shown->name, w - 54.0f, 0.42f, 0.42f));
		drawBadges(*shown, x + 48.0f, y + 22.0f);
	} else {
		drawText(x + 8.0f, y + 6.0f, 0.55f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), TR("call.in_call"));
	}
	drawText(x + 48.0f, y + 32.0f, 0.55f, 0.36f, 0.36f, ScreenManager::colorTextMuted(),
	         clock(voice.connectedSeconds()) + "  " + std::to_string(people.size()));

	// Anyone with the camera on or sharing the screen gets a line under the panel, speaking or not. The video itself
	// cannot be shown on the 3DS.
	float lineY = y + h + 3.0f;
	int lines = 0;
	for (const auto &p : people) {
		if (lines >= 3) {
			break;
		}
		if (!p.selfVideo && !p.selfStream) {
			continue;
		}
		drawRoundedRect(x, lineY, 0.5f, w, 16.0f, 6.0f, C2D_Color32(0, 0, 0, 170));
		drawBadges(p, x + 4.0f, lineY + 2.0f);
		const float badgeW = (p.selfStream ? 31.0f : 0.0f) + (p.selfVideo ? 31.0f : 0.0f);
		drawRichText(x + 6.0f + badgeW, lineY + 2.0f, 0.55f, 0.38f, 0.38f, ScreenManager::colorText(),
		             getTruncatedRichText(p.name, w - 10.0f - badgeW, 0.38f, 0.38f));
		lineY += 19.0f;
		lines++;
	}
}

void drawFull(const std::vector<Discord::VoiceParticipant> &people, Discord::VoiceClient &voice) {
	C2D_DrawRectSolid(0, 0, 0.45f, 400, 240, ScreenManager::colorBackgroundDark());
	drawText(10.0f, 6.0f, 0.55f, 0.5f, 0.5f, ScreenManager::colorSelection(), TR("call.in_call"));
	drawText(330.0f, 6.0f, 0.55f, 0.5f, 0.5f, ScreenManager::colorText(), clock(voice.connectedSeconds()));

	constexpr int cols = 4;
	constexpr int rows = 2;
	constexpr float tileW = 94.0f;
	constexpr float tileH = 88.0f;
	const float startX = (400.0f - cols * tileW) / 2.0f;
	const float startY = 28.0f;

	bool anyVideo = false;
	int shown = 0;
	for (const auto &p : people) {
		if (shown >= cols * rows) {
			break;
		}
		const float tx = startX + (shown % cols) * tileW;
		const float ty = startY + (shown / cols) * tileH;
		const bool speaking = voice.isSpeaking(p.userId);
		drawRoundedRect(tx + 3.0f, ty + 2.0f, 0.5f, tileW - 6.0f, tileH - 4.0f, 8.0f,
		                speaking ? GREEN : ScreenManager::colorBackgroundLight());
		drawRoundedRect(tx + 5.0f, ty + 4.0f, 0.51f, tileW - 10.0f, tileH - 8.0f, 7.0f, ScreenManager::colorBackground());
		drawAvatar(p, tx + (tileW - 44.0f) / 2.0f, ty + 10.0f, 44.0f, false);
		drawRichText(tx + 8.0f, ty + 58.0f, 0.55f, 0.4f, 0.4f, ScreenManager::colorText(),
		             getTruncatedRichText(p.name, tileW - 16.0f, 0.4f, 0.4f));
		drawBadges(p, tx + 8.0f, ty + 8.0f);

		const bool muted = p.mute || p.selfMute;
		const bool deaf = p.deaf || p.selfDeaf;
		float ix = tx + tileW - 22.0f;
		if (deaf) {
			drawStateIcon(p.deaf ? "romfs:/discord-icons/headphones-denied.png" : "romfs:/discord-icons/headphones-muted.png",
			              ix, ty + 70.0f, 12.0f, p.deaf ? RED : ScreenManager::colorTextMuted());
			ix -= 14.0f;
		}
		if (muted) {
			drawStateIcon(p.mute ? "romfs:/discord-icons/mic-denied.png" : "romfs:/discord-icons/mic-muted.png", ix,
			              ty + 70.0f, 12.0f, p.mute ? RED : ScreenManager::colorTextMuted());
		}
		anyVideo = anyVideo || p.selfVideo || p.selfStream;
		shown++;
	}
	if ((int)people.size() > cols * rows) {
		drawText(10.0f, startY + rows * tileH, 0.55f, 0.4f, 0.4f, ScreenManager::colorTextMuted(),
		         "+" + std::to_string((int)people.size() - cols * rows));
	}

	// Statistics of the call, for when the sound is poor.
	char stats[96];
	snprintf(stats, sizeof(stats), "%s %u   %s %u   %s %u", TR("call.stat_received").c_str(),
	         (unsigned)voice.statPacketsDecoded(), TR("call.stat_sent").c_str(), (unsigned)voice.statPacketsSent(),
	         TR("call.stat_failed").c_str(), (unsigned)voice.statDecryptFailures());
	drawText(10.0f, 212.0f, 0.55f, 0.36f, 0.36f, ScreenManager::colorTextMuted(), stats);
	if (anyVideo) {
		drawText(10.0f, 224.0f, 0.55f, 0.36f, 0.36f, ScreenManager::colorTextMuted(), TR("call.no_video"));
	}
}

} // namespace

Mode mode() { return currentMode; }

void cycle() {
	currentMode = currentMode == Mode::COMPACT ? Mode::FULL : currentMode == Mode::FULL ? Mode::HIDDEN : Mode::COMPACT;
	ScreenManager::getInstance().showToast(TR(currentMode == Mode::COMPACT  ? "call.view_compact"
	                                          : currentMode == Mode::FULL ? "call.view_full"
	                                                                      : "call.view_hidden"));
	Utils::requestRedraw();
}

bool coversScreen() {
	return currentMode == Mode::FULL && Discord::VoiceClient::getInstance().getState() == Discord::VoiceState::ESTABLISHED;
}

void render() {
	Discord::VoiceClient &voice = Discord::VoiceClient::getInstance();
	if (voice.getState() != Discord::VoiceState::ESTABLISHED || currentMode == Mode::HIDDEN) {
		return;
	}
	const auto people = VoiceControls::getActiveParticipants();
	if (currentMode == Mode::COMPACT) {
		drawCompact(people, voice);
	} else {
		drawFull(people, voice);
	}
}

} // namespace CallView
} // namespace UI
