#pragma once
#include <stdint.h>

/* Exact smb3pc menu skin: peach panel, paper border tiles from CHR bank $5E,
 * in-game dialog font, red cursor, check/cross markers. The panel is adapted
 * to this launcher's five-row menu and five save slots. */
typedef struct {
    uint32_t *fb; /* ARGB8888, R in the high byte */
    int w, h;
} OverlayTarget;

/* selected is 0..4. on[] is used for the FULLSCREEN row. */
void overlay_menu(OverlayTarget target, int selected, const int on[5], int slot,
                  const uint8_t *chr);
