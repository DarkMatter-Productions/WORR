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
// sv_send.c

#include "server.h"
#include "server/native_shadow.h"
#include "common/net/legacy_damage_event_candidate.h"
#include "common/net/legacy_game_event_candidate.h"
#include "common/net/legacy_poi_event_candidate.h"

#include <limits.h>

/*
=============================================================================

MISC

=============================================================================
*/

char sv_outputbuf[SV_OUTPUTBUF_LENGTH];

void SV_FlushRedirect(int redirected, const char *outputbuf, size_t len)
{
    byte    buffer[MAX_PACKETLEN_DEFAULT];

    if (redirected == RD_PACKET) {
        Q_assert(len <= sizeof(buffer) - 10);
        memcpy(buffer, "\xff\xff\xff\xffprint\n", 10);
        memcpy(buffer + 10, outputbuf, len);
        NET_SendPacket(NS_SERVER, buffer, len + 10, &net_from);
    } else if (redirected == RD_CLIENT) {
        q2proto_svc_message_t message = {.type = Q2P_SVC_PRINT, .print = {0}};
        message.print.level = PRINT_HIGH;
        message.print.string.str = outputbuf;
        message.print.string.len = len;
        q2proto_server_write(&sv_client->q2proto_ctx, (uintptr_t)&sv_client->io_data, &message);
        SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
    }
}

/*
=======================
SV_RateDrop

Returns true if the client is over its current
bandwidth estimation and should not be sent another packet
=======================
*/
static bool SV_RateDrop(client_t *client)
{
    size_t  total;
    int     i;
    const uint32_t current_frame = (uint32_t)client->framenum;

    // never drop over the loopback
    if (!client->rate) {
        return false;
    }

    total = 0;
    for (i = 0; i < RATE_MESSAGES; i++) {
        const size_t normal_bytes = client->message_size[i];
        size_t async_bytes = 0;

        if (client->async_message_valid[i] &&
            current_frame - client->async_message_frame[i] <
                RATE_MESSAGES) {
            async_bytes = client->async_message_size[i];
        }
        if (normal_bytes > (size_t)-1 - total)
            total = (size_t)-1;
        else
            total += normal_bytes;
        if (async_bytes > (size_t)-1 - total)
            total = (size_t)-1;
        else
            total += async_bytes;
    }

#if USE_FPS
    total = total * sv.frametime.div / client->framediv;
#endif

    if (total > client->rate) {
        SV_DPrintf(1, "Frame %d suppressed for %s (total = %zu)\n",
                   client->framenum, client->name, total);
        client->frameflags |= FF_SUPPRESSED;
        client->suppress_count++;
        client->message_size[client->framenum % RATE_MESSAGES] = 0;
        return true;
    }

    return false;
}

static void SV_CalcSendTime(client_t *client, unsigned size,
                            bool async_send)
{
    // never drop over the loopback
    if (!client->rate) {
        client->send_time = svs.realtime;
        client->send_delta = 0;
        return;
    }

    if (client->state == cs_spawned) {
        const uint32_t frame = (uint32_t)client->framenum;
        const uint32_t index = frame % RATE_MESSAGES;

        if (!async_send) {
            client->message_size[index] = size;
        } else {
            if (!client->async_message_valid[index] ||
                client->async_message_frame[index] != frame) {
                client->async_message_size[index] = 0;
                client->async_message_frame[index] = frame;
                client->async_message_valid[index] = true;
            }
            if (size > UINT_MAX - client->async_message_size[index])
                client->async_message_size[index] = UINT_MAX;
            else
                client->async_message_size[index] += size;
        }
    }

    client->send_time = svs.realtime;
    client->send_delta = size * 1000 / client->rate;
}

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/


/*
=================
SV_ClientPrintf

Sends text across to be displayed if the level passes.
NOT archived in MVD stream.
=================
*/
void SV_ClientPrintf(client_t *client, int level, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    size_t      len;

    if (client->bot) {
        return;
    }

    if (level < client->messagelevel)
        return;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    q2proto_svc_message_t message = {.type = Q2P_SVC_PRINT, .print = {0}};
    message.print.level = level;
    message.print.string = q2proto_make_string(string);
    q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);

    SV_ClientAddMessage(client, MSG_RELIABLE | MSG_CLEAR);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients.
NOT archived in MVD stream.
=================
*/
void SV_BroadcastPrintf(int level, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    client_t    *client;
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    q2proto_svc_message_t message = {.type = Q2P_SVC_PRINT, .print = {0}};
    message.print.level = level;
    message.print.string = q2proto_make_string(string);
    q2proto_server_multicast_write(Q2P_PROTOCOL_MULTICAST_FLOAT, Q2PROTO_IOARG_SERVER_WRITE_MULTICAST, &message);

    FOR_EACH_CLIENT(client) {
        if (client->state != cs_spawned)
            continue;
        if (level < client->messagelevel)
            continue;
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}

void SV_ClientCommand(client_t *client, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_stufftext);
    MSG_WriteData(string, len + 1);

    SV_ClientAddMessage(client, MSG_RELIABLE | MSG_CLEAR);
}

/*
=================
SV_BroadcastCommand

Sends command to all active clients.
NOT archived in MVD stream.
=================
*/
void SV_BroadcastCommand(const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    client_t    *client;
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_stufftext);
    MSG_WriteData(string, len + 1);

    FOR_EACH_CLIENT(client) {
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}


