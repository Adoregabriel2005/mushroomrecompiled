# Linux frontend validation — 2026-10-22

Measured locally in Ubuntu 26.04 x86-64 under WSL, GCC 15.2, CMake 4.2.3,
SDL2 2.32.10, Unix Makefiles. This is not a physical Steam Deck or a Debian
installation. Ninja is supported by the script when installed, but was not
available in this environment. No ROM or generated game code is published.

| Check | Observed result |
|---|---|
| `bash tools/build-linux.sh --check` | Passed: NESRecomp and ROM-free host/core compilation, timing and path tests |
| Full Linux build from local compatible ROM | Passed |
| `smb3-verify` | 3,000 native/interpreter frame hashes identical; deterministic save/load; audio at 44.1/48/96 kHz |
| `smb3-wide-verify` | 1,800 native/interpreter hashes identical; expanded enemies, native HUD and margins verified |
| `smb3-scenes-verify`, stages 4 and 6 | Zero background mismatches across 370,944 sampled pixels; reward/bonus/raster/exit scene margins passed |
| Headless, 30 FPS cap + 25 ms presentation delay | 304 simulation ticks after 5.068 seconds; zero dropped catch-up ticks |
| Headless, 40 FPS cap + 25 ms presentation delay | 304 simulation ticks after 5.070 seconds; zero dropped catch-up ticks |
| Headless, 40 FPS cap + 200 ms delay | Recovery bounded to eight ticks per pump; discarded ticks reported (63 after 2.383 seconds) |
| Windows cross-check | Win32 application built; 3,000 native/interpreter hashes, audio and save/load tests passed |
| Windows UI regression | Seven pages, resize, apply/discard, virtual gamepad/hotplug, remembered ROM, Esc/settings/resume passed |

The timing smoke tests use SDL's software renderer and dummy audio/video.
Actual presentation throughput was about 25 FPS with the injected moderate
delay, while simulation remained near 60 Hz. They test decoupling, not GPU
performance or physical audio quality. The independent host clock test also
checks exact 30/40/60 FPS schedules over ten simulated seconds and recovery
after a two-second stall.

Unicode path tests read/write a filename containing accented, CJK and non-BMP
characters through both wide and UTF-8 APIs under the C locale, on both hosts.

GCC emits pre-existing `-Wstringop-overflow` diagnostics in the vendored
`emu2413.c` and `cyc_ramview.c`; neither prevented the build. This patch does
not alter those vendor modules.

Still requiring hardware validation: SteamOS library compatibility, Steam Input
controller enumeration, physical axes/D-pad/A/B/X/Y/Start/Back, USB/Bluetooth
hotplug, fullscreen output, audible playback, normal display pacing and
whole-game correctness. No physical Steam Deck success is claimed here.
