#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "audio_sdl.h"
#include "cyc_overlay.h"
#include "cyc_png.h"
#include "glue.h"
#include "video.h"
#include "widescreen.h"

typedef struct {
    char rom[4096];
    bool widescreen;
    bool fullscreen;
    bool muted;
    bool vsync;
    int scale;
    int renderer; /* 0 = SDL2, 1 = Vulkan */
    int aspect;   /* 0 = fit, 1 = integer, 2 = stretch */
    int scanlines;
    int brightness;
    int volume;
    int audio_rate;
    int slot;
    SDL_Scancode key_a;
    SDL_Scancode key_b;
    SDL_Scancode key_select;
    SDL_Scancode key_start;
    SDL_Scancode key_up;
    SDL_Scancode key_down;
    SDL_Scancode key_left;
    SDL_Scancode key_right;
} Config;

static Config g_cfg;
static char g_base_dir[4096];
static char g_config_path[4096];
static char g_save_dir[4096];
static SDL_Window *g_window;
static Video *g_video;
static AudioOut *g_audio;
static SDL_GameController *g_pad;
static uint32_t *g_present;
static size_t g_present_capacity;
static bool g_running;
static bool g_menu;
static int g_menu_index;
static int g_slot;
static char g_notice[96];
static uint32_t g_notice_until;
static char g_screenshot[4096];
static bool g_screenshot_done;
static uint64_t g_screenshot_frame = 1;
static int g_frame_w;
static int g_frame_h;

static const char *const MENU_ITEMS[] = {
    "RESUME", "SAVE STATE", "LOAD STATE", "WIDESCREEN", "FULLSCREEN", "QUIT"
};
enum { MENU_COUNT = (int)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0])) };

static void config_defaults(Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->scale = 3;
#if defined(MUSHROOM_HAVE_VULKAN) && MUSHROOM_HAVE_VULKAN
    cfg->renderer = 1;
#else
    cfg->renderer = 0;
#endif
    cfg->aspect = 0;
    cfg->scanlines = 0;
    cfg->brightness = 100;
    cfg->volume = 80;
    cfg->audio_rate = 48000;
    cfg->slot = 1;
    cfg->vsync = true;
    cfg->key_a = SDL_SCANCODE_X;
    cfg->key_b = SDL_SCANCODE_Z;
    cfg->key_select = SDL_SCANCODE_LSHIFT;
    cfg->key_start = SDL_SCANCODE_RETURN;
    cfg->key_up = SDL_SCANCODE_UP;
    cfg->key_down = SDL_SCANCODE_DOWN;
    cfg->key_left = SDL_SCANCODE_LEFT;
    cfg->key_right = SDL_SCANCODE_RIGHT;
}

static void parse_key(const char *value, SDL_Scancode *key) {
    SDL_Scancode parsed = SDL_GetScancodeFromName(value);
    if (parsed != SDL_SCANCODE_UNKNOWN) *key = parsed;
}

static const char *key_name(SDL_Scancode key) {
    const char *name = SDL_GetScancodeName(key);
    return name && *name ? name : "Unknown";
}

static void notify(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_notice, sizeof(g_notice), fmt, args);
    va_end(args);
    g_notice_until = SDL_GetTicks() + 2400;
}

static void make_dir(const char *path) {
#ifdef _WIN32
    wchar_t wide[4096];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 4096) > 0) _wmkdir(wide);
#else
    mkdir(path, 0755);
#endif
}

static void join_path(char *out, size_t cap, const char *dir, const char *name) {
    size_t n = strlen(dir);
    bool slash = n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\');
    snprintf(out, cap, "%s%s%s", dir, slash ? "" : "/", name);
}

static bool file_exists(const char *path) {
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    if (!rw) return false;
    SDL_RWclose(rw);
    return true;
}

static bool parse_bool(const char *value) {
    return !SDL_strcasecmp(value, "1") || !SDL_strcasecmp(value, "true") ||
           !SDL_strcasecmp(value, "on") || !SDL_strcasecmp(value, "yes");
}

static void trim(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
}

