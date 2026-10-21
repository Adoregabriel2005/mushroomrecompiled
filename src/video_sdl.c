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

    VideoAspect aspect = options ? options->aspect : VIDEO_ASPECT_FIT;
    int brightness = options ? options->brightness : 100;
    int scanlines = options ? options->scanlines : 0;
    if (brightness < 0) brightness = 0;
    if (brightness > 200) brightness = 200;
    Uint8 mod = (Uint8)((brightness * 255 + 50) / 100);
    SDL_SetTextureColorMod(v->texture, mod, mod, mod);

    SDL_SetRenderDrawColor(v->renderer, 0, 0, 0, 255);
    SDL_RenderClear(v->renderer);
    int x, y, dw, dh;
    video_dest_rect(win_w, win_h, width, height, aspect, &x, &y, &dw, &dh);
    SDL_Rect dst = {x, y, dw, dh};
    if (SDL_RenderCopy(v->renderer, v->texture, NULL, &dst) != 0) {
        fprintf(stderr, "video: SDL_RenderCopy failed: %s\n", SDL_GetError());
        return false;
    }

    if (scanlines > 0 && dh > 2) {
        int alpha = scanlines >= 2 ? 112 : 56;
        SDL_SetRenderDrawColor(v->renderer, 0, 0, 0, (Uint8)alpha);
        for (int row = y + 1; row < y + dh; row += 2) {
            SDL_RenderDrawLine(v->renderer, x, row, x + dw - 1, row);
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
    SDL_SetRenderDrawBlendMode(v->renderer, SDL_BLENDMODE_BLEND);
    return &v->base;
}
