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
- [ ] Voice channel picker with the people connected
- [ ] Mute / deafen with the physical buttons
- [ ] Push-to-talk on a console button
- [ ] Adjustable noise suppression
- [ ] Easier per-user volume
- [ ] Speaking indicator
- [ ] Custom DM call ringtone
- [ ] Keep audio playing with the lid closed

## Phase 8 — Media
- [ ] Short video playback; audio attachments
- [ ] YouTube link preview
- [ ] Attachment gallery for a channel
- [ ] Configurable image cache size
- [ ] WebP; animated avatars

## Phase 9 — Interface
- [ ] More themes (AMOLED, Nintendo, retro) and an on-console theme editor
- [ ] Font size; full CJK and emoji fonts
- [ ] Left-handed mode and configurable button shortcuts
- [ ] Own keyboard with emoji and commands
- [ ] 3D depth on the top screen; context panel on the top screen
- [ ] Swipe gestures; scroll bar and "go to bottom"
- [ ] Low power mode; layout per Old / New 3DS

## Phase 10 — Languages and account
- [ ] Translate the new strings into every language
- [ ] On-demand message translation
- [ ] Region date and time format; automatic timezone
- [ ] Faster remembered QR login; quick account switching
- [ ] PIN lock
- [ ] Log out other devices; view and close active sessions
- [ ] Export and import settings

## Phase 11 — Server tools
- [ ] Scheduled events
- [ ] Join with an invite link; leave a server
- [ ] See your permissions in a channel
- [ ] Basic audit log
- [ ] Moderation: kick, ban, timeout
- [ ] Roll back to the previous version if an update fails
- [ ] Integrity (hash) check of downloads
- [ ] Error log you can send; diagnostics screen (memory, network, gateway latency)

## Phase 12 — Extras
- [ ] Spotify: see what you are listening to
- [ ] Bot command mini-games with buttons
- [ ] Share themes between consoles
- [ ] HOME menu widget with the latest message

> Some items depend on what the 3DS system allows (background work, the HOME icon, the notification LED, playing audio
> with the lid closed). They will be tried on a real console and may change or be dropped.
