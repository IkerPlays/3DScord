#ifndef VIDEO_RECEIVER_H
#define VIDEO_RECEIVER_H

#include <3ds.h>
#include <citro2d.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Utils {

// EXPERIMENTAL. Shows the camera or the shared screen of one person in the call. The H.264 frames (already joined and
// decrypted) are queued by the media thread; a worker thread, on the extra core of a New 3DS, decodes them in software and
// turns the last picture into a small RGB565 image that the screen thread uploads to a texture. When the worker falls
// behind, the queue is dropped and decoding waits for the next key frame.
class VideoReceiver {
  public:
	static VideoReceiver &getInstance();

	// Media thread: one whole frame in Annex B. Only the first person who sends video is followed.
	void submit(const std::string &userId, const uint8_t *data, size_t size);
	// Media thread: a frame was lost or could not be decrypted, so the next ones cannot be decoded until a key frame comes.
	void lost();
	// Leaves the call: stops the worker and frees everything.
	void stop();

	// Screen thread: uploads the newest picture to the texture. Call it once per frame before drawing.
	void update();
	bool hasPicture() const { return textureReady && gotPicture; }
	// True when the picture is recent enough to show (it disappears a few seconds after the stream stops).
	bool isLive() const;
	std::string userId() const;
	// Draws the picture inside the box, keeping its proportions.
	void draw(float x, float y, float z, float w, float h) const;

  private:
	VideoReceiver() = default;
	void run();
	struct Impl;
	Impl &ensure();
	void convert(const uint8_t *y, const uint8_t *u, const uint8_t *v, int width, int height, int strideY, int strideUV);

	Impl *impl = nullptr;
	C3D_Tex texture;
	bool textureReady = false;
	bool gotPicture = false;
	int picW = 0;
	int picH = 0;
	uint32_t shownVersion = 0;
	u64 lastUpdate = 0;
};

} // namespace Utils

#endif // VIDEO_RECEIVER_H
