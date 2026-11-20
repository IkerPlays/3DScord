# Chat

## Servers, channels and messages

- The server list shows folders, channels, forum threads, direct messages and group chats.
- Press **A** on a message to open its menu: reply (with or without ping), edit, delete, pin, react, copy, play audio and more.
- You can find servers, channels and members by name, and open user profiles.

## Mentions

Type `@` and the name (username, display name or nickname) of someone in the conversation, for example `@Lia hello`. When you send it, 3DScord turns it into a real
mention. If two names start the same, the longest wins (`@Ana Maria` before `@Ana`). You can also press **A** on a message and choose **Mention** to add its author to your draft. `@everyone` and `@here` ask you to send twice, so a slip does not ping the whole server.

## Attachments

- The file button opens a file browser with a preview before sending. Photos from the 3DS camera can be sent too (Channel Tools).
- Sound files and voice messages play from the message menu: Opus (voice messages), MP3, WAV, FLAC and Ogg Vorbis. The first three minutes are played.
- MP4 videos (H.264, up to 1280x720 and 16 MB) play without sound.
- GIF, APNG and Lottie animations play in the chat.

## Reactions, polls, pins and more

Reactions, polls, spoilers, embeds, stickers and emoji work. **Channel Tools** has **Save This Chat** (the last 100 messages as a text file in `sdmc:/3ds/3DScord/exports/`), the gallery, threads, members, quick replies, drafts, formatting, sounds and mark as read.

## Message variables

Type `{time}`, `{date}`, `{channel}` or `{server}` in a message and it is replaced when you send it (for example `Be there at {time}`). The time and the date use the formats chosen in Settings.

## Other handy things

- **Slow mode:** if the channel has one and you wrote a moment ago, the app tells you how many seconds are left.
- **Long messages** (over 2000 characters) are sent in several messages, cut at a line break or a space.
- **Who reacted:** in the message menu, shows the names behind the first three reactions.
- **Friends:** under each friend you see what they play or listen to; in the Pending list, the first row accepts all the requests.
- **Favorite channels:** Channel Tools > Add to Favorites; the list is in Quick Access > Favorite Channels.
- **Drafts:** what you leave unsent in a chat is kept (also after closing the app) and a chat with a draft shows "Draft waiting..." in the lists.
- **Recent emoji:** the emoji you used last come first in the picker.
- **Video links:** a link to a video shows its picture with a play button. A direct MP4 link (H.264, up to 16 MB) plays from the message menu without sound; sites like YouTube only show the preview (they do not give a file the console can play).

## YouTube links (Video Link Helper)

Play Video also works for YouTube links. The app asks public Piped / Invidious servers for a small MP4 (up to 360p, up to 16 MB) and plays it without sound. Only the video id is sent, and only when you touch Play Video. If no server answers, add your own in `sdmc:/3ds/3DScord/video_servers.txt`, one per line:

```
piped https://your-piped-api.example
invidious https://your-invidious.example
```

Turn it off in Settings > Video Link Helper. Long videos are usually over 16 MB and will not play.
- **Ignore a person:** message menu > Ignore This Person hides their messages (a short notice stays) and silences their sounds and notifications. It only happens on this console. Use Stop Ignoring on one of their messages to undo it.
- **Ignored People:** Quick Access > Ignored People lists them; X stops ignoring one.
