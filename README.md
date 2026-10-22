# Mushroom Recompiled

An experimental, unofficial native port of Super Mario Bros. 3 (USA). The 6502 code is translated to C by NESRecomp and compiled natively; the runtime synchronizes the PPU, APU, DMA and MMC3 mapper.

**The ROM is not included.** You must supply your own legally obtained copy of Super Mario Bros. 3 (USA, PRG1). The full game has not yet been validated.

## Features

- **Widescreen (16:9):** widens the scenery of horizontal stages without stretching tiles or sprites. The original HUD stays centred with black bars. Vertical rooms, the map and title screens keep the native frame.
- **Whole-pixel scaling:** every frame is presented on a 426 × 240 canvas at an integer multiple. Fullscreen uses the largest exact 16:9 mode (1280 × 720 on a 1080p panel).
- **Video:** Vulkan presentation (the only backend), optional scanlines and brightness.
- **Audio:** 44.1, 48 or 96 kHz output, with volume and mute.
- **Input:** keyboard and gamepad (SDL_GameControllerDB), with remappable keys.
- **ROM picker:** load from `--rom`, from a single nearby `.nes` file, or from a picker in the window (drag and drop works). The validated path is remembered.
- **Saves:** SRAM progress saved automatically, five save-state slots, and session replays.
- **PC menu:** Esc or the Guide button opens resume, save/load state, fullscreen and quit.

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Move | Arrow keys | D-pad / left stick |
| B (run / tail) | Z | B / X |
| A (jump / fly) | X | A / Y |
| Select | Left or Right Shift | Back |
| Start | Enter | Start |
| PC menu | Esc | Guide |
| Fullscreen | F11 | — |
| Mute | F8 | — |
| Volume | - / = | — |
| Save / load state | F5 / F7 | — |
| Save-state slot | 1–5 | — |
| Save session replay | G | — |
| Console reset (SRAM kept) | R | — |

Keys can be remapped with `key_*` entries in `config.ini`.

## Known limitations

- Only the USA revision is supported. goodNES and No-Intro/Rev 1 dumps are accepted because they share the same PRG/CHR payload.
- Physical controllers and audio devices have not been tested exhaustively.
- Widescreen does not apply to vertical rooms, the map or title screens.
- No minimum hardware requirements have been certified.

## Building on Linux

Install the build tools, SDL2 and the Vulkan headers. `libvulkan-dev` and `glslc` are required for the frontend; without them only the engine and the verifiers build.

**Debian / Ubuntu**

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
                 libsdl2-dev libvulkan-dev glslc python3
```

**Fedora**

```sh
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config \
                 SDL2-devel vulkan-loader-devel glslc python3
```

**Arch / Manjaro**

```sh
sudo pacman -S base-devel cmake ninja pkgconf sdl2 vulkan-headers \
               vulkan-icd-loader shaderc python
```

**openSUSE**

```sh
sudo zypper install gcc-c++ cmake ninja pkg-config libSDL2-devel \
                    vulkan-devel shaderc python3
```

Package names vary between releases; if one is missing, search your package manager for SDL2 and Vulkan development headers.

Build the project (the ROM is translated into C at build time):

```sh
cmake -S tools/nesrecomp-src/recompiler -B build/recompiler-native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/recompiler-native -j 8
./build/recompiler-native/NESRecomp "Game.nes" --game game.toml --cycle-seed-file docs/smb3-cycle-seeds.txt

cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j 8
./build/native/mushroom-sdl --rom "Game.nes"
```

Run `./build/native/mushroom-sdl --help` for all options.

## Building on Windows

You need CMake, LLVM-MinGW (or another Clang/GCC toolchain with `mingw32-make`) on the PATH, PowerShell, and the Vulkan SDK (which provides `glslc`). The first run downloads the official SDL2 2.30.0 development package and verifies its SHA-256.

```powershell
.\build-local.ps1 -RomPath "C:\path\Game name.nes"
```

This validates the ROM, generates the C code, and builds `build/MushroomRecompiled.exe`. Add `-Verify` to also run the core verifier.

## Command-line options

| Option | Meaning |
|---|---|
| `--rom FILE` | Load a ROM (otherwise the remembered or nearby ROM is used) |
| `--scale N` | Window scale 1–8 (default 3) |
| `--fullscreen` | Start fullscreen |
| `--scanlines 0\|1\|2` | CRT scanline strength |
| `--brightness 50-150` | Picture brightness |
| `--audio-rate 44100\|48000\|96000` | Audio sample rate |
| `--volume 0-100`, `--mute` | Audio level |
| `--slot 1-5` | Active save slot |
| `--menu` | Start paused in the PC menu |
| `--record FILE` / `--replay FILE` | Record or play raw controller bytes |
| `--frames N` | Run N frames then exit (smoke test) |
| `--screenshot PATH`, `--screenshot-frame N` | Write a presented frame as PNG |
| `--no-vsync` | Disable vsync |

## Where files are stored

- `config.ini`: ROM path, video, audio and key settings. Linux: `~/.local/share/MushroomRecompiled/MushroomRecompiled/`. Windows: `%APPDATA%\MushroomRecompiled\MushroomRecompiled\`.
- `saves/`: SRAM progress (`smb3.srm`), save states (`state-1.sst` … `state-5.sst`) and replays (`replay-N.rec`) in the same folder.

## Verification

The ROM parser tests need no ROM:

```sh
node --test tests/rom.test.mjs
```

The native verifiers take the ROM path as the first argument:

```sh
./build/native/smb3-verify "Game.nes"
./build/native/smb3-wide-verify "Game.nes"
./build/native/smb3-scenes-verify "Game.nes"
./build/native/smb3-wide-collision-verify "Game.nes"
```

More detail: [testing guide](docs/TESTING.md), [portable frontend](docs/PORTABLE.md), [widescreen notes](docs/WIDESCREEN.md).

## Disclaimers

- **No Nintendo or SMB3 assets are in this repository.** It contains no ROM, no generated game code, and no graphics, music or sound data from the game. The 6502 code and data are read from your own ROM at build time. Do not share ROMs, generated files or save data.
- **AI-assisted code.** Parts of this project were written or edited with the help of AI coding tools. The code has been reviewed and tested only to the extent described in the docs. It may contain bugs or behaviour that differs from the original game. Review it before relying on it.

## Credits and licenses

Beta testers: **ALEISTECH** and **RETROCESSO**.

This is an independent, unofficial project. The integration code is MIT-licensed. NESRecomp is licensed under PolyForm Noncommercial 1.0.0, which is not changed by the MIT license here. The APU audio uses Nes_Snd_Emu 0.1.7 and Blip_Buffer 0.3.3 by Shay Green, licensed under the GNU LGPL (text in `src/audio/nes_apu/LGPL.txt`). See [LICENSE_SCOPE](LICENSE_SCOPE.md) and the `LICENSES` folder. ROMs and generated assets are not published.

[![Powered by Underplay](png.png)](https://underplay.com.br)
