# Portable SDL2/Vulkan frontend

`mushroom-sdl` is the cross-platform frontend. It uses the same NESRecomp
engine, save-state format, widescreen compositor, and SMB3 payload validation
as the Windows launcher, but replaces the Win32 shell with SDL2. The presenter
has a Vulkan path and an SDL2 renderer fallback.

The Windows launcher (`MushroomRecompiled-v1.5.0.exe`) remains available. The
two frontends share the engine, not their UI code.

## Requirements

Linux:

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
                 libsdl2-dev libvulkan-dev glslc python3
```

`libvulkan-dev`, `glslc`, and Python are only needed for the Vulkan presenter.
Without them the build still succeeds and the frontend uses SDL2.

Windows: CMake, a C compiler, SDL2 development files, and the Vulkan SDK for
the Vulkan presenter. `glslc` must be on `PATH` during configuration so the
shaders can be compiled and embedded. If Vulkan is not available at build or
run time, the frontend falls back to the SDL2 renderer.

## Generate and build

The generated blocks are not versioned. From the repository root:

```sh
cmake -S tools/nesrecomp-src/recompiler -B build/recompiler-native \
      -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/recompiler-native -j 8

./build/recompiler-native/NESRecomp "Game.nes" \
    --game game.toml --cycle-seed-file docs/smb3-cycle-seeds.txt

cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j 8
```

The Windows script does the same generation and build in one step:

```powershell
.\build-local.ps1 -RomPath "C:\path\Game.nes"
```

## Run

```sh
./build/native/mushroom-sdl --rom "Game.nes"
```

Useful options:

```text
--renderer vulkan|sdl   presentation backend (Vulkan when built with it, SDL2 fallback)
--scale N               window scale 1-8
--fullscreen            start fullscreen
--widescreen            start in 426x240 widescreen
--aspect fit|integer|stretch
--scanlines 0|1|2
--brightness 50-150
--audio-rate 44100|48000|96000
--volume 0-100
--mute
--slot 1-5
--replay FILE           feed raw controller bytes (one per frame)
--record FILE           write one controller byte per emulated frame
--no-vsync
--frames N              run N frames then exit (smoke test)
--screenshot PATH       write a presented frame as PNG
--screenshot-frame N    frame to capture (default 1)
```

The ROM path and the frontend settings are stored in the platform preference
directory:

- Linux: `~/.local/share/MushroomRecompiled/MushroomRecompiled/config.ini`
- Windows: `%APPDATA%\MushroomRecompiled\MushroomRecompiled\config.ini`

Save states live in a `saves/` directory next to that file. Slot files are
`state-1.sst` through `state-5.sst`. Writes go to a sibling `.tmp` file first
and are renamed over the target, so a failed or interrupted save does not
replace a good state with a partial file. The engine does not auto-load a state
at startup, matching the existing launcher behaviour.

The game's own progress slots live in work RAM, which the cartridge header
marks non-battery. Like smb3pc, the frontend still persists that window in
`saves/smb3.srm`: loaded after the ROM, flushed about once a second while it
changes and on exit, written through a `.tmp` file and rename. A file of the
wrong size is reported and never overwritten. Console Reset (R) preserves RAM
across power-on, so progress survives a reset. Replays never touch the file.

`G` saves the session so far as the first free `saves/replay-N.rec`: raw
controller bytes, one per emulated frame, like smb3pc's desktop recordings.
`--record FILE` streams the same bytes to a file; `--replay FILE` feeds them
back instead of live input and quits when the recording ends. A replay must
start with the same widescreen mode it was recorded with, and save states
are refused while one plays.

## Controls

| Action | Keyboard | SDL gamepad |
|---|---|---|
| Move | Arrow keys | D-pad or left stick |
| B: run / hold | Z | B / Square / X |
| A: jump / fly | X | A / Cross / Y |
| Select: inventory | Shift | Back / Share |
| Start: NES pause | Enter | Start / Options |
| PC menu | Esc | Guide / Home |
| Save state | F5 | Menu |
| Load state | F7 | Menu |
| Save session replay | G | — |
| Select slot | 1-5 | Menu |
| Widescreen | F10 | Menu |
| Fullscreen | F11 | Menu |
| Mute | F8 | Menu |
| Volume | - / = | Menu |
| Reset machine | R | Menu |

The menu also exposes resume, save, load, widescreen, fullscreen, and quit.
Gamepad mappings come from `assets/gamecontrollerdb.txt` when present; the
built-in SDL mapping is used otherwise.

Keyboard bindings can be changed in `config.ini` with the `key_a`, `key_b`,
`key_select`, `key_start`, `key_up`, `key_down`, `key_left`, and `key_right`
keys. Values are SDL scancode names such as `Z`, `X`, `Left Shift`, `Return`,
`Up`, `Down`, `Left`, and `Right`. Keyboard menu navigation remains on the
arrow keys, W/S, Enter, and Escape. The config is written on exit, so bindings
survive restarts.

## Scope and known gaps

- The Vulkan presenter performs nearest-neighbour presentation, integer/fit/
  stretch aspect handling, brightness, and optional scanlines. It is not a
  shader-preset system.
- The portable frontend does not include the Win32 launcher's full settings
  pages or per-device button remapping UI. It loads `gamecontrollerdb.txt` and
  uses the standard mapping above; settings such as scale, renderer, aspect,
  volume, sample rate, and slot are persisted in `config.ini`.
- Music and sound effects run on two Blargg voice sets (music + sfx), routed
  by owner tags the translator emits around the game's sound routines
  (`[[sound_owner]]` in game.toml), with the sfx set reset at every level
  load. The four music gates that muted music while an effect shared the
  channel are removed (`[[sound_gate]]`), so music keeps playing; the
  interpreter sees the same fall-through through a ROM-read override, and
  `smb3-verify` checks native/interpreter agreement over 3,000 frames.
  Duplicating the CPU-visible APU state this way leaves DMA/IRQ timing and
  the save-state format unchanged.
- Screenshot paths are passed to the engine's PNG writer; use an ASCII path on
  Windows until the writer has a wide-character entry point.

## Verification

The native verifiers are independent of the frontend:

```sh
./build/native/smb3-verify "Game.nes"
./build/native/smb3-wide-verify "Game.nes"
./build/native/smb3-scenes-verify "Game.nes"
```

`smb3-verify` checks deterministic save/load continuation, native/interpreter
hash agreement over 3,000 frames, and audio at 44.1/48/96 kHz.
`smb3-wide-verify` checks the widescreen hashes and extended enemy state.
