/* The status bar as its own layer: a port of smb3pc's src/runtime/hud.cpp.
 *
 * The frame is two layers with one boundary between them, not a per-row
 * reconstruction of the game's writes. The cartridge's scanline IRQ marks where
 * the game *starts* writing the bar's scroll and CHR banks (cyc_frame_split_line,
 * the last IRQ of the frame, its bar on the next line); the writes land over the
 * next line or two, so the first row or two at the bottom of the picture still
 * shows the playfield's state - outdoors a 1px stripe of sky above the black
 * status bar. bar_first() finds the bar's first row exactly: the first row at or
 * below the split that no longer continues the row above. bar_settled() finds
 * the bar's content anchor - the first row below that break which runs on to the
 * bottom of the picture with the bar's CHR banking.
 *
 * Like hud_build(), the picture is built from the split line down. The rows from
 * the split to the bar's first row are still the level's - the split's staged
 * $2006/$2005 writes have not landed yet - so they keep their own captured
 * scroll and are drawn as the level has them; the ground under the player
 * therefore runs to the level's real last scanline. From the bar's first row
 * down the rows are the bar's: drawn from the anchor until the capture is the
 * bar's own (the anchor row), then from their own, at that row's captured
 * palette, banking and scroll, so the split's staged writes cannot show.
 *
 * Everything a row is drawn from is captured per line at dot 1 (CycLine): v,
 * fine X, $2000/$2001, palette RAM, the CHR index each slot resolved to, and
 * OAM. The nametables come from cyc_frame_nametables(), the mapping the picture
 * was drawn with: by the time this pass runs the live palette and banking have
 * moved on, and the game's NMI handler has already rewritten tiles (level
 * column streaming) for the next frame. The row renderer is the
 * reference's - background fetched one tile at a time, sprites evaluated for the
 * row from the bar's snapshot and composited over the finished row, first sprite
 * with a pixel winning, background priority, the left-column clip and greyscale,
 * all as the hardware applies them.
 *
 * The layer keeps the picture in hud.cpp's cache: hud_matches() compares the
 * state the picture was built from (the split, the two boundary rows, the
 * anchor's scroll and OAM, the frame's nametables, and every row's scroll,
 * $2000/$2001, palette and CHR banking), and hud_build() runs only when that
 * changes; a steady frame costs one compare and the blit of the rows it drew.
 * The mirroring itself is not a separate key: the frame's nametables are the
 * mapped view, so a mirroring change that changes the picture changes them.
 *
 * Only the rows hud_build() drew are blitted, and nothing above the split is
 * touched: the rows there are the level's own, drawn by the per-dot pass.
 *
 * Presentation only: nothing here feeds back into the machine. */

#include "hud.h"

#include "cyc_core.h"
#include "cyc_render.h"

#include <string.h>

#define BAR_W 256
#define PICTURE_H 240
#define NO_HUD 240

static int s_split = NO_HUD, s_first = -1, s_settled = -1;

/* The rows hud_build() last produced, indexed by picture row, and which of them
 * it drew (hud.cpp's hud.pixels / hud.on). */
static uint32_t s_built[BAR_W * PICTURE_H];
static uint8_t s_on[PICTURE_H];
/* A composed picture for the 4:3 presentation. */
static uint32_t s_picture[BAR_W * PICTURE_H];

/* One row of hud.cpp's HudLayer::Source::Row. */
typedef struct {
    uint16_t v;
    uint8_t fine_x, ctrl, mask, on, pad;
    uint8_t pal[32];
    uint32_t chr[8];
} HudRow;

/* What the cached picture was built from (HudLayer::Source). */
typedef struct {
    int top, first, settled;
    uint16_t v;
    uint8_t fine_x;
    uint8_t oam[256];
    uint8_t nt[0x1000];
    HudRow row[PICTURE_H];
} HudSource;

static HudSource s_src;
static int s_cached; /* s_src describes a built layer */
static uint64_t s_builds;

