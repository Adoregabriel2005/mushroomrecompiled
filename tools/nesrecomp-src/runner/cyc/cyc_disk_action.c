/*
 * cyc_disk_action.c - the two-press Disk action (cyc_disk_action.h).
 */
#include "cyc_disk_action.h"

#include <stdio.h>
#include <string.h>

void cyc_disk_action_init(CycDiskAction *a, const CycDiskDrive *drive)
{
    memset(a, 0, sizeof(*a));
    a->drive = *drive;
    a->toast_ms = CYC_DISK_TOAST_MS;
    a->hold_frames = CYC_DISK_HOLD_FRAMES;
    a->target = -1;
    a->last_side = a->drive.side ? a->drive.side(a->drive.ctx) : -1;
}

static unsigned sides(const CycDiskAction *a) { return a->drive.side_count ? a->drive.side_count(a->drive.ctx) : 0; }
static int      in_drive(const CycDiskAction *a) { return a->drive.side ? a->drive.side(a->drive.ctx) : -1; }
static bool     writing(const CycDiskAction *a) { return a->drive.writing && a->drive.writing(a->drive.ctx); }

static bool toast_up(const CycDiskAction *a, uint64_t now_ms)
{
    return a->shown && (now_ms < a->toast_until || a->target >= 0 || a->eject_pending);
}

static int after(const CycDiskAction *a, int side)
{
    unsigned n = sides(a);
    return n ? (side < 0 ? 0 : (int)(((unsigned)side + 1) % n)) : -1;
}

static void refresh(CycDiskAction *a, uint64_t now_ms)
{
    a->shown = true;
    a->toast_until = now_ms + a->toast_ms;
}

/* Take `side` out of an occupied drive or into an empty one, then insert
 * `target` after the hold. */
static CycDiskPress start_swap(CycDiskAction *a, long frame, int target)
{
    int cur = in_drive(a);
    a->target = target;
    a->insert_at = frame + a->hold_frames;
    if (cur < 0) return CYC_DISK_PRESS_INSERT;
    a->last_side = cur;
    a->swaps++;
    if (writing(a)) a->eject_pending = true;
    else a->drive.eject(a->drive.ctx);
    return CYC_DISK_PRESS_SWAP;
}

CycDiskPress cyc_disk_action_press(CycDiskAction *a, uint64_t now_ms, long frame)
{
    if (!sides(a)) return CYC_DISK_PRESS_NONE;
    a->presses++;
    if (!toast_up(a, now_ms)) {
        refresh(a, now_ms);
        a->peeks++;
        return CYC_DISK_PRESS_PEEK;
    }
    refresh(a, now_ms);
    if (a->target >= 0) {
        /* still flipping: one more side on, and the hold starts over */
        a->target = after(a, a->target);
        if (!a->eject_pending) a->insert_at = frame + a->hold_frames;
        return CYC_DISK_PRESS_RETARGET;
    }
    int cur = in_drive(a);
    return start_swap(a, frame, after(a, cur >= 0 ? cur : a->last_side));
}

CycDiskPress cyc_disk_action_choose(CycDiskAction *a, uint64_t now_ms, long frame, unsigned side)
{
    if (side >= sides(a)) return CYC_DISK_PRESS_NONE;
    refresh(a, now_ms);
    if (a->target >= 0) {
        a->target = (int)side;
        if (!a->eject_pending) a->insert_at = frame + a->hold_frames;
        return CYC_DISK_PRESS_RETARGET;
    }
    if (in_drive(a) == (int)side) return CYC_DISK_PRESS_PEEK;
    return start_swap(a, frame, (int)side);
}

void cyc_disk_action_cancel(CycDiskAction *a)
{
    a->target = -1;
    a->eject_pending = false;
}

bool cyc_disk_action_frame(CycDiskAction *a, uint64_t now_ms, long frame)
{
    int cur = in_drive(a);
    if (cur >= 0 && !a->eject_pending) a->last_side = cur;
    if (a->eject_pending) {
        if (cur < 0) {
            /* someone else emptied the drive meanwhile: nothing to pull */
            a->eject_pending = false;
            a->insert_at = frame + a->hold_frames;
        } else if (!writing(a)) {
            a->eject_pending = false;
            a->drive.eject(a->drive.ctx);
            a->insert_at = frame + a->hold_frames;
            return true;
        }
        return false;
    }
    if (a->target < 0 || frame < a->insert_at) return false;
    int target = a->target;
    a->target = -1;
    if (cur >= 0) return false;            /* the host put a side in itself */
    if (!a->drive.insert(a->drive.ctx, (unsigned)target)) return false;
    a->inserts++;
    a->last_side = target;
    refresh(a, now_ms);                     /* show the new state */
    return true;
}

bool cyc_disk_action_toast(const CycDiskAction *a, uint64_t now_ms, CycDiskToast *out)
{
    memset(out, 0, sizeof(*out));
    out->side = in_drive(a);
    out->target = a->target;
    out->sides = sides(a);
    out->swapping = a->target >= 0;
    out->waiting_write = a->eject_pending;
    out->motor = out->side >= 0 && a->drive.motor_on && a->drive.motor_on(a->drive.ctx);
    out->save = a->drive.save_status ? a->drive.save_status(a->drive.ctx) : NULL;
    if (out->save && !*out->save) out->save = NULL;
    out->next = a->target >= 0 ? after(a, a->target) : after(a, out->side >= 0 ? out->side : a->last_side);
    out->visible = out->sides && toast_up(a, now_ms);
    return out->visible;
}

void cyc_disk_side_name(char *out, size_t n, int side)
{
    if (side < 0) snprintf(out, n, "NO DISK");
    else snprintf(out, n, "DISK %d SIDE %c", side / 2 + 1, 'A' + side % 2);
}

void cyc_disk_toast_text(const CycDiskToast *t, const char *action, char *title, size_t title_len, char *body,
                         size_t body_len)
{
    char cur[24], tgt[24], nxt[24];
    cyc_disk_side_name(cur, sizeof(cur), t->side);
    cyc_disk_side_name(tgt, sizeof(tgt), t->target);
    cyc_disk_side_name(nxt, sizeof(nxt), t->next);
    if (t->waiting_write) snprintf(title, title_len, "%s", cur);
    else if (t->swapping) snprintf(title, title_len, "SWAPPING TO %s", tgt);
    else if (t->side < 0) snprintf(title, title_len, "DRIVE EMPTY");
    else snprintf(title, title_len, "%s", cur);

    size_t at = 0;
    body[0] = 0;
#define LINE(...) do { if (at < body_len) { int w_ = snprintf(body + at, body_len - at, __VA_ARGS__); \
                       if (w_ > 0) at += (size_t)w_; } } while (0)
    if (t->waiting_write) LINE("WAITING FOR THE DISK WRITE\nTHEN SWAPPING TO %s", tgt);
    else if (t->swapping) LINE("DRIVE EMPTY");
    else if (t->side >= 0) LINE("MOTOR %s", t->motor ? "ON" : "OFF");
    else LINE("NO DISK IN THE DRIVE");
    if (t->save) LINE("\nSAVE: %s", t->save);
    const char *key = action && *action ? action : "DISK";
    if (t->sides > 1 || t->side < 0 || t->swapping)
        LINE("\n%s AGAIN: %s", key, nxt);
    else
        LINE("\n%s AGAIN: REINSERT %s", key, nxt);
#undef LINE
}
