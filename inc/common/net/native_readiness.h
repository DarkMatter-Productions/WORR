/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/capability.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Isolated FR-10-T04 endpoint-readiness state machine.  This core owns no
 * pointers, transport, storage, clock, or random-number generator and does
 * not advertise native capabilities.  The caller supplies every nonzero
 * readiness nonce and is responsible for carrying records reliably.  Every
 * private binding requires both NATIVE_ENVELOPE_V1 and
 * NATIVE_EPOCH_CANCEL_V1; the public legacy-stage offer remains independent.
 */
#define WORR_NATIVE_READINESS_ABI_VERSION 1u

typedef enum worr_native_readiness_role_v1_e {
    WORR_NATIVE_READINESS_ROLE_SERVER = 1,
    WORR_NATIVE_READINESS_ROLE_CLIENT = 2,
} worr_native_readiness_role_v1;

typedef enum worr_native_readiness_phase_v1_e {
    WORR_NATIVE_READINESS_PHASE_RESET = 0,
    WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_READY = 1,
    WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_CHALLENGE = 2,
    WORR_NATIVE_READINESS_PHASE_CLIENT_WAIT_SERVER_ACTIVE = 3,
    WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE = 4,
    WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE = 5,
    WORR_NATIVE_READINESS_PHASE_FAILED = 6,
    /* Opt-in server-originated semantic DATA confirmation extension. */
    WORR_NATIVE_READINESS_PHASE_SERVER_WAIT_CLIENT_ACTIVE_CONFIRM = 7,
} worr_native_readiness_phase_v1;

typedef enum worr_native_readiness_record_kind_v1_e {
    WORR_NATIVE_READINESS_RECORD_CHALLENGE = 1,
    WORR_NATIVE_READINESS_RECORD_CLIENT_READY = 2,
    WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE = 3,
    /* Valid only for an opt-in server-originated semantic DATA binding. */
    WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM = 4,
} worr_native_readiness_record_kind_v1;

typedef enum worr_native_readiness_result_v1_e {
    WORR_NATIVE_READINESS_OK = 0,
    WORR_NATIVE_READINESS_EXACT_DUPLICATE = 1,
    WORR_NATIVE_READINESS_INVALID_ARGUMENT = 2,
    WORR_NATIVE_READINESS_INVALID_STATE = 3,
    WORR_NATIVE_READINESS_INVALID_RECORD = 4,
    WORR_NATIVE_READINESS_WRONG_ROLE = 5,
    WORR_NATIVE_READINESS_WRONG_ORDER = 6,
    WORR_NATIVE_READINESS_BINDING_MISMATCH = 7,
    WORR_NATIVE_READINESS_CLOCK_REGRESSION = 8,
    WORR_NATIVE_READINESS_DEADLINE_EXPIRED = 9,
    WORR_NATIVE_READINESS_DEADLINE_OVERFLOW = 10,
    WORR_NATIVE_READINESS_EPOCH_NOT_NEWER = 11,
    WORR_NATIVE_READINESS_NONCE_NOT_NEWER = 12,
    WORR_NATIVE_READINESS_NONCE_EXHAUSTED = 13,
    WORR_NATIVE_READINESS_GENERATION_EXHAUSTED = 14,
    WORR_NATIVE_READINESS_EPOCH_EXHAUSTED = 15,
} worr_native_readiness_result_v1;

typedef struct worr_native_readiness_record_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t record_kind;
    uint32_t transport_epoch;
    uint32_t negotiated_capabilities;
    uint64_t readiness_nonce;
    uint32_t record_checksum;
    uint32_t snapshot_epoch;
} worr_native_readiness_record_v1;

/* Every counter saturates at UINT64_MAX. */
typedef struct worr_native_readiness_telemetry_v1_s {
    uint64_t challenges_emitted;
    uint64_t challenges_accepted;
    uint64_t client_ready_emitted;
    uint64_t client_ready_accepted;
    uint64_t server_active_emitted;
    uint64_t server_active_accepted;
    uint64_t exact_duplicates;
    uint64_t binding_mismatches;
    uint64_t order_failures;
    uint64_t invalid_records;
    uint64_t deadline_checks;
    uint64_t deadline_expirations;
    uint64_t clock_regressions;
    uint64_t epoch_advances;
    uint64_t failures;
} worr_native_readiness_telemetry_v1;