void smb3_hud_bounds(int *first, int *anchor)
{
    if (first) *first = s_first;
    if (anchor) *anchor = s_settled;
}

uint64_t smb3_hud_builds(void) { return s_builds; }

int smb3_hud_top(void) { return s_first >= 0 ? s_first : s_split; }

/* smb3pc's `on`: the line had rendering enabled. A line without it belongs to
 * the per-dot pass, whose backdrop the layer must not paint over. */
static int rendering_on(const CycLine *l) { return (l->mask & 0x18) != 0; }

/* The scroll state one scanline below `v` when nothing is written in between:
 * fine Y steps, and at the bottom of the picture the vertical counter wraps to
 * the next nametable row or to the top. */
static uint16_t chain_step(uint16_t v)
{
    unsigned fy = (v >> 12) & 7u;
    if (fy < 7) return (uint16_t)(v + 0x1000u);
    unsigned y = (v >> 5) & 31u, nt = (v >> 11) & 1u;
    if (y == 29) {
        y = 0;
        nt ^= 1u;
    } else if (y == 31) {
        y = 0;
    } else {
        y += 1;
    }
    return (uint16_t)((v & 0x841Fu) | (y << 5) | (nt << 11));
}

/* Whether `cur` continues `prev` with no scroll write between. */
static int chain_cont(uint16_t prev, uint16_t cur) { return chain_step(prev) == cur; }

/* The bar's first row: the first row at or below the split that no longer
 * continues the row above. NO_HUD when the frame has no break. */
static int bar_first(const CycLine *lines)
{
    for (int y = s_split; y < NO_HUD; y++) {
        const CycLine *h = &lines[y];
        if (!rendering_on(h)) return y; /* rendering off: the backdrop owns the row */
        if (y == 0) return y;
        const CycLine *above = &lines[y - 1];
        if (!rendering_on(above)) return y;
        /* A bank switch is the split's other signal: the bar's tiles live in
         * another CHR bank. Captured per line, so this needs no guess. */
        if (memcmp(h->chr, above->chr, sizeof h->chr)) return y;
        if (!chain_cont(above->v, h->v) || h->fine_x != above->fine_x) return y;
    }
    return NO_HUD;
}

/* The bar's content anchor: the first row below its first row that starts a run
 * continuing to the bottom of the picture with the bar's CHR banking. The rows
 * the split has not finished writing are skipped by the continuity test itself.
 * -1 when no such row exists. */
static int bar_settled(const CycLine *lines)
{
    int bottom = PICTURE_H - 1;
    while (bottom > s_split && !rendering_on(&lines[bottom])) bottom--;
    for (int y = s_split; y + 1 < PICTURE_H; y++) {
        const CycLine *a = &lines[y], *b = &lines[y + 1];
        if (!rendering_on(a) || !rendering_on(b)) continue;
        if (memcmp(a->chr, lines[bottom].chr, sizeof a->chr)) continue;
        if (chain_cont(a->v, b->v)) return y;
    }
    return -1;
}

/* Whether the cached picture still describes this frame (hud.cpp's
 * hud_matches): the state the picture was built from, so no mutation path has
 * to remember to invalidate the layer. */
static int hud_matches(int settled, int first)
{
    const HudSource *s = &s_src;
    const CycLine *lines = cyc_frame_lines();
    if (!s_cached) return 0;
    if (s->top != s_split || s->settled != settled || s->first != first) return 0;
    const CycLine *a = &lines[settled];
    if (s->v != a->v || s->fine_x != a->fine_x) return 0;
    if (memcmp(s->oam, a->oam, sizeof s->oam)) return 0;
    if (memcmp(s->nt, cyc_frame_nametables(), sizeof s->nt)) return 0;
    for (int i = 0; i < NO_HUD - s->top; i++) {
        const CycLine *h = &lines[s->top + i];
        const HudRow *r = &s->row[i];
        if (r->v != h->v || r->fine_x != h->fine_x || r->ctrl != h->ctrl ||
            r->mask != h->mask || r->on != (rendering_on(h) ? 1 : 0))
            return 0;
        if (memcmp(r->pal, h->pal, sizeof r->pal)) return 0;
        if (memcmp(r->chr, h->chr, sizeof r->chr)) return 0;
    }
    return 1;
}

