/*
 * cyc_disk_action.h - the player's Disk action on a Famicom Disk System
 * program: one bindable button that shows the drive and flips the disk.
 *
 *   press while no toast is up  -> a toast with the drive's state (which disk
 *                                  and side is in, the motor, the disk save);
 *                                  nothing changes on the machine
 *   press while the toast is up -> one swap to the next side: eject now, keep
 *                                  the drive empty for hold_frames emulated
 *                                  frames, insert the next side (disk 1 A,
 *                                  1 B, 2 A, ... and around), and refresh the
 *                                  toast. A press while a swap still holds the
 *                                  drive empty moves the target on one more
 *                                  side and restarts the hold.
 *   the toast times out         -> the next press is a peek again
 *
 * The drive is changed only through the host's normal eject/insert (the same
 * calls the --fds-event and dev keys make), between frames. The toast's clock
 * is the caller's wall clock (milliseconds); the drive's is emulated frames, so
 * a scripted run of the action is reproducible at any speed.
 *
 * A swap never pulls the disk while the drive is writing it: the eject waits
 * for the write run to end (a real drive would corrupt the block).
 *
 * Pure C11, no SDL and no machine: the drive is a table of callbacks, so the
 * state machine is unit-tested on its own (cyc_disk_action_test.c) and driven
 * from the SDL host, the headless --input DISK_ACTION command and the runtime
 * menu alike.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *ctx;
    int      (*side)(void *ctx);              /* the side in the drive, -1: empty */
    unsigned (*side_count)(void *ctx);
    bool     (*eject)(void *ctx);
    bool     (*insert)(void *ctx, unsigned side);
    bool     (*motor_on)(void *ctx);
    bool     (*writing)(void *ctx);           /* a write run is storing bytes (NULL: never) */
    const char *(*save_status)(void *ctx);    /* "SAVED F1234"... (NULL: none) */
} CycDiskDrive;

#define CYC_DISK_TOAST_MS    3000u   /* how long a toast stays up after a press */
#define CYC_DISK_HOLD_FRAMES 30      /* frames the drive stays empty during a swap */

typedef enum {
    CYC_DISK_PRESS_NONE,      /* not an FDS program, or no sides */
    CYC_DISK_PRESS_PEEK,      /* toast shown; the drive is untouched */
    CYC_DISK_PRESS_SWAP,      /* ejected (or eject queued behind a write); insert scheduled */
    CYC_DISK_PRESS_RETARGET,  /* a swap was holding the drive empty: next side instead */
    CYC_DISK_PRESS_INSERT,    /* the drive was empty: insert scheduled */
} CycDiskPress;

typedef struct {
    CycDiskDrive drive;
    uint32_t toast_ms;
    int      hold_frames;
    /* state */
    uint64_t toast_until;      /* wall ms; the toast is up while now < toast_until */
    bool     shown;            /* a toast was shown at least once */
    int      target;           /* the side a swap puts in, -1: no swap */
    bool     eject_pending;    /* the eject waits for a write run to end */
    long     insert_at;        /* frame the target goes in */
    int      last_side;        /* the side last seen in the drive (-1: none yet) */
    unsigned presses, peeks, swaps, inserts;
} CycDiskAction;

void cyc_disk_action_init(CycDiskAction *a, const CycDiskDrive *drive);

/* A press of the Disk action, before frame `frame` runs (frames done so far). */
CycDiskPress cyc_disk_action_press(CycDiskAction *a, uint64_t now_ms, long frame);

/* Before every frame: a queued eject once the write ends, the insert once the
 * hold is over. Returns true when it changed the drive. */
bool cyc_disk_action_frame(CycDiskAction *a, uint64_t now_ms, long frame);

/* Choose a side directly (the runtime menu): the same swap, to `side`. */
CycDiskPress cyc_disk_action_choose(CycDiskAction *a, uint64_t now_ms, long frame, unsigned side);

/* Forget a pending swap, e.g. after the host changed the drive another way. */
void cyc_disk_action_cancel(CycDiskAction *a);

/* What the toast shows. */
typedef struct {
    bool visible;
    bool swapping;             /* the drive is held empty (or about to be) for a swap */
    bool waiting_write;        /* the eject waits for a disk write to end */
    int  side;                 /* in the drive, -1: empty */
    int  target;               /* going in, -1: none */
    int  next;                 /* what the next press puts in */
    unsigned sides;
    bool motor;
    const char *save;          /* the disk save's state, or NULL */
} CycDiskToast;
bool cyc_disk_action_toast(const CycDiskAction *a, uint64_t now_ms, CycDiskToast *out);

/* Toast text: a title ("DISK 1 SIDE A") and a body of '\n'-separated lines;
 * `action` names the binding for the hint line ("D", "LB"), may be NULL. */
void cyc_disk_toast_text(const CycDiskToast *t, const char *action, char *title, size_t title_len, char *body,
                         size_t body_len);

/* "DISK 1 SIDE A" (sides count disk 1 A = 0, 1 B = 1, 2 A = 2, ...). */
void cyc_disk_side_name(char *out, size_t n, int side);

#ifdef __cplusplus
}
#endif
