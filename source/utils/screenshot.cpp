#include "utils/screenshot.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "ui/screen_manager.h"
#include "utils/redraw.h"
#include "utils/transfers.h"
#include <3ds.h>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#include <vector>

namespace Utils {
namespace Screenshot {

namespace {

std::string targetChannel;
int framesLeft = 0;

constexpr int TOP_W = 400;
constexpr int BOTTOM_W = 320;
constexpr int SCREEN_H = 240;

uint32_t crcTable[256];
bool crcReady = false;

uint32_t crc32(uint32_t crc, const uint8_t *data, size_t len) {
	if (!crcReady) {
		for (uint32_t n = 0; n < 256; n++) {
			uint32_t c = n;
			for (int k = 0; k < 8; k++) {
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			}
			crcTable[n] = c;
		}
		crcReady = true;
	}
	crc = ~crc;
	for (size_t i = 0; i < len; i++) {
		crc = crcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	}
	return ~crc;
}

void put32(std::string &s, uint32_t v) {
	for (int shift = 24; shift >= 0; shift -= 8) {
		s.push_back((char)((v >> shift) & 0xFF));
	}
}

void chunk(std::string &out, const char *type, const std::string &data) {
	put32(out, (uint32_t)data.size());
	std::string body(type, 4);
	body += data;
	out += body;
	put32(out, crc32(0, (const uint8_t *)body.data(), body.size()));
}

// PNG with "stored" deflate blocks: no compression, but any viewer opens it and it needs no library.
std::string encodePng(const std::vector<uint8_t> &rgb, int w, int h) {
	std::string raw;
	raw.reserve((size_t)(w * 3 + 1) * h);
	for (int y = 0; y < h; y++) {
		raw.push_back(0);
		raw.append((const char *)&rgb[(size_t)y * w * 3], (size_t)w * 3);
	}

	std::string z;
	z.push_back(0x78);
	z.push_back(0x01);
	size_t pos = 0;
	do {
		const size_t n = std::min<size_t>(65535, raw.size() - pos);
		const bool last = pos + n >= raw.size();
		z.push_back(last ? 1 : 0);
		z.push_back((char)(n & 0xFF));
		z.push_back((char)(n >> 8));
		z.push_back((char)(~n & 0xFF));
		z.push_back((char)((~n >> 8) & 0xFF));
		z.append(raw, pos, n);
		pos += n;
	} while (pos < raw.size());
	uint32_t a = 1, b = 0;
	for (unsigned char c : raw) {
		a = (a + c) % 65521;
		b = (b + a) % 65521;
	}
	put32(z, (b << 16) | a);

	std::string out("\x89PNG\r\n\x1a\n", 8);
	std::string ihdr;
	put32(ihdr, (uint32_t)w);
	put32(ihdr, (uint32_t)h);
	ihdr.push_back(8); // bit depth
	ihdr.push_back(2); // RGB
	ihdr.push_back(0);
	ihdr.push_back(0);
	ihdr.push_back(0);
	chunk(out, "IHDR", ihdr);
	chunk(out, "IDAT", z);
	chunk(out, "IEND", "");
	return out;
}

// The framebuffers are stored rotated: column by column, bottom to top, three bytes (B, G, R) per pixel.
void copyScreen(std::vector<uint8_t> &rgb, int imageW, int xOffset, int yOffset, gfxScreen_t screen, int width) {
	u16 fbW = 0, fbH = 0;
	const u8 *fb = gfxGetFramebuffer(screen, GFX_LEFT, &fbW, &fbH);
	if (!fb) {
		return;
	}
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < SCREEN_H; y++) {
			const u8 *p = fb + ((size_t)x * fbW + (fbW - 1 - y)) * 3;
			uint8_t *dst = &rgb[((size_t)(yOffset + y) * imageW + xOffset + x) * 3];
			dst[0] = p[2];
			dst[1] = p[1];
			dst[2] = p[0];
		}
	}
}

void capture() {
	auto &sm = UI::ScreenManager::getInstance();
	const int w = TOP_W;
	const int h = SCREEN_H * 2;
	std::vector<uint8_t> rgb;
	rgb.assign((size_t)w * h * 3, 0);
	copyScreen(rgb, w, 0, 0, GFX_TOP, TOP_W);
	copyScreen(rgb, w, (TOP_W - BOTTOM_W) / 2, SCREEN_H, GFX_BOTTOM, BOTTOM_W);
	const std::string png = encodePng(rgb, w, h);

	const std::string dir = std::string(CONFIG_DIR_PATH) + "/screenshots";
	mkdir(dir.c_str(), 0777);
	char name[64];
	const time_t now = time(nullptr);
	strftime(name, sizeof(name), "/shot_%Y%m%d_%H%M%S.png", gmtime(&now));
	const std::string path = dir + name;
	FILE *f = fopen(path.c_str(), "wb");
	if (!f || fwrite(png.data(), 1, png.size(), f) != png.size()) {
		if (f) {
			fclose(f);
		}
		sm.showToast(TR("screenshot.failed"));
		return;
	}
	fclose(f);
	Logger::log("[Screenshot] %s (%zu bytes)", path.c_str(), png.size());

	if (targetChannel.empty()) {
		sm.showToast(TR("screenshot.saved"));
	} else {
		Transfers::getInstance().uploadFile(targetChannel, path, "");
	}
}

} // namespace

void request(const std::string &channelId) {
	targetChannel = channelId;
	// The screen that asked closes, and two frames must be drawn and swapped before the buffer holds the chat.
	framesLeft = 4;
	requestRedraw();
}

bool pending() { return framesLeft > 0; }

void tick() {
	if (framesLeft <= 0) {
		return;
	}
	if (--framesLeft == 0) {
		capture();
	} else {
		requestRedraw();
	}
}

} // namespace Screenshot
} // namespace Utils
