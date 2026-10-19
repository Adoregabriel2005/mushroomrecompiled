#pragma once
#include <stdint.h>

/* Game capability, independent of the NES's two physical controller ports. */
#ifndef NESRECOMP_INPUT_SEATS
#define NESRECOMP_INPUT_SEATS 2
#endif
#if NESRECOMP_INPUT_SEATS < 2 || NESRECOMP_INPUT_SEATS > 4
#error NESRECOMP_INPUT_SEATS must be between 2 and 4
#endif

/* One-based seats. Published once per outer frame after script overrides. */
extern uint8_t g_logical_input[4];
uint8_t nes_input_seat(int seat);
