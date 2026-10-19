// tric_prelude.inc - C# compatibility shims and the cartridge model used by
// tric_core.cpp. Included once, at the top of that translation unit.
//
// TriCNES models the 72-pin cartridge connector, and calls into the mapper at
// the points a real chip would see: FetchCPU/StoreCPU when the CPU accesses
// the cartridge, AccessPPU and FetchPatternAddress on a PPU bus cycle,
// Connector_CheckCIRAM for the nametable arrangement, and PPUClock/CPUClock/
// CPUClockRise every clock. This keeps the "non tilting" fast paths of
// TriCNES's Mapper base class - a normally seated cartridge - and implements
// the same mapper set the runtime does (hw_mapper.c).
//
// The mapper here is a second implementation, not shared code: its bank
// arithmetic comes from the same nesdev documentation, but the points at which
// it is *called* are TriCNES's. That is what makes it a useful check - above
// all for MMC3, whose IRQ counter clocks off rising edges of PPU A12, so the
// two models have to agree about when the PPU drives each address.

#include "../../common/nes_cart.h"
#include "mmc5_state.h"
#include "../../common/nes_eeprom.h"
#include "../../common/nes_barcode.h"

typedef uint8_t  byte;
typedef int8_t   sbyte;
typedef uint16_t ushort;
typedef uint32_t uint;
typedef uint64_t ulong;

#define LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

extern bool SeventyTwoPinConnector[72];
extern bool ConnectorPinFloating[72];
extern byte VRAM[0x1000];
extern byte dataBus;
extern ushort PPU_AddressBus;
extern byte PPU_OctalLatch;
extern bool PPU_READ;
extern bool IRQ_LevelDetector;
extern uint64_t totalCycles;

// How the cartridge drives CIRAM A10 (connector pin 22). Same numbering as
// HwMirroring in hw_mapper.h, so the two state dumps read alike.
enum { TRIC_MIRROR_HORIZONTAL = 0, TRIC_MIRROR_VERTICAL = 1, TRIC_MIRROR_SCREEN_A = 2, TRIC_MIRROR_SCREEN_B = 3 };

struct TricMapper {
    // ---- connector ----
    ushort CPU_AddressIn;
    byte   CPU_NonTiltData;
    bool   CPU_Drove;              // the cartridge answered this CPU read
    ushort PPU_AddressIn;
    byte   PPU_DataIn;
    byte   PPU_DataOut;
    byte   PPU_NonTiltData;

    // ---- bank tables: byte offsets, PRG in 4KB slots, CHR in 1KB pages ----
    uint PRG_Off[8];
    uint CHR_Off[8];

    // ---- work RAM at $6000-$7FFF ----
    byte WRAM[0x20000];
    NesEeprom Eeprom[2];
    NesBarcode Barcode;
    byte ExRAM[1024];
    uint WRAM_Length, WRAM_Bank;
    bool HasWRAM, WRAM_Readable, WRAM_Writable;

    Mmc5State M5;
    byte Mirroring;

    // ---- MMC1 ----
    byte  Shift, ShiftCount, Ctrl, Chr0, Chr1, PrgReg;
    ulong LastWriteCycle;
    // ---- MMC3 ----
    byte BankSelect, Reg[8], MirrorReg, RamProtect;
    byte IrqLatch, IrqCounter;
    bool IrqReload, IrqEnable, IrqOut;
    bool A12;
    ulong A12LowCycle;
    bool ChrA17[8];                // TxSROM: CHR A17 per pattern page = CIRAM A10
    bool ChrA16[8];                // TQROM: CHR A16 per pattern page selects the RAM chip
    byte ChrLow[8];                // TQROM: the bank's low bits, which address the RAM
    // ---- UxROM / CNROM / AxROM / GxROM ----
    byte Latch;
    bool PatternPending;
    ushort PatternAddress;
    ushort VrcChr[8], VrcLatch16, VrcCounter16;
    int16_t IrqDivider;
    byte IrqMode;
    // ---- Namco 163 / 175 / 340 (the 163's internal RAM is ExRAM[0-127]) ----
    byte NamcoChr[8], NamcoNt[4], NamcoPrg[3], NamcoProtect, NamcoRamEnable;
    byte NamcoCiramChr[8];         // 0: CHR ROM; 1/2: CIRAM page 0/1 as CHR RAM
    byte NamcoSndAddr, NamcoSndStep, NamcoSndCh;
    bool NamcoSndInc;
    ushort NamcoIrq;
    // ---- Taito X1-005 / X1-017 ----
    byte X1Prg[3], X1Mode, X1Key[3];
    // ---- Taito TC0690: IRQ output delay ----
    byte TaitoDelay;
    // ---- Jaleco SS88006 ----
    byte JPrg[3], JChr[8], JRam, JReload[4], JCtrl, JMirror;
    // ---- Tengen RAMBO-1 ----
    byte RamboReg[16], RamboPrescale, RamboDelay;
    // ---- Irem H3001 ----
    ushort H3Counter, H3Reload;
    // ---- Sunsoft-3 ----
    ushort S3Counter;
    bool S3LowNext;
    bool Chr93Off;                 // mapper 93: CHR RAM disabled
    byte OekaInner, OekaOuter;     // mapper 96: CHR banks
    ushort OekaLastBus;            // mapper 96: the PPU address the latch last saw
    bool RamboCycle;
    ushort JCounter;
    // ---- Sunsoft FME-7 ----
    byte Fme7Cmd, Fme7Reg[16], Fme7IrqCtrl;
    ushort Fme7Counter;

    uint MMC5RAM(unsigned Bank);
    void MMC5Apply();
    void MMC5ClearFrame();
    void MMC5Reset();
    void MMC5PCM(byte Value);
    bool MMC5Read(ushort Address, byte &Value, bool SideEffects);
    void MMC5Write(ushort Address, byte Input);
    void MMC5Clock(bool PPUReadAtCPU);
    void MMC5PPURead(ushort Address);
    unsigned MMC5CHR(ushort Address);
    byte MMC5NT(ushort Address, bool Render);
    void MMC5PPU();
    void Reset();
    void MapPRG4(uint Slot, int Bank);
    void MapPRG8(uint Slot, int Bank);
    void MapPRG16(uint Slot, int Bank) { MapPRG8(Slot * 2, Bank * 2); MapPRG8(Slot * 2 + 1, Bank * 2 + 1); }
    void MapPRG32(int Bank) { for (uint i = 0; i < 4; i++) MapPRG8(i, Bank * 4 + (int)i); }
    void MapCHR1(uint Page, int Bank);
    void MapCHR2(uint Page, int Bank) { MapCHR1(Page * 2, Bank * 2); MapCHR1(Page * 2 + 1, Bank * 2 + 1); }
    void MapCHR4(uint Page, int Bank) { for (uint i = 0; i < 4; i++) MapCHR1(Page * 4 + i, Bank * 4 + (int)i); }
    void MapCHR8(int Bank) { for (uint i = 0; i < 8; i++) MapCHR1(i, Bank * 8 + (int)i); }

    bool N163();
    void NamcoApply();
    byte NamcoSound(byte value, bool write);
    bool NamcoLow(ushort Address, byte *Out, const byte *In);
    void NamcoStore(ushort Address, byte Input);
    bool NamcoRamReadable();
    void NamcoClock();
    int  NamcoNtRom(ushort Address);
    bool X1017();
    void X1Apply();
    int  X1Ram(ushort Address);
    void X1Store(ushort Address, byte Input);
    void TaitoApply();
    void IremG101Apply();
    void IremH3001Apply();
    void Sunsoft3Store(ushort Address, byte Input);
    void Sunsoft3Clock();
    void IremH3001Store(ushort Address, byte Input);
    void TaitoStore(ushort Address, byte Input);
    void JalecoApply();
    void JalecoStore(ushort Address, byte Input);
    void JalecoClock();
    void RamboApply();
    void RamboStore(ushort Address, byte Input);
    void RamboClockIRQ(byte Assert);
    void RamboCPUClock();
    void FME7Apply();
    bool FME7Low(ushort Address, byte *Out, const byte *In);
    void FME7Store(ushort Address, byte Input);
    void FME7Clock();
    void Sunsoft4Apply();
    int  Sunsoft4Page(ushort Address);
    void Sunsoft4Store(ushort Address, byte Input);
    void Action52Store(ushort Address, byte Input);
    bool Action52Open;             // mapper 228: the absent third chip is selected
    byte DataReads;                // mapper 185: $2007 reads since power-on
    bool Cnrom185Chr();
    void DataPortRead();
    void MMC1Apply();
    void MMC1Write(ushort Address, byte Input);
    void MMC3Apply();
    void MMC3Write(ushort Address, byte Input);
    void MMC3ClockIRQ();
    bool ChrRamSelected(ushort Address);
    void PatternReadLatch(ushort Address);
    void PatternReadEnd(bool Reading);

