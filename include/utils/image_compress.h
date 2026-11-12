#ifndef IMAGE_COMPRESS_H
#define IMAGE_COMPRESS_H

#include <string>

namespace Utils {
namespace Image {

// Reads a picture from the SD card (PNG, JPEG, BMP, TGA), shrinks it so its longest side is at most maxSide and
// returns it as a JPEG. Returns an empty string when the file is not a picture or there is not enough memory.
std::string compressToJpeg(const std::string &path, int maxSide, int quality);

// True when the file name ends like a picture this can read.
bool isCompressible(const std::string &path);

} // namespace Image
} // namespace Utils

#endif // IMAGE_COMPRESS_H
