#include "ui/text_prompt.h"
#include <3ds.h>
#include <vector>

namespace UI {

bool promptText(const std::string &hint, const std::string &initial, std::string &out, int maxLength, bool multiline) {
	SwkbdState swkbd;
	swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, maxLength);
	swkbdSetFeatures(&swkbd, SWKBD_DARKEN_TOP_SCREEN | (multiline ? SWKBD_MULTILINE : 0));
	swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 0, 0);
	swkbdSetHintText(&swkbd, hint.c_str());
	if (!initial.empty()) {
		swkbdSetInitialText(&swkbd, initial.c_str());
	}
	std::vector<char> buffer(maxLength * 4 + 8, 0);
	if (swkbdInputText(&swkbd, buffer.data(), buffer.size()) != SWKBD_BUTTON_CONFIRM) {
		return false;
	}
	out = buffer.data();
	return true;
}

} // namespace UI