    void FetchCPU();
    byte SnoopCPU(ushort Address);
    byte SnoopPPU(ushort Address);
    void StoreCPU(ushort Address, byte Input);
    int  FetchPatternAddress(ushort Address);
    void AccessPPU();
    void PPUClock();
    void CPUClock(bool PPUReadAtCPU);
    bool BandaiBoard();
    void BandaiApply();
    void BandaiWrite(ushort Address, byte Input);
    bool BandaiRead(ushort Address, byte &Value);
    void BandaiClock();
    void DatachLines();
    void VRC7Apply();
    void VRC7Write(ushort Address, byte Input);
    bool VRC6Board();
    unsigned VRC6Nametable(unsigned nt);
    void VRC6Apply();
    void VRC6Write(ushort Address, byte Input);
    bool VRC2Board();
    bool VRC24Board();
    unsigned VRCPort(ushort Address);
    void VRCApply();
    void VRCWrite(ushort Address, byte Input);
    void CPUClockRise() {}
    bool IRQ() { return CartMapperIRQ(); }
    bool CartMapperIRQ();
    bool CheckCIC() { return true; }

    void Connector_SetUpPPUAddressPins() {
        PPU_AddressIn = (ushort)((PPU_AddressBus & 0x3F00) | PPU_OctalLatch);
        SeventyTwoPinConnector[64] = (PPU_AddressBus & 0x2000) != 0;
    }
    void Connector_ReadPPUAddressPins() {}
    void Connector_SetUpPPUDataPins(byte t) { PPU_NonTiltData = t; }
    byte Connector_ReadPPUDataPins(byte) { return PPU_NonTiltData; }
    void Connector_SetUpCPUAddressPins(ushort Address) {
        CPU_AddressIn = Address;
        SeventyTwoPinConnector[49] = (Address & 0x8000) == 0;
    }
    void Connector_ReadCPUAddressPins() {}
    void Connector_SetUpCPUDataPins(byte t) { CPU_NonTiltData = t; }
    byte Connector_ReadCPUDataPins() { return CPU_NonTiltData; }
    void Connector_CheckCIRAM();
    void Connector_PPU_RW(bool RD, bool WR) {
        SeventyTwoPinConnector[20] = !RD;
        SeventyTwoPinConnector[55] = !WR;
    }
};

struct TricCartridge {
    NesCartInfo Info;
    byte  CHRRAM[8192];      // TQROM's separate CHR RAM chip
    byte *PRGROM;
    int   PRGROM_Length;     // the image's own length
    int   PRGSlots;          // 4KB slots in the padded buffer, a power of two
    byte *CHRROM;
    int   CHRROM_Length;
    int   CHRPages;          // 1KB pages in the padded buffer, a power of two
    bool  UsingCHRRAM;
    bool  NametableHorizontalMirroring;
    bool  AlternativeNametableArrangement;
    int   Mapper;
    TricMapper MapperChip;
};

extern TricCartridge Cart;

// ---- bank tables ----
// A negative bank counts from the end of the ROM, and a bank past the end
// wraps, as a board's missing address lines make it (nesdev wiki, per mapper).

inline void TricMapper::MapPRG4(uint Slot, int Bank) {
    PRG_Off[Slot % 8] = ((uint)Bank & ((uint)Cart.PRGSlots - 1)) * 4096;
}

inline void TricMapper::MapPRG8(uint Slot, int Bank) {
    MapPRG4(Slot * 2, Bank * 2);
    MapPRG4(Slot * 2 + 1, Bank * 2 + 1);
}

inline void TricMapper::MapCHR1(uint Page, int Bank) {
    uint n = (uint)Cart.CHRPages;
    CHR_Off[Page & 7] = ((uint)Bank & (n - 1)) * 0x400u;
}

inline bool TricMapper::VRC6Board() { return Cart.Mapper == 24 || Cart.Mapper == 26; }
#include "tric_mmc5.inc"
inline bool TricMapper::CartMapperIRQ() { return Cart.Mapper==5 ? (M5.irq_enable && M5.irq_pending) || (M5.pcm_irq && (M5.pcm_control&128)) || M5.timer_irq : IrqOut; }
#include "tric_vrc.inc"
#include "tric_vrc6.inc"
#include "tric_vrc7.inc"
#include "tric_bandai.inc"
#include "tric_sunsoft.inc"
#include "tric_namco.inc"
#include "tric_jaleco.inc"
#include "tric_tengen.inc"
#include "tric_taito.inc"
#include "tric_taito_x1.inc"

// ---- MMC1 (nesdev wiki, MMC1) ----

inline void TricMapper::MMC1Apply() {
    const byte names[4]={TRIC_MIRROR_SCREEN_A,TRIC_MIRROR_SCREEN_B,TRIC_MIRROR_VERTICAL,TRIC_MIRROR_HORIZONTAL};
    Mirroring=Cart.Info.submapper==7 ? (Cart.Info.vertical?TRIC_MIRROR_VERTICAL:TRIC_MIRROR_HORIZONTAL) : names[Ctrl%4];
    unsigned pins=(Ctrl>=16 && A12)?Chr1:Chr0;
    unsigned group=Cart.PRGSlots>64 ? (pins/16)*16 : 0;
    unsigned banks[2]={PrgReg%16,PrgReg%16}, mode=(Ctrl/4)%4;
    if (mode<2) { banks[0]&=14; banks[1]=banks[0]+1; }
    else if (mode==2) banks[0]=0;
    else banks[1]=15;
    bool a=Cart.Mapper==155 || Cart.Info.submapper==3;
    if (a && PrgReg>=16 && mode>=2) banks[mode==2?0:1]=(PrgReg&8)+(mode==2?0:7);
    for (unsigned slot=0;slot<2;++slot) MapPRG16(slot,group+banks[slot]);
    if (Cart.Info.submapper==5) MapPRG32(0);
    if (Ctrl&16) { MapCHR4(0,Chr0); MapCHR4(1,Chr1); }
    else MapCHR8(Chr0/2);
    unsigned bank=0;
    if (Cart.CHRPages>8 && WRAM_Length>=16384) bank=(pins/16)%2;
    else if (WRAM_Length==32768) bank=(pins/4)%4;
    else if (WRAM_Length==16384) bank=(pins/8)%2;
    WRAM_Bank=bank*8192;
    bool enabled=a || PrgReg<16;
    if (Cart.CHRPages<=8 && Cart.PRGSlots<=64 && WRAM_Length==8192 && pins>=16) enabled=false;
    WRAM_Readable=WRAM_Writable=enabled;
}

inline void TricMapper::MMC1Write(ushort Address, byte Input) {
    if (totalCycles == LastWriteCycle + 1) return;   // ignores back-to-back writes
    LastWriteCycle = totalCycles;
    if (Input & 0x80) {
        Shift = ShiftCount = 0;
        Ctrl |= 0x0C;
        MMC1Apply();
        return;
    }
    Shift = (byte)(Shift >> 1 | (Input & 1) << 4);
    if (++ShiftCount < 5) return;
    byte v = Shift;
    Shift = ShiftCount = 0;
    switch ((Address >> 13) & 3) {
    case 0: Ctrl = v; break;
    case 1: Chr0 = v; break;
    case 2: Chr1 = v; break;
    default: PrgReg = v; break;
    }
    MMC1Apply();
}

// ---- MMC3 (nesdev wiki, MMC3) ----

