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
#ifndef _WIN32
#include <dirent.h>
#endif
#include "glue.h"
#include "overlay.h"
#include "video.h"
#include "widescreen.h"

typedef struct {
    char rom[4096];
    bool fullscreen;
    bool widescreen;
    bool show_fps;
    bool vsync;
    int scale;
    int scanlines;
    int brightness;
    int volume;
    int audio_rate;
    int slot;
    char replay[4096];
    char record[4096];
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
static char g_sram_path[4096];
static bool g_sram_ok = true;
static int g_sram_frames = 0;
static uint8_t *g_session;
static size_t g_session_len, g_session_cap;
static uint8_t *g_replay;
static size_t g_replay_len, g_replay_pos;
static FILE *g_record_fp;
static SDL_Window *g_window;
static Video *g_video;
static AudioOut *g_audio;
static SDL_GameController *g_pad;
static uint32_t *g_present;
static size_t g_present_capacity;
static const uint8_t *g_chr;
static size_t g_chr_len;
static bool g_running;
static bool g_menu;
static int g_menu_index;
static int g_slot;
static char g_notice[96];
static uint32_t g_notice_until;
/* Learned SDL_Delay overshoot in milliseconds. Every sleep runs slightly
 * long; the pacer subtracts this so the average frame lands on the tick. */
static double g_sleep_bias_ms = 0.0;
/* Audio resample ratio. The queue controller only leaves unity when the
 * cushion drifts well outside its target, and moves it slowly so a stretch
 * never becomes audible pitch wobble. */
static double g_audio_rate_scale = 1.0;
/* Emulated frames per second, updated every half second. */
static int g_fps_shown;
static int g_fps_frames;
static Uint64 g_fps_t0;
static char g_screenshot[4096];
static bool g_screenshot_done;
static uint64_t g_screenshot_frame = 1;
static int g_frame_w;
static int g_frame_h;
/* Fullscreen is requested after the window is mapped: on Wayland a request
 * made before the compositor knows the surface is silently dropped. */
static bool g_fullscreen_pending;
static Uint32 g_window_shown_at;

/* Pause-menu tabs (see overlay.h) and the row being recorded by the
 * CONTROLS capture mode, or -1. */
static int g_menu_tab;
static int g_capture = -1;

static int menu_row_count(void) {
    static const int counts[TAB_COUNT] = {4, 1, 3, 8, 2};
    if (g_menu_tab < 0 || g_menu_tab >= TAB_COUNT) return 0;
    return counts[g_menu_tab];
}

static void set_tab(int tab) {
    g_menu_tab = (tab % TAB_COUNT + TAB_COUNT) % TAB_COUNT;
    g_menu_index = 0;
    g_capture = -1;
}

static void notify(const char *fmt, ...);
static void config_save(const Config *cfg);
static const char *key_name(SDL_Scancode key);

/* CONTROLS rows map to the keyboard bindings: A, B, SELECT, START,
 * UP, DOWN, LEFT, RIGHT. */
static SDL_Scancode *binding_for(int row) {
    switch (row) {
        case 0: return &g_cfg.key_a;
        case 1: return &g_cfg.key_b;
        case 2: return &g_cfg.key_select;
        case 3: return &g_cfg.key_start;
        case 4: return &g_cfg.key_up;
        case 5: return &g_cfg.key_down;
        case 6: return &g_cfg.key_left;
        case 7: return &g_cfg.key_right;
        default: return NULL;
    }
}

/* Records a key for a CONTROLS row. If the key is already bound elsewhere
 * the two bindings swap, so swapping A and B is a single capture. */
static void bind_key(int row, SDL_Keycode key) {
    SDL_Scancode *target = binding_for(row);
    if (!target) return;
    SDL_Scancode sc = SDL_GetScancodeFromKey(key);
    if (sc == SDL_SCANCODE_UNKNOWN) {
        notify("KEY NOT USABLE");
        return;
    }
    SDL_Scancode old = *target;
    for (int i = 0; i < 8; i++) {
        SDL_Scancode *other = binding_for(i);
        if (other && i != row && *other == sc) *other = old;
    }
    *target = sc;
    notify("%s IS %s", row == 0 ? "A" : row == 1 ? "B" : row == 2 ? "SELECT" :
            row == 3 ? "START" : row == 4 ? "UP" : row == 5 ? "DOWN" :
            row == 6 ? "LEFT" : "RIGHT", key_name(sc));
    config_save(&g_cfg);
}

static void config_defaults(Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->scale = 3;
    cfg->scanlines = 0;
    cfg->brightness = 100;
    cfg->volume = 100; /* the engine has no volume control: always full level */
    cfg->audio_rate = 48000;
    cfg->slot = 1;
    cfg->vsync = true;
    cfg->widescreen = true;
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

/* ROM picker: candidates are *.nes files beside the app and in the working
 * directory, so a first launch with no remembered ROM just works. */
static void open_controller(int index);
static void config_save(const Config *cfg);
static bool init_video(int fw, int fh);
#define PICK_MAX 64
#define PICK_ROWS 12

static bool pick_is_nes(const char *name) {
    size_t n = strlen(name);
    return n > 4 && !SDL_strcasecmp(name + n - 4, ".nes");
}

static void pick_add(char files[][4096], int *count, const char *dir, const char *name) {
    char path[4096];
    join_path(path, sizeof(path), dir, name);
    for (int i = 0; i < *count; i++)
        if (!SDL_strcasecmp(files[i], path)) return;
    if (*count >= PICK_MAX) return;
    snprintf(files[*count], 4096, "%s", path);
    (*count)++;
}

static int pick_cmp(const void *a, const void *b) {
    return SDL_strcasecmp((const char *)a, (const char *)b);
}

static void pick_scan_one(char files[][4096], int *count, const char *dir) {
    if (!dir || !dir[0] || *count >= PICK_MAX) return;
#ifdef _WIN32
    char pat[4096];
    size_t dn = strlen(dir);
    bool slash = dn > 0 && (dir[dn - 1] == '/' || dir[dn - 1] == '\\');
    snprintf(pat, sizeof(pat), "%s%s*.nes", dir, slash ? "" : "/");
    wchar_t wpat[4096];
    if (MultiByteToWideChar(CP_UTF8, 0, pat, -1, wpat, 4096) <= 0) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (fd.cFileName[0] == L'.') continue;
        char name[4096];
        if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof(name),
                                NULL, NULL) > 0)
            pick_add(files, count, dir, name);
    } while (*count < PICK_MAX && FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while (*count < PICK_MAX && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (pick_is_nes(e->d_name)) pick_add(files, count, dir, e->d_name);
    }
    closedir(d);
#endif
}

