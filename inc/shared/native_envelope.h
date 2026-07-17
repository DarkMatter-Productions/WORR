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

/*
 * Native WORR envelope foundation (FR-10-T04).
 *
 * This layer is deliberately only a transport envelope.  It identifies one
 * of the canonical command, snapshot, event, or event-stream control record
 * classes and carries an opaque byte encoding owned by that class.  It never
 * casts the payload and does not define a second gameplay schema.
 *
 * Version 1 is not negotiated or emitted by a live network path yet.
 */
#define WORR_NATIVE_ENVELOPE_ABI_VERSION 1u
#define WORR_NATIVE_ENVELOPE_WIRE_VERSION 1u
#define WORR_NATIVE_ENVELOPE_WIRE_HEADER_BYTES 56u
#define WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES 1200u
#define WORR_NATIVE_ENVELOPE_MAX_PAYLOAD_BYTES 131072u
#define WORR_NATIVE_ENVELOPE_MAX_FRAGMENTS 128u
#define WORR_NATIVE_ENVELOPE_MAX_PRIORITY 7u
#define WORR_NATIVE_ENVELOPE_TX_QUEUE_CAPACITY 64u
#define WORR_NATIVE_ENVELOPE_AGING_QUANTUM 8u

typedef enum worr_native_record_class_v1_e {
    WORR_NATIVE_RECORD_COMMAND_V1 = 1,
    WORR_NATIVE_RECORD_SNAPSHOT_V1 = 2,
    WORR_NATIVE_RECORD_EVENT_V1 = 3,
    WORR_NATIVE_RECORD_EVENT_STREAM_DESCRIPTOR_V1 = 4,
} worr_native_record_class_v1;

/*
 * A transport reference to a canonical object.  object_epoch and
 * object_sequence copy the corresponding canonical ID values.  They do not
 * replace the canonical ID or alter its lifetime rules.
 */
typedef struct worr_native_record_ref_v1_s {
    uint8_t record_class;
    uint8_t reserved0;
    uint16_t record_schema_version;
    uint32_t object_epoch;
    uint32_t object_sequence;
} worr_native_record_ref_v1;

enum {
    WORR_NATIVE_ENVELOPE_FRAGMENT_FIRST = 1u << 0,
    WORR_NATIVE_ENVELOPE_FRAGMENT_LAST = 1u << 1,
};

/* Pointer-free decoded header.  payload_offset indexes the input datagram. */
typedef struct worr_native_envelope_frame_info_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t wire_header_bytes;
    worr_native_record_ref_v1 record;
    uint32_t transport_epoch;
    uint32_t message_sequence;
    uint32_t total_payload_bytes;
    uint32_t payload_crc32;
    uint32_t fragment_offset;
    uint16_t fragment_payload_bytes;
    uint16_t fragment_index;
    uint16_t fragment_count;
    uint16_t fragment_stride;
    uint8_t priority;
    uint8_t fragment_flags;
    uint16_t reserved0;
    uint32_t payload_offset;
} worr_native_envelope_frame_info_v1;

enum {
    WORR_NATIVE_FRAGMENTER_INITIALIZED = 1u << 0,
    WORR_NATIVE_FRAGMENTER_EXHAUSTED = 1u << 1,
};

/* Pointer-free iterator state. The payload remains caller-owned and must stay
 * byte-identical for the iterator's lifetime. FragmentNext rejects an output
 * datagram whose written range overlaps the complete payload range. */
typedef struct worr_native_envelope_fragmenter_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    worr_native_record_ref_v1 record;
    uint32_t transport_epoch;
    uint32_t message_sequence;
    uint32_t total_payload_bytes;
    uint32_t payload_crc32;
    uint16_t max_datagram_bytes;
    uint16_t fragment_stride;
    uint16_t fragment_count;
    uint16_t next_fragment;
    uint8_t priority;
    uint8_t reserved0[3];
} worr_native_envelope_fragmenter_v1;

typedef enum worr_native_envelope_emit_result_v1_e {
    WORR_NATIVE_ENVELOPE_EMIT_OK = 0,
    WORR_NATIVE_ENVELOPE_EMIT_EXHAUSTED = 1,
    WORR_NATIVE_ENVELOPE_EMIT_INVALID_ARGUMENT = 2,
    WORR_NATIVE_ENVELOPE_EMIT_INVALID_STATE = 3,
    WORR_NATIVE_ENVELOPE_EMIT_OUTPUT_TOO_SMALL = 4,
} worr_native_envelope_emit_result_v1;

