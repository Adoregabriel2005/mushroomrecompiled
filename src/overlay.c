#include "overlay.h"

#include <stdio.h>
#include <string.h>

/* Colours follow smb3pc's NES palette choices. The launcher's framebuffer is
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

static void draw_gametext(OverlayTarget t, int x, int y, const char *text, const uint8_t *chr) {
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
                draw_tile(t, gx, y, chr, ft, K_PAPER, 0, 0, 0);
            }
        }
    }
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

/* The six-row panel is 30 tiles wide and 21 tiles high. The border uses the
 * same world-map status-strip tiles as smb3pc, mirrored on the left. */
#define PANEL_W 240
#define PANEL_H 168
#define PANEL_Y ((240 - PANEL_H) / 2)

static void draw_panel(OverlayTarget t, int x0, int y0, uint32_t fill, const uint8_t *chr) {
    const FontTile top = {0x5E, 0x21}, bottom = {0x5E, 0x24};
    const FontTile left = {0x5E, 0x26}, right = {0x5E, 0x27};
    const FontTile top_corner = {0x5E, 0x22}, bottom_corner = {0x5E, 0x25};
    fill_rect(t, x0, y0, PANEL_W, PANEL_H, fill);
    for (int x = x0 + 8; x < x0 + PANEL_W - 8; x += 8) {
        draw_tile(t, x, y0, chr, top, K_PAPER, 0, 0, 1);
        draw_tile(t, x, y0 + PANEL_H - 8, chr, bottom, K_PAPER, 0, 0, 1);
    }
    for (int y = y0 + 8; y < y0 + PANEL_H - 8; y += 8) {
        draw_tile(t, x0, y, chr, left, K_PAPER, 0, 0, 1);
        draw_tile(t, x0 + PANEL_W - 8, y, chr, right, K_PAPER, 0, 0, 1);
    }
    draw_tile(t, x0, y0, chr, top_corner, K_PAPER, 0, 1, 1);
    draw_tile(t, x0 + PANEL_W - 8, y0, chr, top_corner, K_PAPER, 0, 0, 1);
    draw_tile(t, x0, y0 + PANEL_H - 8, chr, bottom_corner, K_PAPER, 0, 1, 1);
    draw_tile(t, x0 + PANEL_W - 8, y0 + PANEL_H - 8, chr, bottom_corner, K_PAPER, 0, 0, 1);
}

void overlay_menu(OverlayTarget t, int selected, const int on[5], int slot, const uint8_t *chr) {
    static const char *labels[5] = {
        "RESUME", "SAVE STATE", "LOAD STATE", "FULLSCREEN", "QUIT"
    };
    const int x0 = (t.w - PANEL_W) / 2;
    const int y0 = PANEL_Y;
    draw_panel(t, x0, y0, K_PEACH, chr);
    draw_gametext(t, x0 + (PANEL_W - gametext_width("PAUSED")) / 2, y0 + 10, "PAUSED", chr);

    const int label_x = 24, cursor_x = 12, icon_x = 198;
    for (int i = 0; i < 5; i++) {
        int y = y0 + 28 + i * 14;
        draw_gametext(t, x0 + label_x, y, labels[i], chr);
        if (on && on[i]) draw_check(t, x0 + icon_x, y, K_BLACK);
        else if (selected == i) draw_cross(t, x0 + icon_x, y, K_PEACH_DIM);
    }
    draw_cursor(t, x0 + cursor_x, y0 + 28 + selected * 14 - 1);

    char text[40];
    snprintf(text, sizeof text, "SLOT %d", slot < 1 ? 1 : slot > 5 ? 5 : slot);
    draw_gametext(t, x0 + 16, y0 + 116, text, chr);
    draw_gametext(t, x0 + 16, y0 + 128, "F5 SAVE  F7 LOAD", chr);
    draw_gametext(t, x0 + 16, y0 + 140, "1-5 SLOT  F11 FULL", chr);
    draw_gametext(t, x0 + 16, y0 + 152, "ESC CLOSE", chr);
}


