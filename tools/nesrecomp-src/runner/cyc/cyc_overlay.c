/*
 * cyc_overlay.c - presentation-only drawing (cyc_overlay.h).
 */
#include "cyc_overlay.h"

#include <string.h>

static const char GLYPH_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.:/()=+!?,'";
static const uint8_t GLYPHS[][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6},
    {3, 4, 4, 4, 3}, {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4}, {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5},
    {7, 2, 2, 2, 7}, {1, 1, 1, 5, 2}, {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7}, {5, 7, 7, 5, 5}, {6, 5, 5, 5, 5},
    {2, 5, 5, 5, 2}, {6, 5, 6, 4, 4}, {2, 5, 5, 6, 3}, {6, 5, 6, 5, 5}, {3, 4, 2, 1, 6}, {7, 2, 2, 2, 2},
    {5, 5, 5, 5, 7}, {5, 5, 5, 5, 2}, {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5}, {5, 5, 2, 2, 2}, {7, 1, 2, 4, 7},
    {0, 0, 7, 0, 0}, {0, 0, 0, 0, 2}, {0, 2, 0, 2, 0}, {1, 1, 2, 4, 4}, {1, 2, 2, 2, 1}, {4, 2, 2, 2, 4},
    {0, 7, 0, 7, 0}, {0, 2, 7, 2, 0}, {2, 2, 2, 0, 2}, {7, 1, 3, 0, 2}, {0, 0, 0, 2, 4}, {2, 2, 0, 0, 0},
};

const uint8_t *cyc_overlay_glyph(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    const char *at = c == ' ' || !c ? NULL : strchr(GLYPH_CHARS, c);
    return at ? GLYPHS[at - GLYPH_CHARS] : NULL;
}

static void fill(uint32_t *dst, int width, int height, int x, int y, int w, int h, uint32_t color, unsigned alpha)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) {
            uint32_t *p = &dst[yy * width + xx];
            if (alpha >= 255) { *p = 0xFF000000u | color; continue; }
            uint32_t d = *p, r = 0;
            for (int s = 0; s < 24; s += 8) {
                uint32_t dc = (d >> s) & 255, sc = (color >> s) & 255;
                r |= ((sc * alpha + dc * (255 - alpha)) / 255) << s;
            }
            *p = 0xFF000000u | r;
        }
}

int cyc_overlay_text(uint32_t *dst, int width, int height, int x, int y, int scale, uint32_t color, const char *s)
{
    for (; *s; ++s, x += 4 * scale) {
        const uint8_t *g = cyc_overlay_glyph(*s);
        if (!g) continue;
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 3; ++c)
                if (g[r] & (4 >> c)) fill(dst, width, height, x + c * scale, y + r * scale, scale, scale, color, 255);
    }
    return x;
}

void cyc_overlay_toast(uint32_t *dst, int width, int height, const char *title, const char *body)
{
    const char *lines[8];
    size_t lens[8];
    int count = 0;
    if (title && *title) { lines[count] = title; lens[count++] = strlen(title); }
    for (const char *p = body; p && *p && count < 8;) {
        const char *nl = strchr(p, '\n');
        lines[count] = p;
        lens[count++] = nl ? (size_t)(nl - p) : strlen(p);
        p = nl ? nl + 1 : NULL;
    }
    if (!count) return;
    size_t widest = 0;
    for (int i = 0; i < count; ++i)
        if (lens[i] > widest) widest = lens[i];
    const int pad = 4, row = 8;
    int w = (int)widest * 4 - 1 + pad * 2, h = count * row - 3 + pad * 2;
    if (w > width - 4) w = width - 4;
    int x = (width - w) / 2, y = 6;
    fill(dst, width, height, x, y, w, h, 0x181820, 228);
    fill(dst, width, height, x, y, w, 1, 0xFFD25A, 255);
    fill(dst, width, height, x, y + h - 1, w, 1, 0xFFD25A, 255);
    for (int i = 0; i < count; ++i) {
        char buf[96];
        size_t n = lens[i] < sizeof(buf) - 1 ? lens[i] : sizeof(buf) - 1;
        memcpy(buf, lines[i], n);
        buf[n] = 0;
        cyc_overlay_text(dst, width, height, x + pad, y + pad + i * row, 1,
                         i == 0 && title && *title ? 0xFFD25A : 0xE6E6E6, buf);
    }
}