typedef enum worr_native_envelope_decode_result_v1_e {
    WORR_NATIVE_ENVELOPE_DECODE_OK = 0,
    WORR_NATIVE_ENVELOPE_DECODE_INVALID_ARGUMENT = 1,
    WORR_NATIVE_ENVELOPE_DECODE_MALFORMED = 2,
    WORR_NATIVE_ENVELOPE_DECODE_UNSUPPORTED = 3,
    WORR_NATIVE_ENVELOPE_DECODE_CORRUPT = 4,
} worr_native_envelope_decode_result_v1;

enum {
    WORR_NATIVE_REASSEMBLY_INITIALIZED = 1u << 0,
    WORR_NATIVE_REASSEMBLY_COMPLETE = 1u << 1,
};

/*
 * One bounded reassembly slot.  A caller may maintain several slots and map
 * {transport_epoch, message_sequence} to them.  Payload bytes live only in
 * the caller-provided storage passed to AcceptV1.
 */
typedef struct worr_native_envelope_reassembly_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t state_flags;
    worr_native_record_ref_v1 record;
    uint32_t transport_epoch;
    uint32_t message_sequence;
    uint32_t total_payload_bytes;
    uint32_t payload_crc32;
    uint32_t received_payload_bytes;
    uint16_t fragment_stride;
    uint16_t fragment_count;
    uint16_t received_fragment_count;
    uint8_t priority;
    uint8_t reserved0;
    uint64_t received_bitmap[2];
} worr_native_envelope_reassembly_v1;

typedef enum worr_native_envelope_accept_result_v1_e {
    WORR_NATIVE_ENVELOPE_ACCEPTED = 0,
    WORR_NATIVE_ENVELOPE_ACCEPTED_DUPLICATE = 1,
    WORR_NATIVE_ENVELOPE_ACCEPTED_COMPLETE = 2,
    WORR_NATIVE_ENVELOPE_REJECT_INVALID_ARGUMENT = 3,
    WORR_NATIVE_ENVELOPE_REJECT_INVALID_STATE = 4,
    WORR_NATIVE_ENVELOPE_REJECT_MALFORMED = 5,
    WORR_NATIVE_ENVELOPE_REJECT_UNSUPPORTED = 6,
    WORR_NATIVE_ENVELOPE_REJECT_DATAGRAM_CHECKSUM = 7,
    WORR_NATIVE_ENVELOPE_REJECT_STORAGE_CAPACITY = 8,
    WORR_NATIVE_ENVELOPE_REJECT_MESSAGE_CONFLICT = 9,
    WORR_NATIVE_ENVELOPE_REJECT_DUPLICATE_CONFLICT = 10,
    WORR_NATIVE_ENVELOPE_REJECT_MESSAGE_CHECKSUM = 11,
} worr_native_envelope_accept_result_v1;

bool Worr_NativeEnvelopeRecordRefValidV1(
    worr_native_record_ref_v1 record);

bool Worr_NativeEnvelopeFragmenterInitV1(
    worr_native_envelope_fragmenter_v1 *fragmenter,
    uint32_t transport_epoch,
    uint32_t message_sequence,
    worr_native_record_ref_v1 record,
    uint8_t priority,
    const void *payload,
    uint32_t payload_bytes,
    uint16_t max_datagram_bytes);

worr_native_envelope_emit_result_v1 Worr_NativeEnvelopeFragmentNextV1(
    worr_native_envelope_fragmenter_v1 *fragmenter,
    const void *payload,
    uint32_t payload_bytes,
    void *datagram_out,
    size_t datagram_capacity,
    size_t *datagram_bytes_out);

worr_native_envelope_decode_result_v1 Worr_NativeEnvelopeDecodeV1(
    const void *datagram,
    size_t datagram_bytes,
    worr_native_envelope_frame_info_v1 *info_out);

void Worr_NativeEnvelopeReassemblyResetV1(
    worr_native_envelope_reassembly_v1 *reassembly);

worr_native_envelope_accept_result_v1 Worr_NativeEnvelopeReassemblyAcceptV1(
    worr_native_envelope_reassembly_v1 *reassembly,
    void *payload_storage,
    size_t payload_capacity,
    const void *datagram,
    size_t datagram_bytes,
    worr_native_envelope_frame_info_v1 *info_out);

