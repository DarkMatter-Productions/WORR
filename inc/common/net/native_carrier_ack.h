/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/native_carrier_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bounded exact-receipt retention for native-session ACKs carried by WTC1.
 * This isolated core does not advertise a capability or own a live netchan
 * boundary.  Its only receipt-ingress paths verify an RX Commit result or an
 * authoritative ALREADY_COMMITTED receipt retained by the RX session.
 */
#define WORR_NATIVE_CARRIER_ACK_ABI_VERSION 1u
#define WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY                               \
  (WORR_NATIVE_SESSION_RECEIPT_HISTORY_CAPACITY +                              \
   WORR_NATIVE_SESSION_SNAPSHOT_TOMBSTONE_CAPACITY)
#define WORR_NATIVE_CARRIER_ACK_MAX_RANGES WORR_NATIVE_CARRIER_MAX_ENTRIES
#define WORR_NATIVE_CARRIER_ACK_MAX_PROACTIVE_HANDOFFS 8u

enum {
  WORR_NATIVE_CARRIER_ACK_LEDGER_INITIALIZED = 1u << 0,
  WORR_NATIVE_CARRIER_ACK_LEDGER_EMIT_ACTIVE = 1u << 1,
  WORR_NATIVE_CARRIER_ACK_LEDGER_TOKEN_EXHAUSTED = 1u << 2,
  WORR_NATIVE_CARRIER_ACK_LEDGER_TERMINAL_CANCELLED = 1u << 3,
  WORR_NATIVE_CARRIER_ACK_RECEIPT_OCCUPIED = 1u << 0,
  WORR_NATIVE_CARRIER_ACK_RECEIPT_FORCE_DUE = 1u << 1,
  WORR_NATIVE_CARRIER_ACK_TOKEN_INITIALIZED = 1u << 0,
  WORR_NATIVE_CARRIER_ACK_TOKEN_PACKET_BOUND = 1u << 1,
};

/* One exact committed message receipt.  Ranges are formed only at prepare. */
typedef struct worr_native_carrier_ack_receipt_v1_s {
  uint32_t message_sequence;
  uint32_t handoff_attempts;
  uint64_t last_handoff_tick;
  uint16_t handoffs_remaining;
  uint8_t record_class;
  uint8_t state_flags;
  /* Zero for every non-batch receipt.  Schema-2 EVENT admission stores the
   * first logical sequence here; the WNE record ref supplies the last. */
  uint32_t semantic_first_sequence;
} worr_native_carrier_ack_receipt_v1;

/* Counters saturate at UINT64_MAX and change only with committed mutation. */
typedef struct worr_native_carrier_ack_telemetry_v1_s {
  uint64_t commit_captures;
  uint64_t repeat_refreshes;
  uint64_t receipts_retired;
  uint64_t handoff_commits;
  uint64_t ranges_handed_off;
  uint64_t receipts_handed_off;
  uint64_t proactive_bursts_completed;
  uint64_t reconciled_receipts;
} worr_native_carrier_ack_telemetry_v1;

typedef struct worr_native_carrier_ack_token_range_v1_s {
  uint32_t first_message_sequence;
  uint32_t last_message_sequence;
} worr_native_carrier_ack_token_range_v1;

/*
 * Caller-owned prepare/bind/terminal token.  It never authorizes a receipt.
 * connection_owner_id is process-local and is never encoded on the wire.
 */
typedef struct worr_native_carrier_ack_emit_token_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t range_count;
  uint32_t transport_epoch;
  uint16_t state_flags;
  uint16_t reserved0;
  uint64_t connection_owner_id;
  uint64_t ledger_generation;
  uint64_t token_id;
  uint64_t prepare_tick;
  uint32_t packet_crc32;
  uint16_t packet_bytes;
  uint16_t reserved1;
  worr_native_carrier_ack_token_range_v1
      ranges[WORR_NATIVE_CARRIER_ACK_MAX_RANGES];
} worr_native_carrier_ack_emit_token_v1;

