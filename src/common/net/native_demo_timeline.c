/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/native_demo_timeline.h"

#include <string.h>

#define WORR_NATIVE_DEMO_TIMELINE_SCHEMA_VERSION 1u

/* Order-sensitive 64-bit FNV-1a so the plan fingerprint is a byte-stable proof
 * of the dispatch order: reordering, dropping, or duplicating any record
 * changes it, and two builds of the same stream produce the same value. */
static uint64_t worr_timeline_fnv_mix_u64(uint64_t hash, uint64_t value) {
  int i;
  for (i = 0; i < 8; ++i) {
    hash ^= (uint64_t)(value & 0xffu);
    hash *= 0x100000001b3ull;
    value >>= 8;
  }
  return hash;
}

static bool worr_timeline_record_kind_known(uint8_t record_kind) {
  return record_kind == WORR_NATIVE_DEMO_RECORD_COMMAND_V1 ||
         record_kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1 ||
         record_kind == WORR_NATIVE_DEMO_RECORD_EVENT_V1 ||
         record_kind == WORR_NATIVE_DEMO_RECORD_BOOTSTRAP_V1;
}

/*
 * Validate the whole index without writing any caller output.  Because the
 * planner promises steps_out is untouched on failure, all fail-closed checks
 * run here first; the fill pass below only executes once the entire index is
 * proven a canonically ordered, present-once-valid timeline.
 */
static worr_native_demo_timeline_result_v1 worr_timeline_validate(
    const void *encoded, size_t encoded_bytes,
    const worr_native_demo_index_entry_v1 *entries, size_t entry_count) {
  uint64_t prev_ordinal = 0;
  uint64_t prev_time_us = 0;
  bool have_anchor = false;
  bool have_prev = false;
  size_t i;

  for (i = 0; i < entry_count; ++i) {
    const worr_native_demo_index_entry_v1 *entry = &entries[i];

    if (entry->struct_size != sizeof(*entry) ||
        entry->schema_version != entries[0].schema_version)
      return WORR_NATIVE_DEMO_TIMELINE_INVALID_METADATA;
    if (!worr_timeline_record_kind_known(entry->record_kind))
      return WORR_NATIVE_DEMO_TIMELINE_WRONG_CLASS;

    /* Revalidate the entry against the byte image: offsets must be ordered,
     * bounded, and contiguous with the previous record.  A mismatched or
     * truncated index fails closed rather than driving a bogus dispatch. */
    if (entry->record_offset >= entry->next_record_offset ||
        entry->next_record_offset > (uint64_t)encoded_bytes)
      return WORR_NATIVE_DEMO_TIMELINE_MALFORMED;
    if (i > 0 && entry->record_offset != entries[i - 1].next_record_offset)
      return WORR_NATIVE_DEMO_TIMELINE_MALFORMED;

    /* Canonical order: strictly increasing ordinal, non-decreasing time. */
    if (have_prev) {
      if (entry->ordinal <= prev_ordinal)
        return WORR_NATIVE_DEMO_TIMELINE_ORDER;
      if (entry->time_us < prev_time_us)
        return WORR_NATIVE_DEMO_TIMELINE_ORDER;
    }

    if (entry->record_kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1) {
      have_anchor = true;
    } else if (entry->record_kind == WORR_NATIVE_DEMO_RECORD_EVENT_V1) {
      /* Present-once requires a base: an event before any snapshot cannot be
       * dispatched.  Because ordinals strictly increase and the anchor is the
       * most recent snapshot, "anchor precedes" holds by construction; the
       * load-bearing check is that an anchor exists at all.  A BOOTSTRAP is
       * never an anchor: it carries gamestate, not entity state. */
      if (!have_anchor)
        return WORR_NATIVE_DEMO_TIMELINE_ORDER;
    } else if (entry->record_kind == WORR_NATIVE_DEMO_RECORD_BOOTSTRAP_V1) {
      /* Mirror the container's bootstrap-precedes-everything rule
       * (order_state_valid / order_observe_ref in native_demo.c): the
       * gamestate prologue is legal only as the stream's first record, so a
       * mid-stream bootstrap fails closed here too rather than silently
       * planning a second map load. */
      if (i != 0)
        return WORR_NATIVE_DEMO_TIMELINE_ORDER;
    }

    prev_ordinal = entry->ordinal;
    prev_time_us = entry->time_us;
    have_prev = true;
  }
  return WORR_NATIVE_DEMO_TIMELINE_OK;
}

