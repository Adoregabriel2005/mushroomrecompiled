#include "video_internal.h"

Video *video_create(SDL_Window *window, VideoBackend backend, bool vsync) {
    if (!window) return NULL;
    if (backend == VIDEO_BACKEND_VULKAN) return video_vulkan_create(window, vsync);
    return video_sdl_create(window, vsync);
}

void video_destroy(Video *video) {
    if (!video) return;
    if (video->ops && video->ops->destroy) video->ops->destroy(video);
}

bool video_present(Video *video, const uint32_t *argb, int width, int height,
                   const VideoOptions *options) {
    if (!video || !video->ops || !video->ops->present) return false;
    return video->ops->present(video, argb, width, height, options);
}

const char *video_name(const Video *video) {
    if (!video || !video->ops || !video->ops->name) return "unavailable";
    return video->ops->name(video);
}
