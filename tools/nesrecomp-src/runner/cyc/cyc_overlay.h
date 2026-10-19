/*
 * cyc_overlay.h - host drawing that is never part of the machine's picture:
 * the toast (the Disk action's answer) and the dev build's drive bar text,
 * with a 3x5 capitals font.
 *
 * Everything here writes into a PRESENTATION buffer the caller owns: a copy
 * of cyc_frame_argb() for the window without recomp-ui, or a headless
 * --present-out image. cyc_frame_argb() itself, and so screenshots,
 * --hash-out, --frame-log and every comparison, never contain it.
 * Windows with recomp-ui draw the toast with its runtime UI instead
 * (recomp_runtime_ui_set_toast).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Five rows of three bits for `c` (capitals, digits, space and -.:/()=+!?);
 * NULL for a character the font lacks (drawn as a space). */
const uint8_t *cyc_overlay_glyph(char c);

/* Text at (x, y) in `color` (0xRRGGBB), `scale` pixels per font pixel,
 * clipped to the buffer; lower case draws as capitals. Returns the x after it. */
int cyc_overlay_text(uint32_t *dst, int width, int height, int x, int y, int scale, uint32_t color,
                     const char *text);

/* The toast: a panel centred near the top holding `title` and the
 * '\n'-separated lines of `body`. */
void cyc_overlay_toast(uint32_t *dst, int width, int height, const char *title, const char *body);

#ifdef __cplusplus
}
#endif
