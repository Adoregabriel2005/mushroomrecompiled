/* Sunsoft FME-7 / 5A / 5B (mapper 69) and the 5B's audio.
 * https://www.nesdev.org/wiki/Sunsoft_FME-7
 * https://www.nesdev.org/wiki/Sunsoft_5B_audio
 *
 * $8000 selects one of 16 commands and $A000 writes its parameter: eight 1 KiB
 * CHR banks, the $6000 window (ROM, or RAM with an enable bit, else open bus),
 * three 8 KiB PRG banks, mirroring, and a 16-bit IRQ counter that decrements
 * every CPU cycle while enabled and interrupts when it wraps $0000 -> $FFFF.
 * Only a write to the IRQ control command acknowledges the interrupt. The 5B
 * adds a YM2149F-derived sound generator at $C000 (address) / $E000 (data). */

static void fme7_apply(void)
{
    const HwFme7 *f = &hw_cart.m.fme7;
    for (unsigned page = 0; page < 8; ++page) map_chr1(page, f->chr[page]);
    for (unsigned slot = 0; slot < 3; ++slot) map_prg8(slot, f->prg[slot + 1] & 0x3f);
    map_prg8(3, -1);
    static const uint8_t mirror[4] = { HW_MIRROR_VERTICAL, HW_MIRROR_HORIZONTAL,
                                       HW_MIRROR_SCREEN_A, HW_MIRROR_SCREEN_B };
    hw_cart.mirroring = mirror[f->mirror & 3];
    /* Command 8: bit 6 selects RAM, bit 7 enables it; bits 0-5 bank either. */
    unsigned bank = f->prg[0] & 0x3f;
    hw_cart.wram_bank = hw_cart.wram_len ? (bank * 8192u) % hw_cart.wram_len : 0;
    hw_cart.wram_readable = hw_cart.wram_writable = (f->prg[0] & 0xc0) == 0xc0;
}

/* The 5B's logarithmic DAC: 1.5 dB per step of the 5-bit level; levels 0 and
 * 1 are silent. A 4-bit volume v is level 2v+1. One channel at full scale is
 * mixed at the nominal level of one 2A03 pulse at volume 15, like VRC6; the
 * cartridge amplifier's gain, nonlinearity and filtering are not modeled. */
static double s5b_amplitude[32];

static void s5b_reset(void)
{
    Hw5B *s = &hw_cart.m.s5b;
    memset(s, 0, sizeof(*s));
    s->lfsr = 1;  /* any nonzero seed; the chip's power-on value is unspecified */
    if (s5b_amplitude[31] == 0)
        for (unsigned level = 2; level < 32; ++level)
            s5b_amplitude[level] = pow(10.0, ((double)level - 31.0) * 1.5 / 20.0);
}

static unsigned s5b_period(unsigned value) { return value ? value : 1; }

static void s5b_envelope_restart(void)
{
    Hw5B *s = &hw_cart.m.s5b;
    s->env_count = 0;
    s->env_hold = 0;
    s->env_up = (s->reg[13] & 4) != 0;       /* attack */
    s->env_level = s->env_up ? 0 : 31;
}

static void s5b_write(uint16_t addr, uint8_t value)
{
    Hw5B *s = &hw_cart.m.s5b;
    if (addr < 0xe000) { s->address = value; return; }
    /* A nonzero upper nibble deselects the chip, as on the YM2149F. */
    if (s->address & 0xf0) return;
    s->reg[s->address] = value;
    if (s->address == 13) s5b_envelope_restart();
}

/* End of one 32-step envelope segment (register 13: continue, attack,
 * alternate, hold). Without continue the output falls to 0 and holds. */
static void s5b_envelope_step(void)
{
    Hw5B *s = &hw_cart.m.s5b;
    if (s->env_hold) return;
    if (s->env_up ? s->env_level < 31 : s->env_level > 0) {
        s->env_level += s->env_up ? 1 : -1;
        return;
    }
    uint8_t shape = s->reg[13];
    if (!(shape & 8)) { s->env_level = 0; s->env_hold = 1; return; }
    if (shape & 1) {
        /* Hold at the segment's end, inverted by alternate. */
        bool high = s->env_up != ((shape & 2) != 0);
        s->env_level = high ? 31 : 0;
        s->env_hold = 1;
    } else if (shape & 2) {
        s->env_up = !s->env_up;             /* the peak value repeats once */
    } else {
        s->env_level = s->env_up ? 0 : 31;  /* sawtooth restart */
    }
}

