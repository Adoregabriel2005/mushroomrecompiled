/*
 * hw_fds_audio.c - the FDS RAM Adapter's sound unit: a 64-step 6-bit
 * wavetable channel with a volume envelope, frequency modulated by a
 * 64-step modulation table with its own envelope, and a master volume.
 * hw_fds.c routes $4040-$4092 here and clocks it once per CPU cycle.
 *
 * Reference. The oracle is nesref's "Mesen 0.9.9" libretro core, whose FDS
 * code is libretro/Mesen master 0102910 rather than the 0.9.9 release (see
 * hw_fds.c). Its sound unit is Core/FdsAudio.h, Core/ModChannel.h and
 * Core/BaseFdsChannel.h of that tree ("lr:" below), which differ from the
 * 0.9.9 release in four ways, each observed in nesref (tools/cyc/
 * fds_audio_gates.py): the wavetable reads back the sample at the wave
 * position while writes are disabled, the mod output is not gated by the
 * mod unit's enable, $4084/$4085 writes recompute the mod output, and the
 * master envelope speed powers on $E8 (0.9.9: $FF). Mesen2 (SourMesen/Mesen2
 * b9fa69d Core/NES/Mappers/FDS/FdsAudio.cpp, "m2:") differs again:
 *
 *   behaviour                          mesen (default)   mesen2 / hardware
 *   $4080/$4082/$4083 recompute mod    no (lr:125-139)   yes (m2:92-117)
 *   $4083.7 write resets position      no; held at 0     yes (m2:103-105)
 *                                      each cycle (lr:61)
 *   wave advances while $4089.7 set    no (lr:66)        yes (m2:40)
 *   output while $4089.7 set           follows (lr:73)   held (m2:49-51)
 *
 * The output stage is not the sound unit's: Mesen mixes the 6-bit output
 * (0-63) unfiltered at 20 units per step, against 477600 / (8128 / n + 100)
 * for the pulse pair (SoundMixer.cpp GetOutputVolume; Mesen2 NesSoundMixer.cpp
 * the same weight). This runtime's 2A03 mix is the same curve at 1/5000 of
 * Mesen's scale (hw_apu.c pulse_mix), so the FDS level is 20 / 5000 per step,
 * in phase with the 2A03: at full volume 1.69x one full-volume pulse. The
 * hardware profile instead uses the nesdev wiki's measured level (2.4x the
 * pulse, same polarity) and its output filter, "approximated as a 1-pole
 * lowpass with a cutoff of ~2000Hz" (nesdev FDS_audio). The hardware column
 * is a judgment call, not oracle-verified: nothing here can measure a
 * console. That low-pass belongs to the RAM Adapter, on the 2C33's sound
 * alone (nesdev gives it for "this output signal"; lidnariq derived it from
 * the adapter board's RC network, forum t=10233; NSFPlay filters only its FDS
 * channel), so it stays here and not in the console output stage the mix
 * goes through afterwards (hw_apu.c: the FDS defaults to the famicom model). Neither Mesen models the newer nesdev findings ($4083.7 speeding
 * the envelopes up 4x, $4087.6, the 16-cycle wave/mod ticks and 20-bit wave
 * accumulator, $4091); they are not modelled in any profile.
 *
 * Everything the CPU can read back ($4090/$4092 gains, $4040-$407F while
 * writes are disabled = the sample at the wave position) depends on the
 * synthesis, so it runs on every cycle whether or not audio is being output.
 * Envelope gain changes (folded per frame) and a per-frame count of mod and
 * wave steps go to the always-on ring (cyc_ring.h); a step per event would
 * be thousands a frame and evict the drive's events.
 */
#include "hw_fds.h"

#include "cyc_ring.h"
#include "hw_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define F (hw_cart.m.fds)

static bool mesen2_audio;          /* the mesen2 and hardware profiles */
static bool hardware_output;       /* the hardware profile's output stage */

/* Output-stage state; not machine state (it never reaches the CPU). */
static struct {
    double lp, lp_a;
    bool   lp_on;
} out;

/* Per-frame activity for the ring (not machine state). */
static struct {
    uint32_t wave_steps, mod_steps;
} frame_count;

void fds_audio_set_profile(CycFdsProfile profile)
{
    mesen2_audio = profile == CYC_FDS_PROFILE_MESEN2 || profile == CYC_FDS_PROFILE_HARDWARE;
    hardware_output = profile == CYC_FDS_PROFILE_HARDWARE;
}

