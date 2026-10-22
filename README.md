# Mushroom Recompiled

**The ROM is not included with the project or the ZIP.** Select your compatible copy from the menu. The full game has not yet been validated.

## Widescreen and scene fixes — v1.5

The downloadable version expands the scenery of horizontal stages to a 16:9 canvas without stretching tiles or sprites. The original HUD remains centered, with black bars on the sides. That 16:9 picture is always presented, filling the screen; every picture pixel keeps the same width because the window is a whole multiple of the 426x240 canvas and fullscreen runs in the largest 16:9 display mode that is an exact multiple (1280x720 on a 1080p panel).

The compositor uses the actual stage map. The widescreen mode extends object activation, horizontal culling, and rendering to the 16:9 area; vertical rooms and unsupported scenes remain in native framing. Read the [limits and tests](docs/WIDESCREEN.md). Historical packages remain available with updated download names.
The current build keeps bonus games and exit transitions centered with clean black margins, while the end-of-level card and run-off stay in the wide frame and follow the camera to the level edge without creating late margin enemies. It repairs object seams at the original viewport edges and extends enemy activation and persistence to the 16:9 area, drawing their sprites outside the old clipping region. Retains edge and menu fixes. Remembers the ROM and launches directly; Esc opens the menu during gameplay.

## Download and test

Download the [portable Windows package](https://github.com/Adoregabriel2005/mushroomrecompiled/releases/tag/v1.5.0-exp), extract the complete ZIP, and run **MushroomRecompiled.exe**.

1. Choose **Select ROM**.
2. Point to the game ROM as a `.nes` file.
3. After validation, the game starts. On subsequent launches, the remembered ROM is loaded directly.

**Esc** pauses and opens the menu to save, load, continue, configure, or quit. If no ROM is registered, or if the saved file is missing/incompatible, the launcher appears. Direct startup does not automatically load a save state.

Requires Windows x64 and a video/audio device recognized by the system. No minimum requirements certification has been performed. Read the [testing guide](docs/TESTAR.md) and the [settings documentation](docs/INTERFACE.md).

## Interface

The launcher uses the project identity and the original Underplay image supplied by the project owner. In-window settings: keyboard, controller, video, audio, ROM, Project, and Credits. Apply saves changes; Discard preserves the previous ones.

- Controls: automatic or per-device selection, remapping, dead zone, button test, D-pad and analog. USB/Bluetooth/receivers via SDL2 and SDL_GameControllerDB, based on Windows recognition and pairing.
- Video: whole-pixel window scale, scanlines, brightness, and fullscreen in the largest exact-multiple 16:9 display mode. 16:9 expands the scenery and object activation range in supported horizontal stages while keeping the original HUD.
- Audio: volume, mute, device, 44.1/48/96 kHz, and 20/40/60 ms buffer reserve.
- Save states: five slots; save/load from the menu.

| Action | Default keyboard | Default controller |
|---|---|---|
| Move | Arrow keys | D-pad / analog |
| B: run / tail | Z | X / Square |
| A: jump / fly | X | A / Cross |
| Select: items | Shift | Back / Share |
| Start: NES pause | Enter | Start / Options |
| PC menu | Esc | Home / Guide, remappable |
| Fullscreen | F11 | Via menu |
| Save state slot | 1 to 5 | Via menu |

Specific physical controllers still need retesting. Automated testing uses a virtual SDL joystick; this does not prove compatibility with every model. The portable SDL2/Vulkan frontend uses the defaults documented in [PORTABLE.md](docs/PORTABLE.md), including configurable keyboard bindings.

## Technical status

Cycle-accurate NESRecomp backend: 6502 code translated to C and compiled natively, with PPU/APU/DMA/MMC3 synchronized by the runtime.

In the 3,000-frame test script, approximately **99.995% of cycles** ran recompiled code. An instruction crossing a bank boundary uses an interpreted fallback. All 3,000 state hashes matched the interpreted execution under the same hardware model. This validates the translation for that script, not all stages nor independent equivalence with a physical console.

The following also passed: continuation after save/load, audio at all three sample rates, and interface tests (seven pages, apply/discard, and virtual hotplug). Details in [STATUS](docs/STATUS.md).

## Building

### Linux: portable SDL2/Vulkan frontend

The native build uses the same engine and generated C blocks. Install CMake,
Ninja, pkg-config, SDL2, and (optionally) the Vulkan headers, `glslc`, and
Python 3 for the Vulkan presenter:

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
                 libsdl2-dev libvulkan-dev glslc python3
```

Generate the C blocks once, then build:

```sh
cmake -S tools/nesrecomp-src/recompiler -B build/recompiler-native \
      -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/recompiler-native -j 8
./build/recompiler-native/NESRecomp "Game.nes" \
    --game game.toml --cycle-seed-file docs/smb3-cycle-seeds.txt

cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j 8
./build/native/mushroom-sdl --rom "Game.nes"
```

If Vulkan or `glslc` is missing, the executable still builds and uses the SDL2
renderer. See [PORTABLE.md](docs/PORTABLE.md) for options, save-state locations,
controls, and the current scope.

### Windows

Requires Windows, CMake, LLVM-MinGW/Clang, and mingw32-make in PATH. The first setup downloads the official SDL2 2.30.0 package and verifies its SHA-256.

```powershell
.\build-local.ps1 -RomPath "C:\path\GAME NAME.nes"
```

The script compiles the NESRecomp generator, produces the C blocks using your ROM and the seeds in docs/smb3-cycle-seeds.txt, and compiles the application. Both the goodNES and No-Intro/Rev 1 dumps are accepted: they share the same PRG/CHR payload and differ only in the iNES/NES 2.0 header, so the script validates the payload hashes. Generated files are not versioned. Output: build/MushroomRecompiled-v1.5.0.exe. When SDL2 is available, CMake also builds `build/mushroom-sdl.exe`, the portable frontend described in [PORTABLE.md](docs/PORTABLE.md).

```powershell
.\build-local.ps1 -RomPath "Game name.NES" -Verify
cd build
.\smb3-ui-verify.exe
```

Auxiliary inspection: `node tools/rom.mjs <ROM.nes> [extraction directory]`; parser tests: `node --test tests/rom.test.mjs`.

## Credits and licenses



Beta testers: **ALEISTECH** and **RETROCESSO**. Thanks for the testing and bug reports.

Independent, unofficial project. [Branding](docs/BRANDING.md).

The integration code is licensed under MIT. NESRecomp is licensed under **PolyForm Noncommercial 1.0.0**; the MIT license of the interface does not alter the runtime's terms. See [LICENSE_SCOPE](LICENSE_SCOPE.md) and LICENSES. The ROM and generated assets are not published; the release executable contains code and materials derived from the game.

[![Powered by Underplay](png.png)](https://underplay.com.br)
