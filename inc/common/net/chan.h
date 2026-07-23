/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "common/msg.h"
#include "common/net/net.h"
#include "common/net/sequence.h"
#include "common/sizebuf.h"

typedef enum {
    NETCHAN_OLD,
    NETCHAN_NEW
} netchan_type_t;

typedef enum {
    /* No complete application payload was admitted for a legacy parser. */
    NETCHAN_PROCESS_NO_APPLICATION = 0,

    /* A complete application payload (possibly zero bytes) is ready. */
    NETCHAN_PROCESS_APPLICATION_READY = 1,

    /* Transport admitted the packet, but the application hook rejected it. */
    NETCHAN_PROCESS_APPLICATION_REJECTED = 2
} netchan_process_result_t;

/*
 * Dormant post-assembly application transmit hook.
 *
 * The hook is local process ABI only; none of these structures are serialized.
 * The fixed-width, pointer-free V1 records make callback ownership and version
 * mismatches explicit.  Buffer pointers are supplied separately and are valid
 * only for the duration of the callback.
 */
#define NETCHAN_APP_TX_HOOK_ABI_V1 1u

typedef enum {
    /* Send the byte-identical legacy application slice; no token is created. */
    NETCHAN_APP_TX_PREPARE_BYPASS = 0,

    /* Validate and use the candidate described by the prepare output. */
    NETCHAN_APP_TX_PREPARE_PREPARED = 1
} netchan_app_tx_prepare_result_t;

typedef enum {
    /* At least one duplicate copy reached NET_SendPacket successfully. */
    NETCHAN_APP_TX_COMPLETION_ACCEPTED = 1,

    /* No duplicate copy reached NET_SendPacket successfully. */
    NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED = 2,

    /* Candidate metadata was invalid; the legacy application slice was sent. */
    NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID = 3
} netchan_app_tx_completion_result_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t outgoing_sequence;
    uint32_t max_application_bytes;
    uint32_t reliable_bytes;
    uint32_t unreliable_bytes;
    uint32_t legacy_application_bytes;
    uint32_t packet_copies;
} netchan_app_tx_prepare_info_v1_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t application_bytes;
    uint32_t reserved0;
    uint64_t token;
} netchan_app_tx_prepare_output_v1_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t result;
    uint32_t packet_copies;
    uint32_t accepted_copies;
    uint32_t application_bytes;
    uint64_t token;
} netchan_app_tx_completion_info_v1_t;

/*
 * legacy_application is the complete application slice assembled by netchan:
 * reliable bytes followed by unreliable bytes, with no sequence/ack/qport
 * header.  candidate_application is a separate bounded staging buffer whose
 * capacity is exactly info->max_application_bytes.  The staging capacity is
 * zero-filled before entry, but the callback remains responsible for every
 * candidate byte it describes.  Pointers must not be retained and the callback
 * must not mutate legacy_application.
 *
 * Returning BYPASS ignores output and creates no completion obligation; the
 * callback must therefore create no retained token in that case.  Every other
 * return value is a token-bearing attempt.  PREPARED is accepted only when the
 * output V1 header and reserved field are intact and application_bytes fits the
 * supplied capacity.  Unknown results or invalid output fall back to the
 * byte-identical legacy slice and complete once with PREPARE_INVALID, carrying
 * output->token (including an explicit zero token) so caller state is never
 * left pending.
 */
typedef netchan_app_tx_prepare_result_t (*netchan_app_tx_prepare_fn)(
    void *opaque,
    const netchan_app_tx_prepare_info_v1_t *info,
    const byte *legacy_application,
    byte *candidate_application,
    netchan_app_tx_prepare_output_v1_t *output);

/*
 * Called exactly once for every token-bearing prepare attempt, after all
 * packet copies have been attempted.  ACCEPTED means one or more copies were
 * accepted by NET_SendPacket; accepted_copies exposes partial duplicate
 * success.  PREPARE_INVALID describes the transmitted legacy fallback, never
 * acceptance of the rejected candidate.  application points at the exact
 * final application slice used for every packet copy and contains
 * info->application_bytes; it excludes the netchan header and is valid only
 * for the duration of this callback.  The callback pair and opaque value are
 * frozen for one transmit call.  Callbacks must not recursively transmit on or
 * destroy the same netchan.
 */
typedef void (*netchan_app_tx_completion_fn)(
    void *opaque,
    const netchan_app_tx_completion_info_v1_t *info,
    const byte *application);