enum {
    WORR_NATIVE_READINESS_STATE_INITIALIZED = 1u << 0,
};

typedef struct worr_native_readiness_state_v1_s {
    uint32_t struct_size;
    uint16_t schema_version;
    uint16_t role;
    uint16_t phase;
    uint16_t state_flags;
    uint32_t transport_epoch;
    uint32_t negotiated_capabilities;
    uint32_t snapshot_epoch;
    uint64_t readiness_nonce;
    uint64_t nonce_floor;
    uint64_t generation;
    uint64_t timeout_ticks;
    uint64_t phase_start_tick;
    uint64_t deadline_tick;
    uint64_t last_tick;
    worr_native_readiness_telemetry_v1 telemetry;
} worr_native_readiness_state_v1;

/*
 * Records use a canonical little-endian CRC32 over their domain tag and every
 * field except record_checksum.  The checksum detects accidental corruption;
 * it is not authentication.  Invalid initialization leaves output untouched.
 *
 * The legacy constructor is valid only for bindings without
 * CANONICAL_SNAPSHOT_V2 and initializes snapshot_epoch to zero.  The bound
 * constructor enforces the exact relationship: snapshot-capable bindings
 * require a nonzero snapshot_epoch, and all other bindings require zero.
 */
bool Worr_NativeReadinessRecordInitV1(
    worr_native_readiness_record_v1 *record_out,
    uint16_t record_kind,
    uint32_t transport_epoch,
    uint32_t negotiated_capabilities,
    uint64_t readiness_nonce);
bool Worr_NativeReadinessRecordInitBoundV1(
    worr_native_readiness_record_v1 *record_out,
    uint16_t record_kind,
    uint32_t transport_epoch,
    uint32_t snapshot_epoch,
    uint32_t negotiated_capabilities,
    uint64_t readiness_nonce);
bool Worr_NativeReadinessRecordValidateV1(
    const worr_native_readiness_record_v1 *record);
bool Worr_NativeReadinessStateValidateV1(
    const worr_native_readiness_state_v1 *state);

/*
 * Server initialization is legal only after local receive resources and a
 * BYPASS-only transmit hook are ready.  Success atomically initializes state
 * and emits CHALLENGE.  State and output must be disjoint and remain untouched
 * on failure.  timeout_ticks is nonzero and now_tick + timeout_ticks must fit.
 * For every Init entry point, transport_epoch identifies the fresh connection
 * incarnation and must be globally non-reused by the caller.  Before an
 * adapter emits the returned CHALLENGE, it must have made all server-owned
 * retained work from older epochs terminal and unable to emit later.
 */
worr_native_readiness_result_v1 Worr_NativeReadinessServerInitV1(
    worr_native_readiness_state_v1 *state_out,
    uint32_t transport_epoch,
    uint32_t negotiated_capabilities,
    uint64_t readiness_nonce,
    uint64_t now_tick,
    uint64_t timeout_ticks,
    worr_native_readiness_record_v1 *challenge_out);
worr_native_readiness_result_v1 Worr_NativeReadinessServerInitBoundV1(
    worr_native_readiness_state_v1 *state_out,
    uint32_t transport_epoch,
    uint32_t snapshot_epoch,
    uint32_t negotiated_capabilities,
    uint64_t readiness_nonce,
    uint64_t now_tick,
    uint64_t timeout_ticks,
    worr_native_readiness_record_v1 *challenge_out);

/* Client initialization waits for the exact server CHALLENGE. */
worr_native_readiness_result_v1 Worr_NativeReadinessClientInitV1(
    worr_native_readiness_state_v1 *state_out,
    uint32_t transport_epoch,
    uint32_t negotiated_capabilities,
    uint64_t now_tick,
    uint64_t timeout_ticks);
worr_native_readiness_result_v1 Worr_NativeReadinessClientInitBoundV1(
    worr_native_readiness_state_v1 *state_out,
    uint32_t transport_epoch,
    uint32_t snapshot_epoch,
    uint32_t negotiated_capabilities,
    uint64_t now_tick,
    uint64_t timeout_ticks);