/* Parent of dir (trailing separators tolerated); empty when there is none. */
static void parent_dir(char *out, size_t cap, const char *dir) {
    snprintf(out, cap, "%s", dir ? dir : "");
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == '/' || out[n - 1] == '\\')) out[--n] = 0;
    char *sep = strrchr(out, '/');
    char *wsep = strrchr(out, '\\');
    if (wsep && (!sep || wsep > sep)) sep = wsep;
    if (sep) *sep = 0;
    else out[0] = 0;
}

static int pick_scan(char files[][4096]) {
    int count = 0;
    /* The binary often sits nested (build/native/), two levels below the
     * project roms/ folder, so also walk up from the app directory. */
    char up1[4096], up2[4096];
    parent_dir(up1, sizeof(up1), g_base_dir);
    parent_dir(up2, sizeof(up2), up1);
    const char *roots[4] = { g_base_dir, up1, up2, "." };
    for (int r = 0; r < 4; r++) {
        if (!roots[r] || !roots[r][0]) continue;
        pick_scan_one(files, &count, roots[r]);
        char sub[4096];
        join_path(sub, sizeof(sub), roots[r], "roms");
        pick_scan_one(files, &count, sub);
    }
    qsort(files, (size_t)count, sizeof(files[0]), pick_cmp);
    return count;
}

static const char *pick_base(const char *path) {
    const char *b = strrchr(path, '/');
    const char *w = strrchr(path, '\\');
    if (w && (!b || w > b)) b = w;
    return b ? b + 1 : path;
}

static bool pick_ensure(int w, int h) {
    size_t pixels = (size_t)w * (size_t)h;
    if (pixels > g_present_capacity) {
        uint32_t *grown = realloc(g_present, pixels * sizeof(*grown));
        if (!grown) return false;
        g_present = grown;
        g_present_capacity = pixels;
    }
    return true;
}

static void pick_draw(char files[][4096], int n, int sel, const char *error) {
    const int w = 256, h = 240;
    if (!pick_ensure(w, h)) return;
    for (int i = 0; i < w * h; i++) g_present[i] = 0xFF101014u;
    cyc_overlay_text(g_present, w, h, (w - 10 * 8) / 2, 24, 2, 0xFFD25A, "SELECT ROM");
    if (!n) {
        cyc_overlay_text(g_present, w, h, 20, 80, 1, 0xE6E6E6, "NO .NES FILES FOUND");
        cyc_overlay_text(g_present, w, h, 20, 92, 1, 0xE6E6E6, "PUT ONE BESIDE THE APP");
        cyc_overlay_text(g_present, w, h, 20, 104, 1, 0xE6E6E6, "OR DRAG IT HERE");
    } else {
        int top = sel - PICK_ROWS / 2;
        if (top < 0) top = 0;
        if (top > n - PICK_ROWS) top = n - PICK_ROWS;
        if (top < 0) top = 0;
        for (int r = 0; r < PICK_ROWS && top + r < n; r++) {
            char row[64];
            snprintf(row, sizeof(row), "%s %-29.29s", top + r == sel ? ">" : " ",
                     pick_base(files[top + r]));
            cyc_overlay_text(g_present, w, h, 16, 64 + r * 12, 1,
                             top + r == sel ? 0xFFD25A : 0xE6E6E6, row);
        }
    }
    if (error && *error)
        cyc_overlay_text(g_present, w, h, 16, 208, 1, 0xFF6B5E, error);
    else
        cyc_overlay_text(g_present, w, h, 16, 208, 1, 0x8A8A92,
                         "UP/DOWN CHOOSE  ENTER LAUNCH");
    g_frame_w = w;
    g_frame_h = h;
    VideoOptions o;
    memset(&o, 0, sizeof(o));
    video_present(g_video, g_present, w, h, &o);
}

