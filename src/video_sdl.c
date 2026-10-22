#include "video_internal.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    Video base;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    int tex_w;
    int tex_h;
} SdlVideo;

static bool ensure_texture(SdlVideo *v, int width, int height) {
    if (v->texture && v->tex_w == width && v->tex_h == height) return true;
    if (v->texture) {
        SDL_DestroyTexture(v->texture);
        v->texture = NULL;
    }
    v->texture = SDL_CreateTexture(v->renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!v->texture) {
        fprintf(stderr, "video: SDL_CreateTexture(%dx%d) failed: %s\n", width, height, SDL_GetError());
        v->tex_w = v->tex_h = 0;
        return false;
    }
    v->tex_w = width;
    v->tex_h = height;
    return true;
}

static void sdl_destroy(Video *base) {
    SdlVideo *v = (SdlVideo *)base;
    if (v->texture) SDL_DestroyTexture(v->texture);
    if (v->renderer) SDL_DestroyRenderer(v->renderer);
    free(v);
}

static bool sdl_present(Video *base, const uint32_t *argb, int width, int height,
                        const VideoOptions *options) {
    SdlVideo *v = (SdlVideo *)base;
    if (!argb || width <= 0 || height <= 0) return false;
    if (!ensure_texture(v, width, height)) return false;
    if (SDL_UpdateTexture(v->texture, NULL, argb, width * (int)sizeof(uint32_t)) != 0) {
        fprintf(stderr, "video: SDL_UpdateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    int win_w = 0, win_h = 0;
    SDL_GetRendererOutputSize(v->renderer, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) SDL_GetWindowSize(v->window, &win_w, &win_h);

    SDL_SetRenderDrawColor(v->renderer, 0, 0, 0, 255);
    SDL_RenderClear(v->renderer);

    /* smb3pc overscan: 8 pixels hidden on each side, so the 4:3 picture
     * shown is 240 columns wide; the 426-column widescreen compose buffer
     * is shown whole. Square pixels, centred. */
    int src_x = 0, src_w = width;
    if (width == 256) {
        src_x = 8;
        src_w = 240;
    }
    /* Whole pixels only. A fractional fit (1920/426 = 4.5) gives some columns
     * four device pixels and the rest five, which is the uneven-pixel look;
     * every source pixel has to cover the same number of window pixels, so the
     * scale is the largest integer that fits and the remainder is black. */
    const double sx = (double)win_w / (double)src_w;
    const double sy = (double)win_h / (double)height;
    const double fit = sx < sy ? sx : sy;
    int scale = (int)fit;
    if (scale < 1) scale = 1;
    int dw = src_w * scale;
    int dh = height * scale;
    SDL_Rect src = {src_x, 0, src_w, height};
    SDL_Rect dst = {(win_w - dw) / 2, (win_h - dh) / 2, dw, dh};
    if (SDL_RenderCopy(v->renderer, v->texture, &src, &dst) != 0) {
        fprintf(stderr, "video: SDL_RenderCopy failed: %s\n", SDL_GetError());
        return false;
    }

    if (options && options->scanlines && dh > 2) {
        SDL_SetRenderDrawBlendMode(v->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(v->renderer, 0, 0, 0, 72);
        for (int row = 1; row < height; row += 2) {
            int y = dst.y + row * dh / height;
            SDL_RenderDrawLine(v->renderer, dst.x, y, dst.x + dst.w - 1, y);
        }
    }
    SDL_RenderPresent(v->renderer);
    return true;
}

static const char *sdl_name(const Video *base) {
    (void)base;
    return "SDL2";
}

static const VideoOps sdl_ops = {sdl_destroy, sdl_present, sdl_name};

Video *video_sdl_create(SDL_Window *window, bool vsync) {
    if (!window) return NULL;
    SdlVideo *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->base.ops = &sdl_ops;
    v->window = window;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    Uint32 flags = SDL_RENDERER_ACCELERATED;
    if (vsync) flags |= SDL_RENDERER_PRESENTVSYNC;
    v->renderer = SDL_CreateRenderer(window, -1, flags);
    if (!v->renderer) v->renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!v->renderer) {
        fprintf(stderr, "video: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        free(v);
        return NULL;
    }
    return &v->base;
}
