#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ ! -x ./MushroomRecompiled ]]; then
    echo "MushroomRecompiled binary absent: install the native build here." >&2
    exit 1
fi
if [[ -f game.nes ]]; then
    exec ./MushroomRecompiled --rom ./game.nes "$@"
fi
exec ./MushroomRecompiled "$@"