static void config_load(Config *cfg, const char *path) {
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    if (!rw) return;
    Sint64 size = SDL_RWsize(rw);
    if (size <= 0 || size > 1024 * 1024) {
        SDL_RWclose(rw);
        return;
    }
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        SDL_RWclose(rw);
        return;
    }
    size_t got = SDL_RWread(rw, buf, 1, (size_t)size);
    SDL_RWclose(rw);
    buf[got] = 0;
    for (char *line = buf; line && *line;) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        trim(line);
        if (*line && *line != '#' && *line != ';') {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                char *key = line;
                char *value = eq + 1;
                trim(key);
                trim(value);
                if (!SDL_strcasecmp(key, "rom")) snprintf(cfg->rom, sizeof(cfg->rom), "%s", value);
                else if (!SDL_strcasecmp(key, "widescreen")) cfg->widescreen = parse_bool(value);
                else if (!SDL_strcasecmp(key, "fullscreen")) cfg->fullscreen = parse_bool(value);
                else if (!SDL_strcasecmp(key, "muted")) cfg->muted = parse_bool(value);
                else if (!SDL_strcasecmp(key, "vsync")) cfg->vsync = parse_bool(value);
                else if (!SDL_strcasecmp(key, "scale")) cfg->scale = atoi(value);
                else if (!SDL_strcasecmp(key, "renderer"))
                    cfg->renderer = !SDL_strcasecmp(value, "sdl") ? 0 : 1;
                else if (!SDL_strcasecmp(key, "aspect")) {
                    if (!SDL_strcasecmp(value, "integer")) cfg->aspect = 1;
                    else if (!SDL_strcasecmp(value, "stretch")) cfg->aspect = 2;
                    else cfg->aspect = 0;
                } else if (!SDL_strcasecmp(key, "scanlines")) cfg->scanlines = atoi(value);
                else if (!SDL_strcasecmp(key, "brightness")) cfg->brightness = atoi(value);
                else if (!SDL_strcasecmp(key, "volume")) cfg->volume = atoi(value);
                else if (!SDL_strcasecmp(key, "audio_rate")) cfg->audio_rate = atoi(value);
                else if (!SDL_strcasecmp(key, "slot")) cfg->slot = atoi(value);
                else if (!SDL_strcasecmp(key, "key_a")) parse_key(value, &cfg->key_a);
                else if (!SDL_strcasecmp(key, "key_b")) parse_key(value, &cfg->key_b);
                else if (!SDL_strcasecmp(key, "key_select")) parse_key(value, &cfg->key_select);
                else if (!SDL_strcasecmp(key, "key_start")) parse_key(value, &cfg->key_start);
                else if (!SDL_strcasecmp(key, "key_up")) parse_key(value, &cfg->key_up);
                else if (!SDL_strcasecmp(key, "key_down")) parse_key(value, &cfg->key_down);
                else if (!SDL_strcasecmp(key, "key_left")) parse_key(value, &cfg->key_left);
                else if (!SDL_strcasecmp(key, "key_right")) parse_key(value, &cfg->key_right);
            }
        }
        line = next;
    }
    free(buf);
    if (cfg->scale < 1) cfg->scale = 1;
    if (cfg->scale > 8) cfg->scale = 8;
    if (cfg->scanlines < 0) cfg->scanlines = 0;
    if (cfg->scanlines > 2) cfg->scanlines = 2;
    if (cfg->brightness < 50) cfg->brightness = 50;
    if (cfg->brightness > 150) cfg->brightness = 150;
    if (cfg->volume < 0) cfg->volume = 0;
    if (cfg->volume > 100) cfg->volume = 100;
    if (cfg->audio_rate != 44100 && cfg->audio_rate != 48000 && cfg->audio_rate != 96000)
        cfg->audio_rate = 48000;
    if (cfg->slot < 1 || cfg->slot > 5) cfg->slot = 1;
}

