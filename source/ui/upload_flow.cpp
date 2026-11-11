#include "ui/upload_flow.h"
#include "core/i18n.h"
#include "discord/discord_client.h"
#include "ui/file_browser_screen.h"
#include "ui/screen_manager.h"
#include "ui/text_prompt.h"
#include "utils/transfers.h"

namespace UI {

void startFileUpload(const std::string &channelId) {
	if (!Discord::DiscordClient::getInstance().canSendMessage(channelId)) {
		ScreenManager::getInstance().showToast(TR("message.no_permission"));
		return;
	}
	FileBrowserScreen::open(TR("chat.btn.file"), FileBrowserScreen::Filter::ANY, [channelId](const std::string &path) {
		std::string caption;
		if (!promptText(TR("list.caption_hint"), "", caption, 500, false)) {
			return;
		}
		Utils::Transfers::getInstance().uploadFile(channelId, path, caption);
	});
}

} // namespace UI
