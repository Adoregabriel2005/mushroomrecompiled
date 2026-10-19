/* Sunsoft FME-7 / 5B contracts from https://www.nesdev.org/wiki/Sunsoft_FME-7
 * and https://www.nesdev.org/wiki/Sunsoft_5B_audio. Included by mapper_test.c. */

static void fme7(unsigned command, uint8_t value)
{
    hw_cart_cpu_write(0x8000, (uint8_t)command);
    hw_cart_cpu_write(0xa000, value);
}

static void s5b(unsigned reg, uint8_t value)
{
    hw_cart_cpu_write(0xc000, (uint8_t)reg);
    hw_cart_cpu_write(0xe000, value);
}

static void cpu_cycles(unsigned n) { while (n--) hw_cart_cpu_clock(); }

static void test_fme7_banks(void)
{
    cart(69, 256, 256);
    CHECK(hw_cart.watch_cpu && hw_cart.has_wram);
    prg_banks(0, 0, 0, 31);
    fme7(9, 3); fme7(10, 0x45); fme7(11, 0xff);  /* 6 bits, then the ROM wraps */
    prg_banks(3, 5, 31, 31);
    for (unsigned page = 0; page < 8; ++page) fme7(page, (uint8_t)(0x10 + page * 29));
    for (unsigned page = 0; page < 8; ++page) chr_bank(page, (0x10 + page * 29) & 255, 1);
    static const uint8_t mirror[4] = { HW_MIRROR_VERTICAL, HW_MIRROR_HORIZONTAL,
                                       HW_MIRROR_SCREEN_A, HW_MIRROR_SCREEN_B };
    for (unsigned m = 0; m < 8; ++m) { fme7(12, (uint8_t)m); CHECK(hw_cart.mirroring == mirror[m & 3]); }
    /* The $8000 command register uses bits 0-3 only. */
    hw_cart_cpu_write(0x9fff, 0xf9); hw_cart_cpu_write(0xbfff, 7);
    prg_banks(7, 5, 31, 31);

    /* $6000: ROM bank (bits 0-5), open bus with RAM selected but disabled,
     * enabled RAM. */
    uint8_t value = 0xa5;
    prg[2 * 8192 + 0x123] = 0x77;
    fme7(8, 0x02); CHECK(hw_cart_cpu_read(0x6123, &value) && value == 0x77);
    hw_cart_cpu_write(0x6123, 0x11);
    CHECK(hw_cart_cpu_read(0x6123, &value) && value == 0x77);
    fme7(8, 0x40); value = 0xa5; CHECK(!hw_cart_cpu_read(0x6000, &value) && value == 0xa5);
    hw_cart_cpu_write(0x6000, 0x12);
    fme7(8, 0xc0); CHECK(hw_cart_cpu_read(0x6000, &value) && value != 0x12);
    hw_cart_cpu_write(0x7fff, 0x34); CHECK(hw_cart_cpu_read(0x7fff, &value) && value == 0x34);
    fme7(8, 0xc1); CHECK(hw_cart_cpu_read(0x7fff, &value) && value == 0x34); /* 8 KiB wraps */
    fme7(8, 0x80); CHECK(hw_cart_cpu_read(0x7fff, &value) && value == 0xff); /* ROM bank 0 */
    /* A board without RAM leaves the bus open when RAM is selected. */
    hw_cart.has_wram = 0; hw_cart.wram_len = 0;
    fme7(8, 0xc0); value = 0xa5; CHECK(!hw_cart_cpu_read(0x6000, &value) && value == 0xa5);
}

static void test_fme7_irq(void)
{
    cart(69, 256, 256);
    fme7(14, 2); fme7(15, 0);
    cpu_cycles(10); CHECK(!hw_cart_irq());           /* counting disabled */
    fme7(13, 0x81);
    cpu_cycles(2); CHECK(!hw_cart_irq() && hw_cart.m.fme7.counter == 0);
    cpu_cycles(1); CHECK(hw_cart_irq() && hw_cart.m.fme7.counter == 0xffff);
    cpu_cycles(5); CHECK(hw_cart_irq());              /* held until acknowledged */
    /* Counter writes do not acknowledge; only command 13 does. */
    fme7(14, 0); fme7(15, 0); CHECK(hw_cart_irq());
    fme7(13, 0x81); CHECK(!hw_cart_irq());
    cpu_cycles(1); CHECK(hw_cart_irq());
    fme7(13, 0x80); CHECK(!hw_cart_irq());           /* counting without IRQs */
    fme7(14, 0); fme7(15, 0);
    cpu_cycles(3); CHECK(!hw_cart_irq() && hw_cart.m.fme7.counter == 0xfffd);
    fme7(13, 0x01);                                   /* IRQ enabled, not counting */
    cpu_cycles(0x20000); CHECK(!hw_cart_irq() && hw_cart.m.fme7.counter == 0xfffd);
    fme7(13, 0x81); cpu_cycles(0xfffd); CHECK(!hw_cart_irq());
    cpu_cycles(1); CHECK(hw_cart_irq());
}