worr_native_demo_timeline_result_v1 Worr_NativeDemoTimelineBuildV1(
    const worr_native_demo_timeline_config_v1 *config, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, worr_native_demo_timeline_step_v1 *steps_out,
    size_t step_capacity, worr_native_demo_timeline_summary_v1 *summary_out) {
  worr_native_demo_timeline_summary_v1 summary;
  worr_native_demo_timeline_result_v1 validation;
  uint64_t hash = 0xcbf29ce484222325ull;
  uint64_t anchor_snapshot_ordinal = 0;
  uint64_t prev_time_us = 0;
  bool have_anchor = false;
  size_t first_presentable_index;
  size_t i;

  if (config == NULL || entries == NULL || summary_out == NULL)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_ARGUMENT;
  if (config->struct_size != sizeof(*config) ||
      config->schema_version != WORR_NATIVE_DEMO_TIMELINE_SCHEMA_VERSION ||
      config->flags != 0)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_CONFIG;
  if (entry_count == 0)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_ARGUMENT;
  if (encoded == NULL || encoded_bytes == 0)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_ARGUMENT;
  if (config->max_steps != 0 && (uint64_t)entry_count > config->max_steps)
    return WORR_NATIVE_DEMO_TIMELINE_LIMIT;

  memset(&summary, 0, sizeof(summary));
  summary.struct_size = sizeof(summary);
  summary.schema_version = WORR_NATIVE_DEMO_TIMELINE_SCHEMA_VERSION;

  if (step_capacity < entry_count || steps_out == NULL) {
    /* Report the required count so the caller can resize and retry; leave the
     * step buffer untouched. */
    summary.step_count = (uint64_t)entry_count;
    memcpy(summary_out, &summary, sizeof(summary));
    return WORR_NATIVE_DEMO_TIMELINE_CAPACITY;
  }

  /* Prove the whole index before writing a single step: on any failure the
   * caller's steps_out and summary_out stay untouched (documented contract). */
  validation = worr_timeline_validate(encoded, encoded_bytes, entries,
                                      entry_count);
  if (validation != WORR_NATIVE_DEMO_TIMELINE_OK)
    return validation;

  /* Validation pinned any BOOTSTRAP to entry 0, so the first presentable
   * record is entry 1 exactly when a prologue is present. */
  first_presentable_index =
      (entries[0].record_kind == WORR_NATIVE_DEMO_RECORD_BOOTSTRAP_V1) ? 1u : 0u;

  /* Fill pass: the index is already validated, so nothing here can fail. */
  for (i = 0; i < entry_count; ++i) {
    const worr_native_demo_index_entry_v1 *entry = &entries[i];
    worr_native_demo_timeline_step_v1 step;
    uint8_t step_flags = 0;

    if (i == 0)
      step_flags |= WORR_NATIVE_DEMO_TIMELINE_STEP_FIRST |
                    WORR_NATIVE_DEMO_TIMELINE_STEP_TIME_ADVANCE;
    else if (entry->time_us > prev_time_us)
      step_flags |= WORR_NATIVE_DEMO_TIMELINE_STEP_TIME_ADVANCE;

    if (entry->record_kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1) {
      anchor_snapshot_ordinal = entry->ordinal;
      have_anchor = true;
      step_flags |= WORR_NATIVE_DEMO_TIMELINE_STEP_SNAPSHOT_ANCHOR;
      summary.snapshot_count += 1;
      /* "Leading" means the first presentable record, so a BOOTSTRAP prologue
       * at step 0 does not cost a bootstrap-then-snapshot stream this flag.
       * For a stream without a prologue this is still exactly `i == 0`. */
      if (i == first_presentable_index)
        summary.flags |= WORR_NATIVE_DEMO_TIMELINE_SUMMARY_LEADING_SNAPSHOT;
    } else if (entry->record_kind == WORR_NATIVE_DEMO_RECORD_EVENT_V1) {
      summary.event_count += 1;
    } else if (entry->record_kind == WORR_NATIVE_DEMO_RECORD_BOOTSTRAP_V1) {
      /* Planned for the 1:1 step/entry mapping, but counted separately: it is
       * a gamestate prologue, not a command, and not a presentable body. */
      step_flags |= WORR_NATIVE_DEMO_TIMELINE_STEP_BOOTSTRAP;
      summary.flags |= WORR_NATIVE_DEMO_TIMELINE_SUMMARY_LEADING_BOOTSTRAP;
    } else {
      summary.command_count += 1;
    }

    memset(&step, 0, sizeof(step));
    step.struct_size = sizeof(step);
    step.schema_version = WORR_NATIVE_DEMO_TIMELINE_SCHEMA_VERSION;
    step.record_kind = entry->record_kind;
    step.flags = step_flags;
    step.present_sequence = (uint64_t)i;
    step.entry_index = (uint64_t)i;
    step.ordinal = entry->ordinal;
    step.time_us = entry->time_us;
    step.anchor_snapshot_ordinal = have_anchor ? anchor_snapshot_ordinal : 0;
    steps_out[i] = step;

    hash = worr_timeline_fnv_mix_u64(hash, (uint64_t)entry->record_kind);
    hash = worr_timeline_fnv_mix_u64(hash, entry->ordinal);
    hash = worr_timeline_fnv_mix_u64(hash, entry->time_us);
    hash = worr_timeline_fnv_mix_u64(hash, step.anchor_snapshot_ordinal);
    hash = worr_timeline_fnv_mix_u64(hash, (uint64_t)i);

    prev_time_us = entry->time_us;

    if (i == 0)
      summary.first_ordinal = entry->ordinal;
    summary.last_ordinal = entry->ordinal;
    summary.last_time_us = entry->time_us;
  }

  summary.step_count = (uint64_t)entry_count;
  summary.plan_fingerprint = hash;
  memcpy(summary_out, &summary, sizeof(summary));
  return WORR_NATIVE_DEMO_TIMELINE_OK;
}

