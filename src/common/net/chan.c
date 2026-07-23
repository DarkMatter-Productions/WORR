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

#include "shared/shared.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/msg.h"
#include "common/net/chan.h"
#include "common/net/net.h"
#include "common/protocol.h"
#include "common/sizebuf.h"
#include "common/zone.h"
#include "system/system.h"

/*

packet header
-------------
31  sequence
1   does this message contain a reliable payload
31  acknowledge sequence
1   acknowledge receipt of even/odd message
16  qport

The remote connection never knows if it missed a reliable message, the
local side detects that it has been dropped by seeing a sequence acknowledge
higher than the last reliable sequence, but without the correct even/odd
bit for the reliable set.

If the sender notices that a reliable message has been dropped, it will be
retransmitted.  It will not be retransmitted again until a message after
the retransmit has been acknowledged and the reliable still failed to get there.

if the sequence number is -1, the packet should be handled without a netcon

The reliable message can be added to at any time by doing
MSG_Write* (&netchan->message, <data>).

If the message buffer is overflowed, either by a single message, or by
multiple frames worth piling up while the last reliable transmit goes
unacknowledged, the netchan signals a fatal error.

Reliable messages are always placed first in a packet, then the unreliable
message is included if there is sufficient room.

To the receiver, there is no distinction between the reliable and unreliable
parts of the message, they are just processed out as a single larger message.

Illogical packet sequence numbers cause the packet to be dropped, but do
not kill the connection.  This, combined with the tight window of valid
reliable acknowledgement numbers provides protection against malicious
address spoofing.


The qport field is a workaround for bad address translating routers that
sometimes remap the client's source port on a packet during gameplay.

If the base part of the net address matches and the qport matches, then the
channel matches even if the IP port differs.  The IP port should be updated
to the new value before sending out any replies.


If there is no information that needs to be transferred on a given frame,
such as during the connection stage while waiting for the client to load,
then a packet only needs to be delivered if there is something in the
unacknowledged reliable
*/

