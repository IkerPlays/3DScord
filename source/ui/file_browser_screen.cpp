#include "ui/file_browser_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "ui/screen_manager.h"
#include "utils/utf8_utils.h"
#include <3ds.h>
#include <algorithm>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

namespace UI {

std::string FileBrowserScreen::pendingTitle;
FileBrowserScreen::Filter FileBrowserScreen::pendingFilter = FileBrowserScreen::Filter::ANY;
FileBrowserScreen::PickCallback FileBrowserScreen::pendingCallback;
std::string FileBrowserScreen::lastDirectory = "sdmc:/";

namespace {

constexpr float ROW_HEIGHT = 26.0f;
constexpr float LIST_TOP = 8.0f;
constexpr int VISIBLE_ROWS = 7;
constexpr size_t MAX_ENTRIES = 600;
const char *ROOT = "sdmc:/";

std::string fitLine(const std::string &text, float maxWidth, float scale) {
	if (measureText(text, scale, scale) <= maxWidth) {
		return text;
	}
	size_t cursor = 0;
	size_t lastGood = 0;
	while (cursor < text.size()) {
		Utils::Utf8::decodeNext(text, cursor);
		if (measureText(text.substr(0, cursor) + "...", scale, scale) > maxWidth) {
			break;
		}
		lastGood = cursor;
	}
	return text.substr(0, lastGood) + "...";
}

// Same as fitLine but keeps the end, which is what tells paths apart.
std::string fitPathEnd(const std::string &text, float maxWidth, float scale) {
	if (measureText(text, scale, scale) <= maxWidth) {
		return text;
	}
	for (size_t start = 1; start < text.size(); start++) {
		if ((static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) {
			continue;
		}
		const std::string cut = "..." + text.substr(start);
		if (measureText(cut, scale, scale) <= maxWidth) {
			return cut;
		}
	}
	return text;
}

std::string sizeText(long long bytes) {
	if (bytes >= 1024LL * 1024) {
		const long long tenths = bytes * 10 / (1024 * 1024);
		return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MB";
	}
	return std::to_string((bytes + 1023) / 1024) + " KB";
}

bool hasImageExtension(const std::string &name) {
	const size_t dot = name.rfind('.');
	if (dot == std::string::npos) {
		return false;
	}
	const std::string ext = name.substr(dot + 1);
	return strcasecmp(ext.c_str(), "png") == 0 || strcasecmp(ext.c_str(), "jpg") == 0 ||
	       strcasecmp(ext.c_str(), "jpeg") == 0 || strcasecmp(ext.c_str(), "gif") == 0;
}

} // namespace

void FileBrowserScreen::open(const std::string &title, Filter filter, PickCallback onPick, bool replaceCurrent) {
	pendingTitle = title;
	pendingFilter = filter;
	pendingCallback = std::move(onPick);
	if (replaceCurrent) {
		ScreenManager::getInstance().setScreen(ScreenType::FILE_BROWSER);
	} else {
		ScreenManager::getInstance().pushScreen(ScreenType::FILE_BROWSER);
	}
}

FileBrowserScreen::FileBrowserScreen()
    : title(pendingTitle), filter(pendingFilter), onPick(std::move(pendingCallback)), directory(lastDirectory),
      selected(0), scroll(0.0f), repeatTimer(0) {
	pendingCallback = nullptr;
}

void FileBrowserScreen::onEnter() {
	// A folder that was deleted or an ejected card falls back to the root.
	struct stat st;
	if (directory.empty() || stat(directory.c_str(), &st) != 0) {
		directory = ROOT;
	}
	readDirectory();
}

std::string FileBrowserScreen::joined(const std::string &name) const {
	return directory.back() == '/' ? directory + name : directory + "/" + name;
}

void FileBrowserScreen::readDirectory() {
	entries.clear();
	selected = 0;
	scroll = 0.0f;

	if (DIR *dir = opendir(directory.c_str())) {
		while (dirent *e = readdir(dir)) {
			const std::string name = e->d_name;
			if (name.empty() || name[0] == '.') {
				continue;
			}
			struct stat st;
			if (stat(joined(name).c_str(), &st) != 0) {
				continue;
			}
			const bool isDir = S_ISDIR(st.st_mode);
			if (!isDir && filter == Filter::IMAGES && !hasImageExtension(name)) {
				continue;
			}
			entries.push_back({name, isDir, (long long)st.st_size});
			if (entries.size() >= MAX_ENTRIES) {
				break;
			}
		}
		closedir(dir);
	}

	std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
		if (a.isDir != b.isDir) {
			return a.isDir;
		}
		return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
	});
	Logger::log("[FileBrowser] %s: %zu entries", directory.c_str(), entries.size());
}

void FileBrowserScreen::goUp() {
	if (directory == ROOT) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	std::string trimmed = directory;
	while (trimmed.size() > 1 && trimmed.back() == '/') {
		trimmed.pop_back();
	}
	const size_t slash = trimmed.rfind('/');
	std::string parent = slash == std::string::npos ? ROOT : trimmed.substr(0, slash);
	if (parent.empty() || parent == "sdmc:") {
		parent = ROOT;
	}
	const std::string previous = trimmed.substr(slash == std::string::npos ? 0 : slash + 1);
	directory = parent;
	lastDirectory = directory;
	readDirectory();
	// Land on the folder we came from.
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i].name == previous) {
			selected = (int)i;
			break;
		}
	}
	clampSelection();
}

