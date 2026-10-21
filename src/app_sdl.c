/* Minimal native Linux/Steam Deck frontend for the recompilation core.
 * Deliberately independent of the Win32 UI: retain gameplay and ROM checks.
 * No Nintendo program bytes, artwork or sounds are embedded in this source.
 */
#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include "glue.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_AudioDeviceID audio_device;
static SDL_GameController *controller;
static char *save_dir;
static bool paused;
static bool widescreen = true;
static bool fullscreen = true;
static int texture_width;

static double monotonic_seconds(void) {
    return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static bool exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static void path_join(char *dst, size_t cap, const char *dir, const char *name) {
    snprintf(dst, cap, "%s/%s", dir, name);
}

static void set_title(const char *message) {
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "Super Mario Bros. 3 — %s", message);
    SDL_SetWindowTitle(window, buffer);
}

static bool load_rom(const char *path) {
    if (!glue_load_rom(path)) {
        fprintf(stderr, "ROM absente ou incompatible : %s\n"
                "Version attendue : SMB3 USA PRG1 (393232 octets).\n", path);
        return false;
    }
    glue_set_widescreen(widescreen);
    paused = false;
    if (audio_device) {
        SDL_ClearQueuedAudio(audio_device);
        SDL_PauseAudioDevice(audio_device, 0);
    }
    set_title("jouer • F5 sauver • F9 charger • F6 affichage • Échap pause");
    fprintf(stderr, "ROM acceptée : %s\n", path);
    return true;
}

static bool save_or_load(bool save) {
    if (!glue_is_rom_loaded() || !save_dir) return false;
    char path[4096];
    path_join(path, sizeof(path), save_dir, "smb3-slot1.save");
    size_t length = mbstowcs(NULL, path, 0);
    if (length == (size_t)-1) return false;
    wchar_t *wide = malloc((length + 1) * sizeof(*wide));
    if (!wide) return false;
    mbstowcs(wide, path, length + 1);
    bool ok = save ? glue_save_state(wide) : glue_load_state(wide);
    free(wide);
    fprintf(stderr, "%s : %s\n", save ? "Sauvegarde" : "Chargement",
            ok ? "OK" : "échec");
    if (ok && !save && audio_device) SDL_ClearQueuedAudio(audio_device);
    return ok;
}

static void toggle_pause(void) {
    if (!glue_is_rom_loaded()) return;
    paused = !paused;
    if (audio_device) {
        if (paused) SDL_ClearQueuedAudio(audio_device);
        SDL_PauseAudioDevice(audio_device, paused ? 1 : 0);
    }
    set_title(paused ? "pause • Échap reprendre • F5 sauver • F9 charger"
                     : "jouer • F5 sauver • F9 charger • F6 affichage • Échap pause");
}

static void toggle_aspect(void) {
    widescreen = !widescreen;
    glue_set_widescreen(widescreen);
    if (texture) { SDL_DestroyTexture(texture); texture = NULL; }
    texture_width = 0;
    fprintf(stderr, "Affichage : %s\n", widescreen ? "16:9" : "4:3");
}

static uint8_t get_pad(void) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    uint8_t p = 0;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) p |= NES_PAD_RIGHT;
    if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_A]) p |= NES_PAD_LEFT;
    if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) p |= NES_PAD_UP;
    if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) p |= NES_PAD_DOWN;
    if (keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_SPACE]) p |= NES_PAD_A;
    if (keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_C]) p |= NES_PAD_B;
    if (keys[SDL_SCANCODE_RETURN]) p |= NES_PAD_START;
    if (keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_LSHIFT]) p |= NES_PAD_SELECT;
    if (controller && SDL_GameControllerGetAttached(controller)) {
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) p |= NES_PAD_RIGHT;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  p |= NES_PAD_LEFT;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP))    p |= NES_PAD_UP;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  p |= NES_PAD_DOWN;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) ||
            SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y)) p |= NES_PAD_A;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B) ||
            SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X)) p |= NES_PAD_B;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START)) p |= NES_PAD_START;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK)) p |= NES_PAD_SELECT;
        Sint16 x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 y = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
        if (x > 16000) p |= NES_PAD_RIGHT;
        if (x < -16000) p |= NES_PAD_LEFT;
        if (y > 16000) p |= NES_PAD_DOWN;
        if (y < -16000) p |= NES_PAD_UP;
    }
    return p;
}

