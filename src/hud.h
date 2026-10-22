#pragma once
#include <stdint.h>

/* The status bar as its own layer.
 *
 * The cartridge's scanline IRQ marks the split (cyc_frame_split_line, 240 when
 * the frame has none). From there down the picture is rebuilt from the frame's
 * per-line captures: rows between the split and the bar's first row are drawn
 * from their own capture (the level still owns them), the bar's staged rows
 * from its settled row, so no stripe of the playfield (or of the card) shows
 * above the bar, and the bar's own rows from their own capture. The picture is
 * kept across frames and rebuilt only when the state it was built from
 * changes. A frame with no split is untouched.
 *
 * Presentation only: nothing here feeds back into the machine. */

/* `native` (256x240 ARGB) in, `out` = the same picture with the rows from the
 * split down rebuilt; nothing above the split is touched. */
void smb3_hud_compose(const uint32_t *native, uint32_t *out);

/* The 256x240 picture with that rebuild applied, for a 4:3 presentation. */
const uint32_t *smb3_hud_present(const uint32_t *native);

/* The boundaries the last compose used: the bar's first row and the row its
 * content anchor came from. -1 when the frame has no bar. */
void smb3_hud_bounds(int *first, int *anchor);

/* Where the bar starts in the last composed picture: its first row, else the
 * split line, else 240. The wide compositor pastes from here. */
int smb3_hud_top(void);

/* Pictures hud_build() has built since power-on: the layer is rebuilt only when
 * the state it was built from changes, so a frame presented twice costs one
 * compare. Diagnostics only. */
uint64_t smb3_hud_builds(void);
