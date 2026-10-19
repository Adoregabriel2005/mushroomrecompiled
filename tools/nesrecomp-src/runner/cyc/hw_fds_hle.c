/*
 * hw_fds_hle.c - the Famicom Disk System HLE tier, on top of the LLE drive
 * (hw_fds.c) and the recompiled BIOS.
 *
 * What the tier may do is decided once, by nes_fds_hle_plan()
 * (common/nes_fds_hle.h), from what was asked for and what the BIOS and image
 * support; hosts pass the answer to cyc_fds_hle_configure(). This file never
 * second-guesses it. Two parts:
 *
 * Observation (always on, whatever the plan; the machine is untouched):
 *   - disk-ID requests: when the BIOS's disk-ID check runs (its anchor, see
 *     nes_fds_hle.h), the 10-byte ID the program asked for and which sides'
 *     headers it matches -> ring fds.idreq + fds.idbytes. The anchor is seen
 *     on the address bus, as the RAM Adapter (which holds the BIOS ROM) sees
 *     it: a read of the anchor followed on the next CPU cycle by a read of the
 *     byte after it is the opcode fetch and first operand fetch of the JSR
 *     there, in native code, the interpreter and cyc_interp alike (an
 *     interrupt taken at the anchor reads it twice and pushes, so it is not
 *     counted until the JSR really runs).
 *   - load spans: frames in which the program is loading from the disk: the
 *     drive clocked a byte with the transfer released ($4025.1 = 0: reading or
 *     writing, including the wait in a gap for the next block's mark), data
 *     moved through $4031/$4024, the head rewound or is spinning up, or the
 *     BIOS is between its disk-ID check and starting the drive -> ring
 *     fds.span, and the host's fast-load pacing (cyc_fds_hle_loading). A drive
 *     left turning after the BIOS is done (it sets $4025.1 and the head runs
 *     on to the end of the side) is not loading.
 *
 * Auto swap (plan axis; changes the machine, so its state is hashed):
 *   Request at the ID check. The requested ID matches exactly one side and it
 *     is not the one in the drive: eject at the end of the frame, hold the
 *     drive empty for SWAP_HOLD frames, insert that side. The BIOS is then in
 *     its ~40-frame wait before it starts the motor (measured on disksys.rom:
 *     the header check at Otocky f=717 reached the $4032 disk test at f=756),
 *     so the call finds the side it asked for and never fails. A request the
 *     drive already satisfies, one matching several sides (ambiguous) or none
 *     changes nothing; each is a ring event with its evidence.
 *   A program that waits for the player. Games that ask for a side on screen
 *     and watch $4032 for the disk to come out (Otocky, Esper Dream, Nazo no
 *     Murasame-jou) make no BIOS call until it has gone out and back in, so
 *     there is no ID to read yet. Once the drive has gone idle after a disk
 *     access and the program has then polled $4032 in WAIT_POLLS frames
 *     (gaps of up to WAIT_GAP frames allowed) with no disk access, the tier
 *     ejects, holds WAIT_HOLD frames and puts a disk back: first the same side
 *     (a disk bump, harmless if the program was not waiting), so the program's
 *     next ID check says which side it wants and the request path swaps to it.
 *     If the program instead keeps waiting with no ID check in between (a game
 *     that reads the header itself), the next round inserts the next side.
 *     This path is inferred from the program's behaviour, not read from a
 *     request: judgment call (Mesen's auto-insert, FDS.cpp, infers the same
 *     way: > 20 $4032 reads, then eject, 77 frames, insert disk 1 side A,
 *     corrected at $E445).
 *   A host eject or insert (player keys, scripted disk events) cancels
 *   whatever the tier was doing and leaves the drive to the host until the
 *   next disk access.
 */
#include "hw_fds.h"

#include "cyc_core.h"
#include "cyc_ring.h"
#include "hw_internal.h"
#include "../../common/nes_fds_hle.h"

#include <string.h>

