# Changelog

## Unreleased
- **Picture shrinking, improved:** it tries smaller sizes and lower quality until the picture fits in 4 MB, draws transparent
  parts over white, checks memory first and tells you the real reason when it cannot (no memory, not a picture, still too big).
  Pictures taken in portrait may come out sideways.
- **Forum filter:** the list goes back to the top when the filter changes, and the Tag hint only shows in forums that have tags.
- **Big pictures are shrunk:** a PNG/JPEG/BMP over 4 MB is reduced to 1600 px and sent as a JPEG instead of being refused.
- **Forum filter:** Y in a forum cycles through its tags and shows only the posts with that tag.

## 0.10.0
Typing indicator in DMs, follow announcement channels, pinned servers, forum tags and several bug fixes.

- **Forum tags:** each forum post shows its tags before the title.
- **Pinned servers:** Y on a server in the list pins it to the top (Y again unpins).
- **Fixes:** cancelling a bot command's keyboard no longer sends the command; importing a theme can no longer delete one
  you already have; the camera no longer frees a buffer a failed transfer may still use; screenshots check free memory.
- **Follow This Channel** (Channel Tools, in an announcement channel): pick one of your channels where you can create webhooks
  and new posts are copied there.
- **Typing indicator in the DM list:** a conversation shows "typing..." while the other person writes.

## 0.9.0
GIF picker fixes, connections and activity in profiles, share and import themes, bot slash commands and photos from the 3DS camera.

- **Send a Photo** (Channel Tools): takes a picture with the outer camera and sends it to the chat.
- **Bot Commands** (Channel Tools): lists the slash commands of the bots in the channel (Y searches) and runs them,
  asking for the text or numbers they need. With the buttons of the bot messages you can already play bot games.
  Commands that need a user, channel, role, file or subcommand are not supported.
- **Share My Theme / Import a Theme** (Channel Tools): sends the theme in use to the chat as a `.json` file, and imports
  one from the SD card (for example a file saved from a chat) and turns it on.
- **What they listen to or play:** a profile shows the Spotify song or the game when Discord sends it (it only does so for
  friends and for people it has recently seen online).
- **GIF picker:** trending GIFs now load (Discord answers them in a different shape than searches), video-only results
  are skipped and an unexpected answer is written to the log.
- **Connections** (Spotify, Steam, GitHub...) now show in a user's profile.

## 0.8.0
Screenshots to the chat, a compatibility mode for New 3DS, a status that expires and an About Me per server.

- **Status that expires** (Settings > Discord Status): a custom status that clears itself after the minutes you choose.
- **About Me per server** (Channel Tools > Server Settings): a different bio only for that server.
- **Send Screenshot** (Channel Tools): sends a picture of both screens to the chat (also saved in `screenshots/` on the
  SD card). Live screen sharing is not possible on the 3DS.
- **Compatibility Mode** (Settings > Controls and Battery): makes a New 3DS behave like an Old 3DS (smaller queues and
  lighter images) if the app crashes or runs out of memory.

## 0.7.0
Server settings, events, audit log and moderation, safer updates, a fixed GIF preview and quieter voice messages.

- **Voice messages are quieter:** they start at 40% volume (the 3DS microphone is very loud). After recording, Left/Right
  on the D-pad change the volume from 5% to 300% before sending; the message is encoded with that volume when you press A.
- **GIF preview fixed:** the picker shows a small still of the selected GIF at once and swaps in the animated one as soon
  as it has loaded (before, nothing appeared if the big file failed).
- **Join a Reaction:** message menu > Join a Reaction adds the most popular reaction on that message that you have not used.
- **Server Settings** (Channel Tools): nickname, mute, notification level, @everyone and role mentions, phone push,
  server sound, and the server name and description if you can manage the server.
- **Profile theme colors** (Settings > Discord Profile): the two colors of the gradient behind your profile.
- Polls: A on a poll message picks an answer with up/down and votes (A again removes the vote).
- **Events** (Channel Tools > Events): the scheduled events of the server with how many are interested; A marks you as
  interested, X removes it.
- **Audit Log** (Channel Tools > Audit Log): the latest 30 actions of the moderators (needs the View Audit Log permission).
- **My Permissions** (Channel Tools): what you can and cannot do in the channel, bit by bit.
- **Moderation:** in a member's profile (inside a server) press L to time them out (10 minutes, 1 hour, 1 day, 1 week, or
  remove it), kick or ban. Only the actions you are allowed to do are shown, and each needs a second press.
- **Join a Server** (side menu > Quick Access) with an invite link or code, and **Leave This Server** (Channel Tools).
  Discord can refuse joins that ask for a captcha; the app then shows its message.
- **Safer updates:** every download is checked against the SHA-256 that GitHub publishes for it, and a damaged file is
  never installed. **Install Previous Version** (Settings > Updates) goes back to the version before this one.
- **Diagnostics** (Settings > Updates): memory, Wi-Fi signal, battery, free space on the SD card and the latency to
  Discord, and a report (`diagnostics.txt` with the end of the logs) to send when something fails.

## 0.6.0
Themes, voice messages, PIN lock and a Spanish translation.

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
