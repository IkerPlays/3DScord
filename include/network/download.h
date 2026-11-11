#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <atomic>
#include <string>

namespace Network {

struct DownloadControl {
	std::atomic<long long> *downloaded = nullptr;
	std::atomic<long long> *total = nullptr;
	std::atomic<bool> *cancel = nullptr;
};

// Streams a URL to a file on the SD card without holding it in memory. Meant for worker threads.
// expectedSize > 0 also checks the final size. On failure the partial file is removed and error says why.
bool downloadToFile(const std::string &url, const std::string &dest, const DownloadControl &control, std::string &error,
                    long long expectedSize = 0);

} // namespace Network

#endif // DOWNLOAD_H
