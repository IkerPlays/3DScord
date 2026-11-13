#ifndef VIDEO_RECEIVER_H
#define VIDEO_RECEIVER_H

#include <3ds.h>
#include <citro2d.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Utils {

// The video textures are 512x256 (RGB565) and hold the whole picture, stretched.
constexpr int VIDEO_TEX_W = 512;
constexpr int VIDEO_TEX_H = 256;

// Turns a decoded YUV 4:2:0 picture into RGB565, stretched to fill the whole texture, written straight
// in the tiled order the GPU reads (8x8 tiles, Z order inside), so no display transfer is needed. `dst` has VIDEO_TEX_W x VIDEO_TEX_H
// pixels. Gives the size of the original picture (its proportions). False when it is too small.
bool convertToTiledTexture(const uint8_t *y, const uint8_t *u, const uint8_t *v, int width, int height, int strideY, int strideUV,
                           uint16_t *dst, int &outW, int &outH);

// The same for a picture that is already RGB565 (the camera of the console): stretched to fill the 512x256 texture, tiled.
void convertRgb565ToTiledTexture(const uint16_t *src, int width, int height, uint16_t *dst);

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
	// Media thread: the orientation the sender says the picture needs (RTP video orientation): bits 1-0 are quarter turns clockwise, bit 2 a sideways flip.
	void setOrientation(int quarterTurns);
	// Media thread: true while decoding waits for a key frame (so one should be asked for).
	bool wantsKeyFrame();
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
	int orientation = 0; // quarter turns from the sender
	uint32_t shownVersion = 0;
	u64 lastUpdate = 0;
};

// Draws a picture that is in the top left of a 512x512 texture (the video textures) inside the box, keeping its proportions and
// applying the rotation and flip of Settings > Voice.
void drawVideoTexture(C3D_Tex *texture, int picW, int picH, float x, float y, float z, float w, float h, int extraTurns = 0);
// The same with the orientation given: quarter turns clockwise (0-3) and a sideways mirror, on top of how the texture was written. The
// orientation test uses it to find out how the pictures come out on the console.
void drawVideoTextureOriented(C3D_Tex *texture, int picW, int picH, float x, float y, float z, float w, float h, int turns,
                              bool mirror);
// What every picture needs, found with the orientation test (Settings > Updates).
constexpr int VIDEO_DEFAULT_TURNS = 0;
constexpr bool VIDEO_DEFAULT_MIRROR = true;

} // namespace Utils

#endif // VIDEO_RECEIVER_H
