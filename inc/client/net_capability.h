/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/capability.h"

#ifdef __cplusplus
extern "C" {
#endif

void CL_NetCapabilityRegisterOffer(void);
void CL_NetCapabilityRefreshOffer(void);
void CL_NetCapabilityReset(uint32_t connection_epoch);
void CL_NetCapabilityShutdown(void);
bool CL_NetCapabilityPacketBegin(void);
bool CL_NetCapabilityPacketEnd(void);

/* Returns true only for one of the reserved capability-confirm settings. */
bool CL_NetCapabilityObserveSetting(int32_t index, int32_t value);
void CL_NetCapabilityObserveInterveningService(void);

uint32_t CL_NetCapabilityNegotiated(void);
uint32_t CL_NetCapabilityOffered(void);
bool CL_NetCapabilityHas(uint32_t capability);
bool CL_NetCapabilityGetState(worr_net_capability_state_v1 *state_out);

#ifdef __cplusplus
}
#endif
