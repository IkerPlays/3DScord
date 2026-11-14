# Installation

## What you need

- A Nintendo 3DS, 2DS, New 3DS or New 2DS XL with custom firmware (Luma3DS). See [[Compatibility]].
- An SD card and a Wi-Fi connection.
- A Discord account.

## Steps

1. Download `3DScord.cia` or `3DScord.3dsx` from the [latest release](https://github.com/IkerPlays/3DScord/releases/latest).
2. **CIA:** copy it to the SD card and install it with FBI. It appears on the HOME Menu with the 3DScord icon.
   **3DSX:** copy it to `/3ds/3DScord/` on the SD card and start it from the Homebrew Launcher.
3. Start 3DScord and [log in](First-steps).

The app looks for updates every time it starts, shows the release notes and installs the new version when you accept. The CIA restarts by itself;
the 3DSX has to be started again from the Homebrew Launcher.

## Stable releases and devbuilds

| Kind | What it is |
|---|---|
| **Stable** (`v0.17.0`) | Tested versions. |
| **Devbuild** (`v0.17.1-dev.3`) | Work in progress, published after each step. Turn on **Settings > Updates > Include Devbuilds** to get them. |

## Coming from TriCord

On first start 3DScord moves `sdmc:/3ds/TriCord` to `sdmc:/3ds/3DScord`, so your accounts, settings and themes are kept.

## Uninstall

Delete the title with FBI (CIA) or remove `/3ds/3DScord/3DScord.3dsx`. Your data is in `sdmc:/3ds/3DScord/`; delete that folder to remove accounts and settings too.
