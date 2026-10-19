/* Namco 163 (mapper 19) and the cost-reduced Namco 175/340 (mapper 210).
 * https://www.nesdev.org/wiki/Namco_163
 * https://www.nesdev.org/wiki/Namco_163_audio
 * https://www.nesdev.org/wiki/INES_Mapper_210
 *
 * All three: eight 1 KiB CHR banks at $8000-$BFFF and 8 KiB PRG banks at
 * $E000/$E800/$F000 with the last bank fixed. The 163 adds CIRAM as CHR RAM
 * (bank values $E0-$FF, per half unless $E800 bits 6/7 disable it), CHR ROM
 * or CIRAM nametables ($C000-$D800), a 15-bit CPU-cycle IRQ counter, 8 KiB
 * work RAM with 2 KiB write protection, and a 128-byte internal RAM holding
 * wavetables and the channel registers of a time-multiplexed sound generator.
 * The 175 has a RAM enable at $C000 and hardwired mirroring; the 340 selects
 * mirroring with $E000 bits 6-7 and has no RAM. The internal RAM lives in
 * hw_cart.exram[0-127] so it is hashed, battery-saved and dumped. */

static bool namco163(void) { return hw_cart.mapper == 19; }

static void namco_apply(void)
{
    const HwNamco *n = &hw_cart.m.namco;
    hw_cart.chr_ciram = 0;
    for (unsigned page = 0; page < 8; ++page) {
        uint8_t bank = n->chr[page];
        bool disabled = (n->prg[1] >> (page < 4 ? 6 : 7)) & 1;
        if (namco163() && bank >= 0xe0 && !disabled) {
            hw_cart.chr_ciram |= (uint8_t)(1u << page);
            hw_cart.chr_off[page] = (bank & 1u) * 0x400u;
            hw_cart.chr_write[page] = 1;
        } else {
            map_chr1(page, bank);
            hw_cart.chr_write[page] = 0;
        }
    }
    for (unsigned slot = 0; slot < 3; ++slot) map_prg8(slot, n->prg[slot] & 0x3f);
    map_prg8(3, -1);
    if (hw_cart.mapper == 210 && hw_cart.info.submapper == 2) {
        static const uint8_t mirror[4] = { HW_MIRROR_SCREEN_A, HW_MIRROR_VERTICAL,
                                           HW_MIRROR_SCREEN_B, HW_MIRROR_HORIZONTAL };
        hw_cart.mirroring = mirror[n->prg[0] >> 6];
    }
    if (namco163()) {
        /* Reads always work; writes need the upper nibble $4 and a clear
         * per-window bit (checked in namco_write_ram). */
        hw_cart.wram_readable = hw_cart.has_wram;
        hw_cart.wram_writable = 0;
    } else {
        hw_cart.wram_readable = hw_cart.wram_writable = hw_cart.has_wram && (n->ram_enable & 1);
    }
}

/* CIRAM A10 for a nametable address when the 163 maps CIRAM there. */
static uint16_t namco_nt_a10(uint16_t addr)
{
    return (hw_cart.m.namco.nt[(addr >> 10) & 3] & 1) ? 0x400 : 0;
}

static bool namco_nt_is_rom(uint16_t addr) { return hw_cart.m.namco.nt[(addr >> 10) & 3] < 0xe0; }

static uint32_t namco_nt_index(uint16_t addr)
{
    return (hw_cart.m.namco.nt[(addr >> 10) & 3] & (hw_cart.chr_pages - 1)) * 1024u + (addr & 0x3ff);
}

static void namco_write(uint16_t addr, uint8_t value)
{
    HwNamco *n = &hw_cart.m.namco;
    unsigned reg = (addr >> 11) & 15;         /* $8000 -> 0 ... $F800 -> 15 */
    if (reg < 8) n->chr[reg] = value;
    else if (reg < 12) {
        if (namco163()) n->nt[reg - 8] = value;
        else if (reg == 8 && hw_cart.info.submapper == 1) n->ram_enable = value;
    } else if (reg < 15) n->prg[reg - 12] = value;
    else if (namco163()) {
        n->protect = value;
        n->sound_addr = value & 0x7f;
        n->sound_inc = value >> 7;
    }
    namco_apply();
}

