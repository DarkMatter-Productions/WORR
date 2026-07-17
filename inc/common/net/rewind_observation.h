/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef WORR_COMMON_NET_REWIND_OBSERVATION_H
#define WORR_COMMON_NET_REWIND_OBSERVATION_H

#include "common/net/rewind.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_REWIND_OBSERVATION_VERSION 1u

typedef enum worr_rewind_weapon_policy_v1_e {
  WORR_REWIND_WEAPON_UNSPECIFIED = 0,
  WORR_REWIND_WEAPON_MACHINEGUN = 1,
  WORR_REWIND_WEAPON_CHAINGUN = 2,
  WORR_REWIND_WEAPON_SHOTGUN = 3,
  WORR_REWIND_WEAPON_SUPER_SHOTGUN = 4,
  WORR_REWIND_WEAPON_RAILGUN = 5,
  WORR_REWIND_WEAPON_DISRUPTOR_CONVERGENCE = 6,
  WORR_REWIND_WEAPON_PLASMA_BEAM = 7,
  WORR_REWIND_WEAPON_THUNDERBOLT = 8,
  // This is a current-world spawn-forward policy, not a historical rocket
  // impact or splash query.
  WORR_REWIND_WEAPON_ROCKET_SPAWN_FORWARD = 9,
  // Plasma Gun follows the same bounded current-world spawn policy. Its
  // direct hit and small-radius splash remain production current authority.
  WORR_REWIND_WEAPON_PLASMA_GUN_SPAWN_FORWARD = 10,
  // The straight Blaster bolt family (Blaster and HyperBlaster) consumes
  // authenticated spawn delay in the current world; it has no historical
  // projectile contact or radius authority.
  WORR_REWIND_WEAPON_BLASTER_BOLT_SPAWN_FORWARD = 11,
  // Chainfist uses historical player reach/FOV eligibility, bounded by a
  // current-world displacement guard and current-world occlusion/damage.
  WORR_REWIND_WEAPON_CHAINFIST_MELEE_HYBRID = 12,
  // ETF flechettes may consume bounded authenticated spawn delay in the
  // current world; direct contact and damage stay production-owned.
  WORR_REWIND_WEAPON_ETF_FLECHETTE_SPAWN_FORWARD = 13,
  // Phalanx consumes bounded authenticated launch delay in the current world;
  // direct contact and splash stay on its normal projectile lifecycle.
  WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD = 14,
  // Grenade Launcher advances only along its current-world gravity path. A
  // contact during that bounded interval fails closed to the normal launch;
  // bounce, fuse, splash, and all later interaction remain production-owned.
  WORR_REWIND_WEAPON_GRENADE_LAUNCHER_BALLISTIC_SPAWN_FORWARD = 15,
  // A released hand grenade consumes only the release command's accepted age
  // through a current-world gravity path. Priming, held self-explosion,
  // bounce, fuse, splash, and all later interaction remain production-owned.
  WORR_REWIND_WEAPON_HAND_GRENADE_RELEASE_BALLISTIC_SPAWN_FORWARD = 16,
  // A Proximity Launcher mine consumes bounded accepted launch delay through
  // a clear current-world gravity path only. Landing, arming, trigger scans,
  // explosion, and all later deployable behavior remain production-owned.
  WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD = 17,
  // BFG consumes bounded authenticated launch delay in the current world.
  // Its laser ticks, touch, staged explosion, and radius effects remain
  // production-owned current-world behavior.
  WORR_REWIND_WEAPON_BFG_SPAWN_FORWARD = 18,
  // Ion Ripper consumes bounded authenticated delay for each of the normal
  // fifteen randomized current-world bolt spawns. Ricochet, contact, damage,
  // and lifetime remain production-owned current-world behavior.
  WORR_REWIND_WEAPON_ION_RIPPER_BURST_SPAWN_FORWARD = 19,
  // A released Tesla Mine consumes bounded accepted delay only through its
  // clear current-world gravity path. Landing, activation, targeting,
  // damage, effects, and lifetime remain production-owned current-world
  // deployable behavior.
  WORR_REWIND_WEAPON_TESLA_MINE_RELEASE_BALLISTIC_DEPLOY_FORWARD = 20,
  // A released Trap consumes bounded accepted delay only through its clear
  // current-world gravity path. Landing, capture, release, destruction, and
  // lifetime remain production-owned current-world deployable behavior.
  WORR_REWIND_WEAPON_TRAP_RELEASE_BALLISTIC_DEPLOY_FORWARD = 21,
  // The selected Grapple weapon consumes bounded authenticated delay only
  // through a clear current-world fresh-hook flight. Touch, attachment, pull,
  // damage, reset, and the legacy off-hand Hook command stay current-world.
  WORR_REWIND_WEAPON_GRAPPLE_HOOK_SPAWN_FORWARD = 22,
  // A ProBall Chainfist-held throw consumes only its release command's
  // bounded age through a clear current-world gravity path. Possession,
  // pickup, touch, goal, score, team, and reset behavior remain
  // production-owned current-world logic.
  WORR_REWIND_WEAPON_PROBALL_HELD_THROW_BALLISTIC_SPAWN_FORWARD = 23,
  // The native +hook input consumes its own authenticated command age only
  // through a clear current-world fresh-hook flight. Contact, attachment,
  // pull, damage, reset, and the legacy `hook` string remain current-world.
  WORR_REWIND_WEAPON_OFFHAND_HOOK_SPAWN_FORWARD = 24,
  WORR_REWIND_WEAPON_POLICY_COUNT = 25,
} worr_rewind_weapon_policy_v1;

