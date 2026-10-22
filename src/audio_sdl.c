#include "audio_sdl.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

struct AudioOut {
    SDL_AudioDeviceID device;
    int sample_rate;
    int volume;
    bool muted;
    int16_t *stereo;
    size_t stereo_capacity; /* int16 samples */
};

AudioOut *audio_out_create(int sample_rate, int volume, bool muted) {
    if (sample_rate < 8000 || sample_rate > 192000) return NULL;
    AudioOut *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->sample_rate = sample_rate;
    a->volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    a->muted = muted;

    SDL_AudioSpec want, have;
    SDL_zero(want);
    SDL_zero(have);
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = NULL;
    a->device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!a->device) {
        fprintf(stderr, "audio: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        free(a);
        return NULL;
    }
    if (have.format != AUDIO_S16SYS || have.channels != 2) {
        fprintf(stderr, "audio: device did not provide stereo S16 output\n");
        SDL_CloseAudioDevice(a->device);
        free(a);
        return NULL;
    }
    a->sample_rate = have.freq;
    a->stereo_capacity = 4096u * 2u;
    a->stereo = malloc(a->stereo_capacity * sizeof(*a->stereo));
    if (!a->stereo) {
        SDL_CloseAudioDevice(a->device);
        free(a);
        return NULL;
    }
    SDL_PauseAudioDevice(a->device, 0);
    return a;
}

void audio_out_destroy(AudioOut *a) {
    if (!a) return;
    if (a->device) {
        SDL_PauseAudioDevice(a->device, 1);
        SDL_CloseAudioDevice(a->device);
    }
    free(a->stereo);
    free(a);
}

void audio_out_push(AudioOut *a, const int16_t *mono, size_t count) {
    if (!a || !mono || count == 0) return;
    if (count * 2 > a->stereo_capacity) {
        size_t cap = count * 2;
        int16_t *grown = realloc(a->stereo, cap * sizeof(*grown));
        if (!grown) return;
        a->stereo = grown;
        a->stereo_capacity = cap;
    }
    int volume = a->muted ? 0 : a->volume;
    for (size_t i = 0; i < count; i++) {
        int16_t s = (int16_t)((int32_t)mono[i] * volume / 100);
        a->stereo[i * 2] = s;
        a->stereo[i * 2 + 1] = s;
    }
    SDL_QueueAudio(a->device, a->stereo, (Uint32)(count * 2 * sizeof(int16_t)));
}

void audio_out_set_volume(AudioOut *a, int volume) {
    if (!a) return;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    a->volume = volume;
}

void audio_out_set_muted(AudioOut *a, bool muted) {
    if (a) a->muted = muted;
}

void audio_out_pause(AudioOut *a, bool paused) {
    if (a && a->device) SDL_PauseAudioDevice(a->device, paused ? 1 : 0);
}

void audio_out_clear(AudioOut *a) {
    if (a && a->device) SDL_ClearQueuedAudio(a->device);
}

int audio_out_sample_rate(const AudioOut *a) {
    return a ? a->sample_rate : 0;
}

size_t audio_out_queued(const AudioOut *a) {
    return a && a->device ? (size_t)SDL_GetQueuedAudioSize(a->device) : 0;
}

size_t audio_out_target(const AudioOut *a) {
    /* 40 ms stereo S16, the same queue target as smb3pc. */
    return a ? (size_t)(a->sample_rate / 25) * 2u * sizeof(int16_t) : 0;
}
