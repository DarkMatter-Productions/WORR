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
// sv_user.c -- server code for moving users

#include "server.h"
#include "server/command_context.h"
#include "server/local_interaction_authority.h"
#include "server/native_shadow.h"
#include "server/snapshot_shadow.h"
#include "common/net/usercmd_delta.h"

#define MSG_GAMESTATE   (MSG_RELIABLE | MSG_CLEAR)

/*
============================================================

USER STRINGCMD EXECUTION

sv_client and sv_player will be valid.
============================================================
*/

static int      stringCmdCount;

/*
================
SV_CreateBaselines

Entity baselines are used to compress the update messages
to the clients -- only the fields that differ from the
baseline will be transmitted
================
*/
static void SV_CreateBaselines(void)
{
    int        i;
    edict_t    *ent;
    server_entity_packed_t *base, **chunk;

    // clear baselines from previous level
    for (i = 0; i < SV_BASELINES_CHUNKS; i++) {
        base = sv_client->baselines[i];
        if (base) {
            memset(base, 0, sizeof(*base) * SV_BASELINES_PER_CHUNK);
        }
    }

    for (i = 1; i < sv_client->ge->num_edicts; i++) {
        ent = EDICT_NUM2(sv_client->ge, i);

        if ((g_features->integer & GMF_PROPERINUSE) && !ent->inuse) {
            continue;
        }

        if (!HAS_EFFECTS(ent)) {
            continue;
        }

        SV_CheckEntityNumber(ent, i);

        chunk = &sv_client->baselines[i >> SV_BASELINES_SHIFT];
        if (*chunk == NULL) {
            *chunk = SV_Mallocz(sizeof(*base) * SV_BASELINES_PER_CHUNK);
        }

        base = *chunk + (i & SV_BASELINES_MASK);
        PackEntity(&sv_client->q2proto_ctx, &ent->s, &base->e);
        base->number = ent->s.number;

        // no need to transmit data that will change anyway
        if (i <= sv_client->maxclients) {
            VectorClear(base->e.origin);
            VectorClear(base->e.angles);
            base->e.frame = 0;
        }

        // don't ever transmit event
        base->e.event = 0;

#if USE_MVD_CLIENT
        if (sv.state != ss_broadcast)
#endif
        if (sv_client->esFlags & MSG_ES_LONGSOLID && !sv_client->csr->extended) {
            base->e.solid = sv.entities[i].solid32;
        }
    }
}

// Data needed for write_gamestate. Too large for stack, so store statically.
static q2proto_svc_configstring_t configstrings[MAX_CONFIGSTRINGS];
static q2proto_svc_spawnbaseline_t spawnbaselines[MAX_EDICTS];

static void reset_snapshot_shadow(void)
{
    const cvar_t *enabled = Cvar_Get("sv_snapshot_shadow", "1", 0);
    sv_snapshot_shadow_config_v1 config;

    SV_SnapshotShadowDestroyV1(sv_client->worr_snapshot_shadow);
    sv_client->worr_snapshot_shadow = NULL;
    if (!enabled || !enabled->integer)
        return;

    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    config.snapshot_epoch = sv.worr_snapshot_epoch;
    config.max_entities = sv_client->csr->max_edicts;
    config.max_models = sv_client->csr->max_models;
    config.max_sounds = sv_client->csr->max_sounds;
    config.slot_capacity = UPDATE_BACKUP;
    config.entities_per_slot = sv_client->csr->extended
        ? MAX_PACKET_ENTITIES : MAX_PACKET_ENTITIES_OLD;
    config.area_bytes_per_slot = MAX_MAP_AREA_BYTES;
    config.beam_renderfx_mask = RF_BEAM;
    config.legacy_renderfx_allowed_mask = RF_SHELL_LITE_GREEN - 1u;
    config.legacy_beam_clear_mask = RF_GLOW;
    config.extended_entity_state = sv_client->csr->extended ? 1u : 0u;
    sv_client->worr_snapshot_shadow =
        SV_SnapshotShadowCreateV1(&config);
    if (!sv_client->worr_snapshot_shadow) {
        Com_DPrintf("Unable to allocate snapshot emission shadow for %s\n",
                    sv_client->name);
    }
}

static void write_gamestate(void)
{
    msgEsFlags_t baseline_flags = sv_client->q2proto_ctx.features.has_beam_old_origin_fix ? MSG_ES_BEAMORIGIN : 0;
    q2proto_gamestate_t gamestate = {.num_configstrings = 0, .configstrings = configstrings, .num_spawnbaselines = 0, .spawnbaselines = spawnbaselines};
    memset(spawnbaselines, 0, sizeof(spawnbaselines));

    for (int i = 0; i < sv_client->csr->end; i++) {
        const char* string = sv_client->configstrings[i];
        if (!string[0]) {
            continue;
        }
        q2proto_svc_configstring_t *cfgstr = &configstrings[gamestate.num_configstrings++];
        cfgstr->index = i;
        cfgstr->value.str = string;
        // FIXME: this is not very intelligent, and would probably benefit from
        // checking Com_ConfigstringSize and increasing as appropriate to save
        // on some bytes. for instance CS_STATUSBAR can span up to around 5 kb
        // so there's no sense in writing `5 <96 chars> 6 <96 chars>` etc when
        // you can just write `5 <4900>` chars and skip to the next string.
        cfgstr->value.len = Q_strnlen(string, CS_MAX_STRING_LENGTH);
    }

    for (int i = 0; i < SV_BASELINES_CHUNKS; i++) {
        server_entity_packed_t *base = sv_client->baselines[i];
        if (!base) {
            continue;
        }
        for (int j = 0; j < SV_BASELINES_PER_CHUNK; j++) {
            if (base->number) {
                q2proto_svc_spawnbaseline_t *baseline = &spawnbaselines[gamestate.num_spawnbaselines++];
                baseline->entnum = base->number;
                Q2PROTO_MakeEntityDelta(&sv_client->q2proto_ctx, &baseline->delta_state, NULL, &base->e, baseline_flags);
                (void)SV_SnapshotShadowSetBaselineV1(
                    sv_client->worr_snapshot_shadow,
                    baseline->entnum, &baseline->delta_state);
            }
            base++;
        }
    }

    q2protoio_deflate_args_t *deflate_args = NULL;
#if USE_ZLIB
    deflate_args = &sv_client->q2proto_deflate;
#endif
    int write_result;
    do {
        write_result = q2proto_server_write_gamestate(&sv_client->q2proto_ctx, deflate_args, (uintptr_t)&sv_client->io_data, &gamestate);
        SV_ClientAddMessage(sv_client, MSG_GAMESTATE);
    } while (write_result == Q2P_ERR_NOT_ENOUGH_PACKET_SPACE);
}

static void stuff_cmds(const list_t *list)
{
    stuffcmd_t *stuff;

    LIST_FOR_EACH(stuffcmd_t, stuff, list, entry) {
        q2proto_svc_message_t message = {.type = Q2P_SVC_STUFFTEXT};
        message.stufftext.string = q2proto_make_string(va("%s\n", stuff->string));
        q2proto_server_write(&sv_client->q2proto_ctx, (uintptr_t)&sv_client->io_data, &message);
        SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
    }
}

static void stuff_cvar_bans(void)
{
    cvarban_t *ban;

    LIST_FOR_EACH(cvarban_t, ban, &sv_cvarbanlist, entry)
        if (Q_stricmp(ban->var, "version"))
            SV_ClientCommand(sv_client, "cmd \177c %s $%s\n", ban->var, ban->var);
}

static void stuff_junk(void)
{
    static const char junkchars[] =
        "!#&'()*+,-./0123456789:<=>?@[\\]^_``````````abcdefghijklmnopqrstuvwxyz|~~~~~~~~~~";
    char junk[8][16];
    int i, j;

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 15; j++)
            junk[i][j] = junkchars[Q_rand_uniform(sizeof(junkchars) - 1)];
        junk[i][j] = 0;
    }

    Q_strlcpy(sv_client->reconnect_var, junk[2], sizeof(sv_client->reconnect_var));
    Q_strlcpy(sv_client->reconnect_val, junk[3], sizeof(sv_client->reconnect_val));

    SV_ClientCommand(sv_client, "set %s set\n", junk[0]);
    SV_ClientCommand(sv_client, "$%s %s connect\n", junk[0], junk[1]);
    if (Q_rand() & 1) {
        SV_ClientCommand(sv_client, "$%s %s %s\n", junk[0], junk[2], junk[3]);
        SV_ClientCommand(sv_client, "$%s %s %s\n", junk[0], junk[4],
                         sv_force_reconnect->string);
        SV_ClientCommand(sv_client, "$%s %s %s\n", junk[0], junk[5], junk[6]);
    } else {
        SV_ClientCommand(sv_client, "$%s %s %s\n", junk[0], junk[4],
                         sv_force_reconnect->string);
        SV_ClientCommand(sv_client, "$%s %s %s\n", junk[0], junk[5], junk[6]);
        SV_ClientCommand(sv_client, "$%s %s %s\n", junk[0], junk[2], junk[3]);
    }
    SV_ClientCommand(sv_client, "$%s %s \"\"\n", junk[0], junk[0]);
    SV_ClientCommand(sv_client, "$%s $%s\n", junk[1], junk[4]);
}

/*
================
SV_New_f

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
static bool SV_WorrCapabilityCarrierSupported(const client_t *client)
{
    return client &&
           (client->protocol == PROTOCOL_VERSION_Q2PRO ||
            client->protocol == PROTOCOL_VERSION_R1Q2 ||
            client->protocol == PROTOCOL_VERSION_RERELEASE);
}

static void SV_WriteWorrCapabilitySetting(int32_t index, uint32_t value)
{
    MSG_WriteByte(sv_client->protocol == PROTOCOL_VERSION_RERELEASE
                      ? svc_rr_setting
                      : svc_q2pro_setting);
    MSG_WriteLong(index);
    MSG_WriteLong((int32_t)value);
}

/* The adapter stages the complete record and performs one checked final copy.
 * A failed preflight or encode leaves msg_write byte-identical, so callers can
 * disable the unadvertised pilot and keep legacy. */
static bool SV_AppendNativeReadinessRecord(
    const worr_native_readiness_record_v1 *record)
{
    int opcode;

    if (!sv_client || sv_client->netchan.type != NETCHAN_NEW)
        return false;
    opcode = sv_client->protocol == PROTOCOL_VERSION_RERELEASE
                 ? svc_rr_setting
                 : svc_q2pro_setting;
    return SV_NativeShadowAppendSvcReadinessV1(
        &msg_write, &sv_client->netchan.message, opcode, record);
}

bool SV_MaintainNativeShadowChallengePending(client_t *client)
{
    sv_native_shadow_peer_v1 *pilot;

    if (!client || !client->worr_native_shadow_challenge_pending)
        return false;
    pilot = client->worr_native_shadow;
    if (!pilot) {
        client->worr_native_shadow_challenge_pending = false;
        client->worr_native_shadow_challenge_requested_at = 0;
        return false;
    }
    if (!client->worr_capability_confirm_sent ||
        client->worr_capability_epoch == 0 ||
        client->worr_capabilities_supported !=
            WORR_NET_CAP_LEGACY_STAGE_MASK ||
        client->worr_capabilities_negotiated !=
            WORR_NET_CAP_LEGACY_STAGE_MASK) {
        client->worr_native_shadow_challenge_pending = false;
        client->worr_native_shadow_challenge_requested_at = 0;
        SV_NativeShadowPeerDisableV1(
            pilot, SV_NATIVE_SHADOW_FAILURE_OFFICIAL_BINDING);
        return false;
    }
    if (!SV_NativeShadowPeerEnabledV1(pilot)) {
        client->worr_native_shadow_challenge_pending = false;
        client->worr_native_shadow_challenge_requested_at = 0;
        return false;
    }
    if (SV_NativeShadowChallengeQueueExpiredV1(
            client->worr_native_shadow_challenge_requested_at,
            svs.realtime)) {
        client->worr_native_shadow_challenge_pending = false;
        client->worr_native_shadow_challenge_requested_at = 0;
        SV_NativeShadowPeerDisableV1(
            pilot, SV_NATIVE_SHADOW_FAILURE_QUEUE);
        return false;
    }
    return true;
}

