#include "ui/call_view.h"
#include "core/i18n.h"
#include "discord/avatar_cache.h"
#include "discord/discord_client.h"
#include "discord/voice_client.h"
#include "ui/image_manager.h"
#include "ui/screen_manager.h"
#include "ui/voice_controls.h"
#include "utils/redraw.h"
#include "utils/video_receiver.h"
#include "utils/video_sender.h"
#include <algorithm>
#include <cstdio>

namespace UI {
namespace CallView {

namespace {

Mode currentMode = Mode::COMPACT;
// Added to every depth while the full view is drawn, so it covers whatever the chat drew (its items reach about 0.6).
float zOff = 0.0f;
std::string lastSpeaker;
u64 lastSpokeAt = 0;

const u32 GREEN = C2D_Color32(35, 165, 90, 255);
const u32 RED = C2D_Color32(237, 66, 69, 255);

void drawAvatar(const Discord::VoiceParticipant &p, float x, float y, float size, bool speaking) {
	if (speaking) {
		drawCircle(x + size / 2.0f, y + size / 2.0f, 0.5f + zOff, size / 2.0f + 3.0f, GREEN);
	}
	C3D_Tex *avatar = Discord::AvatarCache::getInstance().getAvatar(p.userId, p.avatar, "0");
	if (avatar) {
		Tex3DS_SubTexture sub = {(u16)avatar->width, (u16)avatar->height, 0.0f, 1.0f, 1.0f, 0.0f};
		C2D_Image img = {avatar, &sub};
		C2D_DrawImageAt(img, x, y, 0.52f + zOff, nullptr, size / avatar->width, size / avatar->height);
	} else {
		drawRoundedRect(x, y, 0.52f + zOff, size, size, 6.0f, ScreenManager::colorBackgroundLight());
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
	C2D_DrawImageAt(img, x, y, 0.56f + zOff, &tint, size / tex->width, size / tex->height);
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

	// The camera or the shared screen that is coming in (see Utils::VideoReceiver), small, under the panel.
	float lineY = y + h + 3.0f;
	Utils::VideoReceiver &rx = Utils::VideoReceiver::getInstance();
	if (rx.isLive()) {
		const float vh = w * 9.0f / 16.0f;
		C2D_DrawRectSolid(x, lineY, 0.5f, w, vh, C2D_Color32(0, 0, 0, 255));
		rx.draw(x, lineY, 0.51f, w, vh);
		lineY += vh + 3.0f;
	}
	// What you send (your camera or your screen), small, so you can see it while you chat.
	Utils::VideoSender &tx = Utils::VideoSender::getInstance();
	if (tx.running() && tx.hasPreview()) {
		const float pw = w * 0.5f;
		const float ph = pw * 0.75f;
		C2D_DrawRectSolid(x + w - pw, lineY, 0.5f, pw, ph, C2D_Color32(0, 0, 0, 255));
		tx.drawPreview(x + w - pw, lineY, 0.51f, pw, ph);
		lineY += ph + 3.0f;
	}
	// Anyone with the camera on or sharing the screen gets a line under the panel, speaking or not.
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

// One person as a rounded tile: avatar in the middle (or their camera), green border while speaking, name and the muted
// icon at the bottom.
void drawTile(const Discord::VoiceParticipant &p, Discord::VoiceClient &voice, float x, float y, float w, float h,
              bool showCamera) {
	const bool speaking = voice.isSpeaking(p.userId);
	const float border = speaking ? 3.0f : 0.0f;
	if (speaking) {
		drawRoundedRect(x, y, 0.5f + zOff, w, h, 10.0f, GREEN);
	}
	drawRoundedRect(x + border, y + border, 0.51f + zOff, w - 2 * border, h - 2 * border, 8.0f, C2D_Color32(17, 18, 20, 255));

	Utils::VideoReceiver &rx = Utils::VideoReceiver::getInstance();
	Utils::VideoSender &tx = Utils::VideoSender::getInstance();
	const bool own = tx.running() && p.userId == Discord::DiscordClient::getInstance().getCurrentUser().id;
	if (own && tx.hasPreview()) {
		// What you are sending (your camera or your screen), where your profile picture would be.
		C2D_DrawRectSolid(x + border + 2.0f, y + border + 2.0f, 0.515f + zOff, w - 2 * border - 4.0f, h - 2 * border - 4.0f,
		                  C2D_Color32(0, 0, 0, 255));
		tx.drawPreview(x + border + 2.0f, y + border + 2.0f, 0.52f + zOff, w - 2 * border - 4.0f, h - 2 * border - 4.0f);
		// How the encoder is doing and what Discord says (only with the debug overlay on, L + R): frames a second, milliseconds a frame, key
		// frames asked for, times it could not keep up; Discord's confirmation (S), RTCP messages (R), frames sent (T), frames that could not be
		// encrypted (E) and the packet mode (V).
		if (ScreenManager::getInstance().isDebugOverlayEnabled()) {
			char stat[64];
			snprintf(stat, sizeof(stat), "%d fps  %d ms  K%d  D%d", tx.statFps(), tx.statMs(), tx.statKeyAsks(), tx.statDrops());
			drawRoundedRect(x + border + 3.0f, y + border + 3.0f, 0.57f + zOff, 112.0f, 12.0f, 4.0f, C2D_Color32(0, 0, 0, 170));
			drawText(x + border + 6.0f, y + border + 3.0f, 0.58f + zOff, 0.34f, 0.34f, ScreenManager::colorText(), stat);
			// What Discord says: its confirmation of our video state (S) and the control messages that come back from the call (R). If
			// someone receives the video, R goes up and K (key frames they ask for) too.
			const int echo = Discord::DiscordClient::getInstance().selfVideoEcho.load();
			char state[64];
			auto &mainVc = Discord::VoiceClient::getInstance();
			auto &vc = mainVc.isScreenSharing() ? Discord::VoiceClient::getStreamInstance() : mainVc; // the screen goes on its own stream
			snprintf(state, sizeof(state), "S%s R%u T%u E%u V%d", echo < 0 ? "?" : echo ? "1" : "0", (unsigned)vc.rtcpWhileSending(),
			         (unsigned)vc.videoFramesSentCount(), (unsigned)vc.videoEncryptFailures(), vc.videoVariantNow());
			drawRoundedRect(x + border + 3.0f, y + border + 17.0f, 0.57f + zOff, 112.0f, 12.0f, 4.0f, C2D_Color32(0, 0, 0, 170));
			drawText(x + border + 6.0f, y + border + 17.0f, 0.58f + zOff, 0.34f, 0.34f, ScreenManager::colorText(), state);
		}
	} else if (showCamera && rx.isLive() && rx.userId() == p.userId) {
		C2D_DrawRectSolid(x + border + 2.0f, y + border + 2.0f, 0.515f + zOff, w - 2 * border - 4.0f, h - 2 * border - 4.0f,
		                  C2D_Color32(0, 0, 0, 255));
		rx.draw(x + border + 2.0f, y + border + 2.0f, 0.52f + zOff, w - 2 * border - 4.0f, h - 2 * border - 4.0f);
	} else {
		const float size = std::max(22.0f, std::min(56.0f, std::min(w, h) * 0.42f));
		drawAvatar(p, x + (w - size) / 2.0f, y + (h - size) / 2.0f - 6.0f, size, false);
	}

	// Name, bottom left, on a dark label.
	const float labelW = std::min(w - 12.0f, 120.0f);
	drawRoundedRect(x + 6.0f, y + h - 20.0f, 0.55f + zOff, labelW, 14.0f, 5.0f, C2D_Color32(0, 0, 0, 150));
	drawRichText(x + 10.0f, y + h - 19.0f, 0.56f + zOff, 0.38f, 0.38f, ScreenManager::colorText(),
	             getTruncatedRichText(p.name, labelW - 8.0f, 0.38f, 0.38f));

	const bool muted = p.mute || p.selfMute;
	const bool deaf = p.deaf || p.selfDeaf;
	float ix = x + w - 20.0f;
	if (muted) {
		drawStateIcon(p.mute ? "romfs:/discord-icons/mic-denied.png" : "romfs:/discord-icons/mic-muted.png", ix,
		              y + h - 19.0f, 13.0f, p.mute ? RED : ScreenManager::colorTextMuted());
		ix -= 16.0f;
	}
	if (deaf) {
		drawStateIcon(p.deaf ? "romfs:/discord-icons/headphones-denied.png" : "romfs:/discord-icons/headphones-muted.png",
		              ix, y + h - 19.0f, 13.0f, p.deaf ? RED : ScreenManager::colorTextMuted());
	}
}

// Everyone as big tiles. Alone: one tile; two: side by side; more: a grid. When someone shares their screen it takes the left
// side and everyone else is stacked on the right.
void drawFull(const std::vector<Discord::VoiceParticipant> &people, Discord::VoiceClient &voice) {
	zOff = 0.4f;
	C2D_DrawRectSolid(0, 0, 0.45f + zOff, 400, 240, C2D_Color32(0, 0, 0, 255));

	constexpr float margin = 6.0f;
	constexpr float gap = 6.0f;
	const float areaX = margin;
	const float areaY = margin;
	const float areaW = 400.0f - 2 * margin;
	const float areaH = 240.0f - 2 * margin - 16.0f; // the bottom strip holds the clock and the statistics

	Utils::VideoReceiver &rx = Utils::VideoReceiver::getInstance();
	const Discord::VoiceParticipant *sharer = nullptr;
	if (rx.isLive()) {
		for (const auto &p : people) {
			if (p.userId == rx.userId() && p.selfStream) {
				sharer = &p;
			}
		}
	}

	bool shownCamera = false;
	if (sharer) {
		const float sideW = 104.0f;
		const float mainW = areaW - sideW - gap;
		C2D_DrawRectSolid(areaX, areaY, 0.5f + zOff, mainW, areaH, C2D_Color32(0, 0, 0, 255));
		rx.draw(areaX, areaY, 0.52f + zOff, mainW, areaH);
		shownCamera = true;
		const int maxSide = 4;
		const int count = std::min<int>((int)people.size(), maxSide);
		const float tileH = (areaH - (count - 1) * gap) / (float)std::max(1, count);
		for (int i = 0; i < count; i++) {
			drawTile(people[i], voice, areaX + mainW + gap, areaY + i * (tileH + gap), sideW, tileH, false);
		}
		if ((int)people.size() > maxSide) {
			drawText(areaX + 4.0f, 226.0f, 0.56f + zOff, 0.36f, 0.36f, ScreenManager::colorTextMuted(),
			         "+" + std::to_string((int)people.size() - maxSide));
		}
	} else {
		const int n = (int)people.size();
		const int cols = n <= 1 ? 1 : n <= 2 ? 2 : n <= 4 ? 2 : n <= 6 ? 3 : n <= 9 ? 3 : 4;
		const int rows = n <= 0 ? 1 : (std::min(n, 12) + cols - 1) / cols;
		const int shown = std::min(n, cols * rows);
		const float tileW = (areaW - (cols - 1) * gap) / (float)cols;
		const float tileH = (areaH - (rows - 1) * gap) / (float)rows;
		for (int i = 0; i < shown; i++) {
			const float tx = areaX + (i % cols) * (tileW + gap);
			const float ty = areaY + (i / cols) * (tileH + gap);
			drawTile(people[i], voice, tx, ty, tileW, tileH, true);
			shownCamera = shownCamera || (rx.isLive() && rx.userId() == people[i].userId);
		}
		if (n > shown) {
			drawText(areaX + 4.0f, 226.0f, 0.56f + zOff, 0.36f, 0.36f, ScreenManager::colorTextMuted(),
			         "+" + std::to_string(n - shown));
		}
	}

	// Call time and statistics of the call, for when the sound is poor.
	char stats[112];
	snprintf(stats, sizeof(stats), "%s   %s %u   %s %u   %s %u", clock(voice.connectedSeconds()).c_str(),
	         TR("call.stat_received").c_str(), (unsigned)voice.statPacketsDecoded(), TR("call.stat_sent").c_str(),
	         (unsigned)voice.statPacketsSent(), TR("call.stat_failed").c_str(), (unsigned)voice.statDecryptFailures());
	drawText(margin + 24.0f, 226.0f, 0.56f + zOff, 0.34f, 0.34f, ScreenManager::colorTextMuted(), stats);
	(void)shownCamera;
	zOff = 0.0f;
}

void drawVideo(Discord::VoiceClient &voice) {
	C2D_DrawRectSolid(0, 0, 0.85f, 400, 240, C2D_Color32(0, 0, 0, 255));
	Utils::VideoReceiver::getInstance().draw(0, 0, 0.9f, 400, 240);
	drawText(6.0f, 4.0f, 0.96f, 0.4f, 0.4f, ScreenManager::colorText(), clock(voice.connectedSeconds()));
}

} // namespace

Mode mode() { return currentMode; }

void cycle() {
	const bool video = Utils::VideoReceiver::getInstance().isLive();
	switch (currentMode) {
	case Mode::COMPACT:
		currentMode = Mode::FULL;
		break;
	case Mode::FULL:
		currentMode = video ? Mode::VIDEO : Mode::HIDDEN;
		break;
	case Mode::VIDEO:
		currentMode = Mode::HIDDEN;
		break;
	default:
		currentMode = Mode::COMPACT;
		break;
	}
	ScreenManager::getInstance().showToast(TR(currentMode == Mode::COMPACT  ? "call.view_compact"
	                                          : currentMode == Mode::FULL  ? "call.view_full"
	                                          : currentMode == Mode::VIDEO ? "call.view_video"
	                                                                       : "call.view_hidden"));
	Utils::requestRedraw();
}

bool coversScreen() {
	return (currentMode == Mode::FULL || currentMode == Mode::VIDEO) && Discord::VoiceClient::getInstance().getState() == Discord::VoiceState::ESTABLISHED;
}

void render() {
	Discord::VoiceClient &voice = Discord::VoiceClient::getInstance();
	if (voice.getState() != Discord::VoiceState::ESTABLISHED || currentMode == Mode::HIDDEN) {
		return;
	}
	Utils::VideoReceiver::getInstance().update();
	// The first picture of a stream you are watching fills the screen (touch L + R to go back).
	static bool pictureWasLive = false;
	const bool pictureLive = Utils::VideoReceiver::getInstance().isLive();
	if (pictureLive && !pictureWasLive && voice.isWatching() && currentMode != Mode::VIDEO) {
		currentMode = Mode::VIDEO;
	}
	pictureWasLive = pictureLive;
	if (Utils::VideoSender::getInstance().running()) {
		Utils::VideoSender::getInstance().updatePreview();
	} else {
		Utils::VideoSender::getInstance().releasePreview();
	}
	if (currentMode == Mode::VIDEO) {
		if (Utils::VideoReceiver::getInstance().isLive()) {
			drawVideo(voice);
			return;
		}
		currentMode = Mode::FULL; // the stream stopped
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