/* One CPU cycle. Tone outputs toggle every 16*P cycles (a square of CPU/32P),
 * the envelope steps every 16*E cycles, and the 17-bit noise LFSR (taps 16 and
 * 13) shifts every 32*N cycles. A period of 0 behaves as 1. */
static void s5b_clock(void)
{
    Hw5B *s = &hw_cart.m.s5b;
    if (++s->prescale < 16) return;
    s->prescale = 0;
    for (unsigned ch = 0; ch < 3; ++ch) {
        unsigned period = s5b_period(s->reg[ch * 2] | ((s->reg[ch * 2 + 1] & 15) << 8));
        if (++s->tone_count[ch] >= period) { s->tone_count[ch] = 0; s->tone_out ^= (uint8_t)(1 << ch); }
    }
    if (++s->env_count >= s5b_period(s->reg[11] | (s->reg[12] << 8))) {
        s->env_count = 0;
        s5b_envelope_step();
    }
    s->noise_half ^= 1;
    if (!s->noise_half && ++s->noise_count >= s5b_period(s->reg[6] & 31)) {
        s->noise_count = 0;
        uint32_t bit = (s->lfsr ^ (s->lfsr >> 3)) & 1;
        s->lfsr = (s->lfsr >> 1) | (bit << 16);
    }
}

static double s5b_output(void)
{
    const Hw5B *s = &hw_cart.m.s5b;
    double sum = 0;
    for (unsigned ch = 0; ch < 3; ++ch) {
        /* Register 7: a set bit disables tone/noise; the channel is on when
         * every enabled source is high (both disabled: constant on). */
        bool tone = (s->reg[7] >> ch & 1) || (s->tone_out >> ch & 1);
        bool noise = (s->reg[7] >> (ch + 3) & 1) || (s->lfsr & 1);
        if (!tone || !noise) continue;
        uint8_t v = s->reg[8 + ch];
        unsigned level = (v & 0x10) ? s->env_level : (v & 15) ? (v & 15) * 2u + 1 : 0;
        sum += s5b_amplitude[level];
    }
    return sum * 0.1488;
}

static void fme7_write(uint16_t addr, uint8_t value)
{
    HwFme7 *f = &hw_cart.m.fme7;
    switch (addr & 0xe000) {
    case 0x8000: f->command = value & 15; return;
    case 0xa000: break;
    default: s5b_write(addr, value); return;
    }
    switch (f->command) {
    case 8: f->prg[0] = value; break;
    case 9: case 10: case 11: f->prg[f->command - 8] = value; break;
    case 12: f->mirror = value & 3; break;
    case 13: f->irq_ctrl = value; hw_cart.m.irq_out = 0; return;
    case 14: f->counter = (uint16_t)((f->counter & 0xff00) | value); return;
    case 15: f->counter = (uint16_t)((f->counter & 0x00ff) | value << 8); return;
    default: f->chr[f->command] = value; break;
    }
    fme7_apply();
}

/* $6000-$7FFF: PRG ROM, enabled RAM, or open bus (RAM selected but disabled,
 * or a board without RAM). */
static bool fme7_read(uint16_t addr, uint8_t *value)
{
    const HwFme7 *f = &hw_cart.m.fme7;
    if (!(f->prg[0] & 0x40)) {
        uint32_t bank = (f->prg[0] & 0x3fu) & (hw_cart.prg_slots / 2 - 1);
        *value = hw_cart.prg[bank * 8192u + (addr & 0x1fff)];
        return true;
    }
    if (!hw_cart.has_wram || !hw_cart.wram_readable) return false;
    *value = hw_cart.wram[(hw_cart.wram_bank + (addr & 0x1fff)) % hw_cart.wram_len];
    return true;
}

static void fme7_clock(void)
{
    HwFme7 *f = &hw_cart.m.fme7;
    if ((f->irq_ctrl & 0x80) && f->counter-- == 0 && (f->irq_ctrl & 1)) hw_cart.m.irq_out = 1;
    s5b_clock();
}

