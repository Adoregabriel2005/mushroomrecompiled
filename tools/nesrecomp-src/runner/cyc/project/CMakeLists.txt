cmake_minimum_required(VERSION 3.20)
project(NESRecompCycleGame C)
set(NESRECOMP_ROM "" CACHE FILEPATH "Existing game ROM (required)")
set(NESRECOMP_GAME_CONFIG "" CACHE FILEPATH "Existing game.toml (optional)")
set(NESRECOMP_CYCLE_SEEDS "" CACHE FILEPATH "Cycle seed file override (optional)")
set(NESRECOMP_CYCLE_CAPTURES "" CACHE FILEPATH "Cycle RAM capture file override (optional)")
set(NESRECOMP_HOST_COMPILER "" CACHE FILEPATH "Prebuilt host NESRecomp (optional)")
set(NESRECOMP_FDS_BIOS "" CACHE FILEPATH "FDS BIOS (optional; default game.toml [fds] bios)")
set(NESRECOMP_BOXART "" CACHE FILEPATH "Launcher box art, 24-bit .tga (optional)")
option(NESRECOMP_HEADLESS "Build without SDL window/audio support" OFF)
# NESRECOMP_RECOMP_UI=<recomp-ui checkout> adds the launcher and in-game menu;
# NESRECOMP_DEV_UI=ON the developer surface (runner/cyc/cyc.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/../project.cmake")
set(options "")
if(NESRECOMP_HEADLESS)
    list(APPEND options HEADLESS)
endif()
if(NESRECOMP_BOXART)
    list(APPEND options BOXART "${NESRECOMP_BOXART}")
endif()
nesrecomp_add_cycle_game(nes_game ROM "${NESRECOMP_ROM}"
    GAME_CONFIG "${NESRECOMP_GAME_CONFIG}" SEED_FILE "${NESRECOMP_CYCLE_SEEDS}"
    CAPTURE_FILE "${NESRECOMP_CYCLE_CAPTURES}"
    RECOMPILER "${NESRECOMP_HOST_COMPILER}" BIOS "${NESRECOMP_FDS_BIOS}" ${options})