static void config_save(const Config *cfg) {
    if (!g_config_path[0]) return;
    SDL_RWops *rw = SDL_RWFromFile(g_config_path, "wb");
    if (!rw) {
        fprintf(stderr, "config: cannot write %s: %s\n", g_config_path, SDL_GetError());
        return;
    }
    char key_a[32], key_b[32], key_select[32], key_start[32];
    char key_up[32], key_down[32], key_left[32], key_right[32];
    snprintf(key_a, sizeof(key_a), "%s", key_name(cfg->key_a));
    snprintf(key_b, sizeof(key_b), "%s", key_name(cfg->key_b));
    snprintf(key_select, sizeof(key_select), "%s", key_name(cfg->key_select));
    snprintf(key_start, sizeof(key_start), "%s", key_name(cfg->key_start));
    snprintf(key_up, sizeof(key_up), "%s", key_name(cfg->key_up));
    snprintf(key_down, sizeof(key_down), "%s", key_name(cfg->key_down));
    snprintf(key_left, sizeof(key_left), "%s", key_name(cfg->key_left));
    snprintf(key_right, sizeof(key_right), "%s", key_name(cfg->key_right));
    char text[8192];
    int n = snprintf(text, sizeof(text),
                     "rom=%s\n"
                     "widescreen=%d\n"
                     "fullscreen=%d\n"
                     "muted=%d\n"
                     "vsync=%d\n"
                     "scale=%d\n"
                     "renderer=%s\n"
                     "aspect=%s\n"
                     "scanlines=%d\n"
                     "brightness=%d\n"
                     "volume=%d\n"
                     "audio_rate=%d\n"
                     "slot=%d\n"
                     "key_a=%s\n"
                     "key_b=%s\n"
                     "key_select=%s\n"
                     "key_start=%s\n"
                     "key_up=%s\n"
                     "key_down=%s\n"
                     "key_left=%s\n"
                     "key_right=%s\n",
                     cfg->rom, cfg->widescreen ? 1 : 0, cfg->fullscreen ? 1 : 0,
                     cfg->muted ? 1 : 0, cfg->vsync ? 1 : 0, cfg->scale,
                     cfg->renderer ? "vulkan" : "sdl",
                     cfg->aspect == 1 ? "integer" : cfg->aspect == 2 ? "stretch" : "fit",
                     cfg->scanlines, cfg->brightness, cfg->volume, cfg->audio_rate, cfg->slot,
                     key_a, key_b, key_select, key_start, key_up, key_down, key_left, key_right);
    if (n > 0) SDL_RWwrite(rw, text, 1, (size_t)n);
    SDL_RWclose(rw);
}

static void state_path(char *out, size_t cap, int slot) {
    snprintf(out, cap, "%sstate-%d.sst", g_save_dir, slot);
}

static void save_slot(int slot) {
    char path[4096];
    state_path(path, sizeof(path), slot);
    if (glue_save_state_utf8(path)) notify("SAVED SLOT %d", slot);
    else notify("SAVE SLOT %d FAILED", slot);
}

static void load_slot(int slot) {
    char path[4096];
    state_path(path, sizeof(path), slot);
    if (!file_exists(path)) {
        notify("SLOT %d EMPTY", slot);
        return;
    }
    if (glue_load_state_utf8(path)) notify("LOADED SLOT %d", slot);
    else notify("LOAD SLOT %d FAILED", slot);
}

static void resize_window_to_frame(void) {
    if (!g_window || g_cfg.fullscreen) return;
    int fw = 256, fh = 240;
    (void)glue_get_present_framebuffer(&fw, &fh);
    SDL_SetWindowSize(g_window, fw * g_cfg.scale, fh * g_cfg.scale);
}

static void toggle_widescreen(void) {
    g_cfg.widescreen = !g_cfg.widescreen;
    glue_set_widescreen(g_cfg.widescreen);
    resize_window_to_frame();
    notify("WIDESCREEN %s", g_cfg.widescreen ? "ON" : "OFF");
    config_save(&g_cfg);
}

