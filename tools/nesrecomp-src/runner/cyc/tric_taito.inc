/* Independent Taito TC0190 / TC0690 connector model (nesdev wiki, INES
 * Mapper 033 and 048). The TC0690's counter is the oracle's MMC3 counter
 * (PPUClock / MMC3ClockIRQ); its output is delayed TaitoDelay CPU cycles. */
inline void TricMapper::TaitoApply() {
    MapPRG8(0, Reg[0] % 64); MapPRG8(1, Reg[1] % 64); MapPRG8(2, -2); MapPRG8(3, -1);
    MapCHR2(0, Reg[2]); MapCHR2(1, Reg[3]);
    MapCHR1(4, Reg[4]); MapCHR1(5, Reg[5]); MapCHR1(6, Reg[6]); MapCHR1(7, Reg[7]);
    Mirroring = (MirrorReg & 64) ? TRIC_MIRROR_HORIZONTAL : TRIC_MIRROR_VERTICAL;
}
inline void TricMapper::TaitoStore(ushort Address, byte Input) {
    if (Address < 0x8000) return;
    unsigned page = (Address >> 13) & (Cart.Mapper == 33 ? 1 : 3);   // TC0190 ignores A14
    unsigned port = Address % 4;
    if (page == 0) {                 // $8000-$9FFF
        Reg[port] = Input;
        if (port == 0 && Cart.Mapper == 33) MirrorReg = Input;
    } else if (page == 1) Reg[4 + port] = Input;             // $A000-$BFFF
    else if (page == 2) {                                   // $C000-$DFFF (TC0690)
        if (port == 0) IrqLatch = (byte)(255 - Input);
        else if (port == 1) { IrqCounter = 0; IrqReload = true; }
        else if (port == 2) IrqEnable = true;
        else { IrqEnable = false; IrqOut = false; TaitoDelay = 0; }
    } else if (port == 0) MirrorReg = Input;                // $E000
    TaitoApply();
}
