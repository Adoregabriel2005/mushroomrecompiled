/* Independent Namco 163 / 175 / 340 connector model (nesdev wiki: Namco 163,
 * Namco 163 audio, INES Mapper 210). Shares no state with hw_namco.inc. The
 * 163 drives CIRAM /CE and A10 itself, both for nametables and when CIRAM
 * serves as CHR RAM; this model sets those connector pins and lets the core's
 * CIRAM path answer. Sound output is not synthesized, but the channel phase
 * updates are: they rewrite internal RAM the CPU can read back at $4800. */
inline bool TricMapper::N163() { return Cart.Mapper == 19; }

inline void TricMapper::NamcoApply() {
    for (uint page = 0; page < 8; ++page) {
        byte bank = NamcoChr[page];
        byte off = (byte)(page < 4 ? 0x40 : 0x80);
        NamcoCiramChr[page] = N163() && bank >= 0xe0 && !(NamcoPrg[1] & off) ? (byte)(1 + bank % 2) : 0;
        if (!NamcoCiramChr[page]) MapCHR1(page, bank);
    }
    MapPRG8(0, NamcoPrg[0] % 64); MapPRG8(1, NamcoPrg[1] % 64); MapPRG8(2, NamcoPrg[2] % 64);
    MapPRG8(3, -1);
    if (Cart.Mapper == 210 && Cart.Info.submapper == 2) {
        switch (NamcoPrg[0] / 64) {
        case 0: Mirroring = TRIC_MIRROR_SCREEN_A; break;
        case 1: Mirroring = TRIC_MIRROR_VERTICAL; break;
        case 2: Mirroring = TRIC_MIRROR_SCREEN_B; break;
        default: Mirroring = TRIC_MIRROR_HORIZONTAL; break;
        }
    }
}

inline byte TricMapper::NamcoSound(byte value, bool write) {
    byte &cell = ExRAM[NamcoSndAddr % 128];
    if (write) cell = value; else value = cell;
    if (NamcoSndInc && NamcoSndAddr != 127) NamcoSndAddr++;   // stops at $7F
    return value;
}

// $4800-$5FFF (163 only). Returns whether the chip drove a read.
inline bool TricMapper::NamcoLow(ushort Address, byte *Out, const byte *In) {
    if (!N163() || Address < 0x4800 || Address >= 0x6000) return false;
    unsigned port = (Address - 0x4800) / 0x800;   // 0: sound data, 1: IRQ low, 2: IRQ high
    if (In) {
        if (port == 0) NamcoSound(*In, true);
        else {
            NamcoIrq = port == 1 ? (ushort)((NamcoIrq & 0xff00) | *In) : (ushort)((NamcoIrq & 0xff) | (*In << 8));
            IrqOut = false;
        }
        return true;
    }
    if (Out) *Out = port == 0 ? NamcoSound(0, false) : port == 1 ? (byte)(NamcoIrq & 0xff) : (byte)(NamcoIrq >> 8);
    return true;
}

inline void TricMapper::NamcoStore(ushort Address, byte Input) {
    if (Address < 0x6000) { NamcoLow(Address, nullptr, &Input); return; }
    if (Address < 0x8000) {
        bool ok = N163() ? (NamcoProtect / 16 == 4 && !((NamcoProtect >> ((Address - 0x6000) / 0x800)) & 1))
                         : (Cart.Info.submapper == 1 && (NamcoRamEnable & 1));
        if (HasWRAM && ok) WRAM[(Address % 8192) % WRAM_Length] = Input;
        return;
    }
    switch ((Address - 0x8000) / 0x800) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: NamcoChr[(Address - 0x8000) / 0x800] = Input; break;
    case 8: case 9: case 10: case 11:
        if (N163()) NamcoNt[(Address - 0xc000) / 0x800] = Input;
        else if (Address < 0xc800 && Cart.Info.submapper == 1) NamcoRamEnable = Input;
        break;
    case 12: NamcoPrg[0] = Input; break;
    case 13: NamcoPrg[1] = Input; break;
    case 14: NamcoPrg[2] = Input; break;
    default:
        if (N163()) { NamcoProtect = Input; NamcoSndAddr = Input % 128; NamcoSndInc = Input >= 128; }
        break;
    }
    NamcoApply();
}

inline bool TricMapper::NamcoRamReadable() {
    return HasWRAM && (N163() || (Cart.Info.submapper == 1 && (NamcoRamEnable & 1)));
}

// Each M2 cycle: the IRQ up-counter, and the sound generator's 15-cycle
// channel slots running from channel 8 down to 8-C.
inline void TricMapper::NamcoClock() {
    if (!N163()) return;
    if (NamcoIrq >= 0x8000 && NamcoIrq != 0xffff) {
        NamcoIrq++;
        if (NamcoIrq == 0xffff) IrqOut = true;
    }
    if (NamcoPrg[0] & 0x40) return;
    NamcoSndStep++;
    if (NamcoSndStep != 15) return;
    NamcoSndStep = 0;
    unsigned base = 0x40 + NamcoSndCh * 8;
    uint32_t freq = ExRAM[base] + ExRAM[base + 2] * 256u + (ExRAM[base + 4] % 4) * 65536u;
    uint32_t phase = ExRAM[base + 1] + ExRAM[base + 3] * 256u + ExRAM[base + 5] * 65536u;
    uint32_t length = (64u - ExRAM[base + 4] / 4) * 4;
    phase = (phase + freq) % (length * 65536u);
    ExRAM[base + 1] = (byte)(phase % 256); ExRAM[base + 3] = (byte)(phase / 256 % 256); ExRAM[base + 5] = (byte)(phase / 65536);
    unsigned lowest = 7 - (ExRAM[0x7f] / 16) % 8;
    NamcoSndCh = NamcoSndCh > lowest ? (byte)(NamcoSndCh - 1) : (byte)7;
}

// Nametable quadrant -> CHR ROM page when the select is below $E0.
inline int TricMapper::NamcoNtRom(ushort Address) {
    byte sel = NamcoNt[(Address >> 10) % 4];
    return sel < 0xe0 ? (int)((sel % Cart.CHRPages) * 1024u + Address % 1024) : -1;
}
