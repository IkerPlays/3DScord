#include "utils/video_receiver.h"
#include "core/config.h"
#include "log.h"
#include "utils/redraw.h"
#include "utils/soft_decoder.h"
#include "utils/worker_thread.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Utils {

namespace {
constexpr size_t FRAME_MAX = 256 * 1024;
constexpr int SLOTS = 6;
constexpr int TEX_W = 512;
constexpr int TEX_H = 512;

// A key frame has the parameter sets (NAL 7) or an IDR slice (NAL 5) before its first ordinary slice. An access unit can
// start with other NAL units (delimiter, SEI), so the start codes are searched until the first slice.
bool isKeyFrame(const uint8_t *d, size_t len) {
	for (size_t i = 0; i + 3 < len; i++) {
		if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
			const int type = d[i + 3] & 0x1F;
			if (type == 7 || type == 5) {
				return true;
			}
			if (type == 1) {
				return false;
			}
			i += 2;
		}
	}
	return false;
}

inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
} // namespace

struct VideoReceiver::Impl {
	LightLock lock;
	LightEvent wake;
	WorkerThread thread;
	bool running = false;
	bool resetPending = false;
	bool quit = false;
	std::string owner;
	uint8_t *slots[SLOTS] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
	size_t lens[SLOTS] = {0, 0, 0, 0, 0, 0};
	uint8_t refIdc[SLOTS] = {3, 3, 3, 3, 3, 3}; // nal_ref_idc of the first slice: 0 means nothing refers to the frame
	int head = 0;
	int count = 0;
	bool needKey = true;   // nothing decoded yet: wait for a key frame
	bool wantKey = false;  // something was lost: a key frame would repair the picture
	// The picture the worker produced, waiting for the screen thread.
	u16 *front = nullptr; // being written by the worker
	u16 *ready = nullptr; // finished, linear memory
	int readyW = 0;
	int readyH = 0;
	uint32_t version = 0;
	u64 lastPicture = 0;
	uint32_t decoded = 0;
	uint32_t dropped = 0;
	u64 lastFrameAt = 0;
	uint32_t lostFrames = 0;
	uint32_t errors = 0;
	double msTotal = 0;
};

VideoReceiver &VideoReceiver::getInstance() {
	static VideoReceiver instance;
	return instance;
}

void VideoReceiver::submit(const std::string &userId, const uint8_t *data, size_t size) {
	if (size == 0 || size > FRAME_MAX) {
		return;
	}
	Impl &s = ensure();
	LightLock_Lock(&s.lock);
	if (s.owner.empty()) {
		s.owner = userId;
		Logger::log("[VideoRx] Following the video of %s", userId.c_str());
	}
	const u64 now = osGetTime();
	if (s.owner != userId) {
		// The person followed stopped sending: follow this one instead.
		if (now - s.lastFrameAt < 4000) {
			LightLock_Unlock(&s.lock);
			return;
		}
		Logger::log("[VideoRx] %s stopped; following %s", s.owner.c_str(), userId.c_str());
		s.owner = userId;
		s.count = 0;
		s.needKey = true;
	}
	s.lastFrameAt = now;
	if (s.count == SLOTS) {
		// The decoder cannot keep up. Drop the oldest frame that nothing refers to; if every one is a reference, drop the oldest
		// and let the decoder hide the gap while a key frame is asked for. (Emptying the queue and waiting for the next key frame
		// froze the picture for as long as Discord takes to send one, about 25 seconds.)
		int victim = 0;
		for (int i = 0; i < s.count; i++) {
			if (s.refIdc[(s.head + i) % SLOTS] == 0) {
				victim = i;
				break;
			}
			if (i == s.count - 1) {
				s.wantKey = true;
			}
		}
		for (int i = victim; i + 1 < s.count; i++) {
			const int a = (s.head + i) % SLOTS;
			const int b = (s.head + i + 1) % SLOTS;
			std::swap(s.slots[a], s.slots[b]);
			std::swap(s.lens[a], s.lens[b]);
			std::swap(s.refIdc[a], s.refIdc[b]);
		}
		s.count--;
		s.dropped++;
	}
	const int slot = (s.head + s.count) % SLOTS;
	if (!s.slots[slot]) {
		s.slots[slot] = (uint8_t *)malloc(FRAME_MAX);
	}
	if (s.slots[slot]) {
		memcpy(s.slots[slot], data, size);
		s.lens[slot] = size;
		const int type = size > 4 ? (data[4] & 0x1F) : 0;
		s.refIdc[slot] = (type == 1 || type == 5) ? (uint8_t)((data[4] >> 5) & 3) : 3;
		s.count++;
	}
	const bool start = !s.running;
	if (start) {
		s.running = true;
		s.quit = false;
	}
	LightLock_Unlock(&s.lock);
	if (start) {
		s.thread.start([this] { run(); }, 3, 96 * 1024, true);
	}
	LightEvent_Signal(&s.wake);
}

