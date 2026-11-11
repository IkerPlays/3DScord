# Roadmap

Work is split into **phases**. Every finished phase is published as a **devbuild** (with release notes). Every
**second** phase also ships a **stable** release, which bundles the notes of both.

| Phases | Stable |
| --- | --- |
| 1 + 2 | 0.2.0 (released) |
| 3 + 4 | 0.3.0 |
| 5 + 6 | 0.4.0 |
| 7 + 8 | 0.5.0 |
| 9 + 10 | 0.6.0 |
| 11 + 12 | 0.7.0 |

Status: `[x]` done, `[~]` partly done, `[ ]` planned. The order inside a phase can change.

## Phase 1 — Foundation
- [x] Rename to 3DScord (the old `sdmc:/3ds/TriCord` folder is moved automatically)
- [x] Ask before updating, with the release notes shown first
- [x] Devbuilds with release notes
- [x] Updater: stable or devbuild channel (toggle)
- [x] Pin / unpin messages from the message menu
- [x] Reply with or without ping (setting)
- [x] Jump to the original message of a reply
- [x] Idle status while the lid is closed
- [x] Edit and delete your own messages (already in TriCord)

## Phase 2 — Messages I
- [x] View the pinned messages of a channel
- [x] Search messages in a channel
- [x] Message drafts: save a message without sending it
- [x] Saved (favorite) messages, stored on the SD card
- [x] Quote and code-block / spoiler helpers in the composer
- [x] Quick replies (editable phrases)
- [x] Mark a channel or server as read
- [x] Recent mentions list

## Phase 3 — Sending media
- [x] File browser to upload any file from the SD card (up to 4 MB); also used for avatar and banner
- [x] Send stickers (from the current server)
- [x] GIF picker (Tenor, through Discord)
- [x] Save received images and files to the SD card
- [x] Image viewer with pan and zoom

## Phase 4 — Servers and people
- [x] Threads: create from a message, list, join, archive
- [x] Find a server or channel by name (side menu)
- [x] Search the members of a server and open their profile
- [x] Full user profile (banner, bio, pronouns, account age, join date, roles)
- [x] The update check can no longer slow down the start

## Phase 5 — Friends and DMs
- [x] Friends list with pending requests; send and accept requests
- [x] Block people (from their profile) and unblock them (friends list)
- [x] Create group DMs
- [x] Private notes on users
- [x] Your own nickname in each server (Channel Tools)
- [x] Pinned DMs on top of the list (X on a DM)
- [x] Search in DMs (Channel Tools > Search works in DMs; Find jumps to a DM by name)
- [x] Quick Access in the side menu (Find, Friends, New Group DM, Saved, Mentions)
- [x] Unread-first DM view (setting in Phase 6)
- [ ] Typing indicator in the DM list
- [ ] Carried over: upload photos from the 3DS camera
- [ ] Carried over: compress big images before uploading
- [ ] Carried over: voice messages recorded with the microphone
- [ ] Carried over: follow announcement channels
- [ ] Carried over: forum tags and filters
- [ ] Carried over: reorder and pin favorite servers; editable folders
- [ ] Carried over: full member list with presence (Discord only lets bots list every member)

## Phase 6 — Notifications
- [x] Different sound per server or channel
- [x] Scheduled quiet hours (no message sounds)
- [x] "While you were away" summary when you log in
- [x] Unread DMs first (setting)
- [x] Mute servers and channels for a chosen time (already in TriCord)
- [ ] Not possible on the 3DS: notifications while the app is closed or the console sleeps (apps do not run in the
      background), the notification LED (no system API for apps) and a counter on the HOME icon

## Phase 7 — Voice
- [x] Voice channel picker with the people connected (Quick Access > Voice Channels)
- [x] Mute / deafen with the physical buttons (ZL or L+X, ZR or L+Y)
- [x] Push to talk on a console button (R, or ZR on a New 3DS)
- [x] Adjustable noise gate
- [x] Easier per-user volume (Quick Access > People in the Call)
- [x] Speaking indicator in the call list (already in TriCord)
- [x] Custom call ringtone
- [x] Keep the call going with the lid closed (the console is kept awake)

