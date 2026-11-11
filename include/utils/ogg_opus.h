#ifndef OGG_OPUS_H
#define OGG_OPUS_H

#include <cstdint>
#include <string>
#include <vector>

namespace Utils {

// Packs Opus packets (20 ms each) into an Ogg Opus file, the format Discord uses for voice messages.
class OggOpusWriter {
  public:
	// inputRate is the sample rate the audio was recorded at; it is only informative.
	explicit OggOpusWriter(uint32_t inputRate = 16000);

	void addPacket(const uint8_t *data, size_t size);
	size_t packetCount() const { return packets.size(); }

	// The finished file. Call once, after the last packet.
	std::string finish();

  private:
	void writePage(const std::vector<std::vector<uint8_t>> &pagePackets, uint8_t headerType, uint64_t granule);

	uint32_t inputRate;
	uint32_t serial;
	uint32_t sequence = 0;
	std::vector<std::vector<uint8_t>> packets;
	std::string out;
};

} // namespace Utils

#endif // OGG_OPUS_H