static void reconnect_controller(void) {
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = NULL;
    }
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i) && (controller = SDL_GameControllerOpen(i))) {
            fprintf(stderr, "Manette : %s\n", SDL_GameControllerName(controller));
            return;
        }
    }
}

static bool draw_frame(void) {
    int w, h;
    const uint32_t *pixels = glue_get_present_framebuffer(&w, &h);
    if (!pixels || w < 1 || h != 240) return false;
    if (!texture || texture_width != w) {
        if (texture) SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, w, h);
        texture_width = w;
        /* The NES's 256x240 pixels have a non-square display aspect.
         * Fit stock mode to 4:3, instead of displaying it as 16:15. */
        SDL_RenderSetLogicalSize(renderer, w == 256 ? 320 : w, h);
    }
    if (!texture || SDL_UpdateTexture(texture, NULL, pixels, w * (int)sizeof(uint32_t))) return false;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_Rect dst = {0, 0, w == 256 ? 320 : w, h};
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_RenderPresent(renderer);
    return true;
}

static void play_audio(void) {
    if (!audio_device) return;
    int16_t samples[8192];
    size_t count = glue_get_audio_samples(samples, 8192, 1);
    /* Do not increase output delay indefinitely if the window stalls. */
    if (SDL_GetQueuedAudioSize(audio_device) < 48000u) {
        SDL_QueueAudio(audio_device, samples, (Uint32)(count * sizeof(int16_t)));
    }
}

static void print_usage(const char *name) {
    printf("Usage: %s [--rom /chemin/game.nes] [--windowed] [--native]\n"
           "Une ROM SMB3 USA PRG1 est requise ; elle n'est pas distribuée.\n"
           "Sinon : placer game.nes à côté du programme, ou dans le dossier\n"
           "de sauvegarde SDL de l'application.\n"
           "A: sauter, B/X: courir, Start: pause NES, Échap: pause PC.\n"
           "F5: sauvegarder, F9: charger, F6: 4:3/16:9, F11: plein écran.\n"
           "Steam Deck: Select + L1 = sauver, Select + R1 = charger,\n"
           "Select + Start = pause PC.\n", name);
}