/*
=================
SV_Multicast

Sends the contents of the write buffer to a subset of the clients,
then clears the write buffer.

Archived in MVD stream.

MULTICAST_ALL    same as broadcast (origin can be NULL)
MULTICAST_PVS    send to clients potentially visible from org
MULTICAST_PHS    send to clients potentially hearable from org
=================
*/
void SV_Multicast(const vec3_t origin, multicast_t to, bool reliable)
{
    client_t        *client;
    visrow_t        mask;
    const mleaf_t   *leaf1 = NULL;
    int             flags = 0;

    if (to < MULTICAST_ALL || to > MULTICAST_PVS)
        Com_Error(ERR_DROP, "%s: bad to: %d", __func__, to);

    if (to && !origin)
        Com_Error(ERR_DROP, "%s: NULL origin", __func__);

    if (msg_write.overflowed)
        Com_Error(ERR_DROP, "%s: message buffer overflowed", __func__);

    if (!msg_write.cursize) {
        Com_DPrintf("%s with empty data\n", __func__);
        return;
    }

    if (to) {
        leaf1 = CM_PointLeaf(&sv.cm, origin);
        BSP_ClusterVis(sv.cm.cache, &mask, leaf1->cluster, MULTICAST_PVS - to);
    }
    if (reliable)
        flags |= MSG_RELIABLE;

    // send the data to all relevant clients
    FOR_EACH_CLIENT(client) {
        if (client->state < cs_primed) {
            continue;
        }
        // do not send unreliables to connecting clients
        if (!(flags & MSG_RELIABLE) && !CLIENT_ACTIVE(client)) {
            continue;
        }

        if (to) {
            const mleaf_t *leaf2 = CM_PointLeaf(&sv.cm, client->edict->s.origin);
            if (!CM_AreasConnected(&sv.cm, leaf1->area, leaf2->area))
                continue;
            if (leaf2->cluster == -1)
                continue;
            if (!Q_IsBitSet(mask.b, leaf2->cluster))
                continue;
        }

        SV_ClientAddMessage(client, flags);
    }

    // add to MVD datagram
    SV_MvdMulticast(leaf1, to, flags & MSG_RELIABLE);

    // clear the buffer
    SZ_Clear(&msg_write);
}

#if USE_ZLIB
static bool can_auto_compress(const client_t *client)
{
    if (!client->q2proto_ctx.features.enable_deflate)
        return false;

    // compress only sufficiently large layouts
    if (msg_write.cursize < client->netchan.maxpacketlen / 2)
        return false;

    return true;
}

static int compress_message(client_t *client)
{
    size_t uncompressed_size = msg_write.cursize;
    msg_write.cursize = 0;
    q2proto_error_t err = q2proto_server_write_zpacket(&client->q2proto_ctx, &client->q2proto_deflate, (uintptr_t)&client->io_data, msg_write.data, uncompressed_size);
    if (err != Q2P_ERR_SUCCESS)
    {
        if (err != Q2P_ERR_ALREADY_COMPRESSED)
            Com_WPrintf("Error %s compressing %zu bytes message for %s\n",
                        q2proto_error_string(err), uncompressed_size, client->name);
        msg_write.cursize = uncompressed_size;
        return 0;
    }
    return msg_write.cursize;
}

static byte *get_compressed_data(void)
{
    return msg_write.data;
}
#else
#define can_auto_compress(c)    false
#define compress_message(c)     0
#define get_compressed_data()   NULL
#endif

/*
=======================
SV_ClientAddMessage

Adds contents of the current write buffer to client's message list.
Does NOT clean the buffer for multicast delivery purpose,
unless told otherwise.
=======================
*/
void SV_ClientAddMessage(client_t *client, int flags)
{
    int len;
    const byte *native_reliable_data;
    size_t native_reliable_bytes;

    Q_assert(!msg_write.overflowed);

    if (!msg_write.cursize) {
        return;
    }

    native_reliable_data = msg_write.data;
    native_reliable_bytes = msg_write.cursize;

    bool compress = (flags & MSG_COMPRESS_AUTO) && can_auto_compress(client);

    if (compress && (len = compress_message(client)) && len < msg_write.cursize) {
        client->AddMessage(client, get_compressed_data(), len, flags & MSG_RELIABLE);
        SV_DPrintf(1, "Compressed %sreliable message to %s: %u into %d\n",
                   (flags & MSG_RELIABLE) ? "" : "un", client->name, msg_write.cursize, len);
    } else {
        client->AddMessage(client, msg_write.data, msg_write.cursize, flags & MSG_RELIABLE);
        SV_DPrintf(2, "Added %sreliable message to %s: %u bytes\n",
                   (flags & MSG_RELIABLE) ? "" : "un", client->name, msg_write.cursize);
    }

    /* Observe only after the authoritative reliable append. Unsupported raw
     * service sequences remain legacy-only, while recognized temp/muzzle
     * batches and keyed POIs wait for the next exact final snapshot. */
    if (flags & MSG_RELIABLE) {
        (void)SV_NativeShadowCaptureReliableGameEventsV1(
            client->worr_native_shadow, (uint32_t)sv.spawncount,
            native_reliable_data, native_reliable_bytes);
        (void)SV_NativeShadowCaptureReliableKeyedPOIV1(
            client->worr_native_shadow, (uint32_t)sv.spawncount,
            native_reliable_data, native_reliable_bytes);
    }

    if (flags & MSG_CLEAR) {
        SZ_Clear(&msg_write);
    }
}

