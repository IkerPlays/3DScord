# Contributing to 3DScord

Thanks for helping. Bug reports, ideas, translations and code are all welcome.

## Report a problem

Open an [issue](https://github.com/IkerPlays/3DScord/issues/new/choose). Attach the report of **Settings > Diagnostics** (press **Y** after the failure) and never paste your token.

## Translate

The texts are in `romfs/lang/<language>.json` (`en_US.json` is the reference). A missing key falls back to English, so a partial translation is fine.

1. Copy the keys you want to translate from `en_US.json` into your language file.
2. Keep the placeholders (`{0}`, `{1}`) and the escape sequences (`\n`, `\"`) as they are.
3. Run `python3 tools/check_lang.py` to see which keys are missing or unknown.
4. Open a pull request.

## Build

See [Building from source](https://github.com/IkerPlays/3DScord/wiki/Building-from-source). `./tools_check.sh file.cpp` does a quick syntax check of a file without the toolchain.

## Code

- C++17 for the 3DS (devkitARM). Match the style of the file you edit (tabs, comments that say why).
- Keep `README.md`, `CHANGELOG.md` (the `## Unreleased` section), `ROADMAP.md` and both `en_US.json` and `es_ES.json` in step with what you change.
- Large or risky features should be behind a setting and say what is experimental.
- Releases: devbuilds come from running **Build 3DSX + CIA** by hand; stable ones from **Stable release** with the version number.

## Licence

3DScord is GPLv3 (it is a fork of TriCord by 2b-zipper). By contributing you agree your work is released under the same licence.