/*
 * Pointer-free ACK ledger.  proactive_handoffs bounds unsolicited retries;
 * a later proven ALREADY_COMMITTED observation immediately rearms the exact
 * receipt.  One active emit token serializes prepare, packet binding, the
 * synchronous transport outcome, and terminal accounting.  Receipt ingress
 * is rejected during that short critical section, so a physically accepted
 * ACK can never escape its configured handoff accounting.
 */
typedef struct worr_native_carrier_ack_ledger_v1_s {
  uint32_t struct_size;
  uint16_t schema_version;
  uint16_t state_flags;
  uint32_t transport_epoch;
  uint16_t receipt_count;
  uint8_t proactive_handoffs;
  uint8_t reserved0;
  uint64_t connection_owner_id;
  uint64_t mutation_generation;
  uint64_t next_token_id;
  uint64_t last_handoff_tick;
  worr_native_carrier_ack_telemetry_v1 telemetry;
  worr_native_carrier_ack_emit_token_v1 active_token;
  worr_native_carrier_ack_receipt_v1
      receipts[WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY];
} worr_native_carrier_ack_ledger_v1;

typedef enum worr_native_carrier_ack_result_v1_e {
  WORR_NATIVE_CARRIER_ACK_OK = 0,
  WORR_NATIVE_CARRIER_ACK_NOT_DUE = 1,
  WORR_NATIVE_CARRIER_ACK_INVALID_ARGUMENT = 2,
  WORR_NATIVE_CARRIER_ACK_INVALID_STATE = 3,
  WORR_NATIVE_CARRIER_ACK_LIMIT = 4,
  WORR_NATIVE_CARRIER_ACK_OUTPUT_TOO_SMALL = 5,
  WORR_NATIVE_CARRIER_ACK_NO_CARRIER = 6,
  WORR_NATIVE_CARRIER_ACK_MALFORMED = 7,
  WORR_NATIVE_CARRIER_ACK_UNSUPPORTED = 8,
  WORR_NATIVE_CARRIER_ACK_CORRUPT = 9,
  WORR_NATIVE_CARRIER_ACK_WRONG_EPOCH = 10,
  WORR_NATIVE_CARRIER_ACK_CLOCK_REGRESSION = 11,
  WORR_NATIVE_CARRIER_ACK_STALE_PREPARATION = 12,
  WORR_NATIVE_CARRIER_ACK_PACKET_MISMATCH = 13,
  WORR_NATIVE_CARRIER_ACK_EMIT_ACTIVE = 14,
  WORR_NATIVE_CARRIER_ACK_TOKEN_EXHAUSTED = 15,
} worr_native_carrier_ack_result_v1;

bool Worr_NativeCarrierAckLedgerInitV1(
    worr_native_carrier_ack_ledger_v1 *ledger_out,
    const worr_native_session_binding_v1 *binding, uint8_t proactive_handoffs);

bool Worr_NativeCarrierAckLedgerValidateV1(
    const worr_native_carrier_ack_ledger_v1 *ledger);

/* A strictly newer epoch atomically clears all receipts and prepared tokens. */
bool Worr_NativeCarrierAckLedgerAdvanceEpochV1(
    worr_native_carrier_ack_ledger_v1 *ledger,
    const worr_native_session_binding_v1 *binding);

/*
 * Explicitly cancels every retained exact receipt.  An active emission token
 * represents a packet with an unterminated transport outcome and is refused
 * without mutation.  Success preserves owner/epoch provenance, token and
 * clock high-water marks, configuration, and telemetry; it is idempotent and
 * writes the exact number of cleared receipts.  Before cancellation becomes
 * externally visible, an adapter must also cancel pending RX slots and
 * install its old-epoch admission floor.  The ledger itself is terminal after
 * success, so retained RX history cannot reauthorize a receipt even if the
 * adapter accidentally exposes it before AdvanceEpoch/reinitialization.
 */
worr_native_carrier_ack_result_v1 Worr_NativeCarrierAckCancelAllV1(
    worr_native_carrier_ack_ledger_v1 *ledger,
    uint32_t *cancelled_count_out);

