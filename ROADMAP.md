# Roadmap

Work is split into **phases**. Every finished phase is published as a **devbuild** (with release notes). Every
**second** phase also ships a **stable** release, which bundles the notes of both.

| Phases | Stable |
| --- | --- |
| 1 + 2 | 0.2.0 |
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
- [ ] Upload files from the SD card
- [ ] Upload photos from the 3DS camera
- [ ] Compress big images before uploading
- [ ] Send stickers
- [ ] GIF picker (Tenor / Giphy)
- [ ] Voice messages recorded with the microphone
- [ ] Save received images to the SD card
- [ ] Image viewer with touch zoom and pan

## Phase 4 — Servers and people
- [ ] Threads: create, join, archive
- [ ] Follow announcement channels
- [ ] Forum tags and filters
- [ ] Search servers and channels by name
- [ ] Reorder and pin favorite servers; editable folders
- [ ] Member list with roles and presence
- [ ] Full user profile (banner, bio, roles)

## Phase 5 — Friends and DMs
- [ ] Friends list with pending requests; send and accept requests
- [ ] Block and unblock users
- [ ] Create group DMs
- [ ] Private notes on users; per-server nicknames
- [ ] Pinned DMs on top; unread-first DM view
- [ ] Search in DMs; typing indicator in the list

## Phase 6 — Notifications
- [ ] Background notifications by polling (StreetPass / SpotPass-like)
- [ ] Notification LED on mentions
- [ ] Different sound per server or channel
- [ ] Mute servers and channels for a chosen time (partly in TriCord)
- [ ] Scheduled Do Not Disturb
- [ ] Unread counter on the HOME icon
- [ ] "While you were away" summary on open

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
