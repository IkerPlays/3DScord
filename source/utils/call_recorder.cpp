#include "utils/call_recorder.h"
#include "core/config.h"
#include "log.h"
#include "discord/voice_client.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>

namespace Utils {

namespace {
constexpr int RATE = 16000;
constexpr size_t MAX_PENDING = (size_t)RATE * 10; // ten seconds waiting for the main loop at most

void put32(unsigned char *p, uint32_t v) {
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	p[2] = (v >> 16) & 0xFF;
	p[3] = (v >> 24) & 0xFF;
}

void writeHeader(FILE *f, uint32_t dataBytes) {
	unsigned char h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
	                       1,   0,   1,   0,   0, 0, 0, 0, 0,   0,   0,   0,   2,   0,   16,  0,  'd', 'a', 't', 'a',
	                       0,   0,   0,   0};
	put32(h + 4, 36 + dataBytes);
	put32(h + 24, RATE);
	put32(h + 28, RATE * 2);
	put32(h + 40, dataBytes);
	fwrite(h, 1, sizeof(h), f);
}
} // namespace

CallRecorder &CallRecorder::getInstance() {
	static CallRecorder instance;
	return instance;
}

bool CallRecorder::start() {
	std::lock_guard<std::mutex> lock(mutex);
	if (recording) {
		return true;
	}
	mkdir("sdmc:/3ds", 0777);
	mkdir(CONFIG_DIR_PATH, 0777);
	const std::string dir = std::string(CONFIG_DIR_PATH) + "/recordings";
	mkdir(dir.c_str(), 0777);
	char name[64];
	const time_t now = time(nullptr);
	strftime(name, sizeof(name), "/call_%Y%m%d_%H%M%S.wav", gmtime(&now));
	filePath = dir + name;
	FILE *f = fopen(filePath.c_str(), "wb");
	if (!f) {
		return false;
	}
	writeHeader(f, 0);
	file = f;
	written = 0;
	far.clear();
	near.clear();
	recording = true;
	Logger::log("[CallRecorder] Recording to %s", filePath.c_str());
	return true;
}

void CallRecorder::finish() {
	if (!file) {
		return;
	}
	FILE *f = (FILE *)file;
	fseek(f, 0, SEEK_SET);
	writeHeader(f, written);
	fclose(f);
	file = nullptr;
	recording = false;
	Logger::log("[CallRecorder] Saved %u bytes", written);
}

void CallRecorder::stop() {
	std::lock_guard<std::mutex> lock(mutex);
	if (!recording) {
		return;
	}
	// Whatever is waiting goes into the file first.
	const size_t n = std::max(far.size(), near.size());
	far.resize(n, 0);
	near.resize(n, 0);
	if (file && n > 0) {
		std::vector<int16_t> mixed(n);
		for (size_t i = 0; i < n; i++) {
			mixed[i] = (int16_t)std::clamp((int)far[i] + (int)near[i], -32768, 32767);
		}
		fwrite(mixed.data(), sizeof(int16_t), n, (FILE *)file);
		written += (uint32_t)(n * sizeof(int16_t));
	}
	far.clear();
	near.clear();
	finish();
}

void CallRecorder::addFar(const int16_t *pcm, int samples) {
	std::lock_guard<std::mutex> lock(mutex);
	if (recording && far.size() < MAX_PENDING) {
		far.insert(far.end(), pcm, pcm + samples);
	}
}

void CallRecorder::addNear(const int16_t *pcm, int samples) {
	std::lock_guard<std::mutex> lock(mutex);
	if (recording && near.size() < MAX_PENDING) {
		near.insert(near.end(), pcm, pcm + samples);
	}
}

void CallRecorder::flush() {
	std::vector<int16_t> mixed;
	bool callOver = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!recording) {
			return;
		}
		// Both streams advance at the same rate; mix what both already have.
		const size_t n = std::min(far.size(), near.size());
		if (n > 0) {
			mixed.resize(n);
			for (size_t i = 0; i < n; i++) {
				mixed[i] = (int16_t)std::clamp((int)far[i] + (int)near[i], -32768, 32767);
			}
			far.erase(far.begin(), far.begin() + n);
			near.erase(near.begin(), near.begin() + n);
		}
		callOver = Discord::VoiceClient::getInstance().getState() != Discord::VoiceState::ESTABLISHED;
		if (file && !mixed.empty()) {
			fwrite(mixed.data(), sizeof(int16_t), mixed.size(), (FILE *)file);
			written += (uint32_t)(mixed.size() * sizeof(int16_t));
		}
	}
	if (callOver) {
		stop();
	}
}

} // namespace Utils
