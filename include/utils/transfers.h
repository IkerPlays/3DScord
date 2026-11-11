#ifndef TRANSFERS_H
#define TRANSFERS_H

#include "utils/worker_thread.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace Utils {

// Background file transfers that must not freeze the interface: saving attachments to the SD card, uploading files
// and sending stickers. One worker runs them in order and leaves a one-line result that the main loop shows as a toast.
class Transfers {
  public:
	static Transfers &getInstance();

	// Where saved attachments go.
	static std::string downloadDir();

	void saveAttachment(const std::string &url, const std::string &filename, long long size);
	void uploadFile(const std::string &channelId, const std::string &path, const std::string &caption);
	void sendSticker(const std::string &channelId, const std::string &stickerId);
	// A voice message: an Ogg Opus file, its length in seconds and the waveform (bytes, base64) that Discord draws.
	void sendVoiceMessage(const std::string &channelId, std::string oggData, double seconds, std::string waveform);

	// Empty when there is nothing new to report.
	std::string takeMessage();
	void shutdown();

  private:
	Transfers() = default;
	void enqueue(std::function<void()> job);
	void run();
	void report(const std::string &message);
	std::string uniquePath(const std::string &dir, const std::string &filename);

	WorkerThread worker;
	std::mutex mutex;
	std::condition_variable cv;
	std::deque<std::function<void()>> jobs;
	bool started = false;
	bool stopping = false;
	std::string message;
	std::atomic<bool> cancel{false};
};

} // namespace Utils

#endif // TRANSFERS_H
