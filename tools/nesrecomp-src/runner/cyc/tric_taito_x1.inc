/* Independent Taito X1-005 / X1-017 connector model (nesdev wiki, INES
 * Mapper 080, 207, 082; NES 2.0 mapper 552). X1 state: Reg[0-5] CHR,
 * X1Prg[3], X1Mode ($7EF6), X1Key[3] RAM unlock values. The X1-017 IRQ is
 * undocumented and not modeled. */
inline bool TricMapper::X1017() { return Cart.Mapper == 82 || Cart.Mapper == 552; }
inline void TricMapper::X1Apply() {
    for (uint slot = 0; slot < 3; ++slot) {
        uint v = X1Prg[slot], page;
        if (!X1017()) page = v % 64;
        else if (Cart.Mapper == 82) page = (v / 4) % 16;
        else page = ((v >> 5) & 1) | ((v >> 3) & 2) | ((v >> 1) & 4) | ((v << 1) & 8) | ((v << 3) & 16) | ((v << 5) & 32);
        MapPRG8(slot, (int)page);
    }
    MapPRG8(3, -1);
    bool swap = X1017() && (X1Mode & 2);
    uint twoK = swap ? 4 : 0, oneK = swap ? 0 : 4;
    MapCHR1(twoK + 0, Reg[0] & 0xfe); MapCHR1(twoK + 1, Reg[0] | 1);
    MapCHR1(twoK + 2, Reg[1] & 0xfe); MapCHR1(twoK + 3, Reg[1] | 1);
    for (uint i = 0; i < 4; ++i) MapCHR1(oneK + i, Reg[2 + i]);
    if (Cart.Mapper != 207) Mirroring = (X1Mode & 1) ? TRIC_MIRROR_VERTICAL : TRIC_MIRROR_HORIZONTAL;
}
// Chip RAM offset for a CPU address, or -1 when locked or not decoded.
inline int TricMapper::X1Ram(ushort Address) {
    if (Address < 0x6000 || Address >= 0x8000) return -1;
    if (!X1017()) return Address >= 0x7f00 && X1Key[0] == 0xa3 ? (int)(Address % 128) : -1;
    if (Address < 0x6800) return X1Key[0] == 0xca ? Address - 0x6000 : -1;
    if (Address < 0x7000) return X1Key[1] == 0x69 ? Address - 0x6000 : -1;
    if (Address < 0x7400) return X1Key[2] == 0x84 ? Address - 0x6000 : -1;
    return -1;
}
inline void TricMapper::X1Store(ushort Address, byte Input) {
    if (Address >= 0x7ef0 && Address <= 0x7eff) {
        uint port = Address - 0x7ef0;
        if (port < 6) Reg[port] = Input;
        else if (port == 6) X1Mode = Input;
        else if (X1017()) {
            if (port <= 9) X1Key[port - 7] = Input;
            else if (port <= 12) X1Prg[port - 10] = Input;
        } else if (port >= 8) {
            if (port <= 9) X1Key[0] = Input;
            else X1Prg[(port - 10) / 2] = Input;
        }
        X1Apply();
        return;
    }
    int r = X1Ram(Address);
    if (r >= 0 && HasWRAM) WRAM[(uint)r % WRAM_Length] = Input;
}
