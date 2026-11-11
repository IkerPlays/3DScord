#ifndef PIN_LOCK_SCREEN_H
#define PIN_LOCK_SCREEN_H

#include "screen_manager.h"
#include <string>

namespace UI {

// Asks for a number with the system keypad (hidden). Returns false when cancelled. Used to set and to check the PIN.
bool promptPin(const std::string &hint, std::string &out);

// Shown when the app starts and after the console wakes from sleep, if a PIN is set.
class PinLockScreen : public Screen {
  public:
	// true when the lock is the very first thing of the app start (unlocking then continues the normal start).
	static bool atStartup;

	PinLockScreen();

	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }

  private:
	void ask();

	bool asked;
	int wrongTries;
	u64 lockedUntil;
	std::string message;
};

} // namespace UI

#endif // PIN_LOCK_SCREEN_H
