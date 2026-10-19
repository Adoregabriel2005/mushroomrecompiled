/* nes_fds_hle.h - the Famicom Disk System HLE tier's decisions, shared by the
 * compiler (which checks game.toml [fds] hle) and the cycle runtime.
 *
 * LLE is the baseline: the real BIOS runs recompiled against the modelled RAM
 * Adapter and drive, and that machine is the reference the oracle checks. The
 * HLE tier is opt-in conveniences on top of it, modelled on psxrecomp's
 * psx_bios_hle_plan() (runtime/include/bios_hle_plan.h): ONE pure function
 * decides every axis from what was asked for and what the loaded BIOS and disk
 * image can support, and every call site uses its answer. Off by default; with
 * every axis off the machine runs exactly as without the tier.
 *
 * Axes (independent: refusing one never changes the other):
 *
 *   auto_swap  when the program asks for a disk side that is not in the drive,
 *              eject and insert the side it asked for, with the drive lines a
 *              player's swap produces (runner/cyc/hw_fds_hle.c). Needs the
 *              BIOS's disk-ID check anchor (the entry of the routine every
 *              ID-checking BIOS call runs, and where the caller's disk ID
 *              pointer is): the request is the ID the program passes, matched
 *              against every side's disk header. A BIOS with no known anchor,
 *              a one-sided image, or a side without a readable header makes the
 *              axis structurally unavailable: asking for it is refused and
 *              reported, never forced.
 *
 *   fast_load  while the drive is loading, the host runs the machine as fast as
 *              it can instead of at 60 frames per second. The machine is the
 *              same LLE machine, frame for frame: only presentation and pacing
 *              change, so everything the program can see is what LLE produces.
 *              Needs only the drive; with an anchor a load also covers the
 *              BIOS's wait before it starts the motor.
 *
 * Requests come from five sources; a later one overrides an earlier one per
 * axis: game.toml [fds] hle (compiled into the program), the player's saved
 * settings (the windowed host's config.ini [FDS], written by its runtime menu),
 * the environment (NESRECOMP_FDS_HLE), the command line (--fds-hle) and a live
 * toggle (the runtime menu, or a dev build's keys). Each text source is a list
 * of words: off / none, on / all, auto-swap, fast-load, no-auto-swap,
 * no-fast-load, separated by commas, spaces or +.
 *
 * Hosts that list the axes (a menu row or a dev key per axis) walk
 * nes_fds_hle_axes() rather than naming the fields, so a new axis is one more
 * row there and every host picks it up.
 *
 * Header-only C11, no runtime state: the whole decision matrix is unit-tested
 * directly (runner/cyc/fds_hle_plan_test.c). */
#ifndef NES_FDS_HLE_H
#define NES_FDS_HLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One source's request: -1 says nothing about the axis, 0 off, 1 on. */
typedef struct {
    int8_t auto_swap, fast_load;
} NesFdsHleAsk;

#define NES_FDS_HLE_ASK_NONE ((NesFdsHleAsk){ -1, -1 })

static inline bool nes_fds_hle_word(const char *s, size_t n, const char *w)
{
    size_t i = 0;
    for (; i < n && w[i]; ++i) {
        char c = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
        if (c == '_') c = '-';
        if (c != w[i]) return false;
    }
    return i == n && !w[i];
}

/* Parse a request list into *ask (starting from "says nothing"). False on an
 * unknown word; *bad (if given) then points at it. NULL or "" says nothing. */
static inline bool nes_fds_hle_parse(const char *s, NesFdsHleAsk *ask, const char **bad)
{
    *ask = NES_FDS_HLE_ASK_NONE;
    if (bad) *bad = NULL;
    if (!s) return true;
    while (*s) {
        while (*s == ',' || *s == ' ' || *s == '+' || *s == '\t') ++s;
        size_t n = 0;
        while (s[n] && s[n] != ',' && s[n] != ' ' && s[n] != '+' && s[n] != '\t') ++n;
        if (!n) break;
        if (nes_fds_hle_word(s, n, "off") || nes_fds_hle_word(s, n, "none")) ask->auto_swap = ask->fast_load = 0;
        else if (nes_fds_hle_word(s, n, "on") || nes_fds_hle_word(s, n, "all")) ask->auto_swap = ask->fast_load = 1;
        else if (nes_fds_hle_word(s, n, "auto-swap")) ask->auto_swap = 1;
        else if (nes_fds_hle_word(s, n, "no-auto-swap")) ask->auto_swap = 0;
        else if (nes_fds_hle_word(s, n, "fast-load")) ask->fast_load = 1;
        else if (nes_fds_hle_word(s, n, "no-fast-load")) ask->fast_load = 0;
        else {
            if (bad) *bad = s;
            return false;
        }
        s += n;
    }
    return true;
}

