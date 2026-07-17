/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/snapshot_projection.h"
#include "common/net/snapshot_store.h"
#include "shared/command_abi.h"
#include "shared/event_abi.h"
#include "shared/event_stream.h"
#include "shared/native_envelope.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical native payload codec (FR-10-T04).
 *
 * This is not a live protocol adapter and does not advertise a capability.
 * Every wire integer and IEEE-754 binary32 bit pattern is little-endian.
 * C structure padding, pointers, store refs, and store-local arena serials
 * are never serialized.  Source records and encoded input are caller-owned
 * and must remain byte-identical for the duration of their call.
 */
#define WORR_NATIVE_CODEC_ABI_VERSION 1u
#define WORR_NATIVE_CODEC_WIRE_VERSION 1u
#define WORR_NATIVE_CODEC_WIRE_HEADER_BYTES 48u
#define WORR_NATIVE_CODEC_MAX_ENCODED_BYTES \
    WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES
#define WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES 512u
#define WORR_NATIVE_CODEC_MAX_SNAPSHOT_AREA_BYTES 1024u
#define WORR_NATIVE_CODEC_MAX_SNAPSHOT_EVENT_REFS 512u

#define WORR_NATIVE_CODEC_COMMAND_FIXED_BODY_BYTES 62u
#define WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES 64u
#define WORR_NATIVE_CODEC_EVENT_STREAM_FIXED_BODY_BYTES 8u
#define WORR_NATIVE_CODEC_MAX_EVENT_ENCODED_BYTES                     \
    (WORR_NATIVE_CODEC_WIRE_HEADER_BYTES +                           \
     WORR_NATIVE_CODEC_EVENT_FIXED_BODY_BYTES +                     \
     WORR_EVENT_PAYLOAD_CAPACITY)
#define WORR_NATIVE_CODEC_SNAPSHOT_FIXED_BODY_BYTES 437u
#define WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MIN_BYTES 52u
#define WORR_NATIVE_CODEC_SNAPSHOT_ENTITY_MAX_BYTES 125u
#define WORR_NATIVE_CODEC_SNAPSHOT_EVENT_REF_BYTES 30u

typedef enum worr_native_codec_result_v1_e {
    WORR_NATIVE_CODEC_OK = 0,
    WORR_NATIVE_CODEC_INVALID_ARGUMENT = 1,
    WORR_NATIVE_CODEC_INVALID_RECORD = 2,
    WORR_NATIVE_CODEC_OUTPUT_TOO_SMALL = 3,
    WORR_NATIVE_CODEC_MALFORMED = 4,
    WORR_NATIVE_CODEC_UNSUPPORTED = 5,
    WORR_NATIVE_CODEC_LIMIT = 6,
    WORR_NATIVE_CODEC_CAPACITY = 7,
    WORR_NATIVE_CODEC_CORRUPT = 8,
} worr_native_codec_result_v1;

/* Pointer-free inspection result for a completely framed codec payload. */
typedef struct worr_native_codec_info_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint8_t record_class;
    uint8_t flags;
    uint16_t record_schema_version;
    uint16_t reserved0;
    uint32_t model_revision;
    uint32_t encoded_bytes;
    uint32_t fixed_body_bytes;
    uint32_t range_counts[3];
    uint32_t object_epoch;
    uint32_t object_sequence;
    uint32_t reserved1;
} worr_native_codec_info_v1;

/*
 * Inspect validates the shared header, exact top-level length, class/schema
 * tuple, declared limits, and class-fixed counts.  Class decoders additionally
 * validate every body field and semantic hash.
 */
worr_native_codec_result_v1 Worr_NativeCodecInspectV1(
    const void *encoded,
    size_t encoded_bytes,
    worr_native_codec_info_v1 *info_out);

/* Converts an inspected, envelope-addressable canonical identity. */
bool Worr_NativeCodecInfoRecordRefV1(
    const worr_native_codec_info_v1 *info,
    worr_native_record_ref_v1 *record_out);

worr_native_codec_result_v1 Worr_NativeCodecCommandPreflightV1(
    const worr_command_record_v1 *record,
    uint16_t max_duration_ms,
    uint32_t *encoded_bytes_out);
worr_native_codec_result_v1 Worr_NativeCodecCommandEncodeV1(
    const worr_command_record_v1 *record,
    uint16_t max_duration_ms,
    void *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_bytes_out);
worr_native_codec_result_v1 Worr_NativeCodecCommandDecodeV1(
    const void *encoded,
    size_t encoded_bytes,
    uint16_t max_duration_ms,
    worr_command_record_v1 *record_out);

/* Event transport v1 accepts authoritative canonical T05 records only. */
worr_native_codec_result_v1 Worr_NativeCodecEventPreflightV1(
    const worr_event_record_v1 *record,
    uint32_t max_entities,
    uint32_t *encoded_bytes_out);