/* Revalidate a cursor's binding against the caller-supplied plan on every
 * operation, so a different or resized plan cannot be silently substituted. */
static worr_native_demo_timeline_result_v1 worr_timeline_cursor_check(
    const worr_native_demo_timeline_cursor_v1 *cursor,
    const worr_native_demo_timeline_step_v1 *steps, size_t step_count,
    uint64_t plan_fingerprint) {
  if (cursor == NULL || steps == NULL)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_ARGUMENT;
  if (cursor->struct_size != sizeof(*cursor) ||
      cursor->schema_version != WORR_NATIVE_DEMO_TIMELINE_SCHEMA_VERSION ||
      (cursor->state_flags & WORR_NATIVE_DEMO_TIMELINE_CURSOR_INITIALIZED) == 0)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_STATE;
  if ((uint64_t)step_count != cursor->step_count ||
      plan_fingerprint != cursor->plan_fingerprint)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_METADATA;
  if (cursor->next_step > cursor->step_count)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_STATE;
  return WORR_NATIVE_DEMO_TIMELINE_OK;
}

worr_native_demo_timeline_result_v1 Worr_NativeDemoTimelineCursorInitV1(
    const worr_native_demo_timeline_summary_v1 *summary,
    worr_native_demo_timeline_cursor_v1 *cursor_out) {
  worr_native_demo_timeline_cursor_v1 cursor;

  if (summary == NULL || cursor_out == NULL)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_ARGUMENT;
  if (summary->struct_size != sizeof(*summary) ||
      summary->schema_version != WORR_NATIVE_DEMO_TIMELINE_SCHEMA_VERSION ||
      summary->step_count == 0)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_METADATA;

  memset(&cursor, 0, sizeof(cursor));
  cursor.struct_size = sizeof(cursor);
  cursor.schema_version = WORR_NATIVE_DEMO_TIMELINE_SCHEMA_VERSION;
  cursor.state_flags = WORR_NATIVE_DEMO_TIMELINE_CURSOR_INITIALIZED;
  cursor.plan_fingerprint = summary->plan_fingerprint;
  cursor.step_count = summary->step_count;
  memcpy(cursor_out, &cursor, sizeof(cursor));
  return WORR_NATIVE_DEMO_TIMELINE_OK;
}

/* Commit a contiguous [first, first+count) presentation to the cursor and emit
 * the batch.  Callers must have already validated the binding and computed the
 * range against the present-once frontier. */
