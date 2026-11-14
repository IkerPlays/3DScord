# Changelog

## Unreleased
- **Save a chat to the SD card:** Channel Tools > **Save This Chat** writes the last 100 messages (time, author, text and the links of the files) to `sdmc:/3ds/3DScord/exports/` as a text file.
- **More translated texts:** French, German, Italian, Portuguese (Brazil) and Polish get 100 more strings each (settings, voice, friends, themes, battery and the tips), and the descriptions of the Video setting and of the call camera tool (English and Spanish) no longer say that video is not shown.
- **Notification LED in the app too** (Settings > Voice > Notification LED, on by default; needs Luma3DS): the light of the console pulses for a few seconds when a message that should notify you arrives, also while you use the app. In the background it goes on until you come back.
- **Mention from the message menu:** press A on a message and choose **Mention** to add `@name` of its author to your draft (then write the rest and send).
- **A test of the video the console sends runs in CI** (`tests/`, `.github/workflows/tests.yml`): the encoder and the SPS rewrite are built on a computer and ffmpeg must read the streams without errors, as constrained baseline level 3.1 without reordering.
- **The key of the call is hidden in the log** (`"secret_key":[hidden]`): the diagnostics report is meant to be shared, and it used to contain it.
- **The log is not flooded** by `Decrypt failed` lines any more (the first five and then one in a hundred), so the report keeps the lines that matter.
- **README in Spanish** (`README.es.md`).
- **The stream you watch fills the top screen** as soon as its first picture arrives (L + R changes the view). A stream connection that received nothing no longer stops the receiver used by the call.
- **Releases come with `SHA256SUMS.txt` and a QR code** (`3DScord-cia-qr.png`: FBI > Remote Install > Scan QR Code installs the CIA from it).
- **For contributors:** issue templates (bug report with the diagnostics report, and ideas), `CONTRIBUTING.md` with a translation guide, and a check of the language files in CI (`tools/check_lang.py`).

## 0.19.0
- **Sharper screen sharing:** the screen goes out at 320x192 (top or bottom) or 240x288 (both) to start with, and goes up to the full 400x240 when the console has room, down to 160x96 when it does not (it used to be 208x128).
- **Low memory warning:** once per session, if less than 3 MB of memory is left, a message suggests the Compatibility Mode in Settings.
- **A tip on the start screen** (a different one each start) while the app looks for updates. The diagnostics report also says the state of the video connections and how many pictures of others were decrypted or not.
- **Watch the Go Live of others** (EXPERIMENTAL, needs Settings > Voice > Video; Settings > Voice > **Watch Streams Automatically**): when someone in the call shares their screen, 3DScord asks Discord to watch the stream
  (STREAM_WATCH), opens a third voice connection for it (with its own DAVE group) and shows its picture like a camera (light quality). If the stream does not open in 25 seconds it gives up until the stream restarts.
- **A sound of its own for mentions:** a message that mentions you in a server plays a brighter, three-note sound (direct messages and the rest keep the usual one, and a sound you chose for the channel still wins).
- **Clock and Battery** (Settings > Appearance, off by default): a small time and battery gauge in the top right corner of the top screen, in the clock format you chose.
- **Friend requests warn you:** a message (and the notification sound, or the Home menu notification if the app is in the background) when someone sends you a friend request.
- **The screen stream no longer hangs:** if the Go Live stream does not open within 25 seconds or is lost, it is closed and a message says so. The statistics of your own picture (`fps`, `S1 R5...`) only show with the debug overlay (L + R).
- **Seven more themes** (Settings > Appearance > Theme): Sunset, Ocean, Forest, Rose, **High contrast** (black, white and yellow, for low vision), **Colour-blind friendly** (blue and orange, no red/green pairs) and Paper (warm light).
- **Clock and date formats:** Settings > Appearance has **Clock Format** (24 h or 12 h AM/PM) and **Date Format** (year/month/day, day/month/year or month/day/year) for the time of the messages.
- **Sound Volume** (Settings > Voice, 0 to 100 %) for the sounds of the app, and a **Low Battery Warning** when the battery is low and the console is not charging.
- **Fewer pictures lost in the video of the others:** RTP padding (the P bit) is now removed from the packets before the frame is joined; before, those bytes stayed in the frame and DAVE could not
  decrypt it (the log showed `DAVE could not decrypt a frame` for about one in four frames).

