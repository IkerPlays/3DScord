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

// minimp3 (CC0, https://github.com/lieff/minimp3): an MP3 decoder in one header.
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"

// dr_flac (public domain or MIT-0, https://github.com/mackron/dr_libs): a FLAC decoder in one header.
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_CRC
#include "dr_flac/dr_flac.h"

// stb_vorbis (public domain, https://github.com/nothings/stb): an Ogg Vorbis decoder (built in library/stb_vorbis).
extern "C" {
struct stb_vorbis;
struct stb_vorbis_info {
	unsigned int sample_rate;
	int channels;
	unsigned int setup_memory_required;
	unsigned int setup_temp_memory_required;
	unsigned int temp_memory_required;
	int max_frame_size;
};
stb_vorbis *stb_vorbis_open_memory(const unsigned char *data, int len, int *error, void *alloc_buffer);
stb_vorbis_info stb_vorbis_get_info(stb_vorbis *f);
int stb_vorbis_get_samples_short_interleaved(stb_vorbis *f, int channels, short *buffer, int num_shorts);
void stb_vorbis_close(stb_vorbis *f);
}

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
// Turns what was decoded (any number of channels, any rate) into mono, half the rate when it is high, so a few minutes fit in memory.
void appendMono(std::vector<int16_t> &pcm, const int16_t *samples, int perChannel, int channels, int rate, std::vector<int> &pending,
                int &outRate) {
	const bool halve = rate >= 32000;
	outRate = halve ? rate / 2 : rate;
	for (int i = 0; i < perChannel; i++) {
		int sum = 0;
		for (int c = 0; c < channels; c++) {
			sum += samples[i * channels + c];
		}
		const int mono = sum / std::max(1, channels);
		if (!halve) {
			pcm.push_back((int16_t)mono);
		} else {
			pending.push_back(mono);
			if (pending.size() == 2) {
				pcm.push_back((int16_t)((pending[0] + pending[1]) / 2));
				pending.clear();
			}
		}
	}
}

// MP3: an ID3 tag may come first; then the frames.
bool decodeMp3(const std::string &file, std::vector<int16_t> &pcm, int &rate) {
	const unsigned char *d = (const unsigned char *)file.data();
	size_t pos = 0;
	if (file.size() > 10 && memcmp(d, "ID3", 3) == 0) {
		pos = 10 + (((size_t)d[6] & 0x7F) << 21 | ((size_t)d[7] & 0x7F) << 14 | ((size_t)d[8] & 0x7F) << 7 | ((size_t)d[9] & 0x7F));
	}
	mp3dec_t *dec = (mp3dec_t *)malloc(sizeof(mp3dec_t));
	if (!dec) {
		return false;
	}
	mp3dec_init(dec);
	std::vector<int16_t> frame(MINIMP3_MAX_SAMPLES_PER_FRAME);
	std::vector<int> pending;
	rate = 0;
	while (pos < file.size() && pcm.size() < MAX_SAMPLES) {
		mp3dec_frame_info_t info;
		const int samples = mp3dec_decode_frame(dec, d + pos, (int)(file.size() - pos), frame.data(), &info);
		if (info.frame_bytes <= 0) {
			break;
		}
		pos += (size_t)info.frame_bytes;
		if (samples > 0 && info.channels > 0) {
			int outRate = 0;
			appendMono(pcm, frame.data(), samples, info.channels, info.hz, pending, outRate);
			rate = outRate;
		}
	}
	free(dec);
	return !pcm.empty() && rate > 0;
}