bool SV_TryQueueNativeShadowChallenge(client_t *client)
{
    byte staging_data[SV_NATIVE_SHADOW_SVC_WIRE_BYTES];
    sizebuf_t staging;
    sv_native_shadow_peer_v1 *pilot;
    worr_native_readiness_record_v1 challenge;
    int opcode;

    if (!SV_MaintainNativeShadowChallengePending(client))
        return false;
    pilot = client->worr_native_shadow;
    /* A busy channel is an expected deferral, not a readiness failure.  The
     * 10-second protocol deadline does not exist until this becomes true. */
    if (!SV_NativeShadowPostBootstrapQueueIdleV1(pilot))
        return false;

    SZ_Init(&staging, staging_data, sizeof(staging_data),
            "native_readiness_challenge");
    /* Preflight before advancing readiness: after this check, the validated
     * fixed record has an exact count-derived atomic append reservation. */
    if (!SV_NativeShadowCanAppendSvcReadinessV1(
            &staging, &client->netchan.message)) {
        client->worr_native_shadow_challenge_pending = false;
        client->worr_native_shadow_challenge_requested_at = 0;
        SV_NativeShadowPeerDisableV1(
            pilot, SV_NATIVE_SHADOW_FAILURE_QUEUE);
        return false;
    }
    if (!SV_NativeShadowBeginEpochBoundV1(
            pilot, client->worr_capability_epoch,
            client->worr_capabilities_supported,
            client->worr_capabilities_negotiated,
            pilot->mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT
                ? sv.worr_snapshot_epoch
                : 0,
            svs.realtime, &challenge)) {
        client->worr_native_shadow_challenge_pending = false;
        client->worr_native_shadow_challenge_requested_at = 0;
        return false;
    }
    opcode = client->protocol == PROTOCOL_VERSION_RERELEASE
                 ? svc_rr_setting
                 : svc_q2pro_setting;
    if (!SV_NativeShadowAppendSvcReadinessV1(
            &staging, &client->netchan.message, opcode,
            &challenge)) {
        client->worr_native_shadow_challenge_pending = false;
        client->worr_native_shadow_challenge_requested_at = 0;
        SV_NativeShadowPeerDisableV1(
            pilot, SV_NATIVE_SHADOW_FAILURE_QUEUE);
        return false;
    }
    SZ_Write(&client->netchan.message, staging.data, staging.cursize);
    client->worr_native_shadow_challenge_pending = false;
    client->worr_native_shadow_challenge_requested_at = 0;
    return true;
}

static uint32_t SV_NextWorrSessionEpoch(void)
{
    if (svs.worr_next_session_epoch == UINT32_MAX)
        return 0;
    return ++svs.worr_next_session_epoch;
}

static void SV_ConfirmWorrCapabilities(void)
{
    worr_net_capability_confirm_v1 confirm;
    const uint32_t supported = WORR_NET_CAP_LEGACY_STAGE_MASK;

    sv_client->worr_capabilities_supported = 0;
    sv_client->worr_capabilities_negotiated = 0;
    sv_client->worr_capability_epoch = 0;
    sv_client->worr_native_shadow_challenge_pending = false;
    sv_client->worr_native_shadow_challenge_requested_at = 0;
    if (sv_client->worr_capability_failed ||
        !SV_WorrCapabilityCarrierSupported(sv_client) ||
        sv_client->worr_capabilities_offered == 0 ||
        sv_client->spawncount <= 0) {
        return;
    }
    const uint32_t session_epoch = SV_NextWorrSessionEpoch();
    if (Worr_NetCapabilitySelectV1(
            session_epoch,
            sv_client->worr_capabilities_offered, supported,
            &confirm) != WORR_NET_CAPABILITY_OK ||
        confirm.negotiated == 0) {
        return;
    }

    sv_client->worr_capabilities_supported = confirm.supported;
    sv_client->worr_capabilities_negotiated = confirm.negotiated;
    sv_client->worr_capability_epoch = confirm.connection_epoch;
    sv_client->worr_capability_confirm_sent = true;
    if ((confirm.negotiated &
         WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1) != 0) {
        if (!Worr_LegacyCommandSidebandParserInitV1(
                &sv_client->worr_command_parser)) {
            sv_client->worr_capability_failed = true;
            sv_client->worr_capabilities_negotiated = 0;
            return;
        }
        sv_client->worr_command_parser_initialized = true;
        sv_client->worr_command_sideband_started = false;
        sv_client->worr_command_stream_initialized = false;
        sv_client->worr_command_bootstrap_moves = 0;
        sv_client->worr_command_bootstrap_commands = 0;
        sv_client->worr_command_bootstrap_sample_time_us = 0;
        sv_client->worr_command_fast_forward_attempts = 0;
        sv_client->worr_command_fast_forwards = 0;
        sv_client->worr_command_fast_forwarded_commands = 0;
        sv_client->worr_command_fast_forward_rejections = 0;
        sv_client->worr_command_gap_policy_rejections = 0;
    }
    SV_WriteWorrCapabilitySetting(
        WORR_NET_CAPABILITY_CONFIRM_EPOCH_SETTING,
        confirm.connection_epoch);
    SV_WriteWorrCapabilitySetting(
        WORR_NET_CAPABILITY_CONFIRM_SUPPORTED_SETTING,
        confirm.supported);
    SV_WriteWorrCapabilitySetting(
        WORR_NET_CAPABILITY_CONFIRM_NEGOTIATED_SETTING,
        confirm.negotiated);
}

void SV_New_f(void)
{
    clstate_t oldstate;

    Com_DPrintf("New() from %s\n", sv_client->name);

    oldstate = sv_client->state;
    if (sv_client->state < cs_connected) {
        Com_DPrintf("Going from cs_assigned to cs_connected for %s\n",
                    sv_client->name);
        sv_client->state = cs_connected;
        sv_client->lastmessage = svs.realtime; // don't timeout
        sv_client->connect_time = time(NULL);
    } else if (sv_client->state > cs_connected) {
        Com_DPrintf("New not valid -- already primed\n");
        return;
    }

    // stuff some junk, drop them and expect them to be back soon
    if (sv_force_reconnect->string[0] && !sv_client->reconnect_var[0] &&
        !NET_IsLocalAddress(&sv_client->netchan.remote_address)) {
        stuff_junk();
        SV_DropClient(sv_client, NULL);
        return;
    }

    SV_ClientCommand(sv_client, "\n");

    //
    // serverdata needs to go over for all types of servers
    // to make sure the protocol is right, and to set the gamedir
    //

    // create baselines for this client
    SV_CreateBaselines();
    reset_snapshot_shadow();

    q2proto_svc_message_t message = {.type = Q2P_SVC_SERVERDATA, .serverdata = {0}};
    q2proto_server_fill_serverdata(&sv_client->q2proto_ctx, &message.serverdata);
    message.serverdata.servercount = sv_client->spawncount;
    message.serverdata.attractloop = false;
    message.serverdata.gamedir = q2proto_make_string(sv_client->gamedir);
    if (sv.state == ss_pic || sv.state == ss_cinematic)
        message.serverdata.clientnum = -1;
    else
        message.serverdata.clientnum = sv_client->infonum;
    message.serverdata.levelname = q2proto_make_string(sv_client->configstrings[CS_NAME]);

    message.serverdata.strafejump_hack = sv_client->pmp.strafehack;

    message.serverdata.q2pro.server_state = sv.state;
    message.serverdata.q2pro.qw_mode = sv_client->pmp.qwmode;
    message.serverdata.q2pro.waterjump_hack = sv_client->pmp.waterhack;
    message.serverdata.q2repro.server_fps = SV_FRAMERATE;

    q2proto_server_write(&sv_client->q2proto_ctx, (uintptr_t)&sv_client->io_data, &message);

    SV_ConfirmWorrCapabilities();

    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);

    SV_ClientCommand(sv_client, "\n");

    // send version string request
    if (oldstate == cs_assigned) {
        SV_ClientCommand(sv_client, "cmd \177c version $version\n"
#if USE_AC_SERVER
                         "cmd \177c actoken $actoken\n"
#endif
                        );
        stuff_cmds(&sv_cmdlist_connect);
    }

    // send reconnect var request
    if (sv_force_reconnect->string[0] && !sv_client->reconnected) {
        SV_ClientCommand(sv_client, "cmd \177c connect $%s\n",
                         sv_client->reconnect_var);
    }

    stuff_cvar_bans();

    if (SV_CheckInfoBans(sv_client->userinfo, false))
        return;

    Com_DPrintf("Going from cs_connected to cs_primed for %s\n",
                sv_client->name);
    sv_client->state = cs_primed;

    memset(&sv_client->lastcmd, 0, sizeof(sv_client->lastcmd));

    if (sv.state == ss_pic || sv.state == ss_cinematic)
        return;

    // send gamestate
    write_gamestate();

    // send next command
    SV_ClientCommand(sv_client, "precache %i\n", sv_client->spawncount);
}

/*
==================
SV_Begin_f
==================
*/
void SV_Begin_f(void)
{
    Com_DPrintf("Begin() from %s\n", sv_client->name);

    // handle the case of a level changing while a client was connecting
    if (sv_client->state < cs_primed) {
        Com_DPrintf("Begin not valid -- not yet primed\n");
        SV_New_f();
        return;
    }
    if (sv_client->state > cs_primed) {
        Com_DPrintf("Begin not valid -- already spawned\n");
        return;
    }
    if (sv.state == ss_pic || sv.state == ss_cinematic) {
        Com_DPrintf("Begin not valid -- map not loaded\n");
        return;
    }

    if (!sv_client->version_string) {
        SV_DropClient(sv_client, "!failed version probe");
        return;
    }

    if (sv_force_reconnect->string[0] && !sv_client->reconnected) {
        SV_DropClient(sv_client, "!failed to reconnect");
        return;
    }

    if (!AC_ClientBegin(sv_client)) {
        return;
    }

    Com_DPrintf("Going from cs_primed to cs_spawned for %s\n",
                sv_client->name);
    sv_client->state = cs_spawned;
    sv_client->send_delta = 0;
    sv_client->command_msec = 1800;
    sv_client->cmd_msec_used = 0;
    sv_client->suppress_count = 0;
    sv_client->http_download = false;

    /* Request private readiness only after the complete join/bootstrap stream
     * and accepted begin.  The send scheduler must service this request after
     * all begin/game callbacks have queued their reliable output, and only at
     * a rate-admitted boundary that it will transmit immediately. */
    if (sv_client->worr_native_shadow) {
        sv_client->worr_native_shadow_challenge_pending = true;
        sv_client->worr_native_shadow_challenge_requested_at =
            svs.realtime;
    }

    SV_AlignKeyFrames(sv_client);

    stuff_cmds(&sv_cmdlist_begin);

    // allocate packet entities if not done yet
    if (!sv_client->entities) {
        int max_packet_entities = sv_client->csr->extended ? MAX_PACKET_ENTITIES : MAX_PACKET_ENTITIES_OLD;
        sv_client->num_entities = max_packet_entities * UPDATE_BACKUP;
        sv_client->entities = SV_Mallocz(sizeof(sv_client->entities[0]) * sv_client->num_entities);
    }

    // call the game begin function
    ge->ClientBegin(sv_player);

    AC_ClientAnnounce(sv_client);
}

//=============================================================================

void SV_CloseDownload(client_t *client)
{
    Z_Freep(&client->download);
    Z_Freep(&client->downloadname);
    client->downloadpending = false;
    q2proto_server_download_end(&client->download_state);
}

/*
==================
SV_NextDownload_f
==================
*/
static void SV_NextDownload_f(void)
{
    if (!sv_client->download)
        return;

    sv_client->downloadpending = true;
}

