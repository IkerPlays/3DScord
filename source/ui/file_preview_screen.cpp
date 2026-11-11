#include "ui/file_preview_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "ui/image_manager.h"
#include "ui/screen_manager.h"
#include "utils/message_utils.h"
#include "utils/redraw.h"
#include "utils/utf8_utils.h"
#include <3ds.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>

namespace UI {

std::string FilePreviewScreen::pendingPath;
long long FilePreviewScreen::pendingMax = 0;
std::function<void()> FilePreviewScreen::pendingConfirm;
std::function<void()> FilePreviewScreen::pendingBack;

namespace {

constexpr long long MAX_PREVIEW_IMAGE_BYTES = 6LL * 1024 * 1024;
constexpr size_t MAX_PREVIEW_TEXT_BYTES = 6 * 1024;
constexpr int MAX_TEXT_LINES = 120;
constexpr int TEXT_ROWS = 13;

bool hasExtension(const std::string &name, std::initializer_list<const char *> list) {
	const size_t dot = name.rfind('.');
	if (dot == std::string::npos) {
		return false;
	}
	const std::string ext = name.substr(dot + 1);
	for (const char *e : list) {
		if (strcasecmp(ext.c_str(), e) == 0) {
			return true;
		}
	}
	return false;
}

std::string sizeText(long long bytes) {
	if (bytes >= 1024LL * 1024) {
		const long long tenths = bytes * 10 / (1024 * 1024);
		return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MB";
	}
	return std::to_string((bytes + 1023) / 1024) + " KB";
}

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

} // namespace

void FilePreviewScreen::open(const std::string &path, long long maxBytes, std::function<void()> onConfirm,
                             std::function<void()> onBack) {
	pendingPath = path;
	pendingMax = maxBytes;
	pendingConfirm = std::move(onConfirm);
	pendingBack = std::move(onBack);
	ScreenManager::getInstance().setScreen(ScreenType::FILE_PREVIEW);
}

FilePreviewScreen::FilePreviewScreen()
    : path(pendingPath), size(0), maxBytes(pendingMax), kind(Kind::OTHER), tooBig(false), imageLoaded(false),
      imageFailed(false), scroll(0.0f), onConfirm(std::move(pendingConfirm)), onBack(std::move(pendingBack)) {
	pendingConfirm = nullptr;
	pendingBack = nullptr;
	const size_t slash = path.rfind('/');
	name = slash == std::string::npos ? path : path.substr(slash + 1);
}

FilePreviewScreen::~FilePreviewScreen() {
	// A previewed picture will not be shown again; do not keep it in memory.
	ImageManager::getInstance().releaseLocal(path);
}

FilePreviewScreen::Kind FilePreviewScreen::classify(const std::string &name) {
	if (hasExtension(name, {"png", "jpg", "jpeg", "gif", "bmp"})) {
		return Kind::IMAGE;
	}
	if (hasExtension(name, {"mp4", "mkv", "avi", "mov", "webm", "m4v", "3gp", "flv", "wmv"})) {
		return Kind::VIDEO;
	}
	if (hasExtension(name, {"mp3", "ogg", "opus", "wav", "flac", "m4a", "aac", "wma", "mid"})) {
		return Kind::AUDIO;
	}
	if (hasExtension(name, {"txt", "md", "json", "log", "csv", "xml", "ini", "cfg", "conf", "yml", "yaml", "toml",
	                        "c", "cc", "cpp", "h", "hpp", "py", "js", "ts", "lua", "sh", "bat", "html", "css", "srt",
	                        "java", "rs", "go", "tex", "rtf"})) {
		return Kind::TEXT;
	}
	return Kind::OTHER;
}

void FilePreviewScreen::loadText() {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) {
		return;
	}
	std::string data(MAX_PREVIEW_TEXT_BYTES, '\0');
	const size_t got = fread(&data[0], 1, data.size(), f);
	fclose(f);
	data.resize(got);

	// A file that has NUL bytes is not text, whatever its name says.
	if (data.find('\0') != std::string::npos) {
		kind = Kind::OTHER;
		return;
	}
	data = Utils::Utf8::sanitizeText(data);

