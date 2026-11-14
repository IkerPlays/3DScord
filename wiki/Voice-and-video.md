# Voice and video

## Voice

- Join a voice channel or start a call from a direct message.
- Calls are end-to-end encrypted with Discord's DAVE protocol.
- You can change each person's volume, use push to talk, mute and deafen. Echo cancellation is on.
- With the lid closed the call keeps going if **Settings > Voice > Keep calls with lid closed** is on (it uses more battery).

## Video (experimental)

Needs a **New 3DS / New 2DS XL** and **Settings > Voice > Video (experimental)** turned on. See [[Compatibility]].

The call controls have two buttons for video, and they work at the same time:

- **Camera:** nothing, the **inner** camera (the one facing you; your own preview is mirrored), the **outer** camera (the one on the back), nothing...
- **Screen (Go Live):** nothing, the **top screen**, the **bottom screen**, **both screens** (top above bottom), nothing...

So you can show your face and share your screen together. Each one has its own encoder; the second one to start begins smaller so the console keeps up. The console has no way to record the sound of other apps, so a screen share carries no console sound.

Changing between the two cameras is quick: the stream stays announced and only the capture restarts.

### What others see

- The cameras appear in your tile in the call, in the Discord apps. They are small on purpose (the console encodes in software): the picture size goes up and down with the load.
- Your own tile shows `N fps  M ms  K  D` and `S1 R5 T120 E0 V2`: frames per second, milliseconds per frame, key frames asked for, drops; and Discord's confirmation, RTCP messages, frames sent,
  frames that could not be encrypted and the packet mode. They help when something does not show.

## Go Live (sharing your screen)

The screen options use a real **Go Live** stream: 3DScord asks Discord for a stream, opens a second voice connection for it (with its own encryption group) and sends the screen there.
The others touch **Watch** to see it. The call and the camera keep working on the main connection.

- It works from the HOME Menu and other apps too (experimental: the system has to let 3DScord keep running and read the screen).
- Leaving the call stops the stream.

## Watching the screen of someone else (Go Live)

The sound of the stream plays through the call (it follows the call volume and is silent while you are deafened).

When someone in the call shares their screen, 3DScord can open that stream by itself (**Settings > Voice > Watch Streams Automatically**, on by default when Video is on): it asks Discord to watch the
stream, opens a connection for it and shows the picture on the top screen like a camera. The console decodes it in software, so it asks for a light picture; it is slower than a camera. If the stream does not
open in 25 seconds it gives up until the stream starts again. It uses more memory and processor: turn the setting off if the console struggles.

## Seeing other people's video

The camera or screen of someone in the call is decoded in software and shown in their tile or on the whole top screen. If a picture does not show, check [[Troubleshooting]].
