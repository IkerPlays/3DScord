#include "utils/ogg_opus.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Utils {

namespace {

constexpr uint32_t FRAME_48K = 960; // 20 ms at 48 kHz: Ogg Opus counts granule positions at 48 kHz
constexpr uint16_t PRE_SKIP = 312;
constexpr size_t PACKETS_PER_PAGE = 50;

// Ogg's CRC-32: polynomial 0x04C11DB7, no reflection, start 0, no final xor.
uint32_t crcTable[256];
bool crcReady = false;

void buildCrc() {
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t r = i << 24;
		for (int bit = 0; bit < 8; bit++) {
			r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : (r << 1);
		}
		crcTable[i] = r;
	}
	crcReady = true;
}

uint32_t crc32(const uint8_t *data, size_t size) {
	if (!crcReady) {
		buildCrc();
	}
	uint32_t crc = 0;
	for (size_t i = 0; i < size; i++) {
		crc = (crc << 8) ^ crcTable[((crc >> 24) & 0xFF) ^ data[i]];
	}
	return crc;
}

void put32(std::vector<uint8_t> &v, uint32_t x) {
	for (int i = 0; i < 4; i++) {
		v.push_back((uint8_t)(x >> (8 * i)));
	}
}

void put16(std::vector<uint8_t> &v, uint16_t x) {
	v.push_back((uint8_t)x);
	v.push_back((uint8_t)(x >> 8));
}

} // namespace

OggOpusWriter::OggOpusWriter(uint32_t rate) : inputRate(rate), serial((uint32_t)rand() * 2654435761u + 12345u) {}

void OggOpusWriter::addPacket(const uint8_t *data, size_t size) {
	if (size > 0) {
		packets.emplace_back(data, data + size);
	}
}

void OggOpusWriter::writePage(const std::vector<std::vector<uint8_t>> &pagePackets, uint8_t headerType,
                              uint64_t granule) {
	// Lacing: a packet is a run of 255-byte segments ended by a shorter one (possibly of size 0).
	std::vector<uint8_t> segments;
	size_t dataSize = 0;
	for (const auto &p : pagePackets) {
		size_t left = p.size();
		while (left >= 255) {
			segments.push_back(255);
			left -= 255;
		}
		segments.push_back((uint8_t)left);
		dataSize += p.size();
	}

	std::vector<uint8_t> page;
	page.reserve(27 + segments.size() + dataSize);
	page.insert(page.end(), {'O', 'g', 'g', 'S', 0, headerType});
	for (int i = 0; i < 8; i++) {
		page.push_back((uint8_t)(granule >> (8 * i)));
	}
	put32(page, serial);
	put32(page, sequence++);
	put32(page, 0); // CRC, filled in below
	page.push_back((uint8_t)segments.size());
	page.insert(page.end(), segments.begin(), segments.end());
	for (const auto &p : pagePackets) {
		page.insert(page.end(), p.begin(), p.end());
	}

	const uint32_t crc = crc32(page.data(), page.size());
	for (int i = 0; i < 4; i++) {
		page[22 + i] = (uint8_t)(crc >> (8 * i));
	}
	out.append(reinterpret_cast<const char *>(page.data()), page.size());
}

std::string OggOpusWriter::finish() {
	out.clear();
	sequence = 0;

	// OpusHead: version 1, mono, pre-skip, input rate, no gain, mapping family 0.
	std::vector<uint8_t> head;
	const char *magic = "OpusHead";
	head.insert(head.end(), magic, magic + 8);
	head.push_back(1);
	head.push_back(1);
	put16(head, PRE_SKIP);
	put32(head, inputRate);
	put16(head, 0);
	head.push_back(0);
	writePage({head}, 0x02, 0);

	// OpusTags: vendor string and no comments.
	std::vector<uint8_t> tags;
	const char *tagMagic = "OpusTags";
	tags.insert(tags.end(), tagMagic, tagMagic + 8);
	const char *vendor = "3DScord";
	put32(tags, (uint32_t)strlen(vendor));
	tags.insert(tags.end(), vendor, vendor + strlen(vendor));
	put32(tags, 0);
	writePage({tags}, 0, 0);

	// Audio, a second at a time. The granule of a page is the position after its last packet.
	uint64_t granule = PRE_SKIP;
	size_t index = 0;
	while (index < packets.size()) {
		const size_t count = std::min(PACKETS_PER_PAGE, packets.size() - index);
		std::vector<std::vector<uint8_t>> page(packets.begin() + index, packets.begin() + index + count);
		granule += (uint64_t)count * FRAME_48K;
		index += count;
		writePage(page, index >= packets.size() ? 0x04 : 0, granule);
	}
	if (packets.empty()) {
		writePage({}, 0x04, granule);
	}
	return out;
}

} // namespace Utils