/*
 * Dormant post-admission application receive hook.
 *
 * The hook is local process ABI only.  It observes exactly the unread
 * application slice after NEW-channel sequence/acknowledgement admission and
 * final fragment assembly, but before a legacy protocol parser can consume
 * the bytes.  The fixed records are pointer-free; application is supplied
 * separately and remains valid only for the synchronous callback.
 */
#define NETCHAN_APP_RX_HOOK_ABI_V1 1u

enum {
    NETCHAN_APP_RX_FLAG_RELIABLE = 1u << 0,
    NETCHAN_APP_RX_FLAG_REASSEMBLED = 1u << 1,
};

typedef enum {
    /* Preserve the complete message descriptor and application byte view. */
    NETCHAN_APP_RX_BYPASS = 0,

    /* Expose only output->legacy_bytes from the application prefix. */
    NETCHAN_APP_RX_EXPOSE_LEGACY = 1,

    /* Suppress the application and report an explicit protocol rejection. */
    NETCHAN_APP_RX_REJECT = 2
} netchan_app_rx_result_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t incoming_sequence;
    uint32_t message_bytes;
    uint32_t application_offset;
    uint32_t application_bytes;
    uint32_t flags;
    uint32_t reserved0;
} netchan_app_rx_info_v1_t;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t legacy_bytes;
    uint32_t reserved0;
} netchan_app_rx_output_v1_t;

/*
 * The callback must treat application as read-only and may not retain it,
 * operate the global MSG reader, recursively process/destroy the same netchan,
 * or retain output.  Netchan restores the complete message descriptor after
 * the callback, so descriptor corruption cannot leak into the legacy parser;
 * any observed descriptor mutation rejects the application.
 *
 * BYPASS ignores output.  EXPOSE_LEGACY requires the exact V1 output header,
 * zero reserved fields, and legacy_bytes <= info->application_bytes; zero is
 * valid.  REJECT, an unknown result, or invalid output suppresses the entire
 * application and produces NETCHAN_PROCESS_APPLICATION_REJECTED.  Transport
 * admission is intentionally not rolled back by application rejection.
 */
typedef netchan_app_rx_result_t (*netchan_app_rx_fn)(
    void *opaque,
    const netchan_app_rx_info_v1_t *info,
    const byte *application,
    netchan_app_rx_output_v1_t *output);

typedef struct {
    netchan_type_t  type;
    int         protocol;
    unsigned    maxpacketlen;

    netsrc_t    sock;

    unsigned    dropped;            // between last packet and previous
    unsigned    total_dropped;      // for statistics
    unsigned    total_received;

    unsigned    last_received;      // for timeouts
    unsigned    last_sent;          // for retransmits

    int         qport;              // qport value to write when transmitting
    netadr_t    remote_address;

    sizebuf_t   message;            // writing buffer for reliable data

    bool        reliable_ack_pending;   // set to true each time reliable is received
    bool        fragment_pending;

    // sequencing variables
    unsigned    incoming_sequence;
    unsigned    incoming_acknowledged;
    unsigned    outgoing_sequence;
    unsigned    stale_acknowledgements;
    unsigned    future_acknowledgements;
    unsigned    conflicting_acknowledgements;

    bool        incoming_reliable_acknowledged; // single bit
    bool        incoming_reliable_sequence;     // single bit, maintained local
    bool        reliable_sequence;          // single bit
    unsigned    last_reliable_sequence;     // sequence number of last send
    unsigned    fragment_sequence;

    // message is copied to this buffer when it is first transferred
    unsigned    reliable_length;
    byte        *reliable_buf;      // unacked reliable message

    sizebuf_t   fragment_in;
    sizebuf_t   fragment_out;

    netchan_app_tx_prepare_fn       app_tx_prepare;
    netchan_app_tx_completion_fn    app_tx_completion;
    void                            *app_tx_opaque;

    netchan_app_rx_fn               app_rx;
    void                            *app_rx_opaque;
} netchan_t;

extern cvar_t       *net_qport;
extern cvar_t       *net_maxmsglen;
extern cvar_t       *net_chantype;

void Netchan_Init(void);
void Netchan_OutOfBand(netsrc_t sock, const netadr_t *adr,
                       const char *format, ...) q_printf(3, 4);
void Netchan_Setup(netchan_t *chan, netsrc_t sock, netchan_type_t type,
                   const netadr_t *adr, int qport, size_t maxpacketlen, int protocol);
