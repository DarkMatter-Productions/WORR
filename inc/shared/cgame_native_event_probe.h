/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V1 \
    "WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V1"
#define WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V2 \
    "WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V2"
#define WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION 1u
#define WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION_V2 2u
#define WORR_CGAME_NATIVE_EVENT_PROBE_STATUS_VERSION 3u
#define WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_VERSION 1u
#define WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT 8u
#define WORR_CGAME_NATIVE_EVENT_PROBE_RAW_PENDING_CAPACITY 512u

typedef uint32_t worr_cgame_native_event_probe_kind_v1;
enum {
    WORR_CGAME_NATIVE_EVENT_PROBE_KIND_NONE = 0u,
    WORR_CGAME_NATIVE_EVENT_PROBE_KIND_LEGACY_ENTITY = 1u,
    WORR_CGAME_NATIVE_EVENT_PROBE_KIND_LEGACY_TEMP = 2u,
    WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE = 3u,
    WORR_CGAME_NATIVE_EVENT_PROBE_KIND_SPATIAL_AUDIO = 4u,
    WORR_CGAME_NATIVE_EVENT_PROBE_KIND_DAMAGE = 5u,
    WORR_CGAME_NATIVE_EVENT_PROBE_KIND_HELP_PATH = 6u,
    WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI = 7u,
};

typedef uint32_t worr_cgame_native_event_probe_legacy_disposition_v1;
enum {
    WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED = 1u,
    WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_SUPPRESSED = 2u,
};

/*
 * One value-only parity row. Without a V2 checkpoint its counters are
 * map-scoped. After a successful V2 checkpoint they cover only the new audit
 * window. The raw lane is captured from accepted exact legacy action adapters
 * before presentation. The probe and native lanes are recorded only after the
 * event runtime commits its presenter callback. Hashes are lane-neutral
 * FNV-1a chains over the same presenter kind and a presentation-equivalence
 * semantic hash. Schema 3 preserves canonical payload, routing, tick,
 * ordinal, and expiry semantics while excluding source_time_us, and binds
 * non-POI projections to the exact retained snapshot ID/hash. Keyed POI is a
 * presentation-state action: its projection retains map epoch and canonical
 * payload while neutralizing routing, tick, ordinal, expiry, snapshot
 * sequence, and snapshot hash so a raw reliable action and its following-
 * snapshot native EVENT compare as the same presentation. Legacy action
 * adapters infer omitted time from the client's frame clock, whereas a
 * snapshot-fenced EVENT retains the producer's simulation clock.
 */
typedef struct worr_cgame_native_event_probe_status_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t kind_count;
    uint32_t map_generation;
    uint32_t map_end_count;
    uint32_t map_active;
    uint32_t probe_requested;
    uint32_t probe_latched;
    uint32_t probe_active;
    uint32_t effect_authority_enabled;
    uint32_t resources_required;
    uint32_t legacy_owner_active;
    uint32_t raw_pending_count;
    uint32_t authority_epoch;
    uint32_t authority_requires_resync;
    uint32_t authority_degraded;

    uint64_t raw_action_records;
    uint64_t raw_action_chain_hash;
    uint64_t raw_effect_dispatches;
    uint64_t raw_effect_chain_hash;
    uint64_t raw_effect_suppressions;
    uint64_t raw_pair_failures;
    uint64_t probe_action_commits;
    uint64_t probe_action_chain_hash;
    uint64_t probe_effects_suppressed;
    uint64_t probe_nonvisual_commits;
    uint64_t native_effect_dispatches;
    uint64_t native_effect_chain_hash;
    uint64_t presenter_commit_mismatches;
    uint64_t authoritative_presentations;
    uint64_t authoritative_duplicates;
    uint64_t authoritative_conflicts;
    uint64_t authority_ref_body_joins;
    uint64_t legacy_ref_body_mismatches;

    uint64_t raw_action_by_kind
        [WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT];
    uint64_t probe_action_by_kind
        [WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT];
} worr_cgame_native_event_probe_status_v1;

/* Process-local, main-thread, synchronous extension. A completion consumes
 * only the exact FIFO head for carrier_kind; mismatches fail closed and leave
 * that token pending so the caller cannot accidentally pair a later action. */
typedef struct worr_cgame_native_event_probe_export_v1_s {
    uint32_t struct_size;
    uint32_t api_version;
    bool (*CompleteLegacyDispatch)(
        uint32_t carrier_kind,
        worr_cgame_native_event_probe_legacy_disposition_v1 disposition);
    bool (*GetStatus)(worr_cgame_native_event_probe_status_v1 *status_out);
} worr_cgame_native_event_probe_export_v1;

typedef uint32_t worr_cgame_native_event_probe_checkpoint_result_v1;
enum {
    WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_INVALID_ARGUMENT = 0u,
    WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED = 1u,
    WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED = 2u,
    WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_NOT_READY = 3u,
    WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_STALE_MAP = 4u,
    WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_STALE_AUTHORITY = 5u,
    WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_CONFLICT = 6u,
    WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_BUSY = 7u,
    WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_UNHEALTHY = 8u,
};

