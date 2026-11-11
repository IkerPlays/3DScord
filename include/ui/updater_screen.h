#ifndef UPDATER_SCREEN_H
#define UPDATER_SCREEN_H

#include "screen_manager.h"

namespace UI {

// Shown at startup: the Discord logo spins while the app looks for an update, installs it and restarts.
class UpdaterScreen : public Screen {
  public:
	UpdaterScreen();
	~UpdaterScreen() override;

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }
	bool isAnimating() const override { return true; }

	// Set by the settings screen so that "already up to date" is reported instead of silently continuing.
	static bool manualCheck;

  private:
	float angle;
	bool installStarted;
	bool finished;
	int graceFrames;
};

} // namespace UI

#endif // UPDATER_SCREEN_H
