#ifndef UPDATER_H
#define UPDATER_H

#include "utils/worker_thread.h"
#include <atomic>
#include <mutex>
#include <string>

namespace Utils {

// Self-updater: asks the GitHub releases of UPDATE_REPO, downloads the new .cia (installed with AM) or
// .3dsx (replaces the running file) and asks the app to restart. Every network step runs on a worker thread.
class Updater {
  public:
	struct Version {
		int major = 0;
		int minor = 0;
		int micro = 0;
		int dev = 0; // 0 = stable build, otherwise the devbuild number
	};

	enum class State {
		IDLE,
		CHECKING,
		UP_TO_DATE,
		AVAILABLE,
		DOWNLOADING,
		INSTALLING,
		RESTART,     // installed, relaunch the app
		EXIT_MANUAL, // installed, but the app has to be started again by hand (3dsx)
		FAILED
	};

	struct Release {
		std::string tag;
		Version version;
		std::string assetUrl;
		long long assetSize = 0;
		std::string notes;
	};

	static Updater &getInstance();

	static Version currentVersion();
	static std::string versionString(const Version &v);
	static std::string currentVersionString() { return versionString(currentVersion()); }
	static bool parseTag(const std::string &tag, Version &out);
	static bool isNewer(const Version &candidate, const Version &base);

	// Path the app was started from (argv[0]); only meaningful when launched from the Homebrew Launcher.
	void setLaunchPath(const char *path);
	static bool isCiaInstall();

	void startCheck(bool includeDevBuilds);
	void startInstall();
	void cancel() { cancelRequested = true; }
	void join();

	State getState() const { return state.load(); }
	float getProgress() const;
	std::string getStatus();
	std::string getError();
	Release getRelease();

  private:
	Updater() = default;

	void runCheck(bool includeDevBuilds);
	void runInstall();
	bool download(const Release &rel, const std::string &dest);
	bool installCia(const std::string &file);
	bool installHomebrew(const std::string &file);
	void fail(const std::string &message);
	void setStatus(const std::string &message);

	WorkerThread worker;
	std::atomic<State> state{State::IDLE};
	std::atomic<long long> downloaded{0};
	std::atomic<long long> total{0};
	std::atomic<bool> cancelRequested{false};
	std::mutex mutex;
	std::string status;
	std::string error;
	Release release;
	std::string launchPath;
};

} // namespace Utils

#endif // UPDATER_H
