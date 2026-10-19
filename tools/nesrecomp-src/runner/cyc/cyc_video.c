/* cyc_video.c - the presented width; see cyc_video.h. */
#include "cyc_video.h"

#include "cyc_ring.h"

static int  s_mode = NES_VIDEO_STOCK;
static int  s_width = 256;
static bool s_window;
static bool s_pending;
static int  s_pending_width = 256;
static int  s_draw_w, s_draw_h;

static void commit(int width)
{
    if (width == s_width) return;
    s_width = width;
    cyc_ring_push_len(CYC_EV_VIDEO, (uint16_t)width, (uint32_t)s_mode,
                      s_mode == NES_VIDEO_FIT ? (uint32_t)(s_draw_w > 0 ? s_draw_w : 0) : 0);
}

static void request(int width)
{
    width = nes_video_geometry_clamp_width(width);
    if (!s_window) {
        commit(width);
        s_pending = false;
        return;
    }
    s_pending = width != s_width;
    s_pending_width = width;
}

void cyc_video_set_mode(int mode)
{
    if (mode < 0 || mode >= NES_VIDEO_MODE_COUNT) return;
    s_mode = mode;
    request(nes_video_geometry_width_for(mode, s_draw_w, s_draw_h));
}

int  cyc_video_mode(void) { return s_mode; }

void cyc_video_request_width(int width)
{
    s_mode = NES_VIDEO_STOCK;
    request(width);
}

int cyc_video_width(void) { return s_width; }
int cyc_video_native_x0(void) { return (s_width - 256) / 2; }

void cyc_video_window_ready(void) { s_window = true; }

void cyc_video_window_resized(int drawable_w, int drawable_h)
{
    s_draw_w = drawable_w;
    s_draw_h = drawable_h;
    if (s_mode == NES_VIDEO_FIT) request(nes_video_geometry_width_for(s_mode, drawable_w, drawable_h));
}

bool cyc_video_apply_pending(void)
{
    if (!s_pending) return false;
    s_pending = false;
    int before = s_width;
    commit(s_pending_width);
    return s_width != before;
}
