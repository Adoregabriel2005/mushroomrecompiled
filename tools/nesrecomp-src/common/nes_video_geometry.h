/*
 * nes_video_geometry.h - the output geometry both NES runtimes share: the
 * width of a square-pixel 240-row picture for an aspect ratio, the named
 * modes (stock, 16:9, 21:9, 32:9, fit) and Fit's clamp to the window.
 *
 * The function-level runner (runner/src/nes_video.c) and the cycle host
 * (runner/cyc/cyc_video.c) each keep their own request/apply state machine
 * around these; the numbers come from here only, so the two cannot disagree.
 *
 * Square pixels: a 240-row picture of aspect A is round_even(240 * A)
 * columns (snesrecomp SmCalculateViewport). The vanilla 256x240 frame is
 * therefore 16:15, not 4:3, and STOCK is an explicit 256, never the formula.
 * Fit clamps the window's aspect to [16:15, 32:9]:
 *
 *   STOCK 256   16:9 426   21:9 560   32:9 854   FIT 256..854
 *
 * Widths are even so the stock 256 columns stay centered under symmetric
 * margins, and at most NES_VIDEO_MAX_WIDTH (32:9 at 240 rows is 853.3).
 */
#pragma once
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NES_VIDEO_MAX_WIDTH 864
#define NES_VIDEO_STOCK_ASPECT (256.0 / 240.0)

/* The same values as nes_video.h's NesAspectMode. */
enum {
    NES_VIDEO_STOCK = 0,
    NES_VIDEO_16_9,
    NES_VIDEO_21_9,
    NES_VIDEO_32_9,
    NES_VIDEO_FIT,
    NES_VIDEO_MODE_COUNT
};

static inline int nes_video_geometry_clamp_width(int w)
{
    if (w < 256) w = 256;
    if (w > NES_VIDEO_MAX_WIDTH) w = NES_VIDEO_MAX_WIDTH;
    return w & ~1;
}

static inline int nes_video_geometry_width_for_aspect(double aspect)
{
    if (!(aspect > 0.0)) return 256;
    int w = 2 * (int)floor((240.0 * aspect) / 2.0 + 0.5);
    return nes_video_geometry_clamp_width(w);
}

/* out_w x out_h: the window's drawable size (only Fit reads it; 0 = unknown). */
static inline int nes_video_geometry_width_for(int mode, int out_w, int out_h)
{
    double aspect;
    switch (mode) {
    case NES_VIDEO_16_9: aspect = 16.0 / 9.0; break;
    case NES_VIDEO_21_9: aspect = 21.0 / 9.0; break;
    case NES_VIDEO_32_9: aspect = 32.0 / 9.0; break;
    case NES_VIDEO_FIT:
        if (out_w > 0 && out_h > 0) {
            aspect = (double)out_w / (double)out_h;
            if (aspect < NES_VIDEO_STOCK_ASPECT) aspect = NES_VIDEO_STOCK_ASPECT;
            if (aspect > 32.0 / 9.0) aspect = 32.0 / 9.0;
        } else {
            aspect = NES_VIDEO_STOCK_ASPECT;
        }
        break;
    default:
        return 256;
    }
    return nes_video_geometry_width_for_aspect(aspect);
}

static inline const char *nes_video_geometry_name(int mode)
{
    switch (mode) {
    case NES_VIDEO_STOCK: return "stock";
    case NES_VIDEO_16_9:  return "16:9";
    case NES_VIDEO_21_9:  return "21:9";
    case NES_VIDEO_32_9:  return "32:9";
    case NES_VIDEO_FIT:   return "fit";
    default:              return "?";
    }
}

/* stock / 4:3 / off / 0, 16:9, 21:9, 32:9, fit / adaptive / auto; any case,
 * and '-', 'x' or '_' for ':'. Returns 1 and the mode, or 0. */
static inline int nes_video_geometry_parse(const char *name, int *out)
{
    if (!name || !out) return 0;
    char n[16];
    size_t i;
    for (i = 0; name[i] && i < sizeof(n) - 1; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == '-' || c == 'x' || c == '_') c = ':';
        n[i] = c;
    }
    n[i] = '\0';
    if (!strcmp(n, "stock") || !strcmp(n, "4:3") || !strcmp(n, "off") || !strcmp(n, "0")) { *out = NES_VIDEO_STOCK; return 1; }
    if (!strcmp(n, "16:9")) { *out = NES_VIDEO_16_9; return 1; }
    if (!strcmp(n, "21:9")) { *out = NES_VIDEO_21_9; return 1; }
    if (!strcmp(n, "32:9")) { *out = NES_VIDEO_32_9; return 1; }
    if (!strcmp(n, "fit") || !strcmp(n, "adaptive") || !strcmp(n, "auto")) { *out = NES_VIDEO_FIT; return 1; }
    return 0;
}

#ifdef __cplusplus
}
#endif
