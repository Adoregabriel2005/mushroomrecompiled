#include "snd_owner.h"

#include <string.h>

uint8_t snd_owner = SND_OWNER_MUSIC;
uint8_t snd_sav[256];

static uint8_t both_previous;

void snd_owner_reset(void) {
    snd_owner = SND_OWNER_MUSIC;
    memset(snd_sav, SND_OWNER_MUSIC, sizeof(snd_sav));
    both_previous = SND_OWNER_MUSIC;
}

void snd_owner_push(uint8_t s, uint8_t owner) {
    snd_sav[s] = snd_owner;
    if (owner != SND_OWNER_INHERIT) snd_owner = owner;
}

void snd_owner_pop(uint8_t s) {
    snd_owner = snd_sav[s];
}

void snd_owner_set(uint8_t owner) {
    if (owner <= SND_OWNER_BOTH) snd_owner = owner;
}

void snd_owner_begin_both(void) {
    both_previous = snd_owner;
    snd_owner = SND_OWNER_BOTH;
}

void snd_owner_end_both(void) {
    snd_owner = both_previous;
}

uint8_t snd_owner_current(void) {
    return snd_owner;
}

void snd_owner_save(uint8_t *out) {
    out[0] = snd_owner;
    memcpy(out + 1, snd_sav, sizeof(snd_sav));
}

bool snd_owner_load(const uint8_t *in, size_t len, bool apply) {
    if (!in || len != SND_OWNER_STATE_BYTES) return false;
    if (in[0] > SND_OWNER_BOTH) return false;
    if (!apply) return true;
    snd_owner = in[0];
    memcpy(snd_sav, in + 1, sizeof(snd_sav));
    both_previous = snd_owner;
    return true;
}