/*
==================
SV_BeginDownload_f
==================
*/
static void SV_BeginDownload_f(void)
{
    char    name[MAX_QPATH];
    byte    *download;
    int64_t downloadsize = 0;
    int     maxdownloadsize, result, offset = 0;
    cvar_t  *allow;
    size_t  len;
    qhandle_t f;
    q2proto_download_compress_t download_compress = Q2PROTO_DOWNLOAD_COMPRESS_AUTO;
    q2proto_server_download_state_t *download_state_ptr = NULL;

    if (Cmd_ArgvBuffer(1, name, sizeof(name)) >= sizeof(name)) {
        goto fail1;
    }

    // hack for 'status' command
    if (!strcmp(name, "http")) {
        sv_client->http_download = true;
        return;
    }

    len = FS_NormalizePath(name);

    if (Cmd_Argc() > 2)
        offset = Q_atoi(Cmd_Argv(2));   // downloaded offset

    // hacked by zoid to allow more control over download
    // first off, no .. or global allow check
    if (!allow_download->integer
        // check for empty paths
        || !len
        // check for illegal negative offsets
        || offset < 0
        // don't allow anything with .. path
        || strstr(name, "..")
        // leading dots, slashes, etc are no good
        || !Q_ispath(name[0])
        // trailing dots, slashes, etc are no good
        || !Q_ispath(name[len - 1])
        // MUST be in a subdirectory
        || !strchr(name, '/')) {
        Com_DPrintf("Refusing download of %s to %s\n", name, sv_client->name);
        goto fail1;
    }

    if (FS_pathcmpn(name, CONST_STR_LEN("players/")) == 0) {
        allow = allow_download_players;
    } else if (FS_pathcmpn(name, CONST_STR_LEN("models/")) == 0 ||
               FS_pathcmpn(name, CONST_STR_LEN("sprites/")) == 0) {
        allow = allow_download_models;
    } else if (FS_pathcmpn(name, CONST_STR_LEN("sound/")) == 0) {
        allow = allow_download_sounds;
    } else if (FS_pathcmpn(name, CONST_STR_LEN("maps/")) == 0) {
        allow = allow_download_maps;
    } else if (FS_pathcmpn(name, CONST_STR_LEN("textures/")) == 0 ||
               FS_pathcmpn(name, CONST_STR_LEN("env/")) == 0) {
        allow = allow_download_textures;
    } else if (FS_pathcmpn(name, CONST_STR_LEN("pics/")) == 0) {
        allow = allow_download_pics;
    } else {
        allow = allow_download_others;
    }

    if (!allow->integer) {
        Com_DPrintf("Refusing download of %s to %s\n", name, sv_client->name);
        goto fail1;
    }

    if (sv_client->download) {
        Com_DPrintf("Closing existing download for %s (should not happen)\n", sv_client->name);
        SV_CloseDownload(sv_client);
    }

    f = 0;

#if USE_ZLIB
    // prefer raw deflate stream from .pkz if supported
    if (sv_client->q2proto_ctx.features.download_compress_raw && offset == 0) {
        downloadsize = FS_OpenFile(name, &f, FS_MODE_READ | FS_FLAG_DEFLATE);
        if (f) {
            Com_DPrintf("Serving compressed download to %s\n", sv_client->name);
            download_compress = Q2PROTO_DOWNLOAD_COMPRESS_RAW;
        }
    }
#endif

    if (!f) {
        downloadsize = FS_OpenFile(name, &f, FS_MODE_READ);
        if (!f) {
            Com_DPrintf("Couldn't download %s to %s\n", name, sv_client->name);
            goto fail1;
        }
    }

    q2protoio_deflate_args_t *deflate_args = NULL;
#if USE_ZLIB
    deflate_args = &sv_client->q2proto_deflate;
#endif
    int err = q2proto_server_download_begin(&sv_client->q2proto_ctx, downloadsize, download_compress, deflate_args, &sv_client->download_state);
    if (err != Q2P_ERR_SUCCESS) {
        Com_DPrintf("Couldn't download %s to %s: %s\n", name, sv_client->name, q2proto_error_string(err));
        goto fail1;
    }
    download_state_ptr = &sv_client->download_state;

    maxdownloadsize = MAX_LOADFILE;
    if (sv_max_download_size->integer > 0) {
        maxdownloadsize = Cvar_ClampInteger(sv_max_download_size, 1, MAX_LOADFILE);
    }

    if (downloadsize == 0) {
        Com_DPrintf("Refusing empty download of %s to %s\n", name, sv_client->name);
        goto fail2;
    }

    if (downloadsize > maxdownloadsize) {
        Com_DPrintf("Refusing oversize download of %s to %s\n", name, sv_client->name);
        goto fail2;
    }

    if (offset > downloadsize) {
        Com_DPrintf("Refusing download, %s has wrong version of %s (%d > %d)\n",
                    sv_client->name, name, offset, (int)downloadsize);
        SV_ClientPrintf(sv_client, PRINT_HIGH, "File size differs from server.\n"
                        "Please delete the corresponding .tmp file from your system.\n");
        goto fail2;
    }

    if (offset == downloadsize) {
        Com_DPrintf("Refusing download, %s already has %s (%d bytes)\n",
                    sv_client->name, name, offset);
        FS_CloseFile(f);
        q2proto_svc_message_t message = {.type = Q2P_SVC_DOWNLOAD};
        q2proto_server_download_finish(&sv_client->download_state, &message.download);
        q2proto_server_write(&sv_client->q2proto_ctx, (uintptr_t)&sv_client->io_data, &message);
        SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
        q2proto_server_download_end(&sv_client->download_state);
        return;
    }

    download = SV_Malloc(downloadsize);
    result = FS_Read(download, downloadsize, f);
    if (result != downloadsize) {
        Com_DPrintf("Couldn't download %s to %s\n", name, sv_client->name);
        goto fail3;
    }

    FS_CloseFile(f);

    sv_client->download = download;
    sv_client->download_ptr = (uint8_t *)download + offset;
    sv_client->download_remaining = downloadsize - offset;
    sv_client->downloadname = SV_CopyString(name);
    sv_client->downloadpending = true;

    Com_DPrintf("Downloading %s to %s\n", name, sv_client->name);
    return;

fail3:
    Z_Free(download);
fail2:
    FS_CloseFile(f);
fail1:
    q2proto_svc_message_t message = {.type = Q2P_SVC_DOWNLOAD};
    q2proto_server_download_abort(download_state_ptr, &message.download);
    q2proto_server_write(&sv_client->q2proto_ctx, (uintptr_t)&sv_client->io_data, &message);
    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
    q2proto_server_download_end(download_state_ptr);
}

static void SV_StopDownload_f(void)
{
    if (!sv_client->download)
        return;

    q2proto_svc_message_t message = {.type = Q2P_SVC_DOWNLOAD};
    q2proto_server_download_abort(&sv_client->download_state, &message.download);
    q2proto_server_write(&sv_client->q2proto_ctx, (uintptr_t)&sv_client->io_data, &message);
    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);

    Com_DPrintf("Download of %s to %s stopped by user request\n",
                sv_client->downloadname, sv_client->name);
    SV_CloseDownload(sv_client);
    SV_AlignKeyFrames(sv_client);
}

//============================================================================

// a cinematic has completed or been aborted by a client, so move to the next server
static void SV_NextServer_f(void)
{
    if (sv.state != ss_pic && sv.state != ss_cinematic)
        return;     // can't nextserver while playing a normal game

    if (sv.state == ss_pic && !Cvar_VariableInteger("coop"))
        return;     // ss_pic can be nextserver'd in coop mode

    if (Q_atoi(Cmd_Argv(1)) != sv.spawncount)
        return;     // leftover from last server

    if (sv.nextserver_pending)
        return;

    sv.nextserver_pending = true;   // make sure another doesn't sneak in

    const char *v = Cvar_VariableString("nextserver");
    if (*v) {
        Cbuf_AddText(&cmd_buffer, v);
        Cbuf_AddText(&cmd_buffer, "\n");
    } else {
        Cbuf_AddText(&cmd_buffer, "killserver\n");
    }

    Cvar_Set("nextserver", "");
}

// the client is going to disconnect, so remove the connection immediately
static void SV_Disconnect_f(void)
{
    SV_DropClient(sv_client, "!?disconnected");
    SV_RemoveClient(sv_client);   // don't bother with zombie state
}

// dumps the serverinfo info string
static void SV_ShowServerInfo_f(void)
{
    char serverinfo[MAX_INFO_STRING];

    Cvar_BitInfo(serverinfo, CVAR_SERVERINFO);

    SV_ClientRedirect();
    Info_Print(serverinfo);
    Com_EndRedirect();
}

// dumps misc protocol info
static void SV_ShowMiscInfo_f(void)
{
    SV_ClientRedirect();
    SV_PrintMiscInfo();
    Com_EndRedirect();
}

static void SV_NoGameData_f(void)
{
    sv_client->nodata ^= 1;
    SV_AlignKeyFrames(sv_client);
}

static void SV_Lag_f(void)
{
    client_t *cl;

    if (Cmd_Argc() > 1) {
        SV_ClientRedirect();
        cl = SV_GetPlayer(Cmd_Argv(1), true);
        Com_EndRedirect();
        if (!cl) {
            return;
        }
    } else {
        cl = sv_client;
    }

    SV_ClientPrintf(sv_client, PRINT_HIGH,
                    "Lag stats for:       %s\n"
                    "RTT (min/avg/max):   %d/%d/%d ms\n"
                    "Server to client PL: %.2f%% (approx)\n"
                    "Client to server PL: %.2f%%\n"
                    "Timescale          : %.3f\n",
                    cl->name, cl->min_ping, AVG_PING(cl), cl->max_ping,
                    PL_S2C(cl), PL_C2S(cl), cl->timescale);
}

#if USE_PACKETDUP
static void SV_PacketdupHack_f(void)
{
    int numdups = sv_client->numpackets - 1;

    if (Cmd_Argc() > 1) {
        numdups = Q_atoi(Cmd_Argv(1));
        if (numdups < 0 || numdups > sv_packetdup_hack->integer) {
            SV_ClientPrintf(sv_client, PRINT_HIGH,
                            "Packetdup of %d is not allowed on this server.\n", numdups);
            return;
        }

        sv_client->numpackets = numdups + 1;
    }

    SV_ClientPrintf(sv_client, PRINT_HIGH,
                    "Server is sending %d duplicate packet%s to you.\n",
                    numdups, numdups == 1 ? "" : "s");
}
#endif

static bool match_cvar_val(const char *s, const char *v)
{
    switch (*s++) {
    case '*':
        return *v;
    case '=':
        return Q_atof(v) == Q_atof(s);
    case '<':
        return Q_atof(v) < Q_atof(s);
    case '>':
        return Q_atof(v) > Q_atof(s);
    case '~':
        return Q_stristr(v, s);
    case '#':
        return !Q_stricmp(v, s);
    default:
        return !Q_stricmp(v, s - 1);
    }
}

static bool match_cvar_ban(const cvarban_t *ban, const char *v)
{
    bool success = true;
    const char *s = ban->match;

    if (*s == '!') {
        s++;
        success = false;
    }

    return match_cvar_val(s, v) == success;
}

// returns true if matched ban is kickable
static bool handle_cvar_ban(const cvarban_t *ban, const char *v)
{
    if (!match_cvar_ban(ban, v))
        return false;

    if (ban->action == FA_LOG || ban->action == FA_KICK)
        Com_Printf("%s[%s]: matched cvarban: \"%s\" is \"%s\"\n", sv_client->name,
                   NET_AdrToString(&sv_client->netchan.remote_address), ban->var, v);

    if (ban->action == FA_LOG)
        return false;

    if (ban->comment) {
        q2proto_svc_message_t message;
        if (ban->action == FA_STUFF) {
            message.type = Q2P_SVC_STUFFTEXT;
            message.stufftext.string = q2proto_make_string(va("%s\n", ban->comment));
        } else {
            message.type = Q2P_SVC_PRINT;
            message.print.level = PRINT_HIGH;
            message.print.string = q2proto_make_string(va("%s\n", ban->comment));
        }
        q2proto_server_write(&sv_client->q2proto_ctx, (uintptr_t)&sv_client->io_data, &message);
        SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
    }

    if (ban->action == FA_KICK) {
        SV_DropClient(sv_client, "?was kicked");
        return true;
    }

    return false;
}

static void SV_CvarResult_f(void)
{
    cvarban_t *ban;
    char *c, *v;

    c = Cmd_Argv(1);
    if (!strcmp(c, "version")) {
        if (!sv_client->version_string) {
            v = Cmd_RawArgsFrom(2);
            if (COM_DEDICATED) {
                Com_Printf("%s[%s]: %s\n", sv_client->name,
                           NET_AdrToString(&sv_client->netchan.remote_address), v);
            }
            sv_client->version_string = SV_CopyString(v);
        }
    } else if (!strcmp(c, "connect")) {
        if (sv_client->reconnect_var[0]) {
            if (!strcmp(Cmd_Argv(2), sv_client->reconnect_val)) {
                sv_client->reconnected = true;
            }
        }
    } else if (!strcmp(c, "actoken")) {
        AC_ClientToken(sv_client, Cmd_Argv(2));
    } else if (!strcmp(c, "console")) {
        if (sv_client->console_queries > 0) {
            Com_Printf("%s[%s]: \"%s\" is \"%s\"\n", sv_client->name,
                       NET_AdrToString(&sv_client->netchan.remote_address),
                       Cmd_Argv(2), Cmd_RawArgsFrom(3));
            sv_client->console_queries--;
        }
    }

    LIST_FOR_EACH(cvarban_t, ban, &sv_cvarbanlist, entry) {
        if (!Q_stricmp(ban->var, c)) {
            if (handle_cvar_ban(ban, Cmd_RawArgsFrom(2)))
                return;
            stringCmdCount--;
        }
    }
}

static void SV_AC_List_f(void)
{
    SV_ClientRedirect();
    AC_List_f();
    Com_EndRedirect();
}

static void SV_AC_Info_f(void)
{
    SV_ClientRedirect();
    AC_Info_f();
    Com_EndRedirect();
}

static const ucmd_t ucmds[] = {
    // auto issued
    { "new", SV_New_f },
    { "begin", SV_Begin_f },
    { "baselines", NULL },
    { "configstrings", NULL },
    { "nextserver", SV_NextServer_f },
    { "disconnect", SV_Disconnect_f },

    // issued by hand at client consoles
    { "info", SV_ShowServerInfo_f },
    { "sinfo", SV_ShowMiscInfo_f },

    { "download", SV_BeginDownload_f },
    { "nextdl", SV_NextDownload_f },
    { "stopdl", SV_StopDownload_f },

    { "\177c", SV_CvarResult_f },
    { "nogamedata", SV_NoGameData_f },
    { "lag", SV_Lag_f },
#if USE_PACKETDUP
    { "packetdup", SV_PacketdupHack_f },
#endif
    { "aclist", SV_AC_List_f },
    { "acinfo", SV_AC_Info_f },

    { NULL, NULL }
};