## 0.18.0
- **New logo, icon and banner:** a chat bubble with "3D" on a blurple tile, drawn for the project (it does not use the logos of Discord or Nintendo). It is the HOME Menu icon, the HOME Menu banner,
  the spinning logo of the start screen and the About screen, and `resources/logo.png`. `tools/brand/brand.py` makes all the sizes.
- **New sounds:** notification, join, leave, mic and headphones, outgoing and incoming call, and the HOME Menu banner jingle are now original bell-like tones, about as loud as before
  (`tools/brand/sounds.py`).
- **Older consoles:** the Video setting is hidden and the video features stay off on the Nintendo 3DS / 2DS (not New): the video is encoded and decoded in software and needs the New models.
- **Wiki:** the pages are in `wiki/` and `.github/workflows/wiki.yml` publishes them to the GitHub wiki. A Universal-DB description is in `docs/universal-db.md`, and the README has a table of compatible consoles.
- **Share the bottom screen too:** the button now goes nothing > inner camera > outer camera > top screen > bottom screen > both screens (top above bottom) > nothing. Changing between them does not close
  the stream. It also works outside the app.

## 0.17.0
- **Your camera and your screen show to the others in the call** (EXPERIMENTAL, Settings > Voice > Video): the video the console sends is now accepted by Discord and shown by its apps. Found by comparing with a
  community sender: the SPS of every key frame is rewritten for WebRTC receivers (`max_num_reorder_frames = 0`), the order is voice state, speaking, video signal, packets, and SPS and PPS go as separate packets without
  the playout-delay extension. The first frames are also decrypted again with your own key as a check (`[DAVE] Video self-check`), and `send()` of video packets is checked and counted.
- **Sharing the screen is a real "Go Live" stream:** the button opens a stream (STREAM_CREATE) with its own voice connection and DAVE group and sends the top screen on it; the others touch "Watch" to see it. The call
  and the camera keep working on the main connection. The screen is captured four times cheaper than before.
- **Camera button:** nothing > inner camera > outer camera > screen > nothing. Switching between the two cameras is much faster (the stream stays announced to Discord), the picture size goes up and down with the
  load (256x192 to 96x80, 160 to 380 kbps), your own inner camera preview is mirrored, and your own tile shows `N fps  M ms  K  D` and `S1 R5 T120 E0 V2` (Discord's confirmation, RTCP messages, frames sent,
  frames DAVE could not encrypt).
- **Fixes in the video of the others:** the decoder had a lower priority than your encoder and starved (equal priority now, and Discord is asked for 20 % quality while you send); RTCP packets were never recognised
  (the type was masked); key frames asked for by the others are made at most once a second; RTCP sender reports go once a second.
- **Notifications outside the app** (Settings > Notifications Outside the App): when a message arrives with the app in the Home menu, it adds an entry to the Home menu Notifications and pulses the notification LED.
  It needs Luma3DS and only works if the system keeps the app's network running in the background (it may not).
- **Sharing the screen outside the app (EXPERIMENTAL):** in the Home menu or another application the stream tries to capture what is on the top screen of the console; it depends on the system letting the app run and
  read that memory (the log says: `[VideoTx] Screen outside the app`).
- **You can mention people:** type `@name` (username, display name or nickname of someone in the conversation) and it is sent as a real mention; the longest name wins. Messages and replies.
- **More sounds can be played from the chat:** FLAC and Ogg Vorbis (music) next to the Opus voice messages, MP3 and WAV (minimp3, dr_flac and stb_vorbis, all public domain). The first three minutes play; stereo is mixed to mono.
- The incoming video is no longer saved to the SD card in every call.

## 0.16.0
You can now send video to a call: your inner camera or the top screen of the console. Experimental (Settings > Voice > Video, then join the call again).

- **Camera and Share Screen in a call:** the camera button next to the microphone and headphones goes through nothing > camera > top screen > nothing.
  Your own tile in the full call view shows what you send, with the frames a second and the time of each (`N fps  M ms  K  D`). The picture is
  coded as H.264 on the extra core, encrypted with DAVE and sent as RTP, with the video signalling of Discord's own clients. It works at about 12
  frames a second on a New 2DS XL.
- **The size adapts by itself:** if the encoder takes more than 90 ms a frame it goes down a size (camera 176x144, 128x96, 96x80; screen 208x128,
  160x96, 112x64), and back up when it has room. Discord is told each time.
- **Every picture comes out upright:** citro2d reads a sub-texture whose `top` is below its `bottom` as one turned a quarter (it swaps the axes), which is
  why cameras, shared screens and videos came out turned and mirrored. They are now written with the top row first and drawn the usual way, for the
  cameras and screens of others, your own preview, the camera view and the video player. **Video Rotation** and **Flip Video** are gone. New
  **Video Orientation Test** (Settings > Updates) shows a test picture and cycles the 8 orientations, in case another console is different.
