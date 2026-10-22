# Experimental Linux / Steam Deck frontend

The Linux application uses SDL2 for video, queued stereo audio and standardized
game controllers. It is intentionally minimal: it does not include the Windows
launcher, ROM picker, settings pages or five-slot save menu. The existing Win32
frontend and Windows build script remain available.

This is native CPU recompilation with NES PPU/APU/MMC3 emulation, **not a full
native game engine**. Expanded scenery and object activation use the same
experimental widescreen runtime as Windows. Unsupported scenes remain centered;
the original HUD keeps black side margins. Whole-game correctness and
compatibility across SteamOS versions are not guaranteed.

## Build locally

On Debian/Ubuntu x86-64:

```sh
sudo apt-get install build-essential cmake ninja-build pkg-config libsdl2-dev
bash tools/build-linux.sh --check
bash tools/build-linux.sh --rom '/path/to/game.nes' --verify
```

`--check` compiles NESRecomp, the SDL frontend and runtime objects, and runs the
portable host tests, without reading a ROM or generating game code. It is also
the GitHub Actions job. It cannot establish game correctness on its own.

The full build validates your local ROM, runs NESRecomp with `game.toml` and the
project seeds to generate ignored `generated/` C sources, then compiles the
application and verification programs. Ninja is used when installed; otherwise
CMake uses Make. Override parallelism with `JOBS=4` and the output directory with
`--build-dir /path/to/build`. Do not run simultaneous Windows/Linux generation
in the same checkout. `--verify` runs the native/interpreter, audio, state,
widescreen and scene regression programs.

The supported canonical USA PRG1 iNES image has SHA-256:

```text
4377a7f5e6eb50bdd2ac6f249bf1a7085500aca8eb41f38545c3a2731c51a579
```

The runtime additionally verifies payload CRC32 `0x2e6301ed` and game signatures.
The SHA covers the entire file, including its 16-byte iNES header. An alternate
header can change SHA-256 without changing the payload CRC. Build scripts
require the canonical image and never rewrite user input. The same corrected
SHA-256 is used by the Windows build script.

No ROM, generated game C, saves, binaries or copyrighted Nintendo game assets
are included in this patch or CI artifacts. Users must supply their own
compatible ROM locally. The integration's MIT license does not change the
existing NESRecomp **PolyForm Noncommercial 1.0.0** terms.

## Run on Steam Deck

Build in a Linux development environment with SDL2 development files. SteamOS
has a read-only base system; a build container or another Linux machine can be
used, but binaries may need rebuilding against the target platform's libraries.
Do not assume a binary built on a newer distribution will run on SteamOS.

```sh
bash run-steamdeck.sh --rom '/path/to/game.nes' --aspect 16:9 --fps 40
```

Add the compiled `build/linux/app/MushroomRecompiled` executable as a non-Steam
game. Set launch options to `--rom "/path/to/game.nes" --fullscreen --fps 40`.
Choose Steam Input's **Gamepad** template. Alternatively add `run-steamdeck.sh`
as the executable after making it executable (`chmod +x run-steamdeck.sh`).
The script starts fullscreen; `--windowed` overrides this for desktop testing.
`MUSHROOM_BINARY=/absolute/path/MushroomRecompiled` selects a different build.

SDL's GameController API receives standardized buttons from supported physical
or Steam virtual controllers. The application loads `assets/gamecontrollerdb.txt`
beside the executable and handles connect/disconnect. It respects Steam Input's
device filtering; it does not force raw access to the Deck controller. Pair
wireless controllers in SteamOS first. Model-specific compatibility needs
physical testing; the Guide/Steam button is left to SteamOS.

| Action | Keyboard | Gamepad / Deck |
|---|---|---|
| Move | Arrows | D-pad / left stick (dead zone 8000) |
| Jump / fly (NES A) | X | A or Y |
| Run / action (NES B) | Z | B or X |
| NES Start | Enter | Start / Menu |
| NES Select | Shift | Left stick click |
| PC pause / resume | Esc | Back / View |
| Quicksave | F5 | R1 |
| Quickload | F9 | L1 |
| Toggle 4:3 / 16:9 | F10 | Keyboard only |
| Toggle fullscreen | F11 | Keyboard only |
| Quit | Ctrl+Q / close window | Steam overlay |

PC pause darkens the picture and displays a pause symbol. Window focus loss
also pauses. Resume explicitly with Esc/Back. Start remains the game's own pause
button. One quicksave slot is stored under SDL's per-user preference directory
(normally `~/.local/share/Underplay/MushroomRecompiled/quickstate.save`).
Saving is explicit; quitting does not automatically save or load a state.
Save/load results appear on stderr. No Windows settings are read or changed.

## Timing and diagnostics

The fixed simulation clock uses the runtime's NTSC frequency (about 60.10 Hz).
SDL presentation runs separately; `--fps 30`, `40`, `60` or `0` (uncapped) limits
presentation only. Input and simulation are pumped between presentations, and
audio is drained on every simulation step. This does not make the game run at
half speed with a 30 FPS presentation cap. Sustained CPU overload can still
prevent real-time execution.

After a stall, at most eight simulation ticks run per pump. Excess whole ticks
are discarded and reported as `dropped`; fractional time is preserved. Pause,
state operations and fullscreen changes reset elapsed debt and clear queued
audio. Audio uses 48 kHz, signed 16-bit stereo; if an audio device cannot open,
the application continues silently with an error message. A queue above 200 ms
is cleared to avoid unbounded latency after a stall.

```sh
MUSHROOM_PERF_LOG=1 bash run-steamdeck.sh --rom '/path/game.nes' --fps 40
build/linux/app/MushroomRecompiled --rom '/path/game.nes' --headless --seconds 10 --fps 30 --present-delay-ms 25 --perf
```

The optional log reports simulation/presentation rates, total ticks, discarded
catch-up ticks, audio queue size and pause state once per second. The launcher
keeps the current log and five previous launches under
`${XDG_STATE_HOME:-$HOME/.local/state}/mushroomrecompiled/`. Logging is opt-in.
`--headless` uses SDL dummy drivers if no driver was explicitly selected; it is
a timing smoke test and cannot validate physical controls or display output.
`--present-delay-ms` injects a blocking presentation delay for diagnostics.

## Validation scope

Portable tests exercise 30/40/60 FPS presentation schedules, bounded recovery
after a two-second stall, and Unicode wide paths under the C locale. The Linux
game verifiers compare recompiled/interpreted execution, deterministic saves,
44.1/48/96 kHz audio generation and the existing widescreen scene regressions.

Real Steam Deck gameplay, physical controller mapping, SteamOS pacing and
complete-game testing require a device. CI does not perform these checks.
See [the Linux validation record](docs/LINUX_VALIDATION.md) for measured local
results and outstanding hardware validation.

SDL references: [GameController](https://wiki.libsdl.org/SDL2/CategoryGameController),
[performance counter](https://wiki.libsdl.org/SDL2/SDL_GetPerformanceCounter),
[queued audio](https://wiki.libsdl.org/SDL2/SDL_QueueAudio).
