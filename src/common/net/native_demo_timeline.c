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
         record_kind == WORR_NATIVE_DEMO_RECORD_EVENT_V1;
}

worr_native_demo_timeline_result_v1 Worr_NativeDemoTimelineBuildV1(
    const worr_native_demo_timeline_config_v1 *config, const void *encoded,
    size_t encoded_bytes, const worr_native_demo_index_entry_v1 *entries,
    size_t entry_count, worr_native_demo_timeline_step_v1 *steps_out,
    size_t step_capacity, worr_native_demo_timeline_summary_v1 *summary_out) {
  worr_native_demo_timeline_summary_v1 summary;
  uint64_t hash = 0xcbf29ce484222325ull;
  uint64_t anchor_snapshot_ordinal = 0;
  uint64_t prev_ordinal = 0;
  uint64_t prev_time_us = 0;
  bool have_anchor = false;
  bool have_prev = false;
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

  for (i = 0; i < entry_count; ++i) {
    const worr_native_demo_index_entry_v1 *entry = &entries[i];
    worr_native_demo_timeline_step_v1 step;
    uint8_t step_flags = 0;

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
      if (entry->time_us > prev_time_us)
        step_flags |= WORR_NATIVE_DEMO_TIMELINE_STEP_TIME_ADVANCE;
    } else {
      step_flags |= WORR_NATIVE_DEMO_TIMELINE_STEP_FIRST |
                    WORR_NATIVE_DEMO_TIMELINE_STEP_TIME_ADVANCE;
    }

    if (entry->record_kind == WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1) {
      anchor_snapshot_ordinal = entry->ordinal;
      have_anchor = true;
      step_flags |= WORR_NATIVE_DEMO_TIMELINE_STEP_SNAPSHOT_ANCHOR;
      summary.snapshot_count += 1;
      if (i == 0)
        summary.flags |= WORR_NATIVE_DEMO_TIMELINE_SUMMARY_LEADING_SNAPSHOT;
    } else if (entry->record_kind == WORR_NATIVE_DEMO_RECORD_EVENT_V1) {
      /* Present-once requires a base: an event before any snapshot, or at the
       * same timestamp as its anchor without the snapshot preceding it, cannot
       * be dispatched.  Because ordinals strictly increase and the anchor is
       * the most recent snapshot, "anchor precedes" holds by construction; the
       * load-bearing check is that an anchor exists at all. */
      if (!have_anchor)
        return WORR_NATIVE_DEMO_TIMELINE_ORDER;
      summary.event_count += 1;
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

    prev_ordinal = entry->ordinal;
    prev_time_us = entry->time_us;
    have_prev = true;

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
