/* Namco 163 / 175 / 340 contracts: https://www.nesdev.org/wiki/Namco_163,
 * Namco_163_audio and INES_Mapper_210. Included by mapper_test.c. */

static void namco_cart(int mapper, unsigned sub)
{
    cart(mapper, 256, 256);
    for (unsigned p = 0; p < 256; ++p) memset(chr + p * 1024, (int)p, 1024);
    hw_cart.info.submapper = (uint8_t)sub;
    hw_cart.has_wram = 1; hw_cart.wram_len = 8192;
    hw_cart_power_on();
    hw_cart.has_wram = 1; hw_cart.wram_len = 8192;
    hw_cart_cpu_write(0xe000, hw_cart.m.namco.prg[0]); /* re-apply RAM access */
}

static void test_namco163_banks(void)
{
    namco_cart(19, 3);
    CHECK(hw_cart.watch_cpu);
    prg_banks(0, 0, 0, 31);
    hw_cart_cpu_write(0xe000, 0x05); hw_cart_cpu_write(0xe800, 0x3f); hw_cart_cpu_write(0xf7ff, 0xc9);
    prg_banks(5, 31, 9, 31);
    /* CHR: $00-$DF ROM; $E0-$FF CIRAM unless $E800 bit 6 (low half) / 7 (high half). */
    for (unsigned p = 0; p < 8; ++p) hw_cart_cpu_write((uint16_t)(0x8000 + p * 0x800), (uint8_t)(0x10 + p));
    for (unsigned p = 0; p < 8; ++p) { chr_bank(p, 0x10 + p, 1); CHECK(!hw_cart.chr_write[p]); }
    CHECK(hw_cart.chr_ciram == 0);
    hw_cart_cpu_write(0x8000, 0xe1); hw_cart_cpu_write(0xb800, 0xfe);
    CHECK(hw_cart.chr_ciram == 0x81 && hw_cart.chr_off[0] == 0x400 && hw_cart.chr_off[7] == 0);
    hw_cart_cpu_write(0xe800, 0x40 | 0x3f);   /* low half: $E0-$FF are CHR ROM */
    CHECK(hw_cart.chr_ciram == 0x80); chr_bank(0, 0xe1, 1);
    hw_cart_cpu_write(0xe800, 0x80 | 0x3f);
    CHECK(hw_cart.chr_ciram == 0x01); chr_bank(7, 0xfe, 1);
    /* Nametables: $E0-$FF CIRAM with A10 = bit 0, else a CHR ROM page. */
    uint8_t value = 0;
    hw_cart_cpu_write(0xc000, 0xe0); hw_cart_cpu_write(0xc800, 0xe1);
    hw_cart_cpu_write(0xd000, 0x42); hw_cart_cpu_write(0xdfff, 0xff);
    CHECK(!hw_cart_nt_read(0x2000, true, &value) && hw_cart_ciram_a10(0x2000) == 0);
    CHECK(!hw_cart_nt_read(0x2400, true, &value) && hw_cart_ciram_a10(0x27ff) == 0x400);
    CHECK(hw_cart_nt_read(0x2812, true, &value) && value == 0x42 && hw_cart_nt_write(0x2812, 9));
    CHECK(!hw_cart_nt_read(0x3c00, true, &value) && hw_cart_ciram_a10(0x3c00) == 0x400);
    CHECK(!hw_cart_nt_write(0x2000, 9));
    /* Work RAM: reads always; writes need $F800 upper nibble $4 and a clear bit. */
    hw_cart_cpu_write(0xf800, 0x00); hw_cart_cpu_write(0x6000, 0x11);
    CHECK(hw_cart_cpu_read(0x6000, &value) && value == 0);
    hw_cart_cpu_write(0xf800, 0xc0); hw_cart_cpu_write(0x6000, 0x11);
    CHECK(hw_cart_cpu_read(0x6000, &value) && value == 0);
    hw_cart_cpu_write(0xf800, 0x42);            /* protect $6800-$6FFF only */
    hw_cart_cpu_write(0x6000, 0x11); hw_cart_cpu_write(0x6800, 0x22);
    hw_cart_cpu_write(0x7000, 0x33); hw_cart_cpu_write(0x7fff, 0x44);
    CHECK(hw_cart_cpu_read(0x6000, &value) && value == 0x11);
    CHECK(hw_cart_cpu_read(0x6800, &value) && value == 0);
    CHECK(hw_cart_cpu_read(0x7000, &value) && value == 0x33);
    CHECK(hw_cart_cpu_read(0x7fff, &value) && value == 0x44);
}

