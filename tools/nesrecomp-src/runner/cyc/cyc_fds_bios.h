/*
 * cyc_fds_bios.h - finding and checking the Famicom Disk System BIOS
 * (disksys.rom), shared by the host (cyc_host.c) and recomp-ui's launcher
 * (cyc_ui_launcher.c), so the launcher shows exactly what a start would use.
 *
 * The lookup, first file that exists wins:
 *   --fds-bios FILE                 (when given, the only candidate)
 *   config.ini [FDS] Bios           (the window's; the launcher's pick)
 *   game.toml [fds] bios            (headless runs only: a developer's build path)
 * No file name is searched for: a BIOS is whatever file the player picked,
 * and its identity must match: 8192 bytes and the CRC32 the compiled program
 * was built against, else the one <bios>.toml records (bios/disksys.toml:
 * size, crc32), else the known disksys.rom (common/nes_fds.h, 5E607DCF).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *explicit_path;   /* --fds-bios; NULL: none */
    const char *saved_path;      /* config.ini [FDS] Bios; NULL or "": none */
    const char *compiled_path;   /* game.toml [fds] bios, headless runs only; NULL: none */
    uint32_t    compiled_crc;    /* the compiled program's BIOS CRC32; 0: <bios>.toml, else the known one */
} CycFdsBiosLookup;

typedef enum {
    CYC_FDS_BIOS_OK,             /* found, and the identity matches */
    CYC_FDS_BIOS_MISSING,        /* no candidate exists */
    CYC_FDS_BIOS_WRONG,          /* the file found is not the expected BIOS */
} CycFdsBiosStatus;

typedef struct {
    CycFdsBiosStatus status;
    char        path[1024];      /* the file found (OK, WRONG) */
    const char *source;          /* which candidate: "--fds-bios", "config.ini", ... */
    size_t      size;            /* its size (OK, WRONG) */
    uint32_t    crc, want_crc, want_size;
    uint8_t    *data;            /* OK with want_data: the BIOS (caller frees) */
    char        detail[160];     /* one line for the launcher and the log */
} CycFdsBiosResult;

/* The expected identity for a BIOS file: compiled_crc, else <path>.toml's,
 * else the known disksys.rom. *size gets the expected size. */
uint32_t cyc_fds_bios_expected(const char *path, uint32_t compiled_crc, uint32_t *size);
/* Check one file (a player's pick). OK / MISSING (cannot be read) / WRONG. */
CycFdsBiosStatus cyc_fds_bios_check(const char *path, uint32_t compiled_crc, bool want_data, CycFdsBiosResult *r);
/* Run the lookup. */
CycFdsBiosStatus cyc_fds_bios_locate(const CycFdsBiosLookup *in, bool want_data, CycFdsBiosResult *r);
/* Whether the file is a Famicom Disk System image (.fds, .qd; not a cartridge). */
bool cyc_fds_image_file(const char *path);

#ifdef __cplusplus
}
#endif