#if USE_DEBUG
static cvar_t       *showpackets;
static cvar_t       *showdrop;
#define SHOWPACKET(...) \
    do { if (showpackets->integer) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
#define SHOWDROP(...) \
    do { if (showdrop->integer) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
#else
#define SHOWPACKET(...)
#define SHOWDROP(...)
#endif

#define REL_BIT     BIT(31)
#define FRG_BIT     BIT(30)
#define OLD_MASK    (REL_BIT - 1)
#define NEW_MASK    (FRG_BIT - 1)

cvar_t      *net_qport;
cvar_t      *net_maxmsglen;
cvar_t      *net_chantype;

static bool Netchan_ProcessAcknowledgement(netchan_t *chan,
                                           unsigned sequence_ack,
                                           bool reliable_ack)
{
    const net_sequence_ack_result_t result = NetSequence_ClassifyAck(
        sequence_ack, chan->incoming_acknowledged, chan->outgoing_sequence);

    if (result == NET_SEQUENCE_ACK_STALE) {
        chan->stale_acknowledgements++;
        SHOWDROP("%s: ignored stale acknowledgement %u at %u\n",
                 NET_AdrToString(&chan->remote_address), sequence_ack,
                 chan->incoming_acknowledged);
        return true;
    }

    if (result == NET_SEQUENCE_ACK_FUTURE) {
        chan->future_acknowledgements++;
        SHOWDROP("%s: rejected future acknowledgement %u before %u\n",
                 NET_AdrToString(&chan->remote_address), sequence_ack,
                 chan->outgoing_sequence);
        return false;
    }

    if (!NetSequence_ReliableAckConsistent(
            sequence_ack, reliable_ack, chan->incoming_acknowledged,
            chan->incoming_reliable_acknowledged)) {
        chan->conflicting_acknowledgements++;
        SHOWDROP("%s: ignored conflicting reliable acknowledgement at %u\n",
                 NET_AdrToString(&chan->remote_address), sequence_ack);
        return true;
    }

    chan->incoming_acknowledged = sequence_ack;
    chan->incoming_reliable_acknowledged = reliable_ack;
    if (reliable_ack == chan->reliable_sequence)
        chan->reliable_length = 0;
    return true;
}

// allow either 0 (no hard limit), or an integer between 512 and 4086
static void net_maxmsglen_changed(cvar_t *self)
{
    if (self->integer) {
        Cvar_ClampInteger(self, MIN_PACKETLEN, MAX_PACKETLEN_WRITABLE);
    }
}

/*
===============
Netchan_Init

===============
*/
void Netchan_Init(void)
{
#if USE_DEBUG
    showpackets = Cvar_Get("showpackets", "0", 0);
    showdrop = Cvar_Get("showdrop", "0", 0);
#endif

    // pick a port value that should be nice and random
    net_qport = Cvar_Get("qport", va("%d", Sys_Milliseconds() & 0xffff), 0);
    net_maxmsglen = Cvar_Get("net_maxmsglen", va("%d", MAX_PACKETLEN_WRITABLE_DEFAULT), 0);
    net_maxmsglen->changed = net_maxmsglen_changed;
    net_chantype = Cvar_Get("net_chantype", "1", 0);
}

/*
===============
Netchan_OutOfBand

Sends a text message in an out-of-band datagram
================
*/
void Netchan_OutOfBand(netsrc_t sock, const netadr_t *address, const char *format, ...)
{
    va_list     argptr;
    char        data[MAX_PACKETLEN_DEFAULT];
    size_t      len;

    // write the packet header
    memset(data, 0xff, 4);

    va_start(argptr, format);
    len = Q_vsnprintf(data + 4, sizeof(data) - 4, format, argptr);
    va_end(argptr);

    if (len >= sizeof(data) - 4) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    // send the datagram
    NET_SendPacket(sock, data, len + 4, address);
}

// ============================================================================

/*
===============
NetchanOld_Transmit

tries to send an unreliable message to a connection, and handles the
transmission / retransmission of the reliable messages.

A 0 length will still generate a packet and deal with the reliable messages.
================
*/
static int NetchanOld_Transmit(netchan_t *chan, size_t length, const void *data, int numpackets)
{
    sizebuf_t   send;
    byte        send_buf[MAX_PACKETLEN];
    bool        send_reliable;
    unsigned    w1, w2;

    send_reliable = false;

    // if the remote side dropped the last reliable message, resend it
    if (chan->incoming_acknowledged > chan->last_reliable_sequence &&
        chan->incoming_reliable_acknowledged != chan->reliable_sequence) {
        send_reliable = true;
    }

// if the reliable transmit buffer is empty, copy the current message out
    if (!chan->reliable_length && chan->message.cursize) {
        send_reliable = true;
        memcpy(chan->reliable_buf, chan->message.data, chan->message.cursize);
        chan->reliable_length = chan->message.cursize;
        chan->message.cursize = 0;
        chan->reliable_sequence ^= 1;
    }

// write the packet header
    w1 = chan->outgoing_sequence & OLD_MASK;
    if (send_reliable)
        w1 |= REL_BIT;

    w2 = chan->incoming_sequence & OLD_MASK;
    if (chan->incoming_reliable_sequence)
        w2 |= REL_BIT;

    SZ_Init(&send, send_buf, sizeof(send_buf), "nc_send_old");

    SZ_WriteLong(&send, w1);
    SZ_WriteLong(&send, w2);

#if USE_CLIENT
    // send the qport if we are a client
    if (chan->sock == NS_CLIENT) {
        if (chan->protocol < PROTOCOL_VERSION_R1Q2) {
            SZ_WriteShort(&send, chan->qport);
        } else if (chan->qport) {
            SZ_WriteByte(&send, chan->qport);
        }
    }
#endif

// copy the reliable message to the packet first
    if (send_reliable) {
        SZ_Write(&send, chan->reliable_buf, chan->reliable_length);
        chan->last_reliable_sequence = chan->outgoing_sequence;
    }

// add the unreliable part if space is available
    if (send.maxsize - send.cursize >= length)
        SZ_Write(&send, data, length);
    else
        Com_WPrintf("%s: dumped unreliable\n", NET_AdrToString(&chan->remote_address));

    SHOWPACKET("send %4u : s=%u ack=%u rack=%d",
               send.cursize,
               chan->outgoing_sequence,
               chan->incoming_sequence,
               chan->incoming_reliable_sequence);
    if (send_reliable) {
        SHOWPACKET(" reliable=%d", chan->reliable_sequence);
    }
    SHOWPACKET("\n");

    // send the datagram
    for (int i = 0; i < numpackets; i++) {
        NET_SendPacket(chan->sock, send.data, send.cursize, &chan->remote_address);
    }

    chan->outgoing_sequence++;
    chan->reliable_ack_pending = false;
    chan->last_sent = com_localTime;

    return send.cursize * numpackets;
}

/*
=================
NetchanOld_Process

called when the current net_message is from remote_address
modifies net_message so that it points to the packet payload
=================
*/
static bool NetchanOld_Process(netchan_t *chan)
{
    unsigned    sequence, sequence_ack;
    bool        reliable_ack, reliable_message;

// get sequence numbers
    MSG_BeginReading();
    sequence = MSG_ReadLong();
    sequence_ack = MSG_ReadLong();

    // read the qport if we are a server
    if (chan->sock == NS_SERVER) {
        if (chan->protocol < PROTOCOL_VERSION_R1Q2) {
            MSG_ReadShort();
        } else if (chan->qport) {
            MSG_ReadByte();
        }
    }

    if (msg_read.readcount > msg_read.cursize) {
        SHOWDROP("%s: message too short\n",
                 NET_AdrToString(&chan->remote_address));
        return false;
    }

    reliable_message = sequence & REL_BIT;
    reliable_ack = sequence_ack & REL_BIT;

    sequence &= OLD_MASK;
    sequence_ack &= OLD_MASK;

    SHOWPACKET("recv %4u : s=%u ack=%u rack=%d",
               msg_read.cursize,
               sequence,
               sequence_ack,
               reliable_ack);
    if (reliable_message) {
        SHOWPACKET(" reliable=%d", chan->incoming_reliable_sequence ^ 1);
    }
    SHOWPACKET("\n");

//
// discard stale or duplicated packets
//
    if (!NetSequence_IsNewer(sequence, chan->incoming_sequence)) {
        SHOWDROP("%s: out of order packet %u at %u\n",
                 NET_AdrToString(&chan->remote_address),
                 sequence, chan->incoming_sequence);
        return false;
    }

//
// dropped packets don't keep the message from being used
//
    chan->dropped = sequence - (chan->incoming_sequence + 1);
    if (chan->dropped > 0) {
        SHOWDROP("%s: dropped %u packets at %u\n",
                 NET_AdrToString(&chan->remote_address),
                 chan->dropped, sequence);
    }

//
// if the current outgoing reliable message has been acknowledged
// clear the buffer to make way for the next
//
    if (!Netchan_ProcessAcknowledgement(chan, sequence_ack, reliable_ack))
        return false;

//
// if this message contains a reliable message, bump incoming_reliable_sequence
//
    chan->incoming_sequence = sequence;
    if (reliable_message) {
        chan->reliable_ack_pending = true;
        chan->incoming_reliable_sequence ^= 1;
    }

//
// the message can now be read from the current message pointer
//
    chan->last_received = com_localTime;

    chan->total_dropped += chan->dropped;
    chan->total_received += chan->dropped + 1;

    return true;
}

// ============================================================================

/*
===============
Netchan_TransmitNextFragment
================
*/
int Netchan_TransmitNextFragment(netchan_t *chan)
{
    sizebuf_t   send;
    byte        send_buf[MAX_PACKETLEN];
    bool        send_reliable, more_fragments;
    unsigned    w1, w2, offset, fragment_length;

    Q_assert(chan->type);

    send_reliable = chan->reliable_length;

    // write the packet header
    w1 = (chan->outgoing_sequence & NEW_MASK) | FRG_BIT;
    if (send_reliable)
        w1 |= REL_BIT;

    w2 = chan->incoming_sequence & NEW_MASK;
    if (chan->incoming_reliable_sequence)
        w2 |= REL_BIT;

    SZ_Init(&send, send_buf, sizeof(send_buf), "nc_send_frg");

    SZ_WriteLong(&send, w1);
    SZ_WriteLong(&send, w2);

#if USE_CLIENT
    // send the qport if we are a client
    if (chan->sock == NS_CLIENT && chan->qport) {
        SZ_WriteByte(&send, chan->qport);
    }
#endif

    fragment_length = chan->fragment_out.cursize - chan->fragment_out.readcount;
    if (fragment_length > chan->maxpacketlen) {
        fragment_length = chan->maxpacketlen;
    }

    more_fragments = true;
    if (chan->fragment_out.readcount + fragment_length == chan->fragment_out.cursize) {
        more_fragments = false;
    }

    // write fragment offset
    offset = chan->fragment_out.readcount & 0x7FFF;
    if (more_fragments)
        offset |= 0x8000;
    SZ_WriteShort(&send, offset);

    // write fragment contents
    SZ_Write(&send, chan->fragment_out.data + chan->fragment_out.readcount, fragment_length);

    SHOWPACKET("send %4u : s=%u ack=%u rack=%d "
               "fragment_offset=%u more_fragments=%d",
               send.cursize,
               chan->outgoing_sequence,
               chan->incoming_sequence,
               chan->incoming_reliable_sequence,
               chan->fragment_out.readcount,
               more_fragments);
    if (send_reliable) {
        SHOWPACKET(" reliable=%d", chan->reliable_sequence);
    }
    SHOWPACKET("\n");

    chan->fragment_out.readcount += fragment_length;
    chan->fragment_pending = more_fragments;

    // if the message has been sent completely, clear the fragment buffer
    if (!chan->fragment_pending) {
        chan->outgoing_sequence++;
        chan->last_sent = com_localTime;
        SZ_Clear(&chan->fragment_out);
    }

    // send the datagram
    NET_SendPacket(chan->sock, send.data, send.cursize, &chan->remote_address);

    return send.cursize;
}

/*
===============
NetchanNew_Transmit
================
*/
static int NetchanNew_Transmit(netchan_t *chan, size_t length,
                               const void *data, int numpackets,
                               bool force_reliable)
{
    sizebuf_t   send;
    byte        send_buf[MAX_PACKETLEN];
    byte        staged_application[MAX_PACKETLEN_WRITABLE];
    bool        send_reliable;
    unsigned    w1, w2;
    uint32_t    application_offset, legacy_application_bytes, accepted_copies;
    netchan_app_tx_prepare_fn prepare;
    netchan_app_tx_completion_fn completion;
    void *hook_opaque;
    netchan_app_tx_prepare_result_t prepare_result;
    netchan_app_tx_completion_result_t completion_result;
    netchan_app_tx_prepare_info_v1_t prepare_info;
    netchan_app_tx_prepare_output_v1_t prepare_output;
    netchan_app_tx_completion_info_v1_t completion_info;
    bool prepared_application;

    if (chan->fragment_pending) {
        return Netchan_TransmitNextFragment(chan);
    }

    send_reliable = force_reliable;

// if the remote side dropped the last reliable message, resend it
    if (chan->incoming_acknowledged > chan->last_reliable_sequence &&
        chan->incoming_reliable_acknowledged != chan->reliable_sequence) {
        send_reliable = true;
    }

// if the reliable transmit buffer is empty, copy the current message out
    if (!chan->reliable_length && chan->message.cursize) {
        send_reliable = true;
        memcpy(chan->reliable_buf, chan->message.data, chan->message.cursize);
        chan->reliable_length = chan->message.cursize;
        chan->message.cursize = 0;
        chan->reliable_sequence ^= 1;
    }

    if (length > chan->maxpacketlen || (send_reliable &&
                                        (chan->reliable_length + length > chan->maxpacketlen))) {
        if (send_reliable) {
            chan->last_reliable_sequence = chan->outgoing_sequence;
            SZ_Write(&chan->fragment_out, chan->reliable_buf, chan->reliable_length);
        }
        // add the unreliable part if space is available
        if (chan->fragment_out.maxsize - chan->fragment_out.cursize >= length)
            SZ_Write(&chan->fragment_out, data, length);
        else
            Com_WPrintf("%s: dumped unreliable\n", NET_AdrToString(&chan->remote_address));
        return Netchan_TransmitNextFragment(chan);
    }

// write the packet header
    w1 = chan->outgoing_sequence & NEW_MASK;
    if (send_reliable)
        w1 |= REL_BIT;

    w2 = chan->incoming_sequence & NEW_MASK;
    if (chan->incoming_reliable_sequence)
        w2 |= REL_BIT;

    SZ_Init(&send, send_buf, sizeof(send_buf), "nc_send_new");

    SZ_WriteLong(&send, w1);
    SZ_WriteLong(&send, w2);

#if USE_CLIENT
    // send the qport if we are a client
    if (chan->sock == NS_CLIENT && chan->qport) {
        SZ_WriteByte(&send, chan->qport);
    }
#endif

    application_offset = send.cursize;

    // copy the reliable message to the packet first
    if (send_reliable) {
        chan->last_reliable_sequence = chan->outgoing_sequence;
        SZ_Write(&send, chan->reliable_buf, chan->reliable_length);
    }

    // add the unreliable part
    SZ_Write(&send, data, length);

    /*
     * This is deliberately after complete reliable+unreliable assembly and
     * deliberately absent from Netchan_TransmitNextFragment.  Keep the
     * original send buffer untouched until a staged candidate validates.
     */
    prepare = chan->app_tx_prepare;
    completion = chan->app_tx_completion;
    hook_opaque = chan->app_tx_opaque;
    prepared_application = false;
    completion_result = NETCHAN_APP_TX_COMPLETION_PREPARE_INVALID;
    legacy_application_bytes = send.cursize - application_offset;

    if (prepare && completion) {
        memset(&prepare_info, 0, sizeof(prepare_info));
        prepare_info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
        prepare_info.struct_size = (uint32_t)sizeof(prepare_info);
        prepare_info.outgoing_sequence = chan->outgoing_sequence;
        prepare_info.max_application_bytes = chan->maxpacketlen;
        prepare_info.reliable_bytes = send_reliable ? chan->reliable_length : 0;
        prepare_info.unreliable_bytes = (uint32_t)length;
        prepare_info.legacy_application_bytes = legacy_application_bytes;
        prepare_info.packet_copies =
            (uint32_t)(numpackets > 0 ? numpackets : 0);

        memset(&prepare_output, 0, sizeof(prepare_output));
        prepare_output.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
        prepare_output.struct_size = (uint32_t)sizeof(prepare_output);
        memset(staged_application, 0, sizeof(staged_application));

        prepare_result = prepare(hook_opaque, &prepare_info,
                                 send.data + application_offset,
                                 staged_application, &prepare_output);

        if (prepare_result != NETCHAN_APP_TX_PREPARE_BYPASS) {
            prepared_application =
                prepare_result == NETCHAN_APP_TX_PREPARE_PREPARED &&
                prepare_output.abi_version == NETCHAN_APP_TX_HOOK_ABI_V1 &&
                prepare_output.struct_size == (uint32_t)sizeof(prepare_output) &&
                prepare_output.reserved0 == 0 &&
                prepare_output.application_bytes <= chan->maxpacketlen;

            if (prepared_application) {
                send.cursize = application_offset;
                SZ_Write(&send, staged_application,
                         prepare_output.application_bytes);
            }
        }
    } else {
        prepare_result = NETCHAN_APP_TX_PREPARE_BYPASS;
        memset(&prepare_output, 0, sizeof(prepare_output));
    }

    SHOWPACKET("send %4u : s=%u ack=%u rack=%d",
               send.cursize,
               chan->outgoing_sequence,
               chan->incoming_sequence,
               chan->incoming_reliable_sequence);
    if (send_reliable) {
        SHOWPACKET(" reliable=%d", chan->reliable_sequence);
    }
    SHOWPACKET("\n");

    // send the datagram
    accepted_copies = 0;
    for (int i = 0; i < numpackets; i++) {
        if (NET_SendPacket(chan->sock, send.data, send.cursize,
                           &chan->remote_address)) {
            accepted_copies++;
        }
    }

    chan->outgoing_sequence++;
    chan->reliable_ack_pending = false;
    chan->last_sent = com_localTime;

    if (prepare_result != NETCHAN_APP_TX_PREPARE_BYPASS) {
        if (prepared_application) {
            completion_result = accepted_copies
                ? NETCHAN_APP_TX_COMPLETION_ACCEPTED
                : NETCHAN_APP_TX_COMPLETION_NOT_ACCEPTED;
        }

        memset(&completion_info, 0, sizeof(completion_info));
        completion_info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
        completion_info.struct_size = (uint32_t)sizeof(completion_info);
        completion_info.result = (uint32_t)completion_result;
        completion_info.packet_copies =
            (uint32_t)(numpackets > 0 ? numpackets : 0);
        completion_info.accepted_copies = accepted_copies;
        completion_info.application_bytes = send.cursize - application_offset;
        completion_info.token = prepare_output.token;
        completion(hook_opaque, &completion_info,
                   send.data + application_offset);
    }

    return send.cursize * numpackets;
}

/*
=================
NetchanNew_Process
=================
*/
static netchan_process_result_t NetchanNew_Process(netchan_t *chan)
{
    unsigned    sequence, sequence_ack, fragment_offset, length;
    bool        reliable_message, reliable_ack, fragmented_message, more_fragments;

// get sequence numbers
    MSG_BeginReading();
    sequence = MSG_ReadLong();
    sequence_ack = MSG_ReadLong();

    // read the qport if we are a server
    if (chan->sock == NS_SERVER && chan->qport) {
        MSG_ReadByte();
    }

    reliable_message = sequence & REL_BIT;
    reliable_ack = sequence_ack & REL_BIT;
    fragmented_message = sequence & FRG_BIT;

    sequence &= NEW_MASK;
    sequence_ack &= NEW_MASK;

    fragment_offset = 0;
    more_fragments = false;
    if (fragmented_message) {
        fragment_offset = MSG_ReadWord();
        more_fragments = fragment_offset & 0x8000;
        fragment_offset &= 0x7FFF;
    }

    if (msg_read.readcount > msg_read.cursize) {
        SHOWDROP("%s: message too short\n",
                 NET_AdrToString(&chan->remote_address));
        return NETCHAN_PROCESS_NO_APPLICATION;
    }

    SHOWPACKET("recv %4u : s=%u ack=%u rack=%d",
               msg_read.cursize, sequence, sequence_ack, reliable_ack);
    if (fragmented_message) {
        SHOWPACKET(" fragment_offset=%u more_fragments=%d",
                   fragment_offset, more_fragments);
    }
    if (reliable_message) {
        SHOWPACKET(" reliable=%d", chan->incoming_reliable_sequence ^ 1);
    }
    SHOWPACKET("\n");

//
// discard stale or duplicated packets
//
    if (!NetSequence_IsNewer(sequence, chan->incoming_sequence)) {
        SHOWDROP("%s: out of order packet %u at %u\n",
                 NET_AdrToString(&chan->remote_address),
                 sequence, chan->incoming_sequence);
        return NETCHAN_PROCESS_NO_APPLICATION;
    }

//
// dropped packets don't keep the message from being used
//
    chan->dropped = sequence - (chan->incoming_sequence + 1);
    if (chan->dropped > 0) {
        SHOWDROP("%s: dropped %u packets at %u\n",
                 NET_AdrToString(&chan->remote_address),
                 chan->dropped, sequence);
    }

//
// if the current outgoing reliable message has been acknowledged
// clear the buffer to make way for the next
//
    if (!Netchan_ProcessAcknowledgement(chan, sequence_ack, reliable_ack))
        return NETCHAN_PROCESS_NO_APPLICATION;

//
// parse fragment header, if any
//
    if (fragmented_message) {
        if (chan->fragment_sequence != sequence) {
            // start new receive sequence
            chan->fragment_sequence = sequence;
            SZ_Clear(&chan->fragment_in);
        }

        if (fragment_offset < chan->fragment_in.cursize) {
            SHOWDROP("%s: out of order fragment at %u\n",
                     NET_AdrToString(&chan->remote_address), sequence);
            return NETCHAN_PROCESS_NO_APPLICATION;
        }

        if (fragment_offset > chan->fragment_in.cursize) {
            SHOWDROP("%s: dropped fragment(s) at %u\n",
                     NET_AdrToString(&chan->remote_address), sequence);
            return NETCHAN_PROCESS_NO_APPLICATION;
        }

        length = msg_read.cursize - msg_read.readcount;
        if (length > chan->fragment_in.maxsize - chan->fragment_in.cursize) {
            SHOWDROP("%s: oversize fragment at %u\n",
                     NET_AdrToString(&chan->remote_address), sequence);
            return NETCHAN_PROCESS_NO_APPLICATION;
        }

        SZ_Write(&chan->fragment_in, msg_read.data + msg_read.readcount, length);
        if (more_fragments) {
            return NETCHAN_PROCESS_NO_APPLICATION;
        }

        // message has been successfully assembled
        memcpy(msg_read_buffer, chan->fragment_in.data, chan->fragment_in.cursize);
        SZ_InitRead(&msg_read, msg_read_buffer, chan->fragment_in.cursize);
        SZ_Clear(&chan->fragment_in);
    }

    chan->incoming_sequence = sequence;

//
// if this message contains a reliable message, bump incoming_reliable_sequence
//
    if (reliable_message) {
        chan->reliable_ack_pending = true;
        chan->incoming_reliable_sequence ^= 1;
    }

//
// the message can now be read from the current message pointer
//
    chan->last_received = com_localTime;

    chan->total_dropped += chan->dropped;
    chan->total_received += chan->dropped + 1;

    if (chan->app_rx) {
        const sizebuf_t saved = msg_read;
        const uint32_t application_bytes =
            saved.cursize - saved.readcount;
        netchan_app_rx_info_v1_t info;
        netchan_app_rx_output_v1_t output;
        netchan_app_rx_fn receive = chan->app_rx;
        void *hook_opaque = chan->app_rx_opaque;
        netchan_app_rx_result_t result;
        bool descriptor_intact;

        memset(&info, 0, sizeof(info));
        info.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
        info.struct_size = (uint32_t)sizeof(info);
        info.incoming_sequence = sequence;
        info.message_bytes = saved.cursize;
        info.application_offset = saved.readcount;
        info.application_bytes = application_bytes;
        if (reliable_message)
            info.flags |= NETCHAN_APP_RX_FLAG_RELIABLE;
        if (fragmented_message)
            info.flags |= NETCHAN_APP_RX_FLAG_REASSEMBLED;

        memset(&output, 0, sizeof(output));
        output.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
        output.struct_size = (uint32_t)sizeof(output);

        result = receive(hook_opaque, &info,
                         saved.data + saved.readcount, &output);

        /* A callback never owns the shared reader descriptor. */
        descriptor_intact =
            memcmp(&msg_read, &saved, sizeof(saved)) == 0;
        msg_read = saved;
        if (descriptor_intact && result == NETCHAN_APP_RX_BYPASS)
            return NETCHAN_PROCESS_APPLICATION_READY;

        if (descriptor_intact &&
            result == NETCHAN_APP_RX_EXPOSE_LEGACY &&
            output.abi_version == NETCHAN_APP_RX_HOOK_ABI_V1 &&
            output.struct_size == (uint32_t)sizeof(output) &&
            output.reserved0 == 0 &&
            output.legacy_bytes <= application_bytes) {
            msg_read.cursize = saved.readcount + output.legacy_bytes;
            return NETCHAN_PROCESS_APPLICATION_READY;
        }

        /* Never expose rejected or structurally invalid suffix bytes. */
        msg_read.cursize = saved.readcount;
        return NETCHAN_PROCESS_APPLICATION_REJECTED;
    }

    return NETCHAN_PROCESS_APPLICATION_READY;
}

netchan_process_result_t Netchan_ProcessEx(netchan_t *chan)
{
    if (chan->type)
        return NetchanNew_Process(chan);

    return NetchanOld_Process(chan)
               ? NETCHAN_PROCESS_APPLICATION_READY
               : NETCHAN_PROCESS_NO_APPLICATION;
}

bool Netchan_Process(netchan_t *chan)
{
    return Netchan_ProcessEx(chan) == NETCHAN_PROCESS_APPLICATION_READY;
}

int Netchan_Transmit(netchan_t *chan, size_t length, const void *data, int numpackets)
{
    // overflow is detected externally now, so this should never happen.
    if (chan->message.overflowed) {
        Com_WPrintf("%s: outgoing message overflow\n", NET_AdrToString(&chan->remote_address));
        return 0;
    }

    if (chan->type)
        return NetchanNew_Transmit(
            chan, length, data, numpackets, false);

    return NetchanOld_Transmit(chan, length, data, numpackets);
}

static bool Netchan_ReliableHandoffReady(const netchan_t *chan,
                                         int numpackets)
{
    return chan != NULL && chan->type == NETCHAN_NEW &&
           numpackets > 0 && chan->reliable_buf != NULL &&
           chan->message.data != NULL && !chan->message.overflowed &&
           chan->message.maxsize != 0 && chan->message.readcount == 0 &&
           chan->message.cursize <= chan->message.maxsize &&
           chan->reliable_length == 0 && !chan->fragment_pending &&
           chan->fragment_out.data != NULL &&
           !chan->fragment_out.overflowed &&
           chan->fragment_out.maxsize != 0 &&
           chan->fragment_out.cursize == 0 &&
           chan->fragment_out.readcount == 0;
}

bool Netchan_TransmitQueuedReliablePrefix(
    netchan_t *chan, uint32_t prefix_bytes, int numpackets,
    int *transmit_bytes_out)
{
    uint32_t tail_bytes;
    int transmit_bytes;

    if (transmit_bytes_out)
        *transmit_bytes_out = 0;
    if (!Netchan_ReliableHandoffReady(chan, numpackets) ||
        prefix_bytes == 0 || prefix_bytes > chan->message.cursize ||
        prefix_bytes > chan->fragment_out.maxsize) {
        return false;
    }

    tail_bytes = chan->message.cursize - prefix_bytes;
    memmove(chan->reliable_buf, chan->message.data, prefix_bytes);
    if (tail_bytes != 0) {
        memmove(chan->message.data,
                chan->message.data + prefix_bytes,
                tail_bytes);
    }
    chan->message.cursize = tail_bytes;
    chan->reliable_length = prefix_bytes;
    chan->reliable_sequence ^= 1;

    /* The ordinary transmit path seals message before invoking the hook.  Do
     * the same here so callback-appended bytes join the visible preserved tail
     * as the next reliable generation instead of being overwritten later. */
    transmit_bytes = NetchanNew_Transmit(
        chan, 0, NULL, numpackets, true);
    if (transmit_bytes_out)
        *transmit_bytes_out = transmit_bytes;
    return true;
}

bool Netchan_TransmitIsolatedReliable(
    netchan_t *chan, const void *reliable_data, uint32_t reliable_bytes,
    int numpackets, int *transmit_bytes_out)
{
    int transmit_bytes;

    if (transmit_bytes_out)
        *transmit_bytes_out = 0;
    if (!Netchan_ReliableHandoffReady(chan, numpackets) ||
        reliable_data == NULL || reliable_bytes == 0 ||
        reliable_bytes > chan->message.maxsize ||
        reliable_bytes > chan->fragment_out.maxsize) {
        return false;
    }

    memmove(chan->reliable_buf, reliable_data, reliable_bytes);
    chan->reliable_length = reliable_bytes;
    chan->reliable_sequence ^= 1;
    transmit_bytes = NetchanNew_Transmit(
        chan, 0, NULL, numpackets, true);
    if (transmit_bytes_out)
        *transmit_bytes_out = transmit_bytes;
    return true;
}

/*
==============
Netchan_ShouldUpdate
==============
*/
bool Netchan_ShouldUpdate(const netchan_t *chan)
{
    return chan->message.cursize
        || chan->reliable_ack_pending
        || chan->fragment_out.cursize
        || com_localTime - chan->last_sent > 1000;
}

/*
==============
Netchan_Setup
==============
*/
void Netchan_Setup(netchan_t *chan, netsrc_t sock, netchan_type_t type,
                   const netadr_t *adr, int qport, size_t maxpacketlen, int protocol)
{
    memtag_t tag = sock == NS_SERVER ? TAG_SERVER : TAG_GENERAL;
    byte *buf;

    Q_assert(chan);
    Q_assert(!chan->reliable_buf);
    Q_assert(adr);
    Q_assert(maxpacketlen >= MIN_PACKETLEN);
    Q_assert(maxpacketlen <= MAX_PACKETLEN_WRITABLE);

    chan->type = type;
    chan->protocol = protocol;
    chan->sock = sock;
    chan->remote_address = *adr;
    chan->qport = qport;
    chan->maxpacketlen = maxpacketlen;
    chan->last_received = com_localTime;
    chan->last_sent = com_localTime;
    chan->incoming_sequence = 0;
    chan->outgoing_sequence = 1;

    switch (type) {
    case NETCHAN_OLD:
        chan->reliable_buf = buf = Z_TagMalloc(maxpacketlen * 2, tag);
        SZ_InitWrite(&chan->message, buf + maxpacketlen, maxpacketlen);
        break;

    case NETCHAN_NEW:
        chan->reliable_buf = buf = Z_TagMalloc(MAX_MSGLEN * 4, tag);
        SZ_InitWrite(&chan->message, buf + MAX_MSGLEN, MAX_MSGLEN);
        SZ_Init(&chan->fragment_in, buf + MAX_MSGLEN * 2, MAX_MSGLEN, "nc_frg_in");
        SZ_Init(&chan->fragment_out, buf + MAX_MSGLEN * 3, MAX_MSGLEN, "nc_frg_out");
        break;

    default:
        Q_assert(!"bad type");
    }
}

/*
==============================
Netchan_SetApplicationTxHook
==============================
*/
bool Netchan_SetApplicationTxHook(netchan_t *chan,
                                  netchan_app_tx_prepare_fn prepare,
                                  netchan_app_tx_completion_fn completion,
                                  void *opaque)
{
    if (!chan || chan->type != NETCHAN_NEW)
        return false;

    if (!!prepare != !!completion)
        return false;

    chan->app_tx_prepare = prepare;
    chan->app_tx_completion = completion;
    chan->app_tx_opaque = prepare ? opaque : NULL;
    return true;
}

/*
==============================
Netchan_SetApplicationRxHook
==============================
*/
bool Netchan_SetApplicationRxHook(netchan_t *chan,
                                  netchan_app_rx_fn receive,
                                  void *opaque)
{
    if (!chan || chan->type != NETCHAN_NEW)
        return false;

    chan->app_rx = receive;
    chan->app_rx_opaque = receive ? opaque : NULL;
    return true;
}

/*
==============
Netchan_Close
==============
*/
void Netchan_Close(netchan_t *chan)
{
    Q_assert(chan);
    Z_Free(chan->reliable_buf);
    memset(chan, 0, sizeof(*chan));
}
