#ifndef SOFT_DECODER_H
#define SOFT_DECODER_H

#include <cstddef>
#include <cstdint>

class ISVCDecoder;

namespace Utils {

// EXPERIMENTAL. An H.264 decoder in software (openh264, by Cisco, BSD licence) that reads the High profile that Discord's
// cameras use. It runs on one thread only. A decoded picture is planar YUV 4:2:0.
class SoftDecoder {
  public:
	SoftDecoder() = default;
	~SoftDecoder() { shutdown(); }
	SoftDecoder(const SoftDecoder &) = delete;
	SoftDecoder &operator=(const SoftDecoder &) = delete;

	bool init();
	void shutdown();

	struct Picture {
		const uint8_t *y = nullptr;
		const uint8_t *u = nullptr;
		const uint8_t *v = nullptr;
		int width = 0;
		int height = 0;
		int strideY = 0;
		int strideUV = 0;
	};

	// Feeds one NAL unit (with its 00 00 00 01 start code). Returns the decoder's status code (0 is fine) and fills `out` when a
	// whole picture came out; haveFrame says whether it did.
	int decode(const uint8_t *nal, size_t size, Picture &out, bool &haveFrame);

  private:
	ISVCDecoder *decoder = nullptr;
};

} // namespace Utils

#endif // SOFT_DECODER_H