enum {
    SWAP_HOLD      = 3,    /* frames the drive stays empty in a requested swap */
    WAIT_POLLS     = 20,   /* $4032-polling frames that count as waiting for an eject */
    WAIT_GAP       = 30,   /* frames without a poll that still continue the count */
    WAIT_HOLD      = 10,   /* frames the drive stays empty after a wait */
    ANCHOR_PENDING = 60,   /* frames after an ID check that count as loading until the head rewinds */
    SPAN_MERGE     = 30,   /* idle frames that end a load span */
};

enum { ACT_NONE, ACT_EJECT, ACT_HOLD };

enum { FF_ANCHOR = 1, FF_DATA = 2, FF_REWIND = 4 };

static struct {
    CycFdsHle cfg;
    /* observation */
    uint64_t fetch_cycle;
    bool     fetch_seen;
    uint8_t  frame_flags;
    uint32_t polls;               /* $4032 reads this frame */
    bool     spinning;            /* rewound, not ready yet */
    int      anchor_pending;      /* frames left of the BIOS's wait after an ID check */
    bool     loading;             /* the frame just ended was a load frame */
    bool     in_span;
    uint32_t span_first, span_last, span_active, span_idle, spans;
    uint32_t requests;
    /* auto swap (hashed) */
    uint8_t  action, hold, target, armed, rounds, host_owned, ejected;
    uint16_t poll_frames, gap;
    uint32_t swaps, bumps;
} H;

void cyc_fds_hle_configure(const CycFdsHle *cfg)
{
    CycFdsHle was = H.cfg;
    H.cfg = *cfg;
    if (!H.cfg.id_check) H.cfg.auto_swap = false;     /* the plan never grants it without one */
    if (was.auto_swap && !H.cfg.auto_swap && H.action) {
        /* Turned off mid-swap: never strand the drive empty. */
        if (H.action == ACT_HOLD && cyc_fds_side() < 0) fds_drive_insert(H.target, CYC_FDS_SIDE_HLE);
        H.action = ACT_NONE;
    }
    if (was.auto_swap != H.cfg.auto_swap || was.fast_load != H.cfg.fast_load)
        cyc_ring_push_len(CYC_EV_FDS_HLE, CYC_FDS_HLE_CONFIG, (H.cfg.auto_swap ? 1u : 0u) | (H.cfg.fast_load ? 2u : 0u),
                          H.cfg.id_check);
}

void *fds_hle_state_ptr(size_t *size)
{
    *size = sizeof(H);
    return &H;
}

void fds_hle_power_on(void)
{
    CycFdsHle cfg = H.cfg;
    memset(&H, 0, sizeof(H));
    H.cfg = cfg;
    if (cfg.auto_swap || cfg.fast_load)
        cyc_ring_push_len(CYC_EV_FDS_HLE, CYC_FDS_HLE_CONFIG, (cfg.auto_swap ? 1u : 0u) | (cfg.fast_load ? 2u : 0u),
                          cfg.id_check);
}

/* ---- the disk-ID request ---- */

static uint32_t match_mask(const uint8_t id[NES_FDS_HLE_ID_BYTES])
{
    uint32_t mask = 0;
    unsigned n = cyc_fds_side_count();
    for (unsigned s = 0; s < n && s < 32; ++s) {
        uint32_t len;
        const uint8_t *stream = cyc_fds_side_stream(s, &len);
        uint8_t block1[56];
        if (stream && nes_fds_hle_block1(stream, len, block1) && nes_fds_hle_id_matches(id, block1))
            mask |= 1u << s;
    }
    return mask;
}

