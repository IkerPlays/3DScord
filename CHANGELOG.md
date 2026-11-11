# Changelog

## Unreleased
- **Upload files** from the SD card: Channel Tools > Upload File lists `sdmc:/3ds/3DScord/upload` (up to 4 MB).
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