static void handle_filtercmd(const filtercmd_t *filter)
{
    if (filter->action == FA_IGNORE)
        return;

    if (filter->action == FA_LOG || filter->action == FA_KICK)
        Com_Printf("%s[%s]: issued banned command: %s\n", sv_client->name,
                   NET_AdrToString(&sv_client->netchan.remote_address), filter->string);

    if (filter->action == FA_LOG)
        return;

    if (filter->comment) {
        q2proto_svc_message_t message;
        if (filter->action == FA_STUFF) {
            message.type = Q2P_SVC_STUFFTEXT;
            message.stufftext.string = q2proto_make_string(va("%s\n", filter->comment));
        } else {
            message.type = Q2P_SVC_PRINT;
            message.print.level = PRINT_HIGH;
            message.print.string = q2proto_make_string(va("%s\n", filter->comment));
        }
        q2proto_server_write(&sv_client->q2proto_ctx, (uintptr_t)&sv_client->io_data, &message);
        SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
    }

    if (filter->action == FA_KICK)
        SV_DropClient(sv_client, "?was kicked");
}

void PF_Broadcast_Print(int level, const char *msg);

/*
==================
SV_StubbedSayCommand
==================
*/
static void SV_StubbedSayCommand(client_t *sender, bool is_team)
{
    // FIXME: make this better
    PF_Broadcast_Print(PRINT_CHAT, va("%s: %s\n", sender->name, Cmd_Args()));
}

/*
==================
SV_ExecuteUserCommand
==================
*/
static void SV_ExecuteUserCommand(const char *s)
{
    const ucmd_t *u;
    filtercmd_t *filter;
    char *c;

    Cmd_TokenizeString(s, false);
    sv_player = sv_client->edict;

    c = Cmd_Argv(0);
    if (!c[0]) {
        return;
    }

    if ((u = Com_Find(ucmds, c)) != NULL) {
        if (u->func) {
            u->func();
        }
        return;
    }

    if (sv.state == ss_pic || sv.state == ss_cinematic) {
        return;
    }

    if (sv_client->state != cs_spawned && !sv_allow_unconnected_cmds->integer) {
        return;
    }

    LIST_FOR_EACH(filtercmd_t, filter, &sv_filterlist, entry) {
        if (!Q_stricmp(filter->string, c)) {
            handle_filtercmd(filter);
            return;
        }
    }

    bool is_say = false;

    if (!strcmp(c, "say") || !strcmp(c, "say_team")) {
        // don't timeout. only chat commands count as activity.
        sv_client->lastactivity = svs.realtime;
        is_say = true;

        if (svs.csr.extended) {
            if (!svs.server_supplied_say && !svs.scanned_for_say_cmd) {
                svs.scan_for_say_cmd = true;
                svs.scanned_for_say_cmd = true;
            }
        }
    }

    if (!is_say || !svs.server_supplied_say)
        ge->ClientCommand(sv_player);

    svs.scan_for_say_cmd = false;
    
    if (is_say && svs.server_supplied_say)
        // `say`/`say_team` isn't handled by the game code
        // so we have to basically make our own version of it.
        SV_StubbedSayCommand(sv_client, strcmp(c, "say_team") == 0);
}

/*
===========================================================================

USER CMD EXECUTION

===========================================================================
*/

static bool     moveIssued;
static int      userinfoUpdateCount;
static worr_legacy_command_range_v1 packetCommandRange;
static bool packetCommandRangeValid;
/* Packet execution is single-threaded.  Transaction scratch is shared rather
 * than multiplying roughly half of the command-stream footprint per client. */
static worr_command_stream_slot_v1
    worrCommandStreamScratch[CMD_BACKUP];
static worr_command_record_v1
    worrCommandRecordScratch[WORR_LEGACY_COMMAND_BATCH_MAX_COUNT];

/*
==================
SV_ClientThink
==================
*/
static inline void SV_ClientThink(usercmd_t *cmd,
                                  bool canonical_context_active)
{
    usercmd_t *old = &sv_client->lastcmd;

    if (!canonical_context_active)
        SV_CommandContextReset();

    // Bind the command to a server-owned simulation clock.  The client only
    // acknowledges a wire snapshot number; SV_SetLastFrame validates that
    // acknowledgement and maps it back to the simulation frame captured when
    // the snapshot was built.  Bots execute in the current simulation frame.
    cmd->server_frame = sv_client->bot ? (uint32_t)sv.framenum
                                       : sv_client->last_acked_server_frame;
    cmd->server_frame_delta =
        sv_client->bot ? 1u : sv_client->last_acked_server_frame_delta;

    sv_client->command_msec -= cmd->msec;
    sv_client->cmd_msec_used += cmd->msec;
    sv_client->num_moves++;

    if (sv_client->command_msec < 0 && sv_enforcetime->integer) {
        Com_DPrintf("commandMsec underflow from %s: %d\n",
                    sv_client->name, sv_client->command_msec);
        return;
    }

    if (cmd->buttons != old->buttons
        || cmd->forwardmove != old->forwardmove
        || cmd->sidemove != old->sidemove) {
        // don't timeout
        sv_client->lastactivity = svs.realtime;
    }

    ge->ClientThink(sv_player, cmd);

    if (!canonical_context_active)
        SV_CommandContextReset();
}

void SV_BotClientThink(client_t *client, usercmd_t *cmd)
{
    client_t *saved_client;
    edict_t *saved_player;

    if (!client || !cmd || !client->bot || client->state != cs_spawned ||
        !client->edict || !ge || !ge->ClientThink) {
        return;
    }

    saved_client = sv_client;
    saved_player = sv_player;
    sv_client = client;
    sv_player = client->edict;

    if (client->command_msec < cmd->msec) {
        client->command_msec = cmd->msec;
    }
    SV_ClientThink(cmd, false);
    client->lastcmd = *cmd;

    sv_client = saved_client;
    sv_player = saved_player;
}

static void SV_SetLastFrame(int lastframe)
{
    client_frame_t *frame;
    uint32_t acknowledged_server_frame = 0;
    uint32_t acknowledged_server_frame_delta = 0;
    uint64_t acknowledged_server_time_us = 0;

    if (lastframe > 0) {
        if (lastframe >= sv_client->framenum) {
            // An impossible future acknowledgement must not retain an older
            // valid rewind authority.  Ignore it for delta state and fail the
            // lag-compensation watermark closed.
            sv_client->last_acked_server_frame = 0;
            sv_client->last_acked_server_frame_delta = 0;
            sv_client->last_acked_server_time_us = 0;
            return; // ignore invalid acks
        }

        if (lastframe <= sv_client->lastframe)
            return; // ignore duplicate acks

        if (sv_client->framenum - lastframe <= UPDATE_BACKUP) {
            frame = &sv_client->frames[lastframe & UPDATE_MASK];

            if (frame->number == lastframe) {
                // save time for ping calc
                if (frame->sentTime <= com_eventTime)
                    frame->latency = com_eventTime - frame->sentTime;

                // Preserve the authoritative simulation frame associated with
                // this acknowledged client-rate snapshot.  Never derive this
                // from the client-supplied snapshot number itself.
                acknowledged_server_frame = frame->server_frame;
                acknowledged_server_frame_delta = frame->server_frame_delta;
                acknowledged_server_time_us = frame->server_time_us;
            }
        }

        // count valid ack
        sv_client->frames_acked++;
    }

    sv_client->lastframe = lastframe;
    // If the acknowledged snapshot has already fallen out of the validated
    // ring (or its slot does not match), fail closed instead of retaining an
    // older rewind watermark indefinitely.
    sv_client->last_acked_server_frame = acknowledged_server_frame;
    sv_client->last_acked_server_frame_delta =
        acknowledged_server_frame_delta;
    sv_client->last_acked_server_time_us = acknowledged_server_time_us;
}

static bool SV_WorrCommandSidebandActive(void)
{
    return sv_client && !sv_client->worr_capability_failed &&
           (sv_client->worr_capabilities_negotiated &
            WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1) != 0 &&
           sv_client->worr_command_parser_initialized;
}

#define WORR_CANONICAL_COMMAND_MAX_TRANSPORT_GAP 4096u

static void SV_WorrSaturatingIncrement(uint64_t *value)
{
    if (*value != UINT64_MAX)
        ++*value;
}

static void SV_WorrSaturatingAdd(uint64_t *value, uint64_t amount)
{
    *value = *value > UINT64_MAX - amount
                 ? UINT64_MAX
                 : *value + amount;
}

static bool SV_WorrRejectCommandGapPolicy(void)
{
    SV_WorrSaturatingIncrement(
        &sv_client->worr_command_gap_policy_rejections);
    return false;
}

static void SV_WorrLegacyCommandThink(usercmd_t *command)
{
    SV_ClientThink(command, false);
    if (SV_WorrCommandSidebandActive() &&
        !sv_client->worr_command_stream_initialized) {
        const uint64_t duration_us =
            (uint64_t)command->msec * UINT64_C(1000);
        if (sv_client->worr_command_bootstrap_sample_time_us >
            UINT64_MAX - duration_us) {
            sv_client->worr_command_bootstrap_sample_time_us = UINT64_MAX;
        } else {
            sv_client->worr_command_bootstrap_sample_time_us += duration_us;
        }
        if (sv_client->worr_command_bootstrap_commands != UINT32_MAX)
            ++sv_client->worr_command_bootstrap_commands;
    }
}

static int SV_WorrCommandIdCompare(worr_command_id_v1 left,
                                   worr_command_id_v1 right)
{
    if (left.epoch != right.epoch)
        return left.epoch < right.epoch ? -1 : 1;
    if (left.sequence != right.sequence)
        return left.sequence < right.sequence ? -1 : 1;
    return 0;
}

static bool SV_WorrCommandRangeId(
    const worr_legacy_command_range_v1 *range, uint32_t index,
    worr_command_id_v1 *id_out)
{
    worr_command_id_v1 id;
    uint32_t i;
    if (!range || !id_out || index >= range->command_count)
        return false;
    id = range->first_command_id;
    for (i = 0; i < index; ++i) {
        if (!Worr_CommandIdNextV1(id, &id))
            return false;
    }
    *id_out = id;
    return true;
}

static worr_command_render_watermark_v1 SV_WorrCommandWatermark(
    uint32_t provenance)
{
    worr_command_render_watermark_v1 watermark;
    memset(&watermark, 0, sizeof(watermark));
    watermark.struct_size = sizeof(watermark);
    watermark.schema_version = WORR_COMMAND_ABI_VERSION;
    watermark.provenance = provenance;
    watermark.source_server_tick = sv_client->last_acked_server_frame;
    watermark.tick_interval_us = (uint32_t)SV_FRAMETIME * 1000u;
    watermark.source_server_time_us =
        sv_client->last_acked_server_time_us;
    watermark.rendered_server_time_us =
        sv_client->last_acked_server_time_us;
    return watermark;
}