/* A lone nearby ROM boots straight in, so dropping one .nes into roms/
 * is enough for a flagless double-click launch. Zero or several ROMs
 * still bring up the picker. */
/* Remembered ROMs stay valid no matter which folder the app is started
 * from: the picker scans with relative roots, so absolutize on store. */
static void store_rom_path(const char *path) {
#ifdef _WIN32
    char abs[4096];
    if (GetFullPathNameA(path, (DWORD)sizeof(abs), abs, NULL) && abs[0])
        snprintf(g_cfg.rom, sizeof(g_cfg.rom), "%s", abs);
    else
        snprintf(g_cfg.rom, sizeof(g_cfg.rom), "%s", path);
#else
    char abs[4096];
    if (realpath(path, abs))
        snprintf(g_cfg.rom, sizeof(g_cfg.rom), "%s", abs);
    else
        snprintf(g_cfg.rom, sizeof(g_cfg.rom), "%s", path);
#endif
}

static bool pick_autoload(void) {
    static char files[PICK_MAX][4096];
    if (pick_scan(files) != 1) return false;
    if (!glue_load_rom_utf8(files[0])) return false;
    store_rom_path(files[0]);
    config_save(&g_cfg);
    return true;
}

static bool run_picker(void) {
    static char files[PICK_MAX][4096];
    int n = pick_scan(files);
    int sel = 0;
    char error[96] = "";
    for (;;) {
        pick_draw(files, n, sel, error);
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    return false;
                case SDL_CONTROLLERDEVICEADDED:
                    open_controller(ev.cdevice.which);
                    break;
                case SDL_DROPFILE: {
                    bool ok = ev.drop.file && glue_load_rom_utf8(ev.drop.file);
                    if (ev.drop.file) {
                        if (ok) store_rom_path(ev.drop.file);
                        SDL_free(ev.drop.file);
                    }
                    if (ok) {
                        config_save(&g_cfg);
                        return true;
                    }
                    snprintf(error, sizeof(error), "ROM REJECTED");
                    break;
                }
                case SDL_KEYDOWN: {
                    SDL_Keycode k = ev.key.keysym.sym;
                    bool rep = ev.key.repeat != 0;
                    if (k == SDLK_ESCAPE && !rep) return false;
                    if (n > 0 && (k == SDLK_UP || k == SDLK_w)) {
                        sel = (sel + n - 1) % n;
                        error[0] = 0;
                    } else if (n > 0 && (k == SDLK_DOWN || k == SDLK_s)) {
                        sel = (sel + 1) % n;
                        error[0] = 0;
                    } else if (!rep && n > 0 &&
                               (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE)) {
                        if (glue_load_rom_utf8(files[sel])) {
                            store_rom_path(files[sel]);
                            config_save(&g_cfg);
                            return true;
                        }
                        snprintf(error, sizeof(error), "ROM REJECTED");
                    }
                    break;
                }
                case SDL_CONTROLLERBUTTONDOWN: {
                    Uint8 b = ev.cbutton.button;
                    if (n > 0 && b == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        sel = (sel + n - 1) % n;
                        error[0] = 0;
                    } else if (n > 0 && b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        sel = (sel + 1) % n;
                        error[0] = 0;
                    } else if (!n) {
                        if (b == SDL_CONTROLLER_BUTTON_B) return false;
                    } else if (b == SDL_CONTROLLER_BUTTON_A ||
                               b == SDL_CONTROLLER_BUTTON_START) {
                        if (glue_load_rom_utf8(files[sel])) {
                            store_rom_path(files[sel]);
                            config_save(&g_cfg);
                            return true;
                        }
                        snprintf(error, sizeof(error), "ROM REJECTED");
                    } else if (b == SDL_CONTROLLER_BUTTON_B) {
                        return false;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        SDL_Delay(16);
    }
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
                else if (!SDL_strcasecmp(key, "fullscreen")) cfg->fullscreen = parse_bool(value);
                else if (!SDL_strcasecmp(key, "show_fps")) cfg->show_fps = parse_bool(value);
                else if (!SDL_strcasecmp(key, "widescreen")) cfg->widescreen = parse_bool(value);
                else if (!SDL_strcasecmp(key, "vsync")) cfg->vsync = parse_bool(value);
                else if (!SDL_strcasecmp(key, "scale")) cfg->scale = atoi(value);
                else if (!SDL_strcasecmp(key, "scanlines")) cfg->scanlines = atoi(value);
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
                     "fullscreen=%d\n"
                     "widescreen=%d\n"
                     "show_fps=%d\n"
                     "vsync=%d\n"
                     "scale=%d\n"
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
                     cfg->rom, cfg->fullscreen ? 1 : 0,
                     cfg->widescreen ? 1 : 0,
                     cfg->show_fps ? 1 : 0, cfg->vsync ? 1 : 0, cfg->scale,
                     cfg->scanlines, cfg->brightness, cfg->volume, cfg->audio_rate, cfg->slot,
                     key_a, key_b, key_select, key_start, key_up, key_down, key_left, key_right);
    if (n > 0) SDL_RWwrite(rw, text, 1, (size_t)n);
    SDL_RWclose(rw);
}

static void state_path(char *out, size_t cap, int slot) {
    snprintf(out, cap, "%sstate-%d.sst", g_save_dir, slot);
}

static void save_slot(int slot) {
    if (g_replay) {
        notify("STATES OFF IN REPLAY");
        return;
    }
    char path[4096];
    state_path(path, sizeof(path), slot);
    if (glue_save_state_utf8(path)) notify("SAVED SLOT %d", slot);
    else notify("SAVE SLOT %d FAILED", slot);
}

static void load_slot(int slot) {
    if (g_replay) {
        notify("STATES OFF IN REPLAY");
        return;
    }
    char path[4096];
    state_path(path, sizeof(path), slot);
    if (!file_exists(path)) {
        notify("SLOT %d EMPTY", slot);
        return;
    }
    if (glue_load_state_utf8(path)) notify("LOADED SLOT %d", slot);
    else notify("LOAD SLOT %d FAILED", slot);
}

/* Session replay: one raw controller byte per emulated frame.
 * Paused menu frames never step the machine, so they are never recorded.
 * Replays start at boot and never touch the real save file; save states are
 * refused while one plays. */
static void session_push(uint8_t pad) {
    if (g_session_len < g_session_cap) {
        g_session[g_session_len++] = pad;
        return;
    }
    size_t next = g_session_cap ? g_session_cap * 2 : 4096;
    if (next > 8 * 1024 * 1024) return;
    uint8_t *grown = realloc(g_session, next);
    if (!grown) return;
    g_session = grown;
    g_session_cap = next;
    g_session[g_session_len++] = pad;
}

static void save_replay_file(void) {
    char path[4096];
    int n = 1;
    for (;; n++) {
        snprintf(path, sizeof(path), "%sreplay-%d.rec", g_save_dir, n);
        if (!file_exists(path)) break;
    }
    FILE *f = fopen(path, "wb");
    bool ok = f && (g_session_len == 0 ||
                    fwrite(g_session, 1, g_session_len, f) == g_session_len);
    if (f) fclose(f);
    if (ok) notify("REPLAY %d SAVED", n);
    else {
        remove(path);
        notify("REPLAY SAVE FAILED");
    }
}

/* The window follows the live picture: the 426x240 widescreen canvas or the
 * 256x240 native frame, at an exact integer multiple so every source pixel
 * covers the same number of window pixels and nothing is letterboxed.
 * While the menu is open the window keeps the wide canvas even in 4:3:
 * the menu is drawn full-size over a pillarboxed frame. */
static void resize_window_to_frame(void) {
    if (!g_window || g_cfg.fullscreen) return;
    int fw = SMB3_WIDE_WIDTH, fh = 240;
    (void)glue_get_present_framebuffer(&fw, &fh);
    if (fw <= 0 || fh <= 0) return;
    if (g_menu && fh == 240 && fw < SMB3_WIDE_WIDTH) fw = SMB3_WIDE_WIDTH;
    SDL_SetWindowSize(g_window, fw * g_cfg.scale, fh * g_cfg.scale);
}

/* Fullscreen runs in the largest 16:9 display mode whose height is an exact
 * multiple of the picture's 240 rows (1280x720 = 3x on a 1080p panel), so the
 * picture is drawn with whole device pixels and the compositor scales that
 * surface to the panel. Desktop fullscreen would render at the desktop
 * resolution - 4.5x at 1080p - which is what makes some pixel columns four
 * device pixels wide and the rest five. */
static bool best_fullscreen_mode(SDL_DisplayMode *out) {
    int display = g_window ? SDL_GetWindowDisplayIndex(g_window) : 0;
    if (display < 0) display = 0;
    int count = SDL_GetNumDisplayModes(display);
    int best_scale = 0;
    out->refresh_rate = 0;
    for (int i = 0; i < count; i++) {
        SDL_DisplayMode mode;
        if (SDL_GetDisplayMode(display, i, &mode) != 0) continue;
        if (mode.h <= 0 || mode.h % 240 != 0) continue;
        const int scale = mode.h / 240;
        if (scale < 1 || mode.w < SMB3_WIDE_WIDTH * scale) continue;
        /* 16:9, allowing the rounding of real modes (1366x768 and friends). */
        const long long want = (long long)mode.h * 16;
        const long long have = (long long)mode.w * 9;
        if (have < want * 995 / 1000 || have > want * 1005 / 1000) continue;
        if (scale > best_scale ||
            (scale == best_scale && mode.refresh_rate > out->refresh_rate)) {
            *out = mode;
            best_scale = scale;
        }
    }
    return best_scale > 0;
}

static void set_fullscreen(bool fullscreen) {
    if (!g_window) return;
    if (fullscreen) {
        SDL_DisplayMode mode;
        if (best_fullscreen_mode(&mode)) {
            if (SDL_SetWindowDisplayMode(g_window, &mode) != 0)
                fprintf(stderr, "video: display mode %dx%d unavailable: %s\n",
                        mode.w, mode.h, SDL_GetError());
            /* Wayland derives the fullscreen surface from the window size. */
            SDL_SetWindowSize(g_window, mode.w, mode.h);
        }
        SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN);
    } else {
        SDL_SetWindowFullscreen(g_window, 0);
        resize_window_to_frame();
    }
}

