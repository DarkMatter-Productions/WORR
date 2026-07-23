/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/event_abi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical event-stream authority descriptor (FR-10-T05).
 *
 * This is semantic state, not a transport-session record.  stream_epoch is
 * therefore never inferred from a native transport epoch, snapshot epoch, or
 * connection counter.  Connection provenance and retransmission state remain
 * local to the endpoint that owns the descriptor.
 *
 * { 0, 0 } is intentionally not a valid descriptor.  Engine owners may use
 * that pair as a process-local teardown command, but it is never serialized.
 */
#define WORR_EVENT_STREAM_ABI_VERSION 1u
#define WORR_EVENT_STREAM_MAX_ENTITIES_V1 8192u

enum {
    /* The receiver explicitly accepts native EVENT record schema 2, whose
     * nested logical records remain canonical event ABI v1. */
    WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2 = 1u << 0,
};

typedef struct worr_event_stream_descriptor_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t flags;
    uint32_t stream_epoch;
    uint32_t first_sequence;
    uint32_t event_schema_version;
    uint32_t model_revision;
} worr_event_stream_descriptor_v1;

/* Failure leaves descriptor_out byte-identical. */
bool Worr_EventStreamDescriptorInitV1(
    worr_event_stream_descriptor_v1 *descriptor_out,
    uint32_t stream_epoch,
    uint32_t first_sequence);

bool Worr_EventStreamDescriptorValidateV1(
    const worr_event_stream_descriptor_v1 *descriptor);

/* Equality is defined only for two valid descriptors. */
bool Worr_EventStreamDescriptorEqualV1(
    const worr_event_stream_descriptor_v1 *left,
    const worr_event_stream_descriptor_v1 *right);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_EVENT_STREAM_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_EVENT_STREAM_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_EVENT_STREAM_STATIC_ASSERT(
    sizeof(worr_event_stream_descriptor_v1) == 24,
    "event stream descriptor V1 layout changed");
WORR_EVENT_STREAM_STATIC_ASSERT(
    offsetof(worr_event_stream_descriptor_v1, stream_epoch) == 8,
    "event stream descriptor epoch offset changed");
WORR_EVENT_STREAM_STATIC_ASSERT(
    offsetof(worr_event_stream_descriptor_v1, event_schema_version) == 16,
    "event stream descriptor event-schema offset changed");
WORR_EVENT_STREAM_STATIC_ASSERT(
    offsetof(worr_event_stream_descriptor_v1, model_revision) == 20,
    "event stream descriptor model offset changed");

#undef WORR_EVENT_STREAM_STATIC_ASSERT