/*
 * Pointer-free priority queue entries name payload bytes through an opaque,
 * process-local caller handle.  Lower numeric priority is more urgent.
 */
typedef struct worr_native_envelope_tx_item_v1_s {
    worr_native_record_ref_v1 record;
    uint32_t payload_handle;
    uint32_t payload_bytes;
    uint64_t enqueue_serial;
    uint64_t enqueue_dispatch;
    uint8_t priority;
    uint8_t reserved0[7];
} worr_native_envelope_tx_item_v1;

typedef struct worr_native_envelope_tx_queue_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t count;
    uint64_t next_enqueue_serial;
    uint64_t dispatch_count;
    worr_native_envelope_tx_item_v1
        items[WORR_NATIVE_ENVELOPE_TX_QUEUE_CAPACITY];
} worr_native_envelope_tx_queue_v1;

typedef enum worr_native_envelope_queue_result_v1_e {
    WORR_NATIVE_ENVELOPE_QUEUE_OK = 0,
    WORR_NATIVE_ENVELOPE_QUEUE_INVALID_ARGUMENT = 1,
    WORR_NATIVE_ENVELOPE_QUEUE_INVALID_STATE = 2,
    WORR_NATIVE_ENVELOPE_QUEUE_FULL = 3,
    WORR_NATIVE_ENVELOPE_QUEUE_DUPLICATE = 4,
    WORR_NATIVE_ENVELOPE_QUEUE_EMPTY = 5,
    WORR_NATIVE_ENVELOPE_QUEUE_SERIAL_EXHAUSTED = 6,
} worr_native_envelope_queue_result_v1;

void Worr_NativeEnvelopeTxQueueResetV1(
    worr_native_envelope_tx_queue_v1 *queue);

worr_native_envelope_queue_result_v1 Worr_NativeEnvelopeTxQueuePushV1(
    worr_native_envelope_tx_queue_v1 *queue,
    worr_native_record_ref_v1 record,
    uint8_t priority,
    uint32_t payload_handle,
    uint32_t payload_bytes);

/* item_out must not point anywhere inside queue; aliasing is rejected before
 * either object is mutated. */
worr_native_envelope_queue_result_v1 Worr_NativeEnvelopeTxQueuePopV1(
    worr_native_envelope_tx_queue_v1 *queue,
    worr_native_envelope_tx_item_v1 *item_out);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_ENVELOPE_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_NATIVE_ENVELOPE_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_ENVELOPE_STATIC_ASSERT(sizeof(uint8_t) == 1,
                                   "native envelope requires 8-bit bytes");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(sizeof(worr_native_record_ref_v1) == 12,
                                   "native record reference layout changed");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(
    sizeof(worr_native_envelope_frame_info_v1) == 56,
    "native envelope frame info layout changed");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(
    sizeof(worr_native_envelope_fragmenter_v1) == 48,
    "native envelope fragmenter layout changed");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(
    sizeof(worr_native_envelope_reassembly_v1) == 64,
    "native envelope reassembly layout changed");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(
    sizeof(worr_native_envelope_tx_item_v1) == 48,
    "native envelope tx item layout changed");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(
    sizeof(worr_native_envelope_tx_queue_v1) == 3096,
    "native envelope tx queue layout changed");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(
    offsetof(worr_native_envelope_tx_item_v1, enqueue_serial) == 24,
    "native envelope tx enqueue-serial offset changed");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(
    offsetof(worr_native_envelope_tx_item_v1, enqueue_dispatch) == 32,
    "native envelope tx enqueue-dispatch offset changed");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(
    offsetof(worr_native_envelope_tx_item_v1, priority) == 40,
    "native envelope tx priority offset changed");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(
    offsetof(worr_native_envelope_reassembly_v1, priority) == 46,
    "native envelope reassembly priority offset changed");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(
    offsetof(worr_native_envelope_reassembly_v1, received_bitmap) == 48,
    "native envelope reassembly bitmap offset changed");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(
    sizeof(((worr_native_envelope_reassembly_v1 *)0)->received_bitmap) == 16,
    "native envelope reassembly bitmap width changed");
WORR_NATIVE_ENVELOPE_STATIC_ASSERT(
    offsetof(worr_native_envelope_tx_queue_v1, items) == 24,
    "native envelope tx queue item offset changed");

#undef WORR_NATIVE_ENVELOPE_STATIC_ASSERT