void fds_audio_power_on(void)
{
    /* lr:FdsAudio.h:19-35, BaseFdsChannel.h:8-17, ModChannel.h:11-18: all
     * zero except the master envelope speed. The tables are uninitialized
     * heap in Mesen (only ever read after a program writes them). */
    F.master_speed = 0xE8;
    memset(&frame_count, 0, sizeof(frame_count));
    out.lp = 0;
}

/* ---- the envelopes (BaseFdsChannel) ------------------------------------ */

/* lr:BaseFdsChannel.h:85-88. */
static uint32_t env_period(uint8_t speed) { return 8u * (speed + 1u) * F.master_speed; }

/* $4080 / $4084 (lr:BaseFdsChannel.h:25-38): the timer restarts, and with
 * the envelope off the speed bits are the gain. */
static void env_write(uint8_t *speed, uint8_t *gain, uint8_t *off, uint8_t *inc, uint32_t *timer, uint8_t value)
{
    *speed = value & 0x3F;
    *inc = (value >> 6) & 1;
    *off = value >> 7;
    *timer = env_period(*speed);
    if (*off) *gain = *speed;
}

/* lr:BaseFdsChannel.h:50-66. The timer counts down before it is tested, so
 * a timer no write ever loaded (0 at power-on) wraps and runs 2^32 cycles. */
static bool env_tick(uint8_t speed, uint8_t *gain, uint8_t off, uint8_t inc, uint32_t *timer, unsigned which)
{
    if (off || F.master_speed == 0) return false;
    if (--*timer != 0) return false;
    *timer = env_period(speed);
    uint8_t before = *gain;
    if (inc && *gain < 32) ++*gain;
    else if (!inc && *gain > 0) --*gain;
    if (*gain != before) cyc_ring_push(CYC_EV_FDS_ENV, (uint16_t)which, *gain);
    return true;
}

/* ---- the modulator (ModChannel) ---------------------------------------- */

/* lr:ModChannel.h:55-63: a 7-bit signed counter that wraps. */
static void mod_set_counter(int value)
{
    int8_t c = (int8_t)value;
    if (c >= 64) c = (int8_t)(c - 128);
    else if (c < -64) c = (int8_t)(c + 128);
    F.mod_counter = c;
}

/* Floor of v / 2^n and the low n bits, for negative v too (the reference
 * uses >> and & on int32_t, i.e. two's complement arithmetic shifts). */
static int32_t floor_shift(int32_t v, unsigned n, int32_t *low)
{
    int32_t mask = (int32_t)((1u << n) - 1);
    int32_t lo = (int32_t)((uint32_t)v & (uint32_t)mask);
    *low = lo;
    return (v - lo) / (int32_t)(1 << n);
}

/* lr:ModChannel.h:88-122 (the nesdev wiki's formula as Mesen wrote it):
 * the pitch adjustment from the counter, the mod gain and the wave pitch. */
static void mod_update_output(uint16_t pitch)
{
    int32_t remainder;
    int32_t temp = floor_shift((int32_t)F.mod_counter * F.mod_gain, 4, &remainder);
    /* 1. drop 4 bits, "rounding" away from zero unless bit 7 is set */
    if (remainder > 0 && (temp & 0x80) == 0) temp += F.mod_counter < 0 ? -1 : 2;
    /* 2. wrap into -64..191 */
    if (temp >= 192) temp -= 256;
    else if (temp < -64) temp += 256;
    /* 3. times the pitch, rounded to nearest while dropping 6 bits */
    temp = floor_shift((int32_t)pitch * temp, 6, &remainder);
    if (remainder >= 32) temp += 1;
    F.mod_output = temp;
}

/* lr:ModChannel.h:70-86: the accumulator overflowing steps the table. */
static bool mod_tick(void)
{
    if (F.mod_disabled || F.mod_freq == 0) return false;
    F.mod_overflow = (uint16_t)(F.mod_overflow + F.mod_freq);
    if (F.mod_overflow >= F.mod_freq) return false;
    static const int8_t step[8] = { 0, 1, 2, 4, 0x7F /* reset */, -4, -2, -1 };
    int8_t s = step[F.mod_table[F.mod_pos] & 7];
    mod_set_counter(s == 0x7F ? 0 : F.mod_counter + s);
    F.mod_pos = (F.mod_pos + 1) & 0x3F;
    frame_count.mod_steps++;
    return true;
}

/* ---- the wave channel (FdsAudio) --------------------------------------- */