/*
 * Non-mutating scheduling query for a live transport adapter.  OK means at
 * least one receipt can be prepared at now_tick; NOT_DUE means none can.
 * Every other result mirrors the corresponding PrepareRanges precondition,
 * including an active emission, exhausted token domain, clock regression, or
 * invalid ledger.  This lets an outer send loop request an ACK-only packet
 * without reserving a token or invoking transport from an RX callback.
 */
worr_native_carrier_ack_result_v1
Worr_NativeCarrierAckPeekDueV1(const worr_native_carrier_ack_ledger_v1 *ledger,
                               uint64_t now_tick,
                               uint32_t retry_interval_ticks);

/*
 * Stages the complete RX session, slot array, and ACK ledger; calls the native
 * RX Commit API; and imports only its exact single-message acknowledgement.
 * Snapshot, event, and event-stream descriptor slots return
 * SEMANTIC_ADMISSION_REQUIRED and must use their class-specific admission
 * core, which owns the only retained-ledger ACK-authorizing semantic commit
 * bridge.  A successful COMMITTED result changes all three objects atomically.
 * Every other result leaves them byte-identical.
 */
worr_native_rx_result_v1 Worr_NativeCarrierSessionCommitRetainedV1(
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, uint32_t slot_index, uint32_t message_sequence,
    worr_native_carrier_ack_ledger_v1 *ledger);

/*
 * Decodes and admits one DATA entry through the carrier/session bridge while
 * retaining repeat authority internally.  ALREADY_COMMITTED is the only path
 * that refreshes a receipt: its one-shot native output must match the same
 * owner, epoch, and an identity still present in RX history or a committed
 * snapshot tombstone.  Snapshot, event, and event-stream descriptor repeats
 * return SEMANTIC_REVALIDATION_REQUIRED without refreshing the ledger; they
 * must use the corresponding observation-bound admission API instead.
 * Receipt state is serialized against an active outbound ACK emission.
 */
worr_native_carrier_session_result_v1
Worr_NativeCarrierSessionAcceptDataRetainedV1(
    worr_native_rx_session_v1 *session, worr_native_rx_slot_v1 *slots,
    uint16_t slot_capacity, void *payload_arena, size_t payload_arena_bytes,
    uint64_t now_tick, const void *packet, size_t packet_bytes,
    uint16_t entry_index, worr_native_carrier_ack_ledger_v1 *ledger,
    worr_native_rx_result_v1 *rx_result_out,
    worr_native_rx_message_v1 *message_out);

/*
 * Fair selection for DATA piggyback or ACK-only emission.  Success reserves
 * the ledger's sole emit token; later receipt mutation is rejected until the
 * token is bound and committed, definitely rejected, or safely aborted.
 * Adjacent due receipts are coalesced, but no absent sequence is ever bridged.
 * max_ranges is 1..8; callers reserving one DATA entry must cap it at 7.
 * Ledger and every output are untouched for all failures and NOT_DUE.
 */
worr_native_carrier_ack_result_v1 Worr_NativeCarrierAckPrepareRangesV1(
    worr_native_carrier_ack_ledger_v1 *ledger, uint64_t now_tick,
    uint32_t retry_interval_ticks, uint16_t max_ranges,
    worr_native_ack_range_v1 *ranges_out, uint16_t ranges_capacity,
    uint16_t *range_count_out,
    worr_native_carrier_ack_emit_token_v1 *token_out);

/*
 * Builds an ACK-only WTC1 packet, optionally following a complete final legacy
 * prefix.  The fit is legacy + 32-byte footer + 16 bytes per ACK, capped at
 * eight entries and the actual application budget (which must be <= 1,200).
 * Success returns an already packet-bound token.  The ledger and all outputs
 * are untouched on failure or NOT_DUE.
 */
worr_native_carrier_ack_result_v1 Worr_NativeCarrierAckPreparePacketV1(
    worr_native_carrier_ack_ledger_v1 *ledger, uint64_t now_tick,
    uint32_t retry_interval_ticks, uint16_t application_packet_budget,
    const void *legacy_packet, size_t legacy_bytes, void *packet_out,
    size_t packet_capacity, size_t *packet_bytes_out,
    worr_native_carrier_ack_emit_token_v1 *token_out);

