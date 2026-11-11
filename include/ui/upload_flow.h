#ifndef UPLOAD_FLOW_H
#define UPLOAD_FLOW_H

#include <string>

namespace UI {

// Choose a file on the SD card, look at it, add an optional message and send it to a channel.
void startFileUpload(const std::string &channelId);

} // namespace UI

#endif // UPLOAD_FLOW_H