static bool SV_WorrBuildCommandContext(
    const worr_command_record_v1 *record,
    worr_authoritative_command_context_v1 *context_out)
{
    uint64_t current_time_us;
    uint64_t later_command_us;
    uint64_t interpolation_bias_us;
    uint64_t correction_us;
    uint64_t error_bound_us;
    uint64_t source_interval_us;
    uint32_t current_tick;
    uint32_t source_delta;
    const uint32_t tick_interval_us =
        (uint32_t)SV_FRAMETIME * 1000u;
    const worr_command_render_watermark_v1 *watermark;

    if (!record || !context_out || sv.framenum < 0 ||
        sv.worr_snapshot_epoch == 0 || tick_interval_us == 0 ||
        !Worr_CommandRecordValidateV1(
            record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS)) {
        return false;
    }
    /* Commands are consumed between game frames. sv.framenum names the next
     * frame to run, while the accumulated clock and authoritative world still
     * describe the preceding completed frame. */
    current_tick = sv.framenum > 0 ? (uint32_t)sv.framenum - 1u : 0u;
    current_time_us = sv.worr_server_time_us;
    watermark = &record->render_watermark;
    if (watermark->provenance ==
            WORR_COMMAND_RENDER_PROVENANCE_NONE ||
        watermark->source_server_tick == 0 ||
        watermark->source_server_tick > current_tick ||
        watermark->source_server_tick == UINT32_MAX ||
        current_tick == UINT32_MAX ||
        watermark->source_server_tick !=
            sv_client->last_acked_server_frame ||
        watermark->source_server_time_us !=
            sv_client->last_acked_server_time_us ||
        watermark->source_server_time_us > current_time_us ||
        watermark->tick_interval_us != tick_interval_us) {
        return false;
    }
    if (sv_client->worr_command_stream.last_received_sample_time_us <
        record->sample_time_us) {
        return false;
    }
    later_command_us =
        sv_client->worr_command_stream.last_received_sample_time_us -
        record->sample_time_us;
    /*
     * Bind interpolation uncertainty to the exact validated acknowledgement
     * captured by SV_SetLastFrame.  Looking this up by simulation tick alone
     * is ambiguous after a map reset because old ring slots can reuse ticks.
     */
    source_delta = sv_client->last_acked_server_frame_delta;
    if (source_delta > watermark->source_server_tick)
        return false;
    source_interval_us = (uint64_t)source_delta * tick_interval_us;
    interpolation_bias_us = source_interval_us / 2u;
    if (later_command_us > UINT64_MAX - interpolation_bias_us)
        return false;
    correction_us = later_command_us + interpolation_bias_us;
    error_bound_us =
        (uint64_t)record->command.duration_ms * UINT64_C(1000);
    if (error_bound_us > UINT64_MAX - tick_interval_us)
        return false;
    error_bound_us += tick_interval_us;
    if (error_bound_us > UINT64_MAX - interpolation_bias_us)
        return false;
    error_bound_us += interpolation_bias_us;

    memset(context_out, 0, sizeof(*context_out));
    context_out->struct_size = sizeof(*context_out);
    context_out->schema_version = WORR_COMMAND_CONTEXT_API_VERSION;
    context_out->client_index = (uint32_t)sv_client->number;
    context_out->command = *record;

    context_out->current_snapshot.struct_size =
        sizeof(context_out->current_snapshot);
    context_out->current_snapshot.schema_version =
        WORR_REWIND_ABI_VERSION;
    if (current_tick <= 1u)
        context_out->current_snapshot.flags |=
            WORR_REWIND_SNAPSHOT_MAP_RESET;
    context_out->current_snapshot.tick_interval_us = tick_interval_us;
    context_out->current_snapshot.snapshot_id.epoch =
        sv.worr_snapshot_epoch;
    context_out->current_snapshot.snapshot_id.sequence = current_tick + 1u;
    context_out->current_snapshot.server_tick = current_tick;
    context_out->current_snapshot.server_time_us = current_time_us;
    context_out->current_snapshot.consumed_command.cursor.epoch =
        record->command_id.epoch;
    context_out->current_snapshot.consumed_command.cursor
        .contiguous_sequence = record->command_id.sequence;
    context_out->current_snapshot.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;

    context_out->mapping_proof.struct_size =
        sizeof(context_out->mapping_proof);
    context_out->mapping_proof.schema_version =
        WORR_REWIND_ABI_VERSION;
    context_out->mapping_proof.flags =
        WORR_REWIND_MAPPING_AUTHENTICATED_TIMELINE;
    context_out->mapping_proof.command_id = record->command_id;
    context_out->mapping_proof.source_snapshot_id.epoch =
        sv.worr_snapshot_epoch;
    context_out->mapping_proof.source_snapshot_id.sequence =
        watermark->source_server_tick + 1u;
    context_out->mapping_proof.source_server_tick =
        watermark->source_server_tick;
    context_out->mapping_proof.tick_interval_us = tick_interval_us;
    context_out->mapping_proof.watermark_provenance =
        watermark->provenance;
    context_out->mapping_proof.watermark_flags = watermark->flags;
    context_out->mapping_proof.source_server_time_us =
        watermark->source_server_time_us;
    context_out->mapping_proof.rendered_server_time_us =
        watermark->rendered_server_time_us;
    if (watermark->provenance ==
        WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED) {
        context_out->mapping_proof.mapped_server_time_us =
            watermark->rendered_server_time_us >= correction_us
                ? watermark->rendered_server_time_us - correction_us
                : 0;
        context_out->mapping_proof.mapping_error_bound_us =
            max(error_bound_us, UINT64_C(1));
    } else {
        context_out->mapping_proof.mapped_server_time_us =
            watermark->rendered_server_time_us;
        context_out->mapping_proof.mapping_error_bound_us = 0;
    }
    return Worr_RewindMappingProofValidateV1(
        &context_out->mapping_proof);
}

static bool SV_WorrCommandStreamInit(
    const worr_legacy_command_range_v1 *range,
    uint32_t already_executed_prefix,
    uint32_t maximum_initial_gap)
{
    worr_command_cursor_v1 baseline;
    worr_command_id_v1 boundary;
    uint64_t maximum_prior_sequence;
    if (sv_client->worr_command_stream_initialized)
        return true;
    if (!range || !Worr_LegacyCommandRangeValidateV1(range) ||
        already_executed_prefix > range->command_count ||
        range->first_command_id.epoch !=
            sv_client->worr_capability_epoch ||
        sv_client->worr_command_bootstrap_commands == UINT32_MAX)
        return false;
    maximum_prior_sequence =
        (uint64_t)sv_client->worr_command_bootstrap_commands +
        maximum_initial_gap;
    if ((uint64_t)range->first_command_id.sequence - 1u >
        maximum_prior_sequence) {
        return false;
    }
    baseline.epoch = sv_client->worr_capability_epoch;
    baseline.contiguous_sequence =
        sv_client->worr_command_bootstrap_commands;
    if (already_executed_prefix != 0) {
        if (!SV_WorrCommandRangeId(range,
                                   already_executed_prefix - 1u,
                                   &boundary) ||
            SV_WorrCommandIdCompare(
                boundary,
                (worr_command_id_v1){baseline.epoch,
                                     baseline.contiguous_sequence}) > 0) {
            return false;
        }
    }
    if (already_executed_prefix < range->command_count) {
        if (!SV_WorrCommandRangeId(range, already_executed_prefix,
                                   &boundary) ||
            SV_WorrCommandIdCompare(
                boundary,
                (worr_command_id_v1){baseline.epoch,
                                     baseline.contiguous_sequence}) <= 0) {
            return false;
        }
    }
    if (!Worr_CommandStreamInitV1(
            &sv_client->worr_command_stream,
            sv_client->worr_command_slots, CMD_BACKUP,
            WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS,
            baseline,
            sv_client->worr_command_bootstrap_sample_time_us)) {
        return false;
    }
    sv_client->worr_command_stream_initialized = true;
    return true;
}

static void SV_WorrQueueLocalInteractionAuthorityReceipt(
    worr_command_id_v1 command_id)
{
    worr_local_interaction_authority_receipt_v1 receipt;
    worr_event_record_v1 candidate;
    ptrdiff_t client_index;

    if (!sv_client || !svs.client_pool || sv_client < svs.client_pool ||
        sv_client >= svs.client_pool + svs.maxclients) {
        return;
    }
    client_index = sv_client - svs.client_pool;
    if (!SV_LocalInteractionAuthorityTakeReceiptForCommand(
            (uint32_t)client_index, command_id, &receipt)) {
        return;
    }

    /* This is a private control receipt, not a world event.  Its absent
     * entity references, authoritative-only prediction class, and dedicated
     * payload kind make it non-presentable on the cgame side.  Queue it only
     * after the authenticated command callback has completed: the native
     * sender then owns reliable retransmission and epoch cancellation. */
    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_EVENT_ABI_VERSION;
    candidate.model_revision = WORR_EVENT_MODEL_REVISION;
    candidate.flags = WORR_EVENT_FLAG_CRITICAL;
    candidate.source_tick = sv.framenum > 0 ?
                                (uint32_t)(sv.framenum - 1) :
                                0;
    candidate.source_time_us = sv.worr_server_time_us;
    candidate.source_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.subject_entity.index = WORR_EVENT_NO_ENTITY;
    candidate.event_type = WORR_EVENT_TYPE_AUTHORITY_RECEIPT;
    candidate.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    candidate.prediction_class = WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    candidate.payload_kind =
        WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1;
    candidate.payload_size = sizeof(receipt);
    memcpy(candidate.payload, &receipt, sizeof(receipt));

    (void)SV_NativeShadowQueueEventCandidatesV1(
        sv_client->worr_native_shadow, &candidate, 1, svs.realtime);
}

static bool SV_WorrConsumeCommand(worr_command_id_v1 command_id,
                                  usercmd_t *command,
                                  bool simulate)
{
    worr_command_id_v1 next;
    worr_command_record_v1 retained;
    worr_command_stream_result_v1 result;
    if (!Worr_CommandStreamCopyRecordV1(
            &sv_client->worr_command_stream, command_id, &retained)) {
        /* An already reclaimed command is an idempotent retry. */
        return SV_WorrCommandIdCompare(
                   command_id,
                   (worr_command_id_v1){
                       sv_client->worr_command_stream.consumed_cursor.epoch,
                       sv_client->worr_command_stream.consumed_cursor
                           .contiguous_sequence}) <= 0;
    }
    if (!Worr_CommandCursorNextIdV1(
            sv_client->worr_command_stream.consumed_cursor, &next)) {
        return false;
    }
    if (SV_WorrCommandIdCompare(command_id, next) < 0)
        return true;
    if (SV_WorrCommandIdCompare(command_id, next) != 0)
        return false;
    if (simulate) {
        worr_authoritative_command_context_v1 context;
        bool context_started;

        /*
         * A game callback can terminate through the engine's non-local error
         * path.  Clear any abandoned callback scope before exposing authority
         * for this command so one client can never inherit another's context.
         */
        SV_CommandContextReset();
        context_started = false;
        if (retained.render_watermark.provenance !=
                WORR_COMMAND_RENDER_PROVENANCE_SERVER_SYNTHESIZED &&
            SV_WorrBuildCommandContext(&retained, &context)) {
            context_started = SV_CommandContextBegin(&context);
        }
        if (!context_started)
            context_started = SV_CommandContextBeginRejected();
        if (!context_started)
            return false;
        SV_ClientThink(command, true);
        SV_CommandContextEnd();
        SV_WorrQueueLocalInteractionAuthorityReceipt(command_id);
    }
    result = Worr_CommandStreamConsumeV1(
        &sv_client->worr_command_stream, command_id, NULL);
    return result == WORR_COMMAND_STREAM_CONSUMED ||
           result == WORR_COMMAND_STREAM_ALREADY_CONSUMED;
}

static bool SV_WorrFillCommandGap(
    const worr_legacy_command_range_v1 *range,
    uint32_t maximum_gap,
    uint32_t simulation_budget)
{
    worr_command_id_v1 next;
    uint32_t gap_count = 0;
    uint32_t synthesized = 0;
    uint32_t synthesis_count;
    uint64_t duration_us;
    uint64_t gap_time_us;

    if (!Worr_CommandCursorNextIdV1(
            sv_client->worr_command_stream.received_cursor, &next)) {
        return SV_WorrRejectCommandGapPolicy();
    }
    if (SV_WorrCommandIdCompare(next, range->first_command_id) >= 0)
        return true;

    /* Packet history authenticates maximum_gap.  Identity distance is then
     * computed in constant time and cannot borrow retention capacity as a
     * protocol limit. */
    if (maximum_gap > WORR_CANONICAL_COMMAND_MAX_TRANSPORT_GAP ||
        !Worr_CommandCursorGapBeforeV1(
            sv_client->worr_command_stream.received_cursor,
            range->first_command_id, maximum_gap, &gap_count)) {
        return SV_WorrRejectCommandGapPolicy();
    }

    /* Preflight every fallible property before any synthetic command reaches
     * the game module.  A later fast-forward can therefore never discover a
     * predictable overflow after a partially simulated prefix. */
    duration_us =
        (uint64_t)sv_client->lastcmd.msec * UINT64_C(1000);
    if (!Worr_CommandStreamValidateV1(
            &sv_client->worr_command_stream) ||
        sv_client->worr_command_stream.received_cursor.epoch !=
            sv_client->worr_command_stream.consumed_cursor.epoch ||
        sv_client->worr_command_stream.received_cursor
                .contiguous_sequence !=
            sv_client->worr_command_stream.consumed_cursor
                .contiguous_sequence ||
        sv_client->lastcmd.msec >
            sv_client->worr_command_stream.max_duration_ms ||
        (duration_us != 0 && gap_count > UINT64_MAX / duration_us)) {
        return SV_WorrRejectCommandGapPolicy();
    }
    gap_time_us = duration_us * gap_count;
    if (sv_client->worr_command_stream.last_received_sample_time_us >
        UINT64_MAX - gap_time_us) {
        return SV_WorrRejectCommandGapPolicy();
    }

    /* Small gaps retain the complete synthetic audit trail.  A legitimate
     * transport gap can exceed the fixed retention ring, though.  In that
     * case simulate only the policy budget and fast-forward the remaining
     * already-missed commands in O(capacity), rather than treating retention
     * size as a protocol-validity limit. */
    synthesis_count = gap_count <= (uint32_t)CMD_BACKUP - 1u
                          ? gap_count
                          : min(gap_count, simulation_budget);

    while (synthesized < synthesis_count) {
        worr_command_record_v1 record;
        memset(&record, 0, sizeof(record));
        record.struct_size = sizeof(record);
        record.schema_version = WORR_COMMAND_ABI_VERSION;
        record.command_id = next;
        record.movement_model_revision =
            WORR_PREDICTION_MODEL_REVISION;
        if (!NetUsercmd_ToPredictionCommandV1(
                &sv_client->lastcmd, &record.command)) {
            return false;
        }
        record.render_watermark = SV_WorrCommandWatermark(
            WORR_COMMAND_RENDER_PROVENANCE_SERVER_SYNTHESIZED);
        if (sv_client->worr_command_stream.last_received_sample_time_us >
            UINT64_MAX - duration_us) {
            return false;
        }
        record.sample_time_us =
            sv_client->worr_command_stream.last_received_sample_time_us +
            duration_us;
        if (!Worr_CommandRecordCanonicalizeV1(
                &record, WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) ||
            Worr_CommandStreamInsertV1(
                &sv_client->worr_command_stream, &record) !=
                WORR_COMMAND_STREAM_INSERTED ||
            !SV_WorrConsumeCommand(
                next, &sv_client->lastcmd,
                synthesized < simulation_budget) ||
            !Worr_CommandIdNextV1(next, &next)) {
            return false;
        }
        ++synthesized;
    }

    if (synthesized < gap_count) {
        const worr_command_cursor_v1 before =
            sv_client->worr_command_stream.received_cursor;
        const uint32_t remaining = gap_count - synthesized;
        worr_command_stream_result_v1 result;

        SV_WorrSaturatingIncrement(
            &sv_client->worr_command_fast_forward_attempts);
        result = Worr_CommandStreamFastForwardV1(
            &sv_client->worr_command_stream, remaining,
            sv_client->lastcmd.msec);
        if (result != WORR_COMMAND_STREAM_FAST_FORWARDED) {
            SV_WorrSaturatingIncrement(
                &sv_client->worr_command_fast_forward_rejections);
            return false;
        }
        SV_WorrSaturatingIncrement(
            &sv_client->worr_command_fast_forwards);
        SV_WorrSaturatingAdd(
            &sv_client->worr_command_fast_forwarded_commands,
            remaining);
        Com_DPrintf(
            "canonical command fast-forward: client=%s from=%u:%u "
            "to=%u:%u gap=%u synthesized=%u skipped=%u allowance=%u "
            "totals=%llu/%llu/%llu/%llu policy_rejections=%llu\n",
            sv_client->name, before.epoch,
            before.contiguous_sequence,
            sv_client->worr_command_stream.received_cursor.epoch,
            sv_client->worr_command_stream.received_cursor
                .contiguous_sequence,
            gap_count, synthesized, remaining, maximum_gap,
            (unsigned long long)
                sv_client->worr_command_fast_forward_attempts,
            (unsigned long long)
                sv_client->worr_command_fast_forwards,
            (unsigned long long)
                sv_client->worr_command_fast_forwarded_commands,
            (unsigned long long)
                sv_client->worr_command_fast_forward_rejections,
            (unsigned long long)
                sv_client->worr_command_gap_policy_rejections);
        next = range->first_command_id;
    }
    return SV_WorrCommandIdCompare(next,
                                   range->first_command_id) == 0;
}