/*
===============================================================================

FRAME UPDATES - COMMON

===============================================================================
*/

static inline void free_msg_packet(client_t *client, message_packet_t *msg)
{
    List_Remove(&msg->entry);

    if (msg->cursize > MSG_TRESHOLD) {
        Q_assert(msg->cursize <= client->msg_dynamic_bytes);
        client->msg_dynamic_bytes -= msg->cursize;
        Z_Free(msg);
    } else {
        List_Insert(&client->msg_free_list, &msg->entry);
    }
}

#define FOR_EACH_MSG_SAFE(list) \
    LIST_FOR_EACH_SAFE(message_packet_t, msg, next, list, entry)
#define MSG_FIRST(list) \
    LIST_FIRST(message_packet_t, list, entry)

static void free_all_messages(client_t *client)
{
    message_packet_t *msg, *next;

    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        free_msg_packet(client, msg);
    }
    FOR_EACH_MSG_SAFE(&client->msg_reliable_list) {
        free_msg_packet(client, msg);
    }
    client->msg_unreliable_bytes = 0;
    client->msg_dynamic_bytes = 0;
}

static void add_msg_packet(client_t *client, const byte *data,
                           size_t len, bool reliable)
{
    message_packet_t    *msg;

    if (!client->msg_pool) {
        return; // already dropped
    }

    Q_assert(len <= MAX_MSGLEN);

    if (len > MSG_TRESHOLD) {
        if (client->msg_dynamic_bytes > MAX_MSGLEN - len) {
            Com_DWPrintf("%s to %s: out of dynamic memory\n",
                         __func__, client->name);
            goto overflowed;
        }
        msg = SV_Malloc(sizeof(*msg) + len - MSG_TRESHOLD);
        client->msg_dynamic_bytes += len;
    } else {
        if (LIST_EMPTY(&client->msg_free_list)) {
            Com_DWPrintf("%s to %s: out of message slots\n",
                         __func__, client->name);
            goto overflowed;
        }
        msg = MSG_FIRST(&client->msg_free_list);
        List_Remove(&msg->entry);
    }

    memcpy(msg->data, data, len);
    msg->cursize = (uint16_t)len;

    if (reliable) {
        List_Append(&client->msg_reliable_list, &msg->entry);
    } else {
        List_Append(&client->msg_unreliable_list, &msg->entry);
        client->msg_unreliable_bytes += len;
    }

    return;

overflowed:
    if (reliable) {
        free_all_messages(client);
        SV_DropClient(client, "reliable queue overflowed");
    }
}

// check if this entity is present in current client frame
static bool check_entity(const client_t *client, int entnum)
{
    const client_frame_t *frame;
    int left, right;

    frame = &client->frames[client->framenum & UPDATE_MASK];

    left = 0;
    right = frame->num_entities - 1;
    while (left <= right) {
        int i, j;

        i = (left + right) / 2;
        j = (frame->first_entity + i) & (client->num_entities - 1);
        j = client->entities[j].number;
        if (j < entnum)
            left = i + 1;
        else if (j > entnum)
            right = i - 1;
        else
            return true;
    }

    return false;
}

/*
 * The native event path observes only audio that the real per-client writer
 * just accepted. Visible sources keep exact snapshot lineage; an off-frame
 * source is admitted only when the writer has explicitly forced a position.
 * Client-local cues are world-anchored because their raw entity/channel is a
 * local override key, not an authoritative spatial-lineage claim.
 */