worr_native_codec_result_v1 Worr_NativeCodecEventEncodeV1(
    const worr_event_record_v1 *record,
    uint32_t max_entities,
    void *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_bytes_out);
worr_native_codec_result_v1 Worr_NativeCodecEventDecodeV1(
    const void *encoded,
    size_t encoded_bytes,
    uint32_t max_entities,
    worr_event_record_v1 *record_out);

/*
 * Reliable control record that establishes semantic event authority before
 * any EVENT DATA is admissible.  Its envelope object identity is exactly
 * { stream_epoch, first_sequence }; transport provenance stays outside the
 * descriptor and is checked by the endpoint session owner.
 */
worr_native_codec_result_v1 Worr_NativeCodecEventStreamPreflightV1(
    const worr_event_stream_descriptor_v1 *descriptor,
    uint32_t *encoded_bytes_out);
worr_native_codec_result_v1 Worr_NativeCodecEventStreamEncodeV1(
    const worr_event_stream_descriptor_v1 *descriptor,
    void *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_bytes_out);
worr_native_codec_result_v1 Worr_NativeCodecEventStreamDecodeV1(
    const void *encoded,
    size_t encoded_bytes,
    worr_event_stream_descriptor_v1 *descriptor_out);

/*
 * max_entities is the exclusive entity-index limit.  The independently
 * bounded number of records carried by one projection is capped by
 * WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES.
 */
worr_native_codec_result_v1 Worr_NativeCodecSnapshotPreflightV1(
    const worr_snapshot_projection_view_v2 *view,
    uint32_t max_entities,
    uint32_t *encoded_bytes_out);
worr_native_codec_result_v1 Worr_NativeCodecSnapshotEncodeV1(
    const worr_snapshot_projection_view_v2 *view,
    uint32_t max_entities,
    void *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_bytes_out);

/*
 * Decodes a complete snapshot directly into a transient, fully valid
 * projection.  Unlike SnapshotDecodeV1, this preserves the wire hashes and
 * assigns deterministic nonzero transient range serials so the returned view
 * can be independently rehashed and synchronously consumed.  The projection
 * and all of its pointers remain caller-owned; retaining any pointer after
 * the backing outputs are reused is invalid.
 *
 * All output regions must be pairwise disjoint and must not overlap the
 * encoded input.  Every output is untouched unless the entire payload,
 * semantic hash set, capacities, and projection hash recomputation succeed.
 */
worr_native_codec_result_v1
Worr_NativeCodecSnapshotDecodeProjectionV1(
    const void *encoded,
    size_t encoded_bytes,
    uint32_t max_entities,
    worr_snapshot_v2 *snapshot_out,
    worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out,
    uint32_t entity_capacity,
    uint8_t *area_bytes_out,
    uint32_t area_capacity,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_ref_capacity,
    worr_snapshot_projection_view_v2 *view_out,
    worr_snapshot_projection_hashes_v2 *hashes_out);

/*
 * Snapshot decode is transactional across every destination.  On success it
 * returns the existing store-publication view over caller-owned decoded
 * records.  snapshot_out contains transport metadata only: the three
 * store-owned ranges and five hashes are zero because SnapshotStorePublishV2
 * assigns serials and recomputes all hashes atomically.
 *
 * InspectV1 supplies the three required capacities before this call.  A zero
 * range requires a NULL destination and zero capacity; a non-zero range
 * requires a distinct, sufficiently large destination.
 */
worr_native_codec_result_v1 Worr_NativeCodecSnapshotDecodeV1(
    const void *encoded,
    size_t encoded_bytes,
    uint32_t max_entities,
    worr_snapshot_v2 *snapshot_out,
    worr_snapshot_player_v2 *player_out,
    worr_snapshot_entity_v2 *entities_out,
    uint32_t entity_capacity,
    uint8_t *area_bytes_out,
    uint32_t area_capacity,
    worr_snapshot_event_ref_v2 *event_refs_out,
    uint32_t event_ref_capacity,
    worr_snapshot_store_publish_v2 *publication_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_CODEC_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_NATIVE_CODEC_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_CODEC_STATIC_ASSERT(sizeof(worr_native_codec_info_v1) == 48,
                                "native codec info v1 layout changed");
WORR_NATIVE_CODEC_STATIC_ASSERT(
    offsetof(worr_native_codec_info_v1, range_counts) == 24,
    "native codec range-count offset changed");
WORR_NATIVE_CODEC_STATIC_ASSERT(
    offsetof(worr_native_codec_info_v1, object_epoch) == 36,
    "native codec object-identity offset changed");

#undef WORR_NATIVE_CODEC_STATIC_ASSERT
