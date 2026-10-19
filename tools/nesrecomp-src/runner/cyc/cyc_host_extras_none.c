/* cyc_host_extras_none.c - the default cyc_host_extras() for a game without
 * host extras (cyc_host_extras.h). It is in every cycle program's sources and
 * gives way to a game's own definition: an alternate name for the MSVC linker,
 * a weak definition elsewhere. */
#include "cyc_host_extras.h"

#if defined(_MSC_VER)
const CycHostExtras *cyc_host_extras_none(void) { return NULL; }
#if defined(_M_IX86)
#pragma comment(linker, "/alternatename:_cyc_host_extras=_cyc_host_extras_none")
#else
#pragma comment(linker, "/alternatename:cyc_host_extras=cyc_host_extras_none")
#endif
#else
__attribute__((weak)) const CycHostExtras *cyc_host_extras(void) { return NULL; }
#endif