/* ---- per-BIOS capability facts ----
 *
 * The disk-ID check anchor of a BIOS image: the entry of the routine that
 * compares a disk's header with the ID the caller asked for, and the zero-page
 * address holding the pointer to that 10-byte ID when it runs. For the RAM
 * Adapter BIOS (disksys.rom, CRC32 5E607DCF) that is $E445 (JSR $E6E3: start the
 * drive and read block 1's "*NINTENDO-HVC*", then compare its next 10 bytes with
 * ($00),Y, $FF matching anything; $E451-$E461). Its five callers are every
 * ID-checking call: LoadFiles ($E1F8 -> $E21A), AppendFile / WriteFile
 * ($E237/$E239 -> $E26B, $E290, $E2AB), CheckFileCount / AdjustFileCount
 * ($E2B7/$E2BB -> $E2F7) and SetFileCount ($E301/$E305 -> $E2AB), each after
 * $E3E7/$E3EA stored the ID pointer from the caller's inline arguments at
 * $00/$01. GetDiskInfo ($E32A -> $E346) reads the header without an ID and so
 * never passes it. It is the address Mesen's own auto-insert watches (FDS.cpp
 * ReadRAM). check[] is the code expected there, verified at load.
 * Another BIOS declares its anchor in its identity file (<bios>.toml:
 * hle_id_check / hle_id_pointer), as the synthetic test BIOSes do. */
typedef struct {
    uint16_t id_check;          /* 0: no anchor */
    uint8_t  id_pointer;
    uint8_t  check_len;
    uint8_t  check[3];
} NesFdsHleAnchor;

static inline NesFdsHleAnchor nes_fds_hle_builtin_anchor(uint32_t bios_crc32)
{
    NesFdsHleAnchor a = { 0, 0, 0, { 0, 0, 0 } };
    if (bios_crc32 == 0x5E607DCFu) {
        a.id_check = 0xE445;
        a.id_pointer = 0x00;
        a.check_len = 3;
        a.check[0] = 0x20; a.check[1] = 0xE3; a.check[2] = 0xE6;   /* JSR $E6E3 */
    }
    return a;
}

/* ---- the disk ID a program asks for ----
 *
 * A disk header (block 1, 56 bytes: code $01, "*NINTENDO-HVC*", then maker,
 * game name x3, game type, revision, side, disk number, disk type, and one more
 * byte) matches a requested ID when each of the 10 ID bytes is $FF or equals
 * header byte 15 + i: the BIOS's comparison ($E44E-$E471). */
#define NES_FDS_HLE_ID_BYTES 10u

static inline bool nes_fds_hle_id_matches(const uint8_t id[NES_FDS_HLE_ID_BYTES], const uint8_t block1[56])
{
    for (unsigned i = 0; i < NES_FDS_HLE_ID_BYTES; ++i)
        if (id[i] != 0xFF && id[i] != block1[15 + i]) return false;
    return true;
}

/* Block 1 as the drive would read it from a side's stream: the first block
 * after the lead-in (zeros, then the $80 mark). False if there is none. */
static inline bool nes_fds_hle_block1(const uint8_t *stream, uint32_t len, uint8_t out[56])
{
    uint32_t p = 0;
    while (p < len && stream[p] == 0) ++p;
    if (p >= len || stream[p] != 0x80 || p + 1 + 56 > len || stream[p + 1] != 0x01) return false;
    memcpy(out, stream + p + 1, 56);
    return true;
}

/* ---- the plan ---- */
typedef struct {
    /* game.toml, saved settings, NESRECOMP_FDS_HLE, --fds-hle, toggle. A source
     * that says nothing must be NES_FDS_HLE_ASK_NONE: a zeroed ask asks for off. */
    NesFdsHleAsk config, user, env, cli, live;
    bool     is_fds;                       /* the program runs the RAM Adapter */
    bool     have_anchor;                  /* the BIOS's disk-ID check anchor is known and verified */
    unsigned sides;                        /* sides in the image */
    unsigned sides_with_id;                /* sides whose disk header (block 1) reads */
} NesFdsHleRequest;

typedef struct {
    bool auto_swap, fast_load;
    /* Asked for and refused, and why: a silent downgrade is how psxrecomp's
     * boot-skip bug hid; hosts print these. */
    bool auto_swap_denied, fast_load_denied;
    const char *auto_swap_why, *fast_load_why;
    /* Which source decided each axis: "default", "game.toml", "settings", "env",
     * "cli", "toggle". */
    const char *auto_swap_from, *fast_load_from;
    /* The always-on disk-ID request observation (ring events) runs: it needs
     * only the anchor, whatever was asked for. */
    bool observe;
} NesFdsHlePlan;

