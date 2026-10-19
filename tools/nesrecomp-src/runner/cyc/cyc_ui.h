/*
 * cyc_ui.h - recomp-ui on the windowed cycle host: the pre-boot launcher and
 * the in-game runtime menu. Built only when the game links recomp-ui
 * (nesrecomp_add_cycle_game(... RECOMP_UI <dir>), CYC_WITH_RECOMP_UI); the
 * SDL host runs without it, with the same bindings and Disk action.
 *
 *   cyc_ui_launcher.c  recomp_launcher_run_window(): game card, settings,
 *                      host-owned bindings (every controller button and host
 *                      shortcut, keyboard and controller), persisted in
 *                      config.ini by the host (cyc_settings.h)
 *   cyc_ui_menu.c      the runtime menu's rows: recomp-ui's standard display /
 *                      audio subset, then the host's extra_items (the Disk
 *                      Drive section for an FDS image only, the HLE axes from
 *                      nes_fds_hle_axes(), the shortcut list, Quit) and the
 *                      game's own rows (cyc_host_extras.h)
 *   cyc_ui_imgui.cpp   Dear ImGui over the host's SDL_Renderer (recomp-ui's
 *                      SDL2 platform + SDL_Renderer2 backends): the menu and
 *                      the toast, drawn after the picture is presented, never
 *                      into it
 */
#pragma once
#include "cyc_host_extras.h"
#include "cyc_fds_bios.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

struct CycSettings;                       /* cyc_settings.h (C only: this header is also C++'s) */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
union SDL_Event;

/* What the menu needs from the window. */
typedef struct {
    struct CycSettings *settings;         /* live; the menu edits it */
    const CycHostExtras *extras;          /* the game's, or NULL */
    bool fds;                             /* a Famicom Disk System program */
    void (*apply)(void);                  /* display / audio settings changed: apply them now */
    void (*save)(void);                   /* persist settings (config.ini) */
    void (*quit)(void);                   /* close the window */
    long (*frames_done)(void);
    uint64_t (*now_ms)(void);             /* the toast's clock */
    const char *title;                    /* the menu's title (the game) */
    bool (*save_state)(void);             /* the save state slot (cyc_state.h) */
    bool (*load_state)(void);
    /* The mod runtime's RecompLauncherCModProvider (NULL: no mods) and the
     * image the game runs: the menu's Mods rows edit the features and their
     * options live, commit them for this image, and call mods_changed to
     * activate the plugins again. */
    const void *mods;
    const char *image;
    void (*mods_changed)(void);
} CycUiHost;

/* The launcher, before the image loads. `settings` is edited in place; on
 * launch *rom_path may name another image. 0: launch, 1: the player quit,
 * 2: no launcher could open (go on as without it).
 * `bios` (NULL: none) is the host's FDS BIOS lookup without its saved and
 * image paths (--fds-bios, game.toml's, the compiled CRC): the launcher runs
 * it for the selected image and the player's pick (settings->fds_bios), shows
 * "FDS BIOS required" with Select BIOS... when it finds nothing usable for a
 * disk image, and keeps Start disabled until it does.
 * `mods` (NULL: none) is the mod runtime's RecompLauncherCModProvider
 * (cyc_session.h): the launcher's Mods screen, whose selection PLAY commits. */
int  cyc_ui_launcher(struct CycSettings *settings, const char *settings_path, const CycHostExtras *extras,
                     const char **rom_path, bool fds, const CycFdsBiosLookup *bios, const void *mods);
/* The launcher's BIOS verdict (recomp-ui GameInfo.bios_verify_for_rom), ctx
 * the lookup above. */
struct RecompLauncherCBiosVerify;
int  cyc_ui_bios_verify(void *ctx, const char *bios_path, const char *rom_path,
                        struct RecompLauncherCBiosVerify *out);

/* The runtime menu. */
bool cyc_ui_init(SDL_Window *win, SDL_Renderer *ren, const CycUiHost *host);
void cyc_ui_shutdown(void);
void cyc_ui_process_event(const union SDL_Event *ev);   /* ImGui's view of every event */
bool cyc_ui_menu_open(void);
void cyc_ui_toggle_menu(void);
/* Menu navigation (RecompRuntimeUiInput values). */
void cyc_ui_nav(int input, bool repeat);
/* The toast (NULL, NULL clears it). */
void cyc_ui_set_toast(const char *title, const char *body);
/* Draw the menu and the toast over what the renderer holds (the picture and
 * any dev bar), at the window's full resolution. */
void cyc_ui_render(void);

#ifdef __cplusplus
}
#endif