inline void TricMapper::MMC3Apply() {
    if (BankSelect & 0x40) {
        MapPRG8(0, -2);
        MapPRG8(2, Reg[6]);
    } else {
        MapPRG8(0, Reg[6]);
        MapPRG8(2, -2);
    }
    MapPRG8(1, Reg[7]);
    MapPRG8(3, -1);
    if (Cart.Mapper == 206 && Cart.Info.submapper == 1) MapPRG32(0);
    uint big = (BankSelect & 0x80) ? 4u : 0u;
    uint small_ = big ^ 4u;
    // NAMCOT-3433/3453 (88, 154): PPU A12 drives CHR A16 (nesdev wiki, INES Mapper 088).
    bool a16 = Cart.Mapper == 88 || Cart.Mapper == 154;
    uint big_hi = a16 ? (big ? 64u : 0u) : 0u, small_hi = a16 ? (small_ ? 64u : 0u) : 0u;
    uint big_mask = a16 ? 63u : 255u;
    MapCHR2(big / 2 + 0, (int)(((Reg[0] & big_mask) | big_hi) >> 1));
    MapCHR2(big / 2 + 1, (int)(((Reg[1] & big_mask) | big_hi) >> 1));
    MapCHR1(small_ + 0, (int)((Reg[2] & big_mask) | small_hi));
    MapCHR1(small_ + 1, (int)((Reg[3] & big_mask) | small_hi));
    MapCHR1(small_ + 2, (int)((Reg[4] & big_mask) | small_hi));
    MapCHR1(small_ + 3, (int)((Reg[5] & big_mask) | small_hi));
    // The chip's CHR A17 pin for each pattern page, before any ROM-size wrap
    // (nesdev wiki, TxSROM: the pin drives CIRAM A10 on that board).
    for (uint i = 0; i < 4; ++i) {
        ChrA17[big + i] = (Reg[i / 2] & 0x80) != 0;
        ChrA17[small_ + i] = (Reg[2 + i] & 0x80) != 0;
        ChrA16[big + i] = (Reg[i / 2] & 0x40) != 0;
        ChrA16[small_ + i] = (Reg[2 + i] & 0x40) != 0;
        ChrLow[big + i] = (byte)((Reg[i / 2] & 6) | (i & 1));
        ChrLow[small_ + i] = (byte)(Reg[2 + i] & 7);
    }
    // nesdev wiki, TQROM: with A16 low the CHR ROM sees bank bits 0-5 only.
    if (Cart.Mapper == 119)
        for (uint i = 0; i < 8; ++i)
            if (!ChrA16[i]) CHR_Off[i] %= 65536u;
}
// TQROM: A16 high enables the 6264 SRAM instead of the ROM (nesdev wiki).
inline bool TricMapper::ChrRamSelected(ushort Address) {
    if (Cart.Mapper == 77) return ((Address >> 10) & 7) >= 2;   // LROG017: RAM at $0800-$1FFF
    return Cart.Mapper == 119 && ChrA16[(Address >> 10) & 7];
}

inline void TricMapper::MMC3Write(ushort Address, byte Input) {
    switch (Address & 0xE001) {
    case 0x8000: BankSelect = Input; MMC3Apply(); break;
    case 0x8001: Reg[BankSelect & 7] = Input; MMC3Apply(); break;
    case 0xA000:
        MirrorReg = Input;
        if (Cart.Mapper != 118)   // TxSROM leaves the mirroring output unconnected
            Mirroring = (Input & 1) ? TRIC_MIRROR_HORIZONTAL : TRIC_MIRROR_VERTICAL;
        break;
    case 0xA001:
        RamProtect = Input;
        WRAM_Readable = (Input & 0x80) != 0;
        WRAM_Writable = (Input & 0x80) != 0 && (Input & 0x40) == 0;
        break;
    case 0xC000: IrqLatch = Input; break;
    case 0xC001: IrqCounter = 0; IrqReload = true; break;
    case 0xE000: IrqEnable = false; IrqOut = false; break;
    default: IrqEnable = true; break;
    }
}

// MMC3B/C: reload when zero or requested, else decrement; assert on zero.
void TricMmc3TraceClock(byte counter, bool out, ushort vbus);

inline void TricMapper::MMC3ClockIRQ() {
    if (IrqCounter == 0 || IrqReload) IrqCounter = IrqLatch;
    else IrqCounter--;
    if (IrqCounter == 0 && IrqEnable) {
        if (Cart.Mapper != 48) IrqOut = true;
        else if (!TaitoDelay && !IrqOut) TaitoDelay = 22;  // observed: Flintstones' split is clean for 20-24
    }
    IrqReload = false;
    TricMmc3TraceClock(IrqCounter, IrqOut, PPU_AddressBus);
}

// The MMC3 counts rising edges of PPU A12, ignoring an edge unless A12 has
// been low for at least three CPU cycles. TriCNES calls this once per PPU
// clock with its own address bus, which is the independent part of the check.
inline void TricMapper::PPUClock() {
    if (Cart.Mapper==1 || Cart.Mapper==155) {
        bool high=(PPU_AddressBus&4096)!=0;
        if (A12!=high) { A12=high; MMC1Apply(); }
    }
    // Oeka Kids (nesdev INES Mapper 096): moving the PPU address into $2xxx from
    // outside it latches A9-A8 as the inner 4 KiB CHR bank at $0000.
    if (Cart.Mapper == 96) {
        if ((OekaLastBus & 0x3000) != 0x2000 && (PPU_AddressBus & 0x3000) == 0x2000) {
            OekaInner = (byte)((PPU_AddressBus >> 8) & 3);
            MapCHR4(0, OekaOuter + OekaInner);
        }
        OekaLastBus = PPU_AddressBus;
    }
    if (Cart.Mapper==153 || Cart.Mapper==157) {
        unsigned select=(PPU_AddressBus/1024)%4;
        if (BankSelect!=select) { BankSelect=select; if (Cart.Mapper==157) DatachLines(); else BandaiApply(); }
    }
    bool rambo = Cart.Mapper == 64 || Cart.Mapper == 158;
    if (Cart.Mapper != 4 && Cart.Mapper != 118 && Cart.Mapper != 119 && Cart.Mapper != 48 && !rambo) return;
    if (!(PPU_AddressBus & 0x1000)) {
        if (A12) { A12 = false; A12LowCycle = totalCycles; }
        return;
    }
    if (!A12) {
        A12 = true;
        if (totalCycles - A12LowCycle >= 3) {
            if (!rambo) MMC3ClockIRQ();
            else if (!RamboCycle) RamboClockIRQ(2);   // /IRQ after M2 falls twice
        }
    }
}

// ---- Active Enterprises (nesdev wiki, INES Mapper 228) ----
// Address 1.MHHPPP PPS.CCCC plus D1-D0. The 1.5 MiB image stores chips 0, 1
// and 3; chip 2 is not fitted.
inline void TricMapper::Action52Store(ushort Address, byte Input) {
    unsigned chip = (Address >> 11) % 4, page = (Address >> 6) % 32;
    bool three = Cart.PRGROM_Length == 3 * 524288;
    Action52Open = three && chip == 2;
    unsigned base = (three && chip == 3 ? 2 : chip) * 32 + page;
    bool single = (Address & 32) != 0;
    MapPRG16(0, (int)(single ? base : base - base % 2));
    MapPRG16(1, (int)(single ? base : base - base % 2 + 1));
    MapCHR8((int)((Address % 16) * 4 + Input % 4));
    Mirroring = (Address & 0x2000) ? TRIC_MIRROR_HORIZONTAL : TRIC_MIRROR_VERTICAL;
}

// ---- CNROM + copy protection (nesdev wiki, INES Mapper 185) ----
inline void TricMapper::DataPortRead() { if (Cart.Mapper == 185 && DataReads != 255) DataReads++; }
inline bool TricMapper::Cnrom185Chr() {
    if (Cart.Info.submapper >= 4) return Latch % 4 == Cart.Info.submapper - 4;
    return DataReads >= 3;   // submapper 0: the first two $2007 fetches see no CHR
}

