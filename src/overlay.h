#pragma once
#include <stdint.h>

/* Exact smb3pc menu skin: peach panel, paper border tiles from CHR bank $5E,
 * in-game dialog font, red cursor, check/cross markers. The panel is adapted
 * to this launcher's six-row menu and five save slots. */
typedef struct {
    uint32_t *fb; /* ARGB8888, R in the high byte */
    int w, h;
} OverlayTarget;

/* selected is 0..5. on[] is used for the WIDESCREEN/FULLSCREEN rows. */
void overlay_menu(OverlayTarget target, int selected, const int on[6], int slot,
                  const uint8_t *chr);
