#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <string>

namespace Utils {
namespace Screenshot {

// Takes a picture of both screens (top above, bottom centered below) a few frames from now, so the menu that asked
// for it can close first. The PNG goes to the screenshots folder on the SD card and, when channelId is not empty,
// is sent to that channel.
void request(const std::string &channelId);

// Called by the screen manager before every frame.
void tick();

// True while a screenshot is waiting for its frames; the screen is redrawn meanwhile.
bool pending();

} // namespace Screenshot
} // namespace Utils

#endif // SCREENSHOT_H