typedef enum worr_rewind_observation_path_v1_e {
  WORR_REWIND_OBSERVATION_PATH_NONE = 0,
  WORR_REWIND_OBSERVATION_PATH_CANONICAL = 1,
  WORR_REWIND_OBSERVATION_PATH_LEGACY = 2,
  WORR_REWIND_OBSERVATION_PATH_CURRENT_WORLD = 3,
} worr_rewind_observation_path_v1;

typedef enum worr_rewind_observation_outcome_v1_e {
  WORR_REWIND_OBSERVATION_OUTCOME_NONE = 0,
  WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT = 1,
  WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_MISS = 2,
  WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_HIT = 3,
  WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_MISS = 4,
  WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_BLOCKED = 5,
  WORR_REWIND_OBSERVATION_OUTCOME_POLICY_REJECTED = 6,
  WORR_REWIND_OBSERVATION_OUTCOME_HISTORY_MISS = 7,
  WORR_REWIND_OBSERVATION_OUTCOME_SCENE_REJECTED = 8,
} worr_rewind_observation_outcome_v1;

typedef enum worr_rewind_observation_fallback_v1_e {
  WORR_REWIND_OBSERVATION_FALLBACK_NONE = 0,
  WORR_REWIND_OBSERVATION_FALLBACK_MASTER_DISABLED = 1,
  WORR_REWIND_OBSERVATION_FALLBACK_NO_PLAYER_CONTENT = 2,
  WORR_REWIND_OBSERVATION_FALLBACK_SHOOTER_INELIGIBLE = 3,
  WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED = 4,
  WORR_REWIND_OBSERVATION_FALLBACK_POLICY_REJECTED = 5,
  WORR_REWIND_OBSERVATION_FALLBACK_HISTORY_MISS = 6,
  WORR_REWIND_OBSERVATION_FALLBACK_SCENE_REJECTED = 7,
  WORR_REWIND_OBSERVATION_FALLBACK_IGNORE_REJECTED = 8,
  WORR_REWIND_OBSERVATION_FALLBACK_TRACE_VIEW_REJECTED = 9,
  WORR_REWIND_OBSERVATION_FALLBACK_CURRENT_WORLD_BLOCKED = 10,
} worr_rewind_observation_fallback_v1;

enum {
  WORR_REWIND_OBSERVATION_MASTER_ENABLED = 1u << 0,
  WORR_REWIND_OBSERVATION_CANONICAL_CONTEXT = 1u << 1,
  WORR_REWIND_OBSERVATION_POLICY_ACCEPTED = 1u << 2,
  WORR_REWIND_OBSERVATION_HISTORICAL_SCENE = 1u << 3,
  WORR_REWIND_OBSERVATION_HISTORICAL_QUERY = 1u << 4,
  WORR_REWIND_OBSERVATION_CURRENT_FALLBACK = 1u << 5,
  WORR_REWIND_OBSERVATION_SCENE_REUSED = 1u << 6,
  WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED = 1u << 7,
  WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED = 1u << 8,
};

#define WORR_REWIND_OBSERVATION_KNOWN_FLAGS                               \
  ((uint32_t)(WORR_REWIND_OBSERVATION_MASTER_ENABLED |                   \
              WORR_REWIND_OBSERVATION_CANONICAL_CONTEXT |                \
              WORR_REWIND_OBSERVATION_POLICY_ACCEPTED |                  \
              WORR_REWIND_OBSERVATION_HISTORICAL_SCENE |                 \
              WORR_REWIND_OBSERVATION_HISTORICAL_QUERY |                 \
              WORR_REWIND_OBSERVATION_CURRENT_FALLBACK |                 \
              WORR_REWIND_OBSERVATION_SCENE_REUSED |                     \
              WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED |          \
              WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED))

