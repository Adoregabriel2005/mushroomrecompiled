/* disk_action_test.c - the two-press Disk action (cyc_disk_action.h) against a
 * model drive: peek, swap, timeout, cycling every side of 1-, 2- and 4-sided
 * (two-disk) images, retargeting while the drive is held empty, a swap that
 * waits for a disk write, an empty drive, the runtime menu's direct choice, a
 * host that changes the drive itself, and the toast's text. Every drive change
 * is logged with its frame so the exact eject/insert sequence is checked, not
 * just the end state. */
#include "cyc_disk_action.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct {
    int      side;
    unsigned count;
    bool     motor, write;
    const char *save;
    long     now_frame;
    char     log[512];
} Drive;

static int      d_side(void *c) { return ((Drive *)c)->side; }
static unsigned d_count(void *c) { return ((Drive *)c)->count; }
static bool     d_motor(void *c) { return ((Drive *)c)->motor; }
static bool     d_writing(void *c) { return ((Drive *)c)->write; }
static const char *d_save(void *c) { return ((Drive *)c)->save; }
static void logf_(Drive *d, const char *what, int side)
{
    char e[32];
    snprintf(e, sizeof(e), "%s%ld:%s%d", d->log[0] ? " " : "", d->now_frame, what, side);
    strncat(d->log, e, sizeof(d->log) - strlen(d->log) - 1);
}
static bool d_eject(void *c)
{
    Drive *d = c;
    if (d->side < 0) return false;
    logf_(d, "E", d->side);
    d->side = -1;
    d->motor = false;
    return true;
}
static bool d_insert(void *c, unsigned s)
{
    Drive *d = c;
    if (d->side >= 0 || s >= d->count) return false;
    logf_(d, "I", (int)s);
    d->side = (int)s;
    return true;
}

static void setup(Drive *d, CycDiskAction *a, unsigned count, int side)
{
    memset(d, 0, sizeof(*d));
    d->count = count;
    d->side = side;
    CycDiskDrive drv = { d, d_side, d_count, d_eject, d_insert, d_motor, d_writing, d_save };
    cyc_disk_action_init(a, &drv);
}

/* Run frames [from, to) the way the host does: the action's frame hook before each. */
static void run(Drive *d, CycDiskAction *a, uint64_t *now, long *frame, long frames)
{
    for (long i = 0; i < frames; ++i) {
        d->now_frame = *frame;
        cyc_disk_action_frame(a, *now, *frame);
        *frame += 1;
        *now += 16;
    }
}

static CycDiskPress press(Drive *d, CycDiskAction *a, uint64_t now, long frame)
{
    d->now_frame = frame;
    return cyc_disk_action_press(a, now, frame);
}

static void peek_then_swap(void)
{
    Drive d; CycDiskAction a;
    setup(&d, &a, 2, 0);
    uint64_t now = 1000; long f = 100;
    CycDiskToast t;
    CHECK(!cyc_disk_action_toast(&a, now, &t));               /* nothing shown before a press */
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_PEEK);      /* 1st press: state only */
    CHECK(d.side == 0 && !d.log[0]);
    CHECK(cyc_disk_action_toast(&a, now, &t) && t.side == 0 && t.next == 1 && !t.swapping);
    run(&d, &a, &now, &f, 10);
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_SWAP);      /* 2nd press within the toast: swap */
    CHECK(d.side == -1 && !strcmp(d.log, "110:E0"));          /* ejected before frame 110 runs */
    CHECK(cyc_disk_action_toast(&a, now, &t) && t.swapping && t.target == 1);
    run(&d, &a, &now, &f, CYC_DISK_HOLD_FRAMES);
    CHECK(d.side == -1);                                      /* frames 110-139 ran with the drive empty */
    run(&d, &a, &now, &f, 1);
    CHECK(d.side == 1 && !strcmp(d.log, "110:E0 140:I1"));    /* in before frame 140 */
    CHECK(cyc_disk_action_toast(&a, now, &t) && t.side == 1 && !t.swapping && t.next == 0);
    CHECK(a.peeks == 1 && a.swaps == 1 && a.inserts == 1);
}

static void timeout_makes_peek(void)
{
    Drive d; CycDiskAction a;
    setup(&d, &a, 2, 0);
    uint64_t now = 0; long f = 0;
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_PEEK);
    now += CYC_DISK_TOAST_MS - 1;
    CycDiskToast t;
    CHECK(cyc_disk_action_toast(&a, now, &t));                /* still up 1 ms before the end */
    now += 1;
    CHECK(!cyc_disk_action_toast(&a, now, &t));               /* gone at the timeout */
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_PEEK);      /* so the next press peeks again */
    CHECK(d.side == 0 && !d.log[0]);
    /* after a swap completes the toast is refreshed, then times out the same way */
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_SWAP);
    run(&d, &a, &now, &f, CYC_DISK_HOLD_FRAMES + 1);
    CHECK(d.side == 1);
    uint64_t inserted = now - 16;                             /* the insert refreshed the toast */
    CHECK(cyc_disk_action_toast(&a, inserted + CYC_DISK_TOAST_MS - 1, &t));
    CHECK(!cyc_disk_action_toast(&a, inserted + CYC_DISK_TOAST_MS, &t));
    CHECK(press(&d, &a, inserted + CYC_DISK_TOAST_MS, f) == CYC_DISK_PRESS_PEEK);
}

