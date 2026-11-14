# Building from source

You need [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitARM, plus `makerom` and `bannertool` for the `.cia`.

```bash
git clone https://github.com/IkerPlays/3DScord.git
cd 3DScord
make -j$(nproc)
```

Output: `3DScord.3dsx`, `3DScord.cia` and `3DScord.elf`.

## GitHub Actions

- `build.yml` builds every push. Run it by hand (**Actions > Build 3DSX + CIA > Run workflow**) to publish a **devbuild**.
- `release.yml` (**Stable release**) publishes a stable version; type the version (for example `0.17.0`). The notes come from the matching `## 0.17.0` section of `CHANGELOG.md`.

## Quick syntax check without the toolchain

`./tools_check.sh source/file.cpp` compiles a file against the 3DS headers for errors only (needs the headers it lists).

## Project layout

| Folder | What is in it |
|---|---|
| `source/`, `include/` | The app: `core`, `discord` (gateway, voice, DAVE), `network`, `ui`, `utils` |
| `library/` | Bundled libraries (Opus, libdave, MLSPP, openh264, minih264, minimp3, dr_flac, stb_vorbis, ThorVG, rapidjson, ...) |
| `romfs/` | Files inside the app: languages, icons, sounds, licenses |
| `resources/` | Icon, banner and banner sound for the CIA |
| `tools/brand/` | Scripts that make the logo and the sounds |
| `trailer/` | Scripts, copy and thumbnails for the trailer |

The logo and the sounds are original artwork made with the scripts in `tools/brand/`.

## Credits

Based on [TriCord](https://github.com/2b-zipper/TriCord) by 2b-zipper. Libraries and their licenses are in [romfs/licenses](https://github.com/IkerPlays/3DScord/tree/main/romfs/licenses).