/*
 * One immutable trace/shot-query observation.  The record never contains a
 * client-authored hit conclusion or a live entity pointer.  Zero sequence is
 * accepted as an append template; the journal assigns a nonzero sequence.
 */
typedef struct worr_rewind_observation_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint64_t observation_sequence;
  uint32_t weapon_policy;
  uint32_t path;
  uint32_t outcome;
  uint32_t fallback_reason;
  uint32_t flags;
  uint32_t policy_reason;
  uint32_t query_reason;
  uint32_t candidate_count;
  worr_command_id_v1 command_id;
  worr_snapshot_id_v2 snapshot_id;
  worr_snapshot_id_v2 source_snapshot_id;
  worr_event_entity_ref_v1 hit_entity;
  uint64_t requested_time_us;
  uint64_t mapped_time_us;
  uint64_t applied_time_us;
  uint64_t mapping_error_bound_us;
  uint64_t scene_hash;
  uint64_t authoritative_hash_before;
  uint64_t authoritative_hash_after;
  uint64_t duration_ns;
  float trace_fraction;
  uint32_t reserved0;
  uint64_t reserved1;
} worr_rewind_observation_v1;

/* All counters saturate at UINT64_MAX. */
typedef struct worr_rewind_observation_telemetry_v1_s {
  uint64_t observations;
  uint64_t overwritten;
  uint64_t canonical;
  uint64_t legacy;
  uint64_t current_world;
  uint64_t policy_rejected;
  uint64_t historical_queries;
  uint64_t historical_hits;
  uint64_t historical_misses;
  uint64_t current_fallbacks;
  uint64_t history_misses;
  uint64_t scene_rejected;
  uint64_t authority_guard_checks;
  uint64_t authority_mutations;
  uint64_t duration_total_ns;
  uint64_t duration_max_ns;
} worr_rewind_observation_telemetry_v1;

/* Caller-owned fixed-capacity journal. */
typedef struct worr_rewind_observation_journal_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  worr_rewind_observation_v1 *slots;
  uint32_t capacity;
  uint32_t head;
  uint32_t count;
  uint32_t reserved0;
  uint64_t next_sequence;
  worr_rewind_observation_telemetry_v1 telemetry;
} worr_rewind_observation_journal_v1;

bool Worr_RewindObservationInitV1(worr_rewind_observation_v1 *observation,
                                  uint32_t weapon_policy);
bool Worr_RewindObservationValidateV1(
    const worr_rewind_observation_v1 *observation);
bool Worr_RewindObservationHashV1(
    const worr_rewind_observation_v1 *observation, uint64_t *hash_out);

bool Worr_RewindObservationJournalInitV1(
    worr_rewind_observation_journal_v1 *journal,
    worr_rewind_observation_v1 *storage, uint32_t capacity);
bool Worr_RewindObservationJournalValidateV1(
    const worr_rewind_observation_journal_v1 *journal);
bool Worr_RewindObservationJournalAppendV1(
    worr_rewind_observation_journal_v1 *journal,
    const worr_rewind_observation_v1 *observation,
    uint64_t *assigned_sequence_out);
bool Worr_RewindObservationJournalCopyV1(
    const worr_rewind_observation_journal_v1 *journal,
    worr_rewind_observation_v1 *records_out, uint32_t records_capacity,
    uint32_t *record_count_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_REWIND_OBSERVATION_STATIC_ASSERT static_assert
#else
#define WORR_REWIND_OBSERVATION_STATIC_ASSERT _Static_assert
#endif

WORR_REWIND_OBSERVATION_STATIC_ASSERT(sizeof(float) == 4,
                                      "observation ABI requires float32");
WORR_REWIND_OBSERVATION_STATIC_ASSERT(
    sizeof(worr_rewind_observation_v1) == 160,
    "rewind observation layout changed");
WORR_REWIND_OBSERVATION_STATIC_ASSERT(
    offsetof(worr_rewind_observation_v1, command_id) == 48,
    "rewind observation command offset changed");
WORR_REWIND_OBSERVATION_STATIC_ASSERT(
    offsetof(worr_rewind_observation_v1, requested_time_us) == 80,
    "rewind observation timing offset changed");
WORR_REWIND_OBSERVATION_STATIC_ASSERT(
    offsetof(worr_rewind_observation_v1, trace_fraction) == 144,
    "rewind observation tail offset changed");
WORR_REWIND_OBSERVATION_STATIC_ASSERT(
    sizeof(worr_rewind_observation_telemetry_v1) == 128,
    "rewind observation telemetry layout changed");

#undef WORR_REWIND_OBSERVATION_STATIC_ASSERT

#endif /* WORR_COMMON_NET_REWIND_OBSERVATION_H */