/* Record the state the cached picture is about to be built from. */
static void hud_snapshot(int settled, int first)
{
    HudSource *s = &s_src;
    const CycLine *lines = cyc_frame_lines();
    const CycLine *a = &lines[settled];
    s_cached = 1;
    s->top = s_split;
    s->first = first;
    s->settled = settled;
    s->v = a->v;
    s->fine_x = a->fine_x;
    memcpy(s->oam, a->oam, sizeof s->oam);
    memcpy(s->nt, cyc_frame_nametables(), sizeof s->nt);
    for (int i = 0; i < NO_HUD - s->top; i++) {
        const CycLine *h = &lines[s->top + i];
        HudRow *r = &s->row[i];
        r->v = h->v;
        r->fine_x = h->fine_x;
        r->ctrl = h->ctrl;
        r->mask = h->mask;
        r->on = (uint8_t)(rendering_on(h) ? 1 : 0);
        memcpy(r->pal, h->pal, sizeof r->pal);
        memcpy(r->chr, h->chr, sizeof r->chr);
    }
}

/* The row's first pixel's column in the two nametables side by side. The dot-1
 * capture leads the displayed scroll by the two prefetch bumps (dots 328 and
 * 336 of the line before), so back those 16 pixels off once. */
static unsigned column_of(uint16_t v, unsigned fine_x)
{
    const unsigned nt_x = (v >> 10) & 1u;
    return (nt_x * 256u + (v & 0x1Fu) * 8u + fine_x + 512u - 16u) % 512u;
}

static uint8_t reverse_bits(uint8_t b)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) r = (uint8_t)((r << 1) | ((b >> i) & 1));
    return r;
}

enum { BAR_SPRITES = 8 };

typedef struct {
    uint8_t lo, hi, x, pal;
    uint8_t behind;
} BarSprite;

/* Draw one row of the block at picture row `y` from captured line `row_i`: the
 * row's own capture when it is the row's own, the anchor's for the split's
 * transition rows. `bar_fine_x`/`bar_col` are the bar layer's single horizontal
 * origin. */
