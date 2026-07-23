/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/capability.h"
#include "common/net/native_codec.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Native canonical demo framing foundation (FR-10-T13).
 *
 * WDM1 is an append-only container header followed by WDR1 records.  Each
 * record carries one complete WNC1 codec image without redefining any
 * canonical command, snapshot, or event fields.  Every wire integer is
 * little-endian.  Host structures are pointer-free and are never serialized
 * by copying their object representation.
 *
 * This primitive deliberately owns no filesystem, playback, or seek-array
 * storage.  Callers retain all bytes and any index entries they choose to
 * keep.  It is not integrated with legacy DM2, MVD2, or GTV formats.
 *
 * WDM1 header (64 bytes):
 *   0 magic:"WDM1", 4 version:u16, 6 header_bytes:u16,
 *   8 transport:u16, 10 codec_wire_version:u16, 12 flags:u32=0,
 *   16 capability_version:u16, 18 reserved:u16=0,
 *   20 capability_mask:u32, 24 transport_epoch:u32,
 *   28 timeline_epoch:u32, 32 first_record_ordinal:u64,
 *   40 created_time_us:u64, 48 header_crc32:u32,
 *   52 record_header_bytes:u16, 54 reserved:u16=0, 56 reserved:u64=0.
 *
 * WDR1 record header (48 bytes):
 *   0 magic:"WDR1", 4 version:u16, 6 header_bytes:u16,
 *   8 kind:u8, 9 flags:u8=0, 10 reserved:u16=0,
 *   12 frame_bytes:u32, 16 payload_bytes:u32, 20 frame_crc32:u32,
 *   24 ordinal:u64, 32 time_us:u64, 40 reserved:u64=0.
 * One exact WNC1 image immediately follows the record header.  IEEE CRC-32
 * covers the complete header (with its CRC field zero) and, for WDR1, the
 * complete WNC1 image.
 */
#define WORR_NATIVE_DEMO_ABI_VERSION 1u
#define WORR_NATIVE_DEMO_WIRE_VERSION 1u
#define WORR_NATIVE_DEMO_WIRE_HEADER_BYTES 64u
#define WORR_NATIVE_DEMO_RECORD_WIRE_VERSION 1u
#define WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES 48u
#define WORR_NATIVE_DEMO_MAX_RECORD_BYTES                                      \
  (WORR_NATIVE_DEMO_RECORD_WIRE_HEADER_BYTES +                                 \
   WORR_NATIVE_CODEC_MAX_ENCODED_BYTES)

typedef enum worr_native_demo_transport_v1_e {
  WORR_NATIVE_DEMO_TRANSPORT_WNC1_V1 = 1,
} worr_native_demo_transport_v1;

typedef enum worr_native_demo_record_kind_v1_e {
  WORR_NATIVE_DEMO_RECORD_COMMAND_V1 = WORR_NATIVE_RECORD_COMMAND_V1,
  WORR_NATIVE_DEMO_RECORD_SNAPSHOT_V1 = WORR_NATIVE_RECORD_SNAPSHOT_V1,
  WORR_NATIVE_DEMO_RECORD_EVENT_V1 = WORR_NATIVE_RECORD_EVENT_V1,
} worr_native_demo_record_kind_v1;

typedef enum worr_native_demo_result_v1_e {
  WORR_NATIVE_DEMO_OK = 0,
  WORR_NATIVE_DEMO_INVALID_ARGUMENT = 1,
  WORR_NATIVE_DEMO_INVALID_METADATA = 2,
  WORR_NATIVE_DEMO_OUTPUT_TOO_SMALL = 3,
  WORR_NATIVE_DEMO_TRUNCATED = 4,
  WORR_NATIVE_DEMO_MALFORMED = 5,
  WORR_NATIVE_DEMO_UNSUPPORTED = 6,
  WORR_NATIVE_DEMO_CORRUPT = 7,
  WORR_NATIVE_DEMO_LIMIT = 8,
  WORR_NATIVE_DEMO_ORDER = 9,
  WORR_NATIVE_DEMO_INVALID_STATE = 10,
  WORR_NATIVE_DEMO_NOT_FOUND = 11,
} worr_native_demo_result_v1;

typedef struct worr_native_demo_container_config_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t transport;
  uint32_t capability_mask;
  uint32_t transport_epoch;
  uint32_t timeline_epoch;
  uint32_t reserved0;
  uint64_t first_record_ordinal;
  uint64_t created_time_us;
  uint64_t reserved1;
} worr_native_demo_container_config_v1;

/* Pointer-free decoded WDM1 header. */
typedef struct worr_native_demo_container_info_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t transport;
  uint16_t codec_wire_version;
  uint16_t capability_version;
  uint32_t capability_mask;
  uint32_t transport_epoch;
  uint32_t timeline_epoch;
  uint64_t first_record_ordinal;
  uint64_t created_time_us;
  uint32_t header_crc32;
  uint16_t wire_header_bytes;
  uint16_t record_wire_header_bytes;
  uint64_t reserved0;
} worr_native_demo_container_info_v1;

