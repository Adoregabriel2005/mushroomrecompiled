/*
 * nes_video.c — live-resizable output geometry (see nes_video.h).
 *
 * Owns the requested/pending geometry only. The SDL objects and the pixel
 * buffers live in main_runner.c, which polls nes_video_take_pending() once per
 * frame at the safe point (after present, before the next render).
 */
#include "nes_video.h"
#include "nes_runtime.h"
#include "../../common/nes_video_geometry.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static NesAspectMode s_mode = NES_ASPECT_STOCK;
static int s_window_ready = 0;
static int s_pending = 0;
static int s_pending_left = 0, s_pending_right = 0;
static int s_last_out_w = 0, s_last_out_h = 0;

/* The numbers are common/nes_video_geometry.h's, shared with the cycle host
 * (runner/cyc/cyc_video.c); this file keeps only the function-level runner's
 * request/apply state. */
typedef char nes_video_one_width_limit[NES_MAX_RENDER_WIDTH == NES_VIDEO_MAX_WIDTH ? 1 : -1];
typedef char nes_video_one_mode_list[(NES_ASPECT_STOCK == NES_VIDEO_STOCK && NES_ASPECT_16_9 == NES_VIDEO_16_9 &&
                                      NES_ASPECT_21_9 == NES_VIDEO_21_9 && NES_ASPECT_32_9 == NES_VIDEO_32_9 &&
                                      NES_ASPECT_FIT == NES_VIDEO_FIT && NES_ASPECT_COUNT == NES_VIDEO_MODE_COUNT)
                                     ? 1 : -1];

static int clamp_even_width(int w) { return nes_video_geometry_clamp_width(w); }

int nes_video_width_for_aspect(double aspect) { return nes_video_geometry_width_for_aspect(aspect); }

int nes_video_width_for(NesAspectMode mode, int out_w, int out_h) {
    return nes_video_geometry_width_for((int)mode, out_w, out_h);
}

const char *nes_video_aspect_name(NesAspectMode mode) { return nes_video_geometry_name((int)mode); }

int nes_video_aspect_from_name(const char *name, NesAspectMode *out) {
    int mode;
    if (!out || !nes_video_geometry_parse(name, &mode)) return 0;
    *out = (NesAspectMode)mode;
    return 1;
}

void nes_video_request_margins(int left, int right) {
    if (left < 0) left = 0;
    if (right < 0) right = 0;
    if (256 + left + right > NES_MAX_RENDER_WIDTH) {
        /* Shrink the larger margin first; keep the request satisfiable. */
        int over = 256 + left + right - NES_MAX_RENDER_WIDTH;
        if (left >= right) { left -= over; if (left < 0) { right += left; left = 0; } }
        else               { right -= over; if (right < 0) { left += right; right = 0; } }
    }
    if (!s_window_ready) {
        /* Pre-window: apply now, exactly as the old game_on_init() assignments
         * did, so the first SDL texture is created at the right size. */
        nes_video_commit(left, right);
        s_pending = 0;
        return;
    }
    if (left == g_widescreen_left && right == g_widescreen_right) {
        s_pending = 0;
        return;
    }
    s_pending = 1;
    s_pending_left = left;
    s_pending_right = right;
}

void nes_video_request_width(int width) {
    int w = clamp_even_width(width);
    int m = (w - 256) / 2;
    nes_video_request_margins(m, m);
}

void nes_video_set_aspect_mode(NesAspectMode mode) {
    if (mode < 0 || mode >= NES_ASPECT_COUNT) return;
    s_mode = mode;
    nes_video_request_width(nes_video_width_for(mode, s_last_out_w, s_last_out_h));
}

NesAspectMode nes_video_aspect_mode(void) { return s_mode; }

void nes_video_on_window_resized(int out_w, int out_h) {
    s_last_out_w = out_w;
    s_last_out_h = out_h;
    if (s_mode == NES_ASPECT_FIT)
        nes_video_request_width(nes_video_width_for(s_mode, out_w, out_h));
}

int nes_video_take_pending(int *left, int *right) {
    if (!s_pending) return 0;
    if (left) *left = s_pending_left;
    if (right) *right = s_pending_right;
    s_pending = 0;
    return 1;
}

void nes_video_commit(int left, int right) {
    g_widescreen_left  = left;
    g_widescreen_right = right;
    g_render_width     = 256 + left + right;
}

void nes_video_mark_window_ready(void) { s_window_ready = 1; }
int  nes_video_window_ready(void)      { return s_window_ready; }
