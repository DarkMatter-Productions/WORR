/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/snapshot_projection.h"
#include "shared/snapshot_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

void CL_NativeDemoRecorderInit(void);
void CL_NativeDemoRecorderCleanup(void);
void CL_NativeDemoRecorderMapBoundary(void);
void CL_NativeDemoRecorderObserveSnapshot(
    const worr_snapshot_projection_view_v2 *view,
    const worr_snapshot_projection_hashes_v2 *hashes,
    worr_snapshot_ref_v2 projection_ref);

#ifdef __cplusplus
}
#endif