void SV_QueueNativeSpatialAudio(
    client_t *client, const q2proto_svc_sound_t *sound_message,
    uint32_t delivery_flags)
{
    q2proto_sound_t sound;
    worr_event_record_v1 candidate;
    sv_snapshot_shadow_ref_v1 snapshot_ref;
    sv_snapshot_shadow_result_v1 find_result;
    sv_snapshot_event_candidates_result_v1 build_result;

    if (!client || !sound_message || !client->worr_native_shadow ||
        !client->worr_snapshot_shadow ||
        !SV_NativeShadowModeHasEventV1(
            client->worr_native_shadow->mode) ||
        !client->csr || client->csr->max_edicts <= 0 ||
        client->framenum < 0) {
        return;
    }
    find_result = SV_SnapshotShadowFindWireV1(
        client->worr_snapshot_shadow, client->framenum, &snapshot_ref);
    if (find_result != SV_SNAPSHOT_SHADOW_OK)
        return;
    q2proto_sound_decode_message(sound_message, &sound);
    build_result =
        SV_SnapshotShadowBuildSpatialAudioCandidateWithDeliveryV1(
            client->worr_snapshot_shadow, snapshot_ref,
            (uint32_t)client->csr->max_edicts, &sound, delivery_flags,
            &candidate);
    if (build_result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
        return;
    (void)SV_NativeShadowQueueEventCandidatesV1(
        client->worr_native_shadow, &candidate, 1, svs.realtime);
}

/* Game DLL messages have no structured per-carrier slot. Observe only a
 * bounded mixed temp/muzzle sequence after its complete legacy byte range has
 * fitted this client's post-snapshot datagram. Native admission is atomic:
 * mixed known carriers preserve order, while unknown/reliable/old-netchan,
 * malformed, over-capacity, or non-visible sequences remain legacy-only. */
static void queue_visible_native_game_events(client_t *client,
                                             const message_packet_t *msg)
{
    worr_legacy_game_event_candidate_carrier_v1
        carriers[WORR_LEGACY_GAME_EVENT_CANDIDATE_SEQUENCE_MAX];
    worr_event_record_v1
        candidates[WORR_LEGACY_GAME_EVENT_CANDIDATE_SEQUENCE_MAX];
    sv_snapshot_shadow_ref_v1 snapshot_ref;
    uint32_t carrier_count;
    worr_legacy_game_event_candidate_result_v1 decode_result;
    sv_snapshot_shadow_result_v1 find_result;
    sv_snapshot_event_candidates_result_v1 build_result;

    if (!client || !msg || client->netchan.type != NETCHAN_NEW ||
        !client->worr_native_shadow || !client->worr_snapshot_shadow ||
        !SV_NativeShadowModeHasEventV1(
            client->worr_native_shadow->mode) ||
        !client->csr || client->csr->max_edicts <= 0 ||
        client->framenum < 0 || msg->cursize == SOUND_PACKET) {
        return;
    }
    decode_result = Worr_LegacyGameEventDecodeRawSequenceV1(
        msg->data, msg->cursize, carriers, q_countof(carriers),
        &carrier_count);
    if (decode_result != WORR_LEGACY_GAME_EVENT_CANDIDATE_OK) {
        return;
    }
    find_result = SV_SnapshotShadowFindWireV1(
        client->worr_snapshot_shadow, client->framenum, &snapshot_ref);
    if (find_result != SV_SNAPSHOT_SHADOW_OK)
        return;
    build_result = SV_SnapshotShadowBuildGameEventCandidatesV1(
        client->worr_snapshot_shadow, snapshot_ref,
        (uint32_t)client->csr->max_edicts, carriers, carrier_count,
        candidates);
    if (build_result != SV_SNAPSHOT_EVENT_CANDIDATES_OK)
        return;
    (void)SV_NativeShadowQueueEventCandidatesV1(
        client->worr_native_shadow, candidates, carrier_count, svs.realtime);
}

/* svc_damage is a client-local presentation carrier, not a multicast game
 * event. Observe it only after the complete legacy message has fitted the
 * post-snapshot datagram, then bind its implicit recipient to that exact
 * frame's controlled-entity generation. The carrier exposes no attacker, so
 * the canonical records deliberately retain their world source. */
static void queue_native_damage_indicators(client_t *client,
                                           const message_packet_t *msg)
{
    q2proto_svc_damage_t damage;
    worr_event_record_v1 candidates[Q2PROTO_MAX_DAMAGE_INDICATORS];
    sv_snapshot_shadow_ref_v1 snapshot_ref;
    uint32_t candidate_count;

    if (!client || !msg || client->netchan.type != NETCHAN_NEW ||
        !client->worr_native_shadow || !client->worr_snapshot_shadow ||
        !SV_NativeShadowModeHasEventV1(client->worr_native_shadow->mode) ||
        !client->csr || client->csr->max_edicts <= 0 ||
        client->framenum < 0 || msg->cursize == SOUND_PACKET ||
        Worr_LegacyDamageEventDecodeRawV1(
            msg->data, msg->cursize, &damage) !=
            WORR_LEGACY_DAMAGE_EVENT_CANDIDATE_OK) {
        return;
    }
    if (SV_SnapshotShadowFindWireV1(
            client->worr_snapshot_shadow, client->framenum,
            &snapshot_ref) != SV_SNAPSHOT_SHADOW_OK) {
        return;
    }
    if (SV_SnapshotShadowBuildDamageCandidatesV1(
            client->worr_snapshot_shadow, snapshot_ref,
            (uint32_t)client->csr->max_edicts, &damage, candidates,
            q_countof(candidates), &candidate_count) !=
        SV_SNAPSHOT_EVENT_CANDIDATES_OK) {
        return;
    }
    (void)SV_NativeShadowQueueEventCandidatesV1(
        client->worr_native_shadow, candidates, candidate_count,
        svs.realtime);
}

/* svc_help_path is an unreliable client-local marker carrier. Observe it only
 * after its complete legacy bytes fit the post-snapshot datagram, then bind
 * the implicit recipient to the exact final frame. The position is world
 * data, but the carrier exposes no emitter identity. */
static void queue_native_help_path_marker(client_t *client,
                                          const message_packet_t *msg)
{
    q2proto_svc_help_path_t help_path;
    worr_event_record_v1 candidate;
    sv_snapshot_shadow_ref_v1 snapshot_ref;

    if (!client || !msg || client->netchan.type != NETCHAN_NEW ||
        !client->worr_native_shadow || !client->worr_snapshot_shadow ||
        !SV_NativeShadowModeHasEventV1(client->worr_native_shadow->mode) ||
        !client->csr || client->csr->max_edicts <= 0 ||
        client->framenum < 0 || msg->cursize == SOUND_PACKET ||
        Worr_LegacyHelpPathEventDecodeRawV1(
            msg->data, msg->cursize, &help_path) !=
            WORR_LEGACY_HELP_PATH_EVENT_CANDIDATE_OK) {
        return;
    }
    if (SV_SnapshotShadowFindWireV1(
            client->worr_snapshot_shadow, client->framenum,
            &snapshot_ref) != SV_SNAPSHOT_SHADOW_OK) {
        return;
    }
    if (SV_SnapshotShadowBuildHelpPathCandidateV1(
            client->worr_snapshot_shadow, snapshot_ref,
            (uint32_t)client->csr->max_edicts, &help_path, &candidate) !=
        SV_SNAPSHOT_EVENT_CANDIDATES_OK) {
        return;
    }
    (void)SV_NativeShadowQueueEventCandidatesV1(
        client->worr_native_shadow, &candidate, 1, svs.realtime);
}

/* svc_poi pings are unreliable per-client state carriers. Observe one only
 * after its entire legacy message has fitted the post-snapshot datagram, then
 * bind the implicit recipient to that exact final frame. A zero lifetime is
 * reliable-only and therefore remains legacy-only here. The key is a UI state
 * namespace, not an entity/source identity. */
static void queue_native_keyed_poi(client_t *client,
                                   const message_packet_t *msg)
{
    q2proto_svc_poi_t poi;
    worr_event_record_v1 candidate;
    sv_snapshot_shadow_ref_v1 snapshot_ref;

    if (!client || !msg || client->netchan.type != NETCHAN_NEW ||
        !client->worr_native_shadow || !client->worr_snapshot_shadow ||
        !SV_NativeShadowModeHasEventV1(client->worr_native_shadow->mode) ||
        !client->csr || client->csr->max_edicts <= 0 ||
        client->framenum < 0 || msg->cursize == SOUND_PACKET ||
        Worr_LegacyKeyedPOIEventDecodeRawV1(
            msg->data, msg->cursize, &poi) !=
            WORR_LEGACY_KEYED_POI_EVENT_CANDIDATE_OK) {
        return;
    }
    if (SV_SnapshotShadowFindWireV1(
            client->worr_snapshot_shadow, client->framenum,
            &snapshot_ref) != SV_SNAPSHOT_SHADOW_OK) {
        return;
    }
    if (SV_SnapshotShadowBuildKeyedPOICandidateV1(
            client->worr_snapshot_shadow, snapshot_ref,
            (uint32_t)client->csr->max_edicts, &poi, &candidate) !=
        SV_SNAPSHOT_EVENT_CANDIDATES_OK) {
        return;
    }
    (void)SV_NativeShadowQueueEventCandidatesV1(
        client->worr_native_shadow, &candidate, 1, svs.realtime);
}

// sounds relative to entities are handled specially
static void emit_snd(client_t *client, const message_packet_t *msg)
{
    int entnum = msg->sound.entity;
    int flags = msg->sound.flags;
    bool source_in_frame;
    q2proto_svc_message_t message = {.type = Q2P_SVC_SOUND, .sound = msg->sound};

    // check if position needs to be explicitly sent
    source_in_frame = entnum > 0 && check_entity(client, entnum);
    if (!(flags & SND_POS) && !source_in_frame) {
        SV_DPrintf(2, "Forcing position on entity %d for %s\n",
                   entnum, client->name);
        flags |= SND_POS;   // entity is not present in frame
    }

    message.sound.flags = flags;
    if (q2proto_server_write(&client->q2proto_ctx,
                             (uintptr_t)&client->io_data,
                             &message) == Q2P_ERR_SUCCESS) {
        SV_QueueNativeSpatialAudio(client, &message.sound, 0);
    }
}

static inline void write_snd(client_t *client, message_packet_t *msg, unsigned maxsize)
{
    // if this msg fits, write it
    if (msg_write.cursize + MAX_SOUND_PACKET <= maxsize) {
        emit_snd(client, msg);
    }
    List_Remove(&msg->entry);
    List_Insert(&client->msg_free_list, &msg->entry);
}

static inline void write_msg(client_t *client, message_packet_t *msg, unsigned maxsize)
{
    // if this msg fits, write it
    if (msg_write.cursize + msg->cursize <= maxsize) {
        MSG_WriteData(msg->data, msg->cursize);
        queue_native_damage_indicators(client, msg);
        queue_native_help_path_marker(client, msg);
        queue_native_keyed_poi(client, msg);
        queue_visible_native_game_events(client, msg);
    }
    free_msg_packet(client, msg);
}

static inline void write_unreliables(client_t *client, unsigned maxsize)
{
    message_packet_t    *msg, *next;

    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize == SOUND_PACKET) {
            write_snd(client, msg, maxsize);
        } else {
            write_msg(client, msg, maxsize);
        }
    }
}

