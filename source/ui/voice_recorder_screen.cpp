#include "ui/voice_recorder_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "discord/discord_client.h"
#include "discord/voice_client.h"
#include "log.h"
#include "ui/screen_manager.h"
#include "utils/base64_utils.h"
#include "utils/redraw.h"
#include "utils/transfers.h"
#include <3ds.h>
#include <algorithm>
#include <cstdio>

namespace UI {

std::string VoiceRecorderScreen::pendingChannel;

namespace {
constexpr int FRAME_MS = 20;
constexpr int MAX_FRAMES = 120 * 1000 / FRAME_MS; // two minutes
constexpr int MIN_FRAMES = 500 / FRAME_MS;        // half a second
constexpr int MAX_PACKET = 512;
constexpr int FRAMES_PER_UPDATE = 8;
} // namespace

void VoiceRecorderScreen::open(const std::string &channelId) {
	auto &sm = ScreenManager::getInstance();
	if (!Discord::DiscordClient::getInstance().canSendMessage(channelId)) {
		sm.showToast(TR("message.no_permission"));
		return;
	}
	// The microphone belongs to the call while one is on.
	const auto voiceState = Discord::VoiceClient::getInstance().getState();
	if (voiceState != Discord::VoiceState::DISCONNECTED && voiceState != Discord::VoiceState::FAILED) {
		sm.showToast(TR("voice_msg.in_call"));
		return;
	}
	pendingChannel = channelId;
	sm.pushScreen(ScreenType::VOICE_RECORDER);
}

VoiceRecorderScreen::VoiceRecorderScreen()
    : channelId(pendingChannel), state(State::IDLE), frames(0), level(0) {}

VoiceRecorderScreen::~VoiceRecorderScreen() {
	if (capture.isRunning()) {
		capture.stop();
	}
}

void VoiceRecorderScreen::startRecording() {
	discard();
	if (!capture.start()) {
		state = State::FAILED;
		return;
	}
	writer = std::make_unique<Utils::OggOpusWriter>(Discord::VoiceCapture::ENCODE_RATE);
	frames = 0;
	level = 0;
	peaks.clear();
	state = State::RECORDING;
	Logger::log("[VoiceMessage] Recording");
}

void VoiceRecorderScreen::discard() {
	if (capture.isRunning()) {
		capture.stop();
	}
	writer.reset();
	peaks.clear();
	ogg.clear();
	frames = 0;
	level = 0;
	state = State::IDLE;
}

void VoiceRecorderScreen::pumpMicrophone() {
	int16_t pcm[Discord::VoiceCapture::FRAME_SAMPLES];
	uint8_t packet[MAX_PACKET];
	for (int i = 0; i < FRAMES_PER_UPDATE && frames < MAX_FRAMES && capture.readFrame(pcm); i++) {
		const int encoded = capture.encode(pcm, packet, sizeof(packet));
		if (encoded > 0) {
			writer->addPacket(packet, (size_t)encoded);
			frames++;
			const int peak = capture.lastPeak();
			level = peak;
			peaks.push_back((uint8_t)std::min(255, peak * 255 / 12000));
		}
	}
}

void VoiceRecorderScreen::stopRecording() {
	if (capture.isRunning()) {
		capture.stop();
	}
	if (frames < MIN_FRAMES) {
		ScreenManager::getInstance().showToast(TR("voice_msg.too_short"));
		discard();
		return;
	}
	ogg = writer->finish();
	state = State::READY;
	Logger::log("[VoiceMessage] %d frames, %zu bytes", frames, ogg.size());
}

// Discord draws the message as bars: up to 256 values from 0 to 255.
std::string VoiceRecorderScreen::waveform() const {
	const size_t bars = std::min<size_t>(64, std::max<size_t>(1, peaks.size()));
	std::vector<unsigned char> out(bars, 0);
	for (size_t b = 0; b < bars; b++) {
		const size_t from = b * peaks.size() / bars;
		const size_t to = std::max(from + 1, (b + 1) * peaks.size() / bars);
		unsigned char top = 0;
		for (size_t i = from; i < to && i < peaks.size(); i++) {
			top = std::max(top, peaks[i]);
		}
		out[b] = top;
	}
	return Utils::Base64::encode(out.data(), out.size());
}

void VoiceRecorderScreen::update() {
	const u32 kDown = hidKeysDown();

	if (state == State::RECORDING) {
		pumpMicrophone();
		Utils::requestRedraw();
		if ((kDown & KEY_A) || frames >= MAX_FRAMES) {
			stopRecording();
		} else if (kDown & KEY_B) {
			discard();
			ScreenManager::getInstance().returnToPreviousScreen();
		}
		return;
	}

	if (kDown & KEY_B) {
		discard();
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	if (state == State::IDLE && (kDown & KEY_A)) {
		startRecording();
	} else if (state == State::READY) {
		if (kDown & KEY_A) {
			Utils::Transfers::getInstance().sendVoiceMessage(channelId, ogg, frames * FRAME_MS / 1000.0, waveform());
			ogg.clear();
			ScreenManager::getInstance().returnToPreviousScreen();
		} else if (kDown & KEY_X) {
			startRecording();
		}
	}
}

void VoiceRecorderScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackground());
	C2D_SceneBegin(target);

