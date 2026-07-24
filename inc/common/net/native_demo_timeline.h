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
  WORR_NATIVE_DEMO_TIMELINE_INVALID_STATE = 10,
  WORR_NATIVE_DEMO_TIMELINE_NOT_FOUND = 11,
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

/*
 * Present-once execution cursor over a built plan (FR-10-T13).
 *
 * The planner fixes the static dispatch order; this cursor is the execution
 * half a live demo/MVD/spectator driver ticks against it.  It advances a
 * present-once frontier: a step is dispatched exactly once and only forward, so
 * a paused clock (no time advance) dispatches nothing, a clock that goes
 * backwards without an explicit reset dispatches nothing (no event re-fire on a
 * rewind), and a Reset re-arms the frontier from the start with a new
 * generation for an explicit seek.  The cursor is pointer-free: the plan step
 * array is caller-owned and supplied on every call, and its fingerprint is
 * revalidated so a caller cannot silently substitute a different plan.  This
 * core drives no live client and reads no snapshot bodies; it only decides
 * which already-decoded steps to present this tick.
 */
enum {
  WORR_NATIVE_DEMO_TIMELINE_CURSOR_INITIALIZED = 1u << 0,
};

typedef struct worr_native_demo_timeline_cursor_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t state_flags;
  uint32_t reset_generation;
  uint32_t reserved0;
  uint64_t plan_fingerprint;      /* binds the cursor to one exact plan */
  uint64_t step_count;
  uint64_t next_step;             /* present-once frontier: next step to emit */
  uint64_t presented_count;
  uint64_t last_presented_time_us;
  uint64_t last_snapshot_ordinal; /* anchor of the most recently emitted step */
  uint64_t reserved1;
} worr_native_demo_timeline_cursor_v1;

/* One contiguous batch of steps to present this tick: [first_step, first_step +
 * count).  count == 0 means nothing is due (paused clock, backward target, or
 * end of plan). */
typedef struct worr_native_demo_timeline_batch_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t flags;
  uint64_t first_step;
  uint64_t count;
  uint64_t last_time_us;
  uint64_t last_snapshot_ordinal;
} worr_native_demo_timeline_batch_v1;

/*
 * Binds a cursor to a plan described by `summary` (its fingerprint and step
 * count) and arms the frontier at the start.  cursor_out is untouched on
 * failure.
 */
worr_native_demo_timeline_result_v1 Worr_NativeDemoTimelineCursorInitV1(
    const worr_native_demo_timeline_summary_v1 *summary,
    worr_native_demo_timeline_cursor_v1 *cursor_out);

/*
 * Clock-driven advance: dispatches every not-yet-presented step whose time is
 * <= target_time_us, as one contiguous batch, and advances the frontier past
 * them.  A target before the last presented time (a rewind without a reset)
 * dispatches nothing.  `steps`/`step_count`/`plan_fingerprint` must match the
 * bound plan exactly.  batch_out is untouched on failure; on success count may
 * be zero.
 */
worr_native_demo_timeline_result_v1 Worr_NativeDemoTimelineCursorAdvanceToTimeV1(
    worr_native_demo_timeline_cursor_v1 *cursor,
    const worr_native_demo_timeline_step_v1 *steps, size_t step_count,
    uint64_t plan_fingerprint, uint64_t target_time_us,
    worr_native_demo_timeline_batch_v1 *batch_out);

/*
 * Single-step advance: dispatches exactly the next step (batch count 1) and
 * advances the frontier by one, regardless of time.  Returns NOT_FOUND with an
 * untouched batch_out when the frontier is already at the end of the plan.
 */
worr_native_demo_timeline_result_v1 Worr_NativeDemoTimelineCursorStepV1(
    worr_native_demo_timeline_cursor_v1 *cursor,
    const worr_native_demo_timeline_step_v1 *steps, size_t step_count,
    uint64_t plan_fingerprint,
    worr_native_demo_timeline_batch_v1 *batch_out);

/*
 * Re-arms the frontier at the start for an explicit seek/restart and bumps the
 * reset generation, so a subsequent advance re-presents from the beginning.
 * The plan binding is unchanged.
 */
worr_native_demo_timeline_result_v1 Worr_NativeDemoTimelineCursorResetV1(
    worr_native_demo_timeline_cursor_v1 *cursor);

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
WORR_NATIVE_DEMO_TIMELINE_STATIC_ASSERT(
    sizeof(worr_native_demo_timeline_cursor_v1) == 72,
    "native demo timeline cursor layout changed");
WORR_NATIVE_DEMO_TIMELINE_STATIC_ASSERT(
    sizeof(worr_native_demo_timeline_batch_v1) == 40,
    "native demo timeline batch layout changed");

#undef WORR_NATIVE_DEMO_TIMELINE_STATIC_ASSERT