/*
 * In-incarnation advancement requires a strictly newer epoch and generation.
 * Server nonces must be strictly increasing; client advancement retains the
 * previous accepted nonce as a floor for the next CHALLENGE.  UINT64_MAX is
 * terminal nonce exhaustion, and UINT32_MAX is terminal transport-epoch
 * exhaustion.  Fresh reconnect objects use Init instead.  Every fresh
 * connection incarnation must receive a globally non-reused transport_epoch;
 * that caller-owned invariant prevents an old valid CHALLENGE from binding to
 * a newly initialized client and cannot be enforced by this pointer-free core.
 * Both ServerAdvanceEpoch entry points have the same cancellation
 * precondition as ServerInit.
 */
worr_native_readiness_result_v1 Worr_NativeReadinessServerAdvanceEpochV1(
    worr_native_readiness_state_v1 *state,
    uint32_t transport_epoch,
    uint32_t negotiated_capabilities,
    uint64_t readiness_nonce,
    uint64_t now_tick,
    uint64_t timeout_ticks,
    worr_native_readiness_record_v1 *challenge_out);
worr_native_readiness_result_v1
Worr_NativeReadinessServerAdvanceEpochBoundV1(
    worr_native_readiness_state_v1 *state,
    uint32_t transport_epoch,
    uint32_t snapshot_epoch,
    uint32_t negotiated_capabilities,
    uint64_t readiness_nonce,
    uint64_t now_tick,
    uint64_t timeout_ticks,
    worr_native_readiness_record_v1 *challenge_out);
worr_native_readiness_result_v1 Worr_NativeReadinessClientAdvanceEpochV1(
    worr_native_readiness_state_v1 *state,
    uint32_t transport_epoch,
    uint32_t negotiated_capabilities,
    uint64_t now_tick,
    uint64_t timeout_ticks);
worr_native_readiness_result_v1
Worr_NativeReadinessClientAdvanceEpochBoundV1(
    worr_native_readiness_state_v1 *state,
    uint32_t transport_epoch,
    uint32_t snapshot_epoch,
    uint32_t negotiated_capabilities,
    uint64_t now_tick,
    uint64_t timeout_ticks);

/*
 * Command transition chain:
 *   CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE.
 *
 * NATIVE_EPOCH_CANCEL_V1 makes the first two records reliable lifecycle
 * declarations.  CHALLENGE declares that the server has canceled all of its
 * retained work older than transport_epoch.  CLIENT_READY exactly echoes the
 * epoch/capability/nonce binding and declares that the client has likewise
 * canceled all of its older retained work before emitting the response.  An
 * adapter must satisfy that client-side precondition before calling
 * ClientObserveChallengeV1.  This pointer-free core validates and repeats the
 * declarations but cannot perform adapter-owned cancellation.  Exact
 * duplicates only reassert the same barrier and do not start a new lifecycle.
 *
 * A binding containing NATIVE_ENVELOPE_V1, NATIVE_EPOCH_CANCEL_V1, and either
 * NATIVE_EVENT_STREAM_V1 or CANONICAL_SNAPSHOT_V2 opts into the extended
 * transition chain:
 *   CHALLENGE -> CLIENT_READY -> SERVER_ACTIVE -> CLIENT_ACTIVE_CONFIRM.
 * The server remains in SERVER_WAIT_CLIENT_ACTIVE_CONFIRM until the exact
 * fourth record is accepted.  Native server RX is safe in that wait phase so
 * an application hook can admit DATA appended to the same packet as the
 * reliable confirmation; native server TX stays closed.  Existing bindings do
 * not enter that phase and retain the original three-record behavior.
 *
 * Every mutable object, input record, and output record must be pairwise
 * disjoint.  Invalid arguments/aliasing leave all objects untouched.  A valid
 * but corrupt, out-of-order, or binding-mismatched peer record makes failure
 * sticky and leaves outputs untouched.  Exact duplicates are idempotent;
 * duplicate CHALLENGE/CLIENT_READY observations reproduce the exact response
 * so a lost response can be retried without reopening the state transition.
 */
worr_native_readiness_result_v1 Worr_NativeReadinessClientObserveChallengeV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *challenge,
    uint64_t now_tick,
    worr_native_readiness_record_v1 *client_ready_out);
worr_native_readiness_result_v1 Worr_NativeReadinessServerObserveClientReadyV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *client_ready,
    uint64_t now_tick,
    worr_native_readiness_record_v1 *server_active_out);