static void toggle_fullscreen(void) {
    g_cfg.fullscreen = !g_cfg.fullscreen;
    set_fullscreen(g_cfg.fullscreen);
    notify("FULLSCREEN %s", g_cfg.fullscreen ? "ON" : "OFF");
    config_save(&g_cfg);
}

static void toggle_widescreen(void) {
    g_cfg.widescreen = !g_cfg.widescreen;
    glue_set_widescreen(g_cfg.widescreen);
    resize_window_to_frame();
    notify("WIDESCREEN %s", g_cfg.widescreen ? "16:9" : "4:3");
    config_save(&g_cfg);
}

static void toggle_fps(void) {
    g_cfg.show_fps = !g_cfg.show_fps;
    notify("FPS %s", g_cfg.show_fps ? "ON" : "OFF");
    config_save(&g_cfg);
}

static void set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    g_cfg.volume = volume;
    audio_out_set_volume(g_audio, volume);
}

static void adjust_volume(int delta) {
    set_volume(g_cfg.volume + delta);
    notify("VOLUME %d", g_cfg.volume);
    config_save(&g_cfg);
}

static void open_menu(void) {
    g_menu = true;
    g_menu_index = 0;
    resize_window_to_frame();
    audio_out_pause(g_audio, true);
}

static void close_menu(void) {
    g_menu = false;
    g_capture = -1;
    config_save(&g_cfg);
    resize_window_to_frame();
    audio_out_clear(g_audio);
    audio_out_pause(g_audio, false);
}

