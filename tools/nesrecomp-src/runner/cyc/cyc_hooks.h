/*
 * cyc_hooks.h - trusted mod callbacks at the program's hook sites.
 *
 * A game declares the sites in game.toml:
 *
 *     [[mod_function_hook]]
 *     id    = "smb2j.widescreen.area-parser"   # the plugin that registers here
 *     addr  = 0x9123
 *     bytes = "A5 0E 48 20"                    # the code that must be there
 *
 * and a statically linked plugin registers a callback for that id with
 * runner/include/mod_function_hooks.h (nes_mod_register_function_entry_plugin),
 * disabled until its mod activates it. The recompiler compiles each site as
 * an instruction boundary that returns to the scheduler while any hook is
 * enabled (cyc_hook_stop below; a program without sites compiles nothing).
 * The scheduler (cyc_run.c) then runs the enabled callbacks of the sites at
 * cpu.pc whose `bytes` memory holds right now - the content key is what makes
 * a site in RAM mean one routine: an FDS program loads different files at the
 * same address - and dispatches the instruction as usual. The same happens
 * before an interpreted instruction, so recompiled code, RAM views and the
 * interpreter all fire a site identically.
 *
 * A callback runs at the instruction boundary, before the site's opcode fetch,
 * with the machine exactly as the program left it (cyc_mod.h reads and writes
 * it, and can run the program's own routines in isolation). No site fires
 * while an interrupt is about to be taken there: the program is not entering
 * the site's instruction yet, and the site fires when it does (after RTI).
 *
 * Returning nonzero means the callback handled the routine: the site must be
 * a subroutine entry, and the scheduler returns from it the way its RTS would
 * (pull the return address, continue after the JSR), without the RTS's bus
 * cycles. Zero runs the original instruction.
 *
 * A plugin registered for an id (or, without one, an address) that no site of
 * this program declares is a build error, refused loudly at start
 * (cyc_hooks_validate). Every site keeps counts (fired, handled, content
 * mismatches), and a frame in which a site fired leaves a MOD_HOOK ring event.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "cpu6502.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Nonzero while some registered hook is enabled and no isolated guest call
 * (cyc_mod.h) is running. */
extern uint8_t cyc_hooks_armed;
/* The site the scheduler just ran callbacks for: its compiled instruction lets
 * execution through once. -1: none. */
extern int32_t cyc_hook_passed;
/* Set when compiled code stopped at a site; the dispatch loops return to the
 * scheduler on it (it is cleared there). */
extern bool cyc_hook_hit;

/* Generated code, at a site's instruction. */
static inline bool cyc_hook_stop(uint16_t pc)
{
    if (!cyc_hooks_armed || (cpu.do_nmi | cpu.do_irq | cpu.do_reset)) return false;
    if (cyc_hook_passed == (int32_t)pc) {
        cyc_hook_passed = -1;
        return false;
    }
    cyc_hook_hit = true;
    return true;
}

/* ---- the scheduler's side (cyc_run.c) ---- */

/* Bind the registered plugins to the program's sites; false (after printing
 * why) when a plugin names a site the program does not have. */
bool cyc_hooks_validate(void);
/* A site is at pc and its callbacks should run now. */
bool cyc_hooks_due(uint16_t pc);
/* Run the enabled callbacks of the sites at pc whose content key memory holds.
 * May change cpu.pc (a handled routine returned). */
void cyc_hooks_fire(uint16_t pc);
/* Isolated guest calls turn firing off and back on. */
void cyc_hooks_suspend(bool suspended);
/* End of frame: one MOD_HOOK ring event per site that fired. */
void cyc_hooks_frame_end(void);

typedef struct {
    uint64_t fired;       /* callbacks run */
    uint64_t handled;     /* ... that returned from the routine */
    uint64_t mismatched;  /* the site's address came up with other code there, where the
                             scheduler looked: interpreted code (a compiled view of other
                             code has no exit at the site and never stops there) */
} CycHookStats;
/* Per site (index into cyc_native_hook_sites), or the total for -1. */
void cyc_hooks_stats(int site, CycHookStats *out);

#ifdef __cplusplus
}
#endif
