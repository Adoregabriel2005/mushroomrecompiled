#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Video Video;

typedef enum {
    VIDEO_BACKEND_SDL = 0,
    VIDEO_BACKEND_VULKAN = 1
} VideoBackend;

typedef struct {
    bool scanlines;
} VideoOptions;

/* The window must have been created with SDL_WINDOW_VULKAN for the Vulkan
 * backend. Returns NULL when the requested backend cannot start. */
Video *video_create(SDL_Window *window, VideoBackend backend, bool vsync);
void video_destroy(Video *video);
bool video_present(Video *video, const uint32_t *argb, int width, int height,
                   const VideoOptions *options);
const char *video_name(const Video *video);

#ifdef __cplusplus
}
#endif
