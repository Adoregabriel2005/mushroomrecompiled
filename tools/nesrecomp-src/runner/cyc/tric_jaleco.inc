/* Independent Jaleco SS88006 connector model (nesdev wiki, INES Mapper 018).
 * Shares no state with hw_jaleco.inc; the speech chip is not modeled. */
inline void TricMapper::JalecoApply() {
    MapPRG8(0, JPrg[0] % 64); MapPRG8(1, JPrg[1] % 64); MapPRG8(2, JPrg[2] % 64); MapPRG8(3, -1);
    for (uint i = 0; i < 8; ++i) MapCHR1(i, JChr[i]);
    const byte modes[4] = { TRIC_MIRROR_HORIZONTAL, TRIC_MIRROR_VERTICAL, TRIC_MIRROR_SCREEN_A, TRIC_MIRROR_SCREEN_B };
    Mirroring = modes[JMirror % 4];
    WRAM_Readable = HasWRAM && (JRam & 1);
    WRAM_Writable = HasWRAM && (JRam & 1) && (JRam & 2);
}
inline void TricMapper::JalecoStore(ushort Address, byte Input) {
    if (Address < 0x8000) {
        if (Address >= 0x6000 && WRAM_Writable) WRAM[Address % 8192 % WRAM_Length] = Input;
        return;
    }
    byte nib = Input % 16;
    bool high = Address % 2 == 1;
    unsigned page = Address / 4096, port = (Address / 2) % 2;   // page 8..15, port = A1
    byte *bank = nullptr;
    if (page == 8) bank = &JPrg[port];
    else if (page == 9) { if (port == 0) bank = &JPrg[2]; else if (!high) JRam = nib; }
    else if (page <= 13) bank = &JChr[(page - 10) * 2 + port];
    else if (page == 14) JReload[port * 2 + (high ? 1 : 0)] = nib;
    else switch (Address % 4) {
        case 0: JCounter = (ushort)(JReload[0] + JReload[1] * 16 + JReload[2] * 256 + JReload[3] * 4096); IrqOut = false; break;
        case 1: JCtrl = nib; IrqOut = false; break;
        case 2: JMirror = nib % 4; break;
        default: break;
    }
    if (bank) *bank = high ? (byte)(*bank % 16 + nib * 16) : (byte)(*bank - *bank % 16 + nib);
    JalecoApply();
}
// Every M2 cycle while enabled: the selected width counts down, wrapping from
// zero (which interrupts) without touching the higher bits.
inline void TricMapper::JalecoClock() {
    if (!(JCtrl & 1)) return;
    unsigned width = (JCtrl & 8) ? 4 : (JCtrl & 4) ? 8 : (JCtrl & 2) ? 12 : 16;
    unsigned span = 1u << width, low = JCounter % span;
    if (low == 0) IrqOut = true;
    JCounter = (ushort)(JCounter - low + (low + span - 1) % span);
}
