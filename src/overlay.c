#include "overlay.h"

#include <stdio.h>
#include <string.h>

/* Colours follow the project's NES palette choices. The launcher's framebuffer is
 * ARGB8888 (R in the high byte), so the word layout differs from the donor's
 * R-in-low-byte buffer while the visible colours are identical. */
static uint32_t rgb(uint32_t r, uint32_t g, uint32_t b) {
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
#define K_BLACK rgb(0x00, 0x00, 0x00)
#define K_SELECT rgb(0xF8, 0x38, 0x00)
#define K_PEACH rgb(0xF0, 0xD0, 0xB0)
#define K_PEACH_DIM rgb(0x78, 0x68, 0x58)
#define K_PAPER rgb(0xFC, 0xFC, 0xFC)
#define K_CYAN rgb(0x00, 0xD8, 0xE8)

typedef struct { uint8_t bank, tile; } FontTile;

static void put(OverlayTarget t, int x, int y, uint32_t color) {
    if (x >= 0 && x < t.w && y >= 0 && y < t.h) t.fb[(size_t)y * t.w + x] = color;
}

static void fill_rect(OverlayTarget t, int x, int y, int w, int h, uint32_t color) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) put(t, xx, yy, color);
}

/* SMB3's dialog/status font in CHR banks $5E/$5F. */
static int font_tile(char c, FontTile *t) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'P') { t->bank = 0x5E; t->tile = (uint8_t)(48 + (c - 'A')); return 1; }
    if (c >= 'Q' && c <= 'X') { t->bank = 0x5F; t->tile = (uint8_t)(c - 'Q'); return 1; }
    if (c == 'Y') { t->bank = 0x5F; t->tile = 8; return 1; }
    if (c == 'Z') { t->bank = 0x5F; t->tile = 9; return 1; }
    if (c >= '0' && c <= '9') { t->bank = 0x5F; t->tile = (uint8_t)(48 + (c - '0')); return 1; }
    return 0;
}

/* Blit one 8x8 2bpp tile. Value 3 is transparent unless opaque is set; text
 * uses the outlined values (0/2), the border uses every nonzero value. */
static void draw_tile(OverlayTarget t, int x, int y, const uint8_t *chr, FontTile ft,
                      uint32_t color, int solid, int flip, int opaque) {
    if (!chr) return;
    const uint8_t *g = chr + (size_t)ft.bank * 1024 + (size_t)ft.tile * 16;
    for (int row = 0; row < 8; row++) {
        uint8_t lo = g[row], hi = g[row + 8];
        for (int col = 0; col < 8; col++) {
            int sc = flip ? col : 7 - col;
            unsigned v = (((hi >> sc) & 1) << 1) | ((lo >> sc) & 1);
            if (v == 3 || (!solid && v == 1)) {
                if (opaque) put(t, x + col, y + row, K_BLACK);
                continue;
            }
            put(t, x + col, y + row, color);
        }
    }
}

#define ADVANCE 9

static void draw_gametext_color(OverlayTarget t, int x, int y, const char *text,
                                const uint8_t *chr, uint32_t color) {
    if (!chr) return;
    for (int pass = 0; pass < 2; pass++) {
        int gx = x;
        for (const char *c = text; *c; c++, gx += ADVANCE) {
            FontTile ft;
            if (!font_tile(*c, &ft)) continue;
            if (pass == 0) {
                const uint8_t *g = chr + (size_t)ft.bank * 1024 + (size_t)ft.tile * 16;
                for (int row = 0; row < 8; row++) {
                    uint8_t lo = g[row], hi = g[row + 8];
                    for (int col = 0; col < 8; col++) {
                        unsigned v = (((hi >> (7 - col)) & 1) << 1) | ((lo >> (7 - col)) & 1);
                        if (v == 3 || v == 1) continue;
                        for (int dy = -1; dy <= 1; dy++)
                            for (int dx = -1; dx <= 1; dx++)
                                put(t, gx + col + dx, y + row + dy, K_BLACK);
                    }
                }
            } else {
                draw_tile(t, gx, y, chr, ft, color, 0, 0, 0);
            }
        }
    }
}

static void draw_gametext(OverlayTarget t, int x, int y, const char *text, const uint8_t *chr) {
    draw_gametext_color(t, x, y, text, chr, K_PAPER);
}

static int gametext_width(const char *text) { return (int)strlen(text) * ADVANCE; }

static void put_mark(OverlayTarget t, int x, int y, uint32_t color) {
    put(t, x, y, color);
    put(t, x + 1, y, color);
}

static void draw_check(OverlayTarget t, int x, int y, uint32_t color) {
    for (int i = 0; i < 3; i++) put_mark(t, x + 1 + i, y + 4 + i, color);
    for (int i = 0; i < 4; i++) put_mark(t, x + 3 + i, y + 6 - i, color);
}