int main(int argc, char **argv) {
    setlocale(LC_CTYPE, "");
    const char *rom_arg = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom_arg = argv[++i];
        else if (!strcmp(argv[i], "--windowed")) fullscreen = false;
        else if (!strcmp(argv[i], "--native")) widescreen = false;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Argument inconnu : %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_GameControllerAddMappingsFromFile("assets/gamecontrollerdb.txt");
    save_dir = SDL_GetPrefPath("Personal", "SMB3SteamDeck");
    int flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    window = SDL_CreateWindow("Super Mario Bros. 3", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 1280, 720, (Uint32)flags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_RendererInfo renderer_info;
    if (!SDL_GetRendererInfo(renderer, &renderer_info)) {
        fprintf(stderr, "SDL renderer: %s, flags=0x%x\n",
                renderer_info.name, renderer_info.flags);
    }
    SDL_AudioSpec desired = {0};
    desired.freq = 48000;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
    if (!audio_device) fprintf(stderr, "Audio SDL indisponible : %s\n", SDL_GetError());
    reconnect_controller();
    char local[4096], candidate[4096];
    bool loaded = false;
    if (rom_arg) {
        loaded = load_rom(rom_arg);
    } else {
        char *base = SDL_GetBasePath();
        if (base) {
            path_join(candidate, sizeof(candidate), base, "game.nes");
            if (exists(candidate)) loaded = load_rom(candidate);
            SDL_free(base);
        }
        if (!loaded && exists("game.nes")) loaded = load_rom("game.nes");
        if (!loaded && save_dir) {
            path_join(local, sizeof(local), save_dir, "game.nes");
            if (exists(local)) loaded = load_rom(local);
        }
    }
    if (!loaded) {
        fprintf(stderr, "ROM introuvable ou incompatible. Fournis --rom ou game.nes.\n");
        set_title("ROM manquante : glisser-déposer game.nes dans la fenêtre");
    }
    /* A 30/40 FPS SteamOS frame limit must NOT slow down the simulation.
     * Run the NES at its own cadence; only the presentation is frame-limited. */
    const bool perf_log = SDL_getenv("SMB3_PERF_LOG") != NULL;
    const char *delay_env = SDL_getenv("SMB3_TEST_RENDER_DELAY_MS");
    int render_delay_ms = delay_env ? atoi(delay_env) : 0;
    if (render_delay_ms < 0 || render_delay_ms > 100) render_delay_ms = 0;
    double next_frame = monotonic_seconds();
    double report_at = next_frame + 5.0;
    double report_start = next_frame;
    uint64_t simulated = 0, presented = 0, catchup = 0, capped = 0;
    bool running = true;
    bool pause_combo = false, save_combo = false, load_combo = false;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT: running = false; break;
                case SDL_DROPFILE:
                    load_rom(event.drop.file);
                    SDL_free(event.drop.file);
                    break;
                case SDL_CONTROLLERDEVICEADDED:
                case SDL_CONTROLLERDEVICEREMOVED:
                    reconnect_controller();
                    break;
                case SDL_KEYDOWN:
                    if (event.key.repeat) break;
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE: toggle_pause(); break;
                        case SDLK_F5: save_or_load(true); break;
                        case SDLK_F9: save_or_load(false); break;
                        case SDLK_F6: toggle_aspect(); break;
                        case SDLK_F11:
                            fullscreen = !fullscreen;
                            SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                            break;
                        default: break;
                    }
                    break;
                default: break;
            }
        }
        if (controller && SDL_GameControllerGetAttached(controller)) {
            bool back = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK);
            bool start = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START);
            bool l1 = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
            bool r1 = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
            bool pc = back && start, sc = back && l1, lc = back && r1;
            if (pc && !pause_combo) toggle_pause();
            if (sc && !save_combo) save_or_load(true);
            if (lc && !load_combo) save_or_load(false);
            pause_combo = pc; save_combo = sc; load_combo = lc;
        }
        double now = monotonic_seconds();
        double seconds = glue_is_rom_loaded() ? 1.0 / glue_frame_rate() : 1.0 / 60.0;
        if (now >= next_frame) {
            if (glue_is_rom_loaded() && !paused) {
                uint8_t pad = get_pad();
                if (pause_combo) pad &= (uint8_t)~(NES_PAD_SELECT | NES_PAD_START);
                int steps = 0;
                /* At 30 FPS, typically simulate two NES ticks before each
                 * present. Limit catch-up after suspension or heavy stalls. */
                while (next_frame <= now && steps < 8) {
                    glue_set_pad1(pad);
                    glue_step_frame();
                    play_audio();
                    ++steps;
                    ++simulated;
                    next_frame += seconds;
                }
                if (steps > 1) catchup += (unsigned)(steps - 1);
                if (next_frame <= now) {
                    /* Prevent seconds of fast-forward after a long pause or
                     * if the host cannot sustain full simulation speed. */
                    ++capped;
                    next_frame = now + seconds;
                }
                draw_frame();
            } else if (glue_is_rom_loaded() && paused) {
                draw_frame();
                next_frame = now + seconds;
            } else {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
                SDL_RenderPresent(renderer);
                next_frame = now + seconds;
            }
            ++presented;
            /* Artificial blocking to verify a 30 FPS Gamescope limit without
             * changing SteamOS settings or touching the user's current game. */
            if (render_delay_ms) SDL_Delay((Uint32)render_delay_ms);
        } else {
            Uint32 sleep_ms = (Uint32)((next_frame - now) * 1000.0);
            SDL_Delay(sleep_ms > 1 ? sleep_ms - 1 : 1);
        }
        if (perf_log && (now = monotonic_seconds()) >= report_at) {
            double duration = now - report_start;
            fprintf(stderr, "[smb3-perf] simulated=%.1f/s presented=%.1f/s "
                    "catchup=%llu capped=%llu audio_queue_ms=%.0f\n",
                    simulated / duration, presented / duration,
                    (unsigned long long)catchup, (unsigned long long)capped,
                    audio_device ? (double)SDL_GetQueuedAudioSize(audio_device)
                        / (48000.0 * 2 * sizeof(int16_t)) * 1000.0 : 0.0);
            simulated = presented = catchup = capped = 0;
            report_start = now;
            report_at = now + 5.0;
        }
    }
    if (controller) SDL_GameControllerClose(controller);
    if (audio_device) SDL_CloseAudioDevice(audio_device);
    if (texture) SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_free(save_dir);
    SDL_Quit();
    return 0;
}
