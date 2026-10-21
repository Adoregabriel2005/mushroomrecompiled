#!/usr/bin/env bash
# Build the recompiler and, with an owner-supplied USA PRG1 ROM, the native game.
set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
if [[ "${1:-}" == "--check" ]]; then
    echo "Checking the Linux SDL2 frontend and POSIX glue (without game data)..."
    cc -std=c11 -Wall -Wextra -Wpedantic -fsyntax-only \
        -Iruntime -Isrc -Itools/nesrecomp-src/runner/cyc \
        -Itools/nesrecomp-src/runner/include \
        $(sdl2-config --cflags) src/app_sdl.c runtime/glue.c \
        src/widescreen.c src/wide_enemies.c
    echo "Linux frontend syntax check: OK"
    exit 0
fi
rom="${1:-}"
if [[ -z "$rom" || ! -f "$rom" ]]; then
    echo "Usage: bash tools/build-linux.sh /absolute/path/to/SMB3-USA-PRG1.nes" >&2
    echo "Check frontend only: bash tools/build-linux.sh --check" >&2
    echo "The game ROM is never distributed or downloaded by this script." >&2
    exit 2
fi
rom="$(realpath "$rom")"
hash="$(sha256sum "$rom" | cut -d ' ' -f 1)"
expected="4377a7f5e6eb50bdd2ac6f249bf1a7085500aca8eb41f38545c3a2731c51a579"
if [[ "$hash" != "$expected" ]]; then
    echo "Incompatible ROM revision (SHA-256 $hash); expected SMB3 USA PRG1." >&2
    exit 2
fi
cmake -S tools/nesrecomp-src/recompiler -B build/linux-recompiler \
    -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux-recompiler -j 8
build/linux-recompiler/NESRecomp "$rom" --game game.toml \
    --cycle-seed-file docs/smb3-cycle-seeds.txt
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux -j 8
echo
echo "Native executable: $root/build/linux/MushroomRecompiled"
echo "Test: $root/build/linux/MushroomRecompiled --rom '$rom' --windowed"
if [[ "${VERIFY:-0}" == "1" ]]; then
    (cd build/linux && ./smb3-verify "$rom")
fi