// ---- Irem H3001 (nesdev wiki, INES Mapper 065) ----
// $8000/$A000 PRG; $9000 [X...] puts the $8000 bank at $C000 and $3E at $8000;
// $9001 [MM..] 00 V / 10 H / x1 1scA; $9003 [E...] enable; $9004 reload;
// $9005/$9006 reload high/low; $B000-$B007 1 KiB CHR. 16-bit down counter,
// one per CPU cycle while enabled, IRQ on reaching 0, stops at 0.
inline void TricMapper::IremH3001Apply() {
    bool alt = Ctrl >= 128;
    MapPRG8(alt ? 2 : 0, Reg[0]);
    MapPRG8(alt ? 0 : 2, 0x3e);
    MapPRG8(1, Reg[1]);
    MapPRG8(3, 0x3f);
    switch (MirrorReg / 64) {
    case 0: Mirroring = TRIC_MIRROR_VERTICAL; break;
    case 2: Mirroring = TRIC_MIRROR_HORIZONTAL; break;
    default: Mirroring = TRIC_MIRROR_SCREEN_A; break;
    }
}
inline void TricMapper::IremH3001Store(ushort Address, byte Input) {
    unsigned group = Address >> 12;
    if (group == 8) Reg[0] = Input;
    else if (group == 10) Reg[1] = Input;
    else if (group == 11) { MapCHR1(Address % 8, Input); return; }
    else if (group == 9) {
        unsigned r = Address % 8;
        if (r == 0) Ctrl = Input;
        else if (r == 1) MirrorReg = Input;
        else if (r == 3) { IrqEnable = Input >= 128; IrqOut = false; return; }
        else if (r == 4) { H3Counter = H3Reload; IrqOut = false; return; }
        else if (r == 5) { H3Reload = (ushort)((H3Reload % 256) + Input * 256); return; }
        else if (r == 6) { H3Reload = (ushort)((H3Reload / 256) * 256 + Input); return; }
        else return;
    } else return;
    IremH3001Apply();
}

// ---- Irem G-101 (nesdev wiki, INES Mapper 032) ----
inline void TricMapper::IremG101Apply() {
    bool fixed = Cart.Info.submapper == 1;             // Major League
    if ((Ctrl & 2) && !fixed) { MapPRG8(0, -2); MapPRG8(2, Reg[0] % 32); }
    else { MapPRG8(0, Reg[0] % 32); MapPRG8(2, -2); }
    MapPRG8(1, Reg[1] % 32); MapPRG8(3, -1);
    if (fixed) Mirroring = TRIC_MIRROR_SCREEN_B;       // CIRAM A10 tied high
    else Mirroring = (Ctrl & 1) ? TRIC_MIRROR_HORIZONTAL : TRIC_MIRROR_VERTICAL;
}

// ---- reset ----

inline void TricMapper::Reset() {
    for (int i = 0; i < 8; i++) PRG_Off[i] = 0;
    for (int i = 0; i < 8; i++) CHR_Off[i] = 0;
    for (int i = 0; i < 8; i++) Reg[i] = 0;
    Shift = ShiftCount = Ctrl = Chr0 = Chr1 = PrgReg = 0;
    LastWriteCycle = 0;
    BankSelect = MirrorReg = RamProtect = 0;
    IrqLatch = IrqCounter = 0;
    IrqReload = IrqEnable = IrqOut = false;
    A12 = false;
    A12LowCycle = 0;
    memset(ChrA17, 0, sizeof(ChrA17));
    memset(ChrA16, 0, sizeof(ChrA16));
    memset(ChrLow, 0, sizeof(ChrLow));
    Latch = 0;
    PatternPending = false;
    PatternAddress = 0;
    memset(VrcChr, 0, sizeof(VrcChr));
    VrcLatch16 = VrcCounter16 = 0;
    IrqDivider = 341; IrqMode = 0;
    Fme7Cmd = Fme7IrqCtrl = 0; memset(Fme7Reg, 0, sizeof(Fme7Reg)); Fme7Counter = 0;
    Action52Open = false;
    DataReads = 0;
    memset(JPrg, 0, sizeof(JPrg)); memset(JChr, 0, sizeof(JChr)); memset(JReload, 0, sizeof(JReload));
    JRam = JCtrl = JMirror = 0; JCounter = 0;
    memset(RamboReg, 0, sizeof(RamboReg)); RamboPrescale = RamboDelay = 0; RamboCycle = false;
    H3Counter = H3Reload = 0;
    S3Counter = 0; S3LowNext = false; Chr93Off = false;
    OekaInner = OekaOuter = 0; OekaLastBus = 0;
    TaitoDelay = 0;
    memset(X1Prg, 0, sizeof(X1Prg)); memset(X1Key, 0, sizeof(X1Key)); X1Mode = 0;
    memset(NamcoChr, 0, sizeof(NamcoChr)); memset(NamcoNt, 0, sizeof(NamcoNt)); memset(NamcoPrg, 0, sizeof(NamcoPrg));
    memset(NamcoCiramChr, 0, sizeof(NamcoCiramChr));
    NamcoProtect = NamcoRamEnable = NamcoSndAddr = NamcoSndStep = 0; NamcoSndCh = 7; NamcoSndInc = false; NamcoIrq = 0;
    CPU_Drove = false;
    if (!Cart.Info.battery && !Cart.Info.prg_nvram) memset(ExRAM,0,sizeof(ExRAM));
    WRAM_Length = Cart.Info.prg_ram + Cart.Info.prg_nvram;
    WRAM_Bank = 0;
    memset(WRAM, 0, Cart.Info.prg_nvram ? Cart.Info.prg_ram : sizeof(WRAM));

    Mirroring = Cart.NametableHorizontalMirroring ? TRIC_MIRROR_HORIZONTAL : TRIC_MIRROR_VERTICAL;
    HasWRAM = WRAM_Length != 0 && (!BandaiBoard() || Cart.Mapper==153);
    for (unsigned chip=0;chip<2;++chip) nes_eeprom_reset(&Eeprom[chip]);
    memset(&Barcode,0,sizeof(Barcode));
    WRAM_Readable = WRAM_Writable = HasWRAM;

    switch (Cart.Mapper) {
    case 5: MMC5Reset(); break;
    case 69: FME7Apply(); break;
    case 19: case 210: NamcoApply(); break;
    case 18: JalecoApply(); break;
    case 64: case 158: RamboApply(); break;
    case 33: case 48: TaitoApply(); break;
    case 32: IremG101Apply(); MapCHR8(0); break;
    case 65: Reg[0] = 0; Reg[1] = 1; IremH3001Apply(); MapCHR8(0); break;
    case 80: case 207: case 82: case 552: X1Apply(); break;
    case 68: Sunsoft4Apply(); break;
    case 67: MapPRG16(0, 0); MapPRG16(1, -1); MapCHR8(0); break;
    case 41: MapPRG32(0); MapCHR8(0); Mirroring = TRIC_MIRROR_VERTICAL; break;
    case 228: Action52Store(0x8000, 0); break;
    case 16: case 159: case 153: case 157: BandaiApply(); break;
    case 85: Reg[1]=1; Reg[2]=2; VRC7Apply(); break;
    case 24: case 26: Ctrl=0x20; VRC6Apply(); break;
    case 21: case 22: case 23: case 25: Reg[1] = 1; VRCApply(); break;
    case 73: MapPRG16(0, 0); MapPRG16(1, -1); MapCHR8(0); break;
    case 40:
        MapPRG8(0,4); MapPRG8(1,5); MapPRG8(2,0); MapPRG8(3,7); MapCHR8(0); break;
    case 31:
        for (uint i=0; i<8; ++i) MapPRG4(i, i == 7 ? 255 : 0);
        MapCHR8(0); break;
    case 9:
        MapPRG8(0, 0); MapPRG8(1, -3); MapPRG8(2, -2); MapPRG8(3, -1);
        Chr0 = Chr1 = 1; MapCHR4(0, 0); MapCHR4(1, 0); break;
    case 10:
        MapPRG16(0, 0); MapPRG16(1, -1);
        Chr0 = Chr1 = 1; MapCHR4(0, 0); MapCHR4(1, 0); break;
    case 13: MapPRG32(0); MapCHR4(0, 0); MapCHR4(1, 0); Mirroring = TRIC_MIRROR_VERTICAL; break;
    case 71: MapPRG16(0, 0); MapPRG16(1, -1); MapCHR8(0); break;
    case 75: MapPRG32(0); MapPRG8(3, -1); MapCHR4(0, 0); MapCHR4(1, 0); Mirroring = TRIC_MIRROR_VERTICAL; break;
    case 206: case 88: case 95: Reg[7] = 1; MMC3Apply(); break;
    case 154: Reg[7] = 1; MMC3Apply(); Mirroring = TRIC_MIRROR_SCREEN_A; break;
    case 76: MapPRG16(0, 0); MapPRG16(1, -1); Reg[7] = 1; for (uint j = 0; j < 4; ++j) MapCHR2(j, 0); break;
    case 94: MapPRG16(0, 0); MapPRG16(1, -1); MapCHR8(0); break;
    case 180: MapPRG16(0, 0); MapPRG16(1, 0); MapCHR8(0); break;
    case 78: case 89: case 93: case 72: case 92: MapPRG16(0, 0); MapPRG16(1, -1); MapCHR8(0); break;
    case 97: MapPRG16(0, -1); MapPRG16(1, 0); MapCHR8(0); break;
    case 86: case 101: MapPRG32(0); MapCHR8(0); break;
    case 77: MapPRG32(0); MapCHR1(0, 0); MapCHR1(1, 1); for (uint i = 2; i < 8; ++i) ChrLow[i] = (byte)(i - 2); break;
    case 96: MapPRG32(0); MapCHR4(0, 0); MapCHR4(1, 3); break;
    case 70: case 152:
        MapPRG16(0, 0); MapPRG16(1, -1); MapCHR8(0);
        if (Cart.Mapper == 152) Mirroring = TRIC_MIRROR_SCREEN_A;
        break;
    case 184: MapPRG32(0); MapCHR4(0, 0); MapCHR4(1, 4); break;
    case 232: MapPRG16(0, 0); MapPRG16(1, 3); MapCHR8(0); break;
    case 1: case 155:
        Ctrl = 0x0C;   // PRG mode 3, the state a bit-7 write leaves
        MMC1Apply();
        break;
    case 2:
        MapPRG16(0, 0);
        MapPRG16(1, -1);
        MapCHR8(0);
        break;
    case 4: case 118: case 119:
        Reg[7] = 1;
        MMC3Apply();
        Mirroring = TRIC_MIRROR_VERTICAL;
        break;
    case 7:
        MapPRG32(0);
        MapCHR8(0);
        Mirroring = TRIC_MIRROR_SCREEN_A;
        break;
    default:       // NROM, CNROM, GxROM
        MapPRG32(0);
        MapCHR8(0);
        break;
    }
}