/* Value-only acknowledgement for one map/authority-bound audit checkpoint.
 * A successful checkpoint makes status-v1 activity counters window-relative
 * without resetting authority, journal, snapshot, or presentation state. */
typedef struct worr_cgame_native_event_probe_checkpoint_receipt_v1_s {
    uint32_t struct_size;
    uint32_t schema_version;
    worr_cgame_native_event_probe_checkpoint_result_v1 result;
    uint32_t observed_map_generation;
    uint32_t observed_authority_epoch;
    uint32_t reserved0;
    uint64_t checkpoint_id;
} worr_cgame_native_event_probe_checkpoint_receipt_v1;

/* V2 is a distinct extension so the released V1 table remains byte-for-byte
 * compatible. Checkpoint is synchronous, main-thread, exactly-once per map;
 * replaying the exact tuple is idempotent and never resets later evidence. */
typedef struct worr_cgame_native_event_probe_export_v2_s {
    uint32_t struct_size;
    uint32_t api_version;
    bool (*CompleteLegacyDispatch)(
        uint32_t carrier_kind,
        worr_cgame_native_event_probe_legacy_disposition_v1 disposition);
    bool (*GetStatus)(worr_cgame_native_event_probe_status_v1 *status_out);
    bool (*Checkpoint)(
        uint32_t expected_map_generation,
        uint32_t expected_authority_epoch,
        uint64_t checkpoint_id,
        worr_cgame_native_event_probe_checkpoint_receipt_v1 *receipt_out);
} worr_cgame_native_event_probe_export_v2;

/* `previous_hash == 0` denotes an empty chain. Every appended token is
 * domain-separated by the 19 bytes of "WORR-EVENT-PROBE-V1", followed by a
 * little-endian uint32 presenter kind and little-endian uint64 canonical
 * semantic hash. An empty lane is represented by zero. */
static inline uint64_t Worr_CGameNativeEventProbeChainAppendV1(
    uint64_t previous_hash,
    uint32_t presenter_kind,
    uint64_t presentation_equivalence_hash)
{
    static const uint8_t domain[] = {
        'W', 'O', 'R', 'R', '-', 'E', 'V', 'E', 'N', 'T', '-',
        'P', 'R', 'O', 'B', 'E', '-', 'V', '1'};
    const uint64_t fnv_offset = UINT64_C(14695981039346656037);
    const uint64_t fnv_prime = UINT64_C(1099511628211);
    uint64_t hash = previous_hash ? previous_hash : fnv_offset;
    size_t index;

    for (index = 0; index < sizeof(domain); ++index) {
        hash ^= domain[index];
        hash *= fnv_prime;
    }
    for (index = 0; index < 4u; ++index) {
        hash ^= (uint8_t)(presenter_kind >> (index * 8u));
        hash *= fnv_prime;
    }
    for (index = 0; index < 8u; ++index) {
        hash ^= (uint8_t)(
            presentation_equivalence_hash >> (index * 8u));
        hash *= fnv_prime;
    }
    return hash;
}

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_CGAME_NATIVE_EVENT_PROBE_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_CGAME_NATIVE_EVENT_PROBE_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_CGAME_NATIVE_EVENT_PROBE_STATIC_ASSERT(
    sizeof(worr_cgame_native_event_probe_status_v1) == 336,
    "cgame native event probe status v1 layout changed");
WORR_CGAME_NATIVE_EVENT_PROBE_STATIC_ASSERT(
    offsetof(worr_cgame_native_event_probe_status_v1,
             raw_action_records) == 64,
    "cgame native event probe counter offset changed");
WORR_CGAME_NATIVE_EVENT_PROBE_STATIC_ASSERT(
    offsetof(worr_cgame_native_event_probe_status_v1,
             raw_action_by_kind) == 208,
    "cgame native event probe raw-kind offset changed");
WORR_CGAME_NATIVE_EVENT_PROBE_STATIC_ASSERT(
    offsetof(worr_cgame_native_event_probe_status_v1,
             probe_action_by_kind) == 272,
    "cgame native event probe probe-kind offset changed");
WORR_CGAME_NATIVE_EVENT_PROBE_STATIC_ASSERT(
    sizeof(worr_cgame_native_event_probe_checkpoint_receipt_v1) == 32,
    "cgame native event probe checkpoint receipt layout changed");
WORR_CGAME_NATIVE_EVENT_PROBE_STATIC_ASSERT(
    offsetof(worr_cgame_native_event_probe_checkpoint_receipt_v1,
             checkpoint_id) == 24,
    "cgame native event probe checkpoint id offset changed");
WORR_CGAME_NATIVE_EVENT_PROBE_STATIC_ASSERT(
    sizeof(worr_cgame_native_event_probe_export_v1) ==
        8 + 2 * sizeof(void *),
    "cgame native event probe v1 export layout changed");
WORR_CGAME_NATIVE_EVENT_PROBE_STATIC_ASSERT(
    sizeof(worr_cgame_native_event_probe_export_v2) ==
        8 + 3 * sizeof(void *),
    "cgame native event probe v2 export layout changed");

#undef WORR_CGAME_NATIVE_EVENT_PROBE_STATIC_ASSERT
