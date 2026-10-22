#include "frame_clock.h"
#include <math.h>
void frame_clock_init(FrameClock *c, double hz) {
    c->debt = 0; c->step = 1.0 / hz; c->dropped = 0;
}
unsigned frame_clock_advance(FrameClock *c, double elapsed) {
    if (!isfinite(elapsed) || elapsed < 0) return 0;
    /* Recover at most eight ticks in one pump. Discard whole overdue ticks,
     * retaining the fractional phase so a stall cannot cause a spiral. */
    double total = c->debt + elapsed;
    double due = floor((total + 1e-9) / c->step);
    unsigned ticks = due > 8 ? 8 : (unsigned)due;
    c->debt = total - due * c->step;
    if (due > 8) c->dropped += (uint64_t)fmin(due - 8, 1000000000.0);
    return ticks;
}
