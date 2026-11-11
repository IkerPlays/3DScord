#ifndef VOICE_RECORDER_SCREEN_H
#define VOICE_RECORDER_SCREEN_H

#include "discord/voice_capture.h"
#include "screen_manager.h"
#include "utils/ogg_opus.h"
#include <memory>
#include <string>
#include <vector>

namespace UI {

// Records a voice message with the microphone and sends it to a channel as an Ogg Opus attachment.
// A starts and stops the recording, then sends it; X records again; B cancels.
class VoiceRecorderScreen : public Screen {
  public:
	static void open(const std::string &channelId);

	VoiceRecorderScreen();
	~VoiceRecorderScreen() override;

	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }
	bool isAnimating() const override { return state == State::RECORDING; }

  private:
	enum class State { IDLE, RECORDING, READY, FAILED };

	void startRecording();
	void stopRecording();
	void pumpMicrophone();
	void discard();
	std::string waveform() const;

	static std::string pendingChannel;

	std::string channelId;
	State state;
	Discord::VoiceCapture capture;
	std::unique_ptr<Utils::OggOpusWriter> writer;
	int frames;
	int level;
	std::vector<uint8_t> peaks; // one loudness value (0..255) per 20 ms frame
	std::string ogg;
};

} // namespace UI

#endif // VOICE_RECORDER_SCREEN_H
