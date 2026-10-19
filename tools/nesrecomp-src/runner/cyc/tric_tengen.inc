// Independent Tengen RAMBO-1 (mapper 64) / 800037 (mapper 158) connector model
// (nesdev wiki, RAMBO-1 and INES Mapper 158).
//   $8000 even: [CPK. RRRR]   $8001 odd: R[RRRR]   $A000 even: mirroring (64)
//   $C000 even: IRQ latch     $C001 odd: mode (bit 0: 0 scanline, 1 CPU/4), reload request
//   $E000 even: IRQ off + acknowledge            $E001 odd: IRQ on
inline void TricMapper::RamboApply() {
    bool swap = (BankSelect & 64) != 0, big = (BankSelect & 32) == 0;
    unsigned flip = (BankSelect & 128) ? 4 : 0;
    MapPRG8(0, swap ? RamboReg[15] : RamboReg[6]);
    MapPRG8(1, RamboReg[7]);
    MapPRG8(2, swap ? RamboReg[6] : RamboReg[15]);
    MapPRG8(3, -1);
    // Pattern page -> register: 2 KiB mode passes PPU A10 through as CHR A10.
    byte pages[8] = {
        (byte)(big ? RamboReg[0] & 254 : RamboReg[0]), (byte)(big ? RamboReg[0] | 1 : RamboReg[8]),
        (byte)(big ? RamboReg[1] & 254 : RamboReg[1]), (byte)(big ? RamboReg[1] | 1 : RamboReg[9]),
        RamboReg[2], RamboReg[3], RamboReg[4], RamboReg[5] };
    for (unsigned page = 0; page < 8; ++page) {
        MapCHR1(page ^ flip, pages[page]);
        ChrA17[page ^ flip] = pages[page] >= 128;   // mapper 158: CHR A17 drives CIRAM A10
    }
    if (Cart.Mapper == 64) Mirroring = (MirrorReg & 1) ? TRIC_MIRROR_HORIZONTAL : TRIC_MIRROR_VERTICAL;
}
inline void TricMapper::RamboStore(ushort Address, byte Input) {
    if (Address < 0x8000) return;
    bool odd = Address & 1;
    switch ((Address >> 13) & 3) {
    case 0: if (odd) RamboReg[BankSelect % 16] = Input; else BankSelect = Input; RamboApply(); break;
    case 1: if (!odd) { MirrorReg = Input; RamboApply(); } break;
    case 2: if (odd) { RamboCycle = Input % 2 == 1; IrqReload = true; RamboPrescale = 0; } else IrqLatch = Input; break;
    case 3: if (odd) IrqEnable = true; else { IrqEnable = false; IrqOut = false; RamboDelay = 0; } break;
    }
}
// One counter clock; Assert is how many CPU cycles later /IRQ goes low.
inline void TricMapper::RamboClockIRQ(byte Assert) {
    if (IrqReload) { IrqCounter = IrqLatch == 0 ? 0 : (byte)(IrqLatch | 1); IrqReload = false; }
    else if (IrqCounter == 0) IrqCounter = IrqLatch;
    else --IrqCounter;
    if (IrqCounter == 0 && IrqEnable && RamboDelay == 0 && !IrqOut) RamboDelay = Assert;
}
inline void TricMapper::RamboCPUClock() {
    if (RamboDelay != 0) { --RamboDelay; if (RamboDelay == 0) IrqOut = true; }
    if (!RamboCycle) return;
    RamboPrescale = (RamboPrescale + 1) % 4;
    if (RamboPrescale == 0) RamboClockIRQ(1);
}