/*
 * Binds a ranges-only token to the complete application packet before any
 * transport call.  Length, a full-packet CRC, epoch, and the exact ordered ACK
 * subsequence are retained in both token and ledger.  DATA and legacy bytes
 * may coexist, but are covered by the full-packet CRC.  CRC is accidental
 * corruption/replay identity, not authentication.  Outputs are unchanged on
 * failure.  ACK-only PreparePacket performs this step internally.
 */
worr_native_carrier_ack_result_v1
Worr_NativeCarrierAckBindPacketV1(worr_native_carrier_ack_ledger_v1 *ledger,
                                  worr_native_carrier_ack_emit_token_v1 *token,
                                  const void *packet, size_t packet_bytes);

/*
 * Call only after the bound packet has been accepted synchronously by the
 * transport handoff.  Exact bound length/full CRC, owner, epoch, token, and
 * ACK identity are rechecked before retry credits are consumed atomically.
 * Stale tokens and mismatches do not mutate the ledger.
 */
worr_native_carrier_ack_result_v1 Worr_NativeCarrierAckCommitHandoffV1(
    worr_native_carrier_ack_ledger_v1 *ledger,
    const worr_native_carrier_ack_emit_token_v1 *token, uint64_t handoff_tick,
    const void *packet, size_t packet_bytes);

/*
 * A definite transport rejection consumes the active bound token without
 * spending receipt credits.  Abort is legal only for an unbound token that
 * provably never reached transport.  Both terminals invalidate all copies.
 */
worr_native_carrier_ack_result_v1 Worr_NativeCarrierAckRejectHandoffV1(
    worr_native_carrier_ack_ledger_v1 *ledger,
    const worr_native_carrier_ack_emit_token_v1 *token);
worr_native_carrier_ack_result_v1 Worr_NativeCarrierAckAbortV1(
    worr_native_carrier_ack_ledger_v1 *ledger,
    const worr_native_carrier_ack_emit_token_v1 *token);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_CARRIER_ACK_STATIC_ASSERT(condition, message)              \
  static_assert((condition), message)
#else
#define WORR_NATIVE_CARRIER_ACK_STATIC_ASSERT(condition, message)              \
  _Static_assert((condition), message)
#endif

WORR_NATIVE_CARRIER_ACK_STATIC_ASSERT(
    WORR_NATIVE_CARRIER_ACK_RECEIPT_CAPACITY == 80,
    "native carrier ACK receipt bound changed");
WORR_NATIVE_CARRIER_ACK_STATIC_ASSERT(
    sizeof(worr_native_carrier_ack_receipt_v1) == 24,
    "native carrier ACK receipt v1 layout changed");
WORR_NATIVE_CARRIER_ACK_STATIC_ASSERT(
    sizeof(worr_native_carrier_ack_telemetry_v1) == 64,
    "native carrier ACK telemetry v1 layout changed");
WORR_NATIVE_CARRIER_ACK_STATIC_ASSERT(
    sizeof(worr_native_carrier_ack_emit_token_v1) == 120,
    "native carrier ACK token v1 layout changed");
WORR_NATIVE_CARRIER_ACK_STATIC_ASSERT(
    offsetof(worr_native_carrier_ack_emit_token_v1, ranges) == 56,
    "native carrier ACK token-range offset changed");
WORR_NATIVE_CARRIER_ACK_STATIC_ASSERT(
    sizeof(worr_native_carrier_ack_ledger_v1) == 2152,
    "native carrier ACK ledger v1 layout changed");
WORR_NATIVE_CARRIER_ACK_STATIC_ASSERT(
    offsetof(worr_native_carrier_ack_ledger_v1, active_token) == 112,
    "native carrier ACK active-token offset changed");
WORR_NATIVE_CARRIER_ACK_STATIC_ASSERT(
    offsetof(worr_native_carrier_ack_ledger_v1, receipts) == 232,
    "native carrier ACK receipt offset changed");

#undef WORR_NATIVE_CARRIER_ACK_STATIC_ASSERT
