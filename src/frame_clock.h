#pragma once
#include <stdint.h>
typedef struct {
    double debt, step;
    uint64_t dropped;
} FrameClock;
void frame_clock_init(FrameClock *clock, double hz);
unsigned frame_clock_advance(FrameClock *clock, double elapsed);
