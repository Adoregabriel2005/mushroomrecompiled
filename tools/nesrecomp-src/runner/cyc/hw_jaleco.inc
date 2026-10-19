/* Jaleco SS88006 (mapper 18). https://www.nesdev.org/wiki/INES_Mapper_018
 * Registers decode A15-A12 and A1-A0; each bank register is written as two
 * nibbles (A0 = 1 for the high nibble). $8000-$9001: three 8 KiB PRG banks
 * (6 bits), last bank fixed. $9002: work RAM chip enable (bit 0) and write
 * permission (bit 1). $A000-$D003: eight 1 KiB CHR banks (8 bits).
 * $E000-$E003: IRQ reload nibbles. $F000: reload the counter. $F001: bit 0
 * enables counting; bits 3/2/1 select a 4/8/12-bit counter (else 16). Both
 * acknowledge. The selected low bits count down every CPU cycle; the others
 * keep their value; the IRQ fires when they wrap from 0, and counting goes
 * on. $F002: mirroring (horizontal, vertical, one-screen A, B). The $F003
 * uPD7755/7756 speech chip is not modeled: its samples are in the chip. */

static void jaleco_apply(void)
{
    const HwJaleco *j = &hw_cart.m.jaleco;
    for (unsigned slot = 0; slot < 3; ++slot) map_prg8(slot, j->prg[slot] & 0x3f);
    map_prg8(3, -1);
    for (unsigned page = 0; page < 8; ++page) map_chr1(page, j->chr[page]);
    static const uint8_t mirror[4] = { HW_MIRROR_HORIZONTAL, HW_MIRROR_VERTICAL,
                                       HW_MIRROR_SCREEN_A, HW_MIRROR_SCREEN_B };
    hw_cart.mirroring = mirror[j->mirror & 3];
    hw_cart.wram_readable = hw_cart.has_wram && (j->ram_ctrl & 1);
    hw_cart.wram_writable = hw_cart.has_wram && (j->ram_ctrl & 3) == 3;
}

static uint16_t jaleco_mask(void)
{
    uint8_t c = hw_cart.m.jaleco.ctrl;
    return (c & 8) ? 0x000f : (c & 4) ? 0x00ff : (c & 2) ? 0x0fff : 0xffff;
}

static void jaleco_write(uint16_t addr, uint8_t value)
{
    HwJaleco *j = &hw_cart.m.jaleco;
    unsigned nib = value & 15, hi = addr & 1;
    unsigned reg = ((addr >> 11) & 0x0e) | ((addr >> 1) & 1);   /* (A14-A12) * 2 + A1 */
    uint8_t *bank = NULL;
    switch (reg) {
    case 0: case 1: case 2: bank = &j->prg[reg]; break;     /* $8000, $8002, $9000 */
    case 3: if (!hi) j->ram_ctrl = (uint8_t)nib; break;       /* $9002 ($9003 unused) */
    case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11:
        bank = &j->chr[reg - 4]; break;                       /* $A000-$D003 */
    case 12: case 13:                                         /* $E000-$E003 */
        j->reload[(reg - 12) * 2 + hi] = (uint8_t)nib; break;
    default:                                                  /* $F000-$F003 */
        switch (addr & 3) {
        case 0:
            j->counter = (uint16_t)(j->reload[0] | j->reload[1] << 4 | j->reload[2] << 8 | j->reload[3] << 12);
            hw_cart.m.irq_out = 0; break;
        case 1: j->ctrl = (uint8_t)nib; hw_cart.m.irq_out = 0; break;
        case 2: j->mirror = (uint8_t)(nib & 3); break;
        default: break;                                       /* speech chip */
        }
    }
    if (bank) *bank = (uint8_t)(hi ? (*bank & 0x0f) | nib << 4 : (*bank & 0xf0) | nib);
    jaleco_apply();
}

static void jaleco_clock(void)
{
    HwJaleco *j = &hw_cart.m.jaleco;
    if (!(j->ctrl & 1)) return;
    uint16_t mask = jaleco_mask(), low = j->counter & mask;
    if (!low) hw_cart.m.irq_out = 1;
    j->counter = (uint16_t)((j->counter & ~mask) | ((low - 1) & mask));
}