static uint8_t namco_sound_access(uint8_t *value, bool write)
{
    HwNamco *n = &hw_cart.m.namco;
    uint8_t *cell = &hw_cart.exram[n->sound_addr];
    if (write) *cell = *value; else *value = *cell;
    /* Auto-increment stops at $7F rather than wrapping. */
    if (n->sound_inc && n->sound_addr < 0x7f) ++n->sound_addr;
    return *value;
}

/* $4800-$5FFF registers of the 163. */
static void namco_low_write(uint16_t addr, uint8_t value)
{
    HwNamco *n = &hw_cart.m.namco;
    switch (addr & 0xf800) {
    case 0x4800: namco_sound_access(&value, true); break;
    case 0x5000: n->irq = (uint16_t)((n->irq & 0xff00) | value); hw_cart.m.irq_out = 0; break;
    case 0x5800: n->irq = (uint16_t)((n->irq & 0x00ff) | value << 8); hw_cart.m.irq_out = 0; break;
    }
}

static bool namco_low_read(uint16_t addr, uint8_t *value)
{
    HwNamco *n = &hw_cart.m.namco;
    switch (addr & 0xf800) {
    case 0x4800: namco_sound_access(value, false); return true;
    case 0x5000: *value = (uint8_t)n->irq; return true;
    case 0x5800: *value = (uint8_t)(n->irq >> 8); return true;
    }
    return false;
}

/* 163 work RAM writes: $F800 upper nibble must be $4 and the 2 KiB window's
 * protect bit clear. */
static bool namco_write_ram(uint16_t addr)
{
    uint8_t p = hw_cart.m.namco.protect;
    return (p & 0xf0) == 0x40 && !((p >> ((addr >> 11) & 3)) & 1);
}

/* Nominal gain per NES 2.0 submapper: a full-scale single channel (samples
 * 0/15 at volume 15, 225 steps peak to peak) is the stated dB above the
 * loudest APU square (0.1488 peak to peak here). Submapper 0 leaves it
 * unspecified; use submapper 3's midpoint. Submapper 2 has no expansion
 * sound. The cartridge's analog path is not modeled. */
static double namco_gain(void)
{
    static const double db[6] = { 12.0, 12.0, 0.0, 12.0, 16.5, 18.75 };
    unsigned sub = hw_cart.info.submapper;
    if (!namco163() || sub == 2 || sub > 5) return 0;
    return 0.1488 * pow(10.0, db[sub] / 20.0) / 225.0;
}

/* One CPU cycle: the 15-bit IRQ counter, and one step of the sound
 * generator, which spends 15 cycles per channel on channels 8 down to 8-C
 * (C = $7F bits 4-6) and holds only that channel's output. */
static void namco_clock(void)
{
    HwNamco *n = &hw_cart.m.namco;
    if ((n->irq & 0x8000) && (n->irq & 0x7fff) != 0x7fff && ((++n->irq) & 0x7fff) == 0x7fff)
        hw_cart.m.irq_out = 1;
    if (!namco163() || (n->prg[0] & 0x40)) return;   /* $E000 bit 6: sound disabled */
    if (++n->sound_step < 15) return;
    n->sound_step = 0;
    uint8_t *ram = hw_cart.exram, *ch = ram + 0x40 + n->channel * 8;
    uint32_t freq = ch[0] | ch[2] << 8 | (ch[4] & 3u) << 16;
    uint32_t phase = ch[1] | ch[3] << 8 | (uint32_t)ch[5] << 16;
    uint32_t length = 256u - (ch[4] & 0xfc);
    phase = (phase + freq) % (length << 16);
    ch[1] = (uint8_t)phase; ch[3] = (uint8_t)(phase >> 8); ch[5] = (uint8_t)(phase >> 16);
    uint8_t x = (uint8_t)((phase >> 16) + ch[6]);
    int sample = (ram[x >> 1] >> ((x & 1) * 4)) & 15;
    n->output = (int16_t)((sample - 8) * (ch[7] & 15));
    unsigned count = ((ram[0x7f] >> 4) & 7) + 1;
    /* <= also catches a channel count lowered mid-rotation. */
    n->channel = n->channel <= 8 - count ? 7 : (uint8_t)(n->channel - 1);
}

static double namco_audio(void)
{
    const HwNamco *n = &hw_cart.m.namco;
    if (!namco163() || (n->prg[0] & 0x40)) return 0;
    return n->output * namco_gain();
}