static void toggle_fullscreen(void) {
    g_cfg.fullscreen = !g_cfg.fullscreen;
    SDL_SetWindowFullscreen(g_window, g_cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (!g_cfg.fullscreen) resize_window_to_frame();
    notify("FULLSCREEN %s", g_cfg.fullscreen ? "ON" : "OFF");
    config_save(&g_cfg);
}

static void toggle_mute(void) {
    g_cfg.muted = !g_cfg.muted;
    audio_out_set_muted(g_audio, g_cfg.muted);
    notify("SOUND %s", g_cfg.muted ? "OFF" : "ON");
    config_save(&g_cfg);
}

static void adjust_volume(int delta) {
    int volume = g_cfg.volume + delta;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    g_cfg.volume = volume;
    audio_out_set_volume(g_audio, volume);
    notify("VOLUME %d", volume);
    config_save(&g_cfg);
}

static void open_menu(void) {
    g_menu = true;
    g_menu_index = 0;
    audio_out_pause(g_audio, true);
}

static void close_menu(void) {
    g_menu = false;
    audio_out_clear(g_audio);
    audio_out_pause(g_audio, false);
}

static void activate_menu(void) {
    switch (g_menu_index) {
        case 0: close_menu(); break;
        case 1: save_slot(g_slot); break;
        case 2: load_slot(g_slot); break;
        case 3: toggle_widescreen(); break;
        case 4: toggle_fullscreen(); break;
        case 5: g_running = false; break;
        default: break;
    }
}

static void set_slot(int slot) {
    if (slot < 1 || slot > 5) return;
    g_slot = slot;
    g_cfg.slot = slot;
    notify("SLOT %d", slot);
    config_save(&g_cfg);
}

static void handle_key(SDL_Keycode key, bool repeat) {
    if (key == SDLK_ESCAPE) {
        if (!repeat) {
            if (g_menu) close_menu();
            else open_menu();
        }
        return;
    }
    if (g_menu) {
        if (key == SDLK_UP || key == SDLK_w) {
            g_menu_index = (g_menu_index + MENU_COUNT - 1) % MENU_COUNT;
        } else if (key == SDLK_DOWN || key == SDLK_s) {
            g_menu_index = (g_menu_index + 1) % MENU_COUNT;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
            if (!repeat) activate_menu();
        } else if (key >= SDLK_1 && key <= SDLK_5) {
            if (!repeat) set_slot(key - SDLK_0);
        }
        return;
    }
    if (repeat) return;
    if (key == SDLK_F5) save_slot(g_slot);
    else if (key == SDLK_F7) load_slot(g_slot);
    else if (key == SDLK_F10) toggle_widescreen();
    else if (key == SDLK_F11) toggle_fullscreen();
    else if (key == SDLK_F8) toggle_mute();
    else if (key == SDLK_MINUS) adjust_volume(-5);
    else if (key == SDLK_EQUALS) adjust_volume(5);
    else if (key == SDLK_r) {
        glue_reset();
        audio_out_clear(g_audio);
        notify("RESET");
    } else if (key >= SDLK_1 && key <= SDLK_5) {
        set_slot(key - SDLK_0);
    }
}

static void open_controller(int index) {
    if (g_pad) SDL_GameControllerClose(g_pad);
    g_pad = SDL_GameControllerOpen(index);
    if (g_pad) {
        const char *name = SDL_GameControllerName(g_pad);
        notify("CONTROLLER %s", name && *name ? name : "CONNECTED");
    }
}

static void handle_event(SDL_Event *event) {
    switch (event->type) {
        case SDL_QUIT:
            g_running = false;
            break;
        case SDL_KEYDOWN:
            handle_key(event->key.keysym.sym, event->key.repeat != 0);
            break;
        case SDL_CONTROLLERDEVICEADDED:
            open_controller(event->cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (g_pad) {
                SDL_Joystick *joy = SDL_GameControllerGetJoystick(g_pad);
                if (joy && SDL_JoystickInstanceID(joy) == event->cdevice.which) {
                    SDL_GameControllerClose(g_pad);
                    g_pad = NULL;
                }
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (g_menu) {
                if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
                    g_menu_index = (g_menu_index + MENU_COUNT - 1) % MENU_COUNT;
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                    g_menu_index = (g_menu_index + 1) % MENU_COUNT;
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_A ||
                         event->cbutton.button == SDL_CONTROLLER_BUTTON_START)
                    activate_menu();
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_B)
                    close_menu();
            } else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
                open_menu();
            }
            break;
        case SDL_DROPFILE: {
            const char *path = event->drop.file;
            if (path && glue_load_rom_utf8(path)) {
                snprintf(g_cfg.rom, sizeof(g_cfg.rom), "%s", path);
                glue_set_widescreen(g_cfg.widescreen);
                audio_out_clear(g_audio);
                resize_window_to_frame();
                config_save(&g_cfg);
                notify("ROM LOADED");
            } else {
                notify("ROM LOAD FAILED");
            }
            SDL_free(event->drop.file);
            break;
        }
        default:
            break;
    }
}

