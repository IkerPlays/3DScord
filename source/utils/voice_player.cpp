#include "utils/voice_player.h"
#include "log.h"
#include "network/http_client.h"
#include "network/network_manager.h"
#include "utils/system_utils.h"
#include <3ds.h>
#include <algorithm>
#include <cstring>
#include <opus.h>
#include <vector>

namespace Utils {

namespace {
constexpr int NDSP_VOICE_CHANNEL = 20;
constexpr int RATE = 16000;
constexpr size_t MAX_SAMPLES = (size_t)RATE * 180; // three minutes
constexpr size_t MAX_DOWNLOAD = 4 * 1024 * 1024;

// Cuts the Ogg pages into packets and decodes every Opus packet after the two header packets.
bool decodeOgg(const std::string &file, std::vector<int16_t> &pcm) {
	int err = 0;
	OpusDecoder *dec = opus_decoder_create(RATE, 1, &err);
	if (err != OPUS_OK || !dec) {
		return false;
	}
	const unsigned char *d = (const unsigned char *)file.data();
	const size_t size = file.size();
	std::vector<unsigned char> packet;
	int packetNumber = 0;
	std::vector<int16_t> frame(RATE * 120 / 1000 + 16);
	size_t pos = 0;
	bool ok = true;
	while (pos + 27 <= size && ok) {
		if (memcmp(d + pos, "OggS", 4) != 0) {
			ok = false;
			break;
		}
		const int segments = d[pos + 26];
		if (pos + 27 + segments > size) {
			break;
		}
		size_t body = pos + 27 + segments;
		for (int s = 0; s < segments; s++) {
			const int len = d[pos + 27 + s];
			if (body + len > size) {
				ok = false;
				break;
			}
			packet.insert(packet.end(), d + body, d + body + len);
			body += len;
			if (len < 255) {
				// A packet is complete. The first two are the Opus header and the comments.
				if (packetNumber >= 2 && !packet.empty()) {
					const int n = opus_decode(dec, packet.data(), (opus_int32)packet.size(), frame.data(),
					                          (int)frame.size(), 0);
					if (n > 0) {
						pcm.insert(pcm.end(), frame.begin(), frame.begin() + n);
					}
					if (pcm.size() > MAX_SAMPLES) {
						opus_decoder_destroy(dec);
						return true;
					}
				}
				packetNumber++;
				packet.clear();
			}
		}
		pos = body;
	}
	opus_decoder_destroy(dec);
	return !pcm.empty();
}
} // namespace

VoicePlayer &VoicePlayer::getInstance() {
	static VoicePlayer instance;
	return instance;
}

void VoicePlayer::setError(const std::string &text) {
	std::lock_guard<std::mutex> lock(mutex);
	error = text;
}

std::string VoicePlayer::takeError() {
	std::lock_guard<std::mutex> lock(mutex);
	std::string out;
	out.swap(error);
	return out;
}

void VoicePlayer::ensureWorker() {
	if (!worker.joinable()) {
		quit = false;
		worker.start([this] { run(); }, 2, 96 * 1024);
	}
}

void VoicePlayer::play(const std::string &url, float speed) {
	stop();
	enqueue(url, speed);
}

void VoicePlayer::enqueue(const std::string &url, float speed) {
	{
		std::lock_guard<std::mutex> lock(mutex);
		queue.push_back({url, speed});
	}
	busy = true;
	ensureWorker();
}

void VoicePlayer::stop() {
	{
		std::lock_guard<std::mutex> lock(mutex);
		queue.clear();
	}
	generation++;
	stopRequested = true;
}

void VoicePlayer::shutdown() {
	stop();
	quit = true;
	if (worker.joinable()) {
		worker.join();
	}
}

bool VoicePlayer::playOne(const Item &item) {
	const int myGeneration = generation;
	Network::HttpRequest request{item.url, "GET", "", {}, 30};
	Network::HttpResponse resp = Network::NetworkManager::getInstance().perform(std::move(request));
	if (!resp.success || resp.body.empty()) {
		setError("download");
		return false;
	}
	if (resp.body.size() > MAX_DOWNLOAD) {
		setError("size");
		return false;
	}
	std::vector<int16_t> pcm;
	pcm.reserve(RATE * 30);
	const bool decoded = decodeOgg(resp.body, pcm);
	std::string().swap(resp.body);
	if (!decoded) {
		setError("decode");
		return false;
	}
	if (myGeneration != generation) {
		return false; // stopped while it was loading
	}
	const size_t bytes = pcm.size() * sizeof(int16_t);
	int16_t *audio = (int16_t *)linearAlloc(bytes);
	if (!audio) {
		setError("memory");
		return false;
	}
	memcpy(audio, pcm.data(), bytes);
	DSP_FlushDataCache(audio, bytes);
	std::vector<int16_t>().swap(pcm);

	ndspChnReset(NDSP_VOICE_CHANNEL);
	ndspChnSetInterp(NDSP_VOICE_CHANNEL, NDSP_INTERP_LINEAR);
	ndspChnSetRate(NDSP_VOICE_CHANNEL, (float)RATE * item.speed);
	ndspChnSetFormat(NDSP_VOICE_CHANNEL, NDSP_FORMAT_MONO_PCM16);
	float mix[12] = {};
	mix[0] = 1.0f;
	mix[1] = 1.0f;
	ndspChnSetMix(NDSP_VOICE_CHANNEL, mix);

	ndspWaveBuf buf;
	memset(&buf, 0, sizeof(buf));
	buf.data_vaddr = audio;
	buf.nsamples = (u32)(bytes / sizeof(int16_t));
	stopRequested = false;
	ndspChnWaveBufAdd(NDSP_VOICE_CHANNEL, &buf);
	while (buf.status != NDSP_WBUF_DONE && !stopRequested && !quit) {
		svcSleepThread(50 * 1000 * 1000LL);
	}
	ndspChnWaveBufClear(NDSP_VOICE_CHANNEL);
	ndspChnReset(NDSP_VOICE_CHANNEL);
	linearFree(audio);
	return true;
}

void VoicePlayer::run() {
	while (!quit) {
		Item item;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (queue.empty()) {
				busy = false;
				item.url.clear();
			} else {
				item = queue.front();
				queue.pop_front();
			}
		}
		if (item.url.empty()) {
			svcSleepThread(100 * 1000 * 1000LL);
			continue;
		}
		busy = true;
		playOne(item);
	}
	busy = false;
}

} // namespace Utils
