/* Taito TC0190 (mapper 33) and TC0690 (mapper 48).
 * https://www.nesdev.org/wiki/INES_Mapper_033
 * https://www.nesdev.org/wiki/INES_Mapper_048
 *
 * Both: $8000/$8001 8 KiB PRG banks (6 bits) with the last 16 KiB fixed,
 * $8002/$8003 2 KiB CHR banks (the value counts 2 KiB units), $A000-$A003
 * 1 KiB CHR banks. TC0190 decodes A14 away ($C000 mirrors $8000) and keeps
 * mirroring in $8000 bit 6. TC0690 adds an MMC3-style A12 scanline counter:
 * $C000 latch (written inverted), $C001 reload, $C002 enable, $C003 disable
 * and acknowledge, with the IRQ output 22 CPU cycles after the MMC3 would
 * assert it (see TC0690_IRQ_DELAY); mirroring moves to $E000 bit 6.
 * reg[0-1] PRG, reg[2-7] CHR. */

static void taito_apply(void)
{
    const uint8_t *r = hw_cart.m.reg;
    map_prg8(0, r[0] & 0x3f); map_prg8(1, r[1] & 0x3f);
    map_prg8(2, -2); map_prg8(3, -1);
    map_chr2(0, r[2]); map_chr2(1, r[3]);
    for (unsigned i = 0; i < 4; ++i) map_chr1(4 + i, r[4 + i]);
    hw_cart.mirroring = (hw_cart.m.mirror_reg & 0x40) ? HW_MIRROR_HORIZONTAL : HW_MIRROR_VERTICAL;
}

static void taito_write(uint16_t addr, uint8_t value)
{
    if (hw_cart.mapper == 33) addr &= 0xa003;
    else addr &= 0xe003;
    switch (addr) {
    case 0x8000:
        hw_cart.m.reg[0] = value;
        if (hw_cart.mapper == 33) hw_cart.m.mirror_reg = value;
        break;
    case 0x8001: hw_cart.m.reg[1] = value; break;
    case 0x8002: hw_cart.m.reg[2] = value; break;
    case 0x8003: hw_cart.m.reg[3] = value; break;
    case 0xa000: case 0xa001: case 0xa002: case 0xa003: hw_cart.m.reg[4 + (addr & 3)] = value; break;
    case 0xc000: hw_cart.m.irq_latch = (uint8_t)(value ^ 0xff); break;
    case 0xc001: hw_cart.m.irq_counter = 0; hw_cart.m.irq_reload = 1; break;
    case 0xc002: hw_cart.m.irq_enable = 1; break;
    case 0xc003:
        hw_cart.m.irq_enable = 0;
        hw_cart.m.irq_out = 0; hw_cart.m.latch = 0;     /* also drops a pending delayed IRQ */
        break;
    case 0xe000: hw_cart.m.mirror_reg = value; break;
    default: return;
    }
    taito_apply();
}

/* The TC0690 counter clocks like the MMC3's (mmc3_ppu_addr); when it would
 * assert, a delay of TC0690_IRQ_DELAY CPU cycles (m.latch) starts instead. */
static void tc0690_cpu_clock(void)
{
    if (hw_cart.m.latch && !--hw_cart.m.latch) hw_cart.m.irq_out = 1;
}
