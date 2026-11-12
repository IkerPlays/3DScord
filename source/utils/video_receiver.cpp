#include "utils/video_receiver.h"
#include "log.h"
#include "utils/redraw.h"
#include "utils/soft_decoder.h"
#include "utils/worker_thread.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Utils {

namespace {
constexpr size_t FRAME_MAX = 160 * 1024;
constexpr int SLOTS = 3;
constexpr int TEX_W = 512;
constexpr int TEX_H = 512;

inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
} // namespace

struct VideoReceiver::Impl {
	LightLock lock;
	LightEvent wake;
	WorkerThread thread;
	bool running = false;
	bool quit = false;
	std::string owner;
	uint8_t *slots[SLOTS] = {nullptr, nullptr, nullptr};
	size_t lens[SLOTS] = {0, 0, 0};
	int head = 0;
	int count = 0;
	bool needKey = true;
	// The picture the worker produced, waiting for the screen thread.
	u16 *front = nullptr; // being written by the worker
	u16 *ready = nullptr; // finished, linear memory
	int readyW = 0;
	int readyH = 0;
	uint32_t version = 0;
	u64 lastPicture = 0;
	uint32_t decoded = 0;
	uint32_t dropped = 0;
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
	if (!impl) {
		impl = new Impl();
		LightLock_Init(&impl->lock);
		LightEvent_Init(&impl->wake, RESET_ONESHOT);
		memset(&texture, 0, sizeof(texture));
	}
	Impl &s = *impl;
	LightLock_Lock(&s.lock);
	if (s.owner.empty()) {
		s.owner = userId;
		Logger::log("[VideoRx] Following the video of %s", userId.c_str());
	}
	if (s.owner != userId) {
		LightLock_Unlock(&s.lock);
		return;
	}
	if (s.count == SLOTS) {
		// The decoder cannot keep up: start again at the next key frame.
		s.count = 0;
		s.needKey = true;
		s.dropped++;
	}
	const int slot = (s.head + s.count) % SLOTS;
	if (!s.slots[slot]) {
		s.slots[slot] = (uint8_t *)malloc(FRAME_MAX);
	}
	if (s.slots[slot]) {
		memcpy(s.slots[slot], data, size);
		s.lens[slot] = size;
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
			const int type = len > 4 ? (frame[4] & 0x1F) : 0;
			const bool key = type == 7 || type == 5;
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
			if (state != 0 && !got) {
				LightLock_Lock(&s.lock);
				s.needKey = true; // the decoder lost track: wait for a fresh key frame
				LightLock_Unlock(&s.lock);
				continue;
			}
			if (got) {
				s.decoded++;
				if (s.decoded <= 3 || s.decoded % 150 == 0) {
					Logger::log("[VideoRx] %u pictures (%dx%d), %.1f ms each, %u queues dropped", s.decoded, pic.width,
					            pic.height, s.msTotal / s.decoded, s.dropped);
				}
				convert(pic.y, pic.u, pic.v, pic.width, pic.height, pic.strideY, pic.strideUV);
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
	if (!s.front || s.readyW != w || s.readyH != h) {
		LightLock_Lock(&s.lock);
		if (s.front) {
			linearFree(s.front);
		}
		if (s.ready) {
			linearFree(s.ready);
		}
		s.front = (u16 *)linearAlloc((size_t)w * h * 2);
		s.ready = (u16 *)linearAlloc((size_t)w * h * 2);
		s.readyW = w;
		s.readyH = h;
		LightLock_Unlock(&s.lock);
		if (!s.front || !s.ready) {
			return;
		}
	}
	for (int y = 0; y < h; y++) {
		const uint8_t *py = py0 + (size_t)(y * div) * strideY;
		const uint8_t *pu = pu0 + (size_t)(y * div / 2) * strideUV;
		const uint8_t *pv = pv0 + (size_t)(y * div / 2) * strideUV;
		u16 *dst = s.front + (size_t)y * w;
		for (int x = 0; x < w; x++) {
			const int c = py[x * div] - 16;
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
		GSPGPU_FlushDataCache(s.ready, (u32)picW * picH * 2);
		C3D_SyncDisplayTransfer((u32 *)s.ready, GX_BUFFER_DIM(picW, picH), (u32 *)texture.data, GX_BUFFER_DIM(TEX_W, TEX_H),
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

void VideoReceiver::draw(float x, float y, float z, float w, float h) const {
	if (!hasPicture() || picW <= 0 || picH <= 0) {
		return;
	}
	const float scale = std::min(w / picW, h / picH);
	Tex3DS_SubTexture sub = {(u16)picW, (u16)picH, 0.0f, 1.0f, (float)picW / TEX_W, 1.0f - (float)picH / TEX_H};
	C2D_Image img = {const_cast<C3D_Tex *>(&texture), &sub};
	C2D_DrawImageAt(img, x + (w - picW * scale) / 2.0f, y + (h - picH * scale) / 2.0f, z, nullptr, scale, scale);
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
	if (textureReady) {
		C3D_TexDelete(&texture);
	}
	for (auto *slot : s.slots) {
		free(slot);
	}
	if (s.front) {
		linearFree(s.front);
	}
	if (s.ready) {
		linearFree(s.ready);
	}
	delete impl;
	impl = nullptr;
	textureReady = false;
	gotPicture = false;
	shownVersion = 0;
}

} // namespace Utils
