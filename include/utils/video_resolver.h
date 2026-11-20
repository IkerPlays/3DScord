#ifndef VIDEO_RESOLVER_H
#define VIDEO_RESOLVER_H

#include <string>

namespace Utils {
namespace VideoResolver {

// Asks public Piped / Invidious servers for a small MP4 (H.264 with sound, up to 360p) of a YouTube video and returns its address.
// The video id is the only thing that leaves the console, and only when you ask to play. The servers are listed in the code and
// can be replaced or added to with sdmc:/3ds/3DScord/video_servers.txt (one per line: "piped https://..." or "invidious https://...").
// Blocking: call it from the screen thread only when the user asked to play. False when no server gave a file (error says why).
bool resolveYoutube(const std::string &videoId, std::string &fileUrl, std::string &error);

} // namespace VideoResolver
} // namespace Utils

#endif // VIDEO_RESOLVER_H
