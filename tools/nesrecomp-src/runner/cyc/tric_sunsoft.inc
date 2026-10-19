/* Independent Sunsoft FME-7 connector model (nesdev wiki, Sunsoft FME-7).
 * Shares no bank tables or IRQ state with hw_sunsoft.inc. The 5B's sound
 * generator is not synthesized here, like the other expansion audio. */
inline void TricMapper::FME7Apply() {
    for (uint page = 0; page < 8; ++page) MapCHR1(page, Fme7Reg[page]);
    MapPRG8(0, Fme7Reg[9] % 64); MapPRG8(1, Fme7Reg[10] % 64); MapPRG8(2, Fme7Reg[11] % 64);
    MapPRG8(3, -1);
    switch (Fme7Reg[12] % 4) {
    case 0: Mirroring = TRIC_MIRROR_VERTICAL; break;     // CIRAM A10 = PPU A10
    case 1: Mirroring = TRIC_MIRROR_HORIZONTAL; break;   // CIRAM A10 = PPU A11
    case 2: Mirroring = TRIC_MIRROR_SCREEN_A; break;
    default: Mirroring = TRIC_MIRROR_SCREEN_B; break;
    }
}
// $6000-$7FFF as command 8 selects it: ROM, enabled RAM, or nothing driving.
inline bool TricMapper::FME7Low(ushort Address, byte *Out, const byte *In) {
    byte sel = Fme7Reg[8];
    unsigned offset = (sel % 64) * 8192u + Address % 8192;
    if (sel / 64 == 0) {
        if (Out) *Out = Cart.PRGROM[offset % (uint)(Cart.PRGSlots * 4096)];
        return Out != nullptr;
    }
    if (sel / 64 != 3 || !WRAM_Length) return false;
    if (Out) *Out = WRAM[offset % WRAM_Length];
    if (In) WRAM[offset % WRAM_Length] = *In;
    return true;
}
inline void TricMapper::FME7Store(ushort Address, byte Input) {
    if (Address < 0x8000) { if (Address >= 0x6000) FME7Low(Address, nullptr, &Input); return; }
    if (Address < 0xa000) { Fme7Cmd = Input % 16; return; }
    if (Address >= 0xc000) return;   // 5B audio registers
    if (Fme7Cmd == 13) { Fme7IrqCtrl = Input; IrqOut = false; return; }
    if (Fme7Cmd == 14) { Fme7Counter = (ushort)((Fme7Counter & 0xff00) | Input); return; }
    if (Fme7Cmd == 15) { Fme7Counter = (ushort)((Fme7Counter & 0xff) | (Input << 8)); return; }
    Fme7Reg[Fme7Cmd] = Input;
    FME7Apply();
}
// Every M2 cycle: decrement when bit 7 is set; the $0000 -> $FFFF wrap
// interrupts when bit 0 is set.
inline void TricMapper::FME7Clock() {
    if (!(Fme7IrqCtrl & 0x80)) return;
    bool wrap = Fme7Counter == 0;
    Fme7Counter = (ushort)(Fme7Counter - 1);
    if (wrap && (Fme7IrqCtrl & 1)) IrqOut = true;
}

/* Sunsoft-4 (nesdev wiki, INES Mapper 068). Reg[0-3]: 2 KiB CHR banks;
 * Reg[4-5]: CHR ROM nametable pages (D7 forced); Ctrl: $E000; PrgReg: $F000. */
inline void TricMapper::Sunsoft4Apply() {
    MapCHR2(0, Reg[0]); MapCHR2(1, Reg[1]); MapCHR2(2, Reg[2]); MapCHR2(3, Reg[3]);
    MapPRG16(0, PrgReg % 16); MapPRG16(1, -1);
    const byte modes[4] = { TRIC_MIRROR_VERTICAL, TRIC_MIRROR_HORIZONTAL, TRIC_MIRROR_SCREEN_A, TRIC_MIRROR_SCREEN_B };
    Mirroring = modes[Ctrl % 4];
    WRAM_Readable = WRAM_Writable = HasWRAM && (PrgReg & 16);
}
// Which nametable register a quadrant uses, per the mirroring table.
inline int TricMapper::Sunsoft4Page(ushort Address) {
    unsigned q = (Address >> 10) % 4, sel;
    switch (Ctrl % 4) {
    case 0: sel = q % 2; break;
    case 1: sel = q / 2; break;
    case 2: sel = 0; break;
    default: sel = 1; break;
    }
    return (int)((Reg[4 + sel] % Cart.CHRPages) * 1024u + Address % 1024);
}
inline void TricMapper::Sunsoft4Store(ushort Address, byte Input) {
    switch (Address / 4096) {
    case 8: case 9: case 10: case 11: Reg[Address / 4096 - 8] = Input; break;
    case 12: case 13: Reg[Address / 4096 - 8] = (byte)(Input | 128); break;
    case 14: Ctrl = Input; break;
    default: PrgReg = Input; break;
    }
    Sunsoft4Apply();
}

// ---- Sunsoft-3 (nesdev wiki, INES Mapper 067) ----
// mask $F800: $8000 IRQ ack; $8800/$9800/$A800/$B800 2 KiB CHR; $C800 counter
// high then low (like PPUADDR); $D800 bit 4 count/pause, resets the byte
// toggle; $E800 mirroring 0 V / 1 H / 2 1scA / 3 1scB; $F800 16 KiB PRG.
// Counts down each CPU cycle; on $0000 -> $FFFF it disables itself and asserts.
inline void TricMapper::Sunsoft3Store(ushort Address, byte Input) {
    unsigned reg = (Address >> 11) & 31;           // A15-A11
    if (reg == 16) IrqOut = false;                 // $8000
    else if (reg == 17 || reg == 19 || reg == 21 || reg == 23) MapCHR2((reg - 17) / 2, Input);
    else if (reg == 25) {                          // $C800
        if (S3LowNext) S3Counter = (ushort)((S3Counter / 256) * 256 + Input);
        else S3Counter = (ushort)(Input * 256 + S3Counter % 256);
        S3LowNext = !S3LowNext;
    } else if (reg == 27) { IrqEnable = (Input & 16) != 0; S3LowNext = false; }   // $D800
    else if (reg == 29) {                          // $E800
        static const byte m[4] = { TRIC_MIRROR_VERTICAL, TRIC_MIRROR_HORIZONTAL, TRIC_MIRROR_SCREEN_A, TRIC_MIRROR_SCREEN_B };
        Mirroring = m[Input % 4];
    } else if (reg == 31) MapPRG16(0, Input);      // $F800
}
inline void TricMapper::Sunsoft3Clock() {
    if (!IrqEnable) return;
    if (S3Counter == 0) { S3Counter = 0xffff; IrqEnable = false; IrqOut = true; }
    else --S3Counter;
}