- **Fix, camera black, photos black, camera of the call frozen on one frame:** the camera writes each frame into memory by itself, behind the
  cache of the processor, and the cache kept the first frame (a black one). The cache is now invalidated after every frame. The request for a
  frame is also kept open until the camera fills it, and the capture restarts when nothing comes for three seconds.
- **Phones:** the log shows the RTP video orientation that phones send (it is not applied yet).
- **Fix:** an own camera that was sending no longer blocks the Camera screen without saying why: it says the camera is busy.

## 0.15.0
The video of other people in calls now shows: stable on a New 2DS XL, upright, with the occasional hiccup. Experimental (Settings > Voice > Video).

- **Cameras and shared screens in calls work:** small under the speaker, in the person's tile in the full view, or on the whole top screen (L+R
  cycles the views). If the person stops, the next one who sends is shown.
- **Full call view redesigned like Discord:** black background, big rounded tiles with the avatar in the middle, a green border while someone
  speaks and the muted icons at the bottom; one person fills the screen, two sit side by side, more make a grid. When someone shares their
  screen it takes the left side and everyone else is stacked on the right. It draws above the chat.
- **Video Rotation** and **Flip Video** (Settings > Voice), in case the picture comes out turned or upside down on another console.
- **Faster and smoother:** a key frame is asked for as soon as someone starts sending video (the camera appears in seconds, not up to ~25 s),
  and again after a lost packet; the picture is no longer frozen until the next key frame. When the decoder falls behind it drops the oldest
  frame nothing refers to. The call asks Discord for a lower video quality (40 %), which a 3DS can decode in time.
- **Fixes in the picture (found with the help of tests on a computer):** the decoder gave memory from the linear heap back to the wrong
  allocator when Discord changed the resolution (could turn the picture into noise); the console's display transfer sheared and turned
  the picture, so it is now written straight into the texture in the GPU's order and stretched to fill it (drawn with its own
  proportions); reducing to half size takes the average of 2x2 pixels (no stripes). Checked: the decoder gives exactly the same pictures
  as ffmpeg on a real call stream, also compiled for ARM.
- **Play Video (no sound)** (message menu, MP4 up to 16 MB, 1280x720) uses the same picture path.
- **Fix:** the camera view (Camera mode, Send a Photo) was black on some consoles because of the display transfer; the picture now goes straight into the texture.
- **Network Test** (Settings > Updates): three small requests to Discord; shows the average and the worst time.
- **Message menu:** Exact Time, Copy to Draft, Send Again; an @everyone or @here message needs a second send to confirm.
- **Fix:** leaving a call no longer risks a crash while the video picture is being drawn.

## 0.14.0
Cameras and shared screens in calls, and videos in chats, now play: the console decodes H.264 in software (openh264). Both are experimental.

- **Save pictures and GIFs:** Y in the picture viewer saves them to the downloads folder. **Spoilers:** a caption that starts with ! sends the
  file as a spoiler. **Clear Picture Cache** (Settings > Chat) frees the memory of pictures already seen.
- **Software Decoder Test (experimental)** (Settings > Updates): decodes the video saved from a call with openh264 (Cisco, BSD licence), a decoder
  that reads the High profile, and shows the pictures at half size with the time each takes (`softtest.txt` on the SD card has the
  results). On a New 2DS XL it decodes the call's video at about 20 ms per picture (90 ms for a key frame).
- **Cameras and shared screens in calls (experimental):** with Settings > Voice > Video on, the video of the first person who sends
  one is decoded in software (openh264) on the extra core of a New 3DS and shown at half size: small under the speaker panel in the
  compact call view, in the person's tile in the full view, and on the whole top screen in a new **Video** view (L+R cycles
  compact > full > video > hidden). If the decoder falls behind, or a packet is lost, it waits for the next key frame instead of drawing
  a damaged picture, and it asks Discord for a new key frame (RTCP picture loss indication, at most every 1.5 s). Sending your own camera
  is not done.
- **Play Video (experimental, no sound)** (message menu, on MP4 attachments up to 16 MB): downloads the video and plays it on the top screen with
  the software decoder, on the extra core of a New 3DS. Frames that cannot be decoded in time are skipped. A pauses, Left/Right jump 5 seconds.
  Pictures up to 1280x720; H.264 in MP4 only. Sound is not played yet.
