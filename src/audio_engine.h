#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Audio output bridge for the cycle-accurate runner.
 *
 * NESRecomp's own APU stays authoritative for everything the CPU can see
 * ($4015, the frame IRQ and DMC timing). In parallel, two Blargg APUs render
 * the audible output: one for music and one for sound effects. Generated code
 * tags JSRs into SMB3's sound routines with an owner (snd_owner.h), and every
 * APU write is routed to the matching voice set. The two outputs are summed
 * at full level and clamped.
 */
void cyc_audio_power_on(uint64_t cyc, int sample_rate);
void cyc_audio_set_sample_rate(int sample_rate);
void cyc_audio_write(uint16_t addr, uint8_t value, uint64_t cyc);
void cyc_audio_reset_sfx(void);
void cyc_audio_set_rate_scale(double scale);
size_t cyc_audio_read_samples(int16_t *out, size_t max);
void cyc_audio_render(uint64_t cyc);

/* Host save-state section: both APUs, the selected output rate, and the
 * owner stack. */
size_t cyc_audio_state_size(void);
void cyc_audio_state_save(uint8_t *out);
bool cyc_audio_state_validate(const uint8_t *data, size_t len);
void cyc_audio_state_load(const uint8_t *data, uint64_t cyc);

#ifdef __cplusplus
}
#endif