typedef struct worr_native_demo_record_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint8_t record_kind;
  uint8_t flags;
  uint64_t ordinal;
  uint64_t time_us;
  uint64_t reserved0;
} worr_native_demo_record_v1;

/*
 * Pointer-free decoded WDR1 view.  All offsets are absolute logical-stream
 * offsets supplied by the caller; no input pointer is retained.
 */
typedef struct worr_native_demo_record_info_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint8_t record_kind;
  uint8_t flags;
  uint32_t frame_bytes;
  uint32_t payload_bytes;
  uint32_t frame_crc32;
  uint32_t reserved0;
  uint64_t ordinal;
  uint64_t time_us;
  uint64_t record_offset;
  uint64_t payload_offset;
  uint64_t next_record_offset;
  worr_native_codec_info_v1 codec;
} worr_native_demo_record_info_v1;

/* Compact caller-owned seek/scanning entry derived from a decoded record. */
typedef struct worr_native_demo_index_entry_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint8_t record_kind;
  uint8_t flags;
  uint64_t ordinal;
  uint64_t time_us;
  uint64_t record_offset;
  uint64_t next_record_offset;
  uint32_t frame_bytes;
  uint32_t frame_crc32;
  worr_native_record_ref_v1 record;
  uint32_t reserved0;
} worr_native_demo_index_entry_v1;

enum {
  WORR_NATIVE_DEMO_ORDER_INITIALIZED = 1u << 0,
  WORR_NATIVE_DEMO_ORDER_OBSERVED = 1u << 1,
};

/*
 * Stateful ordering validator.  Canonical identities increase independently
 * per record class.  Events require a prior snapshot.  At an equal time,
 * snapshots precede events and no snapshot may follow an event; commands do
 * not change that same-time phase.
 */
typedef struct worr_native_demo_order_state_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t state_flags;
  uint64_t first_record_ordinal;
  uint64_t last_ordinal;
  uint64_t last_time_us;
  uint32_t command_epoch;
  uint32_t command_sequence;
  uint32_t snapshot_epoch;
  uint32_t snapshot_sequence;
  uint32_t event_epoch;
  uint32_t event_sequence;
  uint32_t seen_class_mask;
  uint8_t same_time_phase;
  uint8_t reserved0[3];
  uint64_t reserved1;
} worr_native_demo_order_state_v1;

/*
 * Full-stream scan policy.  max_records is a caller-selected work bound; a
 * header-only stream is valid with max_records zero.  max_entities is the
 * nonzero exclusive entity-index bound used by canonical snapshot and event
 * semantic validation.  stream_offset is the logical offset assigned to byte
 * zero of the supplied WDM1 image.
 */
typedef struct worr_native_demo_scan_config_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t flags;
  uint64_t stream_offset;
  uint64_t max_records;
  uint32_t max_entities;
  uint32_t reserved0;
} worr_native_demo_scan_config_v1;

enum {
  WORR_NATIVE_DEMO_SCAN_COMPLETE = 1u << 0,
  WORR_NATIVE_DEMO_SCAN_HAS_RECORDS = 1u << 1,
  WORR_NATIVE_DEMO_SCAN_HAS_SNAPSHOTS = 1u << 2,
};

/* Pointer-free result for one exact, completely validated WDM1 image. */
typedef struct worr_native_demo_scan_info_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t flags;
  uint64_t stream_offset;
  uint64_t stream_bytes;
  uint64_t next_stream_offset;
  uint64_t record_count;
  uint64_t snapshot_count;
  worr_native_demo_container_info_v1 container;
  worr_native_demo_order_state_v1 order;
  /* Canonical CRC-32 over every derived index field in stream order. */
  uint32_t index_crc32;
  uint32_t reserved0;
} worr_native_demo_scan_info_v1;

enum {
  WORR_NATIVE_DEMO_SEEK_BY_TIME = 1u << 0,
  WORR_NATIVE_DEMO_SEEK_BY_ORDINAL = 1u << 1,
};

typedef struct worr_native_demo_seek_query_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t flags;
  uint64_t target_time_us;
  uint64_t target_ordinal;
  uint64_t reserved0;
} worr_native_demo_seek_query_v1;

enum {
  WORR_NATIVE_DEMO_SEEK_FOUND = 1u << 0,
  WORR_NATIVE_DEMO_SEEK_TIME_CONSTRAINED = 1u << 1,
  WORR_NATIVE_DEMO_SEEK_ORDINAL_CONSTRAINED = 1u << 2,
};

typedef struct worr_native_demo_seek_result_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t flags;
  uint64_t entry_index;
  worr_native_demo_index_entry_v1 entry;
  uint64_t reserved0;
} worr_native_demo_seek_result_v1;

/*
 * Encoders and decoders are bounded and transactional: every output remains
 * byte-identical on failure.  RecordEncode rejects output/payload overlap.
 * RecordDecode consumes one declared frame from an available byte span.
 */
worr_native_demo_result_v1 Worr_NativeDemoContainerEncodeV1(
    const worr_native_demo_container_config_v1 *config, void *encoded_out,
    size_t encoded_capacity, size_t *encoded_bytes_out);
