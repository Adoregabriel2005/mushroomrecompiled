/* Taito X1-005 (mappers 80, 207) and X1-017 (mappers 82, 552).
 * https://www.nesdev.org/wiki/INES_Mapper_080, INES_Mapper_207,
 * INES_Mapper_082 (which also documents NES 2.0 mapper 552).
 *
 * Registers at $7EF0-$7EFF. Both: $7EF0/$7EF1 2 KiB CHR banks (bit 0
 * ignored), $7EF2-$7EF5 1 KiB CHR banks, three 8 KiB PRG banks plus the fixed
 * last bank, and RAM inside the chip unlocked by magic values.
 *
 * X1-005: $7EF6 bit 0 mirroring (0: CIRAM A10 = PPU A11, 1: = PPU A10; not
 * mirrored at $7EF7); $7EF8/$7EF9 = $A3 unlocks 128 bytes at $7F00-$7F7F,
 * mirrored at $7F80; PRG at $7EFA/B, $7EFC/D, $7EFE/F (6 bits). Mapper 207
 * ignores $7EF6: bit 7 of $7EF0 selects CIRAM A10 for the upper nametables and
 * bit 7 of $7EF1 for the lower ones.
 *
 * X1-017: $7EF6 bit 0 mirroring, bit 1 swaps the 2 KiB and 1 KiB halves;
 * $7EF7 = $CA, $7EF8 = $69, $7EF9 = $84 unlock 2 KiB at $6000, 2 KiB at $6800
 * and 1 KiB at $7000 (5 KiB). PRG at $7EFA-$7EFC: mapper 82 uses bits 2-5 as
 * A13-A16; mapper 552 wires bits 0-5 to A18-A13 (reversed). The IRQ counter at
 * $7EFD-$7EFF is not modeled: its clock and firing condition are undocumented
 * and no commercial game uses it.
 *
 * reg[0-5] CHR, ctrl = $7EF6, vrc_chr[0-2] = RAM unlock values,
 * prg[] slots in reg[6-7] and chr0. */

static bool taito_x1017(void) { return hw_cart.mapper == 82 || hw_cart.mapper == 552; }

static unsigned x1017_prg(uint8_t value)
{
    if (hw_cart.mapper == 82) return (value >> 2) & 15;
    unsigned page = 0;
    for (unsigned bit = 0; bit < 6; ++bit) page |= ((value >> bit) & 1u) << (5 - bit);
    return page;
}

static void taito_x1_apply(void)
{
    const uint8_t *r = hw_cart.m.reg;
    uint8_t prg[3] = { hw_cart.m.reg[6], hw_cart.m.reg[7], hw_cart.m.chr0 };
    for (unsigned slot = 0; slot < 3; ++slot)
        map_prg8(slot, (int)(taito_x1017() ? x1017_prg(prg[slot]) : prg[slot] & 0x3fu));
    map_prg8(3, -1);
    unsigned big = taito_x1017() && (hw_cart.m.ctrl & 2) ? 2 : 0;   /* 2 KiB pair page */
    map_chr2(big + 0, r[0] >> 1);
    map_chr2(big + 1, r[1] >> 1);
    for (unsigned i = 0; i < 4; ++i) map_chr1((big ? 0 : 4) + i, r[2 + i]);
    if (hw_cart.mapper != 207)
        hw_cart.mirroring = (hw_cart.m.ctrl & 1) ? HW_MIRROR_VERTICAL : HW_MIRROR_HORIZONTAL;
}

static void taito_x1_write(uint16_t addr, uint8_t value)
{
    if (addr < 0x7ef0 || addr > 0x7eff) return;
    unsigned port = addr & 15;
    if (port < 6) hw_cart.m.reg[port] = value;
    else if (taito_x1017()) {
        if (port == 6) hw_cart.m.ctrl = value;
        else if (port <= 9) hw_cart.m.vrc_chr[port - 7] = value;
        else if (port == 10) hw_cart.m.reg[6] = value;
        else if (port == 11) hw_cart.m.reg[7] = value;
        else if (port == 12) hw_cart.m.chr0 = value;
        /* $7EFD-$7EFF: unmodeled IRQ */
    } else {
        if (port == 6) hw_cart.m.ctrl = value;
        else if (port == 8 || port == 9) hw_cart.m.vrc_chr[0] = value;
        else if (port >= 10) {
            unsigned slot = (port - 10) >> 1;
            if (slot == 0) hw_cart.m.reg[6] = value;
            else if (slot == 1) hw_cart.m.reg[7] = value;
            else hw_cart.m.chr0 = value;
        }
    }
    taito_x1_apply();
}

/* CIRAM A10 for mapper 207: bit 7 of $7EF0 (upper) or $7EF1 (lower). */
static uint16_t x1005_207_ciram_a10(uint16_t addr)
{
    return (hw_cart.m.reg[((addr >> 11) & 1)] & 0x80) ? 0x400 : 0;
}

/* The chip RAM: an index into wram, or -1 when locked or unmapped. */
static int taito_x1_ram(uint16_t addr)
{
    if (!taito_x1017()) {
        if (addr < 0x7f00 || hw_cart.m.vrc_chr[0] != 0xa3) return -1;
        return addr & 0x7f;
    }
    if (addr >= 0x7400) return -1;
    static const uint8_t key[3] = { 0xca, 0x69, 0x84 };
    unsigned chip = addr < 0x6800 ? 0 : addr < 0x7000 ? 1 : 2;
    return hw_cart.m.vrc_chr[chip] == key[chip] ? (int)(addr - 0x6000) : -1;
}