/*
==============================
SV_WorrCommandGapSelfTest_f

This is a headless operator regression over the exact production recovery
function, not a second command-stream implementation.  Large packet loss uses
simulation_budget zero in SV_OldClientExecuteMove and
SV_EnhancedClientExecuteMove, so the two fixed cases exercise that same
fast-forward-only branch without needing a renderer, a client session, or a
game callback.  The synthetic client is stack-owned and all global callback
pointers are restored before the status line is published.
==============================
*/
void SV_WorrCommandGapSelfTest_f(void)
{
    static const uint32_t gap_cases[] = {161u, 401u};
    const worr_command_cursor_v1 baseline = {
        .epoch = 1u,
        .contiguous_sequence = 1000u,
    };
    const uint32_t maximum_gap = WORR_CANONICAL_COMMAND_MAX_TRANSPORT_GAP;
    const uint32_t command_duration_ms = 16u;
    size_t index;

    for (index = 0; index < q_countof(gap_cases); ++index) {
        const uint32_t expected_gap = gap_cases[index];
        const uint32_t expected_sequence =
            baseline.contiguous_sequence + expected_gap;
        client_t synthetic_client;
        client_t *saved_client = sv_client;
        edict_t *saved_player = sv_player;
        worr_legacy_command_range_v1 range;
        bool initialized = false;
        bool invoked = false;
        bool stream_valid = false;
        bool cursor_valid = false;

        memset(&synthetic_client, 0, sizeof(synthetic_client));
        memset(&range, 0, sizeof(range));
        Q_strlcpy(synthetic_client.name, "command-gap-selftest",
                  sizeof(synthetic_client.name));
        synthetic_client.lastcmd.msec = command_duration_ms;

        initialized =
            Worr_CommandStreamInitV1(
                &synthetic_client.worr_command_stream,
                synthetic_client.worr_command_slots, CMD_BACKUP,
                WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS, baseline,
                UINT64_C(16000000)) &&
            Worr_LegacyCommandRangeInitV1(
                &range,
                (worr_command_id_v1){
                    .epoch = baseline.epoch,
                    .sequence = expected_sequence + 1u,
                },
                1u);

        if (initialized) {
            /* This is the production policy selected for net_drop >= 20. */
            sv_client = &synthetic_client;
            sv_player = NULL;
            invoked = SV_WorrFillCommandGap(&range, maximum_gap, 0u);
            stream_valid =
                Worr_CommandStreamValidateV1(
                    &synthetic_client.worr_command_stream);
            cursor_valid =
                synthetic_client.worr_command_stream.received_cursor.epoch ==
                    baseline.epoch &&
                synthetic_client.worr_command_stream.consumed_cursor.epoch ==
                    baseline.epoch &&
                synthetic_client.worr_command_stream.received_cursor
                        .contiguous_sequence == expected_sequence &&
                synthetic_client.worr_command_stream.consumed_cursor
                        .contiguous_sequence == expected_sequence;
        }

        sv_client = saved_client;
        sv_player = saved_player;

        Com_Printf(
            "worr_command_gap_selftest: case=gap-%u status=%s gap=%u "
            "synthesized=0 skipped=%" PRIu64 " received=%u:%u "
            "consumed=%u:%u attempts=%" PRIu64 " fast_forwards=%" PRIu64
            " fast_forwarded=%" PRIu64 " rejections=%" PRIu64
            " policy_rejections=%" PRIu64 " stream_valid=%u "
            "cursor_valid=%u\n",
            expected_gap,
            initialized && invoked && stream_valid && cursor_valid &&
                    synthetic_client.worr_command_fast_forward_attempts == 1u &&
                    synthetic_client.worr_command_fast_forwards == 1u &&
                    synthetic_client.worr_command_fast_forwarded_commands ==
                        expected_gap &&
                    synthetic_client.worr_command_fast_forward_rejections == 0u &&
                    synthetic_client.worr_command_gap_policy_rejections == 0u
                ? "pass"
                : "fail",
            expected_gap,
            synthetic_client.worr_command_fast_forwarded_commands,
            synthetic_client.worr_command_stream.received_cursor.epoch,
            synthetic_client.worr_command_stream.received_cursor
                .contiguous_sequence,
            synthetic_client.worr_command_stream.consumed_cursor.epoch,
            synthetic_client.worr_command_stream.consumed_cursor
                .contiguous_sequence,
            synthetic_client.worr_command_fast_forward_attempts,
            synthetic_client.worr_command_fast_forwards,
            synthetic_client.worr_command_fast_forwarded_commands,
            synthetic_client.worr_command_fast_forward_rejections,
            synthetic_client.worr_command_gap_policy_rejections,
            stream_valid ? 1u : 0u, cursor_valid ? 1u : 0u);
    }
}

static void SV_WorrReportCommandStreamReject(
    const char *stage,
    const worr_legacy_command_range_v1 *range,
    uint32_t command_count,
    uint32_t already_executed_prefix,
    uint32_t maximum_gap,
    uint32_t simulation_budget,
    uint32_t adapter_result,
    uint32_t stream_result,
    uint32_t failed_index)
{
    const worr_command_stream_v1 *stream =
        &sv_client->worr_command_stream;
    const worr_command_stream_telemetry_v1 *telemetry =
        &stream->telemetry;
    const uint32_t first_epoch = range ? range->first_command_id.epoch : 0;
    const uint32_t first_sequence =
        range ? range->first_command_id.sequence : 0;
    const uint32_t range_count = range ? range->command_count : 0;

    Com_WPrintf(
        "canonical command reject: stage=%s range=%u:%u/%u decoded=%u "
        "prefix=%u allowance=%u simulate=%u failed_index=%u adapter=%u "
        "stream_result=%u initialized=%u stream_count=%u head=%u "
        "received=%u:%u consumed=%u:%u future_gaps=%llu conflicts=%llu "
        "capacity=%llu sample_rejections=%llu invalid_records=%llu "
        "invalid_state=%llu fast_forward=%llu/%llu/%llu/%llu "
        "gap_policy_rejections=%llu\n",
        stage ? stage : "unknown", first_epoch, first_sequence,
        range_count, command_count, already_executed_prefix, maximum_gap,
        simulation_budget, failed_index, adapter_result, stream_result,
        sv_client->worr_command_stream_initialized ? 1u : 0u,
        stream->count, stream->head,
        stream->received_cursor.epoch,
        stream->received_cursor.contiguous_sequence,
        stream->consumed_cursor.epoch,
        stream->consumed_cursor.contiguous_sequence,
        (unsigned long long)telemetry->future_gaps,
        (unsigned long long)telemetry->conflicts,
        (unsigned long long)telemetry->capacity_stalls,
        (unsigned long long)telemetry->sample_time_rejections,
        (unsigned long long)telemetry->invalid_records,
        (unsigned long long)telemetry->invalid_state,
        (unsigned long long)
            sv_client->worr_command_fast_forward_attempts,
        (unsigned long long)
            sv_client->worr_command_fast_forwards,
        (unsigned long long)
            sv_client->worr_command_fast_forwarded_commands,
        (unsigned long long)
            sv_client->worr_command_fast_forward_rejections,
        (unsigned long long)
            sv_client->worr_command_gap_policy_rejections);
}

static bool SV_WorrProcessDecodedCommands(
    const worr_legacy_command_range_v1 *range,
    usercmd_t *commands, uint32_t command_count,
    uint32_t already_executed_prefix,
    uint32_t maximum_gap,
    uint32_t simulation_budget)
{
    worr_prediction_command_v1 canonical[
        WORR_LEGACY_COMMAND_BATCH_MAX_COUNT];
    worr_command_render_watermark_v1 watermark;
    worr_legacy_command_adapter_report_v1 report;
    worr_legacy_command_adapter_result_v1 result;
    uint32_t index;

    if (!range || !commands || command_count == 0 ||
        command_count != range->command_count ||
        command_count > WORR_LEGACY_COMMAND_BATCH_MAX_COUNT) {
        SV_WorrReportCommandStreamReject(
            "arguments", range, command_count, already_executed_prefix,
            maximum_gap, simulation_budget,
            WORR_LEGACY_COMMAND_ADAPTER_INVALID_ARGUMENT,
            WORR_LEGACY_COMMAND_NO_STREAM_RESULT, UINT32_MAX);
        return false;
    }
    if (!SV_WorrCommandStreamInit(
            range, already_executed_prefix, maximum_gap)) {
        SV_WorrReportCommandStreamReject(
            "initialize", range, command_count, already_executed_prefix,
            maximum_gap, simulation_budget,
            WORR_LEGACY_COMMAND_ADAPTER_INVALID_STREAM,
            WORR_LEGACY_COMMAND_NO_STREAM_RESULT, UINT32_MAX);
        return false;
    }
    for (index = 0; index < command_count; ++index) {
        if (!NetUsercmd_ToPredictionCommandV1(
                &commands[index], &canonical[index]) ||
            canonical[index].duration_ms >
            WORR_COMMAND_MAX_NEGOTIATED_DURATION_MS) {
            SV_WorrReportCommandStreamReject(
                "duration", range, command_count,
                already_executed_prefix, maximum_gap,
                simulation_budget,
                WORR_LEGACY_COMMAND_ADAPTER_INVALID_COMMAND,
                WORR_LEGACY_COMMAND_NO_STREAM_RESULT, index);
            return false;
        }
    }
    if (!SV_WorrFillCommandGap(
            range, maximum_gap, simulation_budget)) {
        SV_WorrReportCommandStreamReject(
            "gap", range, command_count, already_executed_prefix,
            maximum_gap, simulation_budget,
            WORR_LEGACY_COMMAND_ADAPTER_FUTURE_GAP,
            WORR_COMMAND_STREAM_FUTURE_GAP, UINT32_MAX);
        return false;
    }
    watermark = SV_WorrCommandWatermark(
        WORR_COMMAND_RENDER_PROVENANCE_LEGACY_PACKET_SHARED);
    result = Worr_LegacyCommandAdapterApplyV1(
        &sv_client->worr_command_stream, range, canonical,
        command_count, WORR_PREDICTION_MODEL_REVISION, &watermark,
        worrCommandRecordScratch,
        WORR_LEGACY_COMMAND_BATCH_MAX_COUNT,
        worrCommandStreamScratch, CMD_BACKUP, &report);
    if (result != WORR_LEGACY_COMMAND_ADAPTER_APPLIED &&
        result != WORR_LEGACY_COMMAND_ADAPTER_IDEMPOTENT) {
        SV_WorrReportCommandStreamReject(
            "adapter", range, command_count, already_executed_prefix,
            maximum_gap, simulation_budget, (uint32_t)result,
            report.stream_result, UINT32_MAX);
        return false;
    }
    for (index = 0; index < command_count; ++index) {
        worr_command_id_v1 id;
        worr_command_record_v1 retained;
        if (!SV_WorrCommandRangeId(range, index, &id)) {
            SV_WorrReportCommandStreamReject(
                "range-id", range, command_count,
                already_executed_prefix, maximum_gap,
                simulation_budget, (uint32_t)result,
                report.stream_result, index);
            return false;
        }
        if (sv_client->worr_native_shadow &&
            Worr_CommandStreamCopyRecordV1(
                &sv_client->worr_command_stream, id, &retained)) {
            /* Observational only: native mismatch/failure can never alter
             * legacy MOVE/BATCH_MOVE simulation authority. */
            (void)SV_NativeShadowObserveLegacyCommandV1(
                sv_client->worr_native_shadow, &retained,
                svs.realtime);
        }
        if (!SV_WorrConsumeCommand(id, &commands[index], true)) {
            SV_WorrReportCommandStreamReject(
                "consume", range, command_count,
                already_executed_prefix, maximum_gap,
                simulation_budget, (uint32_t)result,
                report.stream_result, index);
            return false;
        }
    }
    return true;
}