// ---- connector ----

inline void TricMapper::FetchCPU() {
    CPU_Drove = false;
    if (Cart.Mapper==40 && CPU_AddressIn>=0x6000 && CPU_AddressIn<0x8000) {
        CPU_NonTiltData=Cart.PRGROM[(6%(Cart.PRGSlots/2))*8192+CPU_AddressIn%8192]; CPU_Drove=true; return;
    }
    if (Cart.Mapper==5) { CPU_Drove=MMC5Read(CPU_AddressIn,CPU_NonTiltData,true); return; }
    if (Cart.Mapper==69 && CPU_AddressIn>=0x6000 && CPU_AddressIn<0x8000) { CPU_Drove=FME7Low(CPU_AddressIn,&CPU_NonTiltData,nullptr); return; }
    if ((Cart.Mapper==80 || Cart.Mapper==207 || Cart.Mapper==82 || Cart.Mapper==552) && CPU_AddressIn < 0x8000) {
        int r = X1Ram(CPU_AddressIn);
        if (r >= 0 && HasWRAM) { CPU_NonTiltData = WRAM[(uint)r % WRAM_Length]; CPU_Drove = true; }
        return;
    }
    if ((Cart.Mapper==19 || Cart.Mapper==210) && CPU_AddressIn < 0x8000) {
        if (CPU_AddressIn >= 0x6000) {
            if (NamcoRamReadable()) { CPU_NonTiltData = WRAM[(CPU_AddressIn % 8192) % WRAM_Length]; CPU_Drove = true; }
        } else CPU_Drove = NamcoLow(CPU_AddressIn, &CPU_NonTiltData, nullptr);
        return;
    }
    if (BandaiBoard() && CPU_AddressIn<0x8000) { CPU_Drove=BandaiRead(CPU_AddressIn,CPU_NonTiltData); return; }
    if (VRC24Board() && CPU_AddressIn >= 0x6000 && CPU_AddressIn < 0x8000) {
        if (VRC2Board() && !HasWRAM) {
            if (CPU_AddressIn < 0x7000) {
                CPU_NonTiltData = (CPU_NonTiltData & 0xfe) | Latch;
                CPU_Drove = true;
            }
            return;
        }
        if (!VRC2Board() && WRAM_Length == 2048 && CPU_AddressIn >= 0x7000) return;
    }
    if (!SeventyTwoPinConnector[49] && Cart.Mapper == 228 && Action52Open) return;   // open bus
    if (!SeventyTwoPinConnector[49]) {    // A15 high: PRG ROM
        CPU_NonTiltData = Cart.PRGROM[PRG_Off[(CPU_AddressIn >> 12) & 7] | (CPU_AddressIn & 0xFFF)];
        CPU_Drove = true;
    } else if (CPU_AddressIn >= 0x6000 && HasWRAM && WRAM_Readable) {
        CPU_NonTiltData = WRAM[(WRAM_Bank + (CPU_AddressIn & 0x1FFF)) % WRAM_Length];
        CPU_Drove = true;
    }
}

inline byte TricMapper::SnoopCPU(ushort Address) {
    if (Cart.Mapper==40 && Address>=0x6000 && Address<0x8000)
        return Cart.PRGROM[(6%(Cart.PRGSlots/2))*8192+Address%8192];
    if (Cart.Mapper==5) { byte v=dataBus; MMC5Read(Address,v,false); return v; }
    if (Cart.Mapper==69 && Address>=0x6000 && Address<0x8000) { byte v=dataBus; FME7Low(Address,&v,nullptr); return v; }
    if ((Cart.Mapper==80 || Cart.Mapper==207 || Cart.Mapper==82 || Cart.Mapper==552) && Address < 0x8000) {
        int r = X1Ram(Address);
        return r >= 0 && HasWRAM ? WRAM[(uint)r % WRAM_Length] : dataBus;
    }
    if ((Cart.Mapper==19 || Cart.Mapper==210) && Address >= 0x6000 && Address < 0x8000)
        return NamcoRamReadable() ? WRAM[(Address % 8192) % WRAM_Length] : dataBus;
    if (Cart.Mapper==19 && Address >= 0x4800 && Address < 0x6000) {
        if (Address < 0x5000) return ExRAM[NamcoSndAddr % 128];   // peek: no increment
        return Address < 0x5800 ? (byte)(NamcoIrq & 0xff) : (byte)(NamcoIrq >> 8);
    }
    if (BandaiBoard() && Address<0x8000) { byte v=dataBus; BandaiRead(Address,v); return v; }
    if (VRC24Board() && Address >= 0x6000 && Address < 0x8000) {
        if (VRC2Board() && !HasWRAM) return Address < 0x7000 ? (dataBus & 0xfe) | Latch : dataBus;
        if (!VRC2Board() && WRAM_Length == 2048 && Address >= 0x7000) return dataBus;
    }
    if (Address >= 0x8000 && Cart.Mapper == 228 && Action52Open) return dataBus;
    if (Address >= 0x8000) return Cart.PRGROM[PRG_Off[(Address >> 12) & 7] | (Address & 0xFFF)];
    if (Address >= 0x6000 && HasWRAM && WRAM_Readable) return WRAM[(WRAM_Bank + (Address & 0x1FFF)) % WRAM_Length];
    return dataBus;
}