static void draw_row(uint32_t *out, int y, int row_i, unsigned bar_fine_x, unsigned bar_col)
{
    const CycLine *row = &cyc_frame_lines()[row_i];
    const unsigned ty = (row->v >> 5) & 31u;
    const unsigned nty = (row->v >> 11) & 1u;
    const unsigned fy = (row->v >> 12) & 7u;

    const unsigned bg_base = (row->ctrl & 0x10) ? 0x1000u : 0x0000u;
    const int bg_on = (row->mask & 0x08) != 0;
    const int bg_left_on = (row->mask & 0x02) != 0;
    const int spr_on = (row->mask & 0x10) != 0;
    const int spr_left_on = (row->mask & 0x04) != 0;

    uint32_t *line = out + y * BAR_W;

    /* A row whose captured horizontal state is not the bar's - another split
     * lower down - keeps its own origin instead of being sheared onto the
     * bar's. */
    const unsigned col =
        ((row->v & 0x041Fu) == (cyc_frame_lines()[s_settled].v & 0x041Fu) &&
         row->fine_x == bar_fine_x)
            ? bar_col
            : column_of(row->v, row->fine_x);

    /* Background, one nametable tile at a time: the tile, its attribute and its
     * two pattern bytes are fetched once per eight pixels. */
    uint8_t bg_pat[BAR_W];
    unsigned wx = col;
    unsigned pal2 = 0, lo = 0, hi = 0;
    int loaded = 0;
    for (int x = 0; x < BAR_W; x++) {
        const unsigned fx = wx & 7u;
        if (!loaded || fx == 0) {
            const unsigned ntx = wx >> 8;
            const unsigned tx = (wx & 0xFFu) >> 3;
            const unsigned quad = (nty << 1) | ntx;
            const uint8_t tile =
                cyc_render_frame_nametable((uint16_t)(0x2000 | (quad << 10) | (ty * 32u) | tx));
            const uint8_t attr =
                cyc_render_frame_nametable((uint16_t)(0x23C0 | (quad << 10) | ((ty >> 2) << 3) |
                                                      (tx >> 2)));
            pal2 = (attr >> (((ty & 2) << 1) | (tx & 2))) & 3u;
            const uint16_t pat_addr = (uint16_t)(bg_base + 16u * tile + fy);
            lo = cyc_render_line_chr(row_i, pat_addr);
            hi = cyc_render_line_chr(row_i, (uint16_t)(pat_addr + 8));
            loaded = 1;
        }
        const unsigned bit = 7u - fx;
        const unsigned pat = (((hi >> bit) & 1u) << 1) | ((lo >> bit) & 1u);
        bg_pat[x] = (uint8_t)pat;
        const int visible = bg_on && !(x < 8 && !bg_left_on);
        const unsigned entry = (visible && pat) ? ((pal2 << 2) | pat) : 0u;
        line[x] = cyc_render_line_color(row_i, (int)entry);
        wx = (wx + 1) & 511u;
    }

    if (!spr_on) return; /* sprites off on this row */

    /* Sprites, evaluated for this row from the bar's snapshot and composited
     * over the finished row. The row of a sprite a scanline shows is measured
     * from the OAM capture this line was drawn with (cyc_frame_lines' note on
     * OAM: captured at dot 1, one line ahead of the eight sprites the hardware
     * fetched for the row's own display). */
    const uint8_t *oam = cyc_frame_lines()[s_settled].oam;
    const unsigned height = (row->ctrl & 0x20) ? 16u : 8u;
    const unsigned spr_base = (row->ctrl & 0x08) ? 0x1000u : 0x0000u;
    BarSprite sprs[BAR_SPRITES];
    int nsprs = 0;

    for (int k = 0; k < 64 && nsprs < BAR_SPRITES; k++) {
        const unsigned sy = oam[k * 4], tile = oam[k * 4 + 1], attr = oam[k * 4 + 2];
        const unsigned diff = (unsigned)(y - 1) - sy;
        if (diff >= height) continue;
        const unsigned r = (attr & 0x80) ? (height - 1 - diff) : diff;
        unsigned addr;
        if (height == 8) {
            addr = spr_base + 16u * tile + (r & 7);
        } else {
            addr = ((tile & 1) ? 0x1000u : 0x0000u) + 16u * (tile & 0xFEu) +
                   ((r & 8) << 1) + (r & 7);
        }
        BarSprite sp;
        sp.lo = cyc_render_line_chr(row_i, (uint16_t)addr);
        sp.hi = cyc_render_line_chr(row_i, (uint16_t)(addr + 8));
        if (attr & 0x40) { /* horizontal flip */
            sp.lo = reverse_bits(sp.lo);
            sp.hi = reverse_bits(sp.hi);
        }
        sp.x = (uint8_t)oam[k * 4 + 3];
        sp.pal = (uint8_t)(attr & 3);
        sp.behind = (uint8_t)((attr & 0x20) != 0);
        sprs[nsprs++] = sp;
    }
    if (!nsprs) return;

    uint8_t claimed[BAR_W];
    memset(claimed, 0, sizeof claimed);
    for (int k = 0; k < nsprs; k++) {
        const BarSprite *sp = &sprs[k];
        const int x0 = sp->x;
        const int x1 = sp->x + 8 < BAR_W ? sp->x + 8 : BAR_W;
        for (int x = x0; x < x1; x++) {
            if (x < 0 || claimed[x]) continue;
            if (x < 8 && !spr_left_on) continue;
            const unsigned off = (unsigned)(x - sp->x);
            const unsigned shift = 7u - off;
            const unsigned pat = (((sp->hi >> shift) & 1u) << 1) | ((sp->lo >> shift) & 1u);
            if (!pat) continue;
            claimed[x] = 1; /* the first sprite with a pixel wins, as in the PPU */
            if (sp->behind && bg_pat[x]) continue; /* behind an opaque background */
            line[x] = cyc_render_line_color(row_i, (int)(0x10u + ((unsigned)sp->pal << 2) + pat));
        }
    }
}

