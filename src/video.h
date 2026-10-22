#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Video Video;

typedef struct {
    bool scanlines;
} VideoOptions;

/* Vulkan-only presenter. The window must have been created with
 * SDL_WINDOW_VULKAN. Returns NULL when Vulkan cannot start. */
Video *video_create(SDL_Window *window, bool vsync);
void video_destroy(Video *video);
bool video_present(Video *video, const uint32_t *argb, int width, int height,
                   const VideoOptions *options);
const char *video_name(const Video *video);

#ifdef __cplusplus
}
#endif