/*
===============================================================================

FRAME UPDATES - OLD NETCHAN

===============================================================================
*/

static void add_message_old(client_t *client, const byte *data,
                            size_t len, bool reliable)
{
    if (len > client->netchan.maxpacketlen) {
        if (reliable) {
            SV_DropClient(client, "oversize reliable message");
        } else {
            Com_DPrintf("Dumped oversize unreliable for %s\n", client->name);
        }
        return;
    }

    add_msg_packet(client, data, len, reliable);
}

// this should be the only place data is ever written to netchan message for old clients
static void write_reliables_old(client_t *client, unsigned maxsize)
{
    message_packet_t *msg, *next;
    int count;

    if (client->netchan.reliable_length) {
        SV_DPrintf(2, "%s to %s: unacked\n", __func__, client->name);
        return;    // there is still outgoing reliable message pending
    }

    // find at least one reliable message to send
    count = 0;
    FOR_EACH_MSG_SAFE(&client->msg_reliable_list) {
        // stop if this msg doesn't fit (reliables must be delivered in order)
        if (client->netchan.message.cursize + msg->cursize > maxsize) {
            if (!count) {
                // this should never happen
                Com_WPrintf("%s to %s: overflow on the first message\n",
                            __func__, client->name);
            }
            break;
        }

        SV_DPrintf(2, "%s to %s: writing msg %d: %d bytes\n",
                   __func__, client->name, count, msg->cursize);

        SZ_Write(&client->netchan.message, msg->data, msg->cursize);
        free_msg_packet(client, msg);
        count++;
    }
}