static void test_namco163_irq(void)
{
    namco_cart(19, 3);
    uint8_t value;
    hw_cart_cpu_write(0x5000, 0xfd); hw_cart_cpu_write(0x5800, 0x7f);   /* disabled */
    cpu_cycles(10); CHECK(!hw_cart_irq() && hw_cart_cpu_read(0x5000, &value) && value == 0xfd);
    hw_cart_cpu_write(0x5800, 0xff);                                    /* enable */
    cpu_cycles(1); CHECK(!hw_cart_irq() && hw_cart_cpu_read(0x5000, &value) && value == 0xfe);
    cpu_cycles(1); CHECK(hw_cart_irq());
    CHECK(hw_cart_cpu_read(0x5000, &value) && value == 0xff && hw_cart_cpu_read(0x5800, &value) && value == 0xff);
    cpu_cycles(100); CHECK(hw_cart.m.namco.irq == 0xffff);             /* stops at $7FFF */
    hw_cart_cpu_write(0x5000, 0xff); CHECK(!hw_cart_irq());             /* any write acknowledges */
    hw_cart_cpu_write(0x5000, 0x00); hw_cart_cpu_write(0x5800, 0x80);
    cpu_cycles(0x7ffe); CHECK(!hw_cart_irq()); cpu_cycles(1); CHECK(hw_cart_irq());
    hw_cart_cpu_write(0x5800, 0x80); CHECK(!hw_cart_irq());
    /* Mapper 210 has no IRQ counter and no $4800-$5FFF registers. */
    namco_cart(210, 2);
    hw_cart_cpu_write(0x5800, 0xff); cpu_cycles(0x10000);
    value = 0xa5; CHECK(!hw_cart_irq() && !hw_cart_cpu_read(0x5000, &value) && value == 0xa5);
}

static void test_namco163_sound(void)
{
    namco_cart(19, 3);
    uint8_t value;
    const uint8_t *ram = hw_cart.exram;
    /* $F800: address and auto-increment; the increment stops at $7F. */
    hw_cart_cpu_write(0xf800, 0x80 | 0x7d);
    hw_cart_cpu_write(0x4800, 1); hw_cart_cpu_write(0x4800, 2); hw_cart_cpu_write(0x4800, 3);
    CHECK(ram[0x7d] == 1 && ram[0x7e] == 2 && ram[0x7f] == 3 && ram[0] == 0);
    hw_cart_cpu_write(0xf800, 0x7e);
    CHECK(hw_cart_cpu_read(0x4800, &value) && value == 2 && hw_cart_cpu_read(0x4fff, &value) && value == 2);
    /* A 16-sample square (8 x 0, 8 x 15) on channel 8 alone at volume 15:
     * phase += freq every 15 cycles, output (sample - 8) * volume. */
    hw_cart_cpu_write(0xf800, 0x80);
    for (unsigned i = 0; i < 8; ++i) hw_cart_cpu_write(0x4800, i < 4 ? 0x00 : 0xff);
    hw_cart_cpu_write(0xf800, 0x80 | 0x78);
    static const uint8_t regs[8] = { 0x00, 0, 0x00, 0, 256 - 16, 0, 0, 0x0f }; /* freq $10000? set below */
    for (unsigned i = 0; i < 8; ++i) hw_cart_cpu_write(0x4800, regs[i]);
    CHECK(ram[0x7f] == 0x0f);                  /* one channel enabled */
    hw_cart_cpu_write(0xf800, 0x7c); hw_cart_cpu_write(0x4800, (uint8_t)(0xf0 | 1)); /* length 16, freq bit 16 */
    cpu_cycles(14); CHECK(hw_cart.m.namco.output == 0 && ram[0x7d] == 0);
    cpu_cycles(1);                             /* phase = $10000: sample 1 (value 0) */
    CHECK(ram[0x7d] == 1 && ram[0x7b] == 0 && hw_cart.m.namco.output == -120);
    for (unsigned s = 2; s < 20; ++s) {
        cpu_cycles(15);
        CHECK(ram[0x7d] == s % 16 && hw_cart.m.namco.output == ((s % 16) < 8 ? -120 : 105));
    }
    double gain = hw_cart_audio_level() / hw_cart.m.namco.output;
    CHECK(fabs(gain * 225 / 0.1488 - pow(10, 12.0 / 20)) < 1e-9);
    /* $E000 bit 6 freezes the generator and mutes it. */
    hw_cart_cpu_write(0xe000, 0x40);
    uint8_t phase = ram[0x7d];
    cpu_cycles(60); CHECK(ram[0x7d] == phase && hw_cart_audio_level() == 0);
    hw_cart_cpu_write(0xe000, 0x00);
    /* Eight channels: each updates once per 120 cycles, 8 down to 1. */
    hw_cart_cpu_write(0xf800, 0x7f); hw_cart_cpu_write(0x4800, 0x7f);
    hw_cart.m.namco.sound_step = 0; hw_cart.m.namco.channel = 7;
    for (unsigned ch = 0; ch < 8; ++ch) { hw_cart_cpu_write(0xf800, (uint8_t)(0x40 + ch * 8)); hw_cart_cpu_write(0x4800, 0x01); }
    for (unsigned ch = 0; ch < 7; ++ch) { hw_cart_cpu_write(0xf800, (uint8_t)(0x44 + ch * 8)); hw_cart_cpu_write(0x4800, 0xfc); }
    uint8_t before[8];
    for (unsigned ch = 0; ch < 8; ++ch) before[ch] = ram[0x41 + ch * 8];
    for (int ch = 7; ch >= 0; --ch) {
        cpu_cycles(15);
        for (unsigned k = 0; k < 8; ++k)
            CHECK(ram[0x41 + k * 8] == (uint8_t)(before[k] + ((int)k >= ch ? 1 : 0)));
    }
    CHECK(hw_cart.m.namco.channel == 7);
    /* Lowering the channel count mid-rotation restarts at channel 8. */
    hw_cart.m.namco.channel = 2; hw_cart.m.namco.sound_step = 14;
    hw_cart_cpu_write(0xf800, 0x7f); hw_cart_cpu_write(0x4800, 0x0f);
    cpu_cycles(1); CHECK(hw_cart.m.namco.channel == 7);
    /* Submapper 2 has no expansion sound; the RAM still works. */
    namco_cart(19, 2);
    hw_cart_cpu_write(0xf800, 0x7f); hw_cart_cpu_write(0x4800, 0x0f);
    hw_cart_cpu_write(0xf800, 0x7c); hw_cart_cpu_write(0x4800, 0xf1);
    cpu_cycles(15 * 9); CHECK(hw_cart_audio_level() == 0 && hw_cart.exram[0x7d] != 0);
}

