#ifndef H264_ENCODER_H
#define H264_ENCODER_H

#include <cstdint>

namespace Utils {

// A small H.264 (baseline) video encoder for the camera. Frames go in as planar YUV 4:2:0; the width and height must be
// multiples of 16. This is experimental: it is used by the video test and is the base of video calls.
class H264Encoder {
  public:
	H264Encoder() = default;
	~H264Encoder() { shutdown(); }
	H264Encoder(const H264Encoder &) = delete;
	H264Encoder &operator=(const H264Encoder &) = delete;

	// keyFrameEvery: frames between full pictures. bytesPerFrame: the size each frame aims for.
	bool init(int width, int height, int keyFrameEvery, int bytesPerFrame);
	void shutdown();
	bool ready() const { return encoder != nullptr; }

	// Encodes a frame. out and size point at the coded data (valid until the next call). forceKey makes it a key frame.
	bool encode(const uint8_t *y, const uint8_t *u, const uint8_t *v, bool forceKey, const uint8_t **out, int *size);

  private:
	void *encoder = nullptr;
	void *scratch = nullptr;
	int w = 0;
	int h = 0;
	int targetBytes = 0;
};

} // namespace Utils

#endif // H264_ENCODER_H