inline void TricMapper::StoreCPU(ushort Address, byte Input) {
    if (Cart.Mapper==40) {
        switch (Address/8192) {
        case 4: IrqEnable=IrqOut=false; VrcCounter16=0; break;
        case 5:
            if (!IrqEnable) LastWriteCycle=totalCycles+1;
            IrqEnable=true; break;
        case 7: PrgReg=Input%8; MapPRG8(2,PrgReg); break;
        }
        return;
    }
    if (Cart.Mapper==5) { MMC5Write(Address,Input); return; }
    if (Cart.Mapper==69) { FME7Store(Address,Input); return; }
    if (Cart.Mapper==19 || Cart.Mapper==210) { NamcoStore(Address,Input); return; }
    if (Cart.Mapper==18) { JalecoStore(Address,Input); return; }
    if (Cart.Mapper==64 || Cart.Mapper==158) { RamboStore(Address,Input); return; }
    if (Cart.Mapper==65) { IremH3001Store(Address,Input); return; }
    if (Cart.Mapper==67) { Sunsoft3Store(Address,Input); return; }
    if (Cart.Mapper==80 || Cart.Mapper==207 || Cart.Mapper==82 || Cart.Mapper==552) {
        if (Address < 0x8000) X1Store(Address, Input);
        return;
    }
    if (Cart.Mapper==33 || Cart.Mapper==48) { TaitoStore(Address,Input); return; }
    if (BandaiBoard()) { BandaiWrite(Address,Input); return; }
    if (VRC24Board() && Address >= 0x6000 && Address < 0x8000) {
        if (VRC2Board() && !HasWRAM) {
            if (Address < 0x7000) Latch = Input%2;
            return;
        }
        if (!VRC2Board() && WRAM_Length == 2048 && Address >= 0x7000) return;
    }
    if (Cart.Mapper == 41 && Address >= 0x6000) {   // nesdev wiki, INES Mapper 041
        if (Address < 0x6800) {
            PrgReg = Address % 8; Chr0 = (Address / 8) % 4;
            Mirroring = (Address & 32) ? TRIC_MIRROR_HORIZONTAL : TRIC_MIRROR_VERTICAL;
        } else if (Address >= 0x8000 && PrgReg >= 4) Latch = (byte)(Input & SnoopCPU(Address) % 4);
        else return;
        MapPRG32(PrgReg); MapCHR8(Chr0 * 4 + Latch);
        return;
    }
    if (Cart.Mapper == 31 && Address / 4096 == 5) {
        Reg[Address % 8] = Input;
        MapPRG4(Address % 8, Input);
        return;
    }
    if (Cart.Mapper == 34) {
        if (!nes_cart_nina(&Cart.Info) && Address >= 0x8000) {
            Latch = (byte)(Input & SnoopCPU(Address));
            MapPRG32(Latch);
        } else if (nes_cart_nina(&Cart.Info)) {
            switch (Address) {
            case 0x7ffd: MapPRG32(Input % 2); break;
            case 0x7ffe: MapCHR4(0, Input % 16); break;
            case 0x7fff: MapCHR4(1, Input % 16); break;
            }
        }
    }
    if ((Cart.Mapper == 79 || Cart.Mapper == 146) && (Address & 0xe100) == 0x4100) {   // 146: Sachen 3015, same latch
        Latch = Input;
        MapPRG32((Input / 8) % 2);
        MapCHR8(Input % 8);
        return;
    }
    // Jaleco JF-13 (nesdev INES Mapper 086): $6000-$6FFF [.CPP ..CC]; $7000 speech (not modeled).
    if (Cart.Mapper == 86 && Address >= 0x6000 && Address < 0x8000) {
        if (Address < 0x7000) { MapPRG32((Input / 16) % 4); MapCHR8(Input % 4 + ((Input & 64) ? 4 : 0)); }
        return;
    }
    // Mapper 101 (nesdev INES Mapper 101): $6000-$7FFF selects 8 KiB CHR, bits in order.
    if (Cart.Mapper == 101 && Address >= 0x6000 && Address < 0x8000) { MapCHR8(Input); return; }
    if (Cart.Mapper == 87 && Address >= 0x6000 && Address < 0x8000) {
        MapCHR8((Input % 2) * 2 + (Input / 2) % 2);
        return;
    }
    if (Cart.Mapper == 113 && (Address & 0xe100) == 0x4100) {
        Latch = Input;
        MapPRG32((Input / 8) % 8);
        MapCHR8(Input % 8 + ((Input & 64) ? 8 : 0));
        Mirroring = (Input & 128) ? TRIC_MIRROR_VERTICAL : TRIC_MIRROR_HORIZONTAL;
        return;
    }
    if (Cart.Mapper == 140 && (Address & 0xe000) == 0x6000) {
        MapPRG32((Input / 16) % 4);
        MapCHR8(Input % 16);
        return;
    }
    if (Cart.Mapper == 184 && Address / 0x2000 == 3) {
        MapCHR4(0, Input % 8);
        MapCHR4(1, 4 + (Input / 16) % 4);
        return;
    }
    if (Address >= 0x6000 && Address < 0x8000) {
        if (HasWRAM && WRAM_Writable) WRAM[(WRAM_Bank + (Address & 0x1FFF)) % WRAM_Length] = Input;
        return;
    }
    if (Address < 0x8000) return;
    if ((Cart.Mapper == 2 || Cart.Mapper == 3 || Cart.Mapper == 7) && Cart.Info.submapper == 2)
        Input &= SnoopCPU(Address);
    switch (Cart.Mapper) {
    case 85: VRC7Write(Address,Input); break;
    case 24: case 26: VRC6Write(Address, Input); break;
    case 21: case 22: case 23: case 25: case 73: VRCWrite(Address, Input); break;
    case 9: case 10:
        switch (Address / 4096) {
        case 10:
            PrgReg = Input % 16;
            if (Cart.Mapper == 9) MapPRG8(0, PrgReg); else MapPRG16(0, PrgReg);
            break;
        case 11: case 12: case 13: case 14:
            Reg[Address / 4096 - 11] = Input % 32;
            MapCHR4(0, Reg[Chr0]); MapCHR4(1, Reg[2 + Chr1]);
            break;
        case 15: Mirroring = (Input & 1) ? TRIC_MIRROR_HORIZONTAL : TRIC_MIRROR_VERTICAL; break;
        }
        break;
    case 11: case 144:   // nesdev INES Mapper 011; 144: EffectiveData = ROM & (Written | 1)
        Input = (byte)((Cart.Mapper == 144 ? (Input | 1) : Input) & SnoopCPU(Address));
        Latch = Input;
        MapPRG32(Input % 4);
        MapCHR8(Input / 16);
        break;
    case 13:
        Latch = (byte)(Input & SnoopCPU(Address));
        MapCHR4(1, Latch % 4);
        break;
    case 71:
        if (Address >= 0xc000) MapPRG16(0, Input % 16);
        else if ((!Cart.Info.nes2 && (Address >> 12) == 9) ||
                 (Cart.Info.nes2 && Cart.Info.submapper == 1 && Address < 0xa000))
            Mirroring = (Input & 16) ? TRIC_MIRROR_SCREEN_B : TRIC_MIRROR_SCREEN_A;
        break;
    case 75:
        switch (Address >> 12) {
        case 8: MapPRG8(0, Input % 16); break;
        case 10: MapPRG8(1, Input % 16); break;
        case 12: MapPRG8(2, Input % 16); break;
        case 9:
            Ctrl = Input;
            Mirroring = (Input & 1) ? TRIC_MIRROR_HORIZONTAL : TRIC_MIRROR_VERTICAL;
            break;
        case 14: Chr0 = Input % 16; break;
        case 15: Chr1 = Input % 16; break;
        }
        MapCHR4(0, Chr0 + ((Ctrl & 2) ? 16 : 0));
        MapCHR4(1, Chr1 + ((Ctrl & 4) ? 16 : 0));
        break;
    case 206: case 88: case 95: case 154:
        if (Cart.Mapper == 154) Mirroring = (Input & 64) ? TRIC_MIRROR_SCREEN_B : TRIC_MIRROR_SCREEN_A;  // [.N.. ....] at $8000-$FFFF
        if ((Address & 0xe000) == 0x8000) {
            if (Address % 2 == 0) BankSelect = Input % 8;
            else Reg[BankSelect] = Input % (BankSelect < 6 ? 64 : 16);
            MMC3Apply();
        }
        break;
    case 76:
        if (Address >= 0x8000 && Address <= 0x9fff) {
            if (Address & 1) Reg[BankSelect] = Input % 64;
            else BankSelect = Input % 8;
            MapPRG8(0, Reg[6] % 16); MapPRG8(1, Reg[7] % 16);
            MapCHR2(0, Reg[2]); MapCHR2(1, Reg[3]);
            MapCHR2(2, Reg[4]); MapCHR2(3, Reg[5]);
        }
        break;
    case 94:
        Latch = (byte)(Input & SnoopCPU(Address));
        MapPRG16(0, (Latch / 4) % 8);
        break;
    case 180:
        Latch = (byte)(Input & SnoopCPU(Address));
        MapPRG16(1, Latch % 8);
        break;
    case 78:   // nesdev INES Mapper 078: [CCCC MPPP]; sub 3 M = H/V, else 1scA/1scB; bus conflicts
        Latch = (byte)(Input & SnoopCPU(Address));
        MapPRG16(0, Latch % 8); MapCHR8(Latch / 16);
        if (Cart.Info.submapper == 3) Mirroring = (Latch & 8) ? TRIC_MIRROR_VERTICAL : TRIC_MIRROR_HORIZONTAL;
        else Mirroring = (Latch & 8) ? TRIC_MIRROR_SCREEN_B : TRIC_MIRROR_SCREEN_A;
        break;
    case 89:   // nesdev INES Mapper 089: [CPPP MCCC]; bus conflicts
        Latch = (byte)(Input & SnoopCPU(Address));
        MapPRG16(0, (Latch / 16) % 8); MapCHR8(Latch % 8 + (Latch >= 128 ? 8 : 0));
        Mirroring = (Latch & 8) ? TRIC_MIRROR_SCREEN_B : TRIC_MIRROR_SCREEN_A;
        break;
    case 93:   // nesdev INES Mapper 093: [.PPP ...E]; E = CHR RAM enable; bus conflicts
        Latch = (byte)(Input & SnoopCPU(Address));
        MapPRG16(0, (Latch / 16) % 8); Chr93Off = Latch % 2 == 0;
        break;
    case 97:   // nesdev INES Mapper 097: $8000-$BFFF [M..P PPPP]; 16 KiB at $C000; M 0 = H, 1 = V
        if (Address < 0xc000) {
            MapPRG16(1, Input % 32);
            Mirroring = Input >= 128 ? TRIC_MIRROR_VERTICAL : TRIC_MIRROR_HORIZONTAL;
        }
        break;
    case 148:  // nesdev INES Mapper 148: $8000-$FFFF [...PCCC]; bus conflicts
        Latch = (byte)(Input & SnoopCPU(Address));
        MapPRG32((Latch / 8) % 2); MapCHR8(Latch % 8);
        break;
    case 77:   // nesdev INES Mapper 077: [CCCC PPPP]; 2 KiB CHR ROM at $0000, RAM above; bus conflicts
        Latch = (byte)(Input & SnoopCPU(Address));
        MapPRG32(Latch % 16); MapCHR1(0, (Latch / 16) * 2); MapCHR1(1, (Latch / 16) * 2 + 1);
        break;
    case 96:   // nesdev INES Mapper 096: [.... .CPP]; bus conflicts
        Latch = (byte)(Input & SnoopCPU(Address));
        MapPRG32(Latch % 4); OekaOuter = Latch & 4;
        MapCHR4(0, OekaOuter + OekaInner); MapCHR4(1, OekaOuter + 3);
        break;
    case 72: case 92: {  // nesdev INES Mapper 072/092: load on a 0 -> 1 of bit 7 (PRG) / bit 6 (CHR)
        byte v = (byte)(Input & SnoopCPU(Address));
        if ((v & 128) && !(Latch & 128)) MapPRG16(Cart.Mapper == 92 ? 1 : 0, v % 16);
        if ((v & 64) && !(Latch & 64)) MapCHR8(v % 16);
        Latch = v;
        break;
    }
    case 70:   // nesdev INES Mapper 070: [PPPP CCCC], bus conflicts
        Latch = (byte)(Input & SnoopCPU(Address));
        MapPRG16(0, Latch / 16); MapCHR8(Latch % 16);
        break;
    case 152:  // nesdev INES Mapper 152: [MPPP CCCC], M = one-screen A/B
        Latch = (byte)(Input & SnoopCPU(Address));
        MapPRG16(0, (Latch / 16) % 8); MapCHR8(Latch % 16);
        Mirroring = Latch >= 128 ? TRIC_MIRROR_SCREEN_B : TRIC_MIRROR_SCREEN_A;
        break;
    case 232:
        if (Address >= 0xc000) PrgReg = Input % 4;
        else Ctrl = Cart.Info.submapper == 1 ? ((Input / 8) % 2) * 2 + (Input / 16) % 2 : (Input / 8) % 4;
        MapPRG16(0, Ctrl * 4 + PrgReg);
        MapPRG16(1, Ctrl * 4 + 3);
        break;
    case 1: case 155: MMC1Write(Address, Input); break;
    case 2: Latch = Input; MapPRG16(0, Input); break;
    case 3: Latch = Input; MapCHR8(Input); break;
    case 4: case 118: case 119: MMC3Write(Address, Input); break;
    case 68: Sunsoft4Store(Address, Input); break;
    case 228: Action52Store(Address, Input); break;
    case 185: Latch = (byte)(Input & SnoopCPU(Address)); break;
    case 32:
        switch (Address >> 12) {
        case 8: Reg[0] = Input; break;
        case 9: Ctrl = Input; break;
        case 10: Reg[1] = Input; break;
        case 11: MapCHR1(Address % 8, Input); break;
        }
        IremG101Apply();
        break;
    case 7:
        Latch = Input;
        MapPRG32(Input & 7);
        Mirroring = (Input & 0x10) ? TRIC_MIRROR_SCREEN_B : TRIC_MIRROR_SCREEN_A;
        break;
    case 66:
        Latch = Input;
        MapPRG32((Input >> 4) & 3);
        MapCHR8(Input & 3);
        break;
    default: break;
    }
}

