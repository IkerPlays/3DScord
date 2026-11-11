#ifndef PROFILE_SCREEN_H
#define PROFILE_SCREEN_H

#include "screen_manager.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UI {

// A user's profile: banner, avatar, names, About Me, pronouns, account age and, in a server, join date and roles.
class ProfileScreen : public Screen {
  public:
	static void open(const std::string &userId, const std::string &guildId);

	ProfileScreen();

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return false; }
	bool isAnimating() const override { return false; }

  private:
	struct Profile {
		std::string id;
		std::string username;
		std::string globalName;
		std::string nickname;
		std::string avatar;
		std::string discriminator;
		std::string banner;
		std::string bio;
		std::string pronouns;
		std::string joinedAt;
		std::vector<std::string> roleIds;
		int accentColor = -1;
		bool bot = false;
	};

	struct Shared {
		std::mutex mutex;
		Profile profile;
		std::string error;
		bool loading = true;
		bool changed = false;
		std::string note;
		bool noteChanged = false;
	};

	void buildBodyLines();
	std::string displayName() const;

	static std::string pendingUser;
	static std::string pendingGuild;

	std::string userId;
	std::string guildId;
	std::shared_ptr<Shared> shared;
	Profile profile;
	std::string error;
	std::string note;
	bool confirmBlock = false;
	bool loading;
	std::vector<std::string> bodyLines;
	float scroll;
};

} // namespace UI

#endif // PROFILE_SCREEN_H
