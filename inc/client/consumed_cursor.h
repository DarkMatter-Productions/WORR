/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/consumed_cursor_sideband.h"
#include "shared/snapshot_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Packet-scoped adapter for the negotiated legacy SVC_SETTING carrier.
 * PacketBegin is intentionally called even before capability confirmation so
 * a confirmation and the first canonical frame may share one packet safely.
 */
void CL_ConsumedCursorReset(void);
void CL_ConsumedCursorShutdown(void);
bool CL_ConsumedCursorPacketBegin(void);
bool CL_ConsumedCursorPacketEnd(void);
bool CL_ConsumedCursorObserveSetting(int32_t index, int32_t value);
bool CL_ConsumedCursorObserveInterveningService(void);

/*
 * On legacy/non-negotiated traffic this succeeds with the canonical absent
 * value.  Once negotiated, every frame must have one exact adjacent tuple.
 */
bool CL_ConsumedCursorConsumeFrame(
    worr_snapshot_consumed_command_v2 *consumed_command_out);

bool CL_ConsumedCursorGetTelemetry(
    worr_consumed_cursor_sideband_telemetry_v1 *telemetry_out);

/* False only during pre-command bootstrap (or without negotiation).  Once
 * true it remains true until an explicit connection/map/demo reset. */
bool CL_ConsumedCursorCanonicalEstablished(void);

/* Copies the latest authenticated authoritative cursor. */
bool CL_ConsumedCursorGetLatestV1(worr_command_cursor_v1 *cursor_out);

#ifdef __cplusplus
}
#endif