	size_t pos = 0;
	while (pos <= data.size() && (int)textLines.size() < MAX_TEXT_LINES) {
		size_t end = data.find('\n', pos);
		if (end == std::string::npos) {
			end = data.size();
		}
		std::string line = data.substr(pos, end - pos);
		while (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		std::replace(line.begin(), line.end(), '\t', ' ');
		if (line.empty()) {
			textLines.push_back("");
		} else {
			for (const auto &wrapped : MessageUtils::wrapText(line, 380.0f, 0.4f)) {
				textLines.push_back(wrapped);
			}
		}
		pos = end + 1;
	}
}

void FilePreviewScreen::onEnter() {
	struct stat st;
	if (stat(path.c_str(), &st) == 0) {
		size = st.st_size;
	}
	kind = classify(name);
	tooBig = maxBytes > 0 && size > maxBytes;

	if (kind == Kind::IMAGE) {
		if (size > MAX_PREVIEW_IMAGE_BYTES || !ImageManager::getInstance().getLocalImage(path)) {
			imageFailed = true;
		} else {
			imageLoaded = true;
		}
	} else if (kind == Kind::TEXT || kind == Kind::OTHER) {
		// Unknown extensions are tried as text too: plenty of text files have no extension or an odd one.
		loadText();
		if (kind == Kind::OTHER && !textLines.empty()) {
			kind = Kind::TEXT;
		}
	}
	Logger::log("[Preview] %s (%lld bytes, kind %d)", path.c_str(), size, (int)kind);
}

void FilePreviewScreen::update() {
	const u32 kDown = hidKeysDown();
	const u32 kHeld = hidKeysHeld();

	if (kDown & KEY_B) {
		auto back = onBack;
		if (back) {
			back();
		} else {
			ScreenManager::getInstance().returnToPreviousScreen();
		}
		return;
	}
	if ((kDown & KEY_A) && !tooBig && size > 0) {
		auto confirm = onConfirm;
		ScreenManager::getInstance().returnToPreviousScreen();
		if (confirm) {
			confirm();
		}
		return;
	}

	if (kind == Kind::TEXT) {
		const float maxScroll = std::max(0.0f, (float)textLines.size() - TEXT_ROWS);
		if (kHeld & KEY_DOWN) {
			scroll = std::min(maxScroll, scroll + 0.25f);
		}
		if (kHeld & KEY_UP) {
			scroll = std::max(0.0f, scroll - 0.25f);
		}
		if (kDown & (KEY_DOWN | KEY_UP)) {
			Utils::requestRedraw();
		}
		if (kHeld & (KEY_DOWN | KEY_UP)) {
			Utils::requestRedraw();
		}
	}
}

void FilePreviewScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(target);

	if (kind == Kind::IMAGE && imageLoaded) {
		const auto info = ImageManager::getInstance().getImageInfo(path);
		if (info.tex && info.originalW > 0 && info.originalH > 0) {
			const float uMax = (float)info.originalW / info.tex->width;
			const float vMax = (float)info.originalH / info.tex->height;
			Tex3DS_SubTexture subtex = {(u16)info.originalW, (u16)info.originalH, 0.0f, 1.0f, uMax, 1.0f - vMax};
			const float scale = std::min((float)TOP_SCREEN_WIDTH / info.originalW, (float)TOP_SCREEN_HEIGHT / info.originalH);
			const float w = info.originalW * scale;
			const float h = info.originalH * scale;
			C2D_DrawImageAt({info.tex, &subtex}, (TOP_SCREEN_WIDTH - w) / 2.0f, (TOP_SCREEN_HEIGHT - h) / 2.0f, 0.5f,
			                nullptr, scale, scale);
			return;
		}
	}

	const u32 light = C2D_Color32(225, 225, 225, 255);
	if (kind == Kind::TEXT) {
		drawText(10.0f, 4.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorAccent(), fitLine(name, 380.0f, 0.42f));
		const int first = (int)scroll;
		float y = 24.0f;
		for (int i = 0; i < TEXT_ROWS + 1 && first + i < (int)textLines.size(); i++) {
			drawText(10.0f, y, 0.5f, 0.4f, 0.4f, light, textLines[first + i]);
			y += 15.5f;
		}
		return;
	}

	// Videos, music and everything else cannot be drawn on the console: show what the file is.
	const char *typeKey = kind == Kind::VIDEO   ? "preview.video"
	                      : kind == Kind::AUDIO ? "preview.audio"
	                      : kind == Kind::IMAGE ? "preview.image_failed"
	                                            : "preview.file";
	drawCenteredText(80.0f, 0.5f, 0.6f, 0.6f, light, fitLine(name, 380.0f, 0.6f), TOP_SCREEN_WIDTH);
	drawCenteredText(112.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR(typeKey), TOP_SCREEN_WIDTH);
	drawCenteredText(134.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), sizeText(size), TOP_SCREEN_WIDTH);
	if (kind == Kind::VIDEO || kind == Kind::AUDIO) {
		drawCenteredText(170.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), TR("preview.no_player"),
		                 TOP_SCREEN_WIDTH);
	}
}

void FilePreviewScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);

	drawCenteredText(20.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), fitLine(name, 300.0f, 0.5f),
	                 BOTTOM_SCREEN_WIDTH);

	std::string details = sizeText(size);
	if (kind == Kind::IMAGE && imageLoaded) {
		const auto info = ImageManager::getInstance().getImageInfo(path);
		if (info.originalW > 0) {
			details += "   " + std::to_string(info.originalW) + " x " + std::to_string(info.originalH);
		}
	}
	drawCenteredText(48.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), details, BOTTOM_SCREEN_WIDTH);

	if (tooBig) {
		drawCenteredText(86.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorError(),
		                 Core::I18n::format(TR("preview.too_big"), sizeText(maxBytes)), BOTTOM_SCREEN_WIDTH);
	} else if (size <= 0) {
		drawCenteredText(86.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorError(), TR("preview.empty"),
		                 BOTTOM_SCREEN_WIDTH);
	} else {
		drawCenteredText(120.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorSuccess(), TR("preview.confirm"),
		                 BOTTOM_SCREEN_WIDTH);
	}
	drawCenteredText(150.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), TR("preview.back"),
	                 BOTTOM_SCREEN_WIDTH);
	if (kind == Kind::TEXT) {
		drawCenteredText(200.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), TR("preview.scroll"),
		                 BOTTOM_SCREEN_WIDTH);
	}
}

} // namespace UI
