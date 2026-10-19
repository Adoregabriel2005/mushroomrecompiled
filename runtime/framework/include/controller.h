#pragma once
#include <stdint.h>
#include <SDL.h>

/*
 * controller.h — SDL2 game-controller (gamepad) support.
 *
 * Cross-platform via SDL's SDL_GameController API: Xbox, PlayStation, Switch
 * Pro and generic pads are recognized through SDL's built-in mapping database
 * (on Windows this sits on top of XInput/DirectInput). Legacy titles use two
 * physical ports. Games opting into extra logical seats use the launcher's
 * explicit device choices, then assign unused pads to automatic gamepad seats.
 * Hotplug preserves other seats and leaves a disconnected seat neutral.
 */

/* Initialize the game-controller subsystem and open any already-connected
 * pads. Call once, after SDL_Init. Safe to call with no controllers attached. */
void controller_init(void);

/* Feed SDL events here so device add/remove (hotplug) is handled. */
void controller_handle_event(const SDL_Event *ev);

/* Return the NES controller byte for a one-based logical seat, or 0
 * if no pad is assigned to that player. Bit layout matches keybinds_read_player:
 * A=0x80 B=0x40 SELECT=0x20 START=0x10 UP=0x08 DOWN=0x04 LEFT=0x02 RIGHT=0x01. */
uint8_t controller_read_player(int player);

/* True when an SDL controller instance belongs to the requested NES port.
 * Camera mods use this to keep direct right-stick events on the same device
 * selected for ordinary movement and buttons. */
int controller_instance_is_player(SDL_JoystickID instance_id, int player);

/* Number of controllers currently connected. */
int controller_count(void);

/* Close all controllers and release resources. */
void controller_shutdown(void);
