#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Music and sound effects drive separate Blargg APUs. Generated code calls
 * snd_owner_push() before every JSR and snd_owner_pop() after every RTS. A
 * JSR into a configured sound routine changes the owner; every other call
 * inherits the caller's owner, so shared helpers write for whoever called
 * them. The save/restore slot is the 6502 stack pointer at the call, matching
 * smb3pc's snd_sav[cpu.s].
 */
enum {
    SND_OWNER_MUSIC = 0,
    SND_OWNER_SFX = 1,
    SND_OWNER_BOTH = 2,
    SND_OWNER_INHERIT = 0xFF
};

/* The current owner. Generated code uses snd_owner_push/pop; the runtime
 * audio bridge reads it for each APU write. */
extern uint8_t snd_owner;
extern uint8_t snd_sav[256];

void snd_owner_reset(void);
void snd_owner_push(uint8_t s, uint8_t owner);
void snd_owner_pop(uint8_t s);
void snd_owner_set(uint8_t owner);
void snd_owner_begin_both(void);
void snd_owner_end_both(void);
uint8_t snd_owner_current(void);

/* Fixed host-state section for the owner stack. */
#define SND_OWNER_STATE_BYTES (1u + 256u)
void snd_owner_save(uint8_t *out);
bool snd_owner_load(const uint8_t *in, size_t len, bool apply);

#ifdef __cplusplus
}
#endif
