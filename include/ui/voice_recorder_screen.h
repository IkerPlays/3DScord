#ifndef VOICE_RECORDER_SCREEN_H
#define VOICE_RECORDER_SCREEN_H

#include "discord/voice_capture.h"
#include "screen_manager.h"
#include "utils/ogg_opus.h"
#include <memory>
#include <string>
#include <vector>

struct OpusEncoder;

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
	bool isAnimating() const override { return state == State::RECORDING || state == State::ENCODING; }

  private:
	enum class State { IDLE, RECORDING, READY, ENCODING, FAILED };

	void startRecording();
	void stopRecording();
	void pumpMicrophone();
	void discard();
	void beginEncoding();
	void encodeStep();
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
	std::vector<int16_t> pcmAll; // what the microphone heard, untouched: the volume is applied when it is encoded
	int gainPercent;             // volume of the message, adjustable after recording
	int encodeFrame;
	OpusEncoder *encoder;
};

} // namespace UI

#endif // VOICE_RECORDER_SCREEN_H
