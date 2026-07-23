/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/legacy_damage_event_candidate.h"
#include "common/net/legacy_game_event_candidate.h"
#include "common/net/legacy_help_path_event_candidate.h"
#include "common/net/legacy_poi_event_candidate.h"
#include "server/snapshot_shadow.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SV_SNAPSHOT_EVENT_CANDIDATES_VERSION 1u

enum {
  SV_SNAPSHOT_SPATIAL_AUDIO_RELIABLE = 1u << 0,
  SV_SNAPSHOT_SPATIAL_AUDIO_NO_PHS = 1u << 1,
  SV_SNAPSHOT_SPATIAL_AUDIO_LOCAL_ONLY = 1u << 2,
};
#define SV_SNAPSHOT_SPATIAL_AUDIO_KNOWN_FLAGS \
  (SV_SNAPSHOT_SPATIAL_AUDIO_RELIABLE | SV_SNAPSHOT_SPATIAL_AUDIO_NO_PHS | \
   SV_SNAPSHOT_SPATIAL_AUDIO_LOCAL_ONLY)

enum {
  SV_SNAPSHOT_GAME_EVENT_RELIABLE = 1u << 0,
};
#define SV_SNAPSHOT_GAME_EVENT_KNOWN_FLAGS \
  SV_SNAPSHOT_GAME_EVENT_RELIABLE

enum {
  /* A lifetime of zero means retain indefinitely and is legal only when the
   * legacy service was accepted by the reliable channel. */
  SV_SNAPSHOT_KEYED_POI_RELIABLE = 1u << 0,
};
#define SV_SNAPSHOT_KEYED_POI_KNOWN_FLAGS \
  SV_SNAPSHOT_KEYED_POI_RELIABLE

typedef enum sv_snapshot_event_candidates_result_v1_e {
  SV_SNAPSHOT_EVENT_CANDIDATES_OK = 0,
  SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT = 1,
  SV_SNAPSHOT_EVENT_CANDIDATES_STALE_REF = 2,
  SV_SNAPSHOT_EVENT_CANDIDATES_CAPACITY = 3,
  SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_PROJECTION = 4,
  SV_SNAPSHOT_EVENT_CANDIDATES_SEMANTIC_MISMATCH = 5,
  SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE = 6,
  SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_CANDIDATE = 7,
} sv_snapshot_event_candidates_result_v1;

/*
 * Copies ID-less typed event candidates retained with an exact final-emission
 * snapshot.  No process-local pointer escapes the peer.  A capacity result
 * reports the required count without changing candidates_out; invalid or
 * stale calls change neither output.  Slot overwrite invalidates candidates
 * through the same generation check as SV_SnapshotShadowViewV1.
 */
sv_snapshot_event_candidates_result_v1 SV_SnapshotShadowCopyEventCandidatesV1(
    const sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    worr_event_record_v1 *candidates_out, uint32_t candidate_capacity,
    uint32_t *candidate_count_out);

/*
 * Rebinds one decoded spatial-audio action template to an exact final-emission
 * snapshot. A visible source uses its snapshot identity. An off-frame source
 * is admitted only when the final sound explicitly carries a position; it is
 * world-anchored and marked POSITION_FORCED while its raw entity remains in
 * the payload. candidate_out remains unchanged on failure.
 */
sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildSpatialAudioCandidateV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_sound_t *sound,
    worr_event_record_v1 *candidate_out);

/* Additive structured-delivery form. Reliable audio becomes a non-expiring
 * RELIABLE_ORDERED event. LOCAL_ONLY audio is world-anchored because its raw
 * entity/channel is a client-local override key, not a spatial-lineage claim.
 * Unknown flags and every failed bind leave output unchanged. */
sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildSpatialAudioCandidateWithDeliveryV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_sound_t *sound,
    uint32_t delivery_flags, worr_event_record_v1 *candidate_out);

/*
 * Rebinds one decoded player or monster muzzleflash to an entity identity from
 * an exact final-emission snapshot. The source must be present in that
 * snapshot; candidate_out remains unchanged on failure.
 */
sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildMuzzleCandidateV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_svc_muzzleflash_t *muzzleflash,
    uint32_t family, worr_event_record_v1 *candidate_out);

/* Rebinds an ordered bounded muzzle sequence against one exact final snapshot.
 * Every carrier must resolve its source in that view or candidates_out remains
 * unchanged. */
sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildMuzzleCandidatesV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_svc_muzzleflash_t *muzzleflashes,
    const uint32_t *families, uint32_t muzzle_count,
    worr_event_record_v1 *candidates_out);

/* Rebinds an ordered mixed direct-game-event batch to one exact final snapshot.
 * The entire output remains untouched unless every carrier's lineage is valid. */
sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildGameEventCandidatesV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities,
    const worr_legacy_game_event_candidate_carrier_v1 *carriers,
    uint32_t carrier_count, worr_event_record_v1 *candidates_out);

/* Additive delivery-aware form for a complete legacy direct-game-event
 * message. A reliable message becomes a FIFO, non-expiring native batch;
 * unknown flags or any failed member leave the whole output unchanged. */
sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildGameEventCandidatesWithDeliveryV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities,
    const worr_legacy_game_event_candidate_carrier_v1 *carriers,
    uint32_t carrier_count, uint32_t delivery_flags,
    worr_event_record_v1 *candidates_out);

/* Rebinds every indicator in one decoded per-client damage carrier to the
 * controlled entity identity from an exact final-emission snapshot. The wire
 * format has no attacker identity, so candidates retain their honest world
 * source. The complete bounded batch and count remain unchanged on failure. */
sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildDamageCandidatesV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_svc_damage_t *damage,
    worr_event_record_v1 *candidates_out, uint32_t candidate_capacity,
    uint32_t *candidate_count_out);

/* Rebinds one per-client help-path marker to the controlled entity identity
 * from an exact final-emission snapshot. The carrier is world-positioned and
 * has no emitter identity; candidate_out remains unchanged on failure. */
sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildHelpPathCandidateV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_svc_help_path_t *help_path,
    worr_event_record_v1 *candidate_out);

/* Rebinds one keyed per-client POI to the controlled entity identity from an
 * exact final-emission snapshot. The raw service has no emitter identity, so
 * its canonical source remains the world. This default unreliable form
 * rejects an infinite lifetime; output is unchanged on every failure. */
sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildKeyedPOICandidateV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_svc_poi_t *poi,
    worr_event_record_v1 *candidate_out);

/* Delivery-aware form used by the deferred reliable FIFO. Reliable admission
 * keeps the action template's non-expiring RELIABLE_ORDERED delivery and
 * permits lifetime_ms == 0. Accepted unreliable delivery rewrites to
 * TRANSIENT/source_tick+1; delete and finite lifetimes are legal on either
 * path. */
sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildKeyedPOICandidateWithDeliveryV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_svc_poi_t *poi,
    uint32_t delivery_flags, worr_event_record_v1 *candidate_out);

/*
 * Rebinds a decoded temporary-entity template to the world or exact visible
 * source/subject identities of an emitted final snapshot. Entity-free effects
 * use the snapshot's stable world reference. Source or subject entities that
 * are absent from the exact view are rejected; candidate_out is unchanged.
 */
sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildTempCandidateV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_svc_temp_entity_t *temp_entity,
    worr_event_record_v1 *candidate_out);

/*
 * Rebinds a short ordered sequence of decoded temporary entities to one exact
 * final-emission snapshot. The whole batch is failure-atomic: no output is
 * changed unless every carrier can be built and bound. Count is bounded by the
 * shared raw sequence maximum.
 */
sv_snapshot_event_candidates_result_v1
SV_SnapshotShadowBuildTempCandidatesV1(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 ref,
    uint32_t max_entities, const q2proto_svc_temp_entity_t *temp_entities,
    uint32_t temp_count, worr_event_record_v1 *candidates_out);

#ifdef __cplusplus
}
#endif