// unreliable portion doesn't fit, then throw out low priority effects
static void repack_unreliables(client_t *client, unsigned maxsize)
{
    message_packet_t *msg, *next;

    if (msg_write.cursize + 4 > maxsize) {
        return;
    }

    // temp entities first
    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize == SOUND_PACKET || msg->data[0] != svc_temp_entity) {
            continue;
        }
        // ignore some low-priority effects, these checks come from R1Q2
        if (msg->data[1] == TE_BLOOD || msg->data[1] == TE_SPLASH ||
            msg->data[1] == TE_GUNSHOT || msg->data[1] == TE_BULLET_SPARKS ||
            msg->data[1] == TE_SHOTGUN) {
            continue;
        }
        write_msg(client, msg, maxsize);
    }

    if (msg_write.cursize + 4 > maxsize) {
        return;
    }

    // then entity sounds
    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize == SOUND_PACKET) {
            write_snd(client, msg, maxsize);
        }
    }

    if (msg_write.cursize + 4 > maxsize) {
        return;
    }

    // then positioned sounds
    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize != SOUND_PACKET && msg->data[0] == svc_sound) {
            write_msg(client, msg, maxsize);
        }
    }

    if (msg_write.cursize + 4 > maxsize) {
        return;
    }

    // then everything else left
    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize != SOUND_PACKET) {
            write_msg(client, msg, maxsize);
        }
    }
}

static void write_datagram_old(client_t *client)
{
    message_packet_t *msg;
    unsigned maxsize, cursize;
    bool ret;

    // determine how much space is left for unreliable data
    maxsize = client->netchan.maxpacketlen;
    if (client->netchan.reliable_length) {
        // there is still unacked reliable message pending
        maxsize -= client->netchan.reliable_length;
    } else {
        // find at least one reliable message to send
        // and make sure to reserve space for it
        if (!LIST_EMPTY(&client->msg_reliable_list)) {
            msg = MSG_FIRST(&client->msg_reliable_list);
            maxsize -= msg->cursize;
        }
    }
    Q_assert(maxsize <= client->netchan.maxpacketlen);

    // send over all the relevant entity_state_t
    // and the player_state_t
    ret = SV_WriteFrameToClient_Enhanced(client, maxsize);
    if (!ret) {
        SV_DPrintf(1, "Frame %d overflowed for %s\n", client->framenum, client->name);
        SZ_Clear(&msg_write);
    }

    // now write unreliable messages
    // it is necessary for this to be after the WriteFrame
    // so that entity references will be current
    if (msg_write.cursize + client->msg_unreliable_bytes > maxsize) {
        // throw out some low priority effects
        repack_unreliables(client, maxsize);
    } else {
        // all messages fit, write them in order
        write_unreliables(client, maxsize);
    }

    // write at least one reliable message
    write_reliables_old(client, client->netchan.maxpacketlen - msg_write.cursize);

#if USE_DEBUG
    if (sv_pad_packets->integer > 0) {
        int pad = min(MAX_PACKETLEN - 8, sv_pad_packets->integer);

        while (msg_write.cursize < pad) {
            q2proto_svc_message_t message = {.type = Q2P_SVC_NOP};
            q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);
        }
    }
#endif

    Q_assert(!msg_write.overflowed);

    // send the datagram
    cursize = Netchan_Transmit(&client->netchan,
                               msg_write.cursize,
                               msg_write.data,
                               client->numpackets);

    // record the size for rate estimation
    SV_CalcSendTime(client, cursize, false);

    // clear the write buffer
    SZ_Clear(&msg_write);
}

/*
===============================================================================

FRAME UPDATES - NEW NETCHAN

===============================================================================
*/

static void add_message_new(client_t *client, const byte *data,
                            size_t len, bool reliable)
{
    if (reliable) {
        // don't packetize, netchan level will do fragmentation as needed
        SZ_Write(&client->netchan.message, data, len);
    } else {
        // still have to packetize, relative sounds need special processing
        add_msg_packet(client, data, len, false);
    }
}

static void write_datagram_new(client_t *client)
{
    int cursize;

    // send over all the relevant entity_state_t
    // and the player_state_t
    if (!SV_WriteFrameToClient_Enhanced(client, msg_write.maxsize)) {
        // should never really happen
        Com_WPrintf("Frame overflowed for %s\n", client->name);
        SZ_Clear(&msg_write);
    }

    // now write unreliable messages
    // for this client out to the message
    // it is necessary for this to be after the WriteFrame
    // so that entity references will be current
    if (msg_write.cursize + client->msg_unreliable_bytes > msg_write.maxsize) {
        Com_WPrintf("Dumping datagram for %s\n", client->name);
    } else {
        write_unreliables(client, msg_write.maxsize);
    }

#if USE_DEBUG
    if (sv_pad_packets->integer > 0) {
        int pad = min(msg_write.maxsize, sv_pad_packets->integer);

        while (msg_write.cursize < pad) {
            q2proto_svc_message_t message = {.type = Q2P_SVC_NOP};
            q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);
        }
    }
