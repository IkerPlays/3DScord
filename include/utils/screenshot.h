#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <cstdint>
#include <string>
#include <vector>

namespace Utils {
namespace Screenshot {

// Takes a picture of both screens (top above, bottom centered below) a few frames from now, so the menu that asked
// for it can close first. The PNG goes to the screenshots folder on the SD card and, when channelId is not empty,
// is sent to that channel.
void request(const std::string &channelId);

// A PNG (stored, uncompressed) of w x h pixels, three bytes per pixel in RGB order.
std::string encodePng(const std::vector<uint8_t> &rgb, int w, int h);

// Takes a photo with the outer camera and saves it as a PNG in the screenshots folder. Blocks for about a second.
// Returns the file path, or an empty string when the camera is not available.
std::string takePhoto();

// Called by the screen manager before every frame.
void tick();

// True while a screenshot is waiting for its frames; the screen is redrawn meanwhile.
bool pending();

} // namespace Screenshot
} // namespace Utils

#endif // SCREENSHOT_H
