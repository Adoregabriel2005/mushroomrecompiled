#include "input.h"
#include <SDL.h>
#include <windows.h>
#include <string.h>
#include <stdio.h>

static SDL_Joystick *joystick = NULL;
static SDL_GameController *controller = NULL;
static int active_index = -1;
static char preference[512] = "";
static wchar_t current_name[180] = L"Nenhum controle conectado";
static int bindings[11] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static int deadzone = 25;
static int initialized = 0;

int input_init(void) {
    if (initialized) return 1;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0) {
        return 0;
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    char *base=SDL_GetBasePath();
    if(base) { char db[32768]; snprintf(db,sizeof db,"%sassets/gamecontrollerdb.txt",base); SDL_GameControllerAddMappingsFromFile(db); SDL_free(base); }
    initialized = 1;
    return 1;
}

static void disconnect(void) {
    if (controller) { SDL_GameControllerClose(controller); controller = NULL; }
    else if (joystick) { SDL_JoystickClose(joystick); }
    joystick = NULL;
    active_index = -1;
    wcscpy(current_name, L"Nenhum controle conectado");
}

void input_close(void) {
    disconnect();
    if (initialized) {
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
        initialized = 0;
    }
}

static void guid_string(SDL_JoystickGUID guid, char out[33]) {
    SDL_JoystickGetGUIDString(guid, out, 33);
}

int input_devices(InputDevice *out, int capacity) {
    if (!initialized) return 0;
    int count = 0;
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n && count < capacity; i++) {
        InputDevice *d = &out[count++];
        d->id = i;
        d->mapped = SDL_IsGameController(i);
        SDL_JoystickGUID g = SDL_JoystickGetDeviceGUID(i);
        guid_string(g, d->guid);
        const char *device_path=SDL_JoystickPathForIndex(i);
        snprintf(d->key, sizeof d->key, "%s", device_path&&device_path[0]?device_path:d->guid);
        const char *name = d->mapped ? SDL_GameControllerNameForIndex(i) : SDL_JoystickNameForIndex(i);
        if (!name) name = "Controle Conectado";
        MultiByteToWideChar(CP_UTF8, 0, name, -1, d->name, 180);
        d->name[179] = 0;
    }
    return count;
}

void input_select(const char *guid) {
    snprintf(preference, sizeof preference, "%s", guid ? guid : "");
    disconnect();
}

const char *input_preferred(void) {
    return preference;
}

void input_update(void) {
    if (!initialized) return;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {}

    if (joystick && !SDL_JoystickGetAttached(joystick)) {
        disconnect();
    }
    if (joystick) return;

    InputDevice devs[32];
    int n = input_devices(devs, 32);
    for (int i = 0; i < n; i++) {
        if (preference[0] && strcmp(preference, devs[i].key) != 0) continue;
        if (devs[i].mapped) {
            controller = SDL_GameControllerOpen(devs[i].id);
            if (controller) {
                joystick = SDL_GameControllerGetJoystick(controller);
                active_index = devs[i].id;
                wcscpy(current_name, devs[i].name);
                break;
            }
        } else {
            joystick = SDL_JoystickOpen(devs[i].id);
            if (joystick) {
                active_index = devs[i].id;
                wcscpy(current_name, devs[i].name);
                break;
            }
        }
    }
}

void input_configure(const int b[11]) {
    memcpy(bindings, b, sizeof bindings);
}

void input_set_deadzone(int v) {
    deadzone = v < 5 ? 5 : v > 80 ? 80 : v;
}

int input_deadzone(void) {
    return deadzone;
}

static int check_btn(int action, SDL_GameControllerButton standard, int fallback) {
    int raw = (action >= 0 && action < 11) ? bindings[action] : -1;
    if (raw >= 0 && joystick) {
        return (raw < SDL_JoystickNumButtons(joystick) && SDL_JoystickGetButton(joystick, raw));
    }
    if (controller && standard != SDL_CONTROLLER_BUTTON_INVALID) {
        return SDL_GameControllerGetButton(controller, standard);
    }
    if (joystick && fallback >= 0) {
        return (fallback < SDL_JoystickNumButtons(joystick) && SDL_JoystickGetButton(joystick, fallback));
    }
    return 0;
}

