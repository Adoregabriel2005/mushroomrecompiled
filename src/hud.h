#pragma once
#include <stdint.h>

/* The status bar as its own layer, ported from smb3pc's src/runtime/hud.cpp.
 *
 * The cartridge's scanline IRQ marks the split (cyc_frame_split_line, 240 when
 * the frame has none); the rows the split stages are drawn from the bar's
 * settled row, at that row's captured palette, banking and scroll, so no stripe
 * of the playfield (or of the card) shows above the bar. Rows above the bar's
 * first row keep the level's pixels, and the rows the bar owns are the game's
 * own picture rebuilt from state. A frame with no split is untouched.
 *
 * Presentation only: nothing here feeds back into the machine. */

/* `native` (256x240 ARGB) in, `out` = the same picture with the bar's rows
 * rebuilt; the level's rows are untouched. */
void smb3_hud_compose(const uint32_t *native, uint32_t *out);

/* The 256x240 picture with that repair applied, for a 4:3 presentation. */
const uint32_t *smb3_hud_present(const uint32_t *native);

/* The boundaries the last compose used: the bar's first row and the row its
 * content anchor came from. -1 when the frame has no bar. */
void smb3_hud_bounds(int *first, int *anchor);

/* Where the bar starts in the last composed picture: its first row, else the
 * split line, else 240. The wide compositor pastes from here. */
int smb3_hud_top(void);
