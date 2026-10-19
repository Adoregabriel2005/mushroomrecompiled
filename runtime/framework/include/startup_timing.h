/* Opt-in startup diagnostics. No output unless NESRECOMP_BOOT_TIMING is set. */
#ifndef NESRECOMP_STARTUP_TIMING_H
#define NESRECOMP_STARTUP_TIMING_H
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

static Uint64 s_startup_ticks;
static int s_startup_timing, s_startup_first_frame;

static void startup_timing_begin(void) {
    const char *enabled = getenv("NESRECOMP_BOOT_TIMING");
    s_startup_timing = enabled && enabled[0] && enabled[0] != '0';
    s_startup_ticks = s_startup_timing ? SDL_GetPerformanceCounter() : 0;
    s_startup_first_frame = s_startup_timing;
}

static void startup_timing_mark(const char *phase) {
    if (!s_startup_timing) return;
    double ms = (double)(SDL_GetPerformanceCounter() - s_startup_ticks) * 1000.0 /
                (double)SDL_GetPerformanceFrequency();
    fprintf(stderr, "[startup] %8.1f ms  %s\n", ms, phase);
    fflush(stderr);
}
#endif
