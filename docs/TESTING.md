# Testing the v1.5.0 release

1. Build from source (see README.md) using your own ROM. There are no release binaries to download: everyone builds from source.
2. Start the freshly built `mushroom-sdl` (Linux) or `MushroomRecompiled.exe` (Windows) with `--rom "Game.nes"`, or without arguments if a single `.nes` sits next to the program. The window title should show Mushroom Recompiled - Vulkan.
3. Use a compatible ROM (USA Rev 1, goodNES or No-Intro dump), in `.nes` format. An incompatible ROM must be rejected with a message and must not start the game.
4. Confirm boot, the start of a game, the map, and entry into a level. Close and reopen without arguments: it should start with the remembered ROM. If the ROM is moved or deleted, the picker should appear.
5. Test Z/X/Enter/Shift and a remapped key (edit `key_*` in `config.ini`). For a gamepad, pair it and confirm D-pad, A/B and Start.
6. Check that audio is continuous. If there are dropouts, try `--audio-rate 44100`.
7. Esc opens the PC menu; Start pauses the NES. Test save, load and resume (F5/F7).
8. Change a setting such as `--scale` or `--scanlines`, close, and reopen to confirm it persisted in `config.ini`.

## Reporting problems

Open an issue and include the version, operating system, CPU/GPU, audio device, controller model/connection, steps to reproduce, and a screenshot when useful. Include the ROM region/revision and hashes without attaching the ROM.

Do not attach ROMs, saves or personal configuration files. There is no automatic telemetry or diagnostic upload.

## Limitations

This is an experimental build. The full game, all video modes and all controller models have not been certified. One instruction at a bank boundary on the known route falls back to the interpreter. Translation tests share the hardware model, so they are not an independent reference for a physical console.

For the v1.3 widescreen mode, compare 4:3 and 16:9 in level 1-1: confirm the extra scenery, pixels without stretching, and the same HUD between the black bars. Vertical rooms and map/title screens keep the native frame. See [WIDESCREEN.md](WIDESCREEN.md).

## Checking version and video

Build from source and run the executable from the build folder. The frame is always the 16:9 canvas with the enlarged level and original HUD. There is no separate 16:9 toggle in this build. A "16:9 stretched" mode belongs to earlier versions.

On supported horizontal levels, the scenery fills the wider frame and the HUD stays centred with black bars on the sides. Resize the window and confirm the image stays at whole-pixel scale. Logos and launcher art must not appear over the play area.

## Enemies in the wide area — v1.4

In 16:9 level 1-1, check the enemy on the first pipe in the area to the right before the pipe enters the original 256 pixels. Move forward and back: enemies should stay in the side band and not vanish at the old edge. Check collisions, defeat by jumping, shells, pause, save/load and return to the map. Repeat in 4:3 to compare the original triggers. In full levels the original limit of five main objects can still restrict new activations. Report the level, enemy type and camera position if something fails.

## Scenes and platforms — v1.5

At the end of a level, check the prize screen: no column from the previous map should appear at the edges. Bonus and card screens use the centred native frame. In tall levels, move past the old edges and check blocks, platforms and their outlines. The automated test compares the real PPU background with the enlarged background in two levels, and simulates the prize, bonus and exit indicators.