InputState input_read(void) {
    InputState s = {0};
    if (!joystick) return s;
    s.connected = 1;

    int limit = 32767 * deadzone / 100;
    Sint16 x = 0, y = 0;
    Uint8 hat = (SDL_JoystickNumHats(joystick) > 0) ? SDL_JoystickGetHat(joystick, 0) : 0;

    if (controller) {
        x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
        y = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP))    s.pad |= INPUT_UP;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  s.pad |= INPUT_DOWN;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  s.pad |= INPUT_LEFT;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) s.pad |= INPUT_RIGHT;
    } else {
        if (SDL_JoystickNumAxes(joystick) > 0) x = SDL_JoystickGetAxis(joystick, 0);
        if (SDL_JoystickNumAxes(joystick) > 1) y = SDL_JoystickGetAxis(joystick, 1);
    }

    if (y < -limit || (hat & SDL_HAT_UP))    s.pad |= INPUT_UP;
    if (y > limit  || (hat & SDL_HAT_DOWN))  s.pad |= INPUT_DOWN;
    if (x < -limit || (hat & SDL_HAT_LEFT))  s.pad |= INPUT_LEFT;
    if (x > limit  || (hat & SDL_HAT_RIGHT)) s.pad |= INPUT_RIGHT;

    /* Custom directional overrides if bound */
    if (bindings[4] >= 0 && bindings[4] < SDL_JoystickNumButtons(joystick) && SDL_JoystickGetButton(joystick, bindings[4])) s.pad |= INPUT_UP;
    if (bindings[5] >= 0 && bindings[5] < SDL_JoystickNumButtons(joystick) && SDL_JoystickGetButton(joystick, bindings[5])) s.pad |= INPUT_DOWN;
    if (bindings[6] >= 0 && bindings[6] < SDL_JoystickNumButtons(joystick) && SDL_JoystickGetButton(joystick, bindings[6])) s.pad |= INPUT_LEFT;
    if (bindings[7] >= 0 && bindings[7] < SDL_JoystickNumButtons(joystick) && SDL_JoystickGetButton(joystick, bindings[7])) s.pad |= INPUT_RIGHT;

    /* NES buttons: B = Run/Tail, A = Jump */
    if (check_btn(0, SDL_CONTROLLER_BUTTON_X, 2)) s.pad |= INPUT_B; // X or Square = B
    if (check_btn(1, SDL_CONTROLLER_BUTTON_A, 0)) s.pad |= INPUT_A; // A or Cross = A
    if (check_btn(2, SDL_CONTROLLER_BUTTON_BACK, 6)) s.pad |= INPUT_SELECT; // Back/Share = Select
    if (check_btn(3, SDL_CONTROLLER_BUTTON_START, 7)) s.pad |= INPUT_START; // Start/Options = Start

    /* Turbos */
    if (check_btn(8, SDL_CONTROLLER_BUTTON_Y, 3)) s.pad |= INPUT_TURBO_B; // Y or Triangle = Turbo B
    if (check_btn(9, SDL_CONTROLLER_BUTTON_B, 1)) s.pad |= INPUT_TURBO_A; // B or Circle = Turbo A

    s.menu = 0;
    if (check_btn(10, SDL_CONTROLLER_BUTTON_GUIDE, 8)) {
        s.menu |= INPUT_MENU;
    }
    return s;
}

const wchar_t *input_name(void) {
    return current_name;
}

int input_button_count(void) {
    return joystick ? SDL_JoystickNumButtons(joystick) : 0;
}

int input_first_pressed(void) {
    if (!joystick) return -1;
    int num = SDL_JoystickNumButtons(joystick);
    for (int i = 0; i < num; i++) {
        if (SDL_JoystickGetButton(joystick, i)) return i;
    }
    return -1;
}

void input_default_label(int action, wchar_t *out, int capacity) {
    if (action >= 4 && action <= 7) {
        swprintf(out, capacity, L"Direcional / Analgico");
        return;
    }
    if (!joystick) {
        swprintf(out, capacity, L"Automtico");
        return;
    }
    if (!controller) {
        int defaults[] = {2, 0, 6, 7, -1, -1, -1, -1, 3, 1, 8};
        int btn = (action >= 0 && action < 11) ? defaults[action] : -1;
        swprintf(out, capacity, L"Boto fsico %d", btn);
        return;
    }
    const wchar_t *labels[] = {
        L"X / Quadrado (Boto B)",
        L"A / Cruz (Boto A)",
        L"Back / Share (Select)",
        L"Start / Options (Start)",
        L"D-Pad Cima",
        L"D-Pad Baixo",
        L"D-Pad Esquerda",
        L"D-Pad Direita",
        L"Y / Tringulo (Turbo B)",
        L"B / Círculo (Turbo A)", L"Guide / Home (Menu)"
    };
    if (action >= 0 && action < 11) {
        swprintf(out, capacity, L"%ls", labels[action]);
    } else {
        swprintf(out, capacity, L"Boto");
    }
}