/* Draw the whole layer, from the split line down, into s_built (hud_build).
 * Rows above the bar's first row keep their own capture and are drawn as the
 * level has them; from the bar's first row down, the rows between it and the
 * anchor are drawn from the anchor - the split's staged writes have not all
 * landed - and the rest from their own. */
static void hud_build(void)
{
    const HudSource *s = &s_src;
    const int rows = NO_HUD - s->top;
    s_builds++;
    memset(s_on, 0, sizeof s_on);
    memset(s_built + s->top * BAR_W, 0, (size_t)rows * BAR_W * sizeof(uint32_t));

    /* The bar's origin, in the 512-column two-nametable row: coarse X and
     * nametable from the anchor row, fine X from the tail of the bar (the
     * split's staged $2005 write lands a line or two in, so the head of the
     * capture still carries the playfield's). */
    int tail = rows - 1;
    while (tail > 0 && !s->row[tail].on) tail--;
    const unsigned bar_fine_x = s->row[tail].fine_x;
    const unsigned bar_col = column_of(s->v, bar_fine_x);

    int anchor_i = s->settled - s->top;
    if (anchor_i < 0) anchor_i = 0;
    if (anchor_i > rows - 1) anchor_i = rows - 1;

    for (int i = 0; i < rows; i++) {
        if (!s->row[i].on) continue; /* rendering was off: the per-dot pass drew this row */
        const int y = s->top + i;
        s_on[y] = 1;
        /* The two layers meet at s.first: above it the rows continue the
         * playfield - the split's staged writes have not landed yet - and are
         * drawn from their own capture; from it down, from the anchor until
         * the capture is the bar's own, then from their own. */
        const int own = (y < s->first || i >= anchor_i);
        draw_row(s_built, y, own ? y : s->settled, bar_fine_x, bar_col);
    }
}

/* Composite the built layer over the frame: copy the rows it drew, and nothing
 * else (hud_blit). Nothing is drawn above the split - the rows there are the
 * level's own, drawn by the per-dot pass. */
static void hud_blit(uint32_t *out)
{
    const int top = s_src.top;
    if (!s_cached) return;
    for (int y = top; y < NO_HUD;) {
        if (!s_on[y]) {
            y++;
            continue;
        }
        int end = y;
        while (end < NO_HUD && s_on[end]) end++;
        memcpy(out + y * BAR_W, s_built + y * BAR_W, (size_t)(end - y) * BAR_W * sizeof(uint32_t));
        y = end;
    }
}

void smb3_hud_compose(const uint32_t *native, uint32_t *out)
{
    memcpy(out, native, sizeof s_built);
    s_split = cyc_frame_split_line();
    if (s_split < 0) s_split = NO_HUD;
    if (s_split > NO_HUD) s_split = NO_HUD;
    s_first = -1;
    s_settled = -1;
    if (s_split >= NO_HUD) {
        /* No split this frame: no layer. Dropping the source key makes the
         * next frame with a split rebuild instead of blitting a stale one. */
        s_cached = 0;
        return;
    }

    const CycLine *lines = cyc_frame_lines();
    int first = bar_first(lines);
    if (first < s_split || first >= NO_HUD) first = s_split; /* no break: split at the IRQ */
    int settled = bar_settled(lines);
    if (settled < first) settled = first; /* anchor on a row the bar owns */
    s_first = first;
    s_settled = settled;

    if (!hud_matches(settled, first)) {
        hud_snapshot(settled, first);
        hud_build();
    }
    hud_blit(out);
}

const uint32_t *smb3_hud_present(const uint32_t *native)
{
    smb3_hud_compose(native, s_picture);
    return s_picture;
}
