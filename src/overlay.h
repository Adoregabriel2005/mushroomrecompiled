#pragma once
#include <stdint.h>

/* Menu skin: peach panel, paper border tiles from CHR bank $5E,
 * in-game dialog font, red cursor, check/cross markers. The panel is adapted
 * to this launcher's five-row menu and five save slots. */
typedef struct {
    uint32_t *fb; /* ARGB8888, R in the high byte */
    int w, h;
} OverlayTarget;

/* Pause menu: tabbed pages with the current values inline. Tabs are
 * VIDEO (fullscreen, widescreen, FPS counter, scanlines), AUDIO
 * (volume), GAME (save state, load state, slot), CONTROLS (keyboard
 * bindings, Enter on a row captures the next pressed key) and SYSTEM
 * (resume, quit). capture_row is the binding being recorded, or -1. */
enum { TAB_VIDEO = 0, TAB_AUDIO, TAB_GAME, TAB_CONTROLS, TAB_SYSTEM, TAB_COUNT = 5 };

typedef struct {
    int fullscreen;
    int widescreen;
    int show_fps;
    int scanlines; /* 0 = off, 1-2 = strength */
    int volume;    /* 0-100 */
    int slot;      /* 1-5 */
    int capture_row; /* CONTROLS row recording a key, or -1 */
    char key_names[8][32]; /* A, B, SELECT, START, UP, DOWN, LEFT, RIGHT */
} MenuState;

void overlay_menu(OverlayTarget target, int tab, int selected, const MenuState *st,
                  const uint8_t *chr);

/* FPS counter in the picture's top-left corner, in the in-game dialog font.
 * Needs the CHR ROM for the glyphs; draws nothing without it. */
void overlay_fps(OverlayTarget target, int fps, const uint8_t *chr);