#endif

    Q_assert(!msg_write.overflowed);

    if (client->worr_native_shadow) {
        (void)SV_NativeShadowOutputDueV1(
            client->worr_native_shadow, svs.realtime);
    }

    // send the datagram
    cursize = Netchan_Transmit(&client->netchan,
                               msg_write.cursize,
                               msg_write.data,
                               client->numpackets);

    // record the size for rate estimation
    SV_CalcSendTime(client, cursize, false);

    // clear the write buffer
    SZ_Clear(&msg_write);
}


/*
===============================================================================

COMMON STUFF

===============================================================================
*/

static void finish_frame(client_t *client)
{
    message_packet_t *msg, *next;

    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        free_msg_packet(client, msg);
    }
    client->msg_unreliable_bytes = 0;
}

#if USE_DEBUG && USE_FPS
static void check_key_sync(const client_t *client)
{
    int div = sv.frametime.div / client->framediv;
    int key1 = !(sv.framenum % sv.frametime.div);
    int key2 = !(client->framenum % div);

    if (key1 != key2) {
        Com_LPrintf(PRINT_DEVELOPER,
                    "[%d] frame %d for %s not synced (%d != %d)\n",
                    sv.framenum, client->framenum, client->name, key1, key2);
    }
}
#endif

/*
=======================
SV_SendClientMessages

Called each game frame, sends svc_frame messages to spawned clients only.
Clients in earlier connection state are handled in SV_SendAsyncPackets.
=======================
*/
void SV_SendClientMessages(void)
{
    client_t    *client;
    int         cursize;
    sv_native_shadow_challenge_service_result_t challenge_result;

    // send a message to each connected client
    FOR_EACH_CLIENT(client) {
        if (client->bot) {
            continue;
        }

        if (!CLIENT_ACTIVE(client))
            goto finish;

        if (!SV_CLIENTSYNC(client))
            continue;

#if USE_DEBUG && USE_FPS
        if (developer->integer)
            check_key_sync(client);
#endif

        // if the reliable message overflowed,
        // drop the client (should never happen)
        if (client->netchan.message.overflowed) {
            SZ_Clear(&client->netchan.message);
            SV_DropClient(client, "reliable message overflowed");
            goto finish;
        }

        /* Expiry is gate-independent: sustained rate or fragment pressure
         * must not extend the separate pre-start queue bound. */
        SV_MaintainNativeShadowChallengePending(client);

        // don't overrun bandwidth
        if (SV_RateDrop(client))
            goto advance;

        // don't write any frame data until all fragments are sent
        if (client->netchan.fragment_pending) {
            /* The synchronous fragment owner runs before another normal
             * application packet can carry a native ACK.  Record that real
             * deferral here as well as in the asynchronous scheduler; on an
             * active client this path commonly consumes the fragment before
             * the later async pass can observe it. */
            if (client->worr_native_shadow &&
                SV_NativeShadowOutputEligiblePeekV1(
                    client->worr_native_shadow, svs.realtime)) {
                SV_NativeShadowRecordAsyncFragmentDeferralV1(
                    client->worr_native_shadow);
            }
            client->frameflags |= FF_SUPPRESSED;
            cursize = Netchan_TransmitNextFragment(&client->netchan);
            SV_CalcSendTime(client, cursize, false);
            goto advance;
        }

        /* Service the fixed end-of-Begin precedence barrier before building a
         * frame.  The helper either hands off its sealed bootstrap prefix or
         * the isolated CHALLENGE; the 10-second protocol deadline begins only
         * in the latter transaction immediately before this packet send. */
        challenge_result = SV_ServiceNativeShadowChallenge(
            client, client->numpackets, &cursize);
        if (challenge_result !=
            SV_NATIVE_SHADOW_CHALLENGE_SERVICE_NONE) {
            client->frameflags |= FF_SUPPRESSED;
            client->suppress_count++;
            SV_CalcSendTime(client, cursize, false);
            goto advance;
        }

        // build the new frame and write it
        SV_BuildClientFrame(client);

        if (client->netchan.type == NETCHAN_NEW)
            write_datagram_new(client);
        else
            write_datagram_old(client);

advance:
        // advance for next frame
        client->framenum++;

finish:
        // clear all unreliable messages still left
        finish_frame(client);
    }
}

static void write_pending_download(client_t *client)
{
    sizebuf_t   *buf = &client->netchan.message;
    int         chunk;

    if (!client->download)
        return;

    if (!client->downloadpending)
        return;

    if (client->netchan.reliable_length)
        return;

    if (buf->cursize >= client->netchan.maxpacketlen)
        return;

    chunk = client->netchan.maxpacketlen - buf->cursize;

    client->downloadpending = false;

    q2proto_svc_message_t message = {.type = Q2P_SVC_DOWNLOAD};
    int download_err = q2proto_server_download_data(&client->download_state, &client->download_ptr, &client->download_remaining, chunk, &message.download);
    if (download_err == Q2P_ERR_SUCCESS || download_err == Q2P_ERR_DOWNLOAD_COMPLETE) {
        q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);
        MSG_FlushTo(buf);
    } else if (download_err != Q2P_ERR_NOT_ENOUGH_PACKET_SPACE) {
        Com_WPrintf("%s: failed downloading data to %s: %s\n", __func__, client->name, q2proto_error_string(download_err));
    }

    if (download_err == Q2P_ERR_DOWNLOAD_COMPLETE) {
        SV_CloseDownload(client);
        SV_AlignKeyFrames(client);
    }
}