- **Exact Time** (message menu): shows the exact date and time a message was sent, and when it was edited.
- **Copy to Draft** and **Send Again** (message menu): put a message's text in the draft, or send one of your own messages again.
- **@everyone / @here confirmation:** a message with a mass mention is only sent when you send the same text a second time.
- **Fix:** leaving a call no longer risks a crash while the video picture is being drawn; key frames of up to 256 KB are kept (they were dropped
  above 160 KB) and a key frame that starts with a delimiter or SEI is recognised.

## 0.13.0
Experiments with video in calls: what Discord sends was measured and saved, and the result is that the video cannot be shown on the 3DS.

- **Video (experimental)** (Settings > Voice, off by default): tells Discord this console can receive video; the video that someone sends
  in a call is joined, decrypted and saved on the SD card (`recordings/video_in_*.264`, up to 3 MB), and it plays on a computer
  (H.264 High, 928x522, 30 fps). It is NOT shown on the console, and with it on the audio of the call can stutter. Leave it off.
- **Decoder Test** (Settings > Updates, experimental, New 3DS only): tried the hardware video decoder on that saved video. Result on a
  New 2DS XL: the decoder does not start (it blocks) with any work buffer size, so the video of a call cannot be seen. It writes
  `mvdtest.txt` with every step. The app now declares the permission for the decoder service.
- **Video Test** (Settings > Updates): records 10 seconds of camera video as H.264 and shows how fast the console encodes it.
- **Diagnostics report** holds the last 300 log lines kept in memory (useful with the log file off), and the Decoder and Video tests are
  in Settings > Updates next to it.

## 0.12.0
Call view on the top screen, camera mode, uploads up to your Discord limit, voice tools, an experimental video encoder and chat previews.

**Calls**
- **Call view on the top screen:** the person speaking in the top right corner with a green ring, or everyone in the call as
  tiles (with mute/deafen icons, the call time and statistics), or nothing so the chat has the whole screen. The first call
  button on the bottom screen (or L+R) changes it. Cameras and shared screens cannot be shown on the 3DS: they appear as
  CAM / LIVE badges.
- **"Typing..." shows the real name** (not the ID) in direct messages and servers.
- **Settings > Voice:** Call Volume, Busy (declines calls), Reconnect Calls (joins again if the call drops), Voice Tone
  (warmer or brighter voices), Video (experimental, off).
- **Call someone** from their profile (Left on the D-pad).
- **Voice moderation:** in someone's volume window press X to mute them for everyone, Y to remove them from the call, L to move
  them to another voice channel (needs the permissions).
- **Voice messages you receive** show their length and can be played, at 1.5x, queued, or stopped (message menu). Ogg sound
  files can be played too. **Record a Voice Message** from Channel Tools.
- **Record the Call** (Channel Tools): saves the call as a WAV file in `recordings/` on the SD card. Tell everyone first.
- **Soundboard** (Channel Tools): Discord's sounds and the sounds of the server of your call (Y switches).

**Camera and files**
- **Camera mode** (Channel Tools > Camera): live view on the top screen. The photo is only taken when you press A, R or tap, with a
  self-timer on X (3 or 10 s). Y switches to the inner camera. It is shown before it is sent. The File button's browser opens
  the same camera with X.
- **Bigger files, up to your account's limit:** 10 MB, 50 MB with Nitro Basic/Classic or a level 2 server, 100 MB at level 3,
  500 MB with Nitro. Files over 4 MB are sent straight from the SD card. A picture over the limit is shrunk to a JPEG
  (Settings > Chat > Picture Quality when Sending sets how much).
- **Images:** X turns the picture in the viewer; files in a message show their size.
- **Read File** (message menu): small text files open in the app.
- **Video Test (experimental)** (Channel Tools): records 10 seconds of camera video as H.264 (176x144) with a small built-in
  encoder and shows how fast the 3DS encodes it. The file plays on a computer with VLC. Video calls themselves are not done.
- **Video (experimental)** setting: tells Discord the console can receive video and logs what arrives. Nothing is shown yet.

**Other**
- **Updates** now has its own entry in the side menu (it checks right away); Settings keeps Previous Version, automatic updates
  and devbuilds.
- **GIF picker:** tries other forms of the request when Discord answers with nothing, remembers the one that worked.
- **Pinned servers** (Y on a server), **forum tag filter** (Y in a forum), **DM "typing..." marker**, **Follow This Channel**.

## 0.11.0
Big pictures are shrunk to fit, a tag filter for forums and fixes found in code review.

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
