/* Tengen RAMBO-1 (mapper 64) and Tengen 800037 (mapper 158).
 * https://www.nesdev.org/wiki/RAMBO-1, https://www.nesdev.org/wiki/INES_Mapper_158
 *
 * MMC3-like: $8000 (even) bank select [CPK. RRRR] - C inverts CHR A12, P swaps
 * the $8000 and $C000 PRG windows, K selects four 1 KiB banks (R0, R8, R1, R9)
 * instead of two 2 KiB banks in the lower pattern half, RRRR picks one of 16
 * registers written at $8001 (odd). R6/R7/RF are 8 KiB PRG banks, $E000 is
 * fixed to the last. In 2 KiB mode the low bit of R0/R1 is ignored and PPU A10
 * passes through. $A000 bit 0: 0 vertical, 1 horizontal (mapper 64 only; 158
 * wires CHR A17 to CIRAM A10 instead, like TxSROM).
 *
 * IRQ: $C000 latch; $C001 bit 0 selects scanline (0, the MMC3's filtered A12
 * rise) or CPU-cycle mode (1, one clock per 4 CPU cycles); a $C001 write also
 * requests a reload at the next clock and restarts the prescaler; $E000
 * disables and acknowledges, $E001 enables. A clock reloads if requested (the
 * latch ORed with 1 when non-zero), else reloads a zero counter from the
 * latch, else decrements; a zero counter with IRQs enabled asserts /IRQ two
 * CPU cycles after the A12 rise, or one cycle after the counting clock. */

static void rambo_apply(void)
{
    const uint8_t *r = hw_cart.m.rambo.reg;
    unsigned ctrl = hw_cart.m.bank_select, inv = (ctrl & 0x80) ? 4 : 0;
    map_prg8((ctrl & 0x40) ? 2 : 0, r[6]);
    map_prg8(1, r[7]);
    map_prg8((ctrl & 0x40) ? 0 : 2, r[15]);
    map_prg8(3, -1);
    uint8_t bank[8];
    if (ctrl & 0x20) { bank[0] = r[0]; bank[1] = r[8]; bank[2] = r[1]; bank[3] = r[9]; }
    else { bank[0] = r[0] & 0xfe; bank[1] = r[0] | 1; bank[2] = r[1] & 0xfe; bank[3] = r[1] | 1; }
    for (unsigned i = 0; i < 4; ++i) bank[4 + i] = r[2 + i];
    for (unsigned i = 0; i < 8; ++i) {
        map_chr1(i ^ inv, bank[i]);
        hw_cart.m.rambo.chr_a17[i ^ inv] = bank[i] >> 7;
    }
    if (hw_cart.mapper == 64)
        hw_cart.mirroring = (hw_cart.m.mirror_reg & 1) ? HW_MIRROR_HORIZONTAL : HW_MIRROR_VERTICAL;
}

static void rambo_write(uint16_t addr, uint8_t value)
{
    switch (addr & 0xe001) {
    case 0x8000: hw_cart.m.bank_select = value; break;
    case 0x8001: hw_cart.m.rambo.reg[hw_cart.m.bank_select & 15] = value; break;
    case 0xa000: hw_cart.m.mirror_reg = value; break;
    case 0xc000: hw_cart.m.irq_latch = value; return;
    case 0xc001:
        hw_cart.m.rambo.cycle_mode = value & 1;
        hw_cart.m.irq_reload = 1;
        hw_cart.m.rambo.prescaler = 0;
        return;
    case 0xe000:
        hw_cart.m.irq_enable = 0;
        hw_cart.m.irq_out = 0; hw_cart.m.rambo.delay = 0;
        return;
    case 0xe001: hw_cart.m.irq_enable = 1; return;
    default: return;
    }
    rambo_apply();
}

static void rambo_clock_irq(uint8_t delay)
{
    if (hw_cart.m.irq_reload) hw_cart.m.irq_counter = hw_cart.m.irq_latch ? (hw_cart.m.irq_latch | 1) : 0;
    else if (hw_cart.m.irq_counter == 0) hw_cart.m.irq_counter = hw_cart.m.irq_latch;
    else hw_cart.m.irq_counter--;
    hw_cart.m.irq_reload = 0;
    if (hw_cart.m.irq_counter == 0 && hw_cart.m.irq_enable && !hw_cart.m.rambo.delay && !hw_cart.m.irq_out)
        hw_cart.m.rambo.delay = delay;
}

static void rambo_ppu_addr(uint16_t vbus)
{
    if (!(vbus & 0x1000)) {
        if (hw_cart.m.a12) { hw_cart.m.a12 = 0; hw_cart.m.a12_low_cycle = hw.cycles; }
        return;
    }
    if (!hw_cart.m.a12) {
        hw_cart.m.a12 = 1;
        if (!hw_cart.m.rambo.cycle_mode && hw.cycles - hw_cart.m.a12_low_cycle >= MMC3_A12_FILTER_CYCLES)
            rambo_clock_irq(2);
    }
}

static void rambo_cpu_clock(void)
{
    if (hw_cart.m.rambo.delay && !--hw_cart.m.rambo.delay) hw_cart.m.irq_out = 1;
    if (hw_cart.m.rambo.cycle_mode && (hw_cart.m.rambo.prescaler = (hw_cart.m.rambo.prescaler + 1) & 3) == 0)
        rambo_clock_irq(1);
}

static uint16_t rambo158_ciram_a10(uint16_t addr)
{
    return hw_cart.m.rambo.chr_a17[(addr >> 10) & 3] ? 0x400 : 0;
}
