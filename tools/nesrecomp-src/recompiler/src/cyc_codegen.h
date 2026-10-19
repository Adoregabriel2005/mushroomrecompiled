/*
 * cyc_codegen.h - cycle-accurate code generation (NESRecomp --cycle-accurate)
 *
 * Emits generated/<prefix>_cyc.c for runner/cyc: every reachable ROM
 * instruction becomes C that performs the instruction's individual CPU cycles
 * (bus reads and writes, interrupt polls, the instruction's last cycle)
 * through runner/cyc/cpu6502.h, with PC, opcode and operands folded in and
 * static control flow compiled to gotos. The same instruction templates also
 * produce runner/cyc/cpu6502_interp.c. See runner/cyc/README.md.
 */
#pragma once
#include <stdbool.h>

#include "game_config.h"
#include "rom_parser.h"

/* A Famicom Disk System program: rom is the RAM Adapter (mapper 20) whose PRG
 * ROM is the BIOS, which is all that is compiled; code the BIOS loads from
 * disk into PRG RAM runs on the interpreter. The generated program records
 * the BIOS identity and game.toml's media as the host's defaults. */
typedef struct {
    uint32_t bios_crc32;
    char     bios_path[1024];    /* absolute */
    char     image_path[1024];   /* absolute, or empty */
    char     hle[128];           /* game.toml [fds] hle, checked; empty = none */
} CycFdsProgram;

bool cyc_codegen_emit(const NESRom *rom, const GameConfig *cfg, const char *output_prefix,
                      const CycFdsProgram *fds);
/* Write the cycle-accurate interpreter (runner/cyc/cpu6502_interp.c). */
bool cyc_codegen_emit_interpreter(const char *path);