static void request(void)
{
    uint8_t lo = 0, hi = 0, id[NES_FDS_HLE_ID_BYTES];
    cyc_debug_peek(H.cfg.id_pointer, &lo);
    cyc_debug_peek((uint16_t)((H.cfg.id_pointer + 1) & 0xFF), &hi);
    uint16_t ptr = (uint16_t)(lo | hi << 8);
    for (unsigned i = 0; i < NES_FDS_HLE_ID_BYTES; ++i)
        if (!cyc_debug_peek((uint16_t)(ptr + i), &id[i])) id[i] = 0;
    uint32_t mask = match_mask(id);
    int side = cyc_fds_side();
    H.requests++;
    H.frame_flags |= FF_ANCHOR;
    cyc_ring_push_len(CYC_EV_FDS_IDREQ, ptr, mask, side < 0 ? 0xFFu : (uint32_t)side);
    cyc_ring_push_len(CYC_EV_FDS_IDBYTES, (uint16_t)(id[8] | id[9] << 8),
                      (uint32_t)id[0] | (uint32_t)id[1] << 8 | (uint32_t)id[2] << 16 | (uint32_t)id[3] << 24,
                      (uint32_t)id[4] | (uint32_t)id[5] << 8 | (uint32_t)id[6] << 16 | (uint32_t)id[7] << 24);
    if (!H.cfg.auto_swap) return;
    /* A request: the program is alive and saying what it wants. */
    H.rounds = 0;
    H.host_owned = 0;
    unsigned count = 0;
    for (uint32_t m = mask; m; m &= m - 1) ++count;
    if (side >= 0 && (mask >> side & 1)) {
        cyc_ring_push_len(CYC_EV_FDS_HLE, CYC_FDS_HLE_KEEP, (uint32_t)side, mask);
        H.action = ACT_NONE;
    } else if (count == 1) {
        unsigned target = 0;
        while (!(mask >> target & 1)) ++target;
        cyc_ring_push_len(CYC_EV_FDS_HLE, CYC_FDS_HLE_SWAP, target, mask);
        H.target = (uint8_t)target;
        H.hold = SWAP_HOLD;
        H.action = ACT_EJECT;
    } else {
        cyc_ring_push_len(CYC_EV_FDS_HLE, count ? CYC_FDS_HLE_AMBIGUOUS : CYC_FDS_HLE_NOMATCH, mask,
                          side < 0 ? 0xFFu : (uint32_t)side);
    }
}

void fds_hle_snoop(uint16_t addr)
{
    if (!H.cfg.id_check) return;
    if (addr == H.cfg.id_check) {
        H.fetch_cycle = hw.cycles;
        H.fetch_seen = true;
    } else if (addr == (uint16_t)(H.cfg.id_check + 1) && H.fetch_seen &&
               hw.cycles == H.fetch_cycle + 1 + (uint64_t)hw_dma_stalls) {
        H.fetch_seen = false;
        request();
    }
}

/* ---- what the drive reports ---- */

void fds_hle_status_read(void) { H.polls++; }
void fds_hle_data(void) { H.frame_flags |= FF_DATA; }
void fds_hle_transfer(void) { H.frame_flags |= FF_DATA; }
void fds_hle_rewind(void) { H.frame_flags |= FF_REWIND; H.spinning = true; H.anchor_pending = 0; }
void fds_hle_ready(void) { H.spinning = false; }

void fds_hle_host_disk_change(void)
{
    if (H.action) cyc_ring_push_len(CYC_EV_FDS_HLE, CYC_FDS_HLE_CANCEL, H.target, H.action);
    H.action = ACT_NONE;
    H.armed = 0;
    H.poll_frames = H.gap = 0;
    H.host_owned = 1;
}

/* ---- once per frame ---- */

static void span_frame(bool active)
{
    uint32_t frame = cyc_ring_frame;
    if (active) {
        if (!H.in_span) {
            H.in_span = true;
            H.span_first = frame;
            H.span_active = 0;
        }
        H.span_last = frame;
        H.span_active++;
        H.span_idle = 0;
    } else if (H.in_span && ++H.span_idle > SPAN_MERGE) {
        H.in_span = false;
        H.spans++;
        cyc_ring_push_len(CYC_EV_FDS_SPAN, (uint16_t)((H.span_active > 0x7FFF ? 0x7FFF : H.span_active) |
                                                      (H.cfg.fast_load ? 0x8000 : 0)),
                          H.span_first, H.span_last - H.span_first + 1);
    }
}