/* lr:FdsAudio.h:73-83: gain (clamped to 32) times the master volume. */
static void update_output(void)
{
    static const uint32_t master[4] = { 36, 24, 17, 14 };
    if (mesen2_audio && F.wave_write) return;          /* m2:49-51: held */
    uint32_t level = (F.vol_gain < 32 ? F.vol_gain : 32u) * master[F.master_vol];
    F.out_level = (uint8_t)((F.wave[F.wave_pos] * level) / 1152u);
}

static void wave_step(int32_t pitch)
{
    F.wave_overflow = (uint16_t)(F.wave_overflow + pitch);
    if (F.wave_overflow < pitch) {
        F.wave_pos = (F.wave_pos + 1) & 0x3F;
        frame_count.wave_steps++;
    }
}

/* One CPU cycle (lr:FdsAudio.h:47-71, m2:25-46). */
void fds_audio_clock(void)
{
    uint16_t freq = F.vol_freq;
    if (!F.wave_halt && !F.env_disabled) {
        env_tick(F.vol_speed, &F.vol_gain, F.vol_env_off, F.vol_increase, &F.vol_timer, 0);
        if (env_tick(F.mod_speed, &F.mod_gain, F.mod_env_off, F.mod_increase, &F.mod_timer, 1))
            mod_update_output(freq);
    }
    if (mod_tick()) mod_update_output(freq);
    int32_t pitch = (int32_t)freq + F.mod_output;
    if (mesen2_audio) {
        update_output();
        if (!F.wave_halt && pitch > 0) wave_step(pitch);
    } else if (F.wave_halt) {
        F.wave_pos = 0;
        update_output();
    } else {
        update_output();
        if (pitch > 0 && !F.wave_write) wave_step(pitch);
    }
}

/* ---- registers --------------------------------------------------------- */

/* lr:FdsAudio.h:115-160, m2:84-142. Called only while $4023.1 enables the
 * sound registers (hw_fds.c). */
void fds_audio_write(uint16_t addr, uint8_t value)
{
    if (addr <= 0x407F) {
        if (F.wave_write) F.wave[addr & 0x3F] = value & 0x3F;
        return;
    }
    if (addr <= 0x408A) F.sound_reg[addr - 0x4080] = value;
    switch (addr) {
    case 0x4080:
        env_write(&F.vol_speed, &F.vol_gain, &F.vol_env_off, &F.vol_increase, &F.vol_timer, value);
        if (mesen2_audio) mod_update_output(F.vol_freq);
        break;
    case 0x4082:
        F.vol_freq = (uint16_t)((F.vol_freq & 0x0F00) | value);
        if (mesen2_audio) mod_update_output(F.vol_freq);
        break;
    case 0x4083:
        F.env_disabled = (value >> 6) & 1;
        F.wave_halt = value >> 7;
        if (mesen2_audio && F.wave_halt) F.wave_pos = 0;
        if (F.env_disabled) {
            F.vol_timer = env_period(F.vol_speed);
            F.mod_timer = env_period(F.mod_speed);
        }
        F.vol_freq = (uint16_t)((F.vol_freq & 0x00FF) | (value & 0x0F) << 8);
        if (mesen2_audio) mod_update_output(F.vol_freq);
        break;
    case 0x4084:
        env_write(&F.mod_speed, &F.mod_gain, &F.mod_env_off, &F.mod_increase, &F.mod_timer, value);
        mod_update_output(F.vol_freq);
        break;
    case 0x4085:
        mod_set_counter(value & 0x7F);
        mod_update_output(F.vol_freq);
        break;
    case 0x4086:
        F.mod_freq = (uint16_t)((F.mod_freq & 0x0F00) | value);
        break;
    case 0x4087:
        F.mod_freq = (uint16_t)((F.mod_freq & 0x00FF) | (value & 0x0F) << 8);
        F.mod_disabled = value >> 7;
        if (F.mod_disabled) F.mod_overflow = 0;
        break;
    case 0x4088:
        /* lr:ModChannel.h:46-53: only while the mod unit is disabled; each
         * write fills two steps. */
        if (F.mod_disabled) {
            F.mod_table[F.mod_pos & 0x3F] = value & 7;
            F.mod_table[(F.mod_pos + 1) & 0x3F] = value & 7;
            F.mod_pos = (F.mod_pos + 2) & 0x3F;
        }
        break;
    case 0x4089:
        F.master_vol = value & 3;
        F.wave_write = value >> 7;
        break;
    case 0x408A:
        F.master_speed = value;
        break;
    default:
        break;
    }
}

