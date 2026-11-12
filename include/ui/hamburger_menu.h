#ifndef HAMBURGER_MENU_H
#define HAMBURGER_MENU_H

#include <citro2d.h>
#include <string>
#include <vector>

namespace UI {

enum class MenuItemType { SERVER_LIST, SETTINGS, HOME, ACCOUNT_SWITCH, ABOUT, CHANNEL_TOOLS, QUICK_ACCESS, UPDATES };

struct MenuItem {
	std::string label;
	MenuItemType type;
};

class HamburgerMenu {
  public:
	HamburgerMenu();
	~HamburgerMenu() = default;

	void update();
	void render();

	void toggle();
	void open();
	void close();
	void reset();
	bool isOpen() const { return state == State::OPEN || state == State::OPENING; }
	bool isClosed() const { return state == State::CLOSED; }
	bool isAnimating() const { return state == State::OPENING || state == State::CLOSING; }
	void refreshStrings();

  private:
	enum class State { CLOSED, OPENING, OPEN, CLOSING, ACCOUNT_SELECTION, STATUS_SELECTION, DELETE_CONFIRMATION };

	State state;
	float slideProgress;
	int selectedIndex;

	int accountSelectionIndex;
	int accountScrollOffset;
	int statusSelectionIndex = 0;

	std::vector<MenuItem> items;

	static const float MENU_WIDTH;
	static const float ANIMATION_SPEED;

	void drawMenuItem(int index, float y, float alpha);
	// 40px per entry, tighter once there are more than four so they stay above the account card.
	float itemStep() const { return items.size() > 6 ? 25.0f : items.size() > 4 ? 28.0f : 40.0f; }
	void drawAccountCard(float x, float y, float alpha);
	bool accountCardSelected = false;
	bool inMessages = false;
};

} // namespace UI

#endif // HAMBURGER_MENU_H
