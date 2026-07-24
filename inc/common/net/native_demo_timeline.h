/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_demo.h"
#include "common/net/native_demo_playback.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Allocation-free WDM1 present-once timeline planner (FR-10-T13).
 *
 * The playback cursor decodes one canonical body at a time; the decode-at-index
 * family lets a caller reach any command/event/snapshot without disturbing the
 * snapshot-centric Step traversal.  What a live demo/MVD/spectator driver still
 * needs on top of those cores is the *ordering* half of "present-once command
 * and event replay": a single deterministic pass over the whole validated index
 * that fixes the exact dispatch order and proves the T13 replay invariants
 * before any body is presented to a live client:
 *
 *   - every record is visited exactly once, in strictly increasing ordinal
 *     order (no duplicated or skipped record);
 *   - record time is monotonic non-decreasing;
 *   - an EVENT is only ever dispatched after a SNAPSHOT has established a base
 *     (present-once needs an anchor), and at an equal timestamp the anchoring
 *     snapshot precedes the event;
 *   - the resulting plan is reproducible: an order-sensitive fingerprint is
 *     identical across builds of the same stream and changes if any record is
 *     reordered, dropped, or duplicated.
 *
 * This core is pointer-free and opens no files: the caller owns the byte image
 * and the validated index (produced by the scanner / playback open), and owns
 * the step-buffer the plan is written into.  It publishes nothing to a live
 * client and moves no authority; it only fixes and validates the dispatch order
 * a live driver will later consume.
 */
#define WORR_NATIVE_DEMO_TIMELINE_ABI_VERSION 1u

typedef enum worr_native_demo_timeline_result_v1_e {
  WORR_NATIVE_DEMO_TIMELINE_OK = 0,
  WORR_NATIVE_DEMO_TIMELINE_INVALID_ARGUMENT = 1,
  WORR_NATIVE_DEMO_TIMELINE_INVALID_CONFIG = 2,
  WORR_NATIVE_DEMO_TIMELINE_INVALID_METADATA = 3,
  WORR_NATIVE_DEMO_TIMELINE_UNSUPPORTED = 4,
  WORR_NATIVE_DEMO_TIMELINE_CAPACITY = 5,
  WORR_NATIVE_DEMO_TIMELINE_LIMIT = 6,
  WORR_NATIVE_DEMO_TIMELINE_ORDER = 7,
  WORR_NATIVE_DEMO_TIMELINE_MALFORMED = 8,
  WORR_NATIVE_DEMO_TIMELINE_WRONG_CLASS = 9,
} worr_native_demo_timeline_result_v1;

enum {
  /* The plan's first dispatched step. */
  WORR_NATIVE_DEMO_TIMELINE_STEP_FIRST = 1u << 0,
  /* This snapshot became the anchor subsequent events resolve against. */
  WORR_NATIVE_DEMO_TIMELINE_STEP_SNAPSHOT_ANCHOR = 1u << 1,
  /* First step at a timestamp strictly greater than the previous step. */
  WORR_NATIVE_DEMO_TIMELINE_STEP_TIME_ADVANCE = 1u << 2,
};

/* One planned dispatch step: a single index record placed in canonical order. */
typedef struct worr_native_demo_timeline_step_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint8_t record_kind; /* worr_native_demo_record_kind_v1 */
  uint8_t flags;
  uint64_t present_sequence;        /* 0-based dispatch order */
  uint64_t entry_index;             /* index into the caller's entries[] */
  uint64_t ordinal;
  uint64_t time_us;
  uint64_t anchor_snapshot_ordinal; /* most-recent snapshot ordinal at/before
                                     * this step; 0 before the first snapshot */
} worr_native_demo_timeline_step_v1;

enum {
  /* The plan begins with a SNAPSHOT record (a self-contained standalone
   * timeline).  Absent when leading command records precede the first
   * snapshot. */
  WORR_NATIVE_DEMO_TIMELINE_SUMMARY_LEADING_SNAPSHOT = 1u << 0,
};

/* Whole-plan summary, including the order-sensitive reproduction fingerprint. */
typedef struct worr_native_demo_timeline_summary_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t flags;
  uint64_t step_count;
  uint64_t snapshot_count;
  uint64_t command_count;
  uint64_t event_count;
  uint64_t first_ordinal;
  uint64_t last_ordinal;
  uint64_t last_time_us;
  uint64_t plan_fingerprint;
} worr_native_demo_timeline_summary_v1;

typedef struct worr_native_demo_timeline_config_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t flags;
  uint64_t max_steps; /* additional cap; 0 means bounded only by step_capacity */
} worr_native_demo_timeline_config_v1;

/*
 * Builds the present-once dispatch plan for a validated WDM1 index.
 *
 * `entries` must be the exact canonical index the scanner / playback-open
 * produced for `encoded` (its record offsets are revalidated against the byte
 * image, so a mismatched or truncated index fails closed).  On success the plan
 * is written to `steps_out[0 .. *step_count]` in dispatch order and the summary
 * is filled.  On CAPACITY the required step count is written to
 * summary_out->step_count (when non-null) so the caller can resize and retry;
 * `steps_out` and every other output are otherwise untouched on failure.
 *
 * A well-formed recording always yields a full plan (one step per record); a
 * failure means the index is not a canonically ordered, present-once-valid
 * timeline and must not be replayed.
 */
worr_native_demo_timeline_result_v1 Worr_NativeDemoTimelineBuildV1(
    const worr_native_demo_timeline_config_v1 *config, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, worr_native_demo_timeline_step_v1 *steps_out,
    size_t step_capacity, worr_native_demo_timeline_summary_v1 *summary_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_DEMO_TIMELINE_STATIC_ASSERT(condition, message)            \
  static_assert((condition), message)
#else
#define WORR_NATIVE_DEMO_TIMELINE_STATIC_ASSERT(condition, message)            \
  _Static_assert((condition), message)
#endif

WORR_NATIVE_DEMO_TIMELINE_STATIC_ASSERT(
    sizeof(worr_native_demo_timeline_step_v1) == 48,
    "native demo timeline step layout changed");
WORR_NATIVE_DEMO_TIMELINE_STATIC_ASSERT(
    sizeof(worr_native_demo_timeline_summary_v1) == 72,
    "native demo timeline summary layout changed");
WORR_NATIVE_DEMO_TIMELINE_STATIC_ASSERT(
    sizeof(worr_native_demo_timeline_config_v1) == 16,
    "native demo timeline config layout changed");

#undef WORR_NATIVE_DEMO_TIMELINE_STATIC_ASSERT
