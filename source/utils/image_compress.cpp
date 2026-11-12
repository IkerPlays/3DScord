#include "utils/image_compress.h"
#include "core/config.h"
#include "log.h"
#include "utils/system_utils.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

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

// Each size and quality to try, from the best to the smallest.
struct Attempt {
	int side;
	int quality;
};
constexpr Attempt ATTEMPTS[] = {{2048, 90}, {1600, 80}, {1280, 70}, {960, 60}, {640, 50}};
// Settings > Chat > Picture Quality picks where the list is started: high, normal or small.
size_t firstAttempt() {
	const int q = Config::getInstance().getUploadQuality();
	return q == 2 ? 0 : q == 0 ? 3 : 1;
}
} // namespace

bool isCompressible(const std::string &path) {
	const std::string ext = lowerExtension(path);
	return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "tga";
}

CompressResult compressToJpeg(const std::string &path, long long maxBytes, std::string &jpeg) {
	int w = 0, h = 0, comp = 0;
	if (!stbi_info(path.c_str(), &w, &h, &comp) || w <= 0 || h <= 0) {
		return CompressResult::NOT_A_PICTURE;
	}
	// A picture with transparency is read with four channels so it can be drawn over white.
	const int channels = (comp == 4 || comp == 2) ? 4 : 3;
	const size_t decoded = (size_t)w * h * channels;
	// The decoded picture, the biggest shrunk copy and the JPEG being built have to fit together.
	const size_t first = firstAttempt();
	const int firstSide = std::min(std::max(w, h), ATTEMPTS[first].side);
	const size_t shrunk = (size_t)firstSide * firstSide * 3;
	if (decoded + shrunk + (size_t)maxBytes + 1024 * 1024 > System::heapFreeBytes()) {
		Logger::log("[Compress] %dx%d does not fit in memory", w, h);
		return CompressResult::NO_MEMORY;
	}
	unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &comp, channels);
	if (!pixels) {
		return CompressResult::NOT_A_PICTURE;
	}

	CompressResult result = CompressResult::STILL_TOO_BIG;
	for (size_t ai = first; ai < sizeof(ATTEMPTS) / sizeof(ATTEMPTS[0]); ai++) {
		const Attempt &attempt = ATTEMPTS[ai];
		const int longest = std::max(w, h);
		int outW = w;
		int outH = h;
		if (longest > attempt.side) {
			outW = std::max(1, (int)((long long)w * attempt.side / longest));
			outH = std::max(1, (int)((long long)h * attempt.side / longest));
		}
		unsigned char *small = (unsigned char *)malloc((size_t)outW * outH * 3);
		if (!small) {
			result = CompressResult::NO_MEMORY;
			break;
		}
		// The block of old columns and rows each new one covers, worked out once.
		int *xs = (int *)malloc(sizeof(int) * 2 * (size_t)outW);
		if (!xs) {
			free(small);
			result = CompressResult::NO_MEMORY;
			break;
		}
		for (int x = 0; x < outW; x++) {
			const int x0 = (int)((long long)x * w / outW);
			xs[x * 2] = x0;
			xs[x * 2 + 1] = std::min(w, std::max(x0 + 1, (int)((long long)(x + 1) * w / outW)));
		}
		for (int y = 0; y < outH; y++) {
			const int y0 = (int)((long long)y * h / outH);
			const int y1 = std::min(h, std::max(y0 + 1, (int)((long long)(y + 1) * h / outH)));
			for (int x = 0; x < outW; x++) {
				unsigned sum[3] = {0, 0, 0};
				unsigned count = 0;
				for (int yy = y0; yy < y1; yy++) {
					const unsigned char *row = pixels + (size_t)yy * w * channels;
					for (int xx = xs[x * 2]; xx < xs[x * 2 + 1]; xx++) {
						const unsigned char *p = row + (size_t)xx * channels;
						if (channels == 4) {
							const unsigned a = p[3];
							// Over white: the more transparent, the closer to 255.
							sum[0] += (p[0] * a + 255 * (255 - a)) / 255;
							sum[1] += (p[1] * a + 255 * (255 - a)) / 255;
							sum[2] += (p[2] * a + 255 * (255 - a)) / 255;
						} else {
							sum[0] += p[0];
							sum[1] += p[1];
							sum[2] += p[2];
						}
						count++;
					}
				}
				unsigned char *d = small + ((size_t)y * outW + x) * 3;
				for (int k = 0; k < 3; k++) {
					d[k] = (unsigned char)(count ? sum[k] / count : 255);
				}
			}
		}
		free(xs);
		jpeg.clear();
		stbi_write_jpg_to_func(appendBytes, &jpeg, outW, outH, 3, small, attempt.quality);
		free(small);
		Logger::log("[Compress] %dx%d -> %dx%d q%d: %zu bytes", w, h, outW, outH, attempt.quality, jpeg.size());
		if ((long long)jpeg.size() <= maxBytes) {
			result = CompressResult::OK;
			break;
		}
	}
	stbi_image_free(pixels);
	if (result != CompressResult::OK) {
		jpeg.clear();
	}
	return result;
}

} // namespace Image
} // namespace Utils