/* Expected envelope level after each 16-cycle step with period 1, from the
 * register 13 shape table: \___ /___ \\\\ \___ \/\/ \^^^ //// /^^^ /\/\ /___ */
static int envelope_expect(unsigned shape, unsigned step)
{
    bool attack = (shape & 4) != 0;
    unsigned seg = step / 32, pos = step % 32;
    int ramp = attack ? (int)pos : 31 - (int)pos;
    if (seg == 0) return ramp;
    if (!(shape & 8)) return 0;
    if (shape & 1) return (attack != ((shape & 2) != 0)) ? 31 : 0;
    if (shape & 2) return (seg & 1) ? 31 - ramp : ramp;
    return ramp;
}

static void test_5b(void)
{
    cart(69, 256, 256);
    const Hw5B *s = &hw_cart.m.s5b;
    CHECK(hw_cart_audio_level() == 0);
    /* Tone: the output toggles every 16*P cycles; P=0 acts as 1. */
    for (unsigned period = 0; period < 4; ++period) {
        cart(69, 256, 256);
        s5b(0, (uint8_t)period); s5b(1, 0xf0);   /* upper period bits ignored */
        unsigned toggles = 0, last = s->tone_out & 1;
        for (unsigned c = 0; c < 16 * 60; ++c) {
            hw_cart_cpu_clock();
            if ((s->tone_out & 1) != last) { ++toggles; last = s->tone_out & 1; }
        }
        CHECK(toggles == 60 / (period ? period : 1));
    }
    cart(69, 256, 256);
    s5b(2, 0x34); s5b(3, 0x12); s5b(4, 0x56); s5b(5, 0x0f);
    CHECK(s->reg[2] == 0x34 && s->reg[3] == 0x12);
    /* A nonzero high nibble in the address register blocks data writes. */
    s5b(0x13, 0x99); CHECK(s->reg[3] == 0x12);
    hw_cart_cpu_write(0xdfff, 0x05); hw_cart_cpu_write(0xffff, 0x77); CHECK(s->reg[5] == 0x77);

    /* Noise: a maximal-length 17-bit LFSR (taps 16/13) stepping every 32*N. */
    cart(69, 256, 256);
    s5b(6, 0x20);                               /* 5-bit period: 0 -> acts as 1 */
    CHECK(s->lfsr == 1);
    cpu_cycles(31); CHECK(s->lfsr == 1);
    cpu_cycles(1); CHECK(s->lfsr != 1);
    unsigned long steps = 1;
    while (s->lfsr != 1) { cpu_cycles(32); ++steps; CHECK(steps <= 131071); }
    CHECK(steps == 131071);
    s5b(6, 3); cpu_cycles(95); { uint32_t before = s->lfsr; cpu_cycles(1); CHECK(s->lfsr != before); }

    /* Envelope: 32 levels, every shape, restarted by writing register 13. */
    for (unsigned shape = 0; shape < 16; ++shape) {
        cart(69, 256, 256);
        s5b(11, 1); s5b(12, 0); s5b(13, (uint8_t)shape);
        CHECK(s->env_level == envelope_expect(shape, 0));
        for (unsigned step = 1; step < 130; ++step) {
            cpu_cycles(16);
            CHECK(s->env_level == envelope_expect(shape, step));
        }
        s5b(13, (uint8_t)shape);
        CHECK(s->env_level == envelope_expect(shape, 0) && !s->env_hold);
    }
    cart(69, 256, 256);
    s5b(11, 3); s5b(12, 0); s5b(13, 12);         /* //// at 16*3 cycles per step */
    cpu_cycles(47); CHECK(s->env_level == 0);
    cpu_cycles(1); CHECK(s->env_level == 1);

    /* Mixer: with tone and noise both disabled a channel outputs its level;
     * 1.5 dB per 5-bit step, volume v = level 2v+1, levels 0/1 silent. */
    cart(69, 256, 256);
    s5b(7, 0x3f); s5b(8, 15);
    double full = hw_cart_audio_level();
    CHECK(full > 0.148 && full < 0.1496);
    s5b(8, 14); CHECK(fabs(hw_cart_audio_level() / full - pow(10, -3.0 / 20)) < 1e-9);
    s5b(8, 1); CHECK(fabs(hw_cart_audio_level() / full - pow(10, -42.0 / 20)) < 1e-9);
    s5b(8, 0); CHECK(hw_cart_audio_level() == 0);
    s5b(8, 15); s5b(9, 15); s5b(10, 15); CHECK(fabs(hw_cart_audio_level() - 3 * full) < 1e-12);
    /* Envelope-driven volume uses the 5-bit level directly. */
    s5b(9, 0); s5b(10, 0); s5b(8, 0x10); s5b(11, 1); s5b(13, 4);
    CHECK(hw_cart_audio_level() == 0);                  /* level 0 */
    cpu_cycles(16); CHECK(hw_cart_audio_level() == 0);  /* level 1 */
    cpu_cycles(16); CHECK(fabs(hw_cart_audio_level() / full - pow(10, -29 * 1.5 / 20)) < 1e-9);
    /* Tone enabled: silent while the square is low, on while high. */
    cart(69, 256, 256);
    s5b(7, 0x3e); s5b(8, 15); s5b(0, 1);
    CHECK(hw_cart_audio_level() == 0);
    cpu_cycles(16); CHECK(hw_cart_audio_level() > 0.148);
    /* Tone and noise enabled: logical AND. */
    s5b(7, 0x36);
    for (unsigned c = 0; c < 4096; ++c) {
        hw_cart_cpu_clock();
        bool on = (s->tone_out & 1) && (s->lfsr & 1);
        CHECK((hw_cart_audio_level() > 0) == on);
    }
}


