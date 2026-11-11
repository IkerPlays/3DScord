# Changelog

## Unreleased
- Auto-updater: at startup, while the Discord logo spins, the app looks for a newer release, downloads it, installs it
  (CIA via the system installer, 3DSX by replacing the file) and restarts. Optional devbuild updates.
- New Settings sections: Discord profile (display name, About Me, pronouns, color, avatar, banner), status and custom
  status, chat/message display options, privacy options, and Updates.
- Devbuilds are numbered `x.y.z-dev.N` and the number is built into the app.

## 0.1.0
- First 3DScord release, based on TriCord (2b-zipper, GPLv3).
- GitHub Actions: automatic `.cia` + `.3dsx` builds on every push and pull request.
- Manual workflows for devbuild pre-releases and stable releases.
- Dependabot keeps the GitHub Actions versions up to date.
