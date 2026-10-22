# Interface and controls

The program is a single binary: double-clicking it starts the game directly. Without `--rom` and without a remembered ROM, a single `.nes` file next to the app, in the current directory, or in `roms/` opens directly. With zero or several candidates, a picker opens inside the window (arrow keys/Enter, gamepad, or drag a file onto the window). The ROM in use is validated and remembered by absolute path in `config.ini` for later launches.

- Keyboard: arrow keys, X = A, Z = B, Shift = Select, Enter = Start. Everything is remappable through `key_*` in `config.ini`.
- PC menu (Esc or the Guide button): RESUME, SAVE STATE, LOAD STATE, FULLSCREEN, QUIT. Navigate with the arrow keys/WASD or the D-pad; Enter/Space/A/Start activates, B closes.
- Shortcuts: F5 saves, F7 loads, 1–5 select the save-state slot. F11 toggles fullscreen, F8 mutes, -/= adjust volume. G writes the session to `saves/replay-N.rec`. R is the console Reset (SRAM is kept).
- Gamepad: opened automatically with hotplug support. D-pad and left stick (dead zone ~12000); A/Y = NES A, B/X = NES B, Back = Select, Start = Start. Guide opens the menu.
- Video: `--scale 1-8`, `--scanlines`, `--brightness 50-150`, fullscreen. The frame is always the 16:9 canvas of 426 × 240, and the window is an integer multiple of it, so every image pixel has the same width. In fullscreen the game uses the largest 16:9 mode whose height is a multiple of 240 — 1280 × 720 on a 1080p panel — and the compositor scales that surface to fill the screen. 16:9 widens the horizontal scenery with the original HUD centred; vertical rooms and scenes return to the native frame.
- Audio: volume, mute, `--audio-rate 44100|48000|96000`. The NES output is mono on both channels.
- Persistence: `config.ini` stores the ROM, video, audio and key settings. Progress is kept in `saves/smb3.srm` (window $6000–$7FFF); save states are in `saves/state-1..5.sst`.
- Command line: `--record/--replay` (one control byte per frame), `--frames N` and `--screenshot` for testing, `--menu` starts paused, `--slot 1-5`, `--no-vsync`. `--help` lists everything.

See [WIDESCREEN.md](WIDESCREEN.md) for the 16:9 composition and 1-1 enemy activation. See [PORTABLE.md](PORTABLE.md) for build guarantees and limitations.