worr_native_demo_result_v1
Worr_NativeDemoContainerDecodeV1(const void *encoded, size_t available_bytes,
                                 worr_native_demo_container_info_v1 *info_out);

worr_native_demo_result_v1
Worr_NativeDemoRecordEncodeV1(const worr_native_demo_record_v1 *record,
                              const void *wnc1_payload, size_t payload_bytes,
                              void *encoded_out, size_t encoded_capacity,
                              size_t *encoded_bytes_out);
worr_native_demo_result_v1
Worr_NativeDemoRecordDecodeV1(const void *encoded, size_t available_bytes,
                              uint64_t record_offset,
                              worr_native_demo_record_info_v1 *info_out);

worr_native_demo_result_v1
Worr_NativeDemoOrderInitV1(const worr_native_demo_container_info_v1 *container,
                           worr_native_demo_order_state_v1 *state_out);
worr_native_demo_result_v1
Worr_NativeDemoOrderObserveV1(worr_native_demo_order_state_v1 *state,
                              const worr_native_demo_record_info_v1 *record);

worr_native_demo_result_v1 Worr_NativeDemoIndexEntryFromRecordV1(
    const worr_native_demo_record_info_v1 *record,
    worr_native_demo_index_entry_v1 *entry_out);

/*
 * Validates a complete WDM1 byte image, including WDR1 CRCs, class-specific
 * WNC1 body semantics and snapshot hashes, and the cross-record ordering
 * contract.  Playback still decodes the selected canonical body into its
 * caller-owned destination; a successful scan never retains decoded output.
 *
 * Passing a null entries_out with zero index_capacity performs a bounded
 * count-only scan.  A non-null array is filled only after the complete image
 * and capacity have been accepted.  The input must remain byte-identical for
 * the whole call; under that precondition, all outputs remain byte-identical
 * on every failure.
 */
worr_native_demo_result_v1 Worr_NativeDemoStreamScanV1(
    const worr_native_demo_scan_config_v1 *config, const void *encoded,
    size_t encoded_bytes, worr_native_demo_index_entry_v1 *entries_out,
    size_t index_capacity, worr_native_demo_scan_info_v1 *scan_out);

/*
 * Selects the latest snapshot satisfying every enabled at-or-before bound.
 * The complete caller-owned index is revalidated against scan before a result
 * is committed, so malformed offsets or ordering cannot influence seeking.
 */
worr_native_demo_result_v1 Worr_NativeDemoSeekSnapshotAtOrBeforeV1(
    const worr_native_demo_scan_info_v1 *scan,
    const worr_native_demo_index_entry_v1 *entries, size_t entry_count,
    const worr_native_demo_seek_query_v1 *query,
    worr_native_demo_seek_result_v1 *result_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_DEMO_STATIC_ASSERT(condition, message)                     \
  static_assert((condition), message)
#else
#define WORR_NATIVE_DEMO_STATIC_ASSERT(condition, message)                     \
  _Static_assert((condition), message)
#endif

WORR_NATIVE_DEMO_STATIC_ASSERT(sizeof(uint8_t) == 1,
                               "native demo requires 8-bit bytes");
WORR_NATIVE_DEMO_STATIC_ASSERT(sizeof(worr_native_demo_container_config_v1) ==
                                   48,
                               "native demo container config layout changed");
WORR_NATIVE_DEMO_STATIC_ASSERT(sizeof(worr_native_demo_container_info_v1) == 56,
                               "native demo container info layout changed");
WORR_NATIVE_DEMO_STATIC_ASSERT(sizeof(worr_native_demo_record_v1) == 32,
                               "native demo record layout changed");
WORR_NATIVE_DEMO_STATIC_ASSERT(sizeof(worr_native_demo_record_info_v1) == 112,
                               "native demo record info layout changed");
WORR_NATIVE_DEMO_STATIC_ASSERT(offsetof(worr_native_demo_record_info_v1,
                                        codec) == 64,
                               "native demo codec info offset changed");
WORR_NATIVE_DEMO_STATIC_ASSERT(sizeof(worr_native_demo_index_entry_v1) == 64,
                               "native demo index entry layout changed");
WORR_NATIVE_DEMO_STATIC_ASSERT(sizeof(worr_native_demo_order_state_v1) == 72,
                               "native demo order state layout changed");
WORR_NATIVE_DEMO_STATIC_ASSERT(sizeof(worr_native_demo_scan_config_v1) == 32,
                               "native demo scan config layout changed");
WORR_NATIVE_DEMO_STATIC_ASSERT(sizeof(worr_native_demo_scan_info_v1) == 184,
                               "native demo scan info layout changed");
WORR_NATIVE_DEMO_STATIC_ASSERT(sizeof(worr_native_demo_seek_query_v1) == 32,
                               "native demo seek query layout changed");
WORR_NATIVE_DEMO_STATIC_ASSERT(sizeof(worr_native_demo_seek_result_v1) == 88,
                               "native demo seek result layout changed");

#undef WORR_NATIVE_DEMO_STATIC_ASSERT