/*
==================
SV_SendAsyncPackets

If the client is just connecting, it is pointless to wait another 100ms
before sending next command and/or reliable acknowledge, send it as soon
as client rate limit allows.

For spawned clients, this is not used, as we are forced to send svc_frame
packets synchronously with game DLL ticks.
==================
*/
void SV_SendAsyncPackets(void)
{
    bool        retransmit;
    bool        native_output_eligible;
    bool        native_output_due;
    bool        native_async_wake_started;
    sv_native_shadow_challenge_service_result_t challenge_result;
    client_t    *client;
    netchan_t   *netchan;
    int         cursize;

    FOR_EACH_CLIENT(client) {
        if (client->bot) {
            continue;
        }

        /* This async pass runs even while the simulation is paused.  Keep the
         * pending deadline live before every rate/fragment early exit. */
        SV_MaintainNativeShadowChallengePending(client);

        netchan = &client->netchan;
        native_output_eligible = client->worr_native_shadow &&
            netchan->type == NETCHAN_NEW &&
            SV_NativeShadowOutputEligiblePeekV1(
                client->worr_native_shadow, svs.realtime);

        // don't overrun bandwidth
        if (svs.realtime - client->send_time < client->send_delta) {
            if (native_output_eligible) {
                SV_NativeShadowRecordAsyncRateDeferralV1(
                    client->worr_native_shadow);
            }
            continue;
        }

        // make sure all fragments are transmitted first
        if (netchan->fragment_pending) {
            if (native_output_eligible) {
                SV_NativeShadowRecordAsyncFragmentDeferralV1(
                    client->worr_native_shadow);
            }
            cursize = Netchan_TransmitNextFragment(netchan);
            SV_DPrintf(2, "%s: frag: %d\n", client->name, cursize);
            goto calctime;
        }

        /* Service active clients here as well as in the synchronous frame
         * path.  This pass stays live under pause and can reach a rate-admitted
         * boundary between sparse/suppressed simulation sends. */
        if (CLIENT_ACTIVE(client)) {
            challenge_result = SV_ServiceNativeShadowChallenge(
                client, 1, &cursize);
            if (challenge_result !=
                SV_NATIVE_SHADOW_CHALLENGE_SERVICE_NONE) {
                SV_DPrintf(
                    2, "%s: native %s: %d\n", client->name,
                    challenge_result ==
                            SV_NATIVE_SHADOW_CHALLENGE_SERVICE_CHALLENGE
                        ? "challenge" : "bootstrap drain",
                    cursize);
                goto calctime;
            }
        }

        native_output_due = client->worr_native_shadow &&
            netchan->type == NETCHAN_NEW &&
            SV_NativeShadowOutputDueV1(
                client->worr_native_shadow, svs.realtime);

        // spawned clients are handled elsewhere
        if (CLIENT_ACTIVE(client) && !SV_PAUSED) {
            if (!native_output_due)
                continue;
            native_async_wake_started =
                SV_NativeShadowBeginAsyncWakeV1(
                    client->worr_native_shadow);
            cursize = Netchan_Transmit(netchan, 0, NULL, 1);
            if (native_async_wake_started) {
                SV_NativeShadowEndAsyncWakeV1(
                    client->worr_native_shadow);
            }
            SV_DPrintf(2, "%s: native output: %d\n",
                       client->name, cursize);
            goto calctime;
        }

        // see if it's time to resend a (possibly dropped) packet
        retransmit = (com_localTime - netchan->last_sent > 1000);

        // don't write new reliables if not yet acknowledged
        if (netchan->reliable_length && !retransmit &&
            !native_output_due && client->state != cs_zombie) {
            continue;
        }

        // just update reliable if needed
        if (netchan->type == NETCHAN_OLD) {
            write_reliables_old(client, netchan->maxpacketlen);
        }

        // now fill up remaining buffer space with download
        write_pending_download(client);

        if (netchan->message.cursize || netchan->reliable_ack_pending ||
            netchan->reliable_length || retransmit || native_output_due) {
            if (native_output_due) {
                native_async_wake_started =
                    SV_NativeShadowBeginAsyncWakeV1(
                        client->worr_native_shadow);
            }
            cursize = Netchan_Transmit(netchan, 0, NULL, 1);
            if (native_output_due && native_async_wake_started) {
                SV_NativeShadowEndAsyncWakeV1(
                    client->worr_native_shadow);
            }
            SV_DPrintf(2, "%s: send: %d\n", client->name, cursize);
calctime:
            SV_CalcSendTime(client, cursize, true);
        }
    }
}

void SV_InitClientSend(client_t *newcl)
{
    List_Init(&newcl->msg_free_list);
    List_Init(&newcl->msg_unreliable_list);
    List_Init(&newcl->msg_reliable_list);

    newcl->msg_pool = SV_Malloc(sizeof(newcl->msg_pool[0]) * MSG_POOLSIZE);
    for (int i = 0; i < MSG_POOLSIZE; i++) {
        List_Append(&newcl->msg_free_list, &newcl->msg_pool[i].entry);
    }

    // setup protocol
    if (newcl->netchan.type == NETCHAN_NEW) {
        newcl->AddMessage = add_message_new;
    } else {
        newcl->AddMessage = add_message_old;
    }
}

void SV_ShutdownClientSend(client_t *client)
{
    free_all_messages(client);

    Z_Freep(&client->msg_pool);
    List_Init(&client->msg_free_list);
}