/* Sunsoft-4 (mapper 68). https://www.nesdev.org/wiki/INES_Mapper_068
 * $8000-$B000: four 2 KiB CHR banks. $C000/$D000: the two 1 KiB CHR ROM pages
 * that can replace CIRAM (D7 forced high: the last 128 KiB of CHR ROM).
 * $E000: bits 0-1 mirroring (vertical, horizontal, one-screen low/high), bit 4
 * CHR ROM nametables. $F000: bits 0-3 the 16 KiB PRG bank at $8000, bit 4 the
 * work RAM enable. reg[0-3] = CHR, reg[4-5] = nametable pages. */
static void sunsoft4_apply(void)
{
    for (unsigned i = 0; i < 4; ++i) map_chr2(i, hw_cart.m.reg[i]);
    map_prg16(0, hw_cart.m.prg & 15);
    map_prg16(1, -1);
    static const uint8_t mirror[4] = { HW_MIRROR_VERTICAL, HW_MIRROR_HORIZONTAL,
                                       HW_MIRROR_SCREEN_A, HW_MIRROR_SCREEN_B };
    hw_cart.mirroring = mirror[hw_cart.m.ctrl & 3];
    hw_cart.wram_readable = hw_cart.wram_writable = (hw_cart.m.prg & 0x10) != 0;
}

static void sunsoft4_write(uint16_t addr, uint8_t value)
{
    unsigned page = addr >> 12;
    if (page <= 11) hw_cart.m.reg[page - 8] = value;
    else if (page <= 13) hw_cart.m.reg[page - 8] = value | 0x80;
    else if (page == 14) hw_cart.m.ctrl = value;
    else hw_cart.m.prg = value;
    sunsoft4_apply();
}

/* With $E000 bit 4 set the nametables are CHR ROM pages. The CIRAM A10 the
 * mirroring mode would drive selects which of the two registers applies. */
static uint32_t sunsoft4_nt_index(uint16_t addr)
{
    uint16_t a10 = hw_cart_ciram_a10(addr);
    unsigned bank = hw_cart.m.reg[a10 ? 5 : 4];
    return (bank & (hw_cart.chr_pages - 1)) * 1024u + (addr & 0x3ff);
}

/* Sunsoft-3 (mapper 67), https://www.nesdev.org/wiki/INES_Mapper_067.
 * Registers decode A15-A11: $8000 acknowledges the IRQ; $8800/$9800/$A800/
 * $B800 select the four 2 KiB CHR banks; $C800 writes the counter itself, high
 * byte then low (a toggle $D800 resets); $D800 bit 4 runs the counter; $E800
 * bits 0-1 mirroring (vertical, horizontal, one-screen A, B); $F800 the 16 KiB
 * PRG bank at $8000, the last bank fixed at $C000. The running counter
 * decrements every CPU cycle; wrapping $0000 -> $FFFF stops it and asserts. */
static void sunsoft3_write(uint16_t addr, uint8_t value)
{
    static const uint8_t mirror[4] = { HW_MIRROR_VERTICAL, HW_MIRROR_HORIZONTAL,
                                       HW_MIRROR_SCREEN_A, HW_MIRROR_SCREEN_B };
    switch (addr & 0xf800) {
    case 0x8000: hw_cart.m.irq_out = 0; break;
    case 0x8800: case 0x9800: case 0xa800: case 0xb800: map_chr2((addr >> 12) & 3, value); break;
    case 0xc800:
        hw_cart.m.irq_counter16 = hw_cart.m.irq_mode ?
            (uint16_t)((hw_cart.m.irq_counter16 & 0xff00) | value) :
            (uint16_t)((hw_cart.m.irq_counter16 & 0x00ff) | (value << 8));
        hw_cart.m.irq_mode ^= 1;
        break;
    case 0xd800: hw_cart.m.irq_enable = (value >> 4) & 1; hw_cart.m.irq_mode = 0; break;
    case 0xe800: hw_cart.mirroring = mirror[value & 3]; break;
    case 0xf800: map_prg16(0, value); break;
    default: break;
    }
}

static void sunsoft3_clock(void)
{
    if (hw_cart.m.irq_enable && hw_cart.m.irq_counter16-- == 0) { hw_cart.m.irq_enable = 0; hw_cart.m.irq_out = 1; }
}
