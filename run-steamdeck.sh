#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
default_binary="$root/build/linux/app/MushroomRecompiled"
if [[ -x "$root/MushroomRecompiled" ]]; then default_binary="$root/MushroomRecompiled"; fi
binary=${MUSHROOM_BINARY:-"$default_binary"}
[[ -x "$binary" ]] || { echo 'Build first: bash tools/build-linux.sh --rom /path/game.nes' >&2; exit 1; }
args=(--fullscreen "$@")
if [[ ${MUSHROOM_PERF_LOG:-0} == 1 ]]; then
    logs=${XDG_STATE_HOME:-"$HOME/.local/state"}/mushroomrecompiled
    mkdir -p -- "$logs"
    # Five previous launch logs plus the current log. No rotation without opt-in.
    for ((i=4;i>=1;i--)); do
        [[ ! -f "$logs/perf.log.$i" ]] || mv -f -- "$logs/perf.log.$i" "$logs/perf.log.$((i+1))"
    done
    [[ ! -f "$logs/perf.log" ]] || mv -f -- "$logs/perf.log" "$logs/perf.log.1"
    "$binary" --perf "${args[@]}" 2>&1 | tee "$logs/perf.log"
else
    exec "$binary" "${args[@]}"
fi