static void cycle_all(unsigned count)
{
    Drive d; CycDiskAction a;
    setup(&d, &a, count, 0);
    uint64_t now = 0; long f = 0;
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_PEEK);
    /* every further press while the toast is up: exactly one side on */
    for (unsigned k = 1; k <= 2 * count; ++k) {
        CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_SWAP);
        run(&d, &a, &now, &f, CYC_DISK_HOLD_FRAMES + 1);
        CHECK(d.side == (int)(k % count));
    }
    CHECK(a.swaps == 2 * count && a.inserts == 2 * count && a.peeks == 1);
}

static void retarget_while_empty(void)
{
    Drive d; CycDiskAction a;
    setup(&d, &a, 4, 0);                                      /* two disks, four sides */
    uint64_t now = 0; long f = 0;
    press(&d, &a, now, f);
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_SWAP);      /* -> 1 (disk 1 B) */
    run(&d, &a, &now, &f, 10);
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_RETARGET);  /* -> 2 (disk 2 A), hold restarts */
    run(&d, &a, &now, &f, 10);
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_RETARGET);  /* -> 3 (disk 2 B) */
    CycDiskToast t;
    CHECK(cyc_disk_action_toast(&a, now, &t) && t.target == 3 && t.next == 0);
    run(&d, &a, &now, &f, CYC_DISK_HOLD_FRAMES);
    CHECK(d.side == -1);
    run(&d, &a, &now, &f, 1);
    CHECK(d.side == 3 && !strcmp(d.log, "0:E0 50:I3"));       /* one eject, one insert */
    char title[64], body[256];
    cyc_disk_action_toast(&a, now, &t);
    cyc_disk_toast_text(&t, "D", title, sizeof(title), body, sizeof(body));
    CHECK(!strcmp(title, "DISK 2 SIDE B"));
    CHECK(strstr(body, "D AGAIN: DISK 1 SIDE A"));
}

static void waits_for_write(void)
{
    Drive d; CycDiskAction a;
    setup(&d, &a, 2, 0);
    d.motor = d.write = true;
    uint64_t now = 0; long f = 0;
    press(&d, &a, now, f);
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_SWAP);
    CHECK(d.side == 0 && !d.log[0]);                           /* not pulled mid-write */
    CycDiskToast t;
    CHECK(cyc_disk_action_toast(&a, now + 100000, &t) && t.waiting_write);   /* stays up while waiting */
    run(&d, &a, &now, &f, 20);
    CHECK(d.side == 0);
    d.write = false;
    run(&d, &a, &now, &f, 1);
    CHECK(d.side == -1 && !strcmp(d.log, "20:E0"));
    run(&d, &a, &now, &f, CYC_DISK_HOLD_FRAMES);
    CHECK(d.side == 1 && !strcmp(d.log, "20:E0 50:I1"));      /* hooks 21..50 */      /* the hold counts from the real eject */
}

static void empty_drive(void)
{
    Drive d; CycDiskAction a;
    setup(&d, &a, 2, -1);                                     /* --fds-boot-disk none */
    uint64_t now = 0; long f = 0;
    CycDiskToast t;
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_PEEK);
    cyc_disk_action_toast(&a, now, &t);
    CHECK(t.side == -1 && t.next == 0);
    char title[64], body[256];
    cyc_disk_toast_text(&t, NULL, title, sizeof(title), body, sizeof(body));
    CHECK(!strcmp(title, "DRIVE EMPTY") && strstr(body, "DISK AGAIN: DISK 1 SIDE A"));
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_INSERT);
    run(&d, &a, &now, &f, CYC_DISK_HOLD_FRAMES + 1);
    CHECK(d.side == 0 && !strcmp(d.log, "30:I0"));
    /* an empty drive after the player ejected side A inserts the side after it */
    setup(&d, &a, 2, 0);
    now = 0; f = 0;
    d_eject(&d);
    d.log[0] = 0;
    cyc_disk_action_frame(&a, now, f);
    press(&d, &a, now, f);
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_INSERT);
    run(&d, &a, &now, &f, CYC_DISK_HOLD_FRAMES + 1);
    CHECK(d.side == 1);
}

