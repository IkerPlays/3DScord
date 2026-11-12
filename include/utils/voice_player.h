#ifndef VOICE_PLAYER_H
#define VOICE_PLAYER_H

#include "utils/worker_thread.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <string>

namespace Utils {

// Plays the voice messages (Ogg Opus files) that other people send. The messages play one after another: playing
// one replaces the queue, queueing one adds it to the end. The speed changes the pitch too, as on a tape.
class VoicePlayer {
  public:
	static VoicePlayer &getInstance();

	void play(const std::string &url, float speed);
	void enqueue(const std::string &url, float speed);
	void stop();
	bool isBusy() const { return busy; }
	// A short reason when the last message could not be played, empty otherwise.
	std::string takeError();
	void shutdown();

  private:
	VoicePlayer() = default;
	struct Item {
		std::string url;
		float speed;
	};
	void ensureWorker();
	void run();
	bool playOne(const Item &item);
	void setError(const std::string &text);

	std::mutex mutex;
	std::deque<Item> queue;
	WorkerThread worker;
	std::atomic<bool> busy{false};
	std::atomic<bool> stopRequested{false};
	std::atomic<bool> quit{false};
	std::atomic<int> generation{0}; // changes on every stop, so an item that was loading when it happened is dropped
	std::string error;
};

} // namespace Utils

#endif // VOICE_PLAYER_H