static uint8_t read_pad(void) {
    uint8_t pad = 0;
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    if (keys[g_cfg.key_a]) pad |= NES_PAD_A;
    if (keys[g_cfg.key_b]) pad |= NES_PAD_B;
    if (keys[g_cfg.key_select] || keys[SDL_SCANCODE_RSHIFT]) pad |= NES_PAD_SELECT;
    if (keys[g_cfg.key_start] || keys[SDL_SCANCODE_KP_ENTER]) pad |= NES_PAD_START;
    if (keys[g_cfg.key_up]) pad |= NES_PAD_UP;
    if (keys[g_cfg.key_down]) pad |= NES_PAD_DOWN;
    if (keys[g_cfg.key_left]) pad |= NES_PAD_LEFT;
    if (keys[g_cfg.key_right]) pad |= NES_PAD_RIGHT;
    if (g_pad) {
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_A)) pad |= NES_PAD_A;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_B)) pad |= NES_PAD_B;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_X)) pad |= NES_PAD_B;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_Y)) pad |= NES_PAD_A;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_BACK)) pad |= NES_PAD_SELECT;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_START)) pad |= NES_PAD_START;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) pad |= NES_PAD_UP;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) pad |= NES_PAD_DOWN;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) pad |= NES_PAD_LEFT;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) pad |= NES_PAD_RIGHT;
        int ax = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX);
        int ay = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -12000) pad |= NES_PAD_LEFT;
        if (ax > 12000) pad |= NES_PAD_RIGHT;
        if (ay < -12000) pad |= NES_PAD_UP;
        if (ay > 12000) pad |= NES_PAD_DOWN;
    }
    return pad;
}

static void fill_rect(uint32_t *dst, int width, int height, int x, int y,
                      int w, int h, uint32_t color, unsigned alpha) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            uint32_t *p = &dst[(size_t)yy * width + xx];
            if (alpha >= 255) {
                *p = 0xff000000u | color;
                continue;
            }
            uint32_t old = *p;
            uint32_t out = 0;
            for (int shift = 0; shift < 24; shift += 8) {
                uint32_t dc = (old >> shift) & 255;
                uint32_t sc = (color >> shift) & 255;
                out |= ((sc * alpha + dc * (255 - alpha)) / 255) << shift;
            }
            *p = 0xff000000u | out;
        }
    }
}

static void draw_menu(uint32_t *dst, int width, int height) {
    int panel_w = width < 220 ? width - 8 : 220;
    int panel_h = 132;
    int x = (width - panel_w) / 2;
    int y = (height - panel_h) / 2;
    fill_rect(dst, width, height, x, y, panel_w, panel_h, 0x181820, 238);
    fill_rect(dst, width, height, x, y, panel_w, 1, 0xffd25a, 255);
    fill_rect(dst, width, height, x, y + panel_h - 1, panel_w, 1, 0xffd25a, 255);
    fill_rect(dst, width, height, x, y, 1, panel_h, 0xffd25a, 255);
    fill_rect(dst, width, height, x + panel_w - 1, y, 1, panel_h, 0xffd25a, 255);

    cyc_overlay_text(dst, width, height, x + 10, y + 8, 2, 0xffd25a, "PAUSED");
    char label[48];
    for (int i = 0; i < MENU_COUNT; i++) {
        if (i == 3) snprintf(label, sizeof(label), "WIDESCREEN %s", g_cfg.widescreen ? "ON" : "OFF");
        else if (i == 4) snprintf(label, sizeof(label), "FULLSCREEN %s", g_cfg.fullscreen ? "ON" : "OFF");
        else snprintf(label, sizeof(label), "%s", MENU_ITEMS[i]);
        int item_y = y + 26 + i * 14;
        if (i == g_menu_index)
            fill_rect(dst, width, height, x + 6, item_y - 2, panel_w - 12, 12, 0x3a2a10, 255);
        cyc_overlay_text(dst, width, height, x + 12, item_y, 2,
                         i == g_menu_index ? 0xffd25a : 0xe6e6e6, label);
    }
    snprintf(label, sizeof(label), "SLOT %d", g_slot);
    cyc_overlay_text(dst, width, height, x + 10, y + panel_h - 22, 1, 0xe6e6e6, label);
    cyc_overlay_text(dst, width, height, x + 10, y + panel_h - 12, 1, 0xaaaaaa,
                     "1-5 SLOT  F5 SAVE  F7 LOAD");
    cyc_overlay_text(dst, width, height, x + panel_w - 10 - (int)strlen(video_name(g_video)) * 4,
                     y + panel_h - 12, 1, 0xaaaaaa, video_name(g_video));
}