static void swap_frame(uint8_t flags, uint32_t polls)
{
    int side = cyc_fds_side();
    if (H.action == ACT_EJECT) {
        H.ejected = side < 0 ? 0xFF : (uint8_t)side;
        if (side >= 0) {
            fds_drive_eject(CYC_FDS_SIDE_HLE);
            cyc_ring_push_len(CYC_EV_FDS_HLE, CYC_FDS_HLE_EJECT, (uint32_t)side, H.hold);
        }
        H.action = ACT_HOLD;
        return;
    }
    if (H.action == ACT_HOLD) {
        if (--H.hold == 0) {
            H.action = ACT_NONE;
            if (cyc_fds_side() < 0 && fds_drive_insert(H.target, CYC_FDS_SIDE_HLE)) {
                if (H.target == H.ejected) H.bumps++;
                else H.swaps++;
                cyc_ring_push_len(CYC_EV_FDS_HLE, CYC_FDS_HLE_INSERT, H.target, H.rounds);
            }
        }
        return;
    }
    if (flags || H.spinning || H.anchor_pending > 0) {
        /* A disk access: the next idle stretch may be a wait for the player. */
        if (!H.host_owned) H.armed = 1;
        H.poll_frames = H.gap = 0;
        return;
    }
    if (!H.armed || side < 0) return;
    if (polls) {
        H.poll_frames++;
        H.gap = 0;
    } else if (++H.gap > WAIT_GAP) {
        H.poll_frames = 0;
        H.gap = 0;
    }
    if (H.poll_frames < WAIT_POLLS) return;
    unsigned count = cyc_fds_side_count();
    unsigned next = (unsigned)side;
    if (H.rounds) next = (unsigned)(side + 1) % count;
    cyc_ring_push_len(CYC_EV_FDS_HLE, CYC_FDS_HLE_WAIT, next, H.poll_frames);
    H.target = (uint8_t)next;
    H.hold = WAIT_HOLD;
    H.action = ACT_EJECT;
    H.armed = 0;
    H.poll_frames = H.gap = 0;
    if (H.rounds < 255) H.rounds++;
    /* eject now, at this frame's end, like a requested swap */
    swap_frame(0, 0);
}

void fds_hle_frame_end(void)
{
    uint8_t flags = H.frame_flags;
    uint32_t polls = H.polls;
    H.frame_flags = 0;
    H.polls = 0;
    bool swapping = H.cfg.auto_swap && H.action != ACT_NONE;
    bool active = flags || H.spinning || H.anchor_pending > 0 || swapping;
    if (H.anchor_pending > 0) H.anchor_pending--;
    if (flags & FF_ANCHOR) H.anchor_pending = ANCHOR_PENDING;
    H.loading = active;
    span_frame(active);
    if (H.cfg.auto_swap) swap_frame(flags, polls);
}

/* ---- host view ---- */

bool cyc_fds_hle_loading(void) { return H.loading; }

void cyc_fds_hle_status(CycFdsHleStatus *s)
{
    memset(s, 0, sizeof(*s));
    s->auto_swap = H.cfg.auto_swap;
    s->fast_load = H.cfg.fast_load;
    s->observing = H.cfg.id_check != 0;
    s->loading = H.loading;
    s->swap_target = H.action ? (int)H.target : -1;
    s->swaps = H.swaps;
    s->bumps = H.bumps;
    s->spans = H.spans + (H.in_span ? 1 : 0);
    s->requests = H.requests;
}

/* Only the auto-swap machine acts on the machine; with the axis off nothing
 * here is hashed, so a run without it hashes exactly as before the tier. */
uint64_t fds_hle_state_hash(uint64_t acc)
{
    if (!H.cfg.auto_swap) return acc;
    const uint8_t v[] = { H.action, H.hold, H.target, H.armed, H.rounds, H.host_owned, H.ejected, (uint8_t)H.poll_frames,
                          (uint8_t)(H.poll_frames >> 8), (uint8_t)H.gap, (uint8_t)(H.gap >> 8),
                          (uint8_t)(H.anchor_pending), H.spinning, 0xA5 };
    for (size_t i = 0; i < sizeof(v); ++i) acc = acc * 131 + v[i];
    return acc;
}