// The state lives for the whole run of the app, so the screen thread never reads something the media thread is freeing.
VideoReceiver::Impl &VideoReceiver::ensure() {
	if (!impl) {
		impl = new Impl();
		LightLock_Init(&impl->lock);
		LightEvent_Init(&impl->wake, RESET_ONESHOT);
		memset(&texture, 0, sizeof(texture));
	}
	return *impl;
}

void VideoReceiver::lost() {
	if (!impl) {
		return;
	}
	LightLock_Lock(&impl->lock);
	// A frame was lost: keep decoding (the decoder hides what is missing, a stripe may show) and ask for a new key frame.
	impl->wantKey = true;
	impl->lostFrames++;
	LightLock_Unlock(&impl->lock);
}

bool VideoReceiver::wantsKeyFrame() {
	if (!impl) {
		return false;
	}
	LightLock_Lock(&impl->lock);
	const bool want = (impl->needKey || impl->wantKey) && !impl->owner.empty();
	LightLock_Unlock(&impl->lock);
	return want;
}

void VideoReceiver::run() {
	Impl &s = *impl;
	SoftDecoder decoder;
	if (!decoder.init()) {
		Logger::log("[VideoRx] The decoder could not start");
		return;
	}
	uint8_t *frame = (uint8_t *)malloc(FRAME_MAX);
	if (!frame) {
		return;
	}
	while (true) {
		LightEvent_Wait(&s.wake);
		while (true) {
			LightLock_Lock(&s.lock);
			if (s.quit) {
				LightLock_Unlock(&s.lock);
				free(frame);
				return;
			}
			if (s.count == 0) {
				LightLock_Unlock(&s.lock);
				break;
			}
			const size_t len = s.lens[s.head];
			memcpy(frame, s.slots[s.head], len);
			s.head = (s.head + 1) % SLOTS;
			s.count--;
			const bool needKey = s.needKey;
			LightLock_Unlock(&s.lock);

			// A key frame carries the parameter sets (NAL 7) or is an IDR (NAL 5).
			const bool key = isKeyFrame(frame, len);
			if (key) {
				LightLock_Lock(&s.lock);
				s.wantKey = false;
				LightLock_Unlock(&s.lock);
			}
			if (needKey) {
				if (!key) {
					continue;
				}
				LightLock_Lock(&s.lock);
				s.needKey = false;
				LightLock_Unlock(&s.lock);
			}

			const u64 t0 = svcGetSystemTick();
			SoftDecoder::Picture pic;
			bool got = false;
			const int state = decoder.decode(frame, len, pic, got);
			s.msTotal += (double)(svcGetSystemTick() - t0) / (double)SYSCLOCK_ARM11 * 1000.0;
			if (state != 0) {
				// The decoder reports an error or a missing reference: what it drew is wrong, wait for a fresh key frame.
				s.errors++;
				if (s.errors <= 5 || s.errors % 50 == 0) {
					Logger::log("[VideoRx] Decoder state %d (count %u, frames lost %u)", state, s.errors, s.lostFrames);
				}
				// Missing references: show what the decoder made anyway and ask for a key frame.
				LightLock_Lock(&s.lock);
				s.wantKey = true;
				LightLock_Unlock(&s.lock);
				if (!got) {
					continue;
				}
			}
			if (got) {
				s.decoded++;
				if (s.decoded <= 3 || s.decoded % 150 == 0) {
					Logger::log("[VideoRx] %u pictures (%dx%d), %.1f ms each, %u queues dropped, %u frames lost, %u errors",
					            s.decoded, pic.width, pic.height, s.msTotal / s.decoded, s.dropped, s.lostFrames, s.errors);
				}
				// Only the newest picture is worth drawing: while frames are waiting, keep decoding and skip the drawing.
				LightLock_Lock(&s.lock);
				const bool idle = s.count == 0;
				LightLock_Unlock(&s.lock);
				if (idle) {
					convert(pic.y, pic.u, pic.v, pic.width, pic.height, pic.strideY, pic.strideUV);
				}
			}
		}
	}
}