/*
==================
SV_OldClientExecuteMove
==================
*/
static void SV_OldClientExecuteMove(const q2proto_clc_move_t *move)
{
    usercmd_t   oldest, oldcmd, newcmd;
    int         net_drop;

    if (moveIssued) {
        SV_DropClient(sv_client, "multiple clc_move commands in packet");
        return;     // someone is trying to cheat...
    }

    moveIssued = true;

    const bool has_upmove = svs.game_api != Q2PROTO_GAME_RERELEASE;
    if (!NetUsercmd_ApplyDelta(&move->moves[0], NULL, &oldest,
                               has_upmove) ||
        !NetUsercmd_ApplyDelta(&move->moves[1], &oldest, &oldcmd,
                               has_upmove) ||
        !NetUsercmd_ApplyDelta(&move->moves[2], &oldcmd, &newcmd,
                               has_upmove)) {
        SV_DropClient(sv_client, "invalid user command delta");
        return;
    }

    if (sv_client->state != cs_spawned) {
        SV_SetLastFrame(-1);
        return;
    }

    SV_SetLastFrame(move->lastframe);

    net_drop = sv_client->netchan.dropped;
    if (net_drop > 2) {
        sv_client->frameflags |= FF_CLIENTPRED;
    }

    if (SV_WorrCommandSidebandActive() && packetCommandRangeValid) {
        usercmd_t commands[WORR_LEGACY_COMMAND_MOVE_COUNT];
        const uint32_t missing_packets =
            net_drop > 2 ? (uint32_t)(net_drop - 2) : 0u;
        const uint32_t maximum_gap =
            min(missing_packets,
                (uint32_t)WORR_CANONICAL_COMMAND_MAX_TRANSPORT_GAP);
        const uint32_t simulation_budget =
            net_drop < 20 ? maximum_gap : 0u;
        const uint32_t already_executed_prefix =
            2u - (uint32_t)min(net_drop, 2);
        commands[0] = oldest;
        commands[1] = oldcmd;
        commands[2] = newcmd;
        if (!SV_WorrProcessDecodedCommands(
                &packetCommandRange, commands,
                WORR_LEGACY_COMMAND_MOVE_COUNT,
                already_executed_prefix, maximum_gap,
                simulation_budget)) {
            SV_DropClient(sv_client,
                          "invalid canonical command stream");
            return;
        }
        sv_client->lastcmd = newcmd;
        return;
    }

    if (net_drop < 20) {
        // run lastcmd multiple times if no backups available
        while (net_drop > 2) {
            SV_WorrLegacyCommandThink(&sv_client->lastcmd);
            net_drop--;
        }

        // run backup cmds
        if (net_drop > 1)
            SV_WorrLegacyCommandThink(&oldest);
        if (net_drop > 0)
            SV_WorrLegacyCommandThink(&oldcmd);
    }

    // run new cmd
    SV_WorrLegacyCommandThink(&newcmd);

    sv_client->lastcmd = newcmd;
}

/*
==================
SV_NewClientExecuteMove
==================
*/
static void SV_NewClientExecuteMove(const q2proto_clc_batch_move_t *batch_move)
{
    usercmd_t   cmds[MAX_PACKET_FRAMES][MAX_PACKET_USERCMDS];
    usercmd_t   flattened[WORR_LEGACY_COMMAND_BATCH_MAX_COUNT];
    usercmd_t   *lastcmd, *cmd;
    int         lastframe;
    int         numCmds[MAX_PACKET_FRAMES], numDups;
    int         i, j;
    int         net_drop;
    uint32_t    flattened_count = 0;

    if (moveIssued) {
        SV_DropClient(sv_client, "multiple clc_move commands in packet");
        return;     // someone is trying to cheat...
    }

    moveIssued = true;

    numDups = batch_move->num_dups;

    if (numDups >= MAX_PACKET_FRAMES) {
        SV_DropClient(sv_client, "too many frames in packet");
        return;
    }

    lastframe = batch_move->lastframe;

    // read all cmds
    lastcmd = NULL;
    for (i = 0; i <= numDups; i++) {
        const q2proto_clc_batch_move_frame_t *move_frame = &batch_move->batch_frames[i];
        numCmds[i] = move_frame->num_cmds;
        if (numCmds[i] >= MAX_PACKET_USERCMDS) {
            SV_DropClient(sv_client, "too many usercmds in frame");
            return;
        }
        for (j = 0; j < numCmds[i]; j++) {
            cmd = &cmds[i][j];
            if (!NetUsercmd_ApplyDelta(
                    &move_frame->moves[j], lastcmd, cmd,
                    svs.game_api != Q2PROTO_GAME_RERELEASE)) {
                SV_DropClient(sv_client, "invalid batched user command delta");
                return;
            }
            lastcmd = cmd;
            if (flattened_count >=
                WORR_LEGACY_COMMAND_BATCH_MAX_COUNT) {
                SV_DropClient(sv_client,
                              "too many canonical user commands");
                return;
            }
            flattened[flattened_count++] = *cmd;
        }
    }

    if (sv_client->state != cs_spawned) {
        SV_SetLastFrame(-1);
        return;
    }

    SV_SetLastFrame(lastframe);

    if (q_unlikely(!lastcmd)) {
        return; // should never happen
    }

    net_drop = sv_client->netchan.dropped;
    if (net_drop > numDups) {
        sv_client->frameflags |= FF_CLIENTPRED;
    }

    if (SV_WorrCommandSidebandActive() && packetCommandRangeValid) {
        uint32_t already_executed_prefix = 0;
        uint32_t maximum_gap;
        uint32_t simulation_budget;
        const uint32_t missing_packets =
            net_drop > numDups
                ? (uint32_t)(net_drop - numDups)
                : 0u;
        const uint64_t maximum_gap_wide =
            (uint64_t)missing_packets *
            (MAX_PACKET_USERCMDS - 1u);
        maximum_gap = (uint32_t)min(
            maximum_gap_wide,
            (uint64_t)WORR_CANONICAL_COMMAND_MAX_TRANSPORT_GAP);
        simulation_budget =
            net_drop < 20 ? min(missing_packets, maximum_gap) : 0u;
        if (net_drop <= numDups) {
            const int first_replayed_frame = numDups - net_drop;
            for (i = 0; i < first_replayed_frame; ++i)
                already_executed_prefix += (uint32_t)numCmds[i];
        }
        if (!SV_WorrProcessDecodedCommands(
                &packetCommandRange, flattened, flattened_count,
                already_executed_prefix, maximum_gap,
                simulation_budget)) {
            SV_DropClient(sv_client,
                          "invalid canonical command stream");
            return;
        }
        sv_client->lastcmd = *lastcmd;
        return;
    }

    if (net_drop < 20) {
        // run lastcmd multiple times if no backups available
        while (net_drop > numDups) {
            SV_WorrLegacyCommandThink(&sv_client->lastcmd);
            net_drop--;
        }

        // run backup cmds, if any
        while (net_drop > 0) {
            i = numDups - net_drop;
            for (j = 0; j < numCmds[i]; j++) {
                SV_WorrLegacyCommandThink(&cmds[i][j]);
            }
            net_drop--;
        }

    }

    // run new cmds
    for (j = 0; j < numCmds[numDups]; j++) {
        SV_WorrLegacyCommandThink(&cmds[numDups][j]);
    }

    sv_client->lastcmd = *lastcmd;
}

/*
=================
SV_CheckInfoBans

Returns matched kickable ban or NULL
=================
*/
cvarban_t *SV_CheckInfoBans(const char *info, bool match_only)
{
    char key[MAX_INFO_STRING];
    char value[MAX_INFO_STRING];
    cvarban_t *ban;

    if (LIST_EMPTY(&sv_infobanlist))
        return NULL;

    while (1) {
        Info_NextPair(&info, key, value);
        if (!info)
            return NULL;

        LIST_FOR_EACH(cvarban_t, ban, &sv_infobanlist, entry) {
            if (match_only && ban->action != FA_KICK)
                continue;
            if (Q_stricmp(ban->var, key))
                continue;
            if (match_only) {
                if (match_cvar_ban(ban, value))
                    return ban;
            } else {
                if (handle_cvar_ban(ban, value))
                    return ban;
            }
        }
    }
}

/*
=================
SV_UpdateUserinfo

Ensures that userinfo is valid and name is properly set.
=================
*/
static void SV_UpdateUserinfo(void)
{
    char *s;

    if (!sv_client->userinfo[0]) {
        SV_DropClient(sv_client, "empty userinfo");
        return;
    }

    if (!Info_Validate(sv_client->userinfo)) {
        SV_DropClient(sv_client, "malformed userinfo");
        return;
    }

    // validate name
    s = Info_ValueForKey(sv_client->userinfo, "name");
    s[MAX_CLIENT_NAME - 1] = 0;
    if (COM_IsWhite(s) || (sv_client->name[0] && strcmp(sv_client->name, s) &&
                           SV_RateLimited(&sv_client->ratelimit_namechange))) {
        if (!sv_client->name[0]) {
            SV_DropClient(sv_client, "malformed name");
            return;
        }
        if (!Info_SetValueForKey(sv_client->userinfo, "name", sv_client->name)) {
            SV_DropClient(sv_client, "oversize userinfo");
            return;
        }
        if (COM_IsWhite(s))
            SV_ClientPrintf(sv_client, PRINT_HIGH, "You can't have an empty name.\n");
        else
            SV_ClientPrintf(sv_client, PRINT_HIGH, "You can't change your name too often.\n");
        SV_ClientCommand(sv_client, "set name \"%s\"\n", sv_client->name);
    }

    if (SV_CheckInfoBans(sv_client->userinfo, false))
        return;

    SV_UserinfoChanged(sv_client);
}

static void SV_ParseFullUserinfo(const q2proto_clc_userinfo_t *userinfo)
{
    // malicious users may try sending too many userinfo updates
    if (userinfoUpdateCount >= MAX_PACKET_USERINFOS) {
        Com_DPrintf("Too many userinfos from %s\n", sv_client->name);
        MSG_ReadString(NULL, 0);
        return;
    }

    if (userinfo->str.len >= sizeof(sv_client->userinfo)) {
        SV_DropClient(sv_client, "oversize userinfo");
        return;
    }
    q2pslcpy(sv_client->userinfo, sizeof(sv_client->userinfo), &userinfo->str);

    Com_DDPrintf("%s(%s): %s [%d]\n", __func__,
                 sv_client->name, Com_MakePrintable(sv_client->userinfo), userinfoUpdateCount);

    SV_UpdateUserinfo();
    userinfoUpdateCount++;
}

static void SV_ParseDeltaUserinfo(const q2proto_clc_userinfo_delta_t *userinfo_delta)
{
    char key[MAX_INFO_KEY], value[MAX_INFO_VALUE];

    // malicious users may try sending too many userinfo updates
    if (userinfoUpdateCount >= MAX_PACKET_USERINFOS) {
        Com_DPrintf("Too many userinfos from %s\n", sv_client->name);
        return;
    }

    if (q2pslcpy(key, sizeof(key), &userinfo_delta->name) >= sizeof(key)) {
        SV_DropClient(sv_client, "oversize userinfo key");
        return;
    }

    if (q2pslcpy(value, sizeof(value), &userinfo_delta->value) >= sizeof(value)) {
        SV_DropClient(sv_client, "oversize userinfo value");
        return;
    }

    if (userinfoUpdateCount < MAX_PACKET_USERINFOS) {
        if (!Info_SetValueForKey(sv_client->userinfo, key, value)) {
            SV_DropClient(sv_client, "malformed userinfo");
            return;
        }

        Com_DDPrintf("%s(%s): %s %s [%d]\n", __func__,
                        sv_client->name, key, value, userinfoUpdateCount);

        userinfoUpdateCount++;
    } else {
        Com_DPrintf("Too many userinfos from %s\n", sv_client->name);
    }
}