	const float seconds = frames * FRAME_MS / 1000.0f;
	char time[16];
	snprintf(time, sizeof(time), "%d:%02d", (int)seconds / 60, (int)seconds % 60);

	const u32 red = C2D_Color32(237, 66, 69, 255);
	drawCenteredText(48.0f, 0.5f, 1.2f, 1.2f, state == State::RECORDING ? red : ScreenManager::colorText(), time,
	                 TOP_SCREEN_WIDTH);

	std::string status;
	switch (state) {
	case State::IDLE:
		status = TR("voice_msg.idle");
		break;
	case State::RECORDING:
		status = TR("voice_msg.recording");
		break;
	case State::READY:
		status = TR("voice_msg.ready");
		break;
	case State::FAILED:
		status = TR("voice_msg.no_mic");
		break;
	}
	drawCenteredText(104.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), status, TOP_SCREEN_WIDTH);

	// What the microphone hears right now, and the shape of everything recorded so far.
	const float meterX = 60.0f;
	const float meterW = 280.0f;
	C2D_DrawRectSolid(meterX, 140.0f, 0.5f, meterW, 10.0f, ScreenManager::colorBackgroundLight());
	if (state == State::RECORDING) {
		C2D_DrawRectSolid(meterX, 140.0f, 0.55f, meterW * std::min(1.0f, level / 12000.0f), 10.0f, red);
	}
	if (!peaks.empty()) {
		const size_t shown = std::min<size_t>(peaks.size(), 140);
		const size_t first = peaks.size() - shown;
		for (size_t i = 0; i < shown; i++) {
			const float h = 2.0f + peaks[first + i] / 255.0f * 36.0f;
			C2D_DrawRectSolid(meterX + i * 2.0f, 190.0f - h / 2.0f, 0.5f, 1.5f, h, ScreenManager::colorAccent());
		}
	}
}

void VoiceRecorderScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);

	const char *main = state == State::IDLE        ? "voice_msg.hint_start"
	                   : state == State::RECORDING ? "voice_msg.hint_stop"
	                   : state == State::READY     ? "voice_msg.hint_send"
	                                               : "voice_msg.hint_back";
	drawCenteredText(100.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorSuccess(), TR(main), BOTTOM_SCREEN_WIDTH);
	if (state == State::READY) {
		drawCenteredText(130.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), TR("voice_msg.hint_redo"),
		                 BOTTOM_SCREEN_WIDTH);
	}
	drawCenteredText(160.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), TR("voice_msg.hint_cancel"),
	                 BOTTOM_SCREEN_WIDTH);
}

} // namespace UI