void FileBrowserScreen::activate(const Entry &entry) {
	if (entry.isDir) {
		directory = joined(entry.name);
		lastDirectory = directory;
		readDirectory();
		return;
	}
	lastDirectory = directory;
	const std::string path = joined(entry.name);
	PickCallback callback = std::move(onPick);
	onPick = nullptr;
	if (callback) {
		callback(path);
	}
	ScreenManager::getInstance().returnToPreviousScreen();
}

void FileBrowserScreen::clampSelection() {
	const int n = (int)entries.size();
	selected = n == 0 ? 0 : std::clamp(selected, 0, n - 1);
	if (selected < (int)scroll) {
		scroll = (float)selected;
	}
	if (selected >= (int)scroll + VISIBLE_ROWS) {
		scroll = (float)(selected - VISIBLE_ROWS + 1);
	}
}

void FileBrowserScreen::update() {
	const u32 kDown = hidKeysDown();
	const u32 kHeld = hidKeysHeld();

	if (kDown & KEY_B) {
		goUp();
		return;
	}
	if (kDown & KEY_Y) {
		directory = ROOT;
		lastDirectory = directory;
		readDirectory();
		return;
	}

	int move = 0;
	if (kDown & KEY_DOWN) {
		move = 1;
		repeatTimer = 20;
	} else if (kDown & KEY_UP) {
		move = -1;
		repeatTimer = 20;
	} else if ((kHeld & KEY_DOWN) && --repeatTimer <= 0) {
		move = 1;
		repeatTimer = 4;
	} else if ((kHeld & KEY_UP) && --repeatTimer <= 0) {
		move = -1;
		repeatTimer = 4;
	}
	if (kDown & KEY_R) {
		move = VISIBLE_ROWS;
	}
	if (kDown & KEY_L) {
		move = -VISIBLE_ROWS;
	}
	if (move != 0) {
		selected += move;
		clampSelection();
	}

	if (kDown & KEY_TOUCH) {
		touchPosition touch;
		hidTouchRead(&touch);
		if (touch.py >= LIST_TOP) {
			const int row = (int)scroll + (int)((touch.py - LIST_TOP) / ROW_HEIGHT);
			if (row >= 0 && row < (int)entries.size()) {
				if (row == selected) {
					const Entry entry = entries[row];
					activate(entry);
					return;
				}
				selected = row;
			}
		}
	}

	if ((kDown & KEY_A) && !entries.empty() && selected < (int)entries.size()) {
		const Entry entry = entries[selected];
		activate(entry);
		return;
	}
	clampSelection();
}

void FileBrowserScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackground());
	C2D_SceneBegin(target);

	drawText(10.0f, 6.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorText(), fitLine(title, 380.0f, 0.55f));
	C2D_DrawRectSolid(10.0f, 28.0f, 0.5f, 380.0f, 1.0f, ScreenManager::colorSeparator());
	drawText(10.0f, 36.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorAccent(), fitPathEnd(directory, 380.0f, 0.42f));

	if (entries.empty() || selected >= (int)entries.size()) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(),
		                 TR(filter == Filter::IMAGES ? "browser.no_images" : "browser.empty"), TOP_SCREEN_WIDTH);
		return;
	}

	const Entry &e = entries[selected];
	drawText(10.0f, 70.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), fitLine(e.name, 380.0f, 0.5f));
	drawText(10.0f, 92.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(),
	         e.isDir ? TR("browser.folder") : TR("browser.file") + "  " + sizeText(e.size));
	drawText(10.0f, 190.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), TR("browser.hint_top"));
}

void FileBrowserScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);

	for (int i = 0; i < VISIBLE_ROWS + 1; i++) {
		const int index = (int)scroll + i;
		if (index >= (int)entries.size()) {
			break;
		}
		const Entry &e = entries[index];
		const float y = LIST_TOP + i * ROW_HEIGHT;
		if (y + ROW_HEIGHT > 214.0f) {
			break;
		}
		if (index == selected) {
			C2D_DrawRectSolid(4.0f, y, 0.4f, 312.0f, ROW_HEIGHT - 2.0f, ScreenManager::colorSelection());
		}
		const std::string label = (e.isDir ? "[" + e.name + "]" : e.name);
		const std::string size = e.isDir ? "" : sizeText(e.size);
		const float sizeW = size.empty() ? 0.0f : measureText(size, 0.38f, 0.38f) + 10.0f;
		drawText(10.0f, y + 5.0f, 0.5f, 0.45f, 0.45f, e.isDir ? ScreenManager::colorAccent() : ScreenManager::colorText(),
		         fitLine(label, 296.0f - sizeW, 0.45f));
		if (!size.empty()) {
			drawText(310.0f - sizeW + 10.0f, y + 7.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), size);
		}
	}

	drawCenteredText(218.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), TR("browser.hint"),
	                 BOTTOM_SCREEN_WIDTH);
}

} // namespace UI
