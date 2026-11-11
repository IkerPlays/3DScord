# Changelog

## Next devbuild
- **Quick Access** in the side menu groups Find, Friends, New Group DM, Saved Messages and Mentions.
- **Friends:** list of friends, pending requests and blocked people (Y changes the list). A messages a friend or accepts
  a request, X removes (press twice), R adds a friend by username.
- **Group DMs:** pick two or more friends and start a group chat.
- **Profiles:** A sends a message, Y adds a friend, X blocks (press twice) and R edits your private note about them.
- **Pinned DMs:** press X on a direct message to keep it at the top of the list.
- **My Nickname Here** in Channel Tools changes your nickname in the current server.

## 0.3.0
Sending media, profiles and threads, and a more reliable updater.

- **Profiles:** open anyone's profile from the message menu (View Profile): banner, avatar, names, pronouns,
  About Me, account age and, in servers, join date and roles.
- **Members:** Channel Tools > Members searches the members of a server by name and opens their profile.
- **Find Server / Channel** in the side menu: type part of a name to jump to a server, channel or direct message.
- **Threads:** Channel Tools > Threads lists the active threads (Y: show archived), A opens one, X joins it.
  The message menu has **Create Thread**, and Channel Tools has **Archive This Thread**.
- The update check can no longer hold the app on a slow network: it gives up after a few seconds and starts normally.
- **Updater:** downloads are retried up to three times, errors show their real code, and a small `updater.log` is kept
  in `sdmc:/3ds/3DScord` so a failed update can be diagnosed.
- **Devbuild numbers** now follow the next stable version (`0.3.0-dev.1`, `0.3.0-dev.2`, ...) with no gaps, and the
  release notes are cleaner.
- **File browser:** Channel Tools > Upload File opens a browser of the SD card; pick any file (up to 4 MB) and send it.
  Avatar and banner in Settings use the same browser, so no special folder is needed.
- **GIFs:** Channel Tools > GIFs searches Tenor (or shows what is trending) and sends the one you pick.
- **Stickers:** Channel Tools > Stickers sends one of the stickers of the server.
- **Save to SD Card:** the message menu can save any attachment to `sdmc:/3ds/3DScord/downloads`.
- **View Image:** open an attached image full screen; D-Pad / Circle Pad / touch to move, L and R to zoom, A to fit.
- Old name TriCord removed from the texts of the app.
- Downloads and uploads run in the background and report with a message when they finish.

## 0.2.0
The app is now called **3DScord** and gets channel tools, saved messages and an updater that asks first.

- **Updater:** at startup, while the Discord logo spins, the app looks for a new version, shows the release notes and
  asks before installing (A: update, B: later). Settings > Updates can turn it off or include devbuilds.
- **Devbuilds** now have release notes, shown in the app before updating.
- **New name:** 3DScord. Your TriCord files are moved to `sdmc:/3ds/3DScord` automatically.
- **Channel Tools** (menu > Channel Tools, inside a channel): pinned messages, search, quick replies, drafts,
  formatting helpers (quote, code block, spoiler, bold, italic, underline, strikethrough), mark channel or server as
  read.
- **Saved messages:** save a message from its menu, read it later from menu > Saved Messages.
- **Mentions:** menu > Mentions lists your recent mentions.
- **Pin and unpin** messages (needs Manage Messages); **Jump to Original** on replies.
- **Ping on Reply** setting (Settings > Chat).
- Goes idle when you close the lid and comes back online when you open it.
- **Your Discord account** in Settings: display name, About Me, pronouns, profile color, avatar and banner, status and
  custom status, chat and privacy options.
- New README and ROADMAP.

## 0.1.0
- First 3DScord release, based on TriCord (2b-zipper, GPLv3).
- GitHub Actions: automatic `.cia` + `.3dsx` builds on every push and pull request.
- Manual workflows for devbuild pre-releases and stable releases.
- Dependabot keeps the GitHub Actions versions up to date.
