#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ ! -x ./MushroomRecompiled ]]; then
    echo "MushroomRecompiled binary absent: install the native build here." >&2
    exit 1
fi
# Lightweight diagnostics for occasional frame pacing problems. Keep at
# most two small logs and do not change SteamOS graphics or power settings.
if [[ "${SMB3_PERF_LOG:-1}" != "0" ]]; then
    export SMB3_PERF_LOG=1
    state_dir="${XDG_STATE_HOME:-$HOME/.local/state}/smb3-recompiled"
    if mkdir -p "$state_dir" 2>/dev/null; then
        logfile="$state_dir/perf.log"
        if [[ -f "$logfile" ]] && (( $(stat -c %s "$logfile") > 1048576 )); then
            mv -f "$logfile" "$state_dir/perf.log.1"
        fi
        exec >> "$logfile" 2>&1
    fi
fi
if [[ -f game.nes ]]; then
    exec ./MushroomRecompiled --rom ./game.nes "$@"
fi
exec ./MushroomRecompiled "$@"