#if USE_FPS
// key frames must be aligned for all clients (and game) to ensure there isn't
// additional frame of latency for clients with framediv > 1.
void SV_AlignKeyFrames(client_t *client)
{
    int framediv = sv.frametime.div / client->framediv;
    int framenum = (sv.framenum + client->framediv - 1) / client->framediv;
    int frameofs = framenum % framediv;
    int newnum = frameofs + Q_align_up(client->framenum, framediv);

    Com_DDPrintf("[%d] align %d --> %d (num = %d, div = %d, ofs = %d)\n",
                 sv.framenum, client->framenum, newnum, framenum, framediv, frameofs);
    client->framenum = newnum;
}

static void set_client_fps(int value)
{
    int framediv, framerate;

    // 0 means highest
    if (!value)
        value = sv.framerate;

    framediv = Q_clip(value / BASE_FRAMERATE, 1, MAX_FRAMEDIV);
    framediv = sv.frametime.div / Q_gcd(sv.frametime.div, framediv);
    framerate = sv.framerate / framediv;

    Com_DDPrintf("[%d] client div=%d, server div=%d, rate=%d\n",
                 sv.framenum, framediv, sv.frametime.div, framerate);

    sv_client->framediv = framediv;

    SV_AlignKeyFrames(sv_client);

    // save for status inspection
    sv_client->settings[CLS_FPS] = framerate;

    MSG_WriteByte(sv_client->protocol == PROTOCOL_VERSION_RERELEASE ? svc_rr_setting : svc_q2pro_setting);
    MSG_WriteLong(SVS_FPS);
    MSG_WriteLong(framerate);
    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
}
#endif

static void SV_ParseClientSetting(const q2proto_clc_setting_t *setting)
{
    int idx, value;

    idx = setting->index;
    value = setting->value;

    Com_DDPrintf("%s(%s): [%d] = %d\n", __func__, sv_client->name, idx, value);

    if (idx < 0 || idx >= CLS_MAX)
        return;

    sv_client->settings[idx] = value;

#if USE_FPS
    if (idx == CLS_FPS && sv_client->protocol == PROTOCOL_VERSION_Q2PRO)
        set_client_fps(value);
#endif
}

static void SV_ParseClientCommand(const q2proto_clc_stringcmd_t *stringcmd)
{
    char buffer[MAX_STRING_CHARS];

    if (stringcmd->cmd.len >= sizeof(buffer)) {
        SV_DropClient(sv_client, "oversize stringcmd");
        return;
    }
    q2pslcpy(buffer, sizeof(buffer), &stringcmd->cmd);

    // malicious users may try using too many string commands
    if (stringCmdCount >= MAX_PACKET_STRINGCMDS) {
        Com_DPrintf("Too many stringcmds from %s\n", sv_client->name);
        return;
    }

    Com_DDPrintf("%s(%s): %s\n", __func__, sv_client->name, Com_MakePrintable(buffer));

    SV_ExecuteUserCommand(buffer);
    stringCmdCount++;
}

static uint32_t SV_WorrDecodedCommandCount(
    const q2proto_clc_message_t *message)
{
    uint32_t count = 0;
    int i;
    if (message->type == Q2P_CLC_MOVE)
        return WORR_LEGACY_COMMAND_MOVE_COUNT;
    if (message->type != Q2P_CLC_BATCH_MOVE ||
        message->batch_move.num_dups < 0 ||
        message->batch_move.num_dups >= MAX_PACKET_FRAMES) {
        return 0;
    }
    for (i = 0; i <= message->batch_move.num_dups; ++i) {
        const int commands =
            message->batch_move.batch_frames[i].num_cmds;
        if (commands < 0 || commands >= MAX_PACKET_USERCMDS ||
            count > WORR_LEGACY_COMMAND_BATCH_MAX_COUNT -
                        (uint32_t)commands) {
            return 0;
        }
        count += (uint32_t)commands;
    }
    return count;
}

static bool SV_WorrPrepareCommandMove(
    const q2proto_clc_message_t *message)
{
    worr_legacy_command_sideband_result_v1 result;
    uint32_t carrier;
    const uint32_t count = SV_WorrDecodedCommandCount(message);

    packetCommandRangeValid = false;
    if (!SV_WorrCommandSidebandActive())
        return true;
    if (message->type == Q2P_CLC_BATCH_MOVE && count == 0) {
        result = Worr_LegacyCommandSidebandObserveInterveningServiceV1(
            &sv_client->worr_command_parser);
        if (result == WORR_LEGACY_COMMAND_SIDEBAND_NOT_SIDEBAND)
            return true;
        SV_DropClient(sv_client,
                      "command sideband header has an empty carrier");
        return false;
    }
    carrier = message->type == Q2P_CLC_MOVE
                  ? WORR_LEGACY_COMMAND_CARRIER_MOVE
                  : WORR_LEGACY_COMMAND_CARRIER_BATCH_MOVE;
    result = Worr_LegacyCommandSidebandConsumeMoveV1(
        &sv_client->worr_command_parser, carrier, count,
        &packetCommandRange);
    if (result == WORR_LEGACY_COMMAND_SIDEBAND_MOVE_MATCHED) {
        packetCommandRangeValid = true;
        sv_client->worr_command_sideband_started = true;
        return true;
    }
    if (result == WORR_LEGACY_COMMAND_SIDEBAND_MISSING_HEADER &&
        !sv_client->worr_command_sideband_started &&
        ++sv_client->worr_command_bootstrap_moves <= 8u) {
        return true;
    }
    SV_DropClient(sv_client, "malformed command identity sideband");
    return false;
}

/*
===================
SV_ExecuteClientMessage

The current net_message is parsed for the given client
===================
*/
void SV_ExecuteClientMessage(client_t *client)
{
    bool command_sideband_packet = false;
    bool readiness_sideband_packet = false;
    sv_client = client;
    sv_player = sv_client->edict;

    if (client->worr_native_shadow) {
        /* Netchan has already admitted and stripped any WTC1 trailer.  Check
         * the authoritative retained stream before parsing this packet so a
         * native retry can join a MOVE consumed on an earlier packet. */
        (void)SV_NativeShadowReconcileCommandStreamV1(
            client->worr_native_shadow,
            client->worr_command_stream_initialized
                ? &client->worr_command_stream : NULL,
            svs.realtime);
    }

    // only allow one move command
    moveIssued = false;
    stringCmdCount = 0;
    userinfoUpdateCount = 0;
    int prevUserinfoUpdateCount = 0;
    packetCommandRangeValid = false;
    memset(&packetCommandRange, 0, sizeof(packetCommandRange));
    if (SV_WorrCommandSidebandActive()) {
        if (Worr_LegacyCommandSidebandPacketBeginV1(
                &client->worr_command_parser) !=
            WORR_LEGACY_COMMAND_SIDEBAND_PACKET_STARTED) {
            SV_DropClient(client,
                          "invalid command sideband packet state");
            goto finish;
        }
        command_sideband_packet = true;
    }
    if (client->worr_native_shadow &&
        SV_NativeShadowPacketBeginV1(
            client->worr_native_shadow, svs.realtime)) {
        readiness_sideband_packet = true;
    }

    while (1) {
        if (msg_read.readcount > msg_read.cursize) {
            SV_DropClient(client, "read past end of message");
            break;
        }

        q2proto_clc_message_t message;
        q2proto_error_t err = q2proto_server_read(&client->q2proto_ctx, Q2PROTO_IOARG_SERVER_READ, &message);
        if (err == Q2P_ERR_NO_MORE_INPUT)
            break;
        if (err != Q2P_ERR_SUCCESS) {
            Com_DPrintf("Malformed client message from %s: %s\n",
                        client->name, q2proto_error_string(err));
            SV_DropClient(client, "malformed client message");
            break;
        }

        bool sideband_setting = false;
        if (readiness_sideband_packet && client->worr_native_shadow) {
            if (message.type == Q2P_CLC_SETTING) {
                const bool readiness_setting =
                    SV_NativeShadowSettingIndexV1(
                        message.setting.index);
                sv_native_shadow_observe_result_v1 result;
                worr_native_readiness_record_v1 server_active;

                /* The parser-shared helper classifies every complete record
                 * before reserving SERVER_ACTIVE capacity.  Thus a full
                 * reliable queue cannot turn a valid canceled CLIENT_READY
                 * into a private readiness failure, while a current response
                 * still fails closed before native wire commitment. */
                result = SV_NativeShadowObserveSettingWithResponseCapacityV1(
                    client->worr_native_shadow,
                    message.setting.index, message.setting.value,
                    &msg_write, &client->netchan.message, &server_active);
                if (readiness_setting)
                    sideband_setting = true;
                if (result ==
                    SV_NATIVE_SHADOW_OBSERVE_SERVER_ACTIVE_READY) {
                    if (SV_AppendNativeReadinessRecord(&server_active)) {
                        SV_ClientAddMessage(
                            client, MSG_RELIABLE | MSG_CLEAR);
                        if (!SV_NativeShadowServerActiveQueuedV1(
                                client->worr_native_shadow)) {
                            result =
                                SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
                        }
                    } else {
                        SV_NativeShadowPeerDisableV1(
                            client->worr_native_shadow,
                            SV_NATIVE_SHADOW_FAILURE_QUEUE);
                        result =
                            SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED;
                    }
                }
                if (result ==
                    SV_NATIVE_SHADOW_OBSERVE_PILOT_DISABLED) {
                    readiness_sideband_packet = false;
                }
            } else if (!SV_NativeShadowObserveInterveningServiceV1(
                           client->worr_native_shadow)) {
                /* A private readiness failure is a silent fallback while all
                 * legacy services in this packet continue normally. */
                readiness_sideband_packet = false;
            }
        }
        if (command_sideband_packet) {
            if (message.type == Q2P_CLC_SETTING) {
                const worr_legacy_command_sideband_result_v1 result =
                    Worr_LegacyCommandSidebandObserveSettingV1(
                        &client->worr_command_parser,
                        message.setting.index, message.setting.value);
                if (result ==
                        WORR_LEGACY_COMMAND_SIDEBAND_FIELD_ACCEPTED ||
                    result ==
                        WORR_LEGACY_COMMAND_SIDEBAND_HEADER_COMMITTED) {
                    sideband_setting = true;
                } else if (result !=
                           WORR_LEGACY_COMMAND_SIDEBAND_NOT_SIDEBAND) {
                    SV_DropClient(client,
                                  "malformed command sideband header");
                    break;
                }
            } else if (message.type != Q2P_CLC_MOVE &&
                       message.type != Q2P_CLC_BATCH_MOVE) {
                const worr_legacy_command_sideband_result_v1 result =
                    Worr_LegacyCommandSidebandObserveInterveningServiceV1(
                        &client->worr_command_parser);
                if (result ==
                    WORR_LEGACY_COMMAND_SIDEBAND_RESET_INTERVENING_SERVICE) {
                    SV_DropClient(client,
                                  "interleaved command sideband header");
                    break;
                }
            }
        }

        // Handle batched userinfo deltas
        if (message.type != Q2P_CLC_USERINFO_DELTA && prevUserinfoUpdateCount != userinfoUpdateCount) {
            SV_UpdateUserinfo();
            prevUserinfoUpdateCount = userinfoUpdateCount;
        }

        switch(message.type)
        {
        default:
            SV_DropClient(client, "unknown message type");
            break;

        case Q2P_CLC_NOP:
            break;

        case Q2P_CLC_USERINFO:
            SV_ParseFullUserinfo(&message.userinfo);
            break;

        case Q2P_CLC_MOVE:
            if (SV_WorrPrepareCommandMove(&message))
                SV_OldClientExecuteMove(&message.move);
            break;

        case Q2P_CLC_BATCH_MOVE:
            if (SV_WorrPrepareCommandMove(&message))
                SV_NewClientExecuteMove(&message.batch_move);
            break;

        case Q2P_CLC_STRINGCMD:
            SV_ParseClientCommand(&message.stringcmd);
            break;

        case Q2P_CLC_SETTING:
            if (!sideband_setting)
                SV_ParseClientSetting(&message.setting);
            break;

        case Q2P_CLC_USERINFO_DELTA:
            SV_ParseDeltaUserinfo(&message.userinfo_delta);
            break;

        }

        if (readiness_sideband_packet &&
            (!client->worr_native_shadow ||
             !SV_NativeShadowPeerEnabledV1(
                 client->worr_native_shadow))) {
            readiness_sideband_packet = false;
        }

        if (client->state <= cs_zombie)
            break;    // disconnect command
    }

finish:
    if (readiness_sideband_packet && client->worr_native_shadow)
        (void)SV_NativeShadowPacketEndV1(
            client->worr_native_shadow);
    if (command_sideband_packet &&
        Worr_LegacyCommandSidebandPacketEndV1(
            &client->worr_command_parser) !=
            WORR_LEGACY_COMMAND_SIDEBAND_PACKET_ENDED &&
        client->state > cs_zombie) {
        SV_DropClient(client, "dangling command sideband header");
    }

    // Handle batched userinfo deltas
    if (prevUserinfoUpdateCount != userinfoUpdateCount)
        SV_UpdateUserinfo();

    sv_client = NULL;
    sv_player = NULL;
}