static inline int8_t nes_fds_hle_pick(int8_t config, int8_t user, int8_t env, int8_t cli, int8_t live,
                                      const char **from)
{
    *from = "default";
    int8_t v = 0;
    if (config >= 0) { v = config; *from = "game.toml"; }
    if (user >= 0)   { v = user;   *from = "settings"; }
    if (env >= 0)    { v = env;    *from = "env"; }
    if (cli >= 0)    { v = cli;    *from = "cli"; }
    if (live >= 0)   { v = live;   *from = "toggle"; }
    return v;
}

/* Pure: the same request always gives the same plan. */
static inline NesFdsHlePlan nes_fds_hle_plan(NesFdsHleRequest r)
{
    NesFdsHlePlan p;
    memset(&p, 0, sizeof(p));
    int8_t want_swap = nes_fds_hle_pick(r.config.auto_swap, r.user.auto_swap, r.env.auto_swap, r.cli.auto_swap,
                                        r.live.auto_swap, &p.auto_swap_from);
    int8_t want_fast = nes_fds_hle_pick(r.config.fast_load, r.user.fast_load, r.env.fast_load, r.cli.fast_load,
                                        r.live.fast_load, &p.fast_load_from);
    p.observe = r.is_fds && r.have_anchor;

    /* Axis 1: auto swap. Structurally unavailable unless the request can be
     * read (anchor) and answered (two or more sides, each with a header). */
    if (want_swap > 0) {
        if (!r.is_fds) p.auto_swap_why = "not a Famicom Disk System program";
        else if (!r.have_anchor) p.auto_swap_why = "no disk-ID check anchor is known for this BIOS";
        else if (r.sides < 2) p.auto_swap_why = "the image has one side";
        else if (r.sides_with_id < r.sides) p.auto_swap_why = "a side has no readable disk header";
        if (p.auto_swap_why) p.auto_swap_denied = true;
        else p.auto_swap = true;
    }

    /* Axis 2: fast load. Decided from its own request only, never from axis 1's
     * outcome: it needs nothing but the drive. */
    if (want_fast > 0) {
        if (!r.is_fds) {
            p.fast_load_why = "not a Famicom Disk System program";
            p.fast_load_denied = true;
        } else {
            p.fast_load = true;
        }
    }
    return p;
}

/* ---- the axes, for hosts that list them ----
 *
 * One row per axis: its word in a request list, the player-facing name and
 * help, and where its request and its outcome live. A host menu shows a row
 * per axis (on/off, refused with the reason), a dev build maps a key to each,
 * config.ini stores each under `key`; none of them name the fields. */
typedef struct {
    const char *word;              /* the request word: "auto-swap" */
    const char *key;               /* settings key: "AutoSwap" */
    const char *label;             /* "Auto disk swap" */
    const char *help;              /* one line for a menu */
    size_t ask;                    /* offsetof(NesFdsHleAsk, <axis>) */
    size_t on, denied, why, from;  /* offsetof(NesFdsHlePlan, ...) */
} NesFdsHleAxis;

static inline const NesFdsHleAxis *nes_fds_hle_axes(unsigned *count)
{
    static const NesFdsHleAxis axes[] = {
        { "auto-swap", "AutoSwap", "Auto disk swap",
          "Put in the side the game asks for.",
          offsetof(NesFdsHleAsk, auto_swap), offsetof(NesFdsHlePlan, auto_swap),
          offsetof(NesFdsHlePlan, auto_swap_denied), offsetof(NesFdsHlePlan, auto_swap_why),
          offsetof(NesFdsHlePlan, auto_swap_from) },
        { "fast-load", "FastLoad", "Fast disk loading",
          "Run disk loads at full speed.",
          offsetof(NesFdsHleAsk, fast_load), offsetof(NesFdsHlePlan, fast_load),
          offsetof(NesFdsHlePlan, fast_load_denied), offsetof(NesFdsHlePlan, fast_load_why),
          offsetof(NesFdsHlePlan, fast_load_from) },
    };
    if (count) *count = (unsigned)(sizeof(axes) / sizeof(axes[0]));
    return axes;
}

static inline int8_t *nes_fds_hle_ask_axis(NesFdsHleAsk *a, const NesFdsHleAxis *axis)
{
    return (int8_t *)((char *)a + axis->ask);
}
static inline bool nes_fds_hle_plan_on(const NesFdsHlePlan *p, const NesFdsHleAxis *axis)
{
    return *(const bool *)((const char *)p + axis->on);
}
static inline bool nes_fds_hle_plan_denied(const NesFdsHlePlan *p, const NesFdsHleAxis *axis)
{
    return *(const bool *)((const char *)p + axis->denied);
}
static inline const char *nes_fds_hle_plan_why(const NesFdsHlePlan *p, const NesFdsHleAxis *axis)
{
    return *(const char *const *)((const char *)p + axis->why);
}
static inline const char *nes_fds_hle_plan_from(const NesFdsHlePlan *p, const NesFdsHleAxis *axis)
{
    return *(const char *const *)((const char *)p + axis->from);
}

#endif /* NES_FDS_HLE_H */
