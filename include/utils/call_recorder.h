#ifndef CALL_RECORDER_H
#define CALL_RECORDER_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Utils {

// Saves a call as a WAV file (16 kHz, mono): what everyone says mixed with your own microphone. The audio threads
// only hand over frames; the file is written from the main loop by flush().
class CallRecorder {
  public:
	static CallRecorder &getInstance();

	bool start();
	void stop();
	bool isRecording() const { return recording; }
	std::string path() const { return filePath; }

	void addFar(const int16_t *pcm, int samples);  // what the others say
	void addNear(const int16_t *pcm, int samples); // your microphone

	// Called every frame from the main loop.
	void flush();

  private:
	CallRecorder() = default;
	void finish();

	std::mutex mutex;
	bool recording = false;
	std::string filePath;
	void *file = nullptr; // FILE *
	uint32_t written = 0;
	std::vector<int16_t> far;
	std::vector<int16_t> near;
};

} // namespace Utils

#endif // CALL_RECORDER_H