/* lr:FdsAudio.h:94-113. False leaves the bus open ($4080-$408F, $4091). */
bool fds_audio_read(uint16_t addr, uint8_t *value)
{
    if (addr <= 0x407F) {
        *value = (uint8_t)((*value & 0xC0) | F.wave[F.wave_write ? (addr & 0x3F) : F.wave_pos]);
        return true;
    }
    if (addr == 0x4090) { *value = (uint8_t)((*value & 0xC0) | F.vol_gain); return true; }
    if (addr == 0x4092) { *value = (uint8_t)((*value & 0xC0) | F.mod_gain); return true; }
    return false;
}

/* ---- output ------------------------------------------------------------ */

double fds_audio_level(void)
{
    if (!hardware_output) return F.out_level * (20.0 / 5000.0);
    if (!out.lp_on) {
        out.lp_on = true;
        out.lp_a = 1.0 - exp(-2.0 * 3.14159265358979 * 2000.0 / (21477272.0 / 12.0));
    }
    /* 2.4x the full-volume pulse (hw_apu.c pulse_mix[15]) at output 63. */
    const double full_pulse = 95.52 / (8128.0 / 15.0 + 100.0);
    out.lp += out.lp_a * (F.out_level * (2.4 * full_pulse / 63.0) - out.lp);
    return out.lp;
}

void fds_audio_frame_end(void)
{
    if (frame_count.wave_steps | frame_count.mod_steps)
        cyc_ring_push(CYC_EV_FDS_AUDIO, (uint16_t)(frame_count.mod_steps > 0xFFFF ? 0xFFFF : frame_count.mod_steps),
                      frame_count.wave_steps);
    memset(&frame_count, 0, sizeof(frame_count));
}

/* Mesen's field order and widths (lr:FdsAudio.h:37-45, ModChannel.h:21-27,
 * BaseFdsChannel.h:19-22), so a nesref savestate compares byte for byte. */
size_t cyc_fds_audio_state(uint8_t *o)
{
    uint8_t *p = o;
#define PUT8(v) (*p++ = (uint8_t)(v))
#define PUT16(v) (PUT8(v), PUT8((v) >> 8))
#define PUT32(v) (PUT16(v), PUT16((uint32_t)(v) >> 16))
    PUT8(F.vol_speed); PUT8(F.vol_gain); PUT8(F.vol_env_off); PUT8(F.vol_increase);
    PUT16(F.vol_freq); PUT32(F.vol_timer); PUT8(F.master_speed);
    PUT8(F.mod_speed); PUT8(F.mod_gain); PUT8(F.mod_env_off); PUT8(F.mod_increase);
    PUT16(F.mod_freq); PUT32(F.mod_timer); PUT8(F.master_speed);
    PUT8(F.mod_counter); PUT8(F.mod_disabled); PUT8(F.mod_pos); PUT16(F.mod_overflow);
    memcpy(p, F.mod_table, 64); p += 64;
    PUT32((uint32_t)F.mod_output);
    PUT8(F.wave_write); PUT8(F.env_disabled); PUT8(F.wave_halt); PUT8(F.master_vol);
    PUT16(F.wave_overflow); PUT32(0); /* _wavePitch: never used */ PUT8(F.wave_pos); PUT8(F.out_level);
    memcpy(p, F.wave, 64); p += 64;
#undef PUT8
#undef PUT16
#undef PUT32
    return (size_t)(p - o);
}

void fds_audio_state_dump(void *file)
{
    FILE *f = (FILE *)file;
    fprintf(f, "fds.vol speed=%u gain=%u off=%u inc=%u freq=%03X timer=%u\n"
               "fds.mod speed=%u gain=%u off=%u inc=%u freq=%03X timer=%u counter=%d disabled=%u pos=%u acc=%04X out=%d\n"
               "fds.sound master_speed=%02X env_disabled=%u halt=%u master_vol=%u wave_write=%u pos=%u acc=%04X out=%u\n",
            F.vol_speed, F.vol_gain, F.vol_env_off, F.vol_increase, F.vol_freq, F.vol_timer,
            F.mod_speed, F.mod_gain, F.mod_env_off, F.mod_increase, F.mod_freq, F.mod_timer, F.mod_counter,
            F.mod_disabled, F.mod_pos, F.mod_overflow, F.mod_output,
            F.master_speed, F.env_disabled, F.wave_halt, F.master_vol, F.wave_write, F.wave_pos, F.wave_overflow,
            F.out_level);
    fprintf(f, "fds.wave");
    for (unsigned i = 0; i < 64; ++i) fprintf(f, " %02X", F.wave[i]);
    fprintf(f, "\nfds.mod_table ");
    for (unsigned i = 0; i < 64; ++i) fprintf(f, "%u", F.mod_table[i]);
    fputc('\n', f);
}
