/*
 * controller.c — SDL2 gamepad support for the NES runner. See controller.h.
 *
 * Button mapping is deliberately forgiving so any pad "just works" without
 * configuration: the two right-hand face buttons both act as NES A and the two
 * left-hand face buttons both act as NES B. The d-pad and the left analog stick
 * both drive the NES d-pad.
 */
#include "controller.h"
#include "keybinds.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

#include "logical_input.h"
#define MAX_PADS NESRECOMP_INPUT_SEATS

static SDL_GameController *s_pads[MAX_PADS];     /* physical device slots */
static SDL_JoystickID      s_pad_ids[MAX_PADS];  /* instance id per slot */
static int                 s_count = 0;
static int s_route[MAX_PADS]; /* logical seat -> physical slot; -1 disconnected */
static int s_initialized;

/* Preserve connected assignments during hotplug. Explicit launcher choices
 * are matched before automatic seats, so P1 keyboard + one pad routes to P2.
 * Identical-model pads are distinguished by live instance IDs while the
 * launcher is open, then by unused GUID matches on the next launch. */
static void assign_seats(void) {
    if (NESRECOMP_INPUT_SEATS == 2) {
        for (int p=0;p<MAX_PADS;++p) s_route[p]=p;
        return;
    }
    int used[MAX_PADS]={0};
    for (int p=0;p<MAX_PADS;++p) {
        if (s_route[p]>=0 && s_pads[s_route[p]]) used[s_route[p]]=1;
        else s_route[p]=-1;
    }
    for (int pass=0;pass<3;++pass) for (int p=0;p<MAX_PADS;++p) {
        if (s_route[p]>=0 || g_nes_config.player_src[p]!=2) continue;
        const char *wanted=g_nes_config.player_gamepad_guid[p];
        int instance=g_nes_config.player_gamepad_instance[p];
        for (int d=0;d<MAX_PADS;++d) {
            if (!s_pads[d] || used[d]) continue;
            char guid[40];
            SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(s_pads[d])),guid,sizeof guid);
            int match=pass==0 ? instance>=0 && instance==s_pad_ids[d] :
                pass==1 ? wanted[0] && !strcmp(wanted,guid) : !wanted[0] && instance<0;
            if (!match) continue;
            s_route[p]=d;used[d]=1;break;
        }
    }
}

static int slot_for_instance(SDL_JoystickID id) {
    for (int i = 0; i < MAX_PADS; i++)
        if (s_pads[i] && s_pad_ids[i] == id) return i;
    return -1;
}

static int first_free_slot(void) {
    for (int i = 0; i < MAX_PADS; i++)
        if (!s_pads[i]) return i;
    return -1;
}

static void open_device(int device_index) {
    if (!SDL_IsGameController(device_index)) return;  /* not a mapped gamepad */
    /* Dedupe: SDL reports already-connected pads both via init enumeration and
     * via a CONTROLLERDEVICEADDED event, so the same device can arrive twice. */
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device_index);
    if (id >= 0 && slot_for_instance(id) >= 0) return;
    int slot = first_free_slot();
    if (slot < 0) return;
    SDL_GameController *gc = SDL_GameControllerOpen(device_index);
    if (!gc) {
        fprintf(stderr, "[Controller] open failed: %s\n", SDL_GetError());
        return;
    }
    s_pads[slot]    = gc;
    s_pad_ids[slot] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
    s_count++;
    if (s_initialized) assign_seats();
    printf("[Controller] Device %d connected: %s\n", slot + 1, SDL_GameControllerName(gc));
    fflush(stdout);
}

static void close_instance(SDL_JoystickID id) {
    int slot = slot_for_instance(id);
    if (slot < 0) return;
    printf("[Controller] Device %d disconnected: %s\n",
           slot + 1, SDL_GameControllerName(s_pads[slot]));
    fflush(stdout);
    SDL_GameControllerClose(s_pads[slot]);
    s_pads[slot]    = NULL;
    s_pad_ids[slot] = 0;
    s_count--;
    for (int p=0;p<MAX_PADS;++p) if (s_route[p]==slot) s_route[p]=-1;
}

void controller_init(void) {
    for (int p=0;p<MAX_PADS;++p) s_route[p]=-1;
    s_initialized=0;
    if (!(SDL_WasInit(0) & SDL_INIT_GAMECONTROLLER)) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
            fprintf(stderr, "[Controller] init failed: %s\n", SDL_GetError());
            return;
        }
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) open_device(i);
    s_initialized=1;
    assign_seats();
    if (s_count == 0)
        printf("[Controller] No gamepad detected; keyboard active. Plug one in anytime.\n");
}

void controller_handle_event(const SDL_Event *ev) {
    if (ev->type == SDL_CONTROLLERDEVICEADDED)
        open_device(ev->cdevice.which);
    else if (ev->type == SDL_CONTROLLERDEVICEREMOVED)
        close_instance(ev->cdevice.which);
}

uint8_t controller_read_player(int player) {
    if (player<1 || player>MAX_PADS) return 0;
    int slot = s_route[player-1];
    if (slot < 0 || slot >= MAX_PADS || !s_pads[slot]) return 0;
    SDL_GameController *gc = s_pads[slot];
    const GamepadBinds *gb = keybinds_get_pad(player);

    /* NES button bit per btn_mask index (matches keybinds_read_player order). */
    static const uint8_t nes_bit[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) {
        uint32_t mask = gb->btn_mask[i];
        for (int bit = 0; mask && bit < SDL_CONTROLLER_BUTTON_MAX && bit < 32; bit++) {
            if ((mask & (1u << bit)) &&
                SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)bit)) {
                b |= nes_bit[i];
                break;
            }
        }
    }

    /* Left analog stick also drives the d-pad (configurable). */
    if (gb->analog_dpad) {
        int dz = gb->deadzone;
        if (NESRECOMP_INPUT_SEATS > 2) {
            int percent = g_nes_config.deadzone[player-1];
            if (percent < 0) percent = 0;
            if (percent > 100) percent = 100;
            dz = percent * 32767 / 100;
        }
        int ax = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
        int ay = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -dz) b |= 0x02;  /* left  */
        if (ax >  dz) b |= 0x01;  /* right */
        if (ay < -dz) b |= 0x08;  /* up    */
        if (ay >  dz) b |= 0x04;  /* down  */
    }

    return b;
}

int controller_instance_is_player(SDL_JoystickID instance_id, int player) {
    if (player<1 || player>MAX_PADS) return 0;
    int slot = s_route[player-1];
    return slot >= 0 && slot < MAX_PADS && s_pads[slot] &&
           s_pad_ids[slot] == instance_id;
}

int controller_count(void) { return s_count; }

void controller_shutdown(void) {
    for (int i = 0; i < MAX_PADS; i++) {
        if (s_pads[i]) { SDL_GameControllerClose(s_pads[i]); s_pads[i] = NULL; }
    }
    s_count = 0;
    s_initialized=0;
    for (int p=0;p<MAX_PADS;++p) s_route[p]=-1;
}
