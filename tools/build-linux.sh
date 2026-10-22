#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build/linux"
rom= check=0 verify=0
while (($#)); do
    case "$1" in
        --check) check=1; shift ;;
        --verify) verify=1; shift ;;
        --rom) rom=${2:?--rom requires a local filename}; shift 2 ;;
        --build-dir) build=${2:?--build-dir requires a directory}; shift 2 ;;
        --help) echo 'Usage: bash tools/build-linux.sh [--check] [--rom game.nes] [--verify] [--build-dir DIR]'; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
for tool in cmake cc pkg-config sha256sum; do
    command -v "$tool" >/dev/null || { echo "Missing $tool (install build-essential cmake pkg-config libsdl2-dev)" >&2; exit 1; }
done
pkg-config --atleast-version=2.0.10 sdl2 || { echo 'SDL2 development files >= 2.0.10 required' >&2; exit 1; }
mkdir -p -- "$build"
build=$(cd -- "$build" && pwd)
generator=()
if command -v ninja >/dev/null; then generator=(-G Ninja); fi
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
cmake -S "$root/tools/nesrecomp-src/recompiler" -B "$build/recompiler" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build/recompiler" --parallel "$jobs"
if ((check)); then
    cmake -S "$root" -B "$build/check" "${generator[@]}" -DMUSHROOM_ROM_FREE=ON -DCMAKE_BUILD_TYPE=Release
    cmake --build "$build/check" --parallel "$jobs"
    ctest --test-dir "$build/check" --output-on-failure
    echo 'ROM-free compiler, SDL host/core compilation, timing and path checks passed.'
    if [[ -z "$rom" ]]; then exit 0; fi
fi
[[ -n "$rom" && -f "$rom" ]] || { echo 'Supply your own compatible local ROM using --rom /path/game.nes (or use --check without a ROM).' >&2; exit 2; }
rom=$(realpath -- "$rom")
expected=4377a7f5e6eb50bdd2ac6f249bf1a7085500aca8eb41f38545c3a2731c51a579
actual=$(sha256sum -- "$rom"); actual=${actual%% *}
[[ "$actual" == "$expected" ]] || { echo "Expected canonical USA PRG1 SHA-256 $expected; received $actual. Input was not modified." >&2; exit 1; }
cd -- "$root"
"$build/recompiler/NESRecomp" "$rom" --game "$root/game.toml" --cycle-seed-file "$root/docs/smb3-cycle-seeds.txt"
cmake -S "$root" -B "$build/app" "${generator[@]}" -DMUSHROOM_ROM_FREE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build "$build/app" --parallel "$jobs"
ctest --test-dir "$build/app" --output-on-failure
if ((verify)); then
    cd -- "$build/app"
    ./smb3-verify "$rom"
    ./smb3-wide-verify "$rom"
    ./smb3-scenes-verify "$rom" 4
    ./smb3-scenes-verify "$rom" 6
fi
echo "Built $build/app/MushroomRecompiled"
