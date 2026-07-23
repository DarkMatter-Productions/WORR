/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/cgame_event_shadow.h"
#include "q2proto/q2proto.h"

#ifdef __cplusplus
extern "C" {
#endif

void CL_EventShadowSetConsumer(
    const worr_cgame_event_shadow_export_v1 *consumer);
void CL_EventRangeSetConsumerV2(
    const worr_cgame_event_range_export_v2 *consumer);
void CL_EventShadowReset(uint32_t reason);
void CL_EventShadowDeliverAcceptedFrame(void);
void CL_EventRangeCaptureTempV2(
    const q2proto_svc_temp_entity_t *temp_entity);
void CL_EventRangeCaptureMuzzleV2(
    const q2proto_svc_muzzleflash_t *muzzleflash,
    uint32_t family);
void CL_EventRangeCaptureSoundV2(const q2proto_sound_t *sound);
void CL_EventRangeCaptureDamageV2(
    const q2proto_svc_damage_t *damage);
bool CL_EventRangeCapturePOIV2(const q2proto_svc_poi_t *poi);

#ifdef __cplusplus
}
#endif
