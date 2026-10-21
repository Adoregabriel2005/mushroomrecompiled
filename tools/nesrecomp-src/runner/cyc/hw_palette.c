/*
 * hw_palette.c - ARGB colors for the PPU's 9-bit color indices.
 *
 * Canonical 2C02 NTSC master palette (matches smb3pc's nes_palette.h, from
 * the NESdev / ROM Detectives tables). All eight emphasis banks return the
 * same 64 colors: like smb3pc, emphasis dimming is not modeled.
 *
 * The emulated machine never sees these values: comparisons and hashes use
 * the color indices.
 */
#include "hw_internal.h"

uint32_t hw_palette_argb[512];

/* 0xRRGGBB per NES color index. */
static const uint32_t kNesRgb[64] = {
    0x7C7C7C, 0x0000FC, 0x0000BC, 0x4428BC, 0x940084, 0xA80020, 0xA81000, 0x881400,
    0x503000, 0x007800, 0x006800, 0x005800, 0x004058, 0x000000, 0x000000, 0x000000,
    0xBCBCBC, 0x0078F8, 0x0058F8, 0x6844FC, 0xD800CC, 0xE40058, 0xF83800, 0xE45C10,
    0xAC7C00, 0x00B800, 0x00A800, 0x00A844, 0x008888, 0x000000, 0x000000, 0x000000,
    0xF8F8F8, 0x3CBCFC, 0x6888FC, 0x9878F8, 0xF878F8, 0xF85898, 0xF87858, 0xFCA044,
    0xF8B800, 0xB8F818, 0x58D854, 0x58F898, 0x00E8D8, 0x787878, 0x000000, 0x000000,
    0xFCFCFC, 0xA4E4FC, 0xB8B8F8, 0xD8B8F8, 0xF8B8F8, 0xF8A4C0, 0xF0D0B0, 0xFCE0A8,
    0xF8D878, 0xD8F878, 0xB8F8B8, 0xB8F8D8, 0x00FCFC, 0xF8D8F8, 0x000000, 0x000000,
};

void hw_palette_init(void)
{
    for (int index = 0; index < 512; index++) {
        uint32_t c = kNesRgb[index & 0x3F];
        uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        hw_palette_argb[index] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}
