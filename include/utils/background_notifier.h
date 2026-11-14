#ifndef BACKGROUND_NOTIFIER_H
#define BACKGROUND_NOTIFIER_H

#include <string>

namespace Utils {

// Notifications while the app is not in front (the console is in the Home menu or asleep): an entry in the Notifications list of the Home
// menu and a pulse of the notification LED. They need the services of the system that Luma3DS opens to applications; without them it does
// nothing. When the app is suspended the system may stop its threads, and then there is nothing to notify with: this only works while the
// app keeps running in the background.
class BackgroundNotifier {
  public:
	static BackgroundNotifier &getInstance();

	void init();
	void shutdown();
	// True when the app is in the Home menu (or the console sleeps) and has not been restored yet.
	bool inBackground() const;
	void notify(const std::string &title, const std::string &body);
	// Turns the LED off again (the app came back to the front).
	void clearLed();

  private:
	BackgroundNotifier() = default;
};

} // namespace Utils

#endif // BACKGROUND_NOTIFIER_H