## Phase 8 — Media
- [x] Buttons for files, GIFs, stickers, pins, search and voice right in the chat
- [x] Preview of a file before it is sent (pictures, text; name, type and size for the rest)
- [x] Attachment gallery for a channel
- [x] Configurable image cache size
- [x] YouTube link preview (shown as an embed, already in TriCord)
- [ ] Not possible for now: playing videos and music attachments, WebP pictures and animated avatars (the app has no
      decoder for them; the preview of such files shows their information instead)

## Phase 9 — Interface
- [x] More themes (AMOLED, Nintendo, Retro, Midnight) and an on-console theme editor
- [x] Left-handed mode (face buttons and D-Pad swap roles)
- [x] Battery saver
- [x] Scroll bar and "go to bottom" button (already in the app)
- [ ] Not planned: font size (the text layouts depend on fixed sizes), own keyboard with emoji (the system keyboard is
      used), 3D depth, a context panel and swipe gestures (they would need a redesign of the screens)

## Phase 10 — Languages and account
- [x] Spanish translation of every text of the app (the other languages fall back to English until they are translated)
- [x] PIN lock (at start and after the console wakes up)
- [x] See active sessions and log other devices out
- [x] Export and import settings
- [x] Voice messages (Rec button in the chat)
- [x] GIF previews in the GIF picker
- [x] Quick account switching (already in the side menu, on the account card)
- [ ] Translate the new texts into the other languages
- [ ] Not possible: on-demand message translation (it needs an outside translation service) and the time zone from the
      system (the 3DS does not tell the app its time zone; it is set by hand in Settings)

## Phase 11 — Server tools
- [x] Scheduled events
- [x] Join with an invite link or code; leave a server
- [x] See your permissions in a channel
- [x] Basic audit log
- [x] Moderation: kick, ban, timeout
- [x] Go back to the previous version (Settings > Updates > Install Previous Version)
- [x] Integrity (SHA-256) check of every downloaded update
- [x] Diagnostics screen (memory, network, battery, SD space, Discord latency) and a report to send

## Phase 12 — Servers, profile and chat polish
- [x] GIF picker preview fixed: a still shows at once, the animated GIF replaces it when it has loaded
- [x] Join a reaction already on a message (message menu > Join a Reaction)
- [x] Poll voting with A on a poll message (up/down to pick, A to vote or remove the vote)
- [x] Server Settings (Channel Tools): nickname, mute, notification level, @everyone / role mentions, phone push, sound,
  and the server name and description when you have Manage Server
- [x] Profile theme colors (two-color gradient) next to pronouns, accent color, avatar and banner

## Phase 13 — Sharing the screen (what the 3DS can do)
- [x] Send a screenshot of the top and bottom screens to a chat (Channel Tools > Send Screenshot)
- [ ] "Snapshot sharing": send a screenshot every few seconds to a channel, edited into the same message
- [x] Compatibility Mode: a New 3DS can behave like an Old 3DS (Settings)
- [x] More profile: per-server nickname and bio, status with an expiry
- [x] Connections list in profiles

## Phase 14 — Extras
- [x] Spotify: see what people are listening to (in their profile, when Discord sends it)
- [ ] Bot command mini-games with buttons
- [x] Share themes between consoles (as a .json file through any chat)

## Phase 15 — System integration (to be tried on a real console)
- [ ] HOME menu widget with the latest message

> Some items depend on what the 3DS system allows (background work, the HOME icon, the notification LED, playing audio
> with the lid closed). They will be tried on a real console and may change or be dropped.


## Not possible on the 3DS

- **Live screen sharing** (3DS to Discord or Discord to 3DS). Discord streams are encoded video (H.264/VP8) over WebRTC.
  The 3DS cannot encode video, and decoding a stream is only possible in hardware on the New 3DS with a decoder service
  the app cannot use for this. What is possible is sending screenshots (Phase 13).
- **Video calls.** Discord video needs a VP8 or H.264 encoder and a full WebRTC video stack with end-to-end encryption.
  The 3DS has no hardware video encoder (only a decoder, on the New 3DS), and encoding video with ffmpeg in software
  cannot keep up on its processor, even at a tiny size. Voice calls and voice messages are fully supported.
- Notifications while the app is closed or the console sleeps, the notification LED and a counter on the HOME icon: the
  system does not let apps do these.
- Playing videos or music attachments, WebP pictures: the app has no decoder for them; the preview shows the file
  information instead.