static void one_side(void)
{
    Drive d; CycDiskAction a;
    setup(&d, &a, 1, 0);                                      /* SMB2J */
    uint64_t now = 0; long f = 0;
    press(&d, &a, now, f);
    CycDiskToast t;
    char title[64], body[256];
    cyc_disk_action_toast(&a, now, &t);
    cyc_disk_toast_text(&t, "LB", title, sizeof(title), body, sizeof(body));
    CHECK(strstr(body, "LB AGAIN: REINSERT DISK 1 SIDE A"));
    CHECK(press(&d, &a, now, f) == CYC_DISK_PRESS_SWAP);
    run(&d, &a, &now, &f, CYC_DISK_HOLD_FRAMES + 1);
    CHECK(d.side == 0 && !strcmp(d.log, "0:E0 30:I0"));       /* out and back in */
}

static void no_disk_system(void)
{
    Drive d; CycDiskAction a;
    setup(&d, &a, 0, -1);                                     /* a cartridge */
    CycDiskToast t;
    CHECK(press(&d, &a, 0, 0) == CYC_DISK_PRESS_NONE);
    CHECK(!cyc_disk_action_toast(&a, 0, &t));
    CHECK(a.presses == 0);
}

static void choose_and_host_changes(void)
{
    Drive d; CycDiskAction a;
    setup(&d, &a, 4, 0);
    uint64_t now = 0; long f = 0;
    CHECK(cyc_disk_action_choose(&a, now, f, 0) == CYC_DISK_PRESS_PEEK);   /* already in */
    CHECK(!d.log[0]);
    CHECK(cyc_disk_action_choose(&a, now, f, 2) == CYC_DISK_PRESS_SWAP);
    run(&d, &a, &now, &f, 5);
    CHECK(cyc_disk_action_choose(&a, now, f, 3) == CYC_DISK_PRESS_RETARGET);
    run(&d, &a, &now, &f, CYC_DISK_HOLD_FRAMES + 1);
    CHECK(d.side == 3 && !strcmp(d.log, "0:E0 35:I3"));
    CHECK(cyc_disk_action_choose(&a, now, f, 9) == CYC_DISK_PRESS_NONE);   /* no such side */
    /* the host inserts a side itself during the hold: the action backs off */
    setup(&d, &a, 2, 0);
    now = 0; f = 0;
    press(&d, &a, now, f);
    press(&d, &a, now, f);
    run(&d, &a, &now, &f, 3);
    d_insert(&d, 0);
    run(&d, &a, &now, &f, CYC_DISK_HOLD_FRAMES);
    CHECK(d.side == 0 && a.inserts == 0);
    CycDiskToast t;
    CHECK(cyc_disk_action_toast(&a, now, &t) && !t.swapping);
    /* cancel forgets a scheduled swap */
    setup(&d, &a, 2, 0);
    now = 0; f = 0;
    press(&d, &a, now, f);
    press(&d, &a, now, f);
    cyc_disk_action_cancel(&a);
    run(&d, &a, &now, &f, CYC_DISK_HOLD_FRAMES + 5);
    CHECK(d.side == -1 && a.inserts == 0);
}

static void toast_text(void)
{
    Drive d; CycDiskAction a;
    setup(&d, &a, 2, 1);
    d.motor = true;
    d.save = "SAVED F3919";
    press(&d, &a, 0, 0);
    CycDiskToast t;
    char title[64], body[256];
    cyc_disk_action_toast(&a, 0, &t);
    cyc_disk_toast_text(&t, "D", title, sizeof(title), body, sizeof(body));
    CHECK(!strcmp(title, "DISK 1 SIDE B"));
    CHECK(!strcmp(body, "MOTOR ON\nSAVE: SAVED F3919\nD AGAIN: DISK 1 SIDE A"));
    press(&d, &a, 0, 0);
    cyc_disk_action_toast(&a, 0, &t);
    cyc_disk_toast_text(&t, "D", title, sizeof(title), body, sizeof(body));
    CHECK(!strcmp(title, "SWAPPING TO DISK 1 SIDE A"));
    CHECK(!strcmp(body, "DRIVE EMPTY\nSAVE: SAVED F3919\nD AGAIN: DISK 1 SIDE B"));
    char tiny[8];
    cyc_disk_toast_text(&t, "D", tiny, sizeof(tiny), tiny, sizeof(tiny));   /* truncates, never overruns */
    CHECK(strlen(tiny) < sizeof(tiny));
}

int main(void)
{
    peek_then_swap();
    timeout_makes_peek();
    cycle_all(1);
    cycle_all(2);
    cycle_all(4);
    cycle_all(6);
    retarget_while_empty();
    waits_for_write();
    empty_drive();
    one_side();
    no_disk_system();
    choose_and_host_changes();
    toast_text();
    printf("disk_action_test: %u checks passed\n", checks);
    return 0;
}