// Reduces the picture (half size, or a quarter when it is very big) and turns YUV into RGB565.
void VideoReceiver::convert(const uint8_t *py0, const uint8_t *pu0, const uint8_t *pv0, int width, int height, int strideY,
                            int strideUV) {
	Impl &s = *impl;
	const int div = width > 1024 ? 4 : 2;
	const int w = (width / div) & ~7;
	const int h = (height / div) & ~7;
	if (w <= 0 || h <= 0 || w > TEX_W || h > TEX_H) {
		return;
	}
	if (!s.front || !s.ready) {
		// Rows are as wide as the texture: the display transfer lays the 8x8 tiles out by the width of its input. The two buffers
		// are made once and reused when the picture size changes (Discord lowers the resolution during a call).
		LightLock_Lock(&s.lock);
		s.front = (u16 *)linearAlloc((size_t)TEX_W * TEX_H * 2);
		s.ready = (u16 *)linearAlloc((size_t)TEX_W * TEX_H * 2);
		if (s.front && s.ready) {
			memset(s.front, 0, (size_t)TEX_W * TEX_H * 2);
			memset(s.ready, 0, (size_t)TEX_W * TEX_H * 2);
			GSPGPU_FlushDataCache(s.front, (u32)TEX_W * TEX_H * 2);
			GSPGPU_FlushDataCache(s.ready, (u32)TEX_W * TEX_H * 2);
		}
		LightLock_Unlock(&s.lock);
		if (!s.front || !s.ready) {
			Logger::log("[VideoRx] No linear memory for the picture buffers (%u KB free)", (unsigned)(linearSpaceFree() / 1024));
			if (s.front) {
				linearFree(s.front);
			}
			if (s.ready) {
				linearFree(s.ready);
			}
			s.front = s.ready = nullptr;
			return;
		}
		Logger::log("[VideoRx] Picture buffers ready (%u KB of linear memory left)", (unsigned)(linearSpaceFree() / 1024));
	}
	for (int y = 0; y < h; y++) {
		const uint8_t *py = py0 + (size_t)(y * div) * strideY;
		const uint8_t *pu = pu0 + (size_t)(y * div / 2) * strideUV;
		const uint8_t *pv = pv0 + (size_t)(y * div / 2) * strideUV;
		u16 *dst = s.front + (size_t)y * TEX_W;
		for (int x = 0; x < w; x++) {
			const int c = ((py[x * div] + py[x * div + 1] + py[x * div + strideY] + py[x * div + 1 + strideY]) >> 2) - 16; // 2x2 average, no stripes
			const int d = pu[x * div / 2] - 128;
			const int e = pv[x * div / 2] - 128;
			const int r = clamp255((298 * c + 409 * e + 128) >> 8);
			const int g = clamp255((298 * c - 100 * d - 208 * e + 128) >> 8);
			const int b = clamp255((298 * c + 516 * d + 128) >> 8);
			dst[x] = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
		}
	}
	LightLock_Lock(&s.lock);
	std::swap(s.front, s.ready);
	s.readyW = w;
	s.readyH = h;
	s.version++;
	s.lastPicture = osGetTime();
	LightLock_Unlock(&s.lock);
	requestRedraw();
}