static void draw_cross(OverlayTarget t, int x, int y, uint32_t color) {
    for (int i = 0; i < 6; i++) {
        put_mark(t, x + 1 + i, y + 1 + i, color);
        put_mark(t, x + 5 - i, y + 1 + i, color);
    }
}

static void draw_cursor(OverlayTarget t, int x, int y) {
    for (int row = 0; row < 8; row++) {
        int half = row < 4 ? row : 7 - row;
        for (int col = 0; col <= half + 1; col++)
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    put(t, x + col + dx, y + row + dy, K_BLACK);
    }
    for (int row = 0; row < 8; row++) {
        int half = row < 4 ? row : 7 - row;
        for (int col = 0; col <= half + 1; col++)
            put(t, x + col, y + row, K_SELECT);
    }
}

/* The menu panel is 41 tiles wide with a dynamic height; the border uses
 * the same world-map status-strip tiles, mirrored on the left. */
#define MENU_PANEL_W 328

static int tab_row_count(int tab) {
    switch (tab) {
        case TAB_VIDEO: return 4;
        case TAB_AUDIO: return 1;
        case TAB_GAME: return 3;
        case TAB_CONTROLS: return 8;
        case TAB_SYSTEM: return 2;
        default: return 0;
    }
}

static const char *menu_row_label(int tab, int row) {
    switch (tab) {
        case TAB_VIDEO:
            if (row == 0) return "FULLSCREEN";
            if (row == 1) return "WIDESCREEN";
            if (row == 2) return "FPS COUNTER";
            return "SCANLINES";
        case TAB_AUDIO:
            return "VOLUME";
        case TAB_GAME:
            if (row == 0) return "SAVE STATE";
            if (row == 1) return "LOAD STATE";
            return "SLOT";
        case TAB_CONTROLS:
            if (row == 0) return "A";
            if (row == 1) return "B";
            if (row == 2) return "SELECT";
            if (row == 3) return "START";
            if (row == 4) return "UP";
            if (row == 5) return "DOWN";
            if (row == 6) return "LEFT";
            return "RIGHT";
        default:
            return row == 0 ? "RESUME" : "QUIT";
    }
}

/* Small left/right triangles flanking an adjustable value. */
static void draw_arrow(OverlayTarget t, int x, int y, int dir, uint32_t color) {
    for (int row = 0; row < 7; row++) {
        int half = row < 4 ? row : 6 - row;
        for (int i = 0; i <= half; i++)
            put(t, x + (dir > 0 ? i : -i), y + row, color);
    }
}

static void draw_panel(OverlayTarget t, int x0, int y0, int w, int h, uint32_t fill,
                       const uint8_t *chr) {
    const FontTile top = {0x5E, 0x21}, bottom = {0x5E, 0x24};
    const FontTile left = {0x5E, 0x26}, right = {0x5E, 0x27};
    const FontTile top_corner = {0x5E, 0x22}, bottom_corner = {0x5E, 0x25};
    fill_rect(t, x0, y0, w, h, fill);
    for (int x = x0 + 8; x < x0 + w - 8; x += 8) {
        draw_tile(t, x, y0, chr, top, K_PAPER, 0, 0, 1);
        draw_tile(t, x, y0 + h - 8, chr, bottom, K_PAPER, 0, 0, 1);
    }
    for (int y = y0 + 8; y < y0 + h - 8; y += 8) {
        draw_tile(t, x0, y, chr, left, K_PAPER, 0, 0, 1);
        draw_tile(t, x0 + w - 8, y, chr, right, K_PAPER, 0, 0, 1);
    }
    draw_tile(t, x0, y0, chr, top_corner, K_PAPER, 0, 1, 1);
    draw_tile(t, x0 + w - 8, y0, chr, top_corner, K_PAPER, 0, 0, 1);
    draw_tile(t, x0, y0 + h - 8, chr, bottom_corner, K_PAPER, 0, 1, 1);
    draw_tile(t, x0 + w - 8, y0 + h - 8, chr, bottom_corner, K_PAPER, 0, 0, 1);
}

void overlay_fps(OverlayTarget t, int fps, const uint8_t *chr) {
    char text[16];
    snprintf(text, sizeof text, "FPS %d", fps);
    /* Top-left corner of the picture: inside the 4:3 overscan crop, on the
     * added left columns in widescreen. No backing box: the stroked glyphs
     * stay readable over gameplay on their own. */
    draw_gametext(t, 12, 11, text, chr);
}

static const char *tab_names[TAB_COUNT] = {"VIDEO", "AUDIO", "GAME", "CONTROLS", "SYSTEM"};

