/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdint.h>

#include "shared/command_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FR-10-T08 predictable-weapon seed record.
 *
 * Compact pointer-free seed: the transport payload that carries the server's
 * exact authoritative weapon state to the client so the client can seed its
 * predictor at a known command boundary.  It rides the native side-channel (a
 * versioned record, not the frozen snapshot ABI or a legacy-compat-breaking
 * stat), so it is default-off and never alters the legacy wire.  The state's
 * applied_cursor names the boundary; the client replays commands after it.
 *
 * This type lives in `shared/` rather than beside the predictor because it is
 * an event payload (`WORR_EVENT_PAYLOAD_LOCAL_ACTION_PREDICT_SEED_V1`), and
 * `shared/event_abi.h` must be able to size, validate, hash, and serialize it.
 * The predictor header already depends on `event_abi.h`, so defining it there
 * would be circular.  It depends only on the command cursor.
 */
#define WORR_LOCAL_ACTION_PREDICT_SEED_ABI_VERSION 1u

/*
 * Compact READY-boundary seed (40 bytes, fits the 80-byte native event
 * payload).  The client reconstructs the exact READY state from these fields,
 * so only stable idle-state boundaries are transported; the client predicts
 * forward through the fire/refire cycle from there.
 */
typedef struct worr_local_action_predict_seed_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t catalog_id;
  uint32_t active_ammo_units;
  uint32_t presentation_frame;
  uint32_t reserved0;
  worr_command_cursor_v1 applied_cursor;
  uint64_t sample_time_us;
} worr_local_action_predict_seed_v1;

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_LOCAL_ACTION_PREDICT_SEED_STATIC_ASSERT(condition, message)       \
  static_assert((condition), message)
#else
#define WORR_LOCAL_ACTION_PREDICT_SEED_STATIC_ASSERT(condition, message)       \
  _Static_assert((condition), message)
#endif

/* The wire codec writes these fields individually, so the layout is frozen. */
WORR_LOCAL_ACTION_PREDICT_SEED_STATIC_ASSERT(
    sizeof(worr_local_action_predict_seed_v1) == 40,
    "local action predict seed v1 layout changed");
WORR_LOCAL_ACTION_PREDICT_SEED_STATIC_ASSERT(
    offsetof(worr_local_action_predict_seed_v1, applied_cursor) == 24 &&
        offsetof(worr_local_action_predict_seed_v1, sample_time_us) == 32,
    "local action predict seed v1 field offsets changed");

#undef WORR_LOCAL_ACTION_PREDICT_SEED_STATIC_ASSERT
