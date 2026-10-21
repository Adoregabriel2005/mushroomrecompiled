#include "audio_sdl.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct AudioOut {
    SDL_AudioDeviceID device;
    SDL_mutex *lock;
    int sample_rate;
    int volume;
    bool muted;
    int16_t *ring;
    size_t capacity; /* interleaved samples */
    size_t read;
    size_t write;
    size_t count;
};

static void SDLCALL audio_callback(void *userdata, Uint8 *stream, int len) {
    AudioOut *a = userdata;
    int16_t *out = (int16_t *)stream;
    size_t want = (size_t)len / sizeof(int16_t);
    SDL_LockMutex(a->lock);
    for (size_t i = 0; i < want; i++) {
        if (a->count > 0) {
            out[i] = a->ring[a->read];
            a->read = (a->read + 1) % a->capacity;
            a->count--;
        } else {
            out[i] = 0;
        }
    }
    SDL_UnlockMutex(a->lock);
}

AudioOut *audio_out_create(int sample_rate, int volume, bool muted) {
    if (sample_rate < 8000 || sample_rate > 192000) return NULL;
    AudioOut *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->sample_rate = sample_rate;
    a->volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    a->muted = muted;
    a->lock = SDL_CreateMutex();
    a->capacity = (size_t)sample_rate * 2u * 2u; /* two seconds, stereo */
    a->ring = calloc(a->capacity, sizeof(*a->ring));
    if (!a->lock || !a->ring) {
        audio_out_destroy(a);
        return NULL;
    }

    SDL_AudioSpec want = {0}, have = {0};
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_callback;
    want.userdata = a;
    a->device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!a->device) {
        fprintf(stderr, "audio: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        audio_out_destroy(a);
        return NULL;
    }
    if (have.format != AUDIO_S16SYS || have.channels != 2) {
        fprintf(stderr, "audio: device did not provide stereo S16 output\n");
        audio_out_destroy(a);
        return NULL;
    }
    a->sample_rate = have.freq;
    SDL_PauseAudioDevice(a->device, 0);
    return a;
}

void audio_out_destroy(AudioOut *a) {
    if (!a) return;
    if (a->device) {
        SDL_PauseAudioDevice(a->device, 1);
        SDL_CloseAudioDevice(a->device);
    }
    if (a->lock) SDL_DestroyMutex(a->lock);
    free(a->ring);
    free(a);
}

void audio_out_push(AudioOut *a, const int16_t *interleaved, size_t count) {
    if (!a || !interleaved || count == 0) return;
    SDL_LockMutex(a->lock);
    int volume = a->muted ? 0 : a->volume;
    for (size_t i = 0; i < count; i++) {
        int32_t sample = (int32_t)interleaved[i] * volume / 100;
        if (a->count == a->capacity) {
            a->read = (a->read + 1) % a->capacity;
            a->count--;
        }
        a->ring[a->write] = (int16_t)sample;
        a->write = (a->write + 1) % a->capacity;
        a->count++;
    }
    SDL_UnlockMutex(a->lock);
}

void audio_out_set_volume(AudioOut *a, int volume) {
    if (!a) return;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    SDL_LockMutex(a->lock);
    a->volume = volume;
    SDL_UnlockMutex(a->lock);
}

void audio_out_set_muted(AudioOut *a, bool muted) {
    if (!a) return;
    SDL_LockMutex(a->lock);
    a->muted = muted;
    SDL_UnlockMutex(a->lock);
}

void audio_out_pause(AudioOut *a, bool paused) {
    if (a && a->device) SDL_PauseAudioDevice(a->device, paused ? 1 : 0);
}

void audio_out_clear(AudioOut *a) {
    if (!a) return;
    SDL_LockMutex(a->lock);
    a->read = a->write = a->count = 0;
    SDL_UnlockMutex(a->lock);
}

int audio_out_sample_rate(const AudioOut *a) {
    return a ? a->sample_rate : 0;
}