inline int TricMapper::FetchPatternAddress(ushort Address) {
    if (Cart.Mapper==5) return (int)MMC5CHR(Address);
    uint index = CHR_Off[(Address >> 10) & 7] | (Address & 0x3FF);
    return Cart.UsingCHRRAM ? index % Cart.CHRROM_Length : index;
}

inline void TricMapper::PatternReadLatch(ushort Address) {
    if (Cart.Mapper != 9 && Cart.Mapper != 10) return;
    if (!PatternPending) { PatternPending = true; PatternAddress = Address; }
}

inline void TricMapper::PatternReadEnd(bool Reading) {
    if (Cart.Mapper==5) { if (!Reading) M5.rd=0; return; }
    if (Reading || !PatternPending) return;
    PatternPending = false;
    ushort Address = PatternAddress;
    unsigned half = Address >> 12;
    unsigned offset = Address & 4095;
    bool range = Cart.Mapper == 10 || half == 1;
    if (offset >= 0xfd8 && offset <= (range ? 0xfdf : 0xfd8)) {
        if (half) Chr1 = 0; else Chr0 = 0;
    } else if (offset >= 0xfe8 && offset <= (range ? 0xfef : 0xfe8)) {
        if (half) Chr1 = 1; else Chr0 = 1;
    } else return;
    MapCHR4(half, Reg[half * 2 + (half ? Chr1 : Chr0)]);
}