void overlay_menu(OverlayTarget t, int tab, int selected, const MenuState *st,
                  const uint8_t *chr) {
    if (tab < 0 || tab >= TAB_COUNT) tab = 0;
    int rows = tab_row_count(tab);
    /* Narrow frames (native 4:3) get a slimmer panel with a two-row tab
     * bar so nothing is cut off at the sides. */
    int w = t.w >= MENU_PANEL_W + 8 ? MENU_PANEL_W : (t.w - 8) & ~7;
    if (w < 64) w = 64;
    int compact = w < MENU_PANEL_W;
    int rows_y = compact ? 54 : 42;
    int h = (rows_y + rows * 12 + 11) & ~7;
    const int x0 = (t.w - w) / 2;
    const int y0 = (240 - h) / 2;
    draw_panel(t, x0, y0, w, h, K_PEACH, chr);
    draw_gametext(t, x0 + (w - gametext_width("PAUSED")) / 2, y0 + 8,
                  "PAUSED", chr);

    int tx;
    if (!compact) {
        int bar_w = 0;
        for (int i = 0; i < TAB_COUNT; i++) bar_w += gametext_width(tab_names[i]);
        bar_w += (TAB_COUNT - 1) * ADVANCE;
        tx = x0 + (w - bar_w) / 2;
        for (int i = 0; i < TAB_COUNT; i++) {
            draw_gametext_color(t, tx, y0 + 24, tab_names[i], chr,
                                i == tab ? K_SELECT : K_PAPER);
            tx += gametext_width(tab_names[i]) + ADVANCE;
        }
    } else {
        /* Two centred rows: VIDEO AUDIO GAME / CONTROLS SYSTEM. */
        int top_w = gametext_width("VIDEO") + gametext_width("AUDIO") +
                    gametext_width("GAME") + 2 * ADVANCE;
        int bot_w = gametext_width("CONTROLS") + gametext_width("SYSTEM") + ADVANCE;
        tx = x0 + (w - top_w) / 2;
        for (int i = 0; i < 3; i++) {
            draw_gametext_color(t, tx, y0 + 24, tab_names[i], chr,
                                i == tab ? K_SELECT : K_PAPER);
            tx += gametext_width(tab_names[i]) + ADVANCE;
        }
        tx = x0 + (w - bot_w) / 2;
        for (int i = 3; i < TAB_COUNT; i++) {
            draw_gametext_color(t, tx, y0 + 36, tab_names[i], chr,
                                i == tab ? K_SELECT : K_PAPER);
            tx += gametext_width(tab_names[i]) + ADVANCE;
        }
    }

    const int label_x = 24, cursor_x = 12;
    const int icon_x = x0 + w - 62;
    const int value_right = x0 + w - 28;
    for (int r = 0; r < rows; r++) {
        int y = y0 + rows_y + r * 12;
        draw_gametext(t, x0 + label_x, y, menu_row_label(tab, r), chr);
        char value[32] = "";
        int is_toggle = 0, toggle_on = 0, is_value = 0, is_binding = 0;
        if (st) {
            if (tab == TAB_VIDEO) {
                if (r == 0) { is_toggle = 1; toggle_on = st->fullscreen; }
                else if (r == 1) { is_toggle = 1; toggle_on = st->widescreen; }
                else if (r == 2) { is_toggle = 1; toggle_on = st->show_fps; }
                else {
                    is_value = 1;
                    snprintf(value, sizeof value, "%s",
                             st->scanlines ? (st->scanlines > 1 ? "2" : "1") : "OFF");
                }
            } else if (tab == TAB_AUDIO) {
                is_value = 1;
                snprintf(value, sizeof value, "%d", st->volume);
            } else if (tab == TAB_GAME) {
                if (r == 2) {
                    is_value = 1;
                    snprintf(value, sizeof value, "%d", st->slot);
                }
            } else if (tab == TAB_CONTROLS && r >= 0 && r < 8) {
                is_binding = 1;
                if (st->capture_row == r) snprintf(value, sizeof value, "PRESS KEY");
                else snprintf(value, sizeof value, "%s", st->key_names[r]);
            }
        }
        if (is_toggle) {
            if (toggle_on) draw_check(t, icon_x, y, K_BLACK);
            else if (selected == r) draw_cross(t, icon_x, y, K_PEACH_DIM);
        } else if (is_value || is_binding) {
            int vx = value_right - gametext_width(value);
            if (is_binding && st && st->capture_row == r)
                draw_gametext_color(t, vx, y, value, chr, K_SELECT);
            else
                draw_gametext(t, vx, y, value, chr);
            if (is_value && selected == r) {
                draw_arrow(t, vx - 9, y + 1, -1, K_BLACK);
                draw_arrow(t, vx + gametext_width(value) + 3, y + 1, 1, K_BLACK);
            }
        }
        if (selected == r)
            draw_cursor(t, x0 + cursor_x, y - 1);
    }
}


