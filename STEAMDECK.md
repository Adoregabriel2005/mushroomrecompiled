# Mushroom Recompiled — Steam Deck / Linux experiment

This is a **personal, unofficial, native Linux SDL2 frontend** for the NESRecomp
core of Mushroom Recompiled. It is not affiliated with Nintendo, and does not
provide the ROM or any Nintendo copyrighted content.

## Current status

The Linux frontend is newly introduced. It is **not yet verified on Steam Deck
and has not been validated with the full game**. The Windows interface remains
unchanged. This initial Linux target deliberately implements a compact
game-first UI instead of porting the entire Win32 settings/launcher UI.

The expected ROM is the **USA PRG1** iNES dump with SHA-256:
`4377a7f5e6eb50bdd2ac6f249bf1a7085500aca8eb41f38545c3a2731c51a579`.
No ROM bytes are committed or shipped.

## Build on Debian (developer)

```bash
sudo apt install build-essential cmake ninja-build libsdl2-dev
bash tools/build-linux.sh --check
bash tools/build-linux.sh '/path/to/your/SMB3-USA-PRG1.nes'
VERIFY=1 bash tools/build-linux.sh '/path/to/your/SMB3-USA-PRG1.nes'
```

`--check` checks the Linux sources without a ROM; the actual executable
**cannot be linked without game-specific generated C** from the verified ROM.

## Playing locally

```bash
./build/linux/MushroomRecompiled --rom '/path/to/your/SMB3-USA-PRG1.nes' --windowed
```

For deployment, stage `MushroomRecompiled`, `run-steamdeck.sh`,
`assets/gamecontrollerdb.txt`, and your own **game.nes** in a directory on
the Deck. Mark both the executable and launcher executable; add
`run-steamdeck.sh` as a **non-Steam game** through Steam in Desktop Mode,
then launch from Gaming Mode.

**Important:** a binary linked against Debian 13's newer glibc is not
guaranteed to run on SteamOS. Build on Steam Deck, or package using a
compatible older runtime/container/AppImage toolchain before transferring.
The present script does not create an AppImage.

## Controls

| Action | Keyboard | Steam Deck |
|---|---|---|
| Direction | Arrow keys / WASD | D-pad or left stick |
| Jump (NES A) | X / Space | A or Y |
| Run / attack (NES B) | Z / C | B or X |
| NES Start | Return | Menu/Start |
| NES Select | Shift | View/Select |
| PC pause | Escape | Select + Start |
| Save/load slot 1 | F5/F9 | Select + L1 / Select + R1 |
| 4:3/16:9 | F6 | Keyboard shortcut |
| Fullscreen | F11 | Starts fullscreen |

The interface preserves the NES game frame cadence (approximately 60.1 Hz).
The wide compositor is optional (`--native` starts in 4:3) and may alter
enemy activation relative to original game in supported widescreen stages.
The 4:3 mode remains the fidelity reference.

Save state location: SDL's user-specific preference directory
(`~/.local/share/Personal/SMB3SteamDeck/` on a typical Linux installation).
Back up state files before updating the recompilation runtime.

Known gaps: no in-game graphic settings menu, no integrated file picker,
no audio/video latency profiling on Deck, only one quick-save slot in the
Linux interface, and no Steam Deck hardware validation yet.