worr_native_readiness_result_v1 Worr_NativeReadinessClientObserveServerActiveV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *server_active,
    uint64_t now_tick);

/*
 * Server-originated-semantic-DATA client transition.  It accepts
 * SERVER_ACTIVE and emits the exact CLIENT_ACTIVE_CONFIRM atomically.  Exact
 * duplicate SERVER_ACTIVE records reproduce the same confirmation.  The
 * legacy entry point above rejects event/snapshot bindings without mutation
 * so activation cannot silently omit the explicit confirmation barrier.
 */
worr_native_readiness_result_v1
Worr_NativeReadinessClientObserveServerActiveWithConfirmV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *server_active,
    uint64_t now_tick,
    worr_native_readiness_record_v1 *client_active_confirm_out);

/*
 * Server-originated-semantic-DATA server transition.  The exact confirmation
 * opens native server TX; RX was already safe in the wait phase because
 * application hooks run before legacy setting parsing.  Exact duplicates
 * after activation are idempotent.  Missing, corrupt, out-of-order, or
 * binding-mismatched records remain fail-closed.
 */
worr_native_readiness_result_v1
Worr_NativeReadinessServerObserveClientActiveConfirmV1(
    worr_native_readiness_state_v1 *state,
    const worr_native_readiness_record_v1 *client_active_confirm,
    uint64_t now_tick);

/* Waiting states fail closed at now_tick >= deadline_tick. */
worr_native_readiness_result_v1 Worr_NativeReadinessCheckDeadlineV1(
    worr_native_readiness_state_v1 *state,
    uint64_t now_tick);

/*
 * Client receive becomes safe after CHALLENGE was accepted and local adapter
 * setup completed before CLIENT_READY emission.  Server receive/transmit and
 * client transmit normally become safe only in their respective ACTIVE
 * phases.  The semantic-DATA extension additionally makes server receive safe
 * in SERVER_WAIT_CLIENT_ACTIVE_CONFIRM: the application RX hook runs before
 * the legacy parser can consume a reliable confirmation carried in that same
 * packet.  This applies to both event-stream and canonical-snapshot DATA.
 * Server transmit remains closed until final SERVER_ACTIVE.  Live adapters
 * must use these mutating gates immediately before every native RX/TX
 * admission; each gate applies the same sticky clock/deadline check as
 * Worr_NativeReadinessCheckDeadlineV1 before testing the phase.
 */
bool Worr_NativeReadinessCanReceiveNativeV1(
    worr_native_readiness_state_v1 *state,
    uint64_t now_tick);
bool Worr_NativeReadinessCanTransmitNativeV1(
    worr_native_readiness_state_v1 *state,
    uint64_t now_tick);

/* Teardown clears the complete pointer-free object, including telemetry. */
bool Worr_NativeReadinessResetV1(
    worr_native_readiness_state_v1 *state);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_NATIVE_READINESS_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define WORR_NATIVE_READINESS_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

WORR_NATIVE_READINESS_STATIC_ASSERT(
    sizeof(worr_native_readiness_record_v1) == 32,
    "native readiness record V1 layout changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    offsetof(worr_native_readiness_record_v1, readiness_nonce) == 16,
    "native readiness record nonce offset changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    offsetof(worr_native_readiness_record_v1, record_checksum) == 24,
    "native readiness record checksum offset changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    offsetof(worr_native_readiness_record_v1, snapshot_epoch) == 28,
    "native readiness record snapshot epoch offset changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    sizeof(worr_native_readiness_telemetry_v1) == 120,
    "native readiness telemetry V1 layout changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    sizeof(worr_native_readiness_state_v1) == 200,
    "native readiness state V1 layout changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    offsetof(worr_native_readiness_state_v1, snapshot_epoch) == 20,
    "native readiness state snapshot epoch offset changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    offsetof(worr_native_readiness_state_v1, readiness_nonce) == 24,
    "native readiness state nonce offset changed");
WORR_NATIVE_READINESS_STATIC_ASSERT(
    offsetof(worr_native_readiness_state_v1, telemetry) == 80,
    "native readiness state telemetry offset changed");

#undef WORR_NATIVE_READINESS_STATIC_ASSERT