static void worr_timeline_emit_batch(
    worr_native_demo_timeline_cursor_v1 *cursor,
    const worr_native_demo_timeline_step_v1 *steps, uint64_t first,
    uint64_t count, worr_native_demo_timeline_batch_v1 *batch_out) {
  worr_native_demo_timeline_batch_v1 batch;

  memset(&batch, 0, sizeof(batch));
  batch.struct_size = sizeof(batch);
  batch.schema_version = WORR_NATIVE_DEMO_TIMELINE_SCHEMA_VERSION;
  batch.first_step = first;
  batch.count = count;

  if (count != 0) {
    const worr_native_demo_timeline_step_v1 *last = &steps[first + count - 1];
    uint64_t i;
    for (i = first; i < first + count; ++i) {
      if (steps[i].record_kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1)
        cursor->last_snapshot_ordinal = steps[i].ordinal;
    }
    cursor->next_step = first + count;
    cursor->presented_count += count;
    cursor->last_presented_time_us = last->time_us;
    batch.last_time_us = last->time_us;
  }
  batch.last_snapshot_ordinal = cursor->last_snapshot_ordinal;
  memcpy(batch_out, &batch, sizeof(batch));
}

worr_native_demo_timeline_result_v1 Worr_NativeDemoTimelineCursorAdvanceToTimeV1(
    worr_native_demo_timeline_cursor_v1 *cursor,
    const worr_native_demo_timeline_step_v1 *steps, size_t step_count,
    uint64_t plan_fingerprint, uint64_t target_time_us,
    worr_native_demo_timeline_batch_v1 *batch_out) {
  worr_native_demo_timeline_result_v1 status;
  uint64_t first;
  uint64_t end;

  if (batch_out == NULL)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_ARGUMENT;
  status = worr_timeline_cursor_check(cursor, steps, step_count,
                                      plan_fingerprint);
  if (status != WORR_NATIVE_DEMO_TIMELINE_OK)
    return status;

  first = cursor->next_step;
  end = first;
  /* Present every not-yet-presented step at or before the target time.  A
   * target before an already-presented step contributes nothing: the frontier
   * only moves forward, so there is no event re-fire on a backward clock. */
  while (end < cursor->step_count && steps[end].time_us <= target_time_us)
    ++end;

  worr_timeline_emit_batch(cursor, steps, first, end - first, batch_out);
  return WORR_NATIVE_DEMO_TIMELINE_OK;
}

worr_native_demo_timeline_result_v1 Worr_NativeDemoTimelineCursorStepV1(
    worr_native_demo_timeline_cursor_v1 *cursor,
    const worr_native_demo_timeline_step_v1 *steps, size_t step_count,
    uint64_t plan_fingerprint,
    worr_native_demo_timeline_batch_v1 *batch_out) {
  worr_native_demo_timeline_result_v1 status;

  if (batch_out == NULL)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_ARGUMENT;
  status = worr_timeline_cursor_check(cursor, steps, step_count,
                                      plan_fingerprint);
  if (status != WORR_NATIVE_DEMO_TIMELINE_OK)
    return status;
  if (cursor->next_step >= cursor->step_count)
    return WORR_NATIVE_DEMO_TIMELINE_NOT_FOUND;

  worr_timeline_emit_batch(cursor, steps, cursor->next_step, 1, batch_out);
  return WORR_NATIVE_DEMO_TIMELINE_OK;
}

worr_native_demo_timeline_result_v1 Worr_NativeDemoTimelineCursorResetV1(
    worr_native_demo_timeline_cursor_v1 *cursor) {
  if (cursor == NULL)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_ARGUMENT;
  if (cursor->struct_size != sizeof(*cursor) ||
      cursor->schema_version != WORR_NATIVE_DEMO_TIMELINE_SCHEMA_VERSION ||
      (cursor->state_flags & WORR_NATIVE_DEMO_TIMELINE_CURSOR_INITIALIZED) == 0)
    return WORR_NATIVE_DEMO_TIMELINE_INVALID_STATE;

  cursor->next_step = 0;
  cursor->presented_count = 0;
  cursor->last_presented_time_us = 0;
  cursor->last_snapshot_ordinal = 0;
  cursor->reset_generation += 1;
  return WORR_NATIVE_DEMO_TIMELINE_OK;
}