// WAV: PCM of 8 or 16 bits.
bool decodeWav(const std::string &file, std::vector<int16_t> &pcm, int &rate) {
	const unsigned char *d = (const unsigned char *)file.data();
	const size_t size = file.size();
	if (size < 44 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) {
		return false;
	}
	auto le16 = [&](size_t p) { return (int)(d[p] | (d[p + 1] << 8)); };
	auto le32 = [&](size_t p) { return (size_t)(d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | ((size_t)d[p + 3] << 24)); };
	int channels = 0;
	int bits = 0;
	int format = 0;
	int fileRate = 0;
	size_t pos = 12;
	while (pos + 8 <= size) {
		const size_t len = le32(pos + 4);
		const size_t body = pos + 8;
		if (memcmp(d + pos, "fmt ", 4) == 0 && body + 16 <= size) {
			format = le16(body);
			channels = le16(body + 2);
			fileRate = (int)le32(body + 4);
			bits = le16(body + 14);
		} else if (memcmp(d + pos, "data", 4) == 0) {
			if (format != 1 || channels < 1 || (bits != 8 && bits != 16) || fileRate <= 0) {
				return false;
			}
			const size_t end = std::min(size, body + len);
			const size_t bytesPerFrame = (size_t)channels * (bits / 8);
			std::vector<int16_t> chunk((size_t)channels * 1024);
			std::vector<int> pending;
			size_t at = body;
			while (at + bytesPerFrame <= end && pcm.size() < MAX_SAMPLES) {
				const int frames = (int)std::min<size_t>(1024, (end - at) / bytesPerFrame);
				for (int i = 0; i < frames * channels; i++) {
					chunk[i] = bits == 16 ? (int16_t)le16(at + (size_t)i * 2) : (int16_t)(((int)d[at + i] - 128) << 8);
				}
				int outRate = 0;
				appendMono(pcm, chunk.data(), frames, channels, fileRate, pending, outRate);
				rate = outRate;
				at += (size_t)frames * bytesPerFrame;
			}
			return !pcm.empty();
		}
		pos = body + len + (len & 1);
	}
	return false;
}

// Ogg Vorbis (music files; the voice messages are Opus).
bool decodeVorbis(const std::string &file, std::vector<int16_t> &pcm, int &rate) {
	int err = 0;
	stb_vorbis *v = stb_vorbis_open_memory((const unsigned char *)file.data(), (int)file.size(), &err, nullptr);
	if (!v) {
		return false;
	}
	const stb_vorbis_info info = stb_vorbis_get_info(v);
	const int channels = std::max(1, info.channels);
	std::vector<int16_t> chunk((size_t)channels * 1024);
	std::vector<int> pending;
	rate = 0;
	while (pcm.size() < MAX_SAMPLES) {
		const int frames = stb_vorbis_get_samples_short_interleaved(v, channels, chunk.data(), (int)chunk.size());
		if (frames <= 0) {
			break;
		}
		int outRate = 0;
		appendMono(pcm, chunk.data(), frames, channels, (int)info.sample_rate, pending, outRate);
		rate = outRate;
	}
	stb_vorbis_close(v);
	return !pcm.empty() && rate > 0;
}

// FLAC.
bool decodeFlac(const std::string &file, std::vector<int16_t> &pcm, int &rate) {
	drflac *flac = drflac_open_memory(file.data(), file.size(), nullptr);
	if (!flac) {
		return false;
	}
	const int channels = (int)std::max<drflac_uint32>(1, flac->channels);
	std::vector<int16_t> chunk((size_t)channels * 1024);
	std::vector<int> pending;
	rate = 0;
	while (pcm.size() < MAX_SAMPLES) {
		const drflac_uint64 frames = drflac_read_pcm_frames_s16(flac, 1024, chunk.data());
		if (frames == 0) {
			break;
		}
		int outRate = 0;
		appendMono(pcm, chunk.data(), (int)frames, channels, (int)flac->sampleRate, pending, outRate);
		rate = outRate;
	}
	drflac_close(flac);
	return !pcm.empty() && rate > 0;
}

// Picks the decoder by what the file starts with.
bool decodeAudio(const std::string &file, std::vector<int16_t> &pcm, int &rate) {
	if (file.size() >= 4 && memcmp(file.data(), "OggS", 4) == 0) {
		// Opus (voice messages) or Vorbis (music): the first packet says which.
		const size_t look = std::min<size_t>(file.size(), 128);
		if (std::search(file.begin(), file.begin() + look, "OpusHead", "OpusHead" + 8) != file.begin() + look) {
			rate = RATE;
			return decodeOgg(file, pcm);
		}
		return decodeVorbis(file, pcm, rate);
	}
	if (file.size() >= 4 && memcmp(file.data(), "fLaC", 4) == 0) {
		return decodeFlac(file, pcm, rate);
	}
	if (file.size() >= 12 && memcmp(file.data(), "RIFF", 4) == 0) {
		return decodeWav(file, pcm, rate);
	}
	return decodeMp3(file, pcm, rate);
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
	int rate = RATE;
	const bool decoded = decodeAudio(resp.body, pcm, rate);
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
	ndspChnSetRate(NDSP_VOICE_CHANNEL, (float)rate * item.speed);
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
