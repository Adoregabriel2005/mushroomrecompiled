# Cycle runtime mapper coverage

This applies to `cycle_accurate=true`, `cyc_interp`, and `cyc_oracle`.
The legacy runner has a separate cartridge implementation.

[MAPPER_CATALOG.md](MAPPER_CATALOG.md) lists every known mapper ID with support status, dump
counts, real-title validation and a priority order for the remaining boards.

Start with the [NESdev mapper index](https://www.nesdev.org/wiki/List_of_mappers),
then the board's register description and
[NES 2.0 submapper assignments](https://www.nesdev.org/wiki/NES_2.0_submappers).
The submapper notes matter: a mapper ID can describe electrically different boards.
Register behavior can be cross-checked against the
[Mesen cartridge implementations](https://github.com/SourMesen/Mesen2/tree/master/Core/NES/Mappers).
Those are a cross-check, not a replacement for hardware documentation.

The original IDs are 0, 1, 2, 3, 4, 7, and 66. The additions below have explicit
bank/decode contracts in `mapper_test.c` and generated ROM execution checks in
`tools/cyc/mapper_fixtures.py` and `mapper_ppu_fixtures.py`.
Oracle parity checks integration with two CPU/PPU
models; it does not independently establish the mapper specification.

| ID | Board / reference | Behavior and limits |
|---:|---|---|
| 20 | [FDS RAM Adapter](https://www.nesdev.org/wiki/Family_Computer_Disk_System) | Loaded from a BIOS and a disk image (`cyc_load_fds`), never from an iNES header. 32 KiB PRG RAM at $6000-$DFFF, the 8 KiB BIOS fixed at $E000-$FFFF, 8 KiB CHR RAM, CIRAM A10 from $4025.3. Timer IRQ ($4020-$4022), I/O enables ($4023), the disk drive ($4024-$4026, $4030-$4033) clocked every CPU cycle, and the sound unit (`hw_fds_audio.c`: wavetable, envelopes, modulator, mixed at Mesen's level). Code the BIOS loads into PRG RAM runs as compiled RAM views. See [the FDS section](#fds-ram-adapter-mapper-20). |
| 118 | [TxSROM](https://www.nesdev.org/wiki/TxSROM) | MMC3 banking and A12 IRQ. CHR A17 drives CIRAM A10: bit 7 of the register covering the nametable address's pattern page (R0/R1 or R2-R5 by $8000 bit 7; $3000-$3EFF uses pages 4-7). $A000 is disconnected. iNES keeps the MMC3 8 KiB WRAM default; four-screen headers are rejected. |
| 119 | [TQROM](https://www.nesdev.org/wiki/TQROM) | MMC3 banking and A12 IRQ. CHR bank bit 6 (CHR A16) selects the 8 KiB CHR RAM chip, addressed by bank bits 0-2; otherwise CHR ROM sees bits 0-5 (at most 64 KiB). No work RAM. iNES implies the 8 KiB CHR RAM; NES 2.0 must declare exactly 8 KiB volatile CHR RAM. This is the only board accepted with both CHR ROM and CHR RAM. |
| 69 | [Sunsoft FME-7 / 5A / 5B](https://www.nesdev.org/wiki/Sunsoft_FME-7) | Command/parameter registers: eight 1 KiB CHR banks, three 8 KiB PRG banks (6 bits) plus the fixed last bank, four mirroring modes, and a $6000 window that holds a PRG ROM bank, enabled RAM, or open bus. The 16-bit IRQ counter decrements every CPU cycle (including DMA) and interrupts on the $0000 to $FFFF wrap; only command 13 acknowledges. [5B audio](https://www.nesdev.org/wiki/Sunsoft_5B_audio) is always present: three tones, 17-bit noise, the 32-step envelope with all 16 shapes, and the 1.5 dB/step DAC. Mixing gain is nominal. Code at $6000 uses the interpreter. iNES implies 8 KiB PRG RAM. |
| 67 | [Sunsoft-3](https://www.nesdev.org/wiki/INES_Mapper_067) | Registers decode A15-A11: $8800/$9800/$A800/$B800 2 KiB CHR, $F800 16 KiB PRG at $8000 (last 16 KiB fixed), $E800 bits 0-1 mirroring (V, H, one-screen A, B). IRQ: $C800 writes the counter itself, high byte then low; $D800 bit 4 runs it and resets the byte toggle; it decrements every CPU cycle, and wrapping $0000 -> $FFFF stops it and asserts; only a $8000 write acknowledges (Mesen2 also acknowledges on $D800). Fantasy Zone II matches Mesen2 through its opening play except a palette-dim event Mesen2 reaches around frame 1190 under the scripted input and this runtime does not; not attributed to the mapper, left to the owner playtest. |
| 68 | [Sunsoft-4](https://www.nesdev.org/wiki/INES_Mapper_068) | Four 2 KiB CHR banks; $E000 bit 4 replaces CIRAM with two 1 KiB CHR ROM pages ($C000/$D000, D7 forced: the last 128 KiB), chosen per quadrant by the mirroring mode, and nametable writes are dropped. 16 KiB PRG switch plus the fixed last bank; $F000 bit 4 enables up to 8 KiB WRAM. CHR RAM boards and submapper 1 (Nantettatte!! Baseball's licensing timer and option ROM) are rejected. |
| 41 | [Caltron 6-in-1](https://www.nesdev.org/wiki/INES_Mapper_041) | Outer register latches address bits at $6000-$67FF (data ignored): 32 KiB PRG bank, outer 32 KiB CHR bank, mirroring. Its A2 (PRG banks 4-7) enables the inner 8 KiB CHR select at $8000-$FFFF, modeled with AND bus conflicts (the wiki lists conflicts as "partly"; only this register decodes ROM space). Both clear at power-on. $6000 writes end compiled blocks. |
| 228 | [Active Enterprises](https://www.nesdev.org/wiki/INES_Mapper_228) | A write to $8000-$FFFF latches its address (1.MHHPPP PPS.CCCC) and D0-D1: mirroring, 512 KiB chip, 16 KiB page, 16/32 KiB mode, 8 KiB CHR bank. The 1.5 MiB Action 52 stores chips 0, 1 and 3; chip 2 reads open bus and runs on the interpreter. Power-on state is the $00-to-$8000 write the games expect. The documented $4020-$5FFF nibble RAM is absent on both cartridges and not modeled; no bus conflicts are documented. |
| 19 | [Namco 163](https://www.nesdev.org/wiki/Namco_163) | Eight 1 KiB CHR banks ($E0-$FF select CIRAM as CHR RAM unless $E800 bit 6/7 disables it per half), CHR ROM or CIRAM nametables, three 8 KiB PRG banks plus the fixed last bank, a 15-bit CPU-cycle IRQ up-counter that stops at $7FFF, 8 KiB work RAM whose writes need $F800 = $4x with a clear window bit, and 128 bytes of internal RAM (battery-saved with the work RAM) driving the [N163 sound generator](https://www.nesdev.org/wiki/Namco_163_audio). Submappers 0-5 accepted; 2 is silent, 3-5 set the mixing level. |
| 210 | [Namco 175 / 340](https://www.nesdev.org/wiki/INES_Mapper_210) | The 163's CHR/PRG banking without CIRAM CHR, nametable, IRQ or sound. 175 (submapper 1): $C000 bit 0 enables up to 8 KiB RAM, mirroring hardwired. 340 (submapper 2): $E000 bits 6-7 select one-screen A, vertical, one-screen B, horizontal; no RAM. Submapper 0 is 175 with a battery, else 340. Known 175/340 dumps with mapper-19 headers are identified by PRG+CHR CRC-32 (`common/nes_cart.h`). |
| 185 | [CNROM + copy protection](https://www.nesdev.org/wiki/INES_Mapper_185) | Fixed 16/32 KiB PRG and one 8 KiB CHR ROM whose chip selects (D0-D1, AND bus conflicts) enable it only for the submapper's value (4-7 = 0-3). Disabled pattern reads leave the address byte with D0 pulled up (the early Mighty Bomb Jack board's resistor; others vary). Submapper 0 uses nesdev's heuristic: no CHR for the first two $2007 reads after power-on. Known iNES dumps get their submapper from the CRC table in `common/nes_cart.h`. |
| 18 | [Jaleco SS88006](https://www.nesdev.org/wiki/INES_Mapper_018) | Nibble-written registers: three 8 KiB PRG banks (6 bits) plus the fixed last bank, eight 1 KiB CHR banks, four mirroring modes, and $9002 work RAM enable (bit 0) with write permission (bit 1). The IRQ counter counts down every CPU cycle over 4, 8, 12 or 16 low bits (upper bits kept), interrupts when they wrap from 0 and keeps counting; $F000 (reload) and $F001 acknowledge. The $F003 uPD7755/7756 speech chip is not modeled (its samples are internal to the chip). Mesen2 fires one count early and lacks $9002; this follows nesdev. |
| 33 | [Taito TC0190](https://www.nesdev.org/wiki/INES_Mapper_033) | Two 8 KiB PRG banks (6 bits) plus the fixed last 16 KiB, two 2 KiB CHR banks (numbered in 2 KiB units) and four 1 KiB banks; mirroring in $8000 bit 6; A14 is not decoded. |
| 48 | [Taito TC0690](https://www.nesdev.org/wiki/INES_Mapper_048) | TC0190 banking plus an MMC3-style A12 scanline counter: $C000 latch written inverted, $C001 reload, $C002 enable, $C003 disable and acknowledge; the IRQ asserts 22 CPU cycles after the MMC3 would. nesdev says "about 4", which makes the Flintstones status-bar split flicker; 20-24 render it cleanly (Mesen2 also uses 22). Mirroring in $E000 bit 6. Known TC0690 dumps with mapper-33 headers are identified by CRC. |
| 32 | [Irem G-101](https://www.nesdev.org/wiki/INES_Mapper_032) | Two 8 KiB PRG banks (5 bits); $9000 bit 1 swaps the $8000 bank with the second-to-last bank at $C000, bit 0 selects mirroring; eight 1 KiB CHR banks at $B000-$B007. Submapper 1 (Major League) ties CIRAM A10 high and fixes the PRG mode (Mesen2 uses one-screen A). |
| 80 | [Taito X1-005](https://www.nesdev.org/wiki/INES_Mapper_080) | Registers at $7EF0-$7EFF: two 2 KiB CHR banks (bit 0 ignored), four 1 KiB banks, $7EF6 bit 0 mirroring (not mirrored at $7EF7), three 8 KiB PRG banks (pairs $7EFA/B, $7EFC/D, $7EFE/F) plus the fixed last bank, and 128 bytes of chip RAM at $7F00 (mirrored at $7F80) unlocked by writing $A3 to $7EF8/$7EF9. $7EF0-$7EFF writes end compiled blocks. Mesen2 keeps bit 0 of the 2 KiB banks and mirrors $7EF6 at $7EF7. |
| 207 | [Taito X1-005 (alt. mirroring)](https://www.nesdev.org/wiki/INES_Mapper_207) | Mapper 80 with $7EF6 ignored: bit 7 of $7EF0 selects CIRAM A10 for the upper nametables and bit 7 of $7EF1 for the lower ones. Known Fudou Myouou Den dumps with mapper-80 headers are identified by CRC (their status bar needs this mirroring). |
| 82 | [Taito X1-017](https://www.nesdev.org/wiki/INES_Mapper_082) | X1-005-style CHR registers with $7EF6 bit 1 swapping the 2 KiB and 1 KiB halves; 5 KiB of chip RAM unlocked in 2/2/1 KiB windows by $7EF7 = $CA, $7EF8 = $69, $7EF9 = $84; PRG at $7EFA-$7EFC from bits 2-5. The IRQ at $7EFD-$7EFF is not modeled: its clock and trigger are undocumented and no commercial game uses it. |
| 552 | [Taito X1-017 (NES 2.0)](https://www.nesdev.org/wiki/INES_Mapper_082) | Mapper 82 with PRG bits 0-5 wired to A18-A13 in reverse order, as the chip actually connects them. |
| 40 | [NTDEC 2722](https://www.nesdev.org/wiki/INES_Mapper_040) | Fixed PRG banks at $6000/$8000/$A000/$E000, switchable 8 KiB at $C000, 4096-M2 IRQ. Code at $6000 uses the interpreter. |
| 155 | [MMC1A](https://www.nesdev.org/wiki/MMC1) | RAM stays enabled by the PRG register; bit 4 instead bypasses fixed-bank A17 selection. Uses the same SxROM board wiring as mapper 1. |
| 85 | [VRC7](https://www.nesdev.org/wiki/VRC7) | Three 8 KiB PRG windows, eight CHR windows, WRAM gate, VRC IRQ, and six FM channels. Submapper 1 selects A3 and omits the oscillator; submapper 2 selects A4. |
| 24 | [VRC6a](https://www.nesdev.org/wiki/VRC6) | 16+8 KiB PRG, all CHR/nametable modes, WRAM gate, CPU IRQ and two pulse/one saw audio channels. |
| 26 | [VRC6b](https://www.nesdev.org/wiki/VRC6) | VRC6 with swapped A0/A1 register wiring, including the audio ports. |
| 21 | VRC4a/c | Submappers 1/2 select address wiring; PRG swap, 9-bit CHR, WRAM gate and CPU/divider IRQ. |
| 22 | VRC2a | Swapped address lines, shifted 8-bit CHR, one-bit latch, no IRQ. |
| 23 | VRC2b / VRC4e/f | Submappers 1/2 select VRC4 wiring; submapper 3 selects VRC2. |
| 25 | VRC2c / VRC4b/d | Submappers 1/2 select VRC4 wiring; submapper 3 selects VRC2. |
| 73 | VRC3 | 16 KiB PRG switch and 8/16-bit CPU IRQ counter. |
| 31 | [NSF cartridge](https://www.nesdev.org/wiki/INES_Mapper_031) | Eight independent 4 KiB PRG windows, $5000-$5FFF register aliases, $F000 power-on bank $FF, fixed CHR and H/V wiring. |
| 9 | [MMC2](https://www.nesdev.org/wiki/MMC2) | Switchable 8 KiB PRG plus fixed last 24 KiB; two pairs of 4 KiB CHR banks. Read latches commit when /RD is released. |
| 10 | [MMC4](https://www.nesdev.org/wiki/MMC4) | Switchable 16 KiB PRG plus fixed last 16 KiB, 8 KiB WRAM, and MMC2-style latches with eight-address trigger ranges on both CHR halves. |
| 232 | [Quattro](https://www.nesdev.org/wiki/INES_Mapper_071) | Outer 64 KiB plus inner 16 KiB banking; NES 2.0 submapper 1 swaps the outer bits for Aladdin. |
| 184 | [Sunsoft-1](https://www.nesdev.org/wiki/Sunsoft_1) | Fixed PRG; two 4 KiB CHR windows; upper window forces bank bit 2. |
| 180 | [UxROM variant](https://www.nesdev.org/wiki/UxROM) | First 16 KiB fixed, upper 16 KiB switchable; AND bus conflicts. |
| 65 | [Irem H3001](https://www.nesdev.org/wiki/INES_Mapper_065) | $8000/$A000 8 KiB PRG banks, $9000 bit 7 moves the $8000 bank to $C000 and bank $3E to $8000 (else $C000 = $3E), $E000 = $3F; power-on banks $00/$01; $B000-$B007 1 KiB CHR; $9001 bits 7-6 00 vertical, 10 horizontal, x1 one-screen A. IRQ: 16-bit down counter, one per CPU cycle while enabled ($9003 bit 7), loaded from $9005/$9006 by a $9004 write, asserts on reaching 0 and stops there; $9003 and $9004 writes acknowledge. Mesen2 follows the older document nesdev calls wrong: a $C000 bank register, mirroring from bit 7 only, and IRQs disabling themselves at 0. Daiku no Gen-san 2's title sequence renders the same in both. |
| 64 | [Tengen RAMBO-1](https://www.nesdev.org/wiki/RAMBO-1) | MMC3-like: $8000 [CPK. RRRR] selects one of 16 registers (R6/R7/RF 8 KiB PRG, P swaps $8000/$C000; R0/R1 2 KiB or, with K, R0/R8/R1/R9 1 KiB; R2-R5 1 KiB; C inverts A12; in 2 KiB mode PPU A10 replaces the register's low bit), last 8 KiB fixed, $A000 bit 0 mirroring. IRQ counts filtered A12 rises or, with $C001 bit 0, every 4 CPU cycles; a $C001 write requests a reload (latch ORed with 1 if non-zero) and restarts the prescaler; /IRQ follows 2 CPU cycles after the A12 rise or 1 after the counting clock. No owner title (the one mapper-64 dump, Dig Dug II (J), is NROM with a stray header byte). Mesen2 reloads with +1 (Hard Drivin'), keeps R0's low bit in 2 KiB mode and adds a clock when leaving cycle mode (Skull & Crossbones); this follows nesdev. |
| 158 | [Tengen 800037](https://www.nesdev.org/wiki/INES_Mapper_158) | RAMBO-1 with CHR A17 wired to CIRAM A10 (like TxSROM): each nametable follows bit 7 of the bank its matching lower pattern page selects, including C inversion and R8/R9 in K mode; $A000 has no effect. (Nestopia ignores R8/R9 and K here.) |
| 72 | [Jaleco JF-17](https://www.nesdev.org/wiki/INES_Mapper_072) | One latch at $8000-$FFFF: a 0 -> 1 edge of bit 7 loads the 16 KiB PRG bank at $8000 from the low bits, a 0 -> 1 edge of bit 6 the 8 KiB CHR bank; last 16 KiB fixed; AND bus conflicts. The uPD7756 speech bits are not modeled. |
| 77 | [Irem LROG017](https://www.nesdev.org/wiki/INES_Mapper_077) | [CCCC PPPP] at $8000-$FFFF: 32 KiB PRG and a 2 KiB CHR ROM bank at $0000; CHR RAM is fixed at $0800-$1FFF and the nametables are RAM as well (four-screen, set as a board fact); AND bus conflicts. |
| 78 | [Irem 74HC161 / Jaleco JF-16](https://www.nesdev.org/wiki/INES_Mapper_078) | [CCCC MPPP] at $8000-$FFFF: 16 KiB PRG at $8000, 8 KiB CHR; submapper 3 (Holy Diver) M = horizontal/vertical, otherwise one-screen A/B (Cosmo Carrier); AND bus conflicts. iNES dumps: known ones by CRC; otherwise a set four-screen bit means Holy Diver and is cleared, as nesdev describes. |
| 86 | [Jaleco JF-13](https://www.nesdev.org/wiki/INES_Mapper_086) | $6000-$6FFF [.CPP ..CC]: 32 KiB PRG, 8 KiB CHR with bit 6 as the high CHR bit; no bus conflicts. The $7000 speech port and the accidental register copies at $E000-$FFFF are not modeled. |
| 89 | [Sunsoft-2, Sunsoft-3 board](https://www.nesdev.org/wiki/INES_Mapper_089) | [CPPP MCCC]: 16 KiB PRG at $8000, 8 KiB CHR with bit 7 as its high bit, M one-screen A/B; AND bus conflicts. |
| 92 | [Jaleco JF-19](https://www.nesdev.org/wiki/INES_Mapper_092) | Mapper 72 with the switchable 16 KiB PRG bank at $C000 and bank 0 fixed at $8000. |
| 93 | [Sunsoft-2, Sunsoft-3R board](https://www.nesdev.org/wiki/INES_Mapper_093) | [.PPP ...E]: 16 KiB PRG at $8000; E enables the CHR RAM (disabled: writes ignored, reads open bus, i.e. the address byte); enabled at power-on (Mesen2 also); AND bus conflicts. |
| 96 | [Bandai Oeka Kids](https://www.nesdev.org/wiki/INES_Mapper_096) | [.... .CPP] at $8000-$FFFF: 32 KiB PRG, 16 KiB outer CHR RAM bank (32 KiB CHR RAM, the iNES default for this mapper); the inner 4 KiB bank at $0000 is PPU A9-A8, latched when the PPU address moves into $2xxx from elsewhere, whichever side drives it; $1000 holds inner bank 3; AND bus conflicts. The tablet input device is not emulated. |
| 97 | [Irem TAM-S1](https://www.nesdev.org/wiki/INES_Mapper_097) | $8000-$BFFF [M..P PPPP]: $8000 fixed to the last 16 KiB, P the 16 KiB bank at $C000, M 0 horizontal / 1 vertical; no bus conflicts. Mesen2 decodes all of $8000-$FFFF, four bank bits and four mirroring modes; this follows nesdev. |
| 101 | [Jaleco JF-10 misdump](https://www.nesdev.org/wiki/INES_Mapper_101) | $6000-$7FFF selects the 8 KiB CHR bank with its bits in order (mapper 87 with the bits swapped back); 32 KiB PRG fixed. |
| 70 | [Bandai 74161/7432](https://www.nesdev.org/wiki/INES_Mapper_070) | One latch at $8000-$FFFF: 16 KiB PRG at $8000 in bits 4-7, 8 KiB CHR in bits 0-3, last 16 KiB fixed; soldered mirroring; AND bus conflicts. Mesen2 switches a mapper-70 game to one-screen mirroring once it writes bit 7, to cover mapper-152 boards dumped as 70; here those dumps (Gegege no Kitarou 2) are identified by CRC instead. |
| 144 | [Color Dreams, Death Race board](https://www.nesdev.org/wiki/INES_Mapper_144) | Mapper 11 with a resistor on CPU D0, so the ROM's bit 0 always wins the bus conflict: the latch takes ROM & (written | 1). |
| 146 | [Sachen 3015 / SA-016](https://www.nesdev.org/wiki/INES_Mapper_146) | Functionally mapper 79: the NINA-06 latch at $4100 (decoded by A8, mirrored through $5FFF). |
| 148 | [Sachen SA-008-A / Tengen 800008](https://www.nesdev.org/wiki/INES_Mapper_148) | Mapper 79's [...PCCC] latch moved to $8000-$FFFF, with AND bus conflicts: 32 KiB PRG in bit 3, 8 KiB CHR in bits 0-2. |
| 152 | [Bandai 74161/7432, one-screen](https://www.nesdev.org/wiki/INES_Mapper_152) | Mapper 70 with bit 7 selecting one-screen A/B and three PRG bits (4-6); one-screen A at power-on; AND bus conflicts. |
| 140 | [JF-11/14](https://www.nesdev.org/wiki/INES_Mapper_140) | 32 KiB PRG / 8 KiB CHR; $6000-$7FFF decode; no conflicts. |
| 113 | [HES](https://www.nesdev.org/wiki/INES_Mapper_113) | 32 KiB PRG / 8 KiB CHR, split CHR select bits and H/V control; $4100 decode. |
| 94 | [UxROM variant](https://www.nesdev.org/wiki/UxROM) | PRG bank uses bits 2-4, last 16 KiB fixed; AND bus conflicts. |
| 87 | [J87](https://www.nesdev.org/wiki/INES_Mapper_087) | Fixed PRG; reversed CHR select bits; $6000-$7FFF decode. |
| 79 | [NINA-003/006](https://www.nesdev.org/wiki/NINA-003-006) | 32 KiB PRG / 8 KiB CHR; partial $4100-$5FFF decode. |
| 76 | [Namco 109](https://www.nesdev.org/wiki/INES_Mapper_076) | Two 8 KiB PRG windows; four 2 KiB CHR windows; fixed mirroring. |
| 88 | [Namco 118, NAMCOT-3433](https://www.nesdev.org/wiki/INES_Mapper_088) | Namco 108 banking (as 206) with PPU A12 wired to CHR A16: the left pattern table reads the first 64 KiB, the right the second; soldered mirroring. |
| 95 | [Namco 118, NAMCOT-3425](https://www.nesdev.org/wiki/INES_Mapper_095) | Namco 108 banking with CHR A15 wired to CIRAM A10: bit 5 of R0 selects the upper nametables' CIRAM page, bit 5 of R1 the lower (horizontal or one-screen only). |
| 154 | [Namco 118, NAMCOT-3453](https://www.nesdev.org/wiki/INES_Mapper_154) | Mapper 88 plus one-screen mirroring from bit 6 of any write to $8000-$FFFF (A, then B when set); one-screen A at power-on. Devil Man dumps headered as 88 (with a spurious four-screen bit) are identified by CRC; its gameplay matches Mesen2. |
| 206 | [DxROM](https://www.nesdev.org/wiki/INES_Mapper_206) | Fixed MMC3-style orientation, no IRQ; four-screen memory and Popils WRAM (including old battery-marked iNES); submapper 1 fixes 32 KiB PRG. [Namco 108 hardware diagnostic and evidence limits](NAMCO108_PROBE.md). |
| 75 | [VRC1](https://www.nesdev.org/wiki/VRC1) | Three 8 KiB PRG windows; split CHR high/low bits; H/V control. Vs. System excluded. |
| 71 | [Camerica](https://www.nesdev.org/wiki/INES_Mapper_071) | 16 KiB PRG; NES 2.0 submapper 0 fixes H/V, submapper 1 enables Fire Hawk mirroring at $8000-$9FFF. iNES retains the $9000 heuristic. |
| 34 | [BNROM / NINA-001](https://www.nesdev.org/wiki/INES_Mapper_034) | Submapper 1 selects NINA, 2 selects BNROM; submapper 0/iNES uses CHR size. NINA WRAM writes also reach bank registers. BNROM has AND conflicts. |
| 13 | [CPROM](https://www.nesdev.org/wiki/CPROM) | Fixed PRG; 16 KiB CHR RAM, upper 4 KiB switchable; vertical mirroring; AND conflicts. |
| 11 | [Color Dreams](https://www.nesdev.org/wiki/Color_Dreams) | 32 KiB PRG / 8 KiB CHR; AND bus conflicts. Conflict-free prototypes excluded. |

Where references disagree, observation of real games decides. The TC0690 IRQ
delay was observable: nesdev's "about 4 CPU cycles" put Flintstones' status-bar
split mid-scanline, and a sweep rendered it cleanly and identically only for
20-24 cycles, so 22 (also Mesen2's value) is used. The other differences from
Mesen2 were A/B-tested by switching each to Mesen2's behavior and replaying the
owner titles that use the board for 3000 frames: Namco 340 mirroring order
(Famista '92), N163 write-protect nibble and auto-increment wrap (Megami Tensei
II, Final Lap), SS88006 IRQ one count early (Goal!!) and the X1-005 2 KiB CHR
bit 0 and $7EF7 mirror (Fudou Myouou Den). Every frame's bus trace and memory
hash were identical, so none is exercised by those games; the nesdev register
descriptions are kept, and fixtures pin them.

iNES headers cannot say whether a board has PRG RAM, an EEPROM or a particular
submapper, and a wrong default can stop a game outright: Contra (Japan), a VRC2b
board with only the $6000 latch, crashes at boot with the default 8 KiB WRAM,
and Dragon Ball Z II, an LZ93D50 whose header drops the battery bit, waits
forever for its missing 24C02. `common/nes_known_dumps.inc`, generated by
`tools/cyc/known_dumps.py` from the pinned NES 2.0 header database, gives the
board facts of every known dump of mappers 16, 157 and 159 (dumped as 16), 152 (dumped as 70), 154 (dumped as 88), 78, 21, 23, 25,
185, 207 (dumped as 80), 210 (dumped as 19) and 48 (dumped as 33), keyed by
PRG+CHR CRC-32. It applies to iNES headers only; NES 2.0 headers are trusted.

[NES 2.0](https://www.nesdev.org/wiki/NES_2.0) decoding now preserves the 12-bit
mapper, submapper, extended/exponent ROM lengths, RAM/NVRAM sizes, trainer offset,
timing, and console type. Unknown submappers, unsupported console/timing models,
and mixed CHR ROM/RAM boards other than TQROM are rejected explicitly. NTSC and multi-region
headers run the NTSC machine. File allocations are bounded to 64 MiB per ROM,
128 KiB PRG RAM, and 1 MiB CHR RAM. RAM sizes do not by themselves add banking
registers; implemented MMC1 board wiring is described below.

Four-screen cartridges have four independent nametables, included in memory
hashes and dumps. RAM below a CPU/PPU window size mirrors within that window.
Submapper 5 of MMC1 fixes the 32 KiB PRG map; submappers 1/2 of UxROM, CNROM,
and AxROM select no/AND bus conflicts. Generated programs now check cartridge
metadata as well as PRG bytes: regenerate older cycle output when updating.
Power-on mappings remain deterministic where hardware does not specify a state.
NVRAM bytes survive machine power-on within a loaded cartridge; host save-file
persistence is available with `--save-file FILE`. Files contain raw PRG NVRAM
followed by CHR NVRAM, or the cartridge's serial EEPROM bytes. Saves are opt-in
so unattended regression runs start consistently. Invalid lengths fail before
execution; successful exits flush a temporary file and atomically replace the
save, including after the SDL host returns.

Run the cartridge contracts with `ctest` in a `runner/cyc` build. Run execution
checks with `tools/cyc/test_cyc_runtime.py`; every fixture runs compiled code, the
same binary's interpreter, the standalone interpreter, and the independent oracle
at all four CPU/PPU alignments. Retained logs contain the frame and bus hashes.

Initial 15-mapper expansion validation: 417 cartridge assertions and 41 synthetic programs
(656 executions across the four modes and alignments on each of Windows and
Linux), all with 100% native CPU execution, plus 135 rejection
checks from `test_cyc_mapper_headers.py`. PPU programs assert physical CHR-page
bytes through `$2007`, verify banked CHR RAM, mirroring and overlapping WRAM,
then enable background rendering for frame-hash comparison. These tests establish
specific board contracts. Subsequent commercial smoke checks cover mappers
11/34/71/79, MMC5 and extended MMC1, alongside core titles on mappers 0/1/4.
See [CARTRIDGE_REVIEW.md](CARTRIDGE_REVIEW.md) for the accepted thirteen-title
campaign, its menu-only RPG comparison limits, and outstanding hardware work.
AccuracyCoin and the 3,000-frame SMB3 route also match the pre-expansion traces
exactly at all four alignments. AccuracyCoin retains its existing alignment
scores of 144/144, 143/144, 141/144 and 143/144; this change adds no new failures.

The extended board wiring is described below. Existing projects can opt into
the cycle backend through [PROJECTS.md](PROJECTS.md); legacy renderer and mod
features still require explicit ports.

The metadata/variant implementation adds `cart_header_test.c` and
`tools/cyc/cart_variant_fixtures.py`. Contracts cover Aladdin, fixed Namco PRG,
explicit NINA/BNROM selection, exponent lengths, Camerica variants, Popils RAM,
small RAM mirroring, and four-screen reads/writes through the actual PPU bus.
The full execution harness and header acceptance/rejection harness retain their
per-case logs under the selected output directory.

MMC2/MMC4 latch tests exhaust all 8,192 pattern addresses for each chip, verify the old byte remains
stable throughout the read pulse, and keep peeks/writes from triggering latches.
`latch_fixtures.py` exercises the actual `$2007` state machine and rendered FD/FE
tiles, with native/interpreter/oracle parity at all four alignments. This caught
and corrected an early latch update: `$2007` samples more than once during /RD,
so committing on the first sample returned the new bank too soon. Commercial
MMC2/MMC4 games and a physical cartridge are not part of this validation.

PRG translation and generated views now use 4 KiB banks. Mapper 31 fixtures
replace each of the eight executing windows, use register aliases, read across
independently banked halves of an old 8 KiB window, and load an exponent-encoded
4 KiB ROM. Old `BB:AAAA` seed files retain their physical 8 KiB meaning; new miss
logs use `4k:BB:AAAA`. `test_cyc_seed_units.py` checks log merging and recompiling
from those identities. The PRG table's hardware-state hash layout changes with
its granularity; bus, memory, and picture traces remain comparable.

VRC board contracts follow [VRC2/VRC4](https://www.nesdev.org/wiki/VRC2_and_VRC4),
[VRC IRQ](https://www.nesdev.org/wiki/VRC_IRQ), and
[VRC3](https://www.nesdev.org/wiki/VRC3). The IRQ hook runs once per completed
CPU cycle, including DMA. Tests assert the VRC4 divider's 114/114/113 sequence,
reload/acknowledge behavior, CHR high bits, all PCB address decodes, WRAM gating,
and VRC2's partially driven read bus. CPU programs exercise rendering and IRQs
through OAM DMA. The full five-ID matrix has 33 programs (528 executions on
Windows), all passing at every alignment with 100% native execution. Commercial
VRC games and physical hardware have not been tested.

iNES mapper 23/25 cannot unambiguously distinguish VRC2 from VRC4. Submapper 0
retains the historical VRC4 union of address decodes; use NES 2.0 submapper 3 for
the VRC2 bit latch and 8-bit CHR behavior (including Wai Wai World). VRC4 boards
with explicit 2 KiB WRAM mirror it only through $6000-$6FFF.

VRC6 nametable pin vectors come from BootGod's measurements reported by Quietust
on [Talk:VRC6](https://www.nesdev.org/wiki/Talk:VRC6#Raw_data). The contracts cover
all 64 banking-style values, including CHR-ROM nametables and independent CIRAM
selection. Execution fixtures read these through $2007 and enable rendering.
The audio sequencers run during DMA and when host audio is disabled; PCM mixing
adds their inverted linear DAC before the console output stage (famicom by
default for VRC6, VRC7 and Namco 163, whose boards were only made for the
Famicom; README, APU section). Nominal mixer
gain is approximate; cartridge resistor tolerances and analog response need
hardware comparison. `test_cyc_expansion_audio.py` records native/interpreter
WAVs, checks exact parity and pulse/saw frequencies, and rejects silent/clipped
output. The oracle does not synthesize expansion audio; waveform contracts and
recorded PCM tests provide that validation. Commercial VRC6 games remain to test.

The 5B generator follows the nesdev description of the YM2149F-derived chip:
tone outputs toggle every 16*P CPU cycles, the envelope steps every 16*E, and
the noise LFSR (taps 16 and 13, seeded to 1) shifts every 32*N; a period of 0
acts as 1. A channel sounds when every enabled tone/noise source is high, so
disabling both gives a constant level. Fixed volume v is DAC level 2v+1 and
levels 0-1 are silent. `sunsoft_test.inc` checks every envelope shape for 130
steps, the full 131,071-step noise period, the deselect on a nonzero address
high nibble, and the mixer. `test_cyc_expansion_audio.py --s5b-fixtures`
measures a 220.2 Hz tone and a 249.7 Hz envelope sawtooth within 0.01 Hz and
requires identical native/interpreter WAVs. One full-scale channel is mixed at
the nominal level of an APU pulse; the cartridge amplifier, which is louder,
nonlinear and filtered, is not modeled. Gimmick! is the only 5B-audio game.

The N163 generator spends 15 CPU cycles per channel on channels 8 down to
8-C ($7F bits 4-6) and holds only that channel on the output, so more channels
mean each is heard less often, as on the chip (including the audible
multiplexing whine). Each update adds the 18-bit frequency to the 24-bit phase
modulo the (64-L)*4-sample length and outputs (sample - 8) * volume.
Auto-increment of the $F800 address stops at $7F. $E000 bit 6 freezes the
generator and mutes it (the wiki does not say which; both are modeled). Gain
follows the NES 2.0 submapper: 11-13, 16-17 and 18-19.5 dB above the loudest
APU square for submappers 3-5 (midpoints used), submapper 3's level for 0,
silence for 2. The oracle updates channel phases, which live in CPU-readable
RAM, but does not synthesize the output. `test_cyc_expansion_audio.py
--n163-fixtures` measures a 440.03 Hz tone. Mesen2 wraps the auto-increment,
checks only bit 6 of the write-protect nibble, and swaps the 340's one-screen B
and horizontal modes; this implementation follows the nesdev register pages.

VRC7 uses the pinned MIT-licensed emu2413 core in `vendor/emu2413`, with the
instrument bytes checked against the chip's dumped patch ROM. A rational clock
divider models the independent 3.579545 MHz resonator; power-on phase is
deterministic. Diagnostic test-register behavior, sound reset, ignored writes,
and all six channels have contracts. CPU fixtures cover all three submapper
choices, PRG/CHR/WRAM/mirroring, IRQs through DMA, a custom sine carrier, and the
silent VRC7b/reset cases. The PCM harness measures the 440.601 Hz carrier and
compares native/interpreter WAVs byte for byte. This is an FM model with nominal
gain, not a bit-exact capture of the chip's serial DAC or cartridge analog mixer.
Lagrange Point and Tiny Toon Adventures 2 remain commercial-game validation work.

Bandai mapper 16 supports FCG-1/2 ($6000 registers, direct IRQ counter) and
LZ93D50 ($8000 registers, reload latch). NES 2.0 submappers 4/5 choose those
decodes; ambiguous submapper 0 accepts each in its corresponding window.
Deprecated submappers 1/2/3 are rejected; use mapper 159/157/153 respectively.
LZ93D50's Xicor X24C02 EEPROM uses an A0/A1 device command, MSB-first bytes,
four-byte page wrapping, sequential reads, ACK polling, and a nominal 5 ms
programming interval. Only committed bytes are exported; power interruption
discards an unfinished write. D7 releases SDA for reads and D4 receives the
open-drain result. EEPROM capacity comes from NES 2.0 PRG NVRAM (256 bytes) or
the iNES battery bit. The bytes are not exposed as CPU work RAM.

Sources: [mapper 16](https://www.nesdev.org/wiki/INES_Mapper_016),
[FCG-2 PCB tracing](https://seesaawiki.jp/famicomcartridge/d/Bandai%20FCG-2),
[LZ93D50 PCB tracing](https://seesaawiki.jp/famicomcartridge/d/Bandai%20LZ93D50%20standard),
and the Xicor datasheets attached to the latter page (its two PDF labels are
reversed; inspect the document title). These parts have four-byte pages,
unlike many later 24Cxx devices. The EEPROM primitive is shared with the oracle;
board and IRQ logic are independent. Direct transaction vectors check every
address, sequential wrap, page rollover, NACK, and interrupted programming.
Fourteen 6502 fixtures (224 executions) exercise banks, rendering, IRQs through
DMA, and bit-banged serial traffic at all alignments. Save tests start separate
processes to increment persisted bytes, verify PRG/CHR save layout, and reject
truncated/oversized files without modifying them. Commercial Bandai games and
physical EEPROM timing have not been compared yet.

Mapper 159 uses the same LZ93D50 board with a 128-byte X24C01. Its command is
the seven-bit word address followed by R/W, with no I2C device address. NES 2.0
must declare 128-byte PRG NVRAM; iNES mapper 159 implies that chip. Byte order
is MSB first, as verified by the PCB researcher using sequential reads; older
emulators sometimes reverse both addresses and data. Mapper 159 has its own
bank, IRQ, serial and process-restart fixtures. Low-window FCG writes also
terminate compiled blocks, so switching $6008 cannot execute stale native code.

Mapper 153 (BA-JUMP2/Famicom Jump II) has fixed 8 KiB CHR RAM and 8 KiB
battery RAM, enabled by $800D bit 5. Cold battery RAM starts at $FF because
this game does not tolerate an all-zero uninitialized save. The active PPU
A11:A10 selects which of $8000-$8003 drives PRG A18, including in the otherwise
fixed $C000 window. This follows the [measured PCB wiring](https://seesaawiki.jp/famicomcartridge/d/Bandai%20BA-JUMP2),
rather than ORing four bank registers. With unequal A18 outputs, dispatch uses
the interpreter so every opcode and operand sees changes during PPU clocks or
DMA. Native dispatch resumes when the outputs agree. Its fixtures check both
256 KiB halves from both CPU windows, PPU-controlled outer banks, WRAM gating,
IRQ/DMA and persisted SRAM. Driving RAM and the unconnected SDA input together
resolves D4 low; exact analog contention on that invalid setting is unspecified.

Mapper 157 (Datach) has fixed 8 KiB CHR RAM, its own 256-byte X24C02, and an
optional cartridge 128-byte X24C01. Header NVRAM describes only the latter.
`--datach-save-file FILE` persists the main unit independently of cartridge
`--save-file FILE`, so it can be shared between games. The clock from the active
CHR register's bit 3 selects the external device; $800D bit 5 clocks the internal
one, and both share a resolved open-drain SDA wire. PPU A11:A10 selection is
modeled, so unequal clock registers can generate serial edges during rendering.

The barcode API and headless `--barcode DIGITS --barcode-frame N` provide
EAN-8, UPC-A or EAN-13 light/dark input, including checksum validation. Swipe
speed is selectable with `--barcode-module-cycles N` (default 1000 CPU cycles).
This models a chosen photodiode stimulus; actual swipe speed is user-dependent.
The [Datach PCB tracing](https://seesaawiki.jp/famicomcartridge/d/Bandai%20Datach)
and [mapper 157 register description](https://www.nesdev.org/wiki/INES_Mapper_157)
define the serial and barcode bus wiring. GS1's General Specifications section
5.2 defines the barcode symbols. The scanner contract checks a literal EAN-13
waveform; 48 process runs check its 62 transitions at three speeds and every
alignment. Runtime and oracle share the optical stimulus and EEPROM primitives,
so these expectations are checked directly instead of claiming independent
chip implementations. No commercial Datach game or physical reader was tested.

The enlarged fixture harness compiles its common runtime once as an object
library, explicitly selects Release on single-configuration generators, and
allows `--build-timeout` for slower machines. Generated game code still links
into separate executables and runs all four execution modes.

### FDS RAM Adapter (mapper 20)

`hw_fds.c`. The board is built from `disksys.rom` (the host accepts only the
image `bios/disksys.toml`, or the compiled program, identifies) and a `.fds`
(fwNES or raw) or `.qd` image, whose sides `common/nes_fds.h` rebuilds into
the byte streams the drive clocks. The reference is the FDS of nesref's
"Mesen 0.9.9" libretro core, measured to be libretro/Mesen master (0102910):
0.9.9's `Core/FDS.cpp` with a byte every 150 CPU cycles. The default profile
(`--fds-profile mesen`) transcribes it cycle for cycle; `mesen2` follows
Mesen2 b9fa69d, and `hardware` adds what nesdev describes and Mesen leaves
out ($4030.3 and .6, a CRC check in $4030.4, the write-protect tab, the
battery bit). `hw_fds.c` has the table of differences with line references;
the `hardware` column is a judgment call, not oracle-verified.

- Timer: the reload value is copied into the counter when $4022.1 enables it;
  the IRQ fires the cycle after the counter reaches 0, every reload + 1 cycles
  with $4022.0 set, once otherwise. $4030 reads, $4022 writes that disable it
  and $4023.0 = 0 acknowledge; with the disk registers off $4022 cannot enable
  it, while $4020/$4021 still take writes.
- Drive: no disk or the motor off parks the head at the end; the motor turning
  on (with $4025.1 released) rewinds to byte 0, waits 50,000 cycles, then clocks
  one byte every 150 cycles. $4025.6 set, the first nonzero byte ends the gap
  and transfers without an IRQ; each later byte raises the transfer flag and,
  with $4025.7, the disk IRQ, acknowledged by a $4031 read, a $4024 or $4025
  write, or a $4030 read (which also acknowledges the timer). At the end of the
  side the drive stops its own motor.
- $4032: disk absent (bit 0), not ready = not scanning (bit 1), not writable =
  absent or protected (bit 2), bits 3-7 open bus. $4033 returns $4026 as
  written (Mesen: "always return good battery").
- Writes: write mode clocks $4024 (or the CRC) onto the disk under the head,
  in every profile. Mesen 0.9.9 (nesref's core) stores them two bytes behind
  it; Mesen2 moved them under the head in 352adae9f. Nazo no Murasame-jou's
  name save shows why: the BIOS enters write mode on the byte after the
  previous block's second CRC byte, so two-behind overwrites that CRC and a
  CRC-checking drive then reports DISK TROUBLE ERR.27 on the next boot.
  `--fds-write-at mesen` keeps Mesen's position for in-session comparisons.
  The file Mesen saves (its `.ips`) is the same either way. The host keeps
  what the game writes in a disk save file beside the image, which is never
  written (README, [Disk saves and sides](README.md#disk-saves-and-sides)).
- CRC: the side streams carry the real CRC-16 by default; `--fds-crc mesen`
  carries Mesen's constant $4D $62 instead. Mesen never reports a bad CRC; the
  check (`--fds-crc-check`, or the `hardware` profile) raises $4030.4 when the
  CRC register is not 0 as the CRC control bit rises. The BIOS boots and
  RAM/picture agree with nesref either way, since it discards the CRC bytes.
- Timing against the CPU: Mesen clocks the board before a cycle's bus access
  and samples /IRQ after it; `hw_machine.c` clocks it at tick 11, after the
  tick-7 IRQ sample and before the next access, the same order. In the SMB2J
  boot, byte clocks keep the same offset from Mesen's cycle counter as the
  frame points do (`tools/cyc/fds_oracle_gates.py`; see README).
- Sound ($4040-$4092): `hw_fds_audio.c`, libretro/Mesen 0102910's
  `FdsAudio.h` per CPU cycle (Mesen2's in the `mesen2` and `hardware`
  profiles), mixed at Mesen's level (20/5000 per step, 1.69 full pulses at
  full volume); `hardware` uses the nesdev level (2.4 pulses) and ~2 kHz
  low-pass, the RAM Adapter's own filter on the FDS sound. The mix then goes
  through the `famicom` console output stage by default (README, APU
  section). See README, "The sound unit".
- Code: the BIOS compiles as a fixed ROM. Code the BIOS loads into PRG RAM
  compiles as RAM views: every PRG file on every side at its load address,
  plus what the capture file adds, validated by content and a write watch
  (README, "Code in RAM"). The owner routes run 100% native; anything no view
  covers runs on the interpreter and goes to the capture log.

Every register access, IRQ edge and acknowledge, clocked byte, motor, rewind,
ready and end-of-side transition, CRC check and side change goes into the
always-on event ring (`cyc_ring.h`; `--ring-out`, `NESRECOMP_CYC_RING_DUMP`).
`cyc_fds_board_test` (CTest) checks the registers, IRQs and drive directly and
runs a synthetic BIOS (`tools/cyc/fds_board_fixtures.py`) on the machine;
`tools/cyc/test_cyc_fds_runtime.py` compiles that BIOS and checks native,
`--interp-only` and `cyc_interp` parity at all four alignments. Owner-image
checks (local, not CI): `tools/cyc/fds_oracle_gates.py` against nesref and
`tools/cyc/test_fds_owner_parity.py`. The TriCNES oracle has no RAM Adapter.

The opt-in HLE tier (`hw_fds_hle.c`, plan in `common/nes_fds_hle.h`: auto
side swap from the disk ID the program asks the BIOS for, and fast load as
unpaced host frames) sits on top and changes nothing with its axes off; see
README, "The HLE tier".

### MMC5 / ExROM (mapper 5)

All four PRG modes support ROM and protected RAM in $6000-$DFFF. Tagged bank
tables keep writable and open windows out of native ROM dispatch and ROM miss
logs. Code in cartridge RAM uses the interpreter and may modify itself; returning
to ROM resumes native execution. Writes to $5000+ leave compiled blocks before
the next opcode. Folded ROM reads retain PCM and NMI-vector side effects.

NES 2.0 metadata selects physical RAM geometry: no RAM, EKROM 8 KiB, ETROM's
two 8 KiB chips, EWROM 32 KiB, two 32 KiB chips, or a single 128 KiB chip.
Unpopulated chip selects read open bus. For mixed volatile/nonvolatile chips,
chip 0 is the battery-backed chip, including 8+32 KiB configurations. Legacy
iNES headers with no RAM count use a 64 KiB compatibility allocation because
they cannot identify the board; specify NES 2.0 for exact holes and persistence.
Battery-backed MMC5 saves append its internal 1 KiB ExRAM after PRG/CHR NVRAM;
ExRAM survives machine power-on and is not counted in header RAM sizes.
Mapper-specific four-screen and unknown submapper headers are rejected.

CHR supports all four sizes, write-time 10-bit bank packing, upper-bank latches,
8x8/8x16 bank-set selection and $2007 access. Nametables support both CIRAM
pages, ExRAM and fill mode. Extended attributes and left/right vertical splits
use actual PPU fetch sequences. The scanline IRQ detects three matching
nametable reads and clocks on the following read; frame timeout samples the
actual /RD pin, including during blanking. Fully decoded $2000/$2001 snoops,
NMI-vector acknowledgement, multiplier, MMC5A timer and GPIO registers are
implemented. GPIO has no external peripheral; undriven inputs resolve low.

The audio model has two pulse channels and an 8-bit PCM DAC, with PCM updates
from CPU reads (including opcodes and DMA) or direct writes, zero-sample IRQ,
nonzero-sample acknowledgement, envelopes and length counters. Channel clocks
run even when host audio is disabled. The oracle independently models board,
IRQ, PCM and channel status; it does not synthesize expansion audio. Nominal
audio gain and a 7,457-cycle envelope clock are used. Mixer component tolerances,
analog capture matching, letterless MMC5 silicon and unused SL split wiring
are not claimed. Undocumented low bits in mode-dependent CHR writes resolve
to zero; registers without a specified startup value have deterministic defaults.

References: [MMC5 registers and measured behavior](https://www.nesdev.org/wiki/MMC5),
[ExROM wiring](https://www.nesdev.org/wiki/ExROM),
[audio](https://www.nesdev.org/wiki/MMC5_audio), and
[AWJ's hardware CHR tests and reference pictures](https://sourceforge.net/p/fceultra/bugs/787/).
`tools/cyc/test_cyc_mmc5_public.py` checks all six published pictures, allowing
only a consistent RGB palette conversion and the reference's overscan crop.
ROM-free fixtures exercise bank handoffs, writable code, PCM/DMA, timer and
scanline IRQs, CHR selection, split rendering and expansion tones at all four
CPU/PPU alignments. The cartridge contracts enumerate all 65,536 multiplier
inputs and RAM geometry; rendered PCM is checked separately.

### MMC1 board wiring

[MMC1 board documentation](https://www.nesdev.org/wiki/MMC1) and the
[pinout](https://www.nesdev.org/wiki/MMC1_pinout) define SNROM RAM /CE,
SOROM/SXROM RAM banking, SUROM/SXROM outer PRG banking and SZROM RAM selection.
All use the currently selected CHR output, including PPU A12 changes in 4 KiB
CHR mode. A 512 KiB ROM's fixed bank stays within its selected 256 KiB half.
Native execution falls back while unequal CHR registers can change PRG during
an instruction, then resumes when the mapping is stable.

NES 2.0 sizes distinguish those boards. Deprecated submappers 1, 2 and 4 are
accepted only with matching SUROM, SOROM and SXROM geometry. Submapper 5 retains
fixed 32 KiB PRG; submapper 7 preserves the header's hardwired nametables.
Unspecified iNES RAM counts reserve 32 KiB for compatibility, so old MMC1
battery saves may need migration from 8 KiB; explicit NES 2.0 sizes keep exact
save lengths. Legacy headers with larger CHR use the SZROM RAM select pin.
Only explicit 8 KiB RAM geometry enables SNROM's additional /CE behavior.
SOROM/SZROM saves contain chip 1; chip 0 remains volatile. SXROM saves use the
physical A14:A13 order, with four consecutive 8 KiB banks.

### NTDEC 2722 / mapper 40

The [traced discrete PCB](https://forums.nesdev.org/viewtopic.php?t=19744)
defines fixed 8 KiB banks 6/4/5/7 at $6000/$8000/$A000/$E000, and a three-bit
$E000 bank register for $C000. $8000 clears/disables the counter and acknowledges
IRQ; $A000 starts it, without restarting an already enabled counter. The IRQ
arrives after 4,096 CPU clocks, including DMA. All address aliases use A15:A13;
there are no bus conflicts. Writes to the ROM at $6000 are ignored.

This enables the mapper 40 SMB2J conversion in the cycle runtime and oracle.
The compiler's native window begins at $8000, so code at $6000 runs through the
cycle interpreter and can return to native ROM normally. The different NTDEC
2752 multicart (submapper 1) is explicitly rejected. Tests cover fixed/read-only
windows, instruction handoffs, IRQ acknowledgement and counting through DMA.

Mapper 155 models MMC1A's PRG-register bit 4: RAM remains enabled and PRG
bit 3 drives A17 even in a fixed window. The separate SNROM CHR-controlled
/CE still applies. Deprecated mapper 1 submapper 3 selects the same revision.
Direct contracts enumerate every PRG register value in every mode, and ROM
fixtures test both fixed windows and both A17 states.

The ROM-free board tests exhaust inner/outer PRG mode combinations and PPU
A12, test hardwired mirroring, then run synthetic ROMs through all four
execution modes/alignments. Save tests restart each executable twice and
verify exact physical bytes plus rejection of invalid lengths. Submapper 6
is a Famicom Network System card on a separate card bus, not the console's
CPU/PPU bus; it remains explicitly rejected pending an FCNS machine model.
