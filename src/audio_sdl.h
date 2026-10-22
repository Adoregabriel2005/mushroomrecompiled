#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AudioOut AudioOut;

/* Opens the default SDL2 audio device with a queue (no callback), like
 * smb3pc. The device is stereo; input is mono and is duplicated here. */
AudioOut *audio_out_create(int sample_rate, int volume, bool muted);
void audio_out_destroy(AudioOut *audio);
void audio_out_push(AudioOut *audio, const int16_t *mono, size_t count);
void audio_out_set_volume(AudioOut *audio, int volume);
void audio_out_set_muted(AudioOut *audio, bool muted);
void audio_out_pause(AudioOut *audio, bool paused);
void audio_out_clear(AudioOut *audio);
int audio_out_sample_rate(const AudioOut *audio);
size_t audio_out_queued(const AudioOut *audio);
size_t audio_out_target(const AudioOut *audio);

#ifdef __cplusplus
}
#endif
