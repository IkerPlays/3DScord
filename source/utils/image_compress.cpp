#include "utils/image_compress.h"
#include "log.h"
#include "utils/system_utils.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "stb_image.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace Utils {
namespace Image {

namespace {
void appendBytes(void *context, void *data, int size) {
	static_cast<std::string *>(context)->append(static_cast<const char *>(data), (size_t)size);
}

std::string lowerExtension(const std::string &path) {
	const size_t dot = path.rfind('.');
	std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
	for (char &c : ext) {
		c = (char)tolower((unsigned char)c);
	}
	return ext;
}
} // namespace

bool isCompressible(const std::string &path) {
	const std::string ext = lowerExtension(path);
	return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "tga";
}

std::string compressToJpeg(const std::string &path, int maxSide, int quality) {
	int w = 0, h = 0, comp = 0;
	if (!stbi_info(path.c_str(), &w, &h, &comp) || w <= 0 || h <= 0) {
		return "";
	}
	// The decoded picture (three bytes a pixel) and the shrunk copy have to fit in memory together.
	const size_t decoded = (size_t)w * h * 3;
	if (decoded + decoded / 4 + 1024 * 1024 > System::heapFreeBytes()) {
		Logger::log("[Compress] %dx%d does not fit in memory", w, h);
		return "";
	}
	unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &comp, 3);
	if (!pixels) {
		return "";
	}

	const int longest = std::max(w, h);
	int outW = w;
	int outH = h;
	if (longest > maxSide) {
		outW = std::max(1, (int)((long long)w * maxSide / longest));
		outH = std::max(1, (int)((long long)h * maxSide / longest));
	}

	std::string jpeg;
	if (outW == w && outH == h) {
		stbi_write_jpg_to_func(appendBytes, &jpeg, w, h, 3, pixels, quality);
	} else {
		std::vector<unsigned char> small((size_t)outW * outH * 3);
		// Each new pixel is the average of the block of old pixels it covers.
		for (int y = 0; y < outH; y++) {
			const int y0 = (int)((long long)y * h / outH);
			const int y1 = std::max(y0 + 1, (int)((long long)(y + 1) * h / outH));
			for (int x = 0; x < outW; x++) {
				const int x0 = (int)((long long)x * w / outW);
				const int x1 = std::max(x0 + 1, (int)((long long)(x + 1) * w / outW));
				unsigned sum[3] = {0, 0, 0};
				unsigned count = 0;
				for (int yy = y0; yy < y1 && yy < h; yy++) {
					for (int xx = x0; xx < x1 && xx < w; xx++) {
						const unsigned char *p = pixels + ((size_t)yy * w + xx) * 3;
						sum[0] += p[0];
						sum[1] += p[1];
						sum[2] += p[2];
						count++;
					}
				}
				unsigned char *d = &small[((size_t)y * outW + x) * 3];
				for (int k = 0; k < 3; k++) {
					d[k] = (unsigned char)(count ? sum[k] / count : 0);
				}
			}
		}
		stbi_image_free(pixels);
		pixels = nullptr;
		stbi_write_jpg_to_func(appendBytes, &jpeg, outW, outH, 3, small.data(), quality);
	}
	if (pixels) {
		stbi_image_free(pixels);
	}
	Logger::log("[Compress] %dx%d -> %dx%d, %zu bytes", w, h, outW, outH, jpeg.size());
	return jpeg;
}

} // namespace Image
} // namespace Utils
