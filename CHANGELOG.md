# Changelog

## Unreleased
- **Voice messages:** the new **Rec** button in the chat records with the microphone (A starts and stops, X records again,
  B cancels) and sends the message as a real Discord voice message, up to two minutes. You cannot record while you are in
  a voice call, because the call is using the microphone.
- **GIF previews:** in the GIF picker the top screen plays the GIF of the row you stop on, with its name.
- **PIN Lock** (Settings > Security and Backup): asks for a PIN when the app starts and when the console wakes from
  sleep. Five wrong tries make you wait 30 seconds. The PIN is stored scrambled (salted SHA-256), never as typed.
- **Active Sessions** (same place): see where your account is logged in and log other devices out (press A twice).
- **Export / Import Settings** (same place): save your settings to `sdmc:/3ds/3DScord/settings_backup.json` and restore
  them on another console. Accounts and the PIN are not included.
- **Spanish:** every text of the app is now translated into Spanish.

## Pending 0.6.0
These changes already shipped in a devbuild and will be part of the 0.6.0 release notes.

- **More themes:** AMOLED (real black), Nintendo (white and red), Retro (green terminal) and Midnight (deep blue), next to
  Dark and Light (Settings > Appearance > Theme).
- **Theme Editor** (Settings > Appearance): change any color of the app by typing its hex code, see it on the real screens
  at once, reset it (X) or save it as your own theme (Y). Saved themes appear in the Theme Manager and are normal theme
  files you can share.
- **Left-Handed Mode** (Settings > Controls and Battery): A, B, X and Y swap places with the D-Pad (A is Right, B is Down,
  X is Up, Y is Left), so you can press the action buttons with your left hand.
- **Battery Saver** (same place): turns off the faster New 3DS mode and slows animations down.

## 0.5.0
Voice features, buttons in the chat and file previews.

- **Buttons right in the chat:** under the channel name there is now a row with File, GIF, Sticker, Pins, Search, Voice and
  More. Nothing else to open: tap and go. (They are no longer inside Channel Tools; More opens the rest of the tools.)
- **Preview before sending:** after choosing a file you see it first. Pictures are drawn, text files show their first
  lines, and videos, music and other files show their name, type and size. A sends it, B chooses another file. The same
  preview is used for avatars and banners.
- **Voice** button: shows who is in each voice channel of the server (or the people in your call) and joins one.
- **Gallery** (Channel Tools > Gallery): the pictures and files sent in the channel; A views a picture or saves a file,
  X saves to the SD card.
- **Image Cache** (Settings > Appearance): Small, Normal or Large amount of pictures kept in memory.
- **Voice Channels** (side menu > Quick Access): see who is in each voice channel of the server and join or leave with A.
- **People in the Call** (Quick Access): change how loud each person sounds with A (+10%), X (-10%) and Y (100%); your own
  row changes your microphone volume. The list shows who is speaking.
- **Shortcuts in a call, on every screen:** ZL or L+X mutes, ZR or L+Y deafens.
- **Push to Talk** (Settings > Voice): hold R (or ZR on a New 3DS) to talk.
- **Noise Gate** (Settings > Voice): how loud you have to be to be heard, from 0 (sensitive) to 10 (strict).
- **Keep Calls With Lid Closed** (Settings > Voice): the call keeps going when you close the console.
- **Call Ringtone** (Settings > Voice): choose the sound that plays when someone calls you.

## 0.4.0
Friends, group chats, notification sounds and quiet hours.

- **Sound per server or channel:** Channel Tools > Sound of This Channel / Sound of This Server. Pick a sound and hear it;
  the channel's own sound wins over the server's.
- **Quiet Hours** (Settings > Chat): no message sounds between two hours of the day, even across midnight.
- **While you were away:** when you log in, a message tells you how many mentions you have and in how many conversations.
- **Unread DMs First** (Settings > Chat): unread direct messages go right after the pinned ones.
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
