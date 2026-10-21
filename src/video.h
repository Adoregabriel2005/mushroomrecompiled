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

typedef enum {
    VIDEO_ASPECT_FIT = 0,
    VIDEO_ASPECT_INTEGER = 1,
    VIDEO_ASPECT_STRETCH = 2
} VideoAspect;

typedef struct {
    VideoAspect aspect;
    int brightness;   /* percent, 100 = unchanged */
    int scanlines;    /* 0 off, 1 light, 2 strong */
} VideoOptions;

/* The window must have been created with SDL_WINDOW_VULKAN for the Vulkan
 * backend. Returns NULL when the requested backend cannot start. */
Video *video_create(SDL_Window *window, VideoBackend backend, bool vsync);
void video_destroy(Video *video);
bool video_present(Video *video, const uint32_t *argb, int width, int height,
                   const VideoOptions *options);
const char *video_name(const Video *video);

/* Destination rectangle for letterboxed presentation. */
static inline void video_dest_rect(int win_w, int win_h, int src_w, int src_h,
                                   VideoAspect aspect, int *x, int *y,
                                   int *dst_w, int *dst_h) {
    if (win_w <= 0 || win_h <= 0 || src_w <= 0 || src_h <= 0) {
        *x = *y = 0;
        *dst_w = *dst_h = 1;
        return;
    }
    double scale;
    if (aspect == VIDEO_ASPECT_STRETCH) {
        *x = 0;
        *y = 0;
        *dst_w = win_w;
        *dst_h = win_h;
        return;
    }
    double sx = (double)win_w / (double)src_w;
    double sy = (double)win_h / (double)src_h;
    scale = sx < sy ? sx : sy;
    if (aspect == VIDEO_ASPECT_INTEGER && scale >= 1.0) {
        scale = (double)(int)scale;
    }
    *dst_w = (int)((double)src_w * scale + 0.5);
    *dst_h = (int)((double)src_h * scale + 0.5);
    if (*dst_w < 1) *dst_w = 1;
    if (*dst_h < 1) *dst_h = 1;
    *x = (win_w - *dst_w) / 2;
    *y = (win_h - *dst_h) / 2;
}

#ifdef __cplusplus
}
#endif
