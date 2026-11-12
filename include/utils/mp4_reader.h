#ifndef MP4_READER_H
#define MP4_READER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Utils {

// Reads the video track of an MP4 file that is whole in memory (not the "fragmented" kind): the size of the picture, the
// parameter sets of H.264 (AVC) and where each frame is, with its time. Only what a player needs.
struct Mp4Video {
	struct Sample {
		uint32_t offset = 0;
		uint32_t size = 0;
		uint32_t ms = 0; // when the frame is decoded, from the start
		bool key = false;
	};
	int width = 0;
	int height = 0;
	int lengthSize = 4;                  // bytes of each NAL unit's length prefix
	std::vector<uint8_t> parameterSets;  // SPS and PPS in Annex B (with start codes)
	std::vector<Sample> samples;
	uint32_t durationMs = 0;

	// Returns false and says why in `error` (a short English text for the log) when the file cannot be played.
	bool parse(const uint8_t *data, size_t size, std::string &error);
	// Turns one sample (length-prefixed NAL units) into Annex B in `out`, with the parameter sets first when it is a key frame.
	bool toAnnexB(const uint8_t *data, size_t dataSize, const Sample &sample, std::vector<uint8_t> &out) const;
};

} // namespace Utils

#endif // MP4_READER_H