/* Sunsoft-4: https://www.nesdev.org/wiki/INES_Mapper_068 */
static void test_sunsoft4(void)
{
    cart(68, 128, 256);
    for (unsigned p = 0; p < 256; ++p) memset(chr + p * 1024, (int)p, 1024);
    CHECK(hw_cart.has_wram);
    prg_banks(0, 1, 14, 15);
    uint8_t value = 0xa5;
    CHECK(!hw_cart_cpu_read(0x6000, &value) && value == 0xa5);   /* RAM disabled */
    hw_cart_cpu_write(0xf123, 0x13); prg_banks(6, 7, 14, 15);
    hw_cart_cpu_write(0x6000, 0x5a); CHECK(hw_cart_cpu_read(0x6000, &value) && value == 0x5a);
    hw_cart_cpu_write(0xffff, 0x0e); prg_banks(12, 13, 14, 15);  /* bank 14 wraps to 6 */
    value = 0xa5; CHECK(!hw_cart_cpu_read(0x6000, &value) && value == 0xa5);
    hw_cart_cpu_write(0x8000, 5); hw_cart_cpu_write(0x9fff, 6);
    hw_cart_cpu_write(0xa000, 0x80); hw_cart_cpu_write(0xb000, 0xff);
    chr_bank(0, 10, 2); chr_bank(2, 12, 2); chr_bank(4, 0, 2); chr_bank(6, 254, 2);
    /* Nametable registers force D7: the last 128 KiB. */
    hw_cart_cpu_write(0xc000, 0x05); hw_cart_cpu_write(0xdfff, 0x86);
    CHECK(hw_cart.m.reg[4] == 0x85 && hw_cart.m.reg[5] == 0x86);
    static const uint8_t mirror[4] = { HW_MIRROR_VERTICAL, HW_MIRROR_HORIZONTAL,
                                       HW_MIRROR_SCREEN_A, HW_MIRROR_SCREEN_B };
    static const uint8_t sel[4][4] = { {0,1,0,1}, {0,0,1,1}, {0,0,0,0}, {1,1,1,1} };
    for (unsigned m = 0; m < 4; ++m) {
        hw_cart_cpu_write(0xe000, (uint8_t)m);
        CHECK(hw_cart.mirroring == mirror[m]);
        CHECK(!hw_cart_nt_read(0x2000, true, &value) && !hw_cart_nt_write(0x2000, 1));
        hw_cart_cpu_write(0xe000, (uint8_t)(m | 0x10));
        for (unsigned q = 0; q < 4; ++q) {
            for (unsigned base = 0x2000; base < 0x4000; base += 0x1000) {
                uint16_t addr = (uint16_t)(base + q * 0x400 + 0x3a);
                if (addr >= 0x3f00) continue;
                value = 0;
                CHECK(hw_cart_nt_read(addr, true, &value) && value == (sel[m][q] ? 0x86 : 0x85));
                value = 0x77; CHECK(hw_cart_nt_read(addr, false, &value) && value == 0x77);
                CHECK(hw_cart_nt_write(addr, 0x12)); /* ROM: dropped, CIRAM deselected */
            }
        }
    }
    CHECK(chr[0x85 * 1024 + 0x3a] == 0x85);
    CHECK(!hw_cart_irq());
}

static void test_sunsoft(void)
{
    test_fme7_banks();
    test_fme7_irq();
    test_5b();
    test_sunsoft4();
}