inline byte TricMapper::SnoopPPU(ushort Address) {
    if (Cart.Mapper==5 && Address>=8192) return MMC5NT(Address,false);
    if (Address < 0x2000 && Cart.Mapper == 185 && !Cnrom185Chr()) return (byte)(Address | 1);
    if (Address < 0x2000 && ChrRamSelected(Address)) return Cart.CHRRAM[ChrLow[Address >> 10] * 1024 + (Address & 1023)];
    if (Address < 0x2000) return Cart.CHRROM[FetchPatternAddress(Address)];
    if (VRC6Board()) {
        unsigned bank=VRC6Nametable((Address>>10)&3);
        if (Ctrl&16) return Cart.CHRROM[(bank*1024+(Address&1023))%(Cart.UsingCHRRAM ? Cart.CHRROM_Length : Cart.CHRPages*1024)];
        return VRAM[(bank%2)*1024+(Address&1023)];
    }
    if (Cart.Mapper == 68 && (Ctrl & 16)) return Cart.CHRROM[Sunsoft4Page(Address)];
    if (Cart.Mapper == 19 && Address < 0x2000 && NamcoCiramChr[Address >> 10])
        return VRAM[(NamcoCiramChr[Address >> 10] - 1) * 1024 + Address % 1024];
    if (Cart.Mapper == 19 && Address >= 0x2000) {
        int rom = NamcoNtRom(Address);
        return rom >= 0 ? Cart.CHRROM[rom] : VRAM[(NamcoNt[(Address >> 10) % 4] % 2) * 1024 + Address % 1024];
    }
    if (Cart.AlternativeNametableArrangement) return VRAM[Address & 0xfff];
    if (Cart.Mapper == 118 || Cart.Mapper == 158) return VRAM[(ChrA17[(Address >> 10) & 7] ? 0x400 : 0) | (Address & 0x3ff)];
    if (Cart.Mapper == 95) return VRAM[((Reg[(Address >> 11) & 1] & 32) ? 0x400 : 0) | (Address & 0x3ff)];   // NAMCOT-3425: CHR A15
    if (Cart.Mapper == 207) return VRAM[((Reg[(Address >> 11) & 1] & 0x80) ? 0x400 : 0) | (Address & 0x3ff)];
    ushort Addr = (ushort)(Address & 0x3FF);
    if (Mirroring == TRIC_MIRROR_HORIZONTAL) Addr |= (ushort)((Address & 0x800) ? 0x400 : 0);
    else if (Mirroring == TRIC_MIRROR_VERTICAL) Addr |= (ushort)((Address & 0x400) ? 0x400 : 0);
    else if (Mirroring == TRIC_MIRROR_SCREEN_B) Addr |= 0x400;
    return VRAM[Addr];
}

inline void TricMapper::AccessPPU() {
    if (Cart.Mapper==5) { MMC5PPU(); return; }
    ushort Address = PPU_AddressIn;
    if (VRC6Board() && SeventyTwoPinConnector[64] && (Ctrl & 16)) {
        unsigned index=(VRC6Nametable((Address>>10)&3)*1024+(Address&1023))%(Cart.UsingCHRRAM ? Cart.CHRROM_Length : Cart.CHRPages*1024);
        PPU_DataIn=PPU_NonTiltData;
        if (!SeventyTwoPinConnector[20]) PPU_NonTiltData=PPU_DataOut=Cart.CHRROM[index];
        if (!SeventyTwoPinConnector[55] && Cart.UsingCHRRAM) Cart.CHRROM[index]=PPU_DataIn;
    }
    if (Cart.Mapper == 19 && SeventyTwoPinConnector[64] && NamcoNtRom(Address) >= 0) {
        PPU_DataIn = PPU_NonTiltData;   // CHR ROM nametable: writes are dropped
        if (!SeventyTwoPinConnector[20]) PPU_NonTiltData = PPU_DataOut = Cart.CHRROM[NamcoNtRom(Address)];
        return;
    }
    if (Cart.Mapper == 19 && !SeventyTwoPinConnector[64] && NamcoCiramChr[(Address >> 10) & 7]) return;   // CIRAM answers
    if (Cart.Mapper == 68 && SeventyTwoPinConnector[64] && (Ctrl & 16)) {
        PPU_DataIn = PPU_NonTiltData;   // ROM: writes are dropped
        if (!SeventyTwoPinConnector[20]) PPU_NonTiltData = PPU_DataOut = Cart.CHRROM[Sunsoft4Page(Address)];
        return;
    }
    // Mapper 185 with the protection engaged leaves the CHR ROM undriven:
    // the address byte stays on the pins, D0 pulled up (nesdev INES Mapper 185).
    if (!SeventyTwoPinConnector[64] && Cart.Mapper == 185 && !Cnrom185Chr()) {
        if (!SeventyTwoPinConnector[20]) PPU_NonTiltData = PPU_DataOut = (byte)(Address | 1);
        return;
    }
    // Mapper 93 with CHR RAM disabled: writes are ignored, reads see the address byte (open bus).
    if (!SeventyTwoPinConnector[64] && Cart.Mapper == 93 && Chr93Off) {
        if (!SeventyTwoPinConnector[20]) PPU_NonTiltData = PPU_DataOut = (byte)Address;
        return;
    }
    if (!SeventyTwoPinConnector[64] && ChrRamSelected(Address)) {
        byte &cell = Cart.CHRRAM[ChrLow[(Address >> 10) & 7] * 1024 + (Address & 1023)];
        PPU_DataIn = PPU_NonTiltData;
        if (!SeventyTwoPinConnector[20]) PPU_NonTiltData = PPU_DataOut = cell;
        if (!SeventyTwoPinConnector[55]) cell = PPU_DataIn;
        return;
    }
    if (!SeventyTwoPinConnector[64]) {
        int CHR_Address = FetchPatternAddress(Address);
        PPU_DataIn = PPU_NonTiltData;
        if (!SeventyTwoPinConnector[20]) {
            PPU_DataOut = Cart.CHRROM[CHR_Address];
            PPU_NonTiltData = PPU_DataOut;
            PatternReadLatch(Address);
        }
        if (!SeventyTwoPinConnector[55] && Cart.UsingCHRRAM) {
            Cart.CHRROM[CHR_Address] = PPU_DataIn;
        }
    }
}

inline void TricMapper::Connector_CheckCIRAM() {
    if (Cart.Mapper==5) { SeventyTwoPinConnector[56]=true; return; }
    SeventyTwoPinConnector[56] = (PPU_AddressBus & 0x2000) == 0;
    if (VRC6Board()) {
        if (Ctrl & 16) SeventyTwoPinConnector[56]=true;
        SeventyTwoPinConnector[21]=(VRC6Nametable((PPU_AddressBus>>10)&3)&1)!=0;
        return;
    }
    if (Cart.Mapper == 118 || Cart.Mapper == 158) { SeventyTwoPinConnector[21] = ChrA17[(PPU_AddressBus >> 10) & 7]; return; }
    if (Cart.Mapper == 95) { SeventyTwoPinConnector[21] = (Reg[(PPU_AddressBus >> 11) & 1] & 32) != 0; return; }
    // Mapper 207: the upper nametable pair follows $7EF0 bit 7, the lower $7EF1 bit 7.
    if (Cart.Mapper == 207) { SeventyTwoPinConnector[21] = (Reg[(PPU_AddressBus >> 11) & 1] & 0x80) != 0; return; }
    if (Cart.Mapper == 19) {
        // The 163 enables CIRAM for CIRAM-select nametables and for pattern
        // pages it maps to CIRAM, and drives A10 from the select's bit 0.
        if (PPU_AddressBus & 0x2000) {
            byte sel = NamcoNt[(PPU_AddressBus >> 10) % 4];
            SeventyTwoPinConnector[56] = sel < 0xe0;
            SeventyTwoPinConnector[21] = sel % 2;
        } else {
            byte page = NamcoCiramChr[(PPU_AddressBus >> 10) % 8];
            SeventyTwoPinConnector[56] = page == 0;
            SeventyTwoPinConnector[21] = page == 2;
        }
        return;
    }
    if (Cart.Mapper == 68 && (Ctrl & 16)) SeventyTwoPinConnector[56] = true;   // CIRAM /CE high
    bool a10;
    if (Mirroring == TRIC_MIRROR_HORIZONTAL)    a10 = (PPU_AddressBus & 0x800) != 0;
    else if (Mirroring == TRIC_MIRROR_VERTICAL) a10 = (PPU_AddressBus & 0x400) != 0;
    else                                        a10 = Mirroring == TRIC_MIRROR_SCREEN_B;
    SeventyTwoPinConnector[21] = a10;
}