static bool present_frame(void) {
    int width = 256, height = 240;
    const uint32_t *framebuffer = glue_get_present_framebuffer(&width, &height);
    if (!framebuffer || width <= 0 || height <= 0) return false;
    size_t pixels = (size_t)width * (size_t)height;
    if (pixels > g_present_capacity) {
        uint32_t *grown = realloc(g_present, pixels * sizeof(*grown));
        if (!grown) return false;
        g_present = grown;
        g_present_capacity = pixels;
    }
    memcpy(g_present, framebuffer, pixels * sizeof(*g_present));
    if (g_menu) draw_menu(g_present, width, height);
    if (g_notice[0] && SDL_GetTicks() < g_notice_until)
        cyc_overlay_toast(g_present, width, height, g_notice, NULL);

    g_frame_w = width;
    g_frame_h = height;
    VideoOptions options;
    options.aspect = (VideoAspect)g_cfg.aspect;
    options.brightness = g_cfg.brightness;
    options.scanlines = g_cfg.scanlines;
    return video_present(g_video, g_present, width, height, &options);
}

static bool init_video(int fw, int fh) {
    bool want_vulkan = g_cfg.renderer != 0;
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (want_vulkan) flags |= SDL_WINDOW_VULKAN;
    g_window = SDL_CreateWindow("Mushroom Recompiled", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, fw * g_cfg.scale,
                                fh * g_cfg.scale, flags);
    if (!g_window && want_vulkan) {
        fprintf(stderr, "video: Vulkan window failed, falling back to SDL2: %s\n", SDL_GetError());
        want_vulkan = false;
        g_cfg.renderer = 0;
        g_window = SDL_CreateWindow("Mushroom Recompiled", SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED, fw * g_cfg.scale,
                                    fh * g_cfg.scale, flags & ~SDL_WINDOW_VULKAN);
    }
    if (!g_window) {
        fprintf(stderr, "video: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(g_window, 320, 240);
    g_video = video_create(g_window, want_vulkan ? VIDEO_BACKEND_VULKAN : VIDEO_BACKEND_SDL,
                           g_cfg.vsync);
    if (!g_video && want_vulkan) {
        fprintf(stderr, "video: Vulkan unavailable, falling back to SDL2\n");
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        g_cfg.renderer = 0;
        g_window = SDL_CreateWindow("Mushroom Recompiled", SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED, fw * g_cfg.scale,
                                    fh * g_cfg.scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (g_window) {
            SDL_SetWindowMinimumSize(g_window, 320, 240);
            g_video = video_create(g_window, VIDEO_BACKEND_SDL, g_cfg.vsync);
        }
        if (g_video) notify("VULKAN UNAVAILABLE - USING SDL2");
    }
    if (!g_video) {
        fprintf(stderr, "video: no renderer available\n");
        return false;
    }
    char title[128];
    snprintf(title, sizeof(title), "Mushroom Recompiled - %s", video_name(g_video));
    SDL_SetWindowTitle(g_window, title);
    if (g_cfg.fullscreen)
        SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    return true;
}

static void usage(const char *argv0) {
    printf("Usage: %s --rom GAME.NES [options]\n"
           "  --renderer vulkan|sdl   presentation backend (default vulkan with SDL2 fallback)\n"
           "  --scale N               window scale 1-8 (default 3)\n"
           "  --fullscreen            start fullscreen\n"
           "  --widescreen            start with the 426x240 widescreen view\n"
           "  --aspect fit|integer|stretch\n"
           "  --scanlines 0|1|2       CRT scanline strength\n"
           "  --brightness 50-150     percent\n"
           "  --audio-rate 44100|48000|96000\n"
           "  --volume 0-100          --mute\n"
           "  --slot 1-5              active save slot\n"
           "  --frames N              run N frames then exit (smoke test)\n"
           "  --menu                  start paused in the in-game menu\n"
           "  --screenshot PATH       write a presented frame as PNG\n"
           "  --screenshot-frame N    frame to capture (default 1)\n"
           "  --no-vsync              disable vsync\n"
           "  --help                  this text\n"
           "The ROM can also be stored as rom=... in %s.\n",
           argv0, g_config_path[0] ? g_config_path : "config.ini");
}

static bool parse_args(Config *cfg, int *frame_limit, bool *help, int argc, char **argv) {
    *frame_limit = 0;
    *help = false;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            *help = true;
        } else if (!strcmp(arg, "--rom") && i + 1 < argc) {
            snprintf(cfg->rom, sizeof(cfg->rom), "%s", argv[++i]);
        } else if (!strcmp(arg, "--scale") && i + 1 < argc) {
            cfg->scale = atoi(argv[++i]);
        } else if (!strcmp(arg, "--renderer") && i + 1 < argc) {
            cfg->renderer = !SDL_strcasecmp(argv[++i], "sdl") ? 0 : 1;
        } else if (!strcmp(arg, "--sdl")) {
            cfg->renderer = 0;
        } else if (!strcmp(arg, "--vulkan")) {
            cfg->renderer = 1;
        } else if (!strcmp(arg, "--fullscreen")) {
            cfg->fullscreen = true;
        } else if (!strcmp(arg, "--widescreen")) {
            cfg->widescreen = true;
        } else if (!strcmp(arg, "--no-widescreen")) {
            cfg->widescreen = false;
        } else if (!strcmp(arg, "--aspect") && i + 1 < argc) {
            const char *value = argv[++i];
            cfg->aspect = !SDL_strcasecmp(value, "integer") ? 1 :
                          !SDL_strcasecmp(value, "stretch") ? 2 : 0;
        } else if (!strcmp(arg, "--scanlines") && i + 1 < argc) {
            cfg->scanlines = atoi(argv[++i]);
        } else if (!strcmp(arg, "--brightness") && i + 1 < argc) {
            cfg->brightness = atoi(argv[++i]);
        } else if (!strcmp(arg, "--audio-rate") && i + 1 < argc) {
            cfg->audio_rate = atoi(argv[++i]);
        } else if (!strcmp(arg, "--volume") && i + 1 < argc) {
            cfg->volume = atoi(argv[++i]);
        } else if (!strcmp(arg, "--mute")) {
            cfg->muted = true;
        } else if (!strcmp(arg, "--slot") && i + 1 < argc) {
            cfg->slot = atoi(argv[++i]);
        } else if (!strcmp(arg, "--frames") && i + 1 < argc) {
            *frame_limit = atoi(argv[++i]);
        } else if (!strcmp(arg, "--menu")) {
            g_menu = true;
        } else if (!strcmp(arg, "--screenshot") && i + 1 < argc) {
            snprintf(g_screenshot, sizeof(g_screenshot), "%s", argv[++i]);
        } else if (!strcmp(arg, "--screenshot-frame") && i + 1 < argc) {
            int n = atoi(argv[++i]);
            g_screenshot_frame = n > 0 ? (uint64_t)n : 1;
        } else if (!strcmp(arg, "--no-vsync")) {
            cfg->vsync = false;
        } else if (!strcmp(arg, "--vsync")) {
            cfg->vsync = true;
        } else if (arg[0] != '-' && cfg->rom[0] == 0) {
            snprintf(cfg->rom, sizeof(cfg->rom), "%s", arg);
        } else {
            fprintf(stderr, "unknown option: %s\n", arg);
            return false;
        }
    }
    if (cfg->scale < 1) cfg->scale = 1;
    if (cfg->scale > 8) cfg->scale = 8;
    if (cfg->scanlines < 0) cfg->scanlines = 0;
    if (cfg->scanlines > 2) cfg->scanlines = 2;
    if (cfg->brightness < 50) cfg->brightness = 50;
    if (cfg->brightness > 150) cfg->brightness = 150;
    if (cfg->volume < 0) cfg->volume = 0;
    if (cfg->volume > 100) cfg->volume = 100;
    if (cfg->audio_rate != 44100 && cfg->audio_rate != 48000 && cfg->audio_rate != 96000)
        cfg->audio_rate = 48000;
    if (cfg->slot < 1 || cfg->slot > 5) cfg->slot = 1;
    if (*frame_limit < 0) *frame_limit = 0;
    return true;
}

static void init_paths(void) {
    const char *base = SDL_GetBasePath();
    if (base) {
        snprintf(g_base_dir, sizeof(g_base_dir), "%s", base);
        SDL_free((void *)base);
    }
    char *pref = SDL_GetPrefPath("MushroomRecompiled", "MushroomRecompiled");
    if (!pref) {
        const char *fallback = SDL_GetBasePath();
        const char *dir = fallback ? fallback : ".";
        char saves[4096];
        bool slash = dir[0] && (dir[strlen(dir) - 1] == '/' || dir[strlen(dir) - 1] == '\\');
        snprintf(g_config_path, sizeof(g_config_path), "%s%sconfig.ini", dir, slash ? "" : "/");
        snprintf(saves, sizeof(saves), "%s%ssaves", dir, slash ? "" : "/");
        snprintf(g_save_dir, sizeof(g_save_dir), "%s%ssaves/", dir, slash ? "" : "/");
        make_dir(saves);
        SDL_free((void *)fallback);
        return;
    }
    snprintf(g_config_path, sizeof(g_config_path), "%sconfig.ini", pref);
    snprintf(g_save_dir, sizeof(g_save_dir), "%ssaves/", pref);
    char dir[4096];
    snprintf(dir, sizeof(dir), "%ssaves", pref);
    make_dir(dir);
    SDL_free(pref);
}

int main(int argc, char **argv) {
    SDL_SetMainReady();
    config_defaults(&g_cfg);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_GameControllerEventState(SDL_ENABLE);
    init_paths();
    config_load(&g_cfg, g_config_path);

    int frame_limit = 0;
    bool help = false;
    if (!parse_args(&g_cfg, &frame_limit, &help, argc, argv)) {
        usage(argv[0]);
        SDL_Quit();
        return 2;
    }
    if (help) {
        usage(argv[0]);
        SDL_Quit();
        return 0;
    }
    g_slot = g_cfg.slot;

    if (!g_cfg.rom[0]) {
        fprintf(stderr, "No ROM selected. Use --rom GAME.NES or set rom=... in %s\n", g_config_path);
        SDL_Quit();
        return 2;
    }

    g_audio = audio_out_create(g_cfg.audio_rate, g_cfg.volume, g_cfg.muted);
    if (g_audio) {
        g_cfg.audio_rate = audio_out_sample_rate(g_audio);
    } else {
        fprintf(stderr, "audio: continuing without sound\n");
    }
    glue_set_audio_sample_rate((uint32_t)g_cfg.audio_rate);
    if (!glue_load_rom_utf8(g_cfg.rom)) {
        fprintf(stderr, "ROM rejected: %s\n", g_cfg.rom);
        audio_out_destroy(g_audio);
        SDL_Quit();
        return 1;
    }
    glue_set_widescreen(g_cfg.widescreen);

    if (g_base_dir[0]) {
        char mapping[4096];
        join_path(mapping, sizeof(mapping), g_base_dir, "assets/gamecontrollerdb.txt");
        if (file_exists(mapping)) SDL_GameControllerAddMappingsFromFile(mapping);
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            open_controller(i);
            break;
        }
    }

    int fw = 256, fh = 240;
    (void)glue_get_present_framebuffer(&fw, &fh);
    if (!init_video(fw, fh)) {
        audio_out_destroy(g_audio);
        SDL_Quit();
        return 1;
    }

    if (g_menu) open_menu();
    g_running = true;
    uint64_t iterations = 0;
    Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 next_tick = SDL_GetPerformanceCounter();
    const double frame_rate = glue_frame_rate();
    const double frame_seconds = frame_rate > 1.0 ? 1.0 / frame_rate : 1.0 / 60.0;
    int16_t audio_buffer[4096];

    while (g_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) handle_event(&event);

        if (!g_menu) {
            glue_set_pad1(read_pad());
            glue_step_frame();
            size_t samples = glue_get_audio_samples(audio_buffer, 4096, 1);
            if (samples) audio_out_push(g_audio, audio_buffer, samples);
        }
        if (!present_frame()) {
            fprintf(stderr, "video: presentation failed\n");
            g_running = false;
        }
        iterations++;
        if (g_screenshot[0] && !g_screenshot_done && iterations >= g_screenshot_frame) {
            if (cyc_write_png(g_screenshot, g_present, g_frame_w, g_frame_h))
                g_screenshot_done = true;
        }
        if (frame_limit > 0 && iterations >= (uint64_t)frame_limit) g_running = false;

        next_tick += (Uint64)(frequency * frame_seconds);
        Uint64 now = SDL_GetPerformanceCounter();
        if (next_tick > now) {
            Uint64 delay_ms = (next_tick - now) * 1000u / frequency;
            if (delay_ms > 0) SDL_Delay((Uint32)delay_ms);
        } else if (now - next_tick > frequency / 30u) {
            next_tick = now;
        }
    }

    config_save(&g_cfg);
    audio_out_destroy(g_audio);
    if (g_pad) SDL_GameControllerClose(g_pad);
    video_destroy(g_video);
    if (g_window) SDL_DestroyWindow(g_window);
    free(g_present);
    SDL_Quit();
    return 0;
}