/*
 * Register or clear the dormant NEW-channel application transmit hook.
 * Both callbacks must be non-NULL to register.  Passing both as NULL clears
 * the hook (opaque is ignored).  Invalid/mixed registrations leave the current
 * registration unchanged and return false.  Opaque storage remains owned by
 * the caller and must outlive every synchronous callback that uses the pair.
 * Clearing the hook or closing the netchan only drops the stored pointer; it
 * does not invoke a finalizer or otherwise release caller-owned state.
 */
bool Netchan_SetApplicationTxHook(netchan_t *chan,
                                  netchan_app_tx_prepare_fn prepare,
                                  netchan_app_tx_completion_fn completion,
                                  void *opaque);
/*
 * Register or clear the dormant NEW-channel application receive hook.  A NULL
 * callback clears it (opaque is ignored).  Opaque remains caller-owned and
 * must outlive every synchronous callback.  OLD-channel registrations fail
 * without modifying the channel.  Every processing call site for a registered
 * channel must use Netchan_ProcessEx and treat APPLICATION_REJECTED as a
 * terminal protocol error; the legacy boolean wrapper intentionally collapses
 * rejection into false for compatibility.
 */
bool Netchan_SetApplicationRxHook(netchan_t *chan,
                                  netchan_app_rx_fn receive,
                                  void *opaque);
int Netchan_Transmit(netchan_t *chan, size_t length, const void *data, int numpackets);
/*
 * Transactional NEW-channel reliable handoffs for schedulers that need an
 * exact generation boundary.  Both helpers require a positive packet-copy
 * count and a clean outgoing reliable/fragment generation.  Rejection leaves
 * the channel, queued bytes, callbacks, and output size untouched except that
 * transmit_bytes_out is initialized to zero.
 *
 * Prefix handoff transfers exactly prefix_bytes from the head of message and
 * preserves the remaining queued tail byte-for-byte.  Isolated handoff makes
 * reliable_data the only reliable application bytes for this generation while
 * leaving the ordinary queued message in place.  Registered TX hooks remain
 * active, observe only the generation being handed off, and may append bytes
 * to the visible ordinary queue for the following reliable generation.
 * Success means the generation was transferred to netchan and a send was
 * attempted; it does not imply that NET_SendPacket accepted a copy.
 */
bool Netchan_TransmitQueuedReliablePrefix(
    netchan_t *chan, uint32_t prefix_bytes, int numpackets,
    int *transmit_bytes_out);
bool Netchan_TransmitIsolatedReliable(
    netchan_t *chan, const void *reliable_data, uint32_t reliable_bytes,
    int numpackets, int *transmit_bytes_out);
int Netchan_TransmitNextFragment(netchan_t *chan);
netchan_process_result_t Netchan_ProcessEx(netchan_t *chan);
bool Netchan_Process(netchan_t *chan);
bool Netchan_ShouldUpdate(const netchan_t *chan);
void Netchan_Close(netchan_t *chan);

#if defined(__cplusplus)
#define NETCHAN_APP_TX_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define NETCHAN_APP_TX_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

NETCHAN_APP_TX_STATIC_ASSERT(sizeof(netchan_app_tx_prepare_info_v1_t) == 32,
                             "netchan app TX prepare info V1 ABI drift");
NETCHAN_APP_TX_STATIC_ASSERT(sizeof(netchan_app_tx_prepare_output_v1_t) == 24,
                             "netchan app TX prepare output V1 ABI drift");
NETCHAN_APP_TX_STATIC_ASSERT(sizeof(netchan_app_tx_completion_info_v1_t) == 32,
                             "netchan app TX completion info V1 ABI drift");
NETCHAN_APP_TX_STATIC_ASSERT(sizeof(netchan_app_rx_info_v1_t) == 32,
                             "netchan app RX info V1 ABI drift");
NETCHAN_APP_TX_STATIC_ASSERT(sizeof(netchan_app_rx_output_v1_t) == 16,
                             "netchan app RX output V1 ABI drift");

#undef NETCHAN_APP_TX_STATIC_ASSERT

static inline bool Netchan_SeqTooBig(const netchan_t *chan)
{
    return NetSequence_NearExhaustion(chan->outgoing_sequence,
                                      31u - (unsigned)chan->type, 256u);
}

#define OOB_PRINT(sock, addr, data) \
    NET_SendPacket(sock, CONST_STR_LEN("\xff\xff\xff\xff" data), addr)

#ifdef __cplusplus
}
#endif