/* Left/right on a menu row: toggles flip, values step. In-menu edits
 * apply live and persist on close. */
/* Left/Right steps value rows only; toggles flip with Enter. */
static void menu_adjust(int dir) {
    if (g_menu_tab == TAB_VIDEO && g_menu_index == 3) {
        g_cfg.scanlines = (g_cfg.scanlines + dir + 3) % 3;
    } else if (g_menu_tab == TAB_AUDIO) {
        set_volume(g_cfg.volume + dir * 5);
    } else if (g_menu_tab == TAB_GAME && g_menu_index == 2) {
        int slot = g_slot + dir;
        if (slot < 1) slot = 5;
        if (slot > 5) slot = 1;
        g_slot = slot;
        g_cfg.slot = slot;
    }
}

static void activate_menu(void) {
    if (g_menu_tab == TAB_VIDEO) {
        if (g_menu_index == 0) toggle_fullscreen();
        else if (g_menu_index == 1) toggle_widescreen();
        else if (g_menu_index == 2) {
            g_cfg.show_fps = !g_cfg.show_fps;
            config_save(&g_cfg);
        }
        else menu_adjust(1);
    } else if (g_menu_tab == TAB_AUDIO) {
        menu_adjust(1);
    } else if (g_menu_tab == TAB_GAME) {
        if (g_menu_index == 0) save_slot(g_slot);
        else if (g_menu_index == 1) load_slot(g_slot);
        else menu_adjust(1);
    } else if (g_menu_tab == TAB_CONTROLS) {
        g_capture = g_menu_index;
    } else if (g_menu_tab == TAB_SYSTEM) {
        if (g_menu_index == 0) close_menu();
        else g_running = false;
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
    if (g_menu && g_capture >= 0) {
        /* CONTROLS capture: the next pressed key becomes the binding. */
        if (!repeat) {
            if (key == SDLK_ESCAPE) g_capture = -1;
            else {
                bind_key(g_capture, key);
                g_capture = -1;
            }
        }
        return;
    }
    if (key == SDLK_ESCAPE) {
        if (!repeat) {
            if (g_menu) close_menu();
            else open_menu();
        }
        return;
    }
    if (g_menu) {
        int rows = menu_row_count();
        if (rows < 1) return;
        if (key == SDLK_UP || key == SDLK_w) {
            g_menu_index = (g_menu_index + rows - 1) % rows;
        } else if (key == SDLK_DOWN || key == SDLK_s) {
            g_menu_index = (g_menu_index + 1) % rows;
        } else if (key == SDLK_LEFT || key == SDLK_a) {
            menu_adjust(-1);
        } else if (key == SDLK_RIGHT || key == SDLK_d) {
            menu_adjust(1);
        } else if (key == SDLK_TAB) {
            if (!repeat) set_tab(g_menu_tab + (SDL_GetModState() & KMOD_SHIFT ? -1 : 1));
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
            if (!repeat) activate_menu();
        } else if (key >= SDLK_1 && key <= SDLK_5) {
            if (!repeat) set_tab(key - SDLK_1);
        }
        return;
    }
    if (repeat) return;
    if (key == SDLK_F5) save_slot(g_slot);
    else if (key == SDLK_F7) load_slot(g_slot);
    else if (key == SDLK_g) save_replay_file();
    else if (key == SDLK_F11) toggle_fullscreen();
    else if (key == SDLK_F10) toggle_fps();
    else if (key == SDLK_MINUS) adjust_volume(-5);
    else if (key == SDLK_EQUALS) adjust_volume(5);
    else if (key == SDLK_r) {
        /* Console reset preserves RAM: carry the save slots across power-on. */
        uint8_t sram[0x2000];
        size_t sram_len = glue_sram_size();
        bool keep = sram_len <= sizeof(sram) && glue_sram_export(sram, sram_len);
        glue_reset();
        if (keep) glue_sram_import(sram, sram_len);
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
                int rows = menu_row_count();
                if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
                    g_menu_index = (g_menu_index + rows - 1) % rows;
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
                    g_menu_index = (g_menu_index + 1) % rows;
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT)
                    menu_adjust(-1);
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
                    menu_adjust(1);
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
                    set_tab(g_menu_tab - 1);
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
                    set_tab(g_menu_tab + 1);
                else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_A ||
                         event->cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    if (g_capture < 0) activate_menu();
                } else if (event->cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                    if (g_capture >= 0) g_capture = -1;
                    else close_menu();
                }
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

static void draw_menu(uint32_t *dst, int width, int height) {
    MenuState st;
    st.fullscreen = g_cfg.fullscreen ? 1 : 0;
    st.widescreen = g_cfg.widescreen ? 1 : 0;
    st.show_fps = g_cfg.show_fps ? 1 : 0;
    st.scanlines = g_cfg.scanlines;
    st.volume = g_cfg.volume;
    st.slot = g_slot < 1 ? 1 : g_slot > 5 ? 5 : g_slot;
    st.capture_row = g_capture;
    const SDL_Scancode keys[8] = {g_cfg.key_a, g_cfg.key_b, g_cfg.key_select,
                                  g_cfg.key_start, g_cfg.key_up, g_cfg.key_down,
                                  g_cfg.key_left, g_cfg.key_right};
    for (int i = 0; i < 8; i++)
        snprintf(st.key_names[i], sizeof(st.key_names[i]), "%s", key_name(keys[i]));
    OverlayTarget target = {dst, width, height};
    overlay_menu(target, g_menu_tab, g_menu_index, &st, g_chr);
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
    if (g_menu && width < SMB3_WIDE_WIDTH && height == 240) {
        /* Full-size menu over a narrow frame: pillarbox the frame onto the
         * wide canvas first so the panel is never cut off. */
        size_t wide_pixels = (size_t)SMB3_WIDE_WIDTH * (size_t)height;
        if (wide_pixels > g_present_capacity) {
            uint32_t *grown = realloc(g_present, wide_pixels * sizeof(*grown));
            if (!grown) return false;
            g_present = grown;
            g_present_capacity = wide_pixels;
        }
        memset(g_present, 0, wide_pixels * sizeof(*g_present));
        int x0 = (SMB3_WIDE_WIDTH - width) / 2;
        for (int y = 0; y < height; y++) {
            memcpy(g_present + (size_t)y * SMB3_WIDE_WIDTH + x0,
                   framebuffer + (size_t)y * width,
                   (size_t)width * sizeof(*g_present));
        }
        width = SMB3_WIDE_WIDTH;
    } else {
        memcpy(g_present, framebuffer, pixels * sizeof(*g_present));
    }
    if (g_menu) draw_menu(g_present, width, height);
    if (g_cfg.show_fps) {
        OverlayTarget fps_target = {g_present, width, height};
        overlay_fps(fps_target, g_fps_shown, g_chr);
    }
    if (g_notice[0] && SDL_GetTicks() < g_notice_until)
        cyc_overlay_toast(g_present, width, height, g_notice, NULL);

    g_frame_w = width;
    g_frame_h = height;
    VideoOptions options;
    options.scanlines = g_cfg.scanlines != 0;
    return video_present(g_video, g_present, width, height, &options);
}

static bool init_video(int fw, int fh) {
    g_window = SDL_CreateWindow("Mushroom Recompiled", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, fw * g_cfg.scale,
                                fh * g_cfg.scale,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_VULKAN);
    if (!g_window) {
        fprintf(stderr, "video: SDL_CreateWindow (Vulkan) failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(g_window, SMB3_WIDE_WIDTH, 240);
    g_video = video_create(g_window, g_cfg.vsync);
    if (!g_video) {
        fprintf(stderr, "video: Vulkan presenter failed to start "
                        "(no Vulkan-capable GPU/driver found)\n");
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        return false;
    }
    char title[128];
    snprintf(title, sizeof(title), "Mushroom Recompiled - %s", video_name(g_video));
    SDL_SetWindowTitle(g_window, title);
    /* The compositor only honours fullscreen once the window is mapped, so
     * the request waits for the first frames (see the main loop). */
    g_fullscreen_pending = g_cfg.fullscreen;
    g_window_shown_at = SDL_GetTicks();
    return true;
}

static void usage(const char *argv0) {
    printf("Usage: %s --rom GAME.NES [options]\n"
           "  --scale N               window scale 1-8 (default 3)\n"
           "  --fullscreen            start fullscreen\n"
           "  --widescreen            16:9 playfield (default)\n"
           "  --4:3                   native 4:3 frame\n"
           "  --scanlines 0|1|2       CRT scanline strength\n"
           "  --audio-rate 44100|48000|96000\n"
           "  --volume 0-100\n"
           "  --fps                   show the FPS counter\n"
           "  --slot 1-5              active save slot\n"
           "  --replay FILE           feed raw controller bytes (one per frame)\n"
           "  --record FILE           write one controller byte per emulated frame\n"
           "  --frames N              run N frames then exit (smoke test)\n"
           "  --menu                  start paused in the in-game menu\n"
           "  --screenshot PATH       write a presented frame as PNG\n"
           "  --screenshot-frame N    frame to capture (default 1)\n"
           "  --no-vsync              disable vsync\n"
           "  --help                  this text\n"
           "With no --rom and no remembered ROM, one nearby .nes\n"
           "boots directly; otherwise a picker lists them,\n"
           "and you can also drag one onto it.\n"
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
        } else if (!strcmp(arg, "--vulkan")) {
            /* Accepted for compatibility: Vulkan is now the only presenter. */
        } else if (!strcmp(arg, "--fullscreen")) {
            cfg->fullscreen = true;
        } else if (!strcmp(arg, "--widescreen")) {
            cfg->widescreen = true;
        } else if (!strcmp(arg, "--4:3")) {
            cfg->widescreen = false;
        } else if (!strcmp(arg, "--scanlines") && i + 1 < argc) {
            cfg->scanlines = atoi(argv[++i]);
        } else if (!strcmp(arg, "--brightness") && i + 1 < argc) {
            cfg->brightness = atoi(argv[++i]);
        } else if (!strcmp(arg, "--audio-rate") && i + 1 < argc) {
            cfg->audio_rate = atoi(argv[++i]);
        } else if (!strcmp(arg, "--volume") && i + 1 < argc) {
            cfg->volume = atoi(argv[++i]);
        } else if (!strcmp(arg, "--fps")) {
            cfg->show_fps = true;
        } else if (!strcmp(arg, "--slot") && i + 1 < argc) {
            cfg->slot = atoi(argv[++i]);
        } else if (!strcmp(arg, "--replay") && i + 1 < argc) {
            snprintf(cfg->replay, sizeof(cfg->replay), "%s", argv[++i]);
        } else if (!strcmp(arg, "--record") && i + 1 < argc) {
            snprintf(cfg->record, sizeof(cfg->record), "%s", argv[++i]);
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
    snprintf(g_sram_path, sizeof(g_sram_path), "%ssmb3.srm", g_save_dir);
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

    bool batch = frame_limit > 0 || g_screenshot[0];
    bool rom_ok = g_cfg.rom[0] && glue_load_rom_utf8(g_cfg.rom);
    if (!rom_ok && !batch)
        rom_ok = pick_autoload();
    if (!rom_ok && !batch) {
        if (!init_video(256, 240)) {
            SDL_Quit();
            return 1;
        }
        if (!run_picker()) {
            video_destroy(g_video);
            if (g_window) SDL_DestroyWindow(g_window);
            SDL_Quit();
            return 0;
        }
        rom_ok = true;
    }
    if (!rom_ok) {
        fprintf(stderr, "No ROM: run with no flags to choose one, or --rom GAME.NES\n");
        SDL_Quit();
        return g_cfg.rom[0] ? 1 : 2;
    }

    g_audio = audio_out_create(g_cfg.audio_rate, g_cfg.volume);
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
    if (g_cfg.replay[0]) {
        FILE *rf = fopen(g_cfg.replay, "rb");
        if (!rf) {
            fprintf(stderr, "cannot read replay %s\n", g_cfg.replay);
            audio_out_destroy(g_audio);
            SDL_Quit();
            return 1;
        }
        if (fseek(rf, 0, SEEK_END)) {
            fclose(rf);
            audio_out_destroy(g_audio);
            SDL_Quit();
            return 1;
        }
        long rn = ftell(rf);
        if (rn <= 0 || fseek(rf, 0, SEEK_SET)) {
            fclose(rf);
            fprintf(stderr, "cannot read replay %s\n", g_cfg.replay);
            audio_out_destroy(g_audio);
            SDL_Quit();
            return 1;
        }
        g_replay = malloc((size_t)rn);
        if (!g_replay || fread(g_replay, 1, (size_t)rn, rf) != (size_t)rn) {
            free(g_replay);
            g_replay = NULL;
            fclose(rf);
            fprintf(stderr, "cannot read replay %s\n", g_cfg.replay);
            audio_out_destroy(g_audio);
            SDL_Quit();
            return 1;
        }
        fclose(rf);
        g_replay_len = (size_t)rn;
        /* Replays never touch the real save file. */
        g_sram_ok = false;
    }
    if (g_cfg.record[0]) {
        g_record_fp = fopen(g_cfg.record, "wb");
        if (!g_record_fp) {
            fprintf(stderr, "cannot write %s\n", g_cfg.record);
            audio_out_destroy(g_audio);
            SDL_Quit();
            return 1;
        }
    }
    if (!g_replay) {
        int sram_rc = glue_sram_load(g_sram_path);
        if (sram_rc < 0) {
            g_sram_ok = false;
            fprintf(stderr, "save: %s is not a valid save file; it will not be overwritten\n",
                    g_sram_path);
        }
    }
    g_chr = glue_get_chr_rom(&g_chr_len);
    if (!g_chr || g_chr_len < 0x6000) {
        fprintf(stderr, "video: CHR ROM unavailable; menu skin disabled\n");
        g_chr = NULL;
    }

    int fw = 256, fh = 240;
    (void)glue_get_present_framebuffer(&fw, &fh);
    if (!g_window) {
        if (!init_video(fw, fh)) {
            audio_out_destroy(g_audio);
            SDL_Quit();
            return 1;
        }
    } else {
        resize_window_to_frame();
    }

    if (g_menu) open_menu();
    g_running = true;
    uint64_t iterations = 0;
    Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 next_tick = SDL_GetPerformanceCounter();
    g_fps_t0 = next_tick;
    g_fps_frames = 0;
    const double frame_rate = glue_frame_rate();
    const double frame_seconds = frame_rate > 1.0 ? 1.0 / frame_rate : 1.0 / 60.0;
    int16_t audio_buffer[4096];

    while (g_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) handle_event(&event);
        if (g_fullscreen_pending && SDL_GetTicks() - g_window_shown_at >= 150) {
            g_fullscreen_pending = false;
            set_fullscreen(true);
        }

        if (!g_menu) {
            uint8_t live = read_pad();
            uint8_t fed = live;
            if (g_replay) {
                if (g_replay_pos < g_replay_len) {
                    fed = g_replay[g_replay_pos++];
                } else {
                    fprintf(stderr, "replay finished\n");
                    notify("REPLAY FINISHED");
                    g_running = false;
                }
            }
            if (!g_running) {
                /* Recording over: present the last frame instead of stepping. */
            } else {
            session_push(fed);
            if (g_record_fp) fwrite(&fed, 1, 1, g_record_fp);
            glue_set_pad1(fed);
            glue_step_frame();
            g_fps_frames++;
            Uint64 fps_now = SDL_GetPerformanceCounter();
            if (fps_now - g_fps_t0 >= frequency / 2) {
                g_fps_shown = (int)((double)g_fps_frames * (double)frequency /
                                    (double)(fps_now - g_fps_t0) + 0.5);
                g_fps_frames = 0;
                g_fps_t0 = fps_now;
            }
            if (!g_replay && g_sram_ok && ++g_sram_frames >= 60) {
                g_sram_frames = 0;
                if (!glue_sram_save(g_sram_path))
                    fprintf(stderr, "save: cannot write %s\n", g_sram_path);
            }
            size_t samples = glue_get_audio_samples(audio_buffer, 4096, 0);
            if (samples && g_audio) audio_out_push(g_audio, audio_buffer, samples);
            /* Measure after this frame's samples are queued, then pick the
             * ratio for the next frame. That removes the one-frame feedback
             * delay that otherwise keeps a healthy queue just outside the
             * deadband and makes the controller resample forever. */
            if (g_audio) {
                const size_t target = audio_out_target(g_audio);
                if (target) {
                    double fill = ((double)target - (double)audio_out_queued(g_audio)) / (double)target;
                    if (fill < -1.0) fill = -1.0;
                    if (fill > 1.0) fill = 1.0;
                    /* Deadband: a correctly paced guest and device leave the
                     * queue near its target forever, so do not resample for
                     * ordinary frame jitter. Outside it, trim by at most 0.5%
                     * and slew slowly so the correction cannot become audible
                     * pitch wobble. */
                    const double deadband = 0.10;
                    double want = 1.0;
                    if (fill > deadband) want += 0.005 * (fill - deadband);
                    else if (fill < -deadband) want += 0.005 * (fill + deadband);
                    double delta = want - g_audio_rate_scale;
                    const double max_step = 0.00005;
                    if (delta > max_step) delta = max_step;
                    if (delta < -max_step) delta = -max_step;
                    g_audio_rate_scale += delta;
                    glue_set_audio_rate_scale(g_audio_rate_scale);
                }
            }
            }
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
            /* Sleep in one short call, then spin to the exact tick.
             * SDL_Delay oversleeps by a fraction of a millisecond on every
             * call; slept naively, that bias lands on every frame, the loop
             * runs ~1% slow, and the audio tempo corrector sustains a
             * matching stretch to keep the queue full -- everything sounds
             * flat. Measuring each sleep against what was requested learns
             * the overshoot, so the average frame lands exactly on the tick
             * on any timer granularity. Spinning is bounded to ~1 ms. */
            for (;;) {
                now = SDL_GetPerformanceCounter();
                if (now >= next_tick) break;
                double remaining_ms =
                    (double)(next_tick - now) * 1000.0 / (double)frequency;
                double sleep_ms = remaining_ms - 1.0 - g_sleep_bias_ms;
                if (sleep_ms < 1.0) break; /* spin the tail */
                Uint64 before = SDL_GetPerformanceCounter();
                Uint32 requested = (Uint32)sleep_ms;
                if (requested < 1) requested = 1;
                SDL_Delay(requested);
                double slept_ms = (double)(SDL_GetPerformanceCounter() - before) *
                                  1000.0 / (double)frequency;
                double err = slept_ms - (double)requested;
                if (err > -0.5 && err < 8.0) {
                    g_sleep_bias_ms += 0.05 * (err - g_sleep_bias_ms);
                    if (g_sleep_bias_ms < 0.0) g_sleep_bias_ms = 0.0;
                    if (g_sleep_bias_ms > 4.0) g_sleep_bias_ms = 4.0;
                }
            }
            while (SDL_GetPerformanceCounter() < next_tick) {
                /* exact wakeup; at most ~1 ms of spin plus learned bias */
            }
        } else if (now - next_tick > frequency / 30u) {
            next_tick = now;
        }
    }

    if (!g_replay && g_sram_ok) glue_sram_save(g_sram_path);
    config_save(&g_cfg);
    if (g_record_fp) fclose(g_record_fp);
    free(g_session);
    free(g_replay);
    audio_out_destroy(g_audio);
}