static void test_namco210(void)
{
    /* 175: CHR/PRG like the 163, $C000 bit 0 enables RAM, mirroring fixed. */
    namco_cart(210, 1);
    hw_cart.mirroring = HW_MIRROR_HORIZONTAL;
    uint8_t value = 0xa5;
    CHECK(!hw_cart_cpu_read(0x6000, &value) && value == 0xa5);
    hw_cart_cpu_write(0xc000, 1); hw_cart_cpu_write(0x6000, 0x5a);
    CHECK(hw_cart_cpu_read(0x6000, &value) && value == 0x5a);
    hw_cart_cpu_write(0xc800, 0); CHECK(hw_cart_cpu_read(0x6000, &value));  /* only $C000-$C7FF */
    hw_cart_cpu_write(0xc7ff, 0); value = 0xa5; CHECK(!hw_cart_cpu_read(0x6000, &value));
    hw_cart_cpu_write(0x8000, 0xe1); chr_bank(0, 0xe1, 1); CHECK(hw_cart.chr_ciram == 0);
    for (unsigned v = 0; v < 4; ++v) { hw_cart_cpu_write(0xe000, (uint8_t)(v << 6)); CHECK(hw_cart.mirroring == HW_MIRROR_HORIZONTAL); }
    CHECK(!hw_cart_nt_read(0x2000, true, &value));
    /* 340: $E000 bits 6-7 = one-screen A, vertical, one-screen B, horizontal. */
    namco_cart(210, 2);
    static const uint8_t mirror[4] = { HW_MIRROR_SCREEN_A, HW_MIRROR_VERTICAL, HW_MIRROR_SCREEN_B, HW_MIRROR_HORIZONTAL };
    for (unsigned v = 0; v < 4; ++v) {
        hw_cart_cpu_write(0xe000, (uint8_t)(v << 6 | 3));
        CHECK(hw_cart.mirroring == mirror[v]); prg_banks(3, 0, 0, 31);
    }
    hw_cart_cpu_write(0xc000, 1); value = 0xa5; CHECK(!hw_cart_cpu_read(0x6000, &value));
}

static void test_namco(void)
{
    test_namco163_banks();
    test_namco163_irq();
    test_namco163_sound();
    test_namco210();
}
