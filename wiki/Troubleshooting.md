# Troubleshooting

## First: save a report

**Settings > Diagnostics**, press **Y**. It writes `sdmc:/3ds/3DScord/diagnostics.txt` with the version, memory, signal and the last lines of the log. Press it **after** you tried the thing
that fails (the log only keeps the most recent lines). Attach it when you open an [issue](https://github.com/IkerPlays/3DScord/issues).

## Login problems

- QR code: update the Discord app on your phone and scan again.
- Email/password with a captcha or a new location: use the QR code or a `token.txt` instead.
- Check the console clock and Wi-Fi; Discord rejects requests with a wrong date.

## The app does not start or closes

- Make sure you use the latest release and a custom firmware that is up to date.
- Low memory: close other apps, restart the console, turn the Video setting off.
- Send the report above.

## Voice

- No sound from others: check the volume bars in the call view and the headphone button.
- They cannot hear you: the mic button must not be muted, and push to talk (if on) has to be held.
- The call dies when you close the lid: turn on **Keep calls with lid closed**.

## Video

- **The setting is missing:** only the New 3DS / New 2DS XL show it. See [[Compatibility]].
- **Others do not see your camera:** make sure everyone uses the Discord app (not a very old one), wait a few seconds and look at `S1` in your tile. Save a report and open an issue.
- **Black or frozen camera:** leave the Camera screen of 3DScord (the camera cannot be used twice) and try again.
- **Low frame rate:** the picture size drops by itself when the console is busy.
- **Screen share does not show:** others have to touch **Watch** on your stream.

## Notifications outside the app

See [[Notifications]]: they need Luma3DS and a system that keeps the app running in the background.

## Updates fail

- "No connection to GitHub": check Wi-Fi and try again.
- Clock wrong: fix the date in the console settings.
