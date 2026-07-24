/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FR-10-T13 default-off client native-demo PLAYER.
 *
 * Symmetric to the native-demo recorder: instead of observing live snapshots
 * and writing a WDM1 stream, it loads a WDM1 stream, builds the present-once
 * timeline plan, and drives its execution cursor forward against a demo clock,
 * feeding each due canonical snapshot to the cgame timeline through the same
 * native admission consumer the live snapshot shadow uses.
 *
 * Everything is gated behind the default-off cvar cl_native_demo_play and the
 * explicit `native_play` command; with defaults nothing loads, no per-frame
 * work runs, and no existing path is touched.  A fully playable on-screen
 * session additionally requires the standalone bootstrap / map-load seam (still
 * open): when no cgame timeline consumer is available the player advances and
 * decodes the plan but presents nothing, rather than driving an uninitialized
 * client.
 */
void CL_NativeDemoPlayerInit(void);
void CL_NativeDemoPlayerCleanup(void);

/* Per-client-frame driver; a strict no-op unless a native demo is loaded and
 * cl_native_demo_play is set.  Safe to call every frame. */
void CL_NativeDemoPlayerFrame(void);

#ifdef __cplusplus
}
#endif
