# Notifications

## In the app

New messages in unmuted channels and direct messages play a sound (you can pick a sound per channel or server) and add a badge. **Quiet hours** (Settings) silence the sound at night.

## Outside the app

With **Settings > Notifications Outside the App** on, a message that arrives while the app is in the HOME Menu adds an entry to the **Notifications** of the HOME Menu and pulses the
notification LED.

This needs **Luma3DS** (it opens the system services the app uses) and it only works while the system keeps the app's network running in the background. If the console pauses the app when
you press HOME, nothing can be shown until you return. The log says what happened (`[Notify] ...`) and **Settings > Diagnostics** can save a report.

A program that runs all the time (a system module) would be needed for notifications with the app closed; 3DScord does not install one.
