#pragma once
#include "video.h"

typedef struct VideoOps {
    void (*destroy)(Video *video);
    bool (*present)(Video *video, const uint32_t *argb, int width, int height,
                    const VideoOptions *options);
    const char *(*name)(const Video *video);
} VideoOps;

struct Video {
    const VideoOps *ops;
};

Video *video_sdl_create(SDL_Window *window, bool vsync);
Video *video_vulkan_create(SDL_Window *window, bool vsync);