void VideoReceiver::update() {
	if (!impl) {
		return;
	}
	Impl &s = *impl;
	LightLock_Lock(&s.lock);
	if (s.resetPending) {
		// The call ended: the texture is released here, on the screen thread that uses it.
		s.resetPending = false;
		if (textureReady) {
			C3D_TexDelete(&texture);
		}
		textureReady = false;
		gotPicture = false;
		shownVersion = 0;
	}
	if (s.version == shownVersion || !s.ready) {
		LightLock_Unlock(&s.lock);
		return;
	}
	if (!textureReady) {
		textureReady = C3D_TexInit(&texture, TEX_W, TEX_H, GPU_RGB565);
		if (textureReady) {
			C3D_TexSetFilter(&texture, GPU_LINEAR, GPU_LINEAR);
		}
	}
	if (textureReady) {
		picW = s.readyW;
		picH = s.readyH;
		GSPGPU_FlushDataCache(s.ready, (u32)TEX_W * TEX_H * 2);
		C3D_SyncDisplayTransfer((u32 *)s.ready, GX_BUFFER_DIM(TEX_W, TEX_H), (u32 *)texture.data, GX_BUFFER_DIM(TEX_W, TEX_H),
		                        (GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
		                         GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
		                         GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
		                         GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO)));
		gotPicture = true;
		lastUpdate = s.lastPicture;
	}
	shownVersion = s.version;
	LightLock_Unlock(&s.lock);
}

bool VideoReceiver::isLive() const { return hasPicture() && osGetTime() - lastUpdate < 4000; }

std::string VideoReceiver::userId() const {
	if (!impl) {
		return "";
	}
	LightLock_Lock(&impl->lock);
	std::string id = impl->owner;
	LightLock_Unlock(&impl->lock);
	return id;
}

void drawVideoTexture(C3D_Tex *texture, int picW, int picH, float x, float y, float z, float w, float h) {
	if (!texture || picW <= 0 || picH <= 0) {
		return;
	}
	const int turns = Config::getInstance().getVideoRotation();
	const bool flip = Config::getInstance().isVideoFlipped();
	const float shownW = (turns & 1) ? (float)picH : (float)picW;
	const float shownH = (turns & 1) ? (float)picW : (float)picH;
	const float scale = std::min(w / shownW, h / shownH);
	const float top = flip ? 1.0f - (float)picH / TEX_H : 1.0f;
	const float bottom = flip ? 1.0f : 1.0f - (float)picH / TEX_H;
	Tex3DS_SubTexture sub = {(u16)picW, (u16)picH, 0.0f, top, (float)picW / TEX_W, bottom};
	C2D_Image img = {texture, &sub};
	const float cx = x + w / 2.0f;
	const float cy = y + h / 2.0f;
	if (turns == 0) {
		C2D_DrawImageAt(img, cx - picW * scale / 2.0f, cy - picH * scale / 2.0f, z, nullptr, scale, scale);
	} else {
		// citro2d turns around the centre of the picture, and the position given is that centre.
		C2D_DrawImageAtRotated(img, cx, cy, z, C3D_AngleFromDegrees((float)(turns * 90)), nullptr, scale, scale);
	}
}

void VideoReceiver::draw(float x, float y, float z, float w, float h) const {
	if (!hasPicture()) {
		return;
	}
	drawVideoTexture(const_cast<C3D_Tex *>(&texture), picW, picH, x, y, z, w, h);
}

void VideoReceiver::stop() {
	if (!impl) {
		return;
	}
	Impl &s = *impl;
	LightLock_Lock(&s.lock);
	s.quit = true;
	LightLock_Unlock(&s.lock);
	LightEvent_Signal(&s.wake);
	s.thread.join();
	LightLock_Lock(&s.lock);
	for (auto &slot : s.slots) {
		free(slot);
		slot = nullptr;
	}
	if (s.front) {
		linearFree(s.front);
	}
	if (s.ready) {
		linearFree(s.ready);
	}
	s.front = s.ready = nullptr;
	s.readyW = s.readyH = 0;
	s.head = s.count = 0;
	s.needKey = true;
	s.owner.clear();
	s.version = 0;
	s.running = false;
	s.quit = false;
	s.resetPending = true;
	LightLock_Unlock(&s.lock);
}

} // namespace Utils
