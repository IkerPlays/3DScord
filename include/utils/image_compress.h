#ifndef IMAGE_COMPRESS_H
#define IMAGE_COMPRESS_H

#include <string>

namespace Utils {
namespace Image {

enum class CompressResult { OK, NOT_A_PICTURE, NO_MEMORY, STILL_TOO_BIG };

// Reads a picture from the SD card (PNG, JPEG, BMP, TGA) and returns it as a JPEG of at most maxBytes. It tries
// smaller sizes and lower qualities until it fits. Transparent pixels are drawn over white.
CompressResult compressToJpeg(const std::string &path, long long maxBytes, std::string &jpeg);

// True when the file name ends like a picture this can read.
bool isCompressible(const std::string &path);

} // namespace Image
} // namespace Utils

#endif // IMAGE_COMPRESS_H
