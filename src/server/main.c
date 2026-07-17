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

#include "server.h"
#include "server/native_shadow.h"
#include "server/snapshot_shadow.h"
#include "client/input.h"
#if USE_CLIENT
#include "renderer/renderer.h"
#endif
#include "server/nav.h"
#include "q2proto/q2proto.h"

master_t    sv_masters[MAX_MASTERS];   // address of group servers

LIST_DECL(sv_banlist);
LIST_DECL(sv_blacklist);
LIST_DECL(sv_cmdlist_connect);
LIST_DECL(sv_cmdlist_begin);
LIST_DECL(sv_lrconlist);
LIST_DECL(sv_filterlist);
LIST_DECL(sv_cvarbanlist);
LIST_DECL(sv_infobanlist);
LIST_DECL(sv_clientlist);   // linked list of non-free clients

client_t    *sv_client;         // current client
edict_t     *sv_player;         // current client edict

cvar_t  *sv_enforcetime;
cvar_t  *sv_timescale_time;
cvar_t  *sv_timescale_warn;
cvar_t  *sv_timescale_kick;
cvar_t  *sv_allow_nodelta;
cvar_t  *sv_fps;

cvar_t  *sv_timeout;            // seconds without any message
cvar_t  *sv_zombietime;         // seconds to sink messages after disconnect
cvar_t  *sv_ghostime;
cvar_t  *sv_idlekick;

cvar_t  *sv_password;
cvar_t  *sv_reserved_password;

cvar_t  *sv_force_reconnect;
cvar_t  *sv_show_name_changes;

cvar_t  *sv_airaccelerate;
cvar_t  *sv_qwmod;              // atu QW Physics modificator
cvar_t  *sv_novis;
cvar_t  *sv_shadow_strict_replication;
static cvar_t *sv_worr_native_shadow;
static cvar_t *sv_worr_native_event_shadow;
static cvar_t *sv_worr_native_snapshot_shadow;

cvar_t  *sv_maxclients;
cvar_t  *sv_reserved_slots;
cvar_t  *sv_locked;
cvar_t  *sv_downloadserver;
cvar_t  *sv_redirect_address;

cvar_t  *sv_hostname;
cvar_t  *sv_public;            // should heartbeats be sent

#if USE_DEBUG
cvar_t  *sv_debug;
cvar_t  *sv_pad_packets;
#endif
cvar_t  *sv_lan_force_rate;
cvar_t  *sv_min_rate;
cvar_t  *sv_max_rate;
cvar_t  *sv_calcpings_method;
cvar_t  *sv_changemapcmd;
cvar_t  *sv_max_download_size;
cvar_t  *sv_max_packet_entities;
cvar_t  *sv_trunc_packet_entities;
cvar_t  *sv_prioritize_entities;

cvar_t  *sv_strafejump_hack;
cvar_t  *sv_waterjump_hack;
#if USE_PACKETDUP
cvar_t  *sv_packetdup_hack;
#endif
cvar_t  *sv_allow_map;
cvar_t  *sv_cinematics;
#if USE_SERVER
cvar_t  *sv_recycle;
#endif
cvar_t  *sv_enhanced_setplayer;
static cvar_t  *bot_slot_smoke;
static cvar_t  *bot_min_players_smoke;
static cvar_t  *bot_profile_smoke;
static cvar_t  *bot_profile_smoke_target;
static cvar_t  *bot_team_policy_smoke;
static cvar_t  *bot_warmup_smoke;
static cvar_t  *bot_vote_smoke;
static cvar_t  *bot_admin_audit_smoke;
static cvar_t  *bot_tournament_smoke;
static cvar_t  *bot_mapvote_smoke;
static cvar_t  *bot_mymap_smoke;
static cvar_t  *bot_intermission_smoke;
static cvar_t  *bot_nextmap_smoke;
static cvar_t  *bot_scoreboard_smoke;
static cvar_t  *bot_matchlog_smoke;
static cvar_t  *bot_frame_command_smoke;
static cvar_t  *bot_frame_command_smoke_soak_ms;
static cvar_t  *bot_frame_command_smoke_map_repeat_cycles;
static cvar_t  *bot_frame_command_smoke_map_repeat_reload_timeout_ms;
static cvar_t  *bot_frame_command_smoke_map_repeat_restart;
static cvar_t  *bot_enable;
static cvar_t  *bot_min_players;
static cvar_t  *bot_profile;

cvar_t  *sv_iplimit;
cvar_t  *sv_status_limit;
cvar_t  *sv_status_show;
cvar_t  *sv_uptime;
cvar_t  *sv_auth_limit;
cvar_t  *sv_rcon_limit;
cvar_t  *sv_namechange_limit;

cvar_t  *sv_allow_unconnected_cmds;

cvar_t  *sv_lrcon_password;

// KEX
cvar_t  *sv_tick_rate;
// KEX

cvar_t  *g_features;

static bool     sv_registered;

static const q2proto_protocol_t q2repro_accepted_protocols[] = {Q2P_PROTOCOL_Q2REPRO};

//============================================================================

static void SV_DestroyNativeShadow(client_t *client)
{
    if (!client || !client->worr_native_shadow)
        return;
    SV_NativeShadowPeerDestroyV1(client->worr_native_shadow);
    Z_Free(client->worr_native_shadow);
    client->worr_native_shadow = NULL;
}

void SV_RemoveClient(client_t *client)
{
    if (client->msg_pool) {
        SV_ShutdownClientSend(client);
    }

    /* Netchan_Close has no hook finalizer; destroy the stable opaque owner
     * before the channel storage can be cleared or the slot reused. */
    SV_DestroyNativeShadow(client);
    Netchan_Close(&client->netchan);

    // unlink them from active client list, but don't clear the list entry
    // itself to make code that traverses client list in a loop happy!
    List_Remove(&client->entry);

#if USE_MVD_CLIENT
    // unlink them from MVD client list
    if (sv.state == ss_broadcast) {
        MVD_RemoveClient(client);
    }
#endif

    Com_DPrintf("Going from cs_zombie to cs_free for %s\n", client->name);

    client->state = cs_free;    // can now be reused
    client->name[0] = 0;
}

void SV_CleanClient(client_t *client)
{
    int i;
#if USE_AC_SERVER
    string_entry_t *bad, *next;

    for (bad = client->ac_bad_files; bad; bad = next) {
        next = bad->next;
        Z_Free(bad);
    }
    client->ac_bad_files = NULL;
#endif

    SV_DestroyNativeShadow(client);

    // close any existing download
    SV_CloseDownload(client);

    Z_Freep(&client->version_string);

    // free baselines allocated for this client
    for (i = 0; i < SV_BASELINES_CHUNKS; i++) {
        Z_Freep(&client->baselines[i]);
    }

    // free packet entities
    Z_Freep(&client->entities);
    client->num_entities = 0;

    SV_SnapshotShadowDestroyV1(client->worr_snapshot_shadow);
    client->worr_snapshot_shadow = NULL;
}

static void print_drop_reason(client_t *client, const char *reason, clstate_t oldstate)
{
    int announce = oldstate == cs_spawned ? 2 : 1;
    const char *prefix = " was dropped: ";

    // parse flags
    if (*reason == '!') {
        reason++;
        announce = 0;
    }
    if (*reason == '?') {
        reason++;
        prefix = " ";
    }

    if (announce == 2) {
        // announce to others
#if USE_MVD_CLIENT
        if (sv.state == ss_broadcast)
            MVD_GameClientDrop(client->edict, prefix, reason);
        else
#endif
            SV_BroadcastPrintf(PRINT_HIGH, "%s%s%s\n",
                               client->name, prefix, reason);
    }

    if (announce && !client->bot)
        // print this to client as they will not receive broadcast
        SV_ClientPrintf(client, PRINT_HIGH, "%s%s%s\n",
                        client->name, prefix, reason);

    // print to server console
    if (COM_DEDICATED)
        Com_Printf("%s[%s]%s%s\n", client->name,
                   NET_AdrToString(&client->netchan.remote_address),
                   prefix, reason);
}

/*
=====================
SV_DropClient

Called when the player is totally leaving the server, either willingly
or unwillingly.  This is NOT called if the entire server is quitting
or crashing.
=====================
*/
void SV_DropClient(client_t *client, const char *reason)
{
    clstate_t oldstate;

    if (client->state <= cs_zombie)
        return; // called recursively?

    oldstate = client->state;
    client->state = cs_zombie;        // become free in a few seconds
    client->lastmessage = svs.realtime;

    /* Final legacy disconnect traffic must never pass through an opaque owner
     * that is about to be freed. */
    if (client->worr_native_shadow)
        SV_NativeShadowPeerDetachV1(client->worr_native_shadow);

    // print the reason
    if (reason)
        print_drop_reason(client, reason, oldstate);

    if (!client->bot) {
        // add the disconnect
        q2proto_svc_message_t message = {.type = Q2P_SVC_DISCONNECT};
        q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);
        SV_ClientAddMessage(client, MSG_RELIABLE | MSG_CLEAR);
    }

    if (oldstate == cs_spawned || (g_features->integer & GMF_WANT_ALL_DISCONNECTS)) {
        // call the prog function for removing a client
        // this will remove the body, among other things
        ge->ClientDisconnect(client->edict);
    }

    if (!client->bot) {
        AC_ClientDisconnect(client);
    }

    SV_CleanClient(client);

    Com_DPrintf("Going to cs_zombie for %s\n", client->name);

    // give MVD server a chance to detect if its dummy client was dropped
    SV_MvdClientDropped(client);
}


//============================================================================

// highest power of two that avoids credit overflow for 1 day
#define CREDITS_PER_MSEC    32
#define CREDITS_PER_SEC     (CREDITS_PER_MSEC * 1000)

// allows rates up to 10,000 hits per second
#define RATE_LIMIT_SCALE    10000

/*
===============
SV_RateLimited

Implements simple token bucket filter. Inspired by xt_limit.c from the Linux
kernel. Returns true if limit is exceeded.
===============
*/
bool SV_RateLimited(ratelimit_t *r)
{
    r->credit += (svs.realtime - r->time) * CREDITS_PER_MSEC;
    r->time = svs.realtime;
    if (r->credit > r->credit_cap)
        r->credit = r->credit_cap;

    if (r->credit >= r->cost) {
        r->credit -= r->cost;
        return false;
    }

    return true;
}

/*
===============
SV_RateRecharge

Reverts the effect of SV_RateLimited.
===============
*/
void SV_RateRecharge(ratelimit_t *r)
{
    r->credit += r->cost;
    if (r->credit > r->credit_cap)
        r->credit = r->credit_cap;
}

static unsigned rate2credits(unsigned rate)
{
    if (rate > UINT_MAX / CREDITS_PER_SEC)
        return (rate / RATE_LIMIT_SCALE) * CREDITS_PER_SEC;

    return (rate * CREDITS_PER_SEC) / RATE_LIMIT_SCALE;
}

/*
===============
SV_RateInit

Full syntax is: <limit>[/<period>[sec|min|hour]][*<burst>]
===============
*/
void SV_RateInit(ratelimit_t *r, const char *s)
{
    unsigned limit, period, mult, burst, rate;
    char *p;

    limit = strtoul(s, &p, 10);
    if (*p == '/') {
        period = strtoul(p + 1, &p, 10);
        if (*p == 's' || *p == 'S') {
            mult = 1;
            p++;
        } else if (*p == 'm' || *p == 'M') {
            mult = 60;
            p++;
        } else if (*p == 'h' || *p == 'H') {
            mult = 60 * 60;
            p++;
        } else {
            // everything else are seconds
            mult = 1;
        }
        if (!period)
            period = 1;
    } else {
        // default period is one second
        period = 1;
        mult = 1;
    }

    if (!limit) {
        // unlimited
        memset(r, 0, sizeof(*r));
        return;
    }

    if (period > UINT_MAX / (RATE_LIMIT_SCALE * mult)) {
        Com_Printf("Period too large: %u\n", period);
        return;
    }

    rate = (RATE_LIMIT_SCALE * period * mult) / limit;
    if (!rate) {
        Com_Printf("Limit too large: %u\n", limit);
        return;
    }

    p = strchr(p, '*');
    if (p) {
        burst = strtoul(p + 1, NULL, 10);
    } else {
        // default burst is 5 hits
        burst = 5;
    }

    if (burst > UINT_MAX / rate) {
        Com_Printf("Burst too large: %u\n", burst);
        return;
    }

    r->time = svs.realtime;
    r->credit = rate2credits(rate * burst);
    r->credit_cap = rate2credits(rate * burst);
    r->cost = rate2credits(rate);
}

addrmatch_t *SV_MatchAddress(const list_t *list, const netadr_t *addr)
{
    addrmatch_t *match;

    LIST_FOR_EACH(addrmatch_t, match, list, entry) {
        if (NET_IsEqualBaseAdrMask(addr, &match->addr, &match->mask)) {
            match->hits++;
            match->time = time(NULL);
            return match;
        }
    }

    return NULL;
}

/*
==============================================================================

CONNECTIONLESS COMMANDS

==============================================================================
*/

/*
===============
SV_StatusString

Builds the string that is sent as heartbeats and status replies.
It is assumed that size of status buffer is at least SV_OUTPUTBUF_LENGTH!
===============
*/
static size_t SV_StatusString(char *status)
{
    char entry[MAX_STRING_CHARS];
    client_t *cl;
    size_t total, len;
    char *tmp = sv_maxclients->string;

    // XXX: ugly hack to hide reserved slots
    if (svs.maxclients_soft != svs.maxclients) {
        Q_snprintf(entry, sizeof(entry), "%d", svs.maxclients_soft);
        sv_maxclients->string = entry;
    }

    // add server info
    total = Cvar_BitInfo(status, CVAR_SERVERINFO);

    sv_maxclients->string = tmp;

    // add uptime
    if (sv_uptime->integer > 0) {
        if (sv_uptime->integer > 1) {
            len = Com_UptimeLong_m(entry, MAX_INFO_VALUE);
        } else {
            len = Com_Uptime_m(entry, MAX_INFO_VALUE);
        }
        if (total + 8 + len < MAX_INFO_STRING) {
            memcpy(status + total, "\\uptime\\", 8);
            memcpy(status + total + 8, entry, len);
            total += 8 + len;
        }
    }

    status[total++] = '\n';

    // add player list
    if (sv_status_show->integer > 1) {
        FOR_EACH_CLIENT(cl) {
            if (cl->state == cs_zombie || cl->bot) {
                continue;
            }
            len = Q_snprintf(entry, sizeof(entry),
                             "%i %i \"%s\"\n",
                             SV_GetClient_Stat(cl, STAT_FRAGS),
                             cl->ping, cl->name);
            if (len >= sizeof(entry)) {
                continue;
            }
            if (total + len >= SV_OUTPUTBUF_LENGTH) {
                break;        // can't hold any more
            }
            memcpy(status + total, entry, len);
            total += len;
        }
    }

    status[total] = 0;

    return total;
}

/*
================
SVC_Status

Responds with all the info that qplug or qspy can see
================
*/
static void SVC_Status(void)
{
    char    buffer[MAX_PACKETLEN_DEFAULT];
    size_t  len;

    if (!sv_status_show->integer) {
        return;
    }

    if (SV_RateLimited(&svs.ratelimit_status)) {
        Com_DPrintf("Dropping status request from %s\n",
                    NET_AdrToString(&net_from));
        return;
    }

    // write the packet header
    memcpy(buffer, "\xff\xff\xff\xffprint\n", 10);
    len = 10;

    len += SV_StatusString(buffer + len);

    // send the datagram
    NET_SendPacket(NS_SERVER, buffer, len, &net_from);
}

/*
================
SVC_Ack

================
*/
static void SVC_Ack(void)
{
    int i;

    for (i = 0; i < MAX_MASTERS; i++) {
        if (NET_IsEqualBaseAdr(&sv_masters[i].adr, &net_from)) {
            Com_DPrintf("Ping acknowledge from %s\n",
                        NET_AdrToString(&net_from));
            sv_masters[i].last_ack = svs.realtime;
            break;
        }
    }
}

/*
================
SVC_Info

Responds with short info for broadcast scans
The second parameter should be the current protocol version number.
================
*/
static void SVC_Info(void)
{
    char    buffer[MAX_QPATH+10];
    size_t  len;
    int     version;

    if (svs.maxclients == 1)
        return; // ignore in single player

    version = Q_atoi(Cmd_Argv(1));
    if (version < PROTOCOL_VERSION_DEFAULT || version > PROTOCOL_VERSION_Q2PRO)
        return; // ignore invalid versions

    len = Q_scnprintf(buffer, sizeof(buffer),
                      "\xff\xff\xff\xffinfo\n%16s %8s %2i/%2i\n",
                      sv_hostname->string, sv.name, SV_CountClients(),
                      svs.maxclients_soft);

    NET_SendPacket(NS_SERVER, buffer, len, &net_from);
}

/*
================
SVC_Ping

Just responds with an acknowledgement
================
*/
static void SVC_Ping(void)
{
    OOB_PRINT(NS_SERVER, &net_from, "ack");
}

/*
=================
SVC_GetChallenge

Returns a challenge number that can be used
in a subsequent client_connect command.
We do this to prevent denial of service attacks that
flood the server with invalid connection IPs.  With a
challenge, they must give a valid IP address.
=================
*/
static void SVC_GetChallenge(void)
{
    int         i, oldest;
    unsigned    challenge;
    unsigned    oldestTime;

    oldest = 0;
    oldestTime = UINT_MAX;

    // see if we already have a challenge for this ip
    for (i = 0; i < MAX_CHALLENGES; i++) {
        if (NET_IsEqualBaseAdr(&net_from, &svs.challenges[i].adr))
            break;
        if (svs.challenges[i].time > com_eventTime) {
            svs.challenges[i].time = com_eventTime;
        }
        if (svs.challenges[i].time < oldestTime) {
            oldestTime = svs.challenges[i].time;
            oldest = i;
        }
    }

    challenge = Q_rand() & INT_MAX;
    if (i == MAX_CHALLENGES) {
        // overwrite the oldest
        svs.challenges[oldest].challenge = challenge;
        svs.challenges[oldest].adr = net_from;
        svs.challenges[oldest].time = com_eventTime;
    } else {
        svs.challenges[i].challenge = challenge;
        svs.challenges[i].time = com_eventTime;
    }

    char challenge_extra[64];
    q2proto_get_challenge_extras(challenge_extra, sizeof(challenge_extra), q2repro_accepted_protocols, q_countof(q2repro_accepted_protocols));

    // send it back
    Netchan_OutOfBand(NS_SERVER, &net_from,
                      "challenge %u %s", challenge, challenge_extra);
}

/*
==================
SVC_DirectConnect

A connection request that did not come from the master
==================
*/

typedef struct {
    int         protocol;   // major version
    int         version;    // minor version
    int         qport;
    int         challenge;

    int         maxlength;
    int         nctype;
    bool        has_zlib;

    int         maxclients; // hidden client slots
    char        reconnect_var[16];
    char        reconnect_val[16];
} conn_params_t;

#define reject_printf(...) \
    Netchan_OutOfBand(NS_SERVER, &net_from, "print\n" __VA_ARGS__)

// small hack to permit one-line return statement :)
#define reject(...) reject_printf(__VA_ARGS__), false
#define reject_ptr(...) reject_printf(__VA_ARGS__), NULL

static bool parse_basic_params(const q2proto_connect_t *parsed_connect, conn_params_t *p)
{
    p->protocol = q2proto_get_protocol_netver(parsed_connect->protocol);
    p->qport = parsed_connect->qport;
    p->challenge = parsed_connect->challenge;

    /* Reject any client that doesn't support the rerelease features -
     * Old clients can't support certain things, particularly
     * game-controlled pmove. */
    if (p->protocol != PROTOCOL_VERSION_RERELEASE)
        return reject("You need a 'rerelease' capable client to connect to this server.\n");

    return true;
}

static bool permit_connection(conn_params_t *p)
{
    addrmatch_t *match;
    int i, count;
    client_t *cl;
    const char *s;

    // loopback clients are permitted without any checks
    if (NET_IsLocalAddress(&net_from))
        return true;

    // see if the challenge is valid
    for (i = 0; i < MAX_CHALLENGES; i++) {
        if (!svs.challenges[i].challenge)
            continue;

        if (NET_IsEqualBaseAdr(&net_from, &svs.challenges[i].adr)) {
            if (svs.challenges[i].challenge == p->challenge)
                break;        // good

            return reject("Bad challenge.\n");
        }
    }

    if (i == MAX_CHALLENGES)
        return reject("No challenge for address.\n");

    svs.challenges[i].challenge = 0;

    // check for banned address
    if ((match = SV_MatchAddress(&sv_banlist, &net_from)) != NULL) {
        s = match->comment;
        if (!*s) {
            s = "Your IP address is banned from this server.";
        }
        return reject("%s\nConnection refused.\n", s);
    }

    // check for locked server
    if (sv_locked->integer)
        return reject("Server is locked.\n");

    // link-local IPv6 addresses are permitted without sv_iplimit check
    if (net_from.type == NA_IP6 && NET_IsLanAddress(&net_from))
        return true;

    // limit number of connections from single IPv4 address or /48 IPv6 network
    if (sv_iplimit->integer > 0) {
        count = 0;
        FOR_EACH_CLIENT(cl) {
            netadr_t *adr = &cl->netchan.remote_address;

            if (net_from.type != adr->type)
                continue;
            if (net_from.type == NA_IP && net_from.ip.u32[0] != adr->ip.u32[0])
                continue;
            if (net_from.type == NA_IP6 && memcmp(net_from.ip.u8, adr->ip.u8, 48 / CHAR_BIT))
                continue;

            if (cl->state == cs_zombie)
                count += 1;
            else
                count += 2;
        }
        if (count / 2 >= sv_iplimit->integer) {
            if (net_from.type == NA_IP6)
                return reject("Too many connections from your IPv6 network.\n");
            else
                return reject("Too many connections from your IP address.\n");
        }
    }

    return true;
}

static bool parse_packet_length(const q2proto_connect_t *parsed_connect, conn_params_t *p)
{
    p->maxlength = parsed_connect->packet_length;
    if (p->maxlength < 0 || p->maxlength > MAX_PACKETLEN_WRITABLE)
        return reject("Invalid maximum message length.\n");

    // 0 means highest available
    if (!p->maxlength)
        p->maxlength = MAX_PACKETLEN_WRITABLE;

    if (!NET_IsLocalAddress(&net_from) && net_maxmsglen->integer > 0) {
        // cap to server defined maximum value
        if (p->maxlength > net_maxmsglen->integer)
            p->maxlength = net_maxmsglen->integer;
    }

    // don't allow too small packets
    if (p->maxlength < MIN_PACKETLEN)
        p->maxlength = MIN_PACKETLEN;

    return true;
}

static bool parse_enhanced_params(const q2proto_connect_t *parsed_connect, conn_params_t *p)
{
    p->version = parsed_connect->version;
    p->has_zlib = parsed_connect->has_zlib;
    p->nctype = parsed_connect->q2pro_nctype;

    if (p->nctype != NETCHAN_NEW)
        return reject("Invalid netchan type.\n");

    return true;
}

static const char *userinfo_ip_string(void)
{
    // fake up reserved IPv4 address to prevent IPv6 unaware mods from exploding
    if (net_from.type == NA_IP6 && !(g_features->integer & GMF_IPV6_ADDRESS_AWARE)) {
        static char s[MAX_QPATH];
        uint8_t res = 0;
        int i;

        // stuff /48 network part into the last byte
        for (i = 0; i < 48 / CHAR_BIT; i++)
            res ^= net_from.ip.u8[i];

        Q_snprintf(s, sizeof(s), "198.51.100.%u:%u", res, BigShort(net_from.port));
        return s;
    }

    return NET_AdrToString(&net_from);
}

static bool parse_userinfo(const q2proto_connect_t *parsed_connect, conn_params_t *params, char *userinfo)
{
    char *s;
    cvarban_t *ban;

    // validate userinfo
    if (parsed_connect->userinfo.len == 0)
        return reject("Empty userinfo string.\n");

    // copy userinfo off
    q2pslcpy(userinfo, MAX_INFO_STRING, &parsed_connect->userinfo);

    if (!Info_Validate(userinfo))
        return reject("Malformed userinfo string.\n");

    s = Info_ValueForKey(userinfo, "name");
    s[MAX_CLIENT_NAME - 1] = 0;
    if (COM_IsWhite(s))
        return reject("Please set your name before connecting.\n");

    // allow them to use reserved slots if they know the password
    params->maxclients = svs.maxclients;

    // check password
    s = Info_ValueForKey(userinfo, "password");
    if (sv_password->string[0]) {
        if (!s[0])
            return reject("Please set your password before connecting.\n");

        if (SV_RateLimited(&svs.ratelimit_auth))
            return reject("Invalid password.\n");

        if (strcmp(sv_password->string, s))
            return reject("Invalid password.\n");

        // valid connect packets are not rate limited
        SV_RateRecharge(&svs.ratelimit_auth);
    } else if (!sv_reserved_password->string[0] ||
               strcmp(sv_reserved_password->string, s)) {
        // if no reserved password is set on the server, do not allow
        // anyone to access reserved slots at all
        params->maxclients = svs.maxclients_soft;
    }

    // mvdspec, ip, etc are passed in extra userinfo if supported
    if (!(g_features->integer & GMF_EXTRA_USERINFO)) {
        // make sure mvdspec key is not set
        Info_RemoveKey(userinfo, "mvdspec");

        if (sv_password->string[0] || sv_reserved_password->string[0]) {
            // unset password key to make game mod happy
            Info_RemoveKey(userinfo, "password");
        }

        // force the IP key/value pair so the game can filter based on ip
        if (!Info_SetValueForKey(userinfo, "ip", userinfo_ip_string()))
            return reject("Oversize userinfo string.\n");
    }

    // reject if there is a kickable userinfo ban
    if ((ban = SV_CheckInfoBans(userinfo, true)) != NULL) {
        s = ban->comment;
        if (!s)
            s = "Userinfo banned.";
        return reject("%s\nConnection refused.\n", s);
    }

    return true;
}

static client_t *redirect(const char *addr)
{
    Netchan_OutOfBand(NS_SERVER, &net_from, "client_connect");

    // set up a fake server netchan
    MSG_WriteLong(1);
    MSG_WriteLong(0);
    MSG_WriteByte(svc_print);
    MSG_WriteByte(PRINT_HIGH);
    MSG_WriteString(va("Server is full. Redirecting you to %s...\n", addr));
    MSG_WriteByte(svc_stufftext);
    MSG_WriteString(va("connect %s\n", addr));

    NET_SendPacket(NS_SERVER, msg_write.data, msg_write.cursize, &net_from);
    SZ_Clear(&msg_write);
    return NULL;
}

static client_t *find_client_slot(conn_params_t *params)
{
    client_t *cl;
    char *s;
    int i;

    // if there is already a slot for this ip, reuse it
    FOR_EACH_CLIENT(cl) {
        if (NET_IsEqualAdr(&net_from, &cl->netchan.remote_address)) {
            if (cl->state == cs_zombie) {
                Q_strlcpy(params->reconnect_var, cl->reconnect_var, sizeof(params->reconnect_var));
                Q_strlcpy(params->reconnect_val, cl->reconnect_val, sizeof(params->reconnect_val));
            } else {
                SV_DropClient(cl, "reconnected");
            }

            Com_DPrintf("%s: reconnect\n", NET_AdrToString(&net_from));
            SV_RemoveClient(cl);
            return cl;
        }
    }

    // check for forced redirect to a different address
    s = sv_redirect_address->string;
    if (*s == '!' && params->maxclients == svs.maxclients_soft)
        return redirect(s + 1);

    // FIXME: Use ClientChooseSlot()

    // find a free client slot
    for (i = 0; i < params->maxclients; i++) {
        cl = &svs.client_pool[i];
        if (cl->state == cs_free)
            return cl;
    }

    // clients that know the password are never redirected
    if (params->maxclients != svs.maxclients_soft)
        return reject_ptr("Server and reserved slots are full.\n");

    // optionally redirect them to a different address
    if (*s)
        return redirect(s);

    return reject_ptr("Server is full.\n");
}

static void init_pmove_and_es_flags(client_t *newcl)
{
    // copy default pmove parameters
    newcl->pmp = svs.pmp;
    newcl->pmp.airaccelerate = sv_airaccelerate->integer;

    // common extensions
    newcl->pmp.speedmult = 2;
    newcl->pmp.strafehack = sv_strafejump_hack->integer >= 1;

    // Q2PRO extensions
    if (sv_qwmod->integer) {
        PmoveEnableQW(&newcl->pmp);
    }
    newcl->pmp.flyhack = true;
    newcl->pmp.flyfriction = 4;
    newcl->esFlags |= MSG_ES_UMASK | MSG_ES_LONGSOLID;
    newcl->esFlags |= MSG_ES_BEAMORIGIN;
    newcl->esFlags |= MSG_ES_SHORTANGLES;
    newcl->esFlags |= MSG_ES_EXTENSIONS;
    newcl->esFlags |= MSG_ES_RERELEASE;
    newcl->psFlags = MSG_PS_RERELEASE | MSG_PS_EXTENSIONS;
    newcl->pmp.waterhack = sv_waterjump_hack->integer >= 1;
}

static void bot_add_message(client_t *client, const byte *data,
                            size_t length, bool reliable)
{
    (void)client;
    (void)data;
    (void)length;
    (void)reliable;
}

static int bot_client_pool_limit(void)
{
    if (!svs.client_pool || svs.maxclients <= 0) {
        return 0;
    }

    return min(svs.maxclients, MAX_CLIENTS);
}

static int bot_public_client_limit(void)
{
    if (!svs.client_pool || svs.maxclients_soft <= 0) {
        return 0;
    }

    return min(svs.maxclients_soft, bot_client_pool_limit());
}

typedef struct {
    char id[MAX_QPATH];
    char source[MAX_QPATH];
    char name[MAX_CLIENT_NAME];
    char skin[MAX_INFO_VALUE];
    char team[MAX_INFO_VALUE];
    char skill[MAX_INFO_VALUE];
    char reaction[MAX_INFO_VALUE];
    char aggression[MAX_INFO_VALUE];
    char aim_error[MAX_INFO_VALUE];
    char preferred_weapon[MAX_INFO_VALUE];
    char chat_personality[MAX_INFO_VALUE];
    char role[MAX_INFO_VALUE];
    char movement_style[MAX_INFO_VALUE];
    char teamplay_bias[MAX_INFO_VALUE];
    char objective_bias[MAX_INFO_VALUE];
    char friendly_fire_care[MAX_INFO_VALUE];
    char item_greed[MAX_INFO_VALUE];
    char item_denial[MAX_INFO_VALUE];
    char powerup_timing[MAX_INFO_VALUE];
    char retreat_health[MAX_INFO_VALUE];
} bot_profile_t;

typedef struct {
    const char *dir;
    const char *ext;
} bot_profile_source_t;

typedef struct {
    int reload;
    int candidates;
    int loaded;
    int duplicates;
    int load_failures;
    int path_failures;
    int limit_failures;
    int malformed;
} bot_profile_scan_status_t;

#define MAX_BOT_PROFILES 128

static bot_profile_t bot_profiles[MAX_BOT_PROFILES];
static int bot_profile_count;
static bool bot_profiles_scanned;
static int bot_profile_reload_count;
static int bot_autofill_profile_cursor;
static bot_profile_scan_status_t bot_profile_last_scan;

static const bot_profile_source_t bot_profile_sources[] = {
    { "botfiles/bots", ".c" },
    { "bots/profiles", ".bot" },
    { "bots", ".bot" },
};

static void bot_strip_token_trailing_punctuation(char *token)
{
    size_t len = strlen(token);

    while (len > 0 && (token[len - 1] == ';' || token[len - 1] == ',')) {
        token[--len] = 0;
    }
}

static bool bot_profile_stem_has_suffix(const char *stem, const char *suffix)
{
    size_t stem_len;
    size_t suffix_len;

    if (!stem || !suffix) {
        return false;
    }

    stem_len = strlen(stem);
    suffix_len = strlen(suffix);
    return stem_len > suffix_len &&
           !Q_stricmp(stem + stem_len - suffix_len, suffix);
}

static bool bot_profile_source_id(const bot_profile_source_t *source,
                                  const char *listed_id, char *id,
                                  size_t id_size, const char **skip_reason)
{
    if (skip_reason) {
        *skip_reason = NULL;
    }

    if (!source || !listed_id || !listed_id[0]) {
        if (skip_reason) {
            *skip_reason = "empty_id";
        }
        return false;
    }

    Q_strlcpy(id, listed_id, id_size);

    if (source->dir && source->ext &&
        !Q_stricmp(source->dir, "botfiles/bots") &&
        !Q_stricmp(source->ext, ".c")) {
        if (bot_profile_stem_has_suffix(id, "_i") ||
            bot_profile_stem_has_suffix(id, "_t") ||
            bot_profile_stem_has_suffix(id, "_w")) {
            if (skip_reason) {
                *skip_reason = "companion_script";
            }
            return false;
        }

        if (bot_profile_stem_has_suffix(id, "_c")) {
            id[strlen(id) - 2] = 0;
        }
    }

    return id[0] != 0;
}

static void bot_profile_parse_warning(bot_profile_scan_status_t *status,
                                      const char *path, unsigned line,
                                      const char *key, const char *reason)
{
    if (status) {
        status->malformed++;
    }

    Com_Printf("q3a_bot_profile_parse_warning reload=%d path=%s line=%u "
               "key=%s reason=%s\n",
               status ? status->reload : 0, path ? path : "<none>", line,
               key && key[0] ? key : "<none>", reason);
}

static void bot_profile_set_reaction_seconds(bot_profile_t *profile,
                                             const char *value)
{
    float seconds = Q_atof(value);
    int milliseconds;

    if (seconds < 0.0f) {
        seconds = 0.0f;
    }

    milliseconds = (int)(seconds * 1000.0f + 0.5f);
    Q_snprintf(profile->reaction, sizeof(profile->reaction), "%d",
               milliseconds);
}

static bool bot_profile_set(bot_profile_t *profile, const char *key,
                            const char *value)
{
    char clean_value[MAX_INFO_VALUE];

    if (!key || !key[0] || !value) {
        return false;
    }

    Q_strlcpy(clean_value, value, sizeof(clean_value));
    bot_strip_token_trailing_punctuation(clean_value);

    if (!Q_stricmp(key, "name")) {
        Q_strlcpy(profile->name, clean_value, sizeof(profile->name));
        return true;
    }
    if (!Q_stricmp(key, "skin") ||
        !Q_stricmp(key, "worr_skin") ||
        !Q_stricmp(key, "characteristic_skin")) {
        Q_strlcpy(profile->skin, clean_value, sizeof(profile->skin));
        return true;
    }
    if (!Q_stricmp(key, "team") ||
        !Q_stricmp(key, "worr_team") ||
        !Q_stricmp(key, "characteristic_team")) {
        Q_strlcpy(profile->team, clean_value, sizeof(profile->team));
        return true;
    }
    if (!Q_stricmp(key, "skill")) {
        Q_strlcpy(profile->skill, clean_value, sizeof(profile->skill));
        return true;
    }
    if (!Q_stricmp(key, "reaction") ||
        !Q_stricmp(key, "reaction_time") ||
        !Q_stricmp(key, "reaction_ms") ||
        !Q_stricmp(key, "worr_reaction_ms")) {
        Q_strlcpy(profile->reaction, clean_value, sizeof(profile->reaction));
        return true;
    }
    if (!Q_stricmp(key, "characteristic_reactiontime")) {
        bot_profile_set_reaction_seconds(profile, clean_value);
        return true;
    }
    if (!Q_stricmp(key, "aggression") ||
        !Q_stricmp(key, "aggression_bias") ||
        !Q_stricmp(key, "characteristic_aggression")) {
        Q_strlcpy(profile->aggression, clean_value, sizeof(profile->aggression));
        return true;
    }
    if (!Q_stricmp(key, "aim_error") ||
        !Q_stricmp(key, "aimerror") ||
        !Q_stricmp(key, "accuracy_error") ||
        !Q_stricmp(key, "worr_aim_error")) {
        Q_strlcpy(profile->aim_error, clean_value, sizeof(profile->aim_error));
        return true;
    }
    if (!Q_stricmp(key, "preferred_weapon") ||
        !Q_stricmp(key, "weapon") ||
        !Q_stricmp(key, "favorite_weapon") ||
        !Q_stricmp(key, "worr_preferred_weapon")) {
        Q_strlcpy(profile->preferred_weapon, clean_value, sizeof(profile->preferred_weapon));
        return true;
    }
    if (!Q_stricmp(key, "chat_personality") ||
        !Q_stricmp(key, "chat") ||
        !Q_stricmp(key, "personality") ||
        !Q_stricmp(key, "worr_chat_personality")) {
        Q_strlcpy(profile->chat_personality, clean_value, sizeof(profile->chat_personality));
        return true;
    }
    if (!Q_stricmp(key, "role") ||
        !Q_stricmp(key, "team_role") ||
        !Q_stricmp(key, "worr_role")) {
        Q_strlcpy(profile->role, clean_value, sizeof(profile->role));
        return true;
    }
    if (!Q_stricmp(key, "movement_style") ||
        !Q_stricmp(key, "movement") ||
        !Q_stricmp(key, "move_style") ||
        !Q_stricmp(key, "worr_movement_style")) {
        Q_strlcpy(profile->movement_style, clean_value, sizeof(profile->movement_style));
        return true;
    }
    if (!Q_stricmp(key, "teamplay_bias") ||
        !Q_stricmp(key, "team_bias") ||
        !Q_stricmp(key, "support_bias") ||
        !Q_stricmp(key, "worr_teamplay_bias")) {
        Q_strlcpy(profile->teamplay_bias, clean_value, sizeof(profile->teamplay_bias));
        return true;
    }
    if (!Q_stricmp(key, "objective_bias") ||
        !Q_stricmp(key, "goal_bias") ||
        !Q_stricmp(key, "worr_objective_bias")) {
        Q_strlcpy(profile->objective_bias, clean_value, sizeof(profile->objective_bias));
        return true;
    }
    if (!Q_stricmp(key, "friendly_fire_care") ||
        !Q_stricmp(key, "ff_care") ||
        !Q_stricmp(key, "worr_friendly_fire_care")) {
        Q_strlcpy(profile->friendly_fire_care, clean_value, sizeof(profile->friendly_fire_care));
        return true;
    }
    if (!Q_stricmp(key, "item_greed") ||
        !Q_stricmp(key, "pickup_greed") ||
        !Q_stricmp(key, "worr_item_greed")) {
        Q_strlcpy(profile->item_greed, clean_value, sizeof(profile->item_greed));
        return true;
    }
    if (!Q_stricmp(key, "item_denial") ||
        !Q_stricmp(key, "denial_bias") ||
        !Q_stricmp(key, "worr_item_denial")) {
        Q_strlcpy(profile->item_denial, clean_value, sizeof(profile->item_denial));
        return true;
    }
    if (!Q_stricmp(key, "powerup_timing") ||
        !Q_stricmp(key, "powerup_timing_bias") ||
        !Q_stricmp(key, "worr_powerup_timing")) {
        Q_strlcpy(profile->powerup_timing, clean_value, sizeof(profile->powerup_timing));
        return true;
    }
    if (!Q_stricmp(key, "retreat_health") ||
        !Q_stricmp(key, "retreat_health_threshold") ||
        !Q_stricmp(key, "worr_retreat_health")) {
        Q_strlcpy(profile->retreat_health, clean_value, sizeof(profile->retreat_health));
        return true;
    }
    if (!Q_stricmp(key, "characteristic_name")) {
        Q_strlcpy(profile->name, clean_value, sizeof(profile->name));
        return true;
    }

    return false;
}

static int bot_profile_find_index(const char *id)
{
    if (!id || !id[0]) {
        return -1;
    }

    for (int i = 0; i < bot_profile_count; i++) {
        if (!Q_stricmp(bot_profiles[i].id, id)) {
            return i;
        }
    }

    return -1;
}

static bool bot_profile_parse_file(const char *path, const char *id,
                                   bot_profile_scan_status_t *status)
{
    bot_profile_t profile = { 0 };
    char *buffer;
    const char *parse;
    char key[MAX_TOKEN_CHARS];
    char value[MAX_TOKEN_CHARS];
    int length;
    int duplicate;
    int recognized = 0;
    unsigned saved_linenum;

    if (bot_profile_count >= MAX_BOT_PROFILES) {
        if (status) {
            status->limit_failures++;
        }
        Com_Printf("q3a_bot_profile_scan_skip reload=%d reason=limit "
                   "path=%s id=%s max=%d\n",
                   status ? status->reload : 0, path, id, MAX_BOT_PROFILES);
        return false;
    }

    duplicate = bot_profile_find_index(id);
    if (duplicate >= 0) {
        if (status) {
            status->duplicates++;
        }
        Com_Printf("q3a_bot_profile_scan_skip reload=%d reason=duplicate "
                   "path=%s id=%s original=%s\n",
                   status ? status->reload : 0, path, id,
                   bot_profiles[duplicate].source);
        return false;
    }

    length = FS_LoadFile(path, (void **)&buffer);
    if (length < 0 || !buffer) {
        if (status) {
            status->load_failures++;
        }
        Com_Printf("q3a_bot_profile_scan_skip reload=%d reason=load_failed "
                   "path=%s id=%s error=%s\n",
                   status ? status->reload : 0, path, id,
                   Q_ErrorString(length));
        return false;
    }

    Q_strlcpy(profile.id, id, sizeof(profile.id));
    Q_strlcpy(profile.source, path, sizeof(profile.source));

    parse = buffer;
    saved_linenum = com_linenum;
    com_linenum = 1;

    while (parse) {
        size_t key_len;
        size_t value_len;
        unsigned key_line;
        char clean_value[MAX_TOKEN_CHARS];

        key_len = COM_ParseToken(&parse, key, sizeof(key), PARSE_FLAG_ESCAPE);
        if (!key_len && !parse) {
            break;
        }

        key_line = com_linenum;
        if (key_len >= sizeof(key)) {
            bot_profile_parse_warning(status, path, key_line, key,
                                      "key_truncated");
        }

        bot_strip_token_trailing_punctuation(key);

        if (!key[0] || !strcmp(key, "{") || !strcmp(key, "}")) {
            continue;
        }

        if (!strcmp(key, "=")) {
            bot_profile_parse_warning(status, path, key_line, key,
                                      "unexpected_equals");
            continue;
        }

        value_len = COM_ParseToken(&parse, value, sizeof(value),
                                   PARSE_FLAG_ESCAPE);
        if (!value_len && !parse) {
            bot_profile_parse_warning(status, path, key_line, key,
                                      "missing_value");
            break;
        }

        if (value_len >= sizeof(value)) {
            bot_profile_parse_warning(status, path, key_line, key,
                                      "value_truncated");
        }

        if (!strcmp(value, "=")) {
            value_len = COM_ParseToken(&parse, value, sizeof(value),
                                       PARSE_FLAG_ESCAPE);
            if (!value_len && !parse) {
                bot_profile_parse_warning(status, path, key_line, key,
                                          "missing_value_after_equals");
                break;
            }
            if (value_len >= sizeof(value)) {
                bot_profile_parse_warning(status, path, key_line, key,
                                          "value_truncated");
            }
        }

        if (!strcmp(value, "{") || !strcmp(value, "}")) {
            bot_profile_parse_warning(status, path, key_line, key,
                                      "missing_value");
            continue;
        }

        Q_strlcpy(clean_value, value, sizeof(clean_value));
        bot_strip_token_trailing_punctuation(clean_value);
        if (!clean_value[0] && value[0]) {
            bot_profile_parse_warning(status, path, key_line, key,
                                      "missing_value");
            continue;
        }

        if (bot_profile_set(&profile, key, value)) {
            recognized++;
        }
    }

    com_linenum = saved_linenum;
    FS_FreeFile(buffer);

    if (!recognized) {
        bot_profile_parse_warning(status, path, 1, id, "no_recognized_fields");
    }

    if (!profile.name[0]) {
        Q_strlcpy(profile.name, profile.id, sizeof(profile.name));
    }
    if (!profile.skin[0]) {
        Q_strlcpy(profile.skin, "male/grunt", sizeof(profile.skin));
    }

    bot_profiles[bot_profile_count++] = profile;
    if (status) {
        status->loaded++;
    }
    return true;
}

int SV_BotReloadProfiles(void)
{
    bot_profile_scan_status_t status = { 0 };
    const char *result;

    memset(bot_profiles, 0, sizeof(bot_profiles));
    bot_profile_count = 0;
    bot_profiles_scanned = true;
    bot_autofill_profile_cursor = 0;
    status.reload = ++bot_profile_reload_count;

    Com_Printf("q3a_bot_profile_scan=begin reload=%d max=%d sources=%zu\n",
               status.reload, MAX_BOT_PROFILES, q_countof(bot_profile_sources));

    for (size_t i = 0; i < q_countof(bot_profile_sources); i++) {
        const bot_profile_source_t *source = &bot_profile_sources[i];
        void **list;
        int count = 0;

        list = FS_ListFiles(source->dir, source->ext, FS_SEARCH_STRIPEXT,
                            &count);
        status.candidates += count;
        Com_Printf("q3a_bot_profile_scan_source reload=%d dir=%s ext=%s "
                   "candidates=%d\n",
                   status.reload, source->dir, source->ext, count);
        for (int j = 0; j < count; j++) {
            char path[MAX_QPATH];
            char id[MAX_QPATH];
            const char *listed_id = list[j];
            const char *skip_reason = NULL;

            if (!bot_profile_source_id(source, listed_id, id, sizeof(id),
                                       &skip_reason)) {
                Com_Printf("q3a_bot_profile_scan_skip reload=%d "
                           "reason=%s dir=%s id=%s ext=%s\n",
                           status.reload,
                           skip_reason ? skip_reason : "not_profile",
                           source->dir, listed_id, source->ext);
                continue;
            }

            if (Q_snprintf(path, sizeof(path), "%s/%s%s", source->dir,
                           listed_id,
                           source->ext) >= sizeof(path)) {
                status.path_failures++;
                Com_Printf("q3a_bot_profile_scan_skip reload=%d "
                           "reason=path_too_long dir=%s id=%s ext=%s\n",
                           status.reload, source->dir, listed_id,
                           source->ext);
                continue;
            }

            bot_profile_parse_file(path, id, &status);
        }
        FS_FreeList(list);
    }

    if (!status.candidates) {
        result = "empty";
    } else if (status.duplicates || status.load_failures ||
               status.path_failures || status.limit_failures ||
               status.malformed) {
        result = "warnings";
    } else {
        result = "ok";
    }

    bot_profile_last_scan = status;
    Com_Printf("q3a_bot_profile_scan=end reload=%d candidates=%d loaded=%d "
               "active=%d duplicates=%d load_failures=%d path_failures=%d "
               "limit_failures=%d warnings=%d status=%s\n",
               status.reload, status.candidates, status.loaded,
               bot_profile_count, status.duplicates, status.load_failures,
               status.path_failures, status.limit_failures, status.malformed,
               result);

    return status.loaded;
}

static void bot_ensure_profiles_scanned(void)
{
    if (!bot_profiles_scanned) {
        SV_BotReloadProfiles();
    }
}

static const bot_profile_t *bot_find_profile(const char *profile_name)
{
    if (!profile_name || !profile_name[0]) {
        return NULL;
    }

    bot_ensure_profiles_scanned();

    for (int i = 0; i < bot_profile_count; i++) {
        const bot_profile_t *profile = &bot_profiles[i];

        if (!Q_stricmp(profile->id, profile_name) ||
            !Q_stricmp(profile->name, profile_name)) {
            return profile;
        }
    }

    return NULL;
}

static const bot_profile_t *bot_select_autofill_profile(void)
{
    const bot_profile_t *fallback = NULL;
    int start;

    if (bot_profile && bot_profile->string[0]) {
        const bot_profile_t *forced = bot_find_profile(bot_profile->string);
        if (forced) {
            return forced;
        }
    }

    bot_ensure_profiles_scanned();
    if (bot_profile_count <= 0) {
        return NULL;
    }

    start = bot_autofill_profile_cursor % bot_profile_count;
    for (int attempt = 0; attempt < bot_profile_count; attempt++) {
        int index = (start + attempt) % bot_profile_count;
        const bot_profile_t *candidate = &bot_profiles[index];

        if (!candidate->id[0]) {
            continue;
        }

        if (!fallback) {
            fallback = candidate;
        }

        bot_autofill_profile_cursor = (index + 1) % bot_profile_count;
        return candidate;
    }

    bot_autofill_profile_cursor = (start + 1) % bot_profile_count;
    return fallback ? fallback : &bot_profiles[start];
}

static bool bot_name_matches(const char *existing, const char *name)
{
    const char *prefix;
    char prefixed[MAX_INFO_VALUE];

    if (!existing || !existing[0]) {
        return false;
    }

    if (!Q_stricmp(existing, name)) {
        return true;
    }

    prefix = Cvar_VariableString("bot_name_prefix");
    if (!prefix[0]) {
        return false;
    }

    Q_strlcpy(prefixed, prefix, sizeof(prefixed));
    Q_strlcat(prefixed, name, sizeof(prefixed));
    return !Q_stricmp(existing, prefixed);
}

static bool bot_name_in_use(const char *name)
{
    int limit = bot_client_pool_limit();
    int i;

    for (i = 0; i < limit; i++) {
        client_t *cl = &svs.client_pool[i];
        if (cl->state > cs_zombie) {
            const char *raw_name = Info_ValueForKey(cl->userinfo, "name");

            if (bot_name_matches(raw_name, name) ||
                bot_name_matches(cl->name, name)) {
                return true;
            }
        }
    }

    return false;
}

static void bot_default_name(char *name, size_t size);

static void bot_unique_name_from_base(char *name, size_t size, const char *base)
{
    char suffix[16];
    size_t suffix_len;
    size_t base_len;

    if (!base || !base[0] || COM_IsWhite(base)) {
        bot_default_name(name, size);
        return;
    }

    Q_strlcpy(name, base, size);
    if (!bot_name_in_use(name)) {
        return;
    }

    for (int i = 2; i < 1000; i++) {
        Q_snprintf(suffix, sizeof(suffix), "%d", i);
        suffix_len = strlen(suffix);
        if (suffix_len + 1 >= size) {
            break;
        }

        base_len = size - suffix_len - 1;
        Q_snprintf(name, size, "%.*s%s", (int)base_len, base, suffix);
        if (!bot_name_in_use(name)) {
            return;
        }
    }

    bot_default_name(name, size);
}

static void bot_default_name(char *name, size_t size)
{
    int i;

    for (i = 1; i < 1000; i++) {
        Q_snprintf(name, size, "bot%d", i);
        if (!bot_name_in_use(name)) {
            return;
        }
    }

    Q_snprintf(name, size, "bot");
}

static bool bot_set_optional_userinfo(char *userinfo, const char *key,
                                      const char *value)
{
    if (!value || !value[0]) {
        return true;
    }

    return Info_SetValueForKey(userinfo, key, value);
}

static bool bot_build_userinfo(char *userinfo, const char *name,
                               const char *team,
                               const bot_profile_t *profile)
{
    const char *skin = profile && profile->skin[0] ? profile->skin : "male/grunt";
    const char *final_team = team && team[0] ? team :
        (profile && profile->team[0] ? profile->team : NULL);

    userinfo[0] = 0;

    if (!Info_SetValueForKey(userinfo, "name", name) ||
        !Info_SetValueForKey(userinfo, "skin", skin) ||
        !Info_SetValueForKey(userinfo, "rate", "0") ||
        !Info_SetValueForKey(userinfo, "bot", "1") ||
        !Info_SetValueForKey(userinfo, "ip", "bot")) {
        return false;
    }

    if (profile &&
        (!bot_set_optional_userinfo(userinfo, "bot_profile", profile->id) ||
         !bot_set_optional_userinfo(userinfo, "skill", profile->skill) ||
         !bot_set_optional_userinfo(userinfo, "bot_reaction", profile->reaction) ||
         !bot_set_optional_userinfo(userinfo, "bot_aggression", profile->aggression) ||
         !bot_set_optional_userinfo(userinfo, "bot_aim_error", profile->aim_error) ||
         !bot_set_optional_userinfo(userinfo, "bot_preferred_weapon", profile->preferred_weapon) ||
         !bot_set_optional_userinfo(userinfo, "bot_chat_personality", profile->chat_personality) ||
         !bot_set_optional_userinfo(userinfo, "bot_role", profile->role) ||
         !bot_set_optional_userinfo(userinfo, "bot_movement_style", profile->movement_style) ||
         !bot_set_optional_userinfo(userinfo, "bot_teamplay_bias", profile->teamplay_bias) ||
         !bot_set_optional_userinfo(userinfo, "bot_objective_bias", profile->objective_bias) ||
         !bot_set_optional_userinfo(userinfo, "bot_friendly_fire_care", profile->friendly_fire_care) ||
         !bot_set_optional_userinfo(userinfo, "bot_item_greed", profile->item_greed) ||
         !bot_set_optional_userinfo(userinfo, "bot_item_denial", profile->item_denial) ||
         !bot_set_optional_userinfo(userinfo, "bot_powerup_timing", profile->powerup_timing) ||
         !bot_set_optional_userinfo(userinfo, "bot_retreat_health", profile->retreat_health))) {
        return false;
    }

    if (final_team && !Info_SetValueForKey(userinfo, "team", final_team)) {
        return false;
    }

    userinfo[strlen(userinfo) + 1] = 0;
    return true;
}

typedef struct {
    bool active;
    char name[MAX_CLIENT_NAME];
    char team[MAX_INFO_VALUE];
} bot_add_request_t;

static bot_add_request_t bot_add_queue[MAX_CLIENTS];
static int bot_add_frame = -1;
static int bot_adds_this_frame;
static bool bot_processing_queue;

static void bot_update_add_frame(void)
{
    int add_frame = svs.realtime > 0 ?
        svs.realtime / SV_FRAMETIME :
        sv.framenum;

    if (bot_add_frame != add_frame) {
        bot_add_frame = add_frame;
        bot_adds_this_frame = 0;
    }
}

static void bot_clear_add_queue(void)
{
    memset(bot_add_queue, 0, sizeof(bot_add_queue));
}

static int bot_add_queue_count(void)
{
    int count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (bot_add_queue[i].active) {
            count++;
        }
    }

    return count;
}

static bool bot_enqueue_add(const char *name, const char *team)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        bot_add_request_t *request = &bot_add_queue[i];

        if (request->active) {
            continue;
        }

        request->active = true;
        Q_strlcpy(request->name, name ? name : "", sizeof(request->name));
        Q_strlcpy(request->team, team ? team : "", sizeof(request->team));
        Com_Printf("Queued bot %s for the next server frame.\n",
                   request->name[0] ? request->name : "<default>");
        return true;
    }

    Com_Printf("Bot add queue is full.\n");
    return false;
}

static client_t *bot_find_slot(const char *userinfo)
{
    edict_t *edict;
    int limit = bot_public_client_limit();
    int number;
    int i;

    if (ge->ClientChooseSlot) {
        edict = ge->ClientChooseSlot(userinfo, "", true, NULL, 0, false);
        if (edict) {
            number = NUM_FOR_EDICT(edict) - 1;
            if (number >= 0 && number < limit) {
                client_t *cl = &svs.client_pool[number];
                if (cl->state == cs_free) {
                    return cl;
                }
            }
        }
    }

    for (i = 0; i < limit; i++) {
        client_t *cl = &svs.client_pool[i];
        if (cl->state == cs_free) {
            return cl;
        }
    }

    return NULL;
}

static int SV_BotCount(void);

static bool SV_BotAddImmediate(const char *name, const char *team,
                               bool autofill)
{
    client_t *newcl;
    const bot_profile_t *profile = NULL;
    char userinfo[MAX_INFO_STRING * 2];
    char botname[MAX_CLIENT_NAME];
    const char *reason;
    qboolean allow;
    int number;

    if (!svs.initialized || sv.state != ss_game || !ge) {
        Com_Printf("No game map running.\n");
        return false;
    }

    bot_update_add_frame();

    if (name && name[0] && !COM_IsWhite(name)) {
        profile = bot_find_profile(name);
    }

    if (profile) {
        bot_unique_name_from_base(botname, sizeof(botname), profile->name);
    } else if (!name || !name[0] || COM_IsWhite(name)) {
        bot_default_name(botname, sizeof(botname));
    } else {
        Q_strlcpy(botname, name, sizeof(botname));
    }

    if (!bot_build_userinfo(userinfo, botname, team, profile)) {
        Com_Printf("Invalid bot userinfo for '%s'.\n", botname);
        return false;
    }

    newcl = bot_find_slot(userinfo);
    if (!newcl) {
        Com_Printf("No public client slot available for bot '%s'.\n", botname);
        return false;
    }

    memset(newcl, 0, sizeof(*newcl));
    number = newcl - svs.client_pool;
    newcl->number = newcl->infonum = number;
    newcl->protocol = -1;
    newcl->bot = true;
    newcl->bot_autofill = autofill;
    newcl->nodata = true;
    newcl->state = cs_connected;
    newcl->AddMessage = bot_add_message;
    newcl->edict = EDICT_NUM(number + 1);
    newcl->gamedir = fs_game->string;
    newcl->mapname = sv.name;
    newcl->configstrings = sv.configstrings;
    newcl->csr = &svs.csr;
    newcl->ge = ge;
    newcl->cm = &sv.cm;
    newcl->spawncount = sv.spawncount;
    newcl->maxclients = svs.maxclients;
#if USE_FPS
    newcl->framediv = 1;
    newcl->settings[CLS_FPS] = sv.framerate;
#endif
    init_pmove_and_es_flags(newcl);
    List_Init(&newcl->entry);

    sv_client = newcl;
    sv_player = newcl->edict;
    allow = ge->ClientConnect(newcl->edict, userinfo, "", true);
    sv_client = NULL;
    sv_player = NULL;

    if (!allow) {
        reason = Info_ValueForKey(userinfo, "rejmsg");
        Com_Printf("Bot '%s' rejected by game%s%s\n", botname,
                   reason[0] ? ": " : ".", reason[0] ? reason : "");
        newcl->state = cs_free;
        newcl->name[0] = 0;
        return false;
    }

    Q_strlcpy(newcl->userinfo, userinfo, sizeof(newcl->userinfo));
    SV_UserinfoChanged(newcl);
    newcl->rate = 0;

    sv_client = newcl;
    sv_player = newcl->edict;
    ge->ClientBegin(sv_player);
    sv_client = NULL;
    sv_player = NULL;

    newcl->state = cs_spawned;
    newcl->framenum = 1;
    newcl->last_built_client_frame = 0;
    newcl->last_built_server_frame = 0;
    newcl->lastframe = -1;
    newcl->last_acked_server_frame = 0;
    newcl->last_acked_server_frame_delta = 0;
    newcl->last_acked_server_time_us = 0;
    newcl->lastmessage = svs.realtime;
    newcl->lastactivity = svs.realtime;
    newcl->command_msec = 1800;
    newcl->min_ping = 9999;
    newcl->connect_time = time(NULL);

    Com_Printf("Added bot %s in slot %d.\n", newcl->name, newcl->number);
    bot_adds_this_frame++;
    return true;
}

bool SV_BotAdd(const char *name, const char *team)
{
    bot_update_add_frame();

    if (!bot_processing_queue && bot_adds_this_frame > 0 && SV_BotCount() > 0) {
        return bot_enqueue_add(name, team);
    }

    return SV_BotAddImmediate(name, team, false);
}

static void SV_BotProcessAddQueue(void)
{
    bot_add_request_t request = { 0 };

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    bot_update_add_frame();
    if (bot_adds_this_frame > 0) {
        return;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!bot_add_queue[i].active) {
            continue;
        }

        request = bot_add_queue[i];
        memset(&bot_add_queue[i], 0, sizeof(bot_add_queue[i]));
        break;
    }

    if (!request.active) {
        return;
    }

    bot_processing_queue = true;
    SV_BotAddImmediate(request.name[0] ? request.name : NULL,
                       request.team[0] ? request.team : NULL, false);
    bot_processing_queue = false;
}

static bool SV_BotAddAutofill(void)
{
    const bot_profile_t *profile;

    bot_update_add_frame();
    if (bot_adds_this_frame > 0) {
        return false;
    }

    profile = bot_select_autofill_profile();

    return SV_BotAddImmediate(profile ? profile->id : NULL, NULL, true);
}

bool SV_BotRemove(client_t *client)
{
    if (!client || !client->bot || client->state <= cs_zombie) {
        return false;
    }

    Com_Printf("Removed bot %s from slot %d.\n", client->name, client->number);
    SV_DropClient(client, NULL);
    SV_RemoveClient(client);
    return true;
}

int SV_BotRemoveAll(void)
{
    int count = 0;
    int limit = bot_client_pool_limit();

    bot_clear_add_queue();

    if (!svs.initialized || !limit) {
        return 0;
    }

    for (int i = 0; i < limit; i++) {
        client_t *cl = &svs.client_pool[i];
        if (cl->bot && cl->state > cs_zombie && SV_BotRemove(cl)) {
            count++;
        }
    }

    return count;
}

client_t *SV_BotGetClient(const char *s, bool partial)
{
    client_t *match = NULL;
    int limit = bot_client_pool_limit();
    int count = 0;

    if (!s || !s[0] || !svs.initialized || !limit) {
        return NULL;
    }

    if (COM_IsUint(s)) {
        int i = Q_atoi(s);
        if (i < 0 || i >= limit) {
            Com_Printf("Bad client slot number: %d\n", i);
            return NULL;
        }

        client_t *cl = &svs.client_pool[i];
        if (cl->state <= cs_zombie) {
            Com_Printf("Client slot %d is not active.\n", i);
            return NULL;
        }
        if (!cl->bot) {
            Com_Printf("Client slot %d is not a bot.\n", i);
            return NULL;
        }
        return cl;
    }

    for (int i = 0; i < limit; i++) {
        client_t *cl = &svs.client_pool[i];
        if (!cl->bot || cl->state <= cs_zombie) {
            continue;
        }
        if (!strcmp(cl->name, s) || !Q_stricmp(cl->name, s)) {
            return cl;
        }
    }

    if (!partial) {
        Com_Printf("Bot '%s' is not on the server.\n", s);
        return NULL;
    }

    for (int i = 0; i < limit; i++) {
        client_t *cl = &svs.client_pool[i];
        if (!cl->bot || cl->state <= cs_zombie) {
            continue;
        }
        if (Q_stristr(cl->name, s)) {
            match = cl;
            count++;
        }
    }

    if (!match) {
        Com_Printf("No bots matching '%s' found.\n", s);
        return NULL;
    }

    if (count > 1) {
        Com_Printf("'%s' matches multiple bots.\n", s);
        return NULL;
    }

    return match;
}

static const char *bot_client_state_name(clstate_t state)
{
    switch (state) {
    case cs_free:      return "free";
    case cs_zombie:    return "zombie";
    case cs_assigned:  return "assigned";
    case cs_connected: return "connected";
    case cs_primed:    return "primed";
    case cs_spawned:   return "spawned";
    default:           return "unknown";
    }
}

void SV_BotList(void)
{
    int count = 0;
    int limit = bot_client_pool_limit();

    if (!svs.initialized || !limit) {
        Com_Printf("No server running.\n");
        return;
    }

    Com_Printf("num state     name\n"
               "--- --------- ---------------\n");

    for (int i = 0; i < limit; i++) {
        client_t *cl = &svs.client_pool[i];
        if (!cl->bot || cl->state <= cs_zombie) {
            continue;
        }

        Com_Printf("%3i %-9s %-15.15s\n", cl->number,
                   bot_client_state_name(cl->state), cl->name);
        count++;
    }

    if (!count) {
        Com_Printf("No bots on the server.\n");
    }
}

static int SV_BotCount(void)
{
    int count = 0;
    int limit = bot_client_pool_limit();

    for (int i = 0; i < limit; i++) {
        client_t *cl = &svs.client_pool[i];
        if (cl->bot && cl->state > cs_zombie) {
            count++;
        }
    }

    return count;
}

static int SV_BotAutofillCount(void)
{
    int count = 0;
    int limit = bot_client_pool_limit();

    for (int i = 0; i < limit; i++) {
        client_t *cl = &svs.client_pool[i];
        if (cl->bot && cl->bot_autofill && cl->state > cs_zombie) {
            count++;
        }
    }

    return count;
}

typedef struct {
    int total;
    int profiled;
    int bulwark;
    int relay;
    int smoke;
    int vanguard;
    int vector;
} bot_autofill_profile_coverage_t;

static void SV_BotAutofillProfileCoverage(
    bot_autofill_profile_coverage_t *coverage,
    char *first_profile,
    size_t size)
{
    int limit = bot_client_pool_limit();

    memset(coverage, 0, sizeof(*coverage));
    if (first_profile && size > 0) {
        first_profile[0] = 0;
    }

    for (int i = 0; i < limit; i++) {
        client_t *cl = &svs.client_pool[i];
        const char *profile;

        if (!cl->bot || !cl->bot_autofill || cl->state <= cs_zombie) {
            continue;
        }

        coverage->total++;
        profile = Info_ValueForKey(cl->userinfo, "bot_profile");
        if (!profile[0]) {
            continue;
        }

        if (first_profile && size > 0 && !first_profile[0]) {
            Q_strlcpy(first_profile, profile, size);
        }
        coverage->profiled++;

        if (!Q_stricmp(profile, "bulwark")) {
            coverage->bulwark = 1;
        } else if (!Q_stricmp(profile, "relay")) {
            coverage->relay = 1;
        } else if (!Q_stricmp(profile, "smoke")) {
            coverage->smoke = 1;
        } else if (!Q_stricmp(profile, "vanguard")) {
            coverage->vanguard = 1;
        } else if (!Q_stricmp(profile, "vector")) {
            coverage->vector = 1;
        }
    }
}

static int SV_BotAutofillRequiredProfileCoverage(
    const bot_autofill_profile_coverage_t *coverage)
{
    return coverage->bulwark + coverage->relay + coverage->smoke +
           coverage->vanguard + coverage->vector;
}

static client_t *SV_BotFindAutofill(void)
{
    int limit = bot_client_pool_limit();

    for (int i = limit - 1; i >= 0; i--) {
        client_t *cl = &svs.client_pool[i];
        if (cl->bot && cl->bot_autofill && cl->state > cs_zombie) {
            return cl;
        }
    }

    return NULL;
}

static void SV_BotMaintainMinPlayers(void)
{
    int target;
    int public_limit;
    int humans;
    int bots;
    int autofill;
    int manual_bots;
    int desired_autofill;

    if (!bot_min_players) {
        return;
    }
    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    public_limit = bot_public_client_limit();
    if (!public_limit) {
        return;
    }

    target = Cvar_ClampInteger(bot_min_players, 0, public_limit);
    humans = SV_CountClients();
    bots = SV_BotCount();
    autofill = SV_BotAutofillCount();
    manual_bots = bots - autofill;

    if (target <= 0) {
        desired_autofill = 0;
    } else {
        desired_autofill = target - humans - manual_bots;
        if (desired_autofill < 0) {
            desired_autofill = 0;
        }
    }

    while (autofill > desired_autofill) {
        client_t *bot = SV_BotFindAutofill();
        if (!bot || !SV_BotRemove(bot)) {
            break;
        }
        autofill--;
    }

    if (autofill < desired_autofill) {
        SV_BotAddAutofill();
    }
}

static void SV_BotMinPlayersSmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    bool profile_coverage_mode;
    int fill_target;

    if (!bot_min_players_smoke || bot_min_players_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
    } else if (stage > 3) {
        return;
    }

    profile_coverage_mode = bot_min_players_smoke->integer >= 2;
    fill_target = min(profile_coverage_mode ? 5 : 3, bot_public_client_limit());

    if (stage == 0) {
        SV_BotRemoveAll();
        Cvar_Set("bot_enable", "1");
        Cvar_Set("bot_min_players", va("%d", fill_target));
        Com_Printf("q3a_bot_min_players_smoke=begin target=%d\n", fill_target);
        stage = 1;
        return;
    }

    if (stage == 1) {
        char first_profile[MAX_QPATH];
        bot_autofill_profile_coverage_t coverage;
        int required_profile_count;
        int required_profiles_covered;
        int profile_coverage_pass;
        int profiled;

        if (SV_BotAutofillCount() < fill_target) {
            return;
        }

        SV_BotAutofillProfileCoverage(&coverage, first_profile,
                                      sizeof(first_profile));
        profiled = coverage.profiled;
        Com_Printf("q3a_bot_min_players_smoke_after_fill count=%d auto=%d humans=%d target=%d profiled=%d first_profile=%s\n",
                   SV_BotCount(), SV_BotAutofillCount(), SV_CountClients(),
                   bot_min_players->integer, profiled,
                   first_profile[0] ? first_profile : "<none>");
        if (profile_coverage_mode) {
            required_profile_count = 5;
            required_profiles_covered =
                SV_BotAutofillRequiredProfileCoverage(&coverage);
            profile_coverage_pass =
                coverage.total >= required_profile_count &&
                coverage.profiled >= required_profile_count &&
                required_profiles_covered >= required_profile_count;
            Com_Printf("q3a_bot_min_players_smoke_profile_coverage count=%d auto=%d target=%d profiled=%d required=%d covered=%d bulwark=%d relay=%d smoke=%d vanguard=%d vector=%d pass=%d\n",
                       SV_BotCount(), coverage.total,
                       bot_min_players->integer, coverage.profiled,
                       required_profile_count, required_profiles_covered,
                       coverage.bulwark, coverage.relay, coverage.smoke,
                       coverage.vanguard, coverage.vector,
                       profile_coverage_pass);
        }
        Cvar_Set("bot_min_players", "1");
        stage = 2;
        return;
    }

    if (stage == 2) {
        if (SV_BotAutofillCount() != 1 || SV_BotCount() != 1) {
            return;
        }

        Com_Printf("q3a_bot_min_players_smoke_after_trim count=%d auto=%d target=%d\n",
                   SV_BotCount(), SV_BotAutofillCount(),
                   bot_min_players->integer);
        Cvar_Set("bot_min_players", "0");
        stage = 3;
        return;
    }

    if (SV_BotCount() != 0) {
        return;
    }

    Com_Printf("q3a_bot_min_players_smoke_after_disable count=%d auto=%d enabled=%d\n",
               SV_BotCount(), SV_BotAutofillCount(), bot_enable->integer);
    Com_Printf("q3a_bot_min_players_smoke=end final_count=%d\n", SV_BotCount());
    stage = 4;

    if (bot_min_players_smoke->integer >= 2) {
        Com_Quit(NULL, ERR_DISCONNECT);
    }
}

static const char *SV_BotProfileSmokeTarget(void)
{
    if (bot_profile_smoke_target &&
        bot_profile_smoke_target->string[0] &&
        !COM_IsWhite(bot_profile_smoke_target->string)) {
        return bot_profile_smoke_target->string;
    }

    return "smoke";
}

static void SV_BotProfileSmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    static char smoke_profile_name[MAX_CLIENT_NAME];
    const bot_profile_t *profile;
    const char *target;
    client_t *bot;
    bool added;
    int loaded;
    char profile_id[MAX_INFO_VALUE];
    char skin[MAX_INFO_VALUE];
    char skill[MAX_INFO_VALUE];
    char reaction[MAX_INFO_VALUE];
    char aggression[MAX_INFO_VALUE];
    char aim_error[MAX_INFO_VALUE];
    char preferred_weapon[MAX_INFO_VALUE];
    char chat_personality[MAX_INFO_VALUE];
    char role[MAX_INFO_VALUE];
    char movement_style[MAX_INFO_VALUE];
    char teamplay_bias[MAX_INFO_VALUE];
    char objective_bias[MAX_INFO_VALUE];
    char friendly_fire_care[MAX_INFO_VALUE];
    char item_greed[MAX_INFO_VALUE];
    char item_denial[MAX_INFO_VALUE];
    char powerup_timing[MAX_INFO_VALUE];
    char retreat_health[MAX_INFO_VALUE];

    if (!bot_profile_smoke || bot_profile_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
    } else if (stage > 1) {
        return;
    }

    if (stage == 0) {
        SV_BotRemoveAll();
        target = SV_BotProfileSmokeTarget();
        loaded = SV_BotReloadProfiles();
        profile = bot_find_profile(target);
        Com_Printf("q3a_bot_profile_smoke=begin target=%s reload=%d "
                   "profiles=%d warnings=%d found=%d\n",
                   target, bot_profile_last_scan.reload, loaded,
                   bot_profile_last_scan.malformed, profile ? 1 : 0);

        if (!profile) {
            Com_Printf("q3a_bot_profile_smoke=end final_count=%d\n",
                       SV_BotCount());
            stage = 2;
            if (bot_profile_smoke->integer >= 2) {
                Com_Quit(NULL, ERR_DISCONNECT);
            }
            return;
        }

        Q_strlcpy(smoke_profile_name, profile->name,
                  sizeof(smoke_profile_name));
        added = SV_BotAdd(target, NULL);
        Com_Printf("q3a_bot_profile_smoke_after_add_request added=%d count=%d\n",
                   added, SV_BotCount());
        stage = 1;
        return;
    }

    bot = SV_BotGetClient(smoke_profile_name, true);
    if (!bot) {
        return;
    }

    Q_strlcpy(profile_id, Info_ValueForKey(bot->userinfo, "bot_profile"),
              sizeof(profile_id));
    Q_strlcpy(skin, Info_ValueForKey(bot->userinfo, "skin"), sizeof(skin));
    Q_strlcpy(skill, Info_ValueForKey(bot->userinfo, "skill"), sizeof(skill));
    Q_strlcpy(reaction, Info_ValueForKey(bot->userinfo, "bot_reaction"),
              sizeof(reaction));
    Q_strlcpy(aggression, Info_ValueForKey(bot->userinfo, "bot_aggression"),
              sizeof(aggression));
    Q_strlcpy(aim_error, Info_ValueForKey(bot->userinfo, "bot_aim_error"),
              sizeof(aim_error));
    Q_strlcpy(preferred_weapon,
              Info_ValueForKey(bot->userinfo, "bot_preferred_weapon"),
              sizeof(preferred_weapon));
    Q_strlcpy(chat_personality,
              Info_ValueForKey(bot->userinfo, "bot_chat_personality"),
              sizeof(chat_personality));
    Q_strlcpy(role, Info_ValueForKey(bot->userinfo, "bot_role"),
              sizeof(role));
    Q_strlcpy(movement_style,
              Info_ValueForKey(bot->userinfo, "bot_movement_style"),
              sizeof(movement_style));
    Q_strlcpy(teamplay_bias,
              Info_ValueForKey(bot->userinfo, "bot_teamplay_bias"),
              sizeof(teamplay_bias));
    Q_strlcpy(objective_bias,
              Info_ValueForKey(bot->userinfo, "bot_objective_bias"),
              sizeof(objective_bias));
    Q_strlcpy(friendly_fire_care,
              Info_ValueForKey(bot->userinfo, "bot_friendly_fire_care"),
              sizeof(friendly_fire_care));
    Q_strlcpy(item_greed, Info_ValueForKey(bot->userinfo, "bot_item_greed"),
              sizeof(item_greed));
    Q_strlcpy(item_denial, Info_ValueForKey(bot->userinfo, "bot_item_denial"),
              sizeof(item_denial));
    Q_strlcpy(powerup_timing,
              Info_ValueForKey(bot->userinfo, "bot_powerup_timing"),
              sizeof(powerup_timing));
    Q_strlcpy(retreat_health,
              Info_ValueForKey(bot->userinfo, "bot_retreat_health"),
              sizeof(retreat_health));

    Com_Printf("q3a_bot_profile_smoke_after_add count=%d name=%s "
               "profile=%s skin=%s skill=%s reaction=%s aggression=%s "
               "aim_error=%s preferred_weapon=%s chat=%s role=%s "
               "movement=%s teamplay_bias=%s objective_bias=%s "
               "friendly_fire_care=%s item_greed=%s item_denial=%s "
               "powerup_timing=%s retreat_health=%s\n",
               SV_BotCount(), bot->name, profile_id, skin, skill, reaction,
               aggression, aim_error, preferred_weapon, chat_personality, role,
               movement_style, teamplay_bias, objective_bias,
               friendly_fire_care, item_greed, item_denial, powerup_timing,
               retreat_health);
    SV_BotRemoveAll();
    Com_Printf("q3a_bot_profile_smoke_after_remove_all count=%d\n",
               SV_BotCount());
    Com_Printf("q3a_bot_profile_smoke=end final_count=%d\n", SV_BotCount());
    stage = 2;

    if (bot_profile_smoke->integer >= 2) {
        Com_Quit(NULL, ERR_DISCONNECT);
    }
}

static void SV_BotTeamPolicySmokeStatus(int expected_playing,
                                        int expected_spectators,
                                        int expected_bots,
                                        int expected_queued)
{
    const bot_team_policy_status_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(BOT_TEAM_POLICY_STATUS_API_V1);
    }

    if (!api || api->api_version != 1 || !api->PrintStatus) {
        Com_Printf("q3a_bot_team_policy_status unavailable=1 "
                   "expected_playing=%d expected_spectators=%d "
                   "expected_bots=%d expected_queued=%d pass=0\n",
                   expected_playing, expected_spectators, expected_bots,
                   expected_queued);
        return;
    }

    api->PrintStatus(expected_playing, expected_spectators, expected_bots,
                     expected_queued);
}

static void SV_BotTeamPolicySmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    bool queue_enabled;
    bool added_alpha;
    bool added_bravo;
    bool added_charlie;

    if (!bot_team_policy_smoke || bot_team_policy_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
    } else if (stage > 2) {
        return;
    }

    queue_enabled = bot_team_policy_smoke->integer >= 3;

    if (stage == 0) {
        SV_BotRemoveAll();
        Cvar_Set("g_gametype", "2");
        Cvar_Set("minplayers", "2");
        Cvar_Set("maxplayers", "2");
        Cvar_Set("g_allow_duel_queue", queue_enabled ? "1" : "0");
        Cvar_Set("match_lock", "0");
        Cvar_Set("bot_enable", "1");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_team_policy_smoke=begin queue_enabled=%d\n",
                   queue_enabled ? 1 : 0);
        stage = 1;
        return;
    }

    if (stage == 1) {
        if (SV_BotCount() != 0) {
            SV_BotRemoveAll();
            return;
        }

        added_alpha = SV_BotAdd("DuelOne", NULL);
        added_bravo = SV_BotAdd("DuelTwo", NULL);
        added_charlie = SV_BotAdd("DuelThree", NULL);
        Com_Printf("q3a_bot_team_policy_smoke_after_add_requests "
                   "added_alpha=%d added_bravo=%d added_charlie=%d "
                   "count=%d\n",
                   added_alpha, added_bravo, added_charlie, SV_BotCount());
        stage = 2;
        return;
    }

    if (stage == 2) {
        if (SV_BotCount() < 3) {
            return;
        }

        Com_Printf("q3a_bot_team_policy_smoke_status_requested count=%d\n",
                   SV_BotCount());
        SV_BotTeamPolicySmokeStatus(2, 1, 3, queue_enabled ? 1 : 0);

        SV_BotRemoveAll();
        Com_Printf("q3a_bot_team_policy_smoke_removed_all count=%d\n",
                   SV_BotCount());
        SV_BotTeamPolicySmokeStatus(0, 0, 0, 0);

        Cvar_Set("bot_enable", "0");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_team_policy_smoke=end final_count=%d\n",
                   SV_BotCount());
        stage = 3;

        if (bot_team_policy_smoke->integer >= 2) {
            Com_Quit(NULL, ERR_DISCONNECT);
        }
    }
}

static void SV_BotWarmupSmokeStatus(int expected_bots,
                                    int expected_humans,
                                    int expected_playing,
                                    int expected_can_start)
{
    const bot_warmup_status_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(BOT_WARMUP_STATUS_API_V1);
    }

    if (!api || api->api_version != 1 || !api->PrintStatus) {
        Com_Printf("q3a_bot_warmup_status unavailable=1 "
                   "expected_bots=%d expected_humans=%d "
                   "expected_playing=%d expected_can_start=%d pass=0\n",
                   expected_bots, expected_humans, expected_playing,
                   expected_can_start);
        return;
    }

    api->PrintStatus(expected_bots, expected_humans, expected_playing,
                     expected_can_start);
}

static void SV_BotWarmupSmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    bool added_alpha;
    bool added_bravo;

    if (!bot_warmup_smoke || bot_warmup_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
    } else if (stage > 2) {
        return;
    }

    if (stage == 0) {
        SV_BotRemoveAll();
        Cvar_Set("deathmatch", "1");
        Cvar_Set("coop", "0");
        Cvar_Set("g_gametype", "0");
        Cvar_Set("minplayers", "2");
        Cvar_Set("maxplayers", "16");
        Cvar_Set("warmup_enabled", "1");
        Cvar_Set("warmup_do_ready_up", "1");
        Cvar_Set("match_start_no_humans", "1");
        Cvar_Set("bot_enable", "1");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_warmup_smoke=begin target=2 "
                   "minplayers=2 ready_up=1 start_no_humans=1\n");
        stage = 1;
        return;
    }

    if (stage == 1) {
        if (SV_BotCount() != 0) {
            SV_BotRemoveAll();
            return;
        }

        added_alpha = SV_BotAdd("WarmupOne", NULL);
        added_bravo = SV_BotAdd("WarmupTwo", NULL);
        Com_Printf("q3a_bot_warmup_smoke_after_add_requests "
                   "added_alpha=%d added_bravo=%d count=%d\n",
                   added_alpha, added_bravo, SV_BotCount());
        stage = 2;
        return;
    }

    if (stage == 2) {
        if (SV_BotCount() < 2) {
            return;
        }

        Com_Printf("q3a_bot_warmup_smoke_status_requested count=%d\n",
                   SV_BotCount());
        SV_BotWarmupSmokeStatus(2, 0, 2, 1);

        SV_BotRemoveAll();
        Com_Printf("q3a_bot_warmup_smoke_removed_all count=%d\n",
                   SV_BotCount());
        SV_BotWarmupSmokeStatus(0, 0, 0, 1);

        Cvar_Set("bot_enable", "0");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_warmup_smoke=end final_count=%d\n",
                   SV_BotCount());
        stage = 3;

        if (bot_warmup_smoke->integer >= 2) {
            Com_Quit(NULL, ERR_DISCONNECT);
        }
    }
}

static const bot_vote_status_api_v1_t *SV_BotVoteStatusApi(void)
{
    const bot_vote_status_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(BOT_VOTE_STATUS_API_V1);
    }

    if (!api || api->api_version != 1) {
        return NULL;
    }

    return api;
}

static void SV_BotVoteSmokeResetStatus(void)
{
    const bot_vote_status_api_v1_t *api = SV_BotVoteStatusApi();

    if (api && api->ResetStatus) {
        api->ResetStatus();
    }
}

static void SV_BotVoteSmokeStatus(int expected_bots,
                                  int expected_humans,
                                  int expected_playing,
                                  int expected_voting_clients,
                                  int expected_active_vote,
                                  int expected_last_launch_blocked)
{
    const bot_vote_status_api_v1_t *api = SV_BotVoteStatusApi();

    if (!api || !api->PrintStatus) {
        Com_Printf("q3a_bot_vote_status unavailable=1 "
                   "expected_bots=%d expected_humans=%d "
                   "expected_playing=%d expected_voting_clients=%d "
                   "expected_active_vote=%d "
                   "expected_last_launch_blocked=%d pass=0\n",
                   expected_bots, expected_humans, expected_playing,
                   expected_voting_clients, expected_active_vote,
                   expected_last_launch_blocked);
        return;
    }

    api->PrintStatus(expected_bots, expected_humans, expected_playing,
                     expected_voting_clients, expected_active_vote,
                     expected_last_launch_blocked);
}

static int SV_BotVoteSmokeTryLaunch(void)
{
    const bot_vote_status_api_v1_t *api = SV_BotVoteStatusApi();

    if (!api || !api->TryLaunchFirstBotVote) {
        Com_Printf("q3a_bot_vote_launch attempted=1 bot_found=0 client=-1 "
                   "vote=random arg=2 success=0 blocked=0 "
                   "reason=unavailable active_vote=0 voting_clients=0\n");
        return 0;
    }

    return api->TryLaunchFirstBotVote("random", "2");
}

static void SV_BotVoteSmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    bool added_alpha;
    bool added_bravo;
    int launch_success;

    if (!bot_vote_smoke || bot_vote_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
    } else if (stage > 2) {
        return;
    }

    if (stage == 0) {
        SV_BotRemoveAll();
        SV_BotVoteSmokeResetStatus();
        Cvar_Set("deathmatch", "1");
        Cvar_Set("coop", "0");
        Cvar_Set("g_gametype", "0");
        Cvar_Set("g_allow_voting", "1");
        Cvar_Set("g_allow_spec_vote", "0");
        Cvar_Set("g_vote_limit", "0");
        Cvar_Set("minplayers", "0");
        Cvar_Set("maxplayers", "16");
        Cvar_Set("warmup_enabled", "0");
        Cvar_Set("match_start_no_humans", "1");
        Cvar_Set("bot_enable", "1");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_vote_smoke=begin target=2 allow_voting=1 "
                   "allow_spec_vote=0 bot_vote_block=1\n");
        stage = 1;
        return;
    }

    if (stage == 1) {
        if (SV_BotCount() != 0) {
            SV_BotRemoveAll();
            return;
        }

        added_alpha = SV_BotAdd("VoteOne", NULL);
        added_bravo = SV_BotAdd("VoteTwo", NULL);
        Com_Printf("q3a_bot_vote_smoke_after_add_requests "
                   "added_alpha=%d added_bravo=%d count=%d\n",
                   added_alpha, added_bravo, SV_BotCount());
        stage = 2;
        return;
    }

    if (stage == 2) {
        if (SV_BotCount() < 2) {
            return;
        }

        Com_Printf("q3a_bot_vote_smoke_status_requested count=%d\n",
                   SV_BotCount());
        SV_BotVoteSmokeStatus(2, 0, 2, 0, 0, 0);

        launch_success = SV_BotVoteSmokeTryLaunch();
        Com_Printf("q3a_bot_vote_smoke_launch_requested count=%d "
                   "success=%d\n",
                   SV_BotCount(), launch_success);
        SV_BotVoteSmokeStatus(2, 0, 2, 0, 0, 1);

        SV_BotRemoveAll();
        Com_Printf("q3a_bot_vote_smoke_removed_all count=%d\n",
                   SV_BotCount());
        SV_BotVoteSmokeStatus(0, 0, 0, 0, 0, 1);

        Cvar_Set("bot_enable", "0");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_vote_smoke=end final_count=%d\n",
                   SV_BotCount());
        stage = 3;

        if (bot_vote_smoke->integer >= 2) {
            Com_Quit(NULL, ERR_DISCONNECT);
        }
    }
}

static const bot_admin_audit_status_api_v1_t *SV_BotAdminAuditStatusApi(void)
{
    const bot_admin_audit_status_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(BOT_ADMIN_AUDIT_STATUS_API_V1);
    }

    if (!api || api->api_version != 1) {
        return NULL;
    }

    return api;
}

static void SV_BotAdminAuditSmokeResetStatus(void)
{
    const bot_admin_audit_status_api_v1_t *api =
        SV_BotAdminAuditStatusApi();

    if (api && api->ResetStatus) {
        api->ResetStatus();
    }
}

static void SV_BotAdminAuditSmokeStatus(int expected_bots,
                                        int expected_admin_bots,
                                        int expected_last_blocked,
                                        int expected_red_locked)
{
    const bot_admin_audit_status_api_v1_t *api =
        SV_BotAdminAuditStatusApi();

    if (!api || !api->PrintStatus) {
        Com_Printf("q3a_bot_admin_audit_status unavailable=1 "
                   "expected_bots=%d expected_admin_bots=%d "
                   "expected_last_blocked=%d expected_red_locked=%d "
                   "pass=0\n",
                   expected_bots, expected_admin_bots,
                   expected_last_blocked, expected_red_locked);
        return;
    }

    api->PrintStatus(expected_bots, expected_admin_bots,
                     expected_last_blocked, expected_red_locked);
}

static int SV_BotAdminAuditSmokeTryCommand(void)
{
    const bot_admin_audit_status_api_v1_t *api =
        SV_BotAdminAuditStatusApi();

    if (!api || !api->TryFirstBotAdminCommand) {
        Com_Printf("q3a_bot_admin_audit_attempt attempted=1 bot_found=0 "
                   "client=-1 forced_admin=0 admin_session=0 "
                   "command=lock_team command_found=0 admin_only=0 "
                   "allowed=0 executed=0 blocked=0 reason=unavailable "
                   "red_locked_before=0 red_locked_after=0 admin_bots=0\n");
        return 0;
    }

    return api->TryFirstBotAdminCommand();
}

static void SV_BotAdminAuditSmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    bool added;
    int blocked;

    if (!bot_admin_audit_smoke ||
        bot_admin_audit_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
    } else if (stage > 2) {
        return;
    }

    if (stage == 0) {
        SV_BotRemoveAll();
        SV_BotAdminAuditSmokeResetStatus();
        Cvar_Set("deathmatch", "1");
        Cvar_Set("coop", "0");
        Cvar_Set("g_gametype", "0");
        Cvar_Set("g_allow_admin", "1");
        Cvar_Set("minplayers", "0");
        Cvar_Set("maxplayers", "16");
        Cvar_Set("warmup_enabled", "0");
        Cvar_Set("match_start_no_humans", "1");
        Cvar_Set("bot_enable", "1");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_admin_audit_smoke=begin target=1 "
                   "admin_command=lock_team bot_admin_block=1\n");
        stage = 1;
        return;
    }

    if (stage == 1) {
        if (SV_BotCount() != 0) {
            SV_BotRemoveAll();
            return;
        }

        added = SV_BotAdd("AdminAuditOne", NULL);
        Com_Printf("q3a_bot_admin_audit_smoke_after_add_requests "
                   "added=%d count=%d\n",
                   added, SV_BotCount());
        stage = 2;
        return;
    }

    if (stage == 2) {
        if (SV_BotCount() < 1) {
            return;
        }

        Com_Printf("q3a_bot_admin_audit_smoke_status_requested "
                   "count=%d\n",
                   SV_BotCount());
        SV_BotAdminAuditSmokeStatus(1, 0, 0, 0);

        blocked = SV_BotAdminAuditSmokeTryCommand();
        Com_Printf("q3a_bot_admin_audit_smoke_command_requested "
                   "count=%d blocked=%d\n",
                   SV_BotCount(), blocked);
        SV_BotAdminAuditSmokeStatus(1, 0, 1, 0);

        SV_BotRemoveAll();
        Com_Printf("q3a_bot_admin_audit_smoke_removed_all count=%d\n",
                   SV_BotCount());
        SV_BotAdminAuditSmokeStatus(0, 0, 1, 0);

        Cvar_Set("bot_enable", "0");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_admin_audit_smoke=end final_count=%d pass=1\n",
                   SV_BotCount());
        stage = 3;

        if (bot_admin_audit_smoke->integer >= 2) {
            Com_Quit(NULL, ERR_DISCONNECT);
        }
    }
}

static const bot_tournament_status_api_v1_t *SV_BotTournamentStatusApi(void)
{
    const bot_tournament_status_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(BOT_TOURNAMENT_STATUS_API_V1);
    }

    if (!api || api->api_version != 1) {
        return NULL;
    }

    return api;
}

static void SV_BotTournamentSmokeResetStatus(void)
{
    const bot_tournament_status_api_v1_t *api =
        SV_BotTournamentStatusApi();

    if (api && api->ResetStatus) {
        api->ResetStatus();
    }
}

static void SV_BotTournamentSmokeStatus(int expected_bots,
                                        int expected_active,
                                        int expected_veto_started,
                                        int expected_picks,
                                        int expected_bans,
                                        int expected_last_veto_blocked)
{
    const bot_tournament_status_api_v1_t *api =
        SV_BotTournamentStatusApi();

    if (!api || !api->PrintStatus) {
        Com_Printf("q3a_bot_tournament_status unavailable=1 "
                   "expected_bots=%d expected_active=%d "
                   "expected_veto_started=%d expected_picks=%d "
                   "expected_bans=%d expected_last_veto_blocked=%d "
                   "pass=0\n",
                   expected_bots, expected_active,
                   expected_veto_started, expected_picks, expected_bans,
                   expected_last_veto_blocked);
        return;
    }

    api->PrintStatus(expected_bots, expected_active, expected_veto_started,
                     expected_picks, expected_bans,
                     expected_last_veto_blocked);
}

static int SV_BotTournamentSmokeSetup(void)
{
    const bot_tournament_status_api_v1_t *api =
        SV_BotTournamentStatusApi();

    if (!api || !api->SetupBotVetoState) {
        Com_Printf("q3a_bot_tournament_setup attempted=1 bot_found=0 "
                   "client=-1 configured=0 active=0 veto_started=0 "
                   "bot_is_home=0 bot_social=none map0=none pool=0 "
                   "best_of=0 picks_needed=0\n");
        return 0;
    }

    return api->SetupBotVetoState();
}

static int SV_BotTournamentSmokeTryVeto(void)
{
    const bot_tournament_status_api_v1_t *api =
        SV_BotTournamentStatusApi();

    if (!api || !api->TryFirstBotVetoPick) {
        Com_Printf("q3a_bot_tournament_veto attempted=1 bot_found=0 "
                   "client=-1 map=none active_before=0 "
                   "veto_started_before=0 veto_complete_before=0 "
                   "picks_before=0 bans_before=0 allowed=0 blocked=0 "
                   "reason=unavailable picks_after=0 bans_after=0 "
                   "veto_complete_after=0\n");
        return 0;
    }

    return api->TryFirstBotVetoPick(NULL);
}

static int SV_BotTournamentSmokeSetupReplay(void)
{
    const bot_tournament_status_api_v1_t *api =
        SV_BotTournamentStatusApi();

    if (!api || !api->SetupReplayState) {
        Com_Printf("q3a_bot_tournament_replay_setup attempted=1 "
                   "configured=0 active=0 order=0 history=0 "
                   "games_played=0 player0_wins=0 player1_wins=0 "
                   "series_complete=0 replay_map=none best_of=0 "
                   "win_target=0\n");
        return 0;
    }

    return api->SetupReplayState();
}

static int SV_BotTournamentSmokeTryReplay(int game_number)
{
    const bot_tournament_status_api_v1_t *api =
        SV_BotTournamentStatusApi();

    if (!api || !api->TryReplayGame) {
        Com_Printf("q3a_bot_tournament_replay attempted=1 game=%d "
                   "active_before=0 success=0 rejected=1 "
                   "reason=unavailable target_map=none games_before=0 "
                   "games_after=0 winners_before=0 winners_after=0 "
                   "ids_before=0 ids_after=0 maps_before=0 maps_after=0 "
                   "player0_wins_before=0 player0_wins_after=0 "
                   "player1_wins_before=0 player1_wins_after=0 "
                   "series_complete_before=0 series_complete_after=0 "
                   "change_map_before=0 change_map_after=0 preserved=0 "
                   "reset_applied=0\n",
                   game_number);
        return 0;
    }

    return api->TryReplayGame(game_number);
}

static void SV_BotTournamentSmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    bool replay_mode;
    bool added;
    int configured;
    int blocked;
    int invalid_preserved;
    int replay_reset;

    if (!bot_tournament_smoke ||
        bot_tournament_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
    } else if (stage > 2) {
        return;
    }

    replay_mode = bot_tournament_smoke->integer == 3;

    if (stage == 0) {
        SV_BotRemoveAll();
        SV_BotTournamentSmokeResetStatus();
        Cvar_Set("deathmatch", "1");
        Cvar_Set("coop", "0");
        Cvar_Set("g_gametype", "0");
        Cvar_Set("match_setup_type", "tournament");
        Cvar_Set("minplayers", "0");
        Cvar_Set("maxplayers", "16");
        Cvar_Set("warmup_enabled", "0");
        Cvar_Set("match_start_no_humans", "1");
        Cvar_Set("bot_enable", "1");
        Cvar_Set("bot_min_players", "0");

        if (replay_mode) {
            Com_Printf("q3a_bot_tournament_smoke=begin target=0 "
                       "replay_reset=1 invalid_game=99 replay_game=2\n");
        } else {
            Com_Printf("q3a_bot_tournament_smoke=begin target=1 "
                       "bot_veto_block=1 action=pick\n");
        }

        stage = 1;
        return;
    }

    if (replay_mode) {
        if (stage == 1) {
            configured = SV_BotTournamentSmokeSetupReplay();
            Com_Printf("q3a_bot_tournament_smoke_replay_setup_requested "
                       "configured=%d\n",
                       configured);
            SV_BotTournamentSmokeStatus(0, 1, 1, 2, 0, 0);

            invalid_preserved = SV_BotTournamentSmokeTryReplay(99);
            Com_Printf("q3a_bot_tournament_smoke_replay_invalid_requested "
                       "game=99 preserved=%d\n",
                       invalid_preserved);
            SV_BotTournamentSmokeStatus(0, 1, 1, 2, 0, 0);

            replay_reset = SV_BotTournamentSmokeTryReplay(2);
            Com_Printf("q3a_bot_tournament_smoke_replay_valid_requested "
                       "game=2 reset=%d\n",
                       replay_reset);
            SV_BotTournamentSmokeStatus(0, 1, 1, 2, 0, 0);

            SV_BotRemoveAll();
            Com_Printf("q3a_bot_tournament_smoke_removed_all count=%d\n",
                       SV_BotCount());

            Cvar_Set("bot_enable", "0");
            Cvar_Set("bot_min_players", "0");
            Cvar_Set("match_setup_type", "standard");
            Com_Printf("q3a_bot_tournament_smoke=end final_count=%d "
                       "pass=%d\n",
                       SV_BotCount(),
                       configured && invalid_preserved && replay_reset);
            stage = 3;

            if (bot_tournament_smoke->integer >= 2) {
                Com_Quit(NULL, ERR_DISCONNECT);
            }
        }

        return;
    }

    if (stage == 1) {
        if (SV_BotCount() != 0) {
            SV_BotRemoveAll();
            return;
        }

        added = SV_BotAdd("TourneyVetoBot", NULL);
        Com_Printf("q3a_bot_tournament_smoke_after_add_requests "
                   "added=%d count=%d\n",
                   added, SV_BotCount());
        stage = 2;
        return;
    }

    if (stage == 2) {
        if (SV_BotCount() < 1) {
            return;
        }

        configured = SV_BotTournamentSmokeSetup();
        Com_Printf("q3a_bot_tournament_smoke_setup_requested "
                   "count=%d configured=%d\n",
                   SV_BotCount(), configured);
        SV_BotTournamentSmokeStatus(1, 1, 1, 0, 0, 0);

        blocked = SV_BotTournamentSmokeTryVeto();
        Com_Printf("q3a_bot_tournament_smoke_veto_requested "
                   "count=%d blocked=%d\n",
                   SV_BotCount(), blocked);
        SV_BotTournamentSmokeStatus(1, 1, 1, 0, 0, 1);

        SV_BotRemoveAll();
        Com_Printf("q3a_bot_tournament_smoke_removed_all count=%d\n",
                   SV_BotCount());
        SV_BotTournamentSmokeStatus(0, 1, 1, 0, 0, 1);

        Cvar_Set("bot_enable", "0");
        Cvar_Set("bot_min_players", "0");
        Cvar_Set("match_setup_type", "standard");
        Com_Printf("q3a_bot_tournament_smoke=end final_count=%d pass=1\n",
                   SV_BotCount());
        stage = 3;

        if (bot_tournament_smoke->integer >= 2) {
            Com_Quit(NULL, ERR_DISCONNECT);
        }
    }
}

static const bot_mapvote_status_api_v1_t *SV_BotMapVoteStatusApi(void)
{
    const bot_mapvote_status_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(BOT_MAPVOTE_STATUS_API_V1);
    }

    if (!api || api->api_version != 1) {
        return NULL;
    }

    return api;
}

static void SV_BotMapVoteSmokeResetStatus(void)
{
    const bot_mapvote_status_api_v1_t *api = SV_BotMapVoteStatusApi();

    if (api && api->ResetStatus) {
        api->ResetStatus();
    }
}

static void SV_BotMapVoteSmokeStatus(int expected_bots,
                                     int expected_active,
                                     int expected_candidates,
                                     int expected_last_bot_vote_blocked,
                                     int expected_last_finalize_success,
                                     int expected_change_map_set)
{
    const bot_mapvote_status_api_v1_t *api = SV_BotMapVoteStatusApi();

    if (!api || !api->PrintStatus) {
        Com_Printf("q3a_bot_mapvote_status unavailable=1 "
                   "expected_bots=%d expected_active=%d "
                   "expected_candidates=%d "
                   "expected_last_bot_vote_blocked=%d "
                   "expected_last_finalize_success=%d "
                   "expected_change_map_set=%d pass=0\n",
                   expected_bots, expected_active, expected_candidates,
                   expected_last_bot_vote_blocked,
                   expected_last_finalize_success,
                   expected_change_map_set);
        return;
    }

    api->PrintStatus(expected_bots, expected_active, expected_candidates,
                     expected_last_bot_vote_blocked,
                     expected_last_finalize_success,
                     expected_change_map_set);
}

static int SV_BotMapVoteSmokeBegin(void)
{
    const bot_mapvote_status_api_v1_t *api = SV_BotMapVoteStatusApi();

    if (!api || !api->BeginCurrentMapVote) {
        Com_Printf("q3a_bot_mapvote_begin attempted=1 success=0 "
                   "reason=unavailable map=- map_seeded=0 active=0 "
                   "candidates=0 candidate0=- vote_count0=0 "
                   "vote_count1=0 vote_count2=0\n");
        return 0;
    }

    return api->BeginCurrentMapVote();
}

static int SV_BotMapVoteSmokeTryBotVote(int vote_index)
{
    const bot_mapvote_status_api_v1_t *api = SV_BotMapVoteStatusApi();

    if (!api || !api->TryCastFirstBotVote) {
        Com_Printf("q3a_bot_mapvote_bot_vote attempted=1 bot_found=0 "
                   "client=-1 requested_index=%d active=0 blocked=0 "
                   "counted=0 stored_vote=-1 reason=unavailable "
                   "vote_count0=0 vote_count1=0 vote_count2=0 "
                   "bot_votes=0 human_votes=0\n",
                   vote_index);
        return 0;
    }

    return api->TryCastFirstBotVote(vote_index);
}

static int SV_BotMapVoteSmokeFinalize(void)
{
    const bot_mapvote_status_api_v1_t *api = SV_BotMapVoteStatusApi();

    if (!api || !api->FinalizeAndExit) {
        Com_Printf("q3a_bot_mapvote_finalize attempted=1 success=0 "
                   "reason=unavailable target_map=- current_map=- "
                   "selected_index=-1 selected_votes=0 candidates=0 "
                   "exit_requested=0 change_map_set=0 active=0 "
                   "vote_count0=0 vote_count1=0 vote_count2=0\n");
        return 0;
    }

    return api->FinalizeAndExit();
}

static unsigned SV_BotMapVoteSmokeElapsedMilliseconds(
    unsigned start_realtime,
    int *realtime_reset)
{
    if (realtime_reset) {
        *realtime_reset = 0;
    }

    if (svs.realtime < start_realtime) {
        if (realtime_reset) {
            *realtime_reset = 1;
        }
        return 0;
    }

    return svs.realtime - start_realtime;
}

static void SV_BotMapVoteSmokeFrame(void)
{
    enum {
        MAPVOTE_RELOAD_TIMEOUT_MS = 10000
    };
    static int seen_spawncount;
    static int stage;
    static int added_alpha;
    static int added_bravo;
    static bool reload_pending;
    static int reload_spawncount;
    static unsigned reload_request_realtime;
    static char target_map[MAX_QPATH];
    bool observed_reload = false;
    int realtime_reset = 0;
    unsigned elapsed_ms = 0;
    int begin_success;
    int bot_vote_blocked;
    int finalize_success;

    if (!bot_mapvote_smoke || bot_mapvote_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        const int old_spawncount = seen_spawncount;

        seen_spawncount = sv.spawncount;
        if (reload_pending) {
            elapsed_ms = SV_BotMapVoteSmokeElapsedMilliseconds(
                reload_request_realtime, &realtime_reset);
            reload_pending = false;
            observed_reload = true;
            stage = 3;
            Com_Printf("q3a_bot_mapvote_smoke_reload=observed "
                       "old_spawncount=%d reload_spawncount=%d "
                       "new_spawncount=%d elapsed_ms=%u "
                       "realtime_reset=%d target_map=%s current_map=%s\n",
                       old_spawncount, reload_spawncount, sv.spawncount,
                       elapsed_ms, realtime_reset,
                       target_map[0] ? target_map : "-",
                       sv.name[0] ? sv.name : "-");
        } else {
            stage = 0;
            added_alpha = 0;
            added_bravo = 0;
            reload_spawncount = 0;
            reload_request_realtime = 0;
            target_map[0] = 0;
        }
    } else if (reload_pending) {
        elapsed_ms = SV_BotMapVoteSmokeElapsedMilliseconds(
            reload_request_realtime, &realtime_reset);
        if (!realtime_reset &&
            elapsed_ms >= (unsigned)MAPVOTE_RELOAD_TIMEOUT_MS) {
            const int removed = SV_BotRemoveAll();

            Cvar_Set("bot_enable", "0");
            Cvar_Set("bot_min_players", "0");
            reload_pending = false;
            stage = 4;
            Com_Printf("q3a_bot_mapvote_smoke_reload=timeout "
                       "from_spawncount=%d current_spawncount=%d "
                       "elapsed_ms=%u timeout_ms=%d target_map=%s "
                       "current_map=%s removed=%d pass=0\n",
                       reload_spawncount, sv.spawncount, elapsed_ms,
                       MAPVOTE_RELOAD_TIMEOUT_MS,
                       target_map[0] ? target_map : "-",
                       sv.name[0] ? sv.name : "-", removed);
            Com_Printf("q3a_bot_mapvote_smoke=end final_count=%d pass=0\n",
                       SV_BotCount());
            if (bot_mapvote_smoke->integer >= 2) {
                Com_Quit(NULL, ERR_DISCONNECT);
            }
        }
        return;
    } else if (stage > 3) {
        return;
    }

    if (stage == 0) {
        SV_BotRemoveAll();
        SV_BotMapVoteSmokeResetStatus();
        Cvar_Set("deathmatch", "1");
        Cvar_Set("coop", "0");
        Cvar_Set("g_gametype", "0");
        Cvar_Set("g_maps_selector", "1");
        Cvar_Set("g_allow_voting", "1");
        Cvar_Set("g_allow_spec_vote", "0");
        Cvar_Set("minplayers", "0");
        Cvar_Set("maxplayers", "16");
        Cvar_Set("warmup_enabled", "0");
        Cvar_Set("match_start_no_humans", "1");
        Cvar_Set("bot_enable", "1");
        Cvar_Set("bot_min_players", "0");
        Q_strlcpy(target_map, sv.name, sizeof(target_map));
        added_alpha = 0;
        added_bravo = 0;
        Com_Printf("q3a_bot_mapvote_smoke=begin target=2 map=%s "
                   "selector=1 bot_vote_block=1\n",
                   target_map[0] ? target_map : "-");
        stage = 1;
        return;
    }

    if (stage == 1) {
        if (SV_BotCount() != 0) {
            SV_BotRemoveAll();
            return;
        }

        added_alpha = SV_BotAdd("MapVoteOne", NULL) ? 1 : 0;
        added_bravo = SV_BotAdd("MapVoteTwo", NULL) ? 1 : 0;
        Com_Printf("q3a_bot_mapvote_smoke_after_add_requests "
                   "added_alpha=%d added_bravo=%d count=%d\n",
                   added_alpha, added_bravo, SV_BotCount());
        stage = 2;
        return;
    }

    if (stage == 2) {
        if (SV_BotCount() < 2) {
            return;
        }

        Com_Printf("q3a_bot_mapvote_smoke_status_requested count=%d\n",
                   SV_BotCount());
        SV_BotMapVoteSmokeStatus(2, 0, 0, 0, 0, 0);

        begin_success = SV_BotMapVoteSmokeBegin();
        Com_Printf("q3a_bot_mapvote_smoke_begin_requested count=%d "
                   "map=%s success=%d\n",
                   SV_BotCount(), target_map[0] ? target_map : "-",
                   begin_success);
        SV_BotMapVoteSmokeStatus(2, 1, 1, 0, 0, 0);

        bot_vote_blocked = SV_BotMapVoteSmokeTryBotVote(0);
        Com_Printf("q3a_bot_mapvote_smoke_bot_vote_requested count=%d "
                   "blocked=%d\n",
                   SV_BotCount(), bot_vote_blocked);
        SV_BotMapVoteSmokeStatus(2, 1, 1, 1, 0, 0);

        finalize_success = SV_BotMapVoteSmokeFinalize();
        Com_Printf("q3a_bot_mapvote_smoke_finalize_requested count=%d "
                   "map=%s success=%d\n",
                   SV_BotCount(), target_map[0] ? target_map : "-",
                   finalize_success);
        SV_BotMapVoteSmokeStatus(2, 0, 1, 1, 1, 0);

        reload_pending = true;
        reload_spawncount = sv.spawncount;
        reload_request_realtime = svs.realtime;
        stage = 4;
        Com_Printf("q3a_bot_mapvote_smoke_reload=queued "
                   "from_spawncount=%d target_map=%s timeout_ms=%d\n",
                   reload_spawncount, target_map[0] ? target_map : "-",
                   MAPVOTE_RELOAD_TIMEOUT_MS);
        return;
    }

    if (stage == 3) {
        int removed;

        Com_Printf("q3a_bot_mapvote_smoke_post_reload_status_requested "
                   "count=%d observed_reload=%d\n",
                   SV_BotCount(), observed_reload ? 1 : 0);
        SV_BotMapVoteSmokeStatus(-1, 0, 0, 1, 1, 0);

        removed = SV_BotRemoveAll();
        Com_Printf("q3a_bot_mapvote_smoke_removed_all count=%d removed=%d\n",
                   SV_BotCount(), removed);
        SV_BotMapVoteSmokeStatus(0, 0, 0, 1, 1, 0);

        Cvar_Set("bot_enable", "0");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_mapvote_smoke=end final_count=%d "
                   "target_map=%s current_map=%s pass=1\n",
                   SV_BotCount(), target_map[0] ? target_map : "-",
                   sv.name[0] ? sv.name : "-");
        stage = 4;

        if (bot_mapvote_smoke->integer >= 2) {
            Com_Quit(NULL, ERR_DISCONNECT);
        }
    }
}

static const bot_mymap_status_api_v1_t *SV_BotMyMapStatusApi(void)
{
    const bot_mymap_status_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(BOT_MYMAP_STATUS_API_V1);
    }

    if (!api || api->api_version != 1) {
        return NULL;
    }

    return api;
}

static void SV_BotMyMapSmokeReset(void)
{
    const bot_mymap_status_api_v1_t *api = SV_BotMyMapStatusApi();

    if (api && api->ResetStatus) {
        api->ResetStatus();
    }
    if (api && api->ClearQueues) {
        api->ClearQueues();
    }
}

static void SV_BotMyMapSmokeStatus(int expected_bots,
                                   int expected_play_queue,
                                   int expected_mymap_queue,
                                   int expected_last_queue_success,
                                   int expected_last_consume_success)
{
    const bot_mymap_status_api_v1_t *api = SV_BotMyMapStatusApi();

    if (!api || !api->PrintStatus) {
        Com_Printf("q3a_bot_mymap_status unavailable=1 "
                   "expected_bots=%d expected_play_queue=%d "
                   "expected_mymap_queue=%d "
                   "expected_last_queue_success=%d "
                   "expected_last_consume_success=%d pass=0\n",
                   expected_bots, expected_play_queue, expected_mymap_queue,
                   expected_last_queue_success,
                   expected_last_consume_success);
        return;
    }

    api->PrintStatus(expected_bots, expected_play_queue,
                     expected_mymap_queue, expected_last_queue_success,
                     expected_last_consume_success);
}

static int SV_BotMyMapSmokeTryQueue(const char *map_name)
{
    const bot_mymap_status_api_v1_t *api = SV_BotMyMapStatusApi();

    if (!api || !api->TryQueueFirstBotMyMap) {
        Com_Printf("q3a_bot_mymap_queue attempted=1 bot_found=0 client=-1 "
                   "map=%s social=- social_assigned=0 success=0 rejected=1 "
                   "reason=unavailable play_queue=0 mymap_queue=0\n",
                   map_name && map_name[0] ? map_name : "-");
        return 0;
    }

    return api->TryQueueFirstBotMyMap(map_name);
}

static int SV_BotMyMapSmokeConsume(void)
{
    const bot_mymap_status_api_v1_t *api = SV_BotMyMapStatusApi();

    if (!api || !api->ConsumeQueuedMap) {
        Com_Printf("q3a_bot_mymap_consume attempted=1 success=0 "
                   "reason=unavailable map=- social=- "
                   "play_queue=0 mymap_queue=0\n");
        return 0;
    }

    return api->ConsumeQueuedMap();
}

static void SV_BotMyMapSmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    bool added;
    int queue_success;
    int consume_success;

    if (!bot_mymap_smoke || bot_mymap_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
    } else if (stage > 2) {
        return;
    }

    if (stage == 0) {
        SV_BotRemoveAll();
        SV_BotMyMapSmokeReset();
        Cvar_Set("deathmatch", "1");
        Cvar_Set("coop", "0");
        Cvar_Set("g_gametype", "0");
        Cvar_Set("g_maps_mymap", "1");
        Cvar_Set("g_allow_mymap", "1");
        Cvar_Set("g_maps_mymap_queue_limit", "2");
        Cvar_Set("minplayers", "0");
        Cvar_Set("maxplayers", "16");
        Cvar_Set("warmup_enabled", "0");
        Cvar_Set("match_start_no_humans", "1");
        Cvar_Set("bot_enable", "1");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_mymap_smoke=begin target=1 map=%s "
                   "maps_mymap=1 allow_mymap=1 queue_limit=2\n",
                   sv.name[0] ? sv.name : "-");
        stage = 1;
        return;
    }

    if (stage == 1) {
        if (SV_BotCount() != 0) {
            SV_BotRemoveAll();
            return;
        }

        added = SV_BotAdd("MyMapOne", NULL);
        Com_Printf("q3a_bot_mymap_smoke_after_add_request "
                   "added=%d count=%d\n",
                   added, SV_BotCount());
        stage = 2;
        return;
    }

    if (stage == 2) {
        if (SV_BotCount() < 1) {
            return;
        }

        Com_Printf("q3a_bot_mymap_smoke_status_requested count=%d\n",
                   SV_BotCount());
        SV_BotMyMapSmokeStatus(1, 0, 0, 0, 0);

        queue_success = SV_BotMyMapSmokeTryQueue(sv.name);
        Com_Printf("q3a_bot_mymap_smoke_queue_requested count=%d "
                   "map=%s success=%d\n",
                   SV_BotCount(), sv.name[0] ? sv.name : "-",
                   queue_success);
        SV_BotMyMapSmokeStatus(1, 1, 1, 1, 0);

        consume_success = SV_BotMyMapSmokeConsume();
        Com_Printf("q3a_bot_mymap_smoke_consume_requested count=%d "
                   "success=%d\n",
                   SV_BotCount(), consume_success);
        SV_BotMyMapSmokeStatus(1, 0, 0, 1, 1);

        SV_BotRemoveAll();
        Com_Printf("q3a_bot_mymap_smoke_removed_all count=%d\n",
                   SV_BotCount());
        SV_BotMyMapSmokeStatus(0, 0, 0, 1, 1);

        Cvar_Set("bot_enable", "0");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_mymap_smoke=end final_count=%d map=%s\n",
                   SV_BotCount(), sv.name[0] ? sv.name : "-");
        stage = 3;

        if (bot_mymap_smoke->integer >= 2) {
            Com_Quit(NULL, ERR_DISCONNECT);
        }
    }
}

static const bot_intermission_status_api_v1_t *SV_BotIntermissionStatusApi(void)
{
    const bot_intermission_status_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(BOT_INTERMISSION_STATUS_API_V1);
    }

    if (!api || api->api_version != 1) {
        return NULL;
    }

    return api;
}

static void SV_BotIntermissionSmokeResetStatus(void)
{
    const bot_intermission_status_api_v1_t *api =
        SV_BotIntermissionStatusApi();

    if (api && api->ResetStatus) {
        api->ResetStatus();
    }
}

static void SV_BotIntermissionSmokeStatus(int expected_bots,
                                          int expected_humans,
                                          int expected_playing,
                                          int expected_intermission,
                                          int expected_pm_freeze_bots,
                                          int expected_post_intermission,
                                          int expected_sorted_bots)
{
    const bot_intermission_status_api_v1_t *api =
        SV_BotIntermissionStatusApi();

    if (!api || !api->PrintStatus) {
        Com_Printf("q3a_bot_intermission_status unavailable=1 "
                   "expected_bots=%d expected_humans=%d "
                   "expected_playing=%d expected_intermission=%d "
                   "expected_pm_freeze_bots=%d "
                   "expected_post_intermission=%d "
                   "expected_sorted_bots=%d pass=0\n",
                   expected_bots, expected_humans, expected_playing,
                   expected_intermission, expected_pm_freeze_bots,
                   expected_post_intermission, expected_sorted_bots);
        return;
    }

    api->PrintStatus(expected_bots, expected_humans, expected_playing,
                     expected_intermission, expected_pm_freeze_bots,
                     expected_post_intermission, expected_sorted_bots);
}

static int SV_BotIntermissionSmokeBegin(void)
{
    const bot_intermission_status_api_v1_t *api =
        SV_BotIntermissionStatusApi();

    if (!api || !api->BeginIntermission) {
        Com_Printf("q3a_bot_intermission_begin attempted=1 bot_count=0 "
                   "success=0 reason=unavailable map=- intermission=0 "
                   "change_map_current=0 intermission_bots=0 "
                   "pm_freeze_bots=0 sorted_bots=0\n");
        return 0;
    }

    return api->BeginIntermission();
}

static void SV_BotIntermissionSmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    static int added_alpha;
    static int added_bravo;
    int begin_success;

    if (!bot_intermission_smoke ||
        bot_intermission_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
        added_alpha = 0;
        added_bravo = 0;
    } else if (stage > 2) {
        return;
    }

    if (stage == 0) {
        SV_BotRemoveAll();
        SV_BotIntermissionSmokeResetStatus();
        Cvar_Set("deathmatch", "1");
        Cvar_Set("coop", "0");
        Cvar_Set("g_gametype", "0");
        Cvar_Set("minplayers", "0");
        Cvar_Set("maxplayers", "16");
        Cvar_Set("warmup_enabled", "0");
        Cvar_Set("match_start_no_humans", "1");
        Cvar_Set("bot_enable", "1");
        Cvar_Set("bot_min_players", "0");
        added_alpha = 0;
        added_bravo = 0;
        Com_Printf("q3a_bot_intermission_smoke=begin target=2\n");
        stage = 1;
        return;
    }

    if (stage == 1) {
        if (SV_BotCount() != 0) {
            SV_BotRemoveAll();
            return;
        }

        added_alpha = SV_BotAdd("InterOne", NULL) ? 1 : 0;
        added_bravo = SV_BotAdd("InterTwo", NULL) ? 1 : 0;
        stage = 2;
        return;
    }

    if (stage == 2) {
        if (SV_BotCount() < 2) {
            return;
        }

        Com_Printf("q3a_bot_intermission_smoke_after_add_requests "
                   "added_alpha=%d added_bravo=%d count=%d\n",
                   added_alpha, added_bravo, SV_BotCount());
        Com_Printf("q3a_bot_intermission_smoke_status_requested count=%d\n",
                   SV_BotCount());
        SV_BotIntermissionSmokeStatus(2, 0, 2, 0, 0, 0, 2);

        begin_success = SV_BotIntermissionSmokeBegin();
        Com_Printf("q3a_bot_intermission_smoke_begin_requested count=%d "
                   "success=%d\n",
                   SV_BotCount(), begin_success);
        SV_BotIntermissionSmokeStatus(2, 0, 2, 1, 2, 0, 2);

        SV_BotRemoveAll();
        Com_Printf("q3a_bot_intermission_smoke_removed_all count=%d\n",
                   SV_BotCount());
        SV_BotIntermissionSmokeStatus(0, 0, 0, 1, 0, 0, 0);

        Cvar_Set("bot_enable", "0");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_intermission_smoke=end final_count=%d\n",
                   SV_BotCount());
        stage = 3;

        if (bot_intermission_smoke->integer >= 2) {
            Com_Quit(NULL, ERR_DISCONNECT);
        }
    }
}

static const bot_nextmap_status_api_v1_t *SV_BotNextMapStatusApi(void)
{
    const bot_nextmap_status_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(BOT_NEXTMAP_STATUS_API_V1);
    }

    if (!api || api->api_version != 1) {
        return NULL;
    }

    return api;
}

static void SV_BotNextMapSmokeResetStatus(void)
{
    const bot_nextmap_status_api_v1_t *api = SV_BotNextMapStatusApi();

    if (api && api->ResetStatus) {
        api->ResetStatus();
    }
}

static void SV_BotNextMapSmokeStatus(int expected_bots,
                                     int expected_play_queue,
                                     int expected_mymap_queue,
                                     int expected_last_transition_success,
                                     int expected_last_transition_consumed,
                                     int expected_change_map_set)
{
    const bot_nextmap_status_api_v1_t *api = SV_BotNextMapStatusApi();

    if (!api || !api->PrintStatus) {
        Com_Printf("q3a_bot_nextmap_status unavailable=1 "
                   "expected_bots=%d expected_play_queue=%d "
                   "expected_mymap_queue=%d "
                   "expected_last_transition_success=%d "
                   "expected_last_transition_consumed=%d "
                   "expected_change_map_set=%d pass=0\n",
                   expected_bots, expected_play_queue, expected_mymap_queue,
                   expected_last_transition_success,
                   expected_last_transition_consumed,
                   expected_change_map_set);
        return;
    }

    api->PrintStatus(expected_bots, expected_play_queue, expected_mymap_queue,
                     expected_last_transition_success,
                     expected_last_transition_consumed,
                     expected_change_map_set);
}

static int SV_BotNextMapSmokeTransitionQueuedMap(void)
{
    const bot_nextmap_status_api_v1_t *api = SV_BotNextMapStatusApi();

    if (!api || !api->TransitionQueuedMap) {
        Com_Printf("q3a_bot_nextmap_transition attempted=1 success=0 "
                   "consumed=0 reason=unavailable target_map=- "
                   "current_map=- play_queue_before=0 "
                   "mymap_queue_before=0 play_queue_after=0 "
                   "mymap_queue_after=0 override_enable_flags=0 "
                   "override_disable_flags=0 change_map_set=0\n");
        return 0;
    }

    return api->TransitionQueuedMap();
}

static unsigned SV_BotNextMapSmokeElapsedMilliseconds(
    unsigned start_realtime,
    int *realtime_reset)
{
    if (realtime_reset) {
        *realtime_reset = 0;
    }

    if (svs.realtime < start_realtime) {
        if (realtime_reset) {
            *realtime_reset = 1;
        }
        return 0;
    }

    return svs.realtime - start_realtime;
}

static void SV_BotNextMapSmokeFrame(void)
{
    enum {
        NEXTMAP_RELOAD_TIMEOUT_MS = 10000
    };
    static int seen_spawncount;
    static int stage;
    static int added;
    static bool reload_pending;
    static int reload_spawncount;
    static unsigned reload_request_realtime;
    static char target_map[MAX_QPATH];
    bool observed_reload = false;
    int realtime_reset = 0;
    unsigned elapsed_ms = 0;
    int queue_success;
    int transition_success;

    if (!bot_nextmap_smoke || bot_nextmap_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        const int old_spawncount = seen_spawncount;

        seen_spawncount = sv.spawncount;
        if (reload_pending) {
            elapsed_ms = SV_BotNextMapSmokeElapsedMilliseconds(
                reload_request_realtime, &realtime_reset);
            reload_pending = false;
            observed_reload = true;
            stage = 3;
            Com_Printf("q3a_bot_nextmap_smoke_reload=observed "
                       "old_spawncount=%d reload_spawncount=%d "
                       "new_spawncount=%d elapsed_ms=%u "
                       "realtime_reset=%d target_map=%s current_map=%s\n",
                       old_spawncount, reload_spawncount, sv.spawncount,
                       elapsed_ms, realtime_reset, target_map[0] ? target_map : "-",
                       sv.name[0] ? sv.name : "-");
        } else {
            stage = 0;
            added = 0;
            reload_spawncount = 0;
            reload_request_realtime = 0;
            target_map[0] = 0;
        }
    } else if (reload_pending) {
        elapsed_ms = SV_BotNextMapSmokeElapsedMilliseconds(
            reload_request_realtime, &realtime_reset);
        if (!realtime_reset &&
            elapsed_ms >= (unsigned)NEXTMAP_RELOAD_TIMEOUT_MS) {
            const int removed = SV_BotRemoveAll();

            Cvar_Set("bot_enable", "0");
            Cvar_Set("bot_min_players", "0");
            reload_pending = false;
            stage = 4;
            Com_Printf("q3a_bot_nextmap_smoke_reload=timeout "
                       "from_spawncount=%d current_spawncount=%d "
                       "elapsed_ms=%u timeout_ms=%d target_map=%s "
                       "current_map=%s removed=%d pass=0\n",
                       reload_spawncount, sv.spawncount, elapsed_ms,
                       NEXTMAP_RELOAD_TIMEOUT_MS,
                       target_map[0] ? target_map : "-",
                       sv.name[0] ? sv.name : "-", removed);
            Com_Printf("q3a_bot_nextmap_smoke=end final_count=%d pass=0\n",
                       SV_BotCount());
            if (bot_nextmap_smoke->integer >= 2) {
                Com_Quit(NULL, ERR_DISCONNECT);
            }
        }
        return;
    } else if (stage > 3) {
        return;
    }

    if (stage == 0) {
        SV_BotRemoveAll();
        SV_BotMyMapSmokeReset();
        SV_BotNextMapSmokeResetStatus();
        Cvar_Set("deathmatch", "1");
        Cvar_Set("coop", "0");
        Cvar_Set("g_gametype", "0");
        Cvar_Set("g_maps_mymap", "1");
        Cvar_Set("g_allow_mymap", "1");
        Cvar_Set("g_maps_mymap_queue_limit", "2");
        Cvar_Set("minplayers", "0");
        Cvar_Set("maxplayers", "16");
        Cvar_Set("warmup_enabled", "0");
        Cvar_Set("match_start_no_humans", "1");
        Cvar_Set("bot_enable", "1");
        Cvar_Set("bot_min_players", "0");
        Q_strlcpy(target_map, sv.name, sizeof(target_map));
        added = 0;
        Com_Printf("q3a_bot_nextmap_smoke=begin target=1 map=%s "
                   "maps_mymap=1 allow_mymap=1 queue_limit=2\n",
                   target_map[0] ? target_map : "-");
        stage = 1;
        return;
    }

    if (stage == 1) {
        if (SV_BotCount() != 0) {
            SV_BotRemoveAll();
            return;
        }

        added = SV_BotAdd("NextMapOne", NULL) ? 1 : 0;
        Com_Printf("q3a_bot_nextmap_smoke_after_add_request "
                   "added=%d count=%d\n",
                   added, SV_BotCount());
        stage = 2;
        return;
    }

    if (stage == 2) {
        if (SV_BotCount() < 1) {
            return;
        }

        Com_Printf("q3a_bot_nextmap_smoke_status_requested count=%d\n",
                   SV_BotCount());
        SV_BotMyMapSmokeStatus(1, 0, 0, 0, 0);
        SV_BotNextMapSmokeStatus(1, 0, 0, 0, 0, 0);

        queue_success = SV_BotMyMapSmokeTryQueue(target_map);
        Com_Printf("q3a_bot_nextmap_smoke_queue_requested count=%d "
                   "map=%s success=%d\n",
                   SV_BotCount(), target_map[0] ? target_map : "-",
                   queue_success);
        SV_BotMyMapSmokeStatus(1, 1, 1, 1, 0);
        SV_BotNextMapSmokeStatus(1, 1, 1, 0, 0, 0);

        transition_success = SV_BotNextMapSmokeTransitionQueuedMap();
        Com_Printf("q3a_bot_nextmap_smoke_transition_requested count=%d "
                   "map=%s success=%d\n",
                   SV_BotCount(), target_map[0] ? target_map : "-",
                   transition_success);
        SV_BotNextMapSmokeStatus(1, 0, 0, 1, 1, 0);

        reload_pending = true;
        reload_spawncount = sv.spawncount;
        reload_request_realtime = svs.realtime;
        stage = 4;
        Com_Printf("q3a_bot_nextmap_smoke_reload=queued "
                   "from_spawncount=%d target_map=%s timeout_ms=%d\n",
                   reload_spawncount, target_map[0] ? target_map : "-",
                   NEXTMAP_RELOAD_TIMEOUT_MS);
        return;
    }

    if (stage == 3) {
        int removed;

        Com_Printf("q3a_bot_nextmap_smoke_post_reload_status_requested "
                   "count=%d observed_reload=%d\n",
                   SV_BotCount(), observed_reload ? 1 : 0);
        SV_BotNextMapSmokeStatus(-1, 0, 0, 1, 1, 0);

        removed = SV_BotRemoveAll();
        Com_Printf("q3a_bot_nextmap_smoke_removed_all count=%d removed=%d\n",
                   SV_BotCount(), removed);
        SV_BotNextMapSmokeStatus(0, 0, 0, 1, 1, 0);

        Cvar_Set("bot_enable", "0");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_nextmap_smoke=end final_count=%d "
                   "target_map=%s current_map=%s pass=1\n",
                   SV_BotCount(), target_map[0] ? target_map : "-",
                   sv.name[0] ? sv.name : "-");
        stage = 4;

        if (bot_nextmap_smoke->integer >= 2) {
            Com_Quit(NULL, ERR_DISCONNECT);
        }
    }
}

static const bot_scoreboard_status_api_v1_t *SV_BotScoreboardStatusApi(void)
{
    const bot_scoreboard_status_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(BOT_SCOREBOARD_STATUS_API_V1);
    }

    if (!api || api->api_version != 1) {
        return NULL;
    }

    return api;
}

static void SV_BotScoreboardSmokeResetStatus(void)
{
    const bot_scoreboard_status_api_v1_t *api = SV_BotScoreboardStatusApi();

    if (api && api->ResetStatus) {
        api->ResetStatus();
    }
}

static void SV_BotScoreboardSmokeStatus(int expected_bots,
                                        int expected_humans,
                                        int expected_playing,
                                        int expected_sorted_bots,
                                        int expected_leader_bot,
                                        int expected_top_score,
                                        int expected_second_score)
{
    const bot_scoreboard_status_api_v1_t *api = SV_BotScoreboardStatusApi();

    if (!api || !api->PrintStatus) {
        Com_Printf("q3a_bot_scoreboard_status unavailable=1 "
                   "expected_bots=%d expected_humans=%d "
                   "expected_playing=%d expected_sorted_bots=%d "
                   "expected_leader_bot=%d expected_top_score=%d "
                   "expected_second_score=%d pass=0\n",
                   expected_bots, expected_humans, expected_playing,
                   expected_sorted_bots, expected_leader_bot,
                   expected_top_score, expected_second_score);
        return;
    }

    api->PrintStatus(expected_bots, expected_humans, expected_playing,
                     expected_sorted_bots, expected_leader_bot,
                     expected_top_score, expected_second_score);
}

static int SV_BotScoreboardSmokeApplyScores(int leader_score,
                                            int runner_score)
{
    const bot_scoreboard_status_api_v1_t *api = SV_BotScoreboardStatusApi();

    if (!api || !api->ApplyTestScores) {
        Com_Printf("q3a_bot_scoreboard_scores attempted=1 bot_count=0 "
                   "applied=0 leader_client=-1 runner_client=-1 "
                   "leader_score=%d runner_score=%d reason=unavailable "
                   "top_client=-1 top_score=0 sorted_bots=0\n",
                   leader_score, runner_score);
        return 0;
    }

    return api->ApplyTestScores(leader_score, runner_score);
}

static void SV_BotScoreboardSmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    static int added_alpha;
    static int added_bravo;
    int score_success;

    if (!bot_scoreboard_smoke ||
        bot_scoreboard_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
        added_alpha = 0;
        added_bravo = 0;
    } else if (stage > 2) {
        return;
    }

    if (stage == 0) {
        SV_BotRemoveAll();
        SV_BotScoreboardSmokeResetStatus();
        Cvar_Set("deathmatch", "1");
        Cvar_Set("coop", "0");
        Cvar_Set("g_gametype", "0");
        Cvar_Set("minplayers", "0");
        Cvar_Set("maxplayers", "16");
        Cvar_Set("warmup_enabled", "0");
        Cvar_Set("match_start_no_humans", "1");
        Cvar_Set("bot_enable", "1");
        Cvar_Set("bot_min_players", "0");
        added_alpha = 0;
        added_bravo = 0;
        Com_Printf("q3a_bot_scoreboard_smoke=begin target=2 "
                   "leader_score=7 runner_score=3\n");
        stage = 1;
        return;
    }

    if (stage == 1) {
        if (SV_BotCount() != 0) {
            SV_BotRemoveAll();
            return;
        }

        added_alpha = SV_BotAdd("ScoreOne", NULL) ? 1 : 0;
        added_bravo = SV_BotAdd("ScoreTwo", NULL) ? 1 : 0;
        stage = 2;
        return;
    }

    if (stage == 2) {
        if (SV_BotCount() < 2) {
            return;
        }

        Com_Printf("q3a_bot_scoreboard_smoke_after_add_requests "
                   "added_alpha=%d added_bravo=%d count=%d\n",
                   added_alpha, added_bravo, SV_BotCount());
        Com_Printf("q3a_bot_scoreboard_smoke_status_requested count=%d\n",
                   SV_BotCount());
        SV_BotScoreboardSmokeStatus(2, 0, 2, 2, 1, 0, 0);

        score_success = SV_BotScoreboardSmokeApplyScores(7, 3);
        Com_Printf("q3a_bot_scoreboard_smoke_scores_requested count=%d "
                   "success=%d\n",
                   SV_BotCount(), score_success);
        SV_BotScoreboardSmokeStatus(2, 0, 2, 2, 1, 7, 3);

        SV_BotRemoveAll();
        Com_Printf("q3a_bot_scoreboard_smoke_removed_all count=%d\n",
                   SV_BotCount());
        SV_BotScoreboardSmokeStatus(0, 0, 0, 0, 0, -1, -1);

        Cvar_Set("bot_enable", "0");
        Cvar_Set("bot_min_players", "0");
        Com_Printf("q3a_bot_scoreboard_smoke=end final_count=%d\n",
                   SV_BotCount());
        stage = 3;

        if (bot_scoreboard_smoke->integer >= 2) {
            Com_Quit(NULL, ERR_DISCONNECT);
        }
    }
}

static const match_logging_status_api_v1_t *SV_MatchLoggingStatusApi(void)
{
    const match_logging_status_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(MATCH_LOGGING_STATUS_API_V1);
    }

    if (!api || api->api_version != 1 || !api->PrintSchemaStatus) {
        return NULL;
    }

    return api;
}

static int SV_BotMatchLogSmokePrintSchema(void)
{
    const match_logging_status_api_v1_t *api = SV_MatchLoggingStatusApi();

    if (!api) {
        Com_Printf("q3a_match_logging_schema attempted=1 pass=0 "
                   "reason=unavailable\n");
        return 0;
    }

    return api->PrintSchemaStatus();
}

static void SV_BotMatchLogSmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    int pass;

    if (!bot_matchlog_smoke ||
        bot_matchlog_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
    } else if (stage > 0) {
        return;
    }

    SV_BotRemoveAll();
    Cvar_Set("deathmatch", "1");
    Cvar_Set("coop", "0");
    Cvar_Set("g_gametype", "0");
    Cvar_Set("g_statex_export_html", "0");
    Com_Printf("q3a_bot_matchlog_smoke=begin target=0 schema=1\n");

    pass = SV_BotMatchLogSmokePrintSchema();
    Com_Printf("q3a_bot_matchlog_smoke_schema_requested pass=%d\n", pass);
    Com_Printf("q3a_bot_matchlog_smoke=end final_count=%d pass=%d\n",
               SV_BotCount(), pass);
    stage = 1;

    if (bot_matchlog_smoke->integer >= 2) {
        Com_Quit(NULL, ERR_DISCONNECT);
    }
}

static const bot_frame_command_api_v1_t *SV_BotFrameCommandApi(void)
{
    const bot_frame_command_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(BOT_FRAME_COMMAND_API_V1);
    }

    if (!api || api->api_version != 1 || !api->BuildCommand) {
        return NULL;
    }

    return api;
}

static void SV_BotFrameCommandStatus(int expected_min_frames,
                                     int expected_min_commands)
{
    const bot_frame_command_api_v1_t *api = SV_BotFrameCommandApi();

    if (!api || !api->PrintStatus) {
        Com_Printf("q3a_bot_frame_command_status unavailable=1 "
                   "expected_min_frames=%d expected_min_commands=%d pass=0\n",
                   expected_min_frames, expected_min_commands);
        return;
    }

    api->PrintStatus(expected_min_frames, expected_min_commands);
}

static void SV_BotChatPolicySmokeStatus(int expected_bots,
                                        int expected_profile_chat,
                                        int expected_allow_chat,
                                        int expected_dispatch_enabled)
{
    const bot_chat_policy_status_api_v1_t *api = NULL;

    if (ge && ge->GetExtension) {
        api = ge->GetExtension(BOT_CHAT_POLICY_STATUS_API_V1);
    }

    if (!api || api->api_version != 1 || !api->PrintStatus) {
        Com_Printf("q3a_bot_chat_policy_status unavailable=1 "
                   "expected_bots=%d expected_profile_chat=%d "
                   "expected_allow_chat=%d expected_dispatch_enabled=%d "
                   "pass=0\n",
                   expected_bots, expected_profile_chat, expected_allow_chat,
                   expected_dispatch_enabled);
        return;
    }

    api->PrintStatus(expected_bots, expected_profile_chat,
                     expected_allow_chat, expected_dispatch_enabled);
}

#define bot_FRAME_COMMAND_STATUS_CAPTURE_TARGET 19
#define bot_FRAME_COMMAND_STATUS_CAPTURE_SIZE 262144

static char bot_frame_command_status_capture[
    bot_FRAME_COMMAND_STATUS_CAPTURE_SIZE];
static size_t bot_frame_command_status_capture_len;

static void SV_BotFrameCommandStatusCaptureReset(void)
{
    bot_frame_command_status_capture_len = 0;
    bot_frame_command_status_capture[0] = 0;
}

static void SV_BotFrameCommandStatusCaptureFlush(int redirected,
                                                 const char *outputbuf,
                                                 size_t len)
{
    size_t copy_len;

    (void)redirected;

    if (!outputbuf || !len ||
        bot_frame_command_status_capture_len >=
        sizeof(bot_frame_command_status_capture) - 1) {
        return;
    }

    copy_len = min(
        len,
        sizeof(bot_frame_command_status_capture) - 1 -
            bot_frame_command_status_capture_len);
    memcpy(bot_frame_command_status_capture +
               bot_frame_command_status_capture_len,
           outputbuf, copy_len);
    bot_frame_command_status_capture_len += copy_len;
    bot_frame_command_status_capture[
        bot_frame_command_status_capture_len] = 0;
}

static const char *SV_BotFrameCommandStatusCapture(int expected_min_frames,
                                                   int expected_min_commands)
{
    char redirect_buffer[MAXPRINTMSG];

    SV_BotFrameCommandStatusCaptureReset();
    Com_BeginRedirect(bot_FRAME_COMMAND_STATUS_CAPTURE_TARGET,
                      redirect_buffer, sizeof(redirect_buffer),
                      SV_BotFrameCommandStatusCaptureFlush);
    SV_BotFrameCommandStatus(expected_min_frames, expected_min_commands);
    Com_EndRedirect();

    return bot_frame_command_status_capture;
}

static bool SV_BotFrameCommandStatusReadInt(const char *status,
                                            const char *field,
                                            int *value)
{
    char pattern[64];
    const char *cursor;
    char *end;
    long parsed;

    if (!status || !field || !value ||
        Q_snprintf(pattern, sizeof(pattern), "%s=", field) >=
            sizeof(pattern)) {
        return false;
    }

    cursor = status;
    while ((cursor = strstr(cursor, pattern)) != NULL) {
        if (cursor == status ||
            cursor[-1] == ' ' ||
            cursor[-1] == '\n' ||
            cursor[-1] == '\r' ||
            cursor[-1] == '\t') {
            break;
        }
        cursor++;
    }
    if (!cursor) {
        return false;
    }

    cursor += strlen(pattern);
    parsed = strtol(cursor, &end, 10);
    if (end == cursor) {
        return false;
    }

    *value = (int)parsed;
    return true;
}

static int SV_BotFrameCommandStatusCapturedPass(const char *status,
                                                int expected_min_frames,
                                                int expected_min_commands,
                                                bool *official_pass)
{
    const char *frame_status;
    int pass;
    int frames;
    int commands;
    int route_commands;
    int route_failures;
    int item_goal_reservation_skips;
    int item_goal_peak_active_reservations;

    if (official_pass) {
        *official_pass = false;
    }

    frame_status = strstr(status ? status : "", "q3a_bot_frame_command_status");
    if (frame_status) {
        status = frame_status;
    }

    if (SV_BotFrameCommandStatusReadInt(status, "pass", &pass) &&
        (pass == 0 || pass == 1)) {
        if (official_pass) {
            *official_pass = true;
        }
        return pass;
    }

    if (!SV_BotFrameCommandStatusReadInt(status, "frames", &frames) ||
        !SV_BotFrameCommandStatusReadInt(status, "commands", &commands) ||
        !SV_BotFrameCommandStatusReadInt(status, "route_commands",
                                         &route_commands) ||
        !SV_BotFrameCommandStatusReadInt(status, "route_failures",
                                         &route_failures) ||
        !SV_BotFrameCommandStatusReadInt(status, "item_goal_reservation_skips",
                                         &item_goal_reservation_skips) ||
        !SV_BotFrameCommandStatusReadInt(
            status, "item_goal_peak_active_reservations",
            &item_goal_peak_active_reservations)) {
        return -1;
    }

    if (frames < expected_min_frames ||
        commands < expected_min_commands ||
        route_commands < expected_min_commands ||
        route_failures != 0 ||
        item_goal_reservation_skips <= 0 ||
        item_goal_peak_active_reservations < expected_min_commands) {
        return 0;
    }

    return 1;
}

static int SV_BotFrameCommandStatusCapturedCleanupPass(
    const char *status,
    int bot_count,
    int *active_reservations)
{
    const char *frame_status;
    int reservations;

    if (active_reservations) {
        *active_reservations = -1;
    }

    frame_status = strstr(status ? status : "", "q3a_bot_frame_command_status");
    if (frame_status) {
        status = frame_status;
    }

    if (!SV_BotFrameCommandStatusReadInt(status,
                                         "item_goal_active_reservations",
                                         &reservations)) {
        return 0;
    }

    if (active_reservations) {
        *active_reservations = reservations;
    }

    return bot_count == 0 && reservations == 0;
}

static int SV_BotFrameCommandSmokeMode(void)
{
    return bot_frame_command_smoke ? bot_frame_command_smoke->integer : 0;
}

static bool SV_BotFrameCommandSmokeStallsMovement(void)
{
    return SV_BotFrameCommandSmokeMode() == 4;
}

static bool SV_BotFrameCommandSmokeIsSoak(void)
{
    return SV_BotFrameCommandSmokeMode() == 18;
}

static bool SV_BotFrameCommandSmokeIsMapRepeat(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 19 || mode == 73;
}

static bool SV_BotFrameCommandSmokeIsEngageEnemy(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 20 || mode == 81;
}

static bool SV_BotFrameCommandSmokeIsSwitchWeapons(void)
{
    return SV_BotFrameCommandSmokeMode() == 21;
}

static bool SV_BotFrameCommandSmokeIsHealthArmorPickup(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 22 || mode == 85;
}

static bool SV_BotFrameCommandSmokeIsTeamObjective(void)
{
    return SV_BotFrameCommandSmokeMode() == 23;
}

static bool SV_BotFrameCommandSmokeIsAimFairness(void)
{
    return SV_BotFrameCommandSmokeMode() == 24;
}

static bool SV_BotFrameCommandSmokeIsAimFirePolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 66;
}

static bool SV_BotFrameCommandSmokeIsAmmoPressure(void)
{
    return SV_BotFrameCommandSmokeMode() == 67;
}

static bool SV_BotFrameCommandSmokeIsSurvivalInventory(void)
{
    return SV_BotFrameCommandSmokeMode() == 68;
}

static bool SV_BotFrameCommandSmokeIsSurvivalHealthRoute(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 69 || mode == 84;
}

static bool SV_BotFrameCommandSmokeIsSurvivalArmorRoute(void)
{
    return SV_BotFrameCommandSmokeMode() == 70;
}

static bool SV_BotFrameCommandSmokeIsCombatSurvivalRegression(void)
{
    return SV_BotFrameCommandSmokeMode() == 71;
}

static bool SV_BotFrameCommandSmokeIsThreatRetreat(void)
{
    return SV_BotFrameCommandSmokeMode() == 72;
}

static bool SV_BotFrameCommandSmokeIsTdmRoleSpawnStability(void)
{
    return SV_BotFrameCommandSmokeMode() == 73;
}

static bool SV_BotFrameCommandSmokeIsFfaLivePacing(void)
{
    return SV_BotFrameCommandSmokeMode() == 74;
}

static bool SV_BotFrameCommandSmokeIsDuelLivePacing(void)
{
    return SV_BotFrameCommandSmokeMode() == 75;
}

static bool SV_BotFrameCommandSmokeIsCtfObjectiveTransitions(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 76 || mode == 86 || mode == 87;
}

static bool SV_BotFrameCommandSmokeIsCoopLiveLoop(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 77 || mode == 91;
}

static bool SV_BotFrameCommandSmokeIsCoopShareLoop(void)
{
    return SV_BotFrameCommandSmokeMode() == 78;
}

static bool SV_BotFrameCommandSmokeIsSurvivalRoute(void)
{
    return SV_BotFrameCommandSmokeIsSurvivalHealthRoute() ||
        SV_BotFrameCommandSmokeIsSurvivalArmorRoute() ||
        SV_BotFrameCommandSmokeIsCombatSurvivalRegression();
}

static bool SV_BotFrameCommandSmokeIsItemTimer(void)
{
    return SV_BotFrameCommandSmokeMode() == 25;
}

static bool SV_BotFrameCommandSmokeIsMatchReadiness(void)
{
    return SV_BotFrameCommandSmokeMode() == 26;
}

static bool SV_BotFrameCommandSmokeIsCoopLeadAdvance(void)
{
    return SV_BotFrameCommandSmokeMode() == 27;
}

static bool SV_BotFrameCommandSmokeIsCoopResourceShare(void)
{
    return SV_BotFrameCommandSmokeMode() == 28;
}

static bool SV_BotFrameCommandSmokeIsCoopAntiBlocking(void)
{
    return SV_BotFrameCommandSmokeMode() == 29;
}

static bool SV_BotFrameCommandSmokeIsCoopTargetShare(void)
{
    return SV_BotFrameCommandSmokeMode() == 30;
}

static bool SV_BotFrameCommandSmokeIsCoopDoorElevator(void)
{
    return SV_BotFrameCommandSmokeMode() == 31;
}

static bool SV_BotFrameCommandSmokeIsTeamRoleRoute(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 32 || mode == 73;
}

static bool SV_BotFrameCommandSmokeIsTeamItemRoles(void)
{
    return SV_BotFrameCommandSmokeMode() == 33;
}

static bool SV_BotFrameCommandSmokeIsTeamResourceDenial(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 50 || mode == 89;
}

static bool SV_BotFrameCommandSmokeIsMatchItemPolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 51;
}

static bool SV_BotFrameCommandSmokeIsBehaviorPolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 52;
}

static bool SV_BotFrameCommandSmokeIsBehaviorArbitration(void)
{
    return SV_BotFrameCommandSmokeMode() == 63;
}

static bool SV_BotFrameCommandSmokeIsTargetMemory(void)
{
    return SV_BotFrameCommandSmokeMode() == 64;
}

static bool SV_BotFrameCommandSmokeIsWeaponScoring(void)
{
    return SV_BotFrameCommandSmokeMode() == 65;
}

static bool SV_BotFrameCommandSmokeUsesBehaviorPolicy(void)
{
    return SV_BotFrameCommandSmokeIsBehaviorPolicy() ||
        SV_BotFrameCommandSmokeIsBehaviorArbitration();
}

static bool SV_BotFrameCommandSmokeIsProfileRolePolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 53;
}

static bool SV_BotFrameCommandSmokeIsProfileTeamPolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 54;
}

static bool SV_BotFrameCommandSmokeIsProfileItemPolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 55;
}

static bool SV_BotFrameCommandSmokeIsProfileMovementPolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 56;
}

static bool SV_BotFrameCommandSmokeIsBotChatPolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 57;
}

static bool SV_BotFrameCommandSmokeIsBotChatTeamPolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 58;
}

static bool SV_BotFrameCommandSmokeIsBotChatRatePolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 59;
}

static bool SV_BotFrameCommandSmokeIsBotChatInitialPolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 60;
}

static bool SV_BotFrameCommandSmokeIsBotChatReplyPolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 61;
}

static bool SV_BotFrameCommandSmokeIsBotChatEventPolicy(void)
{
    return SV_BotFrameCommandSmokeMode() == 62;
}

static bool SV_BotFrameCommandSmokeIsBotChatLiveEvents(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 79 || mode == 80 || mode == 81 || mode == 82 ||
        mode == 83 || mode == 84 || mode == 85 || mode == 86 ||
        mode == 87 || mode == 88 || mode == 89 || mode == 90;
}

static bool SV_BotFrameCommandSmokeIsBotChatLiveEventCooldown(void)
{
    return SV_BotFrameCommandSmokeMode() == 80;
}

static bool SV_BotFrameCommandSmokeIsBotChatLiveEnemySighted(void)
{
    return SV_BotFrameCommandSmokeMode() == 81;
}

static bool SV_BotFrameCommandSmokeIsBotChatLiveLowHealth(void)
{
    return SV_BotFrameCommandSmokeMode() == 84;
}

static bool SV_BotFrameCommandSmokeIsBotChatLiveItemTaken(void)
{
    return SV_BotFrameCommandSmokeMode() == 85;
}

static bool SV_BotFrameCommandSmokeIsBotChatLiveItemDenied(void)
{
    return SV_BotFrameCommandSmokeMode() == 89;
}

static bool SV_BotFrameCommandSmokeIsBotChatLiveMatchResult(void)
{
    return SV_BotFrameCommandSmokeMode() == 90;
}

static bool SV_BotFrameCommandSmokeIsBotChatLiveObjectiveChanged(void)
{
    return SV_BotFrameCommandSmokeMode() == 86;
}

static bool SV_BotFrameCommandSmokeIsBotChatLiveFlagState(void)
{
    return SV_BotFrameCommandSmokeMode() == 87;
}

static bool SV_BotFrameCommandSmokeIsBotChatLiveBlocked(void)
{
    return SV_BotFrameCommandSmokeMode() == 88;
}

static bool SV_BotFrameCommandSmokeIsBotChatPhraseLibrary(void)
{
    return SV_BotFrameCommandSmokeMode() == 82;
}

static bool SV_BotFrameCommandSmokeIsBotChatDuplicateSuppression(void)
{
    return SV_BotFrameCommandSmokeMode() == 83;
}

static bool SV_BotFrameCommandSmokeUsesBotChatPolicy(void)
{
    return SV_BotFrameCommandSmokeIsBotChatPolicy() ||
        SV_BotFrameCommandSmokeIsBotChatTeamPolicy() ||
        SV_BotFrameCommandSmokeIsBotChatRatePolicy() ||
        SV_BotFrameCommandSmokeIsBotChatInitialPolicy() ||
        SV_BotFrameCommandSmokeIsBotChatReplyPolicy() ||
        SV_BotFrameCommandSmokeIsBotChatEventPolicy() ||
        SV_BotFrameCommandSmokeIsBotChatLiveEvents();
}

static bool SV_BotFrameCommandSmokeIsFfaItemRoles(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 46 || mode == 74;
}

static bool SV_BotFrameCommandSmokeIsCtfItemRoles(void)
{
    return SV_BotFrameCommandSmokeMode() == 47;
}

static bool SV_BotFrameCommandSmokeIsFfaRoleCombat(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 48 || mode == 49 || mode == 74;
}

static bool SV_BotFrameCommandSmokeIsTeamFireAvoidance(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 34 || mode == 44;
}

static bool SV_BotFrameCommandSmokeIsTeamRoleCombat(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 43 || mode == 44 || mode == 73;
}

static bool SV_BotFrameCommandSmokeIsTeamRoleCombatAvoidance(void)
{
    return SV_BotFrameCommandSmokeMode() == 44;
}

static bool SV_BotFrameCommandSmokeIsFfaRoamRoute(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 42 || mode == 45 || mode == 74;
}

static bool SV_BotFrameCommandSmokeIsFfaSpawnCampAvoidance(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 45 || mode == 49 || mode == 74;
}

static bool SV_BotFrameCommandSmokeIsFfaSpawnCampCombatAvoidance(void)
{
    const int mode = SV_BotFrameCommandSmokeMode();

    return mode == 49 || mode == 74;
}

static bool SV_BotFrameCommandSmokeIsCtfRoleRoute(void)
{
    return SV_BotFrameCommandSmokeMode() == 35;
}

static bool SV_BotFrameCommandSmokeIsCtfRoleCombat(void)
{
    return SV_BotFrameCommandSmokeMode() == 36;
}

static bool SV_BotFrameCommandSmokeIsCtfDroppedFlagRoute(void)
{
    return SV_BotFrameCommandSmokeMode() == 37;
}

static bool SV_BotFrameCommandSmokeIsCtfCarrierSupportRoute(void)
{
    return SV_BotFrameCommandSmokeMode() == 38;
}

static bool SV_BotFrameCommandSmokeIsCtfBaseReturnRoute(void)
{
    return SV_BotFrameCommandSmokeMode() == 39;
}

static bool SV_BotFrameCommandSmokeIsCtfObjectiveRoute(void)
{
    return SV_BotFrameCommandSmokeMode() == 40;
}

static bool SV_BotFrameCommandSmokeIsCtfObjectiveRoutePrecedence(void)
{
    return SV_BotFrameCommandSmokeMode() == 41;
}

static bool SV_BotFrameCommandSmokeIsHazardContext(void)
{
    return SV_BotFrameCommandSmokeMode() == 96;
}

static bool SV_BotFrameCommandSmokeIsCrouchRoute(void)
{
    return SV_BotFrameCommandSmokeMode() == 92;
}

static bool SV_BotFrameCommandSmokeUsesScenarioCvars(void)
{
    return SV_BotFrameCommandSmokeIsEngageEnemy() ||
        SV_BotFrameCommandSmokeIsSwitchWeapons() ||
        SV_BotFrameCommandSmokeIsHealthArmorPickup() ||
        SV_BotFrameCommandSmokeIsAmmoPressure() ||
        SV_BotFrameCommandSmokeIsSurvivalInventory() ||
        SV_BotFrameCommandSmokeIsSurvivalRoute() ||
        SV_BotFrameCommandSmokeIsThreatRetreat() ||
        SV_BotFrameCommandSmokeIsTeamObjective() ||
        SV_BotFrameCommandSmokeIsAimFairness() ||
        SV_BotFrameCommandSmokeIsAimFirePolicy() ||
        SV_BotFrameCommandSmokeIsItemTimer() ||
        SV_BotFrameCommandSmokeIsMatchReadiness() ||
        SV_BotFrameCommandSmokeIsCoopLeadAdvance() ||
        SV_BotFrameCommandSmokeIsCoopResourceShare() ||
        SV_BotFrameCommandSmokeIsCoopAntiBlocking() ||
        SV_BotFrameCommandSmokeIsCoopTargetShare() ||
        SV_BotFrameCommandSmokeIsCoopDoorElevator() ||
        SV_BotFrameCommandSmokeIsCoopLiveLoop() ||
        SV_BotFrameCommandSmokeIsCoopShareLoop() ||
        SV_BotFrameCommandSmokeIsDuelLivePacing() ||
        SV_BotFrameCommandSmokeIsFfaRoamRoute() ||
        SV_BotFrameCommandSmokeIsFfaRoleCombat() ||
        SV_BotFrameCommandSmokeIsFfaItemRoles() ||
        SV_BotFrameCommandSmokeIsTeamRoleRoute() ||
        SV_BotFrameCommandSmokeIsTeamItemRoles() ||
        SV_BotFrameCommandSmokeIsTeamResourceDenial() ||
        SV_BotFrameCommandSmokeIsMatchItemPolicy() ||
        SV_BotFrameCommandSmokeUsesBehaviorPolicy() ||
        SV_BotFrameCommandSmokeIsProfileRolePolicy() ||
        SV_BotFrameCommandSmokeIsProfileTeamPolicy() ||
        SV_BotFrameCommandSmokeIsProfileItemPolicy() ||
        SV_BotFrameCommandSmokeIsProfileMovementPolicy() ||
        SV_BotFrameCommandSmokeUsesBotChatPolicy() ||
        SV_BotFrameCommandSmokeIsTargetMemory() ||
        SV_BotFrameCommandSmokeIsWeaponScoring() ||
        SV_BotFrameCommandSmokeIsTeamFireAvoidance() ||
        SV_BotFrameCommandSmokeIsTeamRoleCombat() ||
        SV_BotFrameCommandSmokeIsCtfRoleRoute() ||
        SV_BotFrameCommandSmokeIsCtfRoleCombat() ||
        SV_BotFrameCommandSmokeIsCtfDroppedFlagRoute() ||
        SV_BotFrameCommandSmokeIsCtfCarrierSupportRoute() ||
        SV_BotFrameCommandSmokeIsCtfBaseReturnRoute() ||
        SV_BotFrameCommandSmokeIsCtfObjectiveRoute() ||
        SV_BotFrameCommandSmokeIsCtfObjectiveTransitions() ||
        SV_BotFrameCommandSmokeIsCtfObjectiveRoutePrecedence() ||
        SV_BotFrameCommandSmokeIsCtfItemRoles() ||
        SV_BotFrameCommandSmokeIsCrouchRoute() ||
        SV_BotFrameCommandSmokeIsHazardContext();
}

static int SV_BotFrameCommandSmokeSoakMilliseconds(void)
{
    int duration_ms = bot_frame_command_smoke_soak_ms ?
        bot_frame_command_smoke_soak_ms->integer : 600000;

    if (duration_ms < 1000) {
        duration_ms = 1000;
    }

    return duration_ms;
}

static int SV_BotFrameCommandSmokeSoakProgressMilliseconds(void)
{
    const int duration_ms = SV_BotFrameCommandSmokeSoakMilliseconds();
    int progress_ms = duration_ms / 4;

    if (progress_ms < 1000) {
        progress_ms = 1000;
    }
    if (progress_ms > 60000) {
        progress_ms = 60000;
    }

    return progress_ms;
}

static int SV_BotFrameCommandSmokeMapRepeatCycles(void)
{
    int cycles = bot_frame_command_smoke_map_repeat_cycles ?
        bot_frame_command_smoke_map_repeat_cycles->integer : 2;

    if (cycles < 2) {
        cycles = 2;
    }
    if (cycles > 8) {
        cycles = 8;
    }

    return cycles;
}

static int SV_BotFrameCommandSmokeMapRepeatReloadTimeoutMilliseconds(void)
{
    int timeout_ms = bot_frame_command_smoke_map_repeat_reload_timeout_ms ?
        bot_frame_command_smoke_map_repeat_reload_timeout_ms->integer :
        10000;

    if (timeout_ms < 1000) {
        timeout_ms = 1000;
    }
    if (timeout_ms > 120000) {
        timeout_ms = 120000;
    }

    return timeout_ms;
}

static bool SV_BotFrameCommandSmokeMapRepeatRestartReload(void)
{
    return bot_frame_command_smoke_map_repeat_restart &&
        bot_frame_command_smoke_map_repeat_restart->integer > 0;
}

static const char *SV_BotFrameCommandSmokeMapRepeatReloadCommand(void)
{
    return SV_BotFrameCommandSmokeMapRepeatRestartReload() ?
        "map_force" : "gamemap";
}

static unsigned SV_BotFrameCommandSmokeElapsedMilliseconds(
    unsigned start_realtime,
    bool *realtime_reset)
{
    if (realtime_reset) {
        *realtime_reset = false;
    }

    if (svs.realtime < start_realtime) {
        if (realtime_reset) {
            *realtime_reset = true;
        }
        return 0;
    }

    return svs.realtime - start_realtime;
}

static const char *SV_BotFrameCommandSmokeMapRepeatPhase(int completed_cycles)
{
    return completed_cycles <= 0 ? "pre_reload" : "post_reload";
}

static const char *SV_BotFrameCommandSmokeMapRepeatCyclePhase(int cycle)
{
    return cycle <= 1 ? "pre_reload" : "post_reload";
}

static bool SV_BotFrameCommandSmokeUsesPositionGoal(void)
{
    return SV_BotFrameCommandSmokeMode() == 8;
}

static bool SV_BotFrameCommandSmokeUsesTravelTypeGoal(void)
{
    switch (SV_BotFrameCommandSmokeMode()) {
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 31:
    case 77:
    case 91:
    case 88:
    case 92:
    case 93:
    case 94:
    case 95:
        return true;
    default:
        return false;
    }
}

static int SV_BotFrameCommandSmokeTargetBots(void)
{
    if (SV_BotFrameCommandSmokeIsMatchReadiness()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsTeamObjective()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsDuelLivePacing()) {
        return min(2, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsFfaRoamRoute()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsFfaItemRoles()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsFfaRoleCombat()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsTargetMemory() ||
        SV_BotFrameCommandSmokeIsAimFirePolicy() ||
        SV_BotFrameCommandSmokeIsWeaponScoring() ||
        SV_BotFrameCommandSmokeIsCombatSurvivalRegression() ||
        SV_BotFrameCommandSmokeIsThreatRetreat()) {
        return min(2, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsTeamRoleRoute()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsTeamItemRoles()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsTeamResourceDenial()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsMatchItemPolicy()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeUsesBehaviorPolicy()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsProfileRolePolicy()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsProfileTeamPolicy()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsProfileItemPolicy()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsProfileMovementPolicy()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsBotChatLiveEnemySighted()) {
        return min(2, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsBotChatLiveLowHealth()) {
        return min(1, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsBotChatLiveItemTaken()) {
        return min(1, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsBotChatLiveObjectiveChanged() ||
        SV_BotFrameCommandSmokeIsBotChatLiveFlagState()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsBotChatLiveBlocked()) {
        return min(1, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeUsesBotChatPolicy()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsTeamFireAvoidance()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsTeamRoleCombat()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsCtfRoleRoute() ||
        SV_BotFrameCommandSmokeIsCtfRoleCombat() ||
        SV_BotFrameCommandSmokeIsCtfDroppedFlagRoute() ||
        SV_BotFrameCommandSmokeIsCtfCarrierSupportRoute() ||
        SV_BotFrameCommandSmokeIsCtfBaseReturnRoute() ||
        SV_BotFrameCommandSmokeIsCtfObjectiveRoute() ||
        SV_BotFrameCommandSmokeIsCtfObjectiveTransitions() ||
        SV_BotFrameCommandSmokeIsCtfObjectiveRoutePrecedence() ||
        SV_BotFrameCommandSmokeIsCtfItemRoles()) {
        return min(4, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsAimFairness() ||
        SV_BotFrameCommandSmokeIsEngageEnemy() ||
        SV_BotFrameCommandSmokeIsSwitchWeapons()) {
        return min(2, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeIsHealthArmorPickup() ||
        SV_BotFrameCommandSmokeIsAmmoPressure() ||
        SV_BotFrameCommandSmokeIsSurvivalInventory() ||
        SV_BotFrameCommandSmokeIsSurvivalRoute() ||
        SV_BotFrameCommandSmokeIsItemTimer() ||
        SV_BotFrameCommandSmokeIsHazardContext() ||
        SV_BotFrameCommandSmokeIsCoopLeadAdvance()) {
        return 1;
    }

    if (SV_BotFrameCommandSmokeIsCoopResourceShare() ||
        SV_BotFrameCommandSmokeIsCoopTargetShare() ||
        SV_BotFrameCommandSmokeIsCoopAntiBlocking() ||
        SV_BotFrameCommandSmokeIsCoopDoorElevator() ||
        SV_BotFrameCommandSmokeIsCoopLiveLoop() ||
        SV_BotFrameCommandSmokeIsCoopShareLoop()) {
        return min(2, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeUsesPositionGoal() ||
        SV_BotFrameCommandSmokeUsesTravelTypeGoal()) {
        return 1;
    }

    if (SV_BotFrameCommandSmokeIsSoak() ||
        SV_BotFrameCommandSmokeIsMapRepeat() ||
        SV_BotFrameCommandSmokeMode() == 17) {
        return min(8, bot_public_client_limit());
    }

    if (SV_BotFrameCommandSmokeMode() == 16) {
        return min(4, bot_public_client_limit());
    }

    return SV_BotFrameCommandSmokeMode() >= 3 ?
        min(2, bot_public_client_limit()) : 1;
}

static const char *SV_BotFrameCommandSmokeBotName(int index)
{
    static const char *profile_names[] = {
        "smoke",
        "bulwark",
        "relay",
        "vanguard"
    };
    static const char *names[] = {
        "Mover",
        "MoverTwo",
        "MoverThree",
        "MoverFour",
        "MoverFive",
        "MoverSix",
        "MoverSeven",
        "MoverEight"
    };

    if (SV_BotFrameCommandSmokeIsProfileRolePolicy() ||
        SV_BotFrameCommandSmokeIsProfileTeamPolicy() ||
        SV_BotFrameCommandSmokeIsProfileItemPolicy() ||
        SV_BotFrameCommandSmokeIsProfileMovementPolicy() ||
        SV_BotFrameCommandSmokeUsesBotChatPolicy()) {
        if (index >= 0 && index < q_countof(profile_names)) {
            return profile_names[index];
        }
        return NULL;
    }

    if (index >= 0 && index < q_countof(names)) {
        return names[index];
    }

    return NULL;
}

static int SV_BotFrameCommandSmokeForcedTravelType(void)
{
    switch (SV_BotFrameCommandSmokeMode()) {
    case 5:
        return 5; /* TRAVEL_JUMP */
    case 6:
        return 3; /* TRAVEL_CROUCH */
    case 7:
        return 8; /* TRAVEL_SWIM */
    default:
        return 0;
    }
}

static int SV_BotFrameCommandSmokeTravelTypeGoal(void)
{
    switch (SV_BotFrameCommandSmokeMode()) {
    case 9:
        return 5; /* TRAVEL_JUMP */
    case 10:
        return 6; /* TRAVEL_LADDER */
    case 11:
        return 7; /* TRAVEL_WALKOFFLEDGE */
    case 12:
    case 31:
    case 77:
        return 11; /* TRAVEL_ELEVATOR */
    case 13:
        return 4; /* TRAVEL_BARRIERJUMP */
    case 14:
    case 15:
    case 88:
        return 12; /* TRAVEL_ROCKETJUMP */
    case 92:
        return 3; /* TRAVEL_CROUCH */
    case 93:
        return 8; /* TRAVEL_SWIM */
    case 94:
        return 9; /* TRAVEL_WATERJUMP */
    case 95:
        return 10; /* TRAVEL_TELEPORT */
    default:
        return 0;
    }
}

static bool SV_BotFrameCommandSmokeAllowsRocketJump(void)
{
    return SV_BotFrameCommandSmokeMode() == 14;
}

static bool SV_BotFrameCommandSmokeExpectsBlockedTravelTypeGoal(void)
{
    return SV_BotFrameCommandSmokeMode() == 15 ||
        SV_BotFrameCommandSmokeIsBotChatLiveBlocked();
}

static int SV_BotFrameCommandSmokeExpectedCommands(int target_bots)
{
    if (SV_BotFrameCommandSmokeExpectsBlockedTravelTypeGoal()) {
        return 0;
    }

    return target_bots;
}

static int SV_BotFrameCommandSmokeSettleFrames(void)
{
    if (SV_BotFrameCommandSmokeStallsMovement()) {
        return 14;
    }

    if (SV_BotFrameCommandSmokeMode() == 12) {
        return 24;
    }

    if (SV_BotFrameCommandSmokeUsesScenarioCvars()) {
        return 60;
    }

    return 8;
}

static int SV_BotFrameCommandSmokeRuntimeWarmupLimit(void)
{
    if (SV_BotFrameCommandSmokeUsesScenarioCvars()) {
        return 120;
    }

    return SV_BotFrameCommandSmokeSettleFrames();
}

static void SV_BotFrameCommandStatusPrintCaptured(const char *captured_status)
{
    const char *cursor;
    size_t remaining;

    if (!captured_status || !captured_status[0]) {
        return;
    }

    cursor = captured_status;
    remaining = strlen(captured_status);
    while (remaining > 0) {
        const char *line_end = memchr(cursor, '\n', remaining);
        size_t line_len = line_end ?
            (size_t)(line_end - cursor) + 1 :
            remaining;

        Com_Printf("%.*s", (int)line_len, cursor);
        cursor += line_len;
        remaining -= line_len;
    }

    if (bot_frame_command_status_capture_len == 0 ||
        captured_status[bot_frame_command_status_capture_len - 1] != '\n') {
        Com_Printf("\n");
    }
}

static bool SV_BotFrameCommandSmokeStatusWaitingForRuntime(
    const char *captured_status)
{
    int commands;
    int skipped_runtime;

    if (!SV_BotFrameCommandSmokeUsesScenarioCvars()) {
        return false;
    }

    if (!SV_BotFrameCommandStatusReadInt(
            captured_status, "commands", &commands) ||
        !SV_BotFrameCommandStatusReadInt(
            captured_status, "skipped_runtime", &skipped_runtime)) {
        return false;
    }

    return commands <= 0 && skipped_runtime > 0;
}

static void SV_BotRunFrameCommands(void)
{
    const bot_frame_command_api_v1_t *api;
    int limit;

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    api = SV_BotFrameCommandApi();
    if (!api) {
        return;
    }

    limit = bot_client_pool_limit();
    for (int i = 0; i < limit; i++) {
        client_t *cl = &svs.client_pool[i];
        usercmd_t cmd;

        if (!cl->bot || cl->state != cs_spawned || !cl->edict) {
            continue;
        }

        memset(&cmd, 0, sizeof(cmd));
        if (api->BuildCommand(cl->edict, &cmd)) {
            if (SV_BotFrameCommandSmokeStallsMovement()) {
                continue;
            }
            SV_BotClientThink(cl, &cmd);
        }
    }
}

static void SV_BotFrameCommandSmokeResetRuntimeCvars(void)
{
    Cvar_Set("bot_enable", "0");
    Cvar_Set("bot_min_players", "0");
    Cvar_Set("bot_frame_command_smoke_travel_type", "0");
    Cvar_Set("bot_frame_command_smoke_combat", "0");
    Cvar_Set("bot_frame_command_smoke_weapon_switch", "0");
    Cvar_Set("bot_frame_command_smoke_item_focus", "0");
    Cvar_Set("bot_frame_command_smoke_team_objective", "0");
    Cvar_Set("bot_frame_command_smoke_aim_fairness", "0");
    Cvar_Set("bot_frame_command_smoke_item_timer", "0");
    Cvar_Set("bot_frame_command_smoke_match_readiness", "0");
    Cvar_Set("bot_frame_command_smoke_survival_inventory", "0");
    Cvar_Set("bot_frame_command_smoke_survival_route", "0");
    Cvar_Set("bot_threat_retreat", "0");
    Cvar_Set("bot_nav_position_goal_enable", "0");
    Cvar_Set("bot_nav_travel_type_goal", "0");
    Cvar_Set("bot_nav_travel_type_goal_warp", "0");
    Cvar_Set("bot_nav_travel_type_goal_expect_blocked", "0");
    Cvar_Set("bot_frame_command_smoke_soak", "0");
    Cvar_Set("bot_allow_rocketjump", "0");
    Cvar_Set("bot_behavior_enable", "0");
    Cvar_Set("bot_frame_command_smoke_target_memory", "0");
    Cvar_Set("bot_coop_progress_wait", "0");
    Cvar_Set("bot_coop_interaction_retry", "0");
    Cvar_Set("bot_coop_lead_advance", "0");
    Cvar_Set("bot_coop_resource_share", "0");
    Cvar_Set("bot_coop_anti_blocking", "0");
    Cvar_Set("bot_coop_target_share", "0");
    Cvar_Set("bot_coop_door_elevator", "0");
    Cvar_Set("bot_coop_live_loop", "0");
    Cvar_Set("bot_coop_share_loop", "0");
    Cvar_Set("bot_ffa_roam_route", "0");
    Cvar_Set("bot_ffa_spawn_camp_avoidance", "0");
    Cvar_Set("bot_ffa_spawn_camp_combat_avoidance", "0");
    Cvar_Set("bot_ffa_item_roles", "0");
    Cvar_Set("bot_ffa_role_combat", "0");
    Cvar_Set("bot_duel_live_pacing", "0");
    Cvar_Set("bot_match_item_policy", "0");
    Cvar_Set("bot_allow_chat", "0");
    Cvar_Set("bot_chat_team_only", "0");
    Cvar_Set("bot_chat_min_interval_ms", "0");
    Cvar_Set("bot_chat_reply_policy_smoke", "0");
    Cvar_Set("bot_chat_event_policy_smoke", "0");
    Cvar_Set("bot_chat_live_events", "0");
    Cvar_Set("bot_team_role_route", "0");
    Cvar_Set("bot_team_item_roles", "0");
    Cvar_Set("bot_team_resource_denial", "0");
    Cvar_Set("bot_team_fire_avoidance", "0");
    Cvar_Set("bot_team_role_combat", "0");
    Cvar_Set("bot_ctf_role_route", "0");
    Cvar_Set("bot_ctf_role_combat", "0");
    Cvar_Set("bot_ctf_dropped_flag_route", "0");
    Cvar_Set("bot_ctf_carrier_support_route", "0");
    Cvar_Set("bot_ctf_base_return_route", "0");
    Cvar_Set("bot_ctf_objective_route", "0");
    Cvar_Set("bot_ctf_objective_transitions", "0");
    Cvar_Set("bot_ctf_item_roles", "0");
}

static int SV_BotFrameCommandSmokeMapRepeatCleanupStatus(int cycle,
                                                         const char *phase,
                                                         const char *reason)
{
    const char *captured_status;
    int active_reservations;
    int pass;

    Com_Printf("q3a_bot_frame_command_smoke_map_repeat_cleanup_status_requested "
               "cycle=%d phase=%s reason=%s count=%d status_line=next\n",
               cycle, phase, reason, SV_BotCount());

    captured_status = SV_BotFrameCommandStatusCapture(0, 0);
    SV_BotFrameCommandStatusPrintCaptured(captured_status);

    pass = SV_BotFrameCommandStatusCapturedCleanupPass(
        captured_status, SV_BotCount(), &active_reservations);

    Com_Printf("\nq3a_bot_frame_command_smoke_map_repeat_cleanup_status "
               "cycle=%d phase=%s reason=%s count=%d "
               "active_reservations=%d pass=%d status_line=previous\n",
               cycle, phase, reason, SV_BotCount(), active_reservations,
               pass);

    return pass;
}

static void SV_BotFrameCommandSmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    static int settle_frames;
    static bool match_result_intermission_requested;
    static bool soak_active;
    static unsigned soak_start_realtime;
    static unsigned soak_last_progress_realtime;
    static int soak_progress_reports;
    static int map_repeat_cycle;
    static int map_repeat_completed_cycles;
    static int map_repeat_map_changes;
    static bool map_repeat_reload_pending;
    static int map_repeat_reload_spawncount;
    static unsigned map_repeat_reload_request_realtime;
    static char map_repeat_map[MAX_QPATH];
    int target_bots;
    int forced_travel_type;
    int travel_type_goal;
    bool added;

    if (!bot_frame_command_smoke ||
        bot_frame_command_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game || !ge) {
        return;
    }

    target_bots = SV_BotFrameCommandSmokeTargetBots();
    if (target_bots < 1) {
        target_bots = 1;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
        settle_frames = 0;
        match_result_intermission_requested = false;
        soak_active = false;
        soak_start_realtime = 0;
        soak_last_progress_realtime = 0;
        soak_progress_reports = 0;
        if (SV_BotFrameCommandSmokeIsMapRepeat()) {
            if (map_repeat_reload_pending) {
                bool reload_realtime_reset;
                const unsigned reload_elapsed_ms =
                    SV_BotFrameCommandSmokeElapsedMilliseconds(
                        map_repeat_reload_request_realtime,
                        &reload_realtime_reset);
                const int reload_timeout_ms =
                    SV_BotFrameCommandSmokeMapRepeatReloadTimeoutMilliseconds();

                map_repeat_reload_pending = false;
                map_repeat_cycle++;
                Com_Printf("q3a_bot_frame_command_smoke_map_repeat_reloaded "
                           "cycle=%d completed_cycles=%d map_changes=%d "
                           "old_spawncount=%d new_spawncount=%d map=%s\n",
                           map_repeat_cycle, map_repeat_completed_cycles,
                           map_repeat_map_changes,
                           map_repeat_reload_spawncount, sv.spawncount,
                           sv.name);
                Com_Printf("q3a_bot_frame_command_smoke_map_repeat_reload=observed "
                           "cycle=%d phase=%s completed_cycles=%d "
                           "map_changes=%d old_spawncount=%d "
                           "new_spawncount=%d elapsed_ms=%u "
                           "realtime_reset=%d timeout_ms=%d map=%s "
                           "command=%s restart=%d\n",
                           map_repeat_cycle,
                           SV_BotFrameCommandSmokeMapRepeatPhase(
                               map_repeat_completed_cycles),
                           map_repeat_completed_cycles,
                           map_repeat_map_changes,
                           map_repeat_reload_spawncount, sv.spawncount,
                           reload_elapsed_ms, reload_realtime_reset ? 1 : 0,
                           reload_timeout_ms, sv.name,
                           SV_BotFrameCommandSmokeMapRepeatReloadCommand(),
                           SV_BotFrameCommandSmokeMapRepeatRestartReload() ?
                               1 : 0);
                map_repeat_reload_request_realtime = 0;
            } else {
                map_repeat_cycle = 1;
                map_repeat_completed_cycles = 0;
                map_repeat_map_changes = 0;
                map_repeat_reload_spawncount = 0;
                map_repeat_reload_request_realtime = 0;
                map_repeat_map[0] = 0;
            }
        } else {
            map_repeat_cycle = 0;
            map_repeat_completed_cycles = 0;
            map_repeat_map_changes = 0;
            map_repeat_reload_pending = false;
            map_repeat_reload_spawncount = 0;
            map_repeat_reload_request_realtime = 0;
            map_repeat_map[0] = 0;
        }
    } else if (stage > 2) {
        if (SV_BotFrameCommandSmokeIsMapRepeat() &&
            map_repeat_reload_pending) {
            const int timeout_ms =
                SV_BotFrameCommandSmokeMapRepeatReloadTimeoutMilliseconds();
            bool realtime_reset;
            const unsigned elapsed_ms =
                SV_BotFrameCommandSmokeElapsedMilliseconds(
                    map_repeat_reload_request_realtime, &realtime_reset);

            if (!realtime_reset && elapsed_ms >= (unsigned)timeout_ms) {
                const int removed = SV_BotRemoveAll();

                SV_BotFrameCommandSmokeResetRuntimeCvars();
                map_repeat_reload_pending = false;
                map_repeat_reload_request_realtime = 0;
                Com_Printf("q3a_bot_frame_command_smoke_map_repeat_reload=timeout "
                           "cycle=%d next_cycle=%d completed_cycles=%d "
                           "target_cycles=%d map_changes=%d "
                           "from_spawncount=%d current_spawncount=%d "
                           "elapsed_ms=%u realtime_reset=%d timeout_ms=%d "
                           "map=%s command=%s restart=%d removed=%d pass=0\n",
                           map_repeat_cycle, map_repeat_cycle + 1,
                           map_repeat_completed_cycles,
                           SV_BotFrameCommandSmokeMapRepeatCycles(),
                           map_repeat_map_changes,
                           map_repeat_reload_spawncount, sv.spawncount,
                           elapsed_ms, realtime_reset ? 1 : 0, timeout_ms,
                           map_repeat_map,
                           SV_BotFrameCommandSmokeMapRepeatReloadCommand(),
                           SV_BotFrameCommandSmokeMapRepeatRestartReload() ?
                               1 : 0,
                           removed);
                Com_Printf("q3a_bot_frame_command_smoke_map_repeat_cleanup "
                           "cycle=%d phase=reload_wait "
                           "reason=reload_timeout final_count=%d\n",
                           map_repeat_cycle, SV_BotCount());
                Com_Printf("q3a_bot_frame_command_smoke_map_repeat=failed "
                           "reason=reload_timeout cycles=%d map_changes=%d "
                           "final_map=%s final_spawncount=%d "
                           "final_count=%d pass=0\n",
                           map_repeat_completed_cycles,
                           map_repeat_map_changes, sv.name, sv.spawncount,
                           SV_BotCount());
                stage = 4;
                if (bot_frame_command_smoke->integer >= 2) {
                    Com_Quit(NULL, ERR_DISCONNECT);
                }
            }
        }
        return;
    }

    if (stage == 0) {
        SV_BotRemoveAll();
        Cvar_Set("bot_enable", "1");
        Cvar_Set("bot_min_players", "0");
        Cvar_Set("g_gametype",
                 SV_BotFrameCommandSmokeIsDuelLivePacing() ? "2" :
                 (SV_BotFrameCommandSmokeIsFfaRoamRoute() ||
                 SV_BotFrameCommandSmokeIsFfaRoleCombat() ||
                 SV_BotFrameCommandSmokeIsAimFirePolicy() ||
                  SV_BotFrameCommandSmokeIsBotChatLiveItemTaken() ||
                  SV_BotFrameCommandSmokeIsBotChatLiveBlocked() ||
                  SV_BotFrameCommandSmokeIsAmmoPressure() ||
                  SV_BotFrameCommandSmokeIsSurvivalInventory() ||
                  SV_BotFrameCommandSmokeIsSurvivalRoute() ||
                  SV_BotFrameCommandSmokeIsThreatRetreat() ||
                  SV_BotFrameCommandSmokeIsTargetMemory() ||
                  SV_BotFrameCommandSmokeIsFfaItemRoles()) ? "1" :
                 ((SV_BotFrameCommandSmokeIsMatchReadiness() ||
                 SV_BotFrameCommandSmokeIsTeamRoleRoute() ||
                 SV_BotFrameCommandSmokeIsTeamItemRoles() ||
                  SV_BotFrameCommandSmokeIsTeamResourceDenial() ||
                  SV_BotFrameCommandSmokeIsMatchItemPolicy() ||
                  SV_BotFrameCommandSmokeIsProfileItemPolicy() ||
                  SV_BotFrameCommandSmokeIsProfileMovementPolicy() ||
                  (SV_BotFrameCommandSmokeUsesBotChatPolicy() &&
                   !SV_BotFrameCommandSmokeIsBotChatLiveObjectiveChanged() &&
                   !SV_BotFrameCommandSmokeIsBotChatLiveFlagState() &&
                   !SV_BotFrameCommandSmokeIsBotChatLiveBlocked()) ||
                  SV_BotFrameCommandSmokeUsesBehaviorPolicy() ||
                  SV_BotFrameCommandSmokeIsProfileRolePolicy() ||
                 SV_BotFrameCommandSmokeIsTeamFireAvoidance() ||
                 SV_BotFrameCommandSmokeIsTeamRoleCombat()) ? "3" :
                 ((SV_BotFrameCommandSmokeIsCtfRoleRoute() ||
                   SV_BotFrameCommandSmokeIsCtfRoleCombat() ||
                   SV_BotFrameCommandSmokeIsCtfDroppedFlagRoute() ||
                   SV_BotFrameCommandSmokeIsCtfCarrierSupportRoute() ||
                   SV_BotFrameCommandSmokeIsCtfBaseReturnRoute() ||
                   SV_BotFrameCommandSmokeIsCtfObjectiveRoute() ||
                   SV_BotFrameCommandSmokeIsCtfObjectiveTransitions() ||
                   SV_BotFrameCommandSmokeIsCtfObjectiveRoutePrecedence() ||
                   SV_BotFrameCommandSmokeIsProfileTeamPolicy() ||
                   SV_BotFrameCommandSmokeIsCtfItemRoles()) ? "5" :
                  (SV_BotFrameCommandSmokeIsTeamObjective() ? "1" : "0"))));
        Cvar_Set("bot_allow_rocketjump",
                 SV_BotFrameCommandSmokeAllowsRocketJump() ? "1" : "0");
        Cvar_Set("bot_allow_chat",
                 SV_BotFrameCommandSmokeUsesBotChatPolicy() ? "1" : "0");
        Cvar_Set("bot_chat_team_only",
                 SV_BotFrameCommandSmokeIsBotChatTeamPolicy() ? "1" : "0");
        Cvar_Set("bot_chat_min_interval_ms",
                 (SV_BotFrameCommandSmokeIsBotChatRatePolicy() ||
                  SV_BotFrameCommandSmokeIsBotChatLiveEventCooldown()) ?
                     "60000" : "0");
        Cvar_Set("bot_chat_reply_policy_smoke",
                 SV_BotFrameCommandSmokeIsBotChatReplyPolicy() ? "1" : "0");
        Cvar_Set("bot_chat_event_policy_smoke",
                 (SV_BotFrameCommandSmokeIsBotChatEventPolicy() ||
                  SV_BotFrameCommandSmokeIsBotChatDuplicateSuppression()) ?
                     "1" : "0");
        Cvar_Set("bot_chat_live_events",
                 SV_BotFrameCommandSmokeIsBotChatLiveEvents() ? "1" : "0");
        Cvar_Set("bot_nav_travel_type_goal_expect_blocked",
                 SV_BotFrameCommandSmokeExpectsBlockedTravelTypeGoal() ? "1" : "0");
        Cvar_Set("bot_frame_command_smoke_soak",
                 SV_BotFrameCommandSmokeIsSoak() ? "1" : "0");
        Com_Printf("q3a_bot_frame_command_smoke=begin\n");
        if (SV_BotFrameCommandSmokeAllowsRocketJump()) {
            Com_Printf("q3a_bot_frame_command_smoke_allow_rocketjump=1\n");
        }
        if (SV_BotFrameCommandSmokeExpectsBlockedTravelTypeGoal()) {
            Com_Printf("q3a_bot_frame_command_smoke_travel_type_goal_expect_blocked=1\n");
        }
        if (SV_BotFrameCommandSmokeStallsMovement()) {
            Com_Printf("q3a_bot_frame_command_smoke_stall_commands=1\n");
        }
        if (target_bots > 2) {
            Com_Printf("q3a_bot_frame_command_smoke_multi_bot_target=%d\n",
                       target_bots);
        }
        if (SV_BotFrameCommandSmokeIsSoak()) {
            Com_Printf("q3a_bot_frame_command_smoke_soak_ms=%d\n",
                       SV_BotFrameCommandSmokeSoakMilliseconds());
        }
        if (SV_BotFrameCommandSmokeIsMapRepeat()) {
            Com_Printf("q3a_bot_frame_command_smoke_map_repeat_cycle=begin "
                       "cycle=%d phase=%s target_cycles=%d completed_cycles=%d "
                       "map_changes=%d map=%s spawncount=%d command=%s "
                       "restart=%d\n",
                       map_repeat_cycle,
                       SV_BotFrameCommandSmokeMapRepeatPhase(
                           map_repeat_completed_cycles),
                       SV_BotFrameCommandSmokeMapRepeatCycles(),
                       map_repeat_completed_cycles,
                       map_repeat_map_changes,
                       sv.name,
                       sv.spawncount,
                       SV_BotFrameCommandSmokeMapRepeatReloadCommand(),
                       SV_BotFrameCommandSmokeMapRepeatRestartReload() ? 1 : 0);
        }
        if (SV_BotFrameCommandSmokeUsesScenarioCvars()) {
            const char *combat_mode =
                SV_BotFrameCommandSmokeIsAimFirePolicy() ?
                    "aim_fire_policy" :
                SV_BotFrameCommandSmokeIsWeaponScoring() ?
                    "weapon_scoring" :
                (SV_BotFrameCommandSmokeIsEngageEnemy() ||
                 SV_BotFrameCommandSmokeIsCombatSurvivalRegression() ||
                 SV_BotFrameCommandSmokeIsAimFairness() ||
                 (SV_BotFrameCommandSmokeIsTeamFireAvoidance() &&
                  !SV_BotFrameCommandSmokeIsTeamRoleCombatAvoidance())) ?
                    "engage_enemy" :
                (SV_BotFrameCommandSmokeIsSwitchWeapons() ?
                    "switch_weapons" : "0");
            const char *item_focus =
                SV_BotFrameCommandSmokeIsAmmoPressure() ?
                    "ammo" :
                SV_BotFrameCommandSmokeIsHealthArmorPickup() ?
                    "health_armor" : "0";
            const int weapon_switch =
                (SV_BotFrameCommandSmokeIsSwitchWeapons() ||
                 SV_BotFrameCommandSmokeIsWeaponScoring()) ? 1 : 0;
            const int team_objective =
                SV_BotFrameCommandSmokeIsTeamObjective() ? 1 : 0;
            const int aim_fairness =
                (SV_BotFrameCommandSmokeIsAimFairness() ||
                 SV_BotFrameCommandSmokeIsAimFirePolicy()) ? 1 : 0;
            const int aim_fire_policy =
                SV_BotFrameCommandSmokeIsAimFirePolicy() ? 1 : 0;
            const int item_timer =
                SV_BotFrameCommandSmokeIsItemTimer() ? 1 : 0;
            const int match_readiness =
                SV_BotFrameCommandSmokeIsMatchReadiness() ? 1 : 0;
            const int target_share =
                SV_BotFrameCommandSmokeIsCoopTargetShare() ? 1 : 0;
            const int door_elevator =
                SV_BotFrameCommandSmokeIsCoopDoorElevator() ? 1 : 0;
            const int coop_live_loop =
                SV_BotFrameCommandSmokeIsCoopLiveLoop() ? 1 : 0;
            const int coop_share_loop =
                SV_BotFrameCommandSmokeIsCoopShareLoop() ? 1 : 0;
            const int ffa_roam_route =
                SV_BotFrameCommandSmokeIsFfaRoamRoute() ? 1 : 0;
            const int ffa_spawn_camp_avoidance =
                SV_BotFrameCommandSmokeIsFfaSpawnCampAvoidance() ? 1 : 0;
            const int ffa_spawn_camp_combat_avoidance =
                SV_BotFrameCommandSmokeIsFfaSpawnCampCombatAvoidance() ? 1 : 0;
            const int ffa_item_roles =
                SV_BotFrameCommandSmokeIsFfaItemRoles() ? 1 : 0;
            const int ffa_role_combat =
                SV_BotFrameCommandSmokeIsFfaRoleCombat() ? 1 : 0;
            const int ffa_live_pacing =
                SV_BotFrameCommandSmokeIsFfaLivePacing() ? 1 : 0;
            const int duel_live_pacing =
                SV_BotFrameCommandSmokeIsDuelLivePacing() ? 1 : 0;
            const int team_role_route =
                SV_BotFrameCommandSmokeIsTeamRoleRoute() ? 1 : 0;
            const int team_item_roles =
                SV_BotFrameCommandSmokeIsTeamItemRoles() ? 1 : 0;
            const int team_resource_denial =
                SV_BotFrameCommandSmokeIsTeamResourceDenial() ? 1 : 0;
            const int match_item_policy =
                (SV_BotFrameCommandSmokeIsMatchItemPolicy() ||
                 SV_BotFrameCommandSmokeIsProfileItemPolicy()) ? 1 : 0;
            const int behavior_policy =
                SV_BotFrameCommandSmokeUsesBehaviorPolicy() ? 1 : 0;
            const int behavior_arbitration =
                SV_BotFrameCommandSmokeIsBehaviorArbitration() ? 1 : 0;
            const int target_memory =
                SV_BotFrameCommandSmokeIsTargetMemory() ? 1 : 0;
            const int weapon_scoring =
                SV_BotFrameCommandSmokeIsWeaponScoring() ? 1 : 0;
            const int ammo_pressure =
                SV_BotFrameCommandSmokeIsAmmoPressure() ? 1 : 0;
            const int survival_inventory =
                SV_BotFrameCommandSmokeIsSurvivalInventory() ? 1 : 0;
            const int survival_route =
                SV_BotFrameCommandSmokeIsSurvivalRoute() ? 1 : 0;
            const char *survival_route_kind =
                SV_BotFrameCommandSmokeIsCombatSurvivalRegression() ? "combat_health" :
                SV_BotFrameCommandSmokeIsSurvivalArmorRoute() ? "armor" :
                (SV_BotFrameCommandSmokeIsSurvivalHealthRoute() ? "health" : "0");
            const int threat_retreat =
                SV_BotFrameCommandSmokeIsThreatRetreat() ? 1 : 0;
            const int tdm_role_spawn_stability =
                SV_BotFrameCommandSmokeIsTdmRoleSpawnStability() ? 1 : 0;
            const int profile_role_policy =
                SV_BotFrameCommandSmokeIsProfileRolePolicy() ? 1 : 0;
            const int profile_team_policy =
                SV_BotFrameCommandSmokeIsProfileTeamPolicy() ? 1 : 0;
            const int profile_item_policy =
                SV_BotFrameCommandSmokeIsProfileItemPolicy() ? 1 : 0;
            const int profile_movement_policy =
                SV_BotFrameCommandSmokeIsProfileMovementPolicy() ? 1 : 0;
            const int bot_chat_policy =
                SV_BotFrameCommandSmokeUsesBotChatPolicy() ? 1 : 0;
            const int bot_chat_team_policy =
                SV_BotFrameCommandSmokeIsBotChatTeamPolicy() ? 1 : 0;
            const int bot_chat_rate_policy =
                SV_BotFrameCommandSmokeIsBotChatRatePolicy() ? 1 : 0;
            const int bot_chat_initial_policy =
                SV_BotFrameCommandSmokeIsBotChatInitialPolicy() ? 1 : 0;
            const int bot_chat_reply_policy =
                SV_BotFrameCommandSmokeIsBotChatReplyPolicy() ? 1 : 0;
            const int bot_chat_event_policy =
                (SV_BotFrameCommandSmokeIsBotChatEventPolicy() ||
                 SV_BotFrameCommandSmokeIsBotChatDuplicateSuppression()) ? 1 : 0;
            const int bot_chat_live_events =
                SV_BotFrameCommandSmokeIsBotChatLiveEvents() ? 1 : 0;
            const int bot_chat_live_event_cooldown =
                SV_BotFrameCommandSmokeIsBotChatLiveEventCooldown() ? 1 : 0;
            const int bot_chat_live_enemy_sighted =
                SV_BotFrameCommandSmokeIsBotChatLiveEnemySighted() ? 1 : 0;
            const int bot_chat_live_low_health =
                SV_BotFrameCommandSmokeIsBotChatLiveLowHealth() ? 1 : 0;
            const int bot_chat_live_item_taken =
                SV_BotFrameCommandSmokeIsBotChatLiveItemTaken() ? 1 : 0;
            const int bot_chat_live_item_denied =
                SV_BotFrameCommandSmokeIsBotChatLiveItemDenied() ? 1 : 0;
            const int bot_chat_live_match_result =
                SV_BotFrameCommandSmokeIsBotChatLiveMatchResult() ? 1 : 0;
            const int bot_chat_live_objective_changed =
                SV_BotFrameCommandSmokeIsBotChatLiveObjectiveChanged() ? 1 : 0;
            const int bot_chat_live_flag_state =
                SV_BotFrameCommandSmokeIsBotChatLiveFlagState() ? 1 : 0;
            const int bot_chat_live_blocked =
                SV_BotFrameCommandSmokeIsBotChatLiveBlocked() ? 1 : 0;
            const int bot_chat_phrase_library =
                SV_BotFrameCommandSmokeIsBotChatPhraseLibrary() ? 1 : 0;
            const int bot_chat_duplicate_suppression =
                SV_BotFrameCommandSmokeIsBotChatDuplicateSuppression() ? 1 : 0;
            const int allow_chat = bot_chat_policy ? 1 : 0;
            const int chat_team_only = bot_chat_team_policy ? 1 : 0;
            const int chat_min_interval_ms =
                (bot_chat_rate_policy || bot_chat_live_event_cooldown) ?
                    60000 : 0;
            const int team_fire_avoidance =
                SV_BotFrameCommandSmokeIsTeamFireAvoidance() ? 1 : 0;
            const int team_role_combat =
                SV_BotFrameCommandSmokeIsTeamRoleCombat() ? 1 : 0;
            const int ctf_role_route =
                (SV_BotFrameCommandSmokeIsCtfRoleRoute() ||
                 SV_BotFrameCommandSmokeIsCtfObjectiveRoutePrecedence()) ? 1 : 0;
            const int ctf_role_combat =
                SV_BotFrameCommandSmokeIsCtfRoleCombat() ? 1 : 0;
            const int ctf_dropped_flag_route =
                SV_BotFrameCommandSmokeIsCtfDroppedFlagRoute() ? 1 : 0;
            const int ctf_carrier_support_route =
                SV_BotFrameCommandSmokeIsCtfCarrierSupportRoute() ? 1 : 0;
            const int ctf_base_return_route =
                SV_BotFrameCommandSmokeIsCtfBaseReturnRoute() ? 1 : 0;
            const int ctf_objective_route =
                (SV_BotFrameCommandSmokeIsCtfObjectiveRoute() ||
                 SV_BotFrameCommandSmokeIsCtfObjectiveTransitions() ||
                 SV_BotFrameCommandSmokeIsCtfObjectiveRoutePrecedence()) ? 1 : 0;
            const int ctf_objective_transitions =
                SV_BotFrameCommandSmokeIsCtfObjectiveTransitions() ? 1 : 0;
            const int ctf_objective_route_precedence =
                SV_BotFrameCommandSmokeIsCtfObjectiveRoutePrecedence() ? 1 : 0;
            const int ctf_item_roles =
                SV_BotFrameCommandSmokeIsCtfItemRoles() ? 1 : 0;
            const char *gametype =
                SV_BotFrameCommandSmokeIsDuelLivePacing() ? "2" :
                (SV_BotFrameCommandSmokeIsFfaRoamRoute() ||
                 SV_BotFrameCommandSmokeIsFfaRoleCombat() ||
                 SV_BotFrameCommandSmokeIsAimFirePolicy() ||
                 SV_BotFrameCommandSmokeIsCombatSurvivalRegression() ||
                 SV_BotFrameCommandSmokeIsBotChatLiveItemTaken() ||
                 SV_BotFrameCommandSmokeIsBotChatLiveBlocked() ||
                 SV_BotFrameCommandSmokeIsAmmoPressure() ||
                 SV_BotFrameCommandSmokeIsSurvivalInventory() ||
                 SV_BotFrameCommandSmokeIsSurvivalRoute() ||
                 SV_BotFrameCommandSmokeIsThreatRetreat() ||
                 SV_BotFrameCommandSmokeIsTargetMemory() ||
                 SV_BotFrameCommandSmokeIsWeaponScoring() ||
                 SV_BotFrameCommandSmokeIsFfaItemRoles()) ? "1" :
                ((SV_BotFrameCommandSmokeIsMatchReadiness() ||
                 SV_BotFrameCommandSmokeIsTeamRoleRoute() ||
                 SV_BotFrameCommandSmokeIsTeamItemRoles() ||
                 SV_BotFrameCommandSmokeIsTeamResourceDenial() ||
                 SV_BotFrameCommandSmokeIsMatchItemPolicy() ||
                 SV_BotFrameCommandSmokeIsProfileItemPolicy() ||
                 SV_BotFrameCommandSmokeIsProfileMovementPolicy() ||
                 (SV_BotFrameCommandSmokeUsesBotChatPolicy() &&
                  !SV_BotFrameCommandSmokeIsBotChatLiveObjectiveChanged() &&
                  !SV_BotFrameCommandSmokeIsBotChatLiveFlagState() &&
                  !SV_BotFrameCommandSmokeIsBotChatLiveBlocked()) ||
                 SV_BotFrameCommandSmokeUsesBehaviorPolicy() ||
                 SV_BotFrameCommandSmokeIsProfileRolePolicy() ||
                 SV_BotFrameCommandSmokeIsTeamFireAvoidance() ||
                 SV_BotFrameCommandSmokeIsTeamRoleCombat()) ? "3" :
                ((SV_BotFrameCommandSmokeIsCtfRoleRoute() ||
                  SV_BotFrameCommandSmokeIsCtfRoleCombat() ||
                  SV_BotFrameCommandSmokeIsCtfDroppedFlagRoute() ||
                  SV_BotFrameCommandSmokeIsCtfCarrierSupportRoute() ||
                  SV_BotFrameCommandSmokeIsCtfBaseReturnRoute() ||
                  SV_BotFrameCommandSmokeIsCtfObjectiveRoute() ||
                  SV_BotFrameCommandSmokeIsCtfObjectiveTransitions() ||
                  SV_BotFrameCommandSmokeIsCtfObjectiveRoutePrecedence() ||
                  SV_BotFrameCommandSmokeIsProfileTeamPolicy() ||
                  SV_BotFrameCommandSmokeIsCtfItemRoles()) ? "5" :
                (SV_BotFrameCommandSmokeIsTeamObjective() ? "1" : "0")));

            Cvar_Set("bot_frame_command_smoke_combat", combat_mode);
            Cvar_Set("bot_frame_command_smoke_weapon_switch",
                     weapon_switch ? "1" : "0");
            Cvar_Set("bot_frame_command_smoke_item_focus", item_focus);
            Cvar_Set("bot_frame_command_smoke_team_objective",
                     team_objective ? "1" : "0");
            Cvar_Set("bot_frame_command_smoke_aim_fairness",
                     aim_fairness ? "1" : "0");
            Cvar_Set("bot_frame_command_smoke_item_timer",
                     item_timer ? "1" : "0");
            Cvar_Set("bot_frame_command_smoke_match_readiness",
                     match_readiness ? "1" : "0");
            Cvar_Set("bot_coop_target_share",
                     target_share ? "1" : "0");
            Cvar_Set("bot_coop_door_elevator",
                     door_elevator ? "1" : "0");
            Cvar_Set("bot_coop_live_loop",
                     coop_live_loop ? "1" : "0");
            Cvar_Set("bot_coop_share_loop",
                     coop_share_loop ? "1" : "0");
            Cvar_Set("bot_ffa_roam_route",
                     ffa_roam_route ? "1" : "0");
            Cvar_Set("bot_ffa_spawn_camp_avoidance",
                     ffa_spawn_camp_avoidance ? "1" : "0");
            Cvar_Set("bot_ffa_spawn_camp_combat_avoidance",
                     ffa_spawn_camp_combat_avoidance ? "1" : "0");
            Cvar_Set("bot_ffa_item_roles",
                     ffa_item_roles ? "1" : "0");
            Cvar_Set("bot_ffa_role_combat",
                     ffa_role_combat ? "1" : "0");
            Cvar_Set("bot_duel_live_pacing",
                     duel_live_pacing ? "1" : "0");
            Cvar_Set("bot_match_item_policy",
                     match_item_policy ? "1" : "0");
            Cvar_Set("bot_profile_item_policy_smoke",
                     profile_item_policy ? "1" : "0");
            Cvar_Set("bot_profile_movement_policy_smoke",
                     profile_movement_policy ? "1" : "0");
            Cvar_Set("bot_allow_chat",
                     allow_chat ? "1" : "0");
            Cvar_Set("bot_chat_team_only",
                     chat_team_only ? "1" : "0");
            Cvar_Set("bot_chat_min_interval_ms",
                     chat_min_interval_ms ? "60000" : "0");
            Cvar_Set("bot_chat_reply_policy_smoke",
                     bot_chat_reply_policy ? "1" : "0");
            Cvar_Set("bot_chat_event_policy_smoke",
                     bot_chat_event_policy ? "1" : "0");
            Cvar_Set("bot_chat_live_events",
                     bot_chat_live_events ? "1" : "0");
            Cvar_Set("bot_behavior_enable",
                     behavior_policy ? "1" : "0");
            Cvar_Set("bot_frame_command_smoke_target_memory",
                     target_memory ? "1" : "0");
            Cvar_Set("bot_frame_command_smoke_survival_inventory",
                     survival_inventory ? "1" : "0");
            Cvar_Set("bot_frame_command_smoke_survival_route",
                     SV_BotFrameCommandSmokeIsCombatSurvivalRegression() ?
                         "combat_health" :
                     SV_BotFrameCommandSmokeIsSurvivalArmorRoute() ? "armor" :
                     (survival_route ? "1" : "0"));
            Cvar_Set("bot_threat_retreat",
                     threat_retreat ? "1" : "0");
            Cvar_Set("bot_team_role_route",
                     team_role_route ? "1" : "0");
            Cvar_Set("bot_team_item_roles",
                     team_item_roles ? "1" : "0");
            Cvar_Set("bot_team_resource_denial",
                     team_resource_denial ? "1" : "0");
            Cvar_Set("bot_team_fire_avoidance",
                     team_fire_avoidance ? "1" : "0");
            Cvar_Set("bot_team_role_combat",
                     team_role_combat ? "1" : "0");
            Cvar_Set("bot_ctf_role_route",
                     ctf_role_route ? "1" : "0");
            Cvar_Set("bot_ctf_role_combat",
                     ctf_role_combat ? "1" : "0");
            Cvar_Set("bot_ctf_dropped_flag_route",
                     ctf_dropped_flag_route ? "1" : "0");
            Cvar_Set("bot_ctf_carrier_support_route",
                     ctf_carrier_support_route ? "1" : "0");
            Cvar_Set("bot_ctf_base_return_route",
                     ctf_base_return_route ? "1" : "0");
            Cvar_Set("bot_ctf_objective_route",
                     ctf_objective_route ? "1" : "0");
            Cvar_Set("bot_ctf_objective_transitions",
                     ctf_objective_transitions ? "1" : "0");
            Cvar_Set("bot_ctf_item_roles",
                     ctf_item_roles ? "1" : "0");
            Com_Printf("q3a_bot_frame_command_smoke_scenario=begin "
                       "mode=%d map=%s combat=%s weapon_switch=%d item_focus=%s "
                       "team_objective=%d target=%d gametype=%s "
                       "aim_fairness=%d aim_fire_policy=%d "
                       "item_timer=%d match_readiness=%d "
                       "target_share=%d door_elevator=%d "
                       "coop_live_loop=%d coop_share_loop=%d "
                       "ffa_roam_route=%d "
                       "ffa_spawn_camp_avoidance=%d "
                       "ffa_spawn_camp_combat_avoidance=%d "
                       "ffa_item_roles=%d "
                       "ffa_role_combat=%d "
                       "ffa_live_pacing=%d "
                       "duel_live_pacing=%d "
                       "match_item_policy=%d behavior_enable=%d "
                       "behavior_arbitration=%d "
                       "target_memory=%d weapon_scoring=%d "
                       "ammo_pressure=%d survival_inventory=%d "
                       "survival_route=%d survival_route_kind=%s "
                       "threat_retreat=%d "
                       "tdm_role_spawn_stability=%d "
                       "profile_role_policy=%d "
                        "profile_team_policy=%d "
                        "profile_item_policy=%d "
                        "profile_movement_policy=%d "
                        "bot_chat_policy=%d bot_chat_team_policy=%d "
                        "bot_chat_rate_policy=%d "
                        "bot_chat_initial_policy=%d "
                        "bot_chat_reply_policy=%d "
                        "bot_chat_event_policy=%d "
                        "bot_chat_live_events=%d "
                        "bot_chat_live_event_cooldown=%d "
                        "bot_chat_live_enemy_sighted=%d "
                        "bot_chat_live_low_health=%d "
                        "bot_chat_live_item_taken=%d "
                        "bot_chat_live_item_denied=%d "
                        "bot_chat_live_match_result=%d "
                        "bot_chat_live_objective_changed=%d "
                        "bot_chat_live_flag_state=%d "
                        "bot_chat_live_blocked=%d "
                        "bot_chat_phrase_library=%d "
                        "bot_chat_duplicate_suppression=%d "
                        "allow_chat=%d chat_team_only=%d "
                        "chat_min_interval_ms=%d "
                        "team_role_route=%d team_item_roles=%d "
                       "team_resource_denial=%d "
                       "team_fire_avoidance=%d team_role_combat=%d "
                       "ctf_role_route=%d "
                       "ctf_role_combat=%d ctf_dropped_flag_route=%d "
                       "ctf_carrier_support_route=%d "
                       "ctf_base_return_route=%d "
                       "ctf_objective_route=%d "
                       "ctf_objective_transitions=%d "
                       "ctf_objective_route_precedence=%d "
                       "ctf_item_roles=%d\n",
                       SV_BotFrameCommandSmokeMode(), sv.name, combat_mode,
                       weapon_switch, item_focus, team_objective,
                       target_bots, gametype, aim_fairness, aim_fire_policy,
                       item_timer, match_readiness, target_share, door_elevator,
                       coop_live_loop, coop_share_loop,
                       ffa_roam_route, ffa_spawn_camp_avoidance,
                       ffa_spawn_camp_combat_avoidance,
                       ffa_item_roles, ffa_role_combat, ffa_live_pacing,
                       duel_live_pacing,
                       match_item_policy, behavior_policy,
                        behavior_arbitration, target_memory, weapon_scoring,
                        ammo_pressure, survival_inventory, survival_route,
                        survival_route_kind, threat_retreat,
                        tdm_role_spawn_stability,
                        profile_role_policy, profile_team_policy,
                        profile_item_policy, profile_movement_policy,
                        bot_chat_policy, bot_chat_team_policy,
                        bot_chat_rate_policy, bot_chat_initial_policy,
                        bot_chat_reply_policy, bot_chat_event_policy,
                        bot_chat_live_events,
                        bot_chat_live_event_cooldown,
                        bot_chat_live_enemy_sighted,
                        bot_chat_live_low_health,
                        bot_chat_live_item_taken,
                        bot_chat_live_item_denied,
                        bot_chat_live_match_result,
                        bot_chat_live_objective_changed,
                        bot_chat_live_flag_state,
                        bot_chat_live_blocked,
                        bot_chat_phrase_library,
                        bot_chat_duplicate_suppression,
                        allow_chat,
                        chat_team_only, chat_min_interval_ms,
                        team_role_route, team_item_roles,
                       team_resource_denial,
                       team_fire_avoidance, team_role_combat,
                       ctf_role_route,
                       ctf_role_combat, ctf_dropped_flag_route,
                       ctf_carrier_support_route,
                       ctf_base_return_route, ctf_objective_route,
                       ctf_objective_transitions,
                       ctf_objective_route_precedence, ctf_item_roles);
        } else {
            Cvar_Set("bot_frame_command_smoke_combat", "0");
            Cvar_Set("bot_frame_command_smoke_weapon_switch", "0");
            Cvar_Set("bot_frame_command_smoke_item_focus", "0");
            Cvar_Set("bot_frame_command_smoke_team_objective", "0");
            Cvar_Set("bot_frame_command_smoke_aim_fairness", "0");
            Cvar_Set("bot_frame_command_smoke_item_timer", "0");
            Cvar_Set("bot_frame_command_smoke_match_readiness", "0");
            Cvar_Set("bot_coop_target_share", "0");
            Cvar_Set("bot_coop_door_elevator", "0");
            Cvar_Set("bot_coop_live_loop", "0");
            Cvar_Set("bot_coop_share_loop", "0");
            Cvar_Set("bot_ffa_roam_route", "0");
            Cvar_Set("bot_ffa_spawn_camp_avoidance", "0");
            Cvar_Set("bot_ffa_spawn_camp_combat_avoidance", "0");
            Cvar_Set("bot_ffa_item_roles", "0");
            Cvar_Set("bot_ffa_role_combat", "0");
            Cvar_Set("bot_duel_live_pacing", "0");
            Cvar_Set("bot_match_item_policy", "0");
            Cvar_Set("bot_profile_item_policy_smoke", "0");
            Cvar_Set("bot_profile_movement_policy_smoke", "0");
            Cvar_Set("bot_allow_chat", "0");
            Cvar_Set("bot_chat_team_only", "0");
            Cvar_Set("bot_chat_min_interval_ms", "0");
            Cvar_Set("bot_chat_reply_policy_smoke", "0");
            Cvar_Set("bot_chat_event_policy_smoke", "0");
            Cvar_Set("bot_chat_live_events", "0");
            Cvar_Set("bot_behavior_enable", "0");
            Cvar_Set("bot_frame_command_smoke_target_memory", "0");
            Cvar_Set("bot_frame_command_smoke_survival_inventory", "0");
            Cvar_Set("bot_frame_command_smoke_survival_route", "0");
            Cvar_Set("bot_threat_retreat", "0");
            Cvar_Set("bot_team_role_route", "0");
            Cvar_Set("bot_team_item_roles", "0");
            Cvar_Set("bot_team_resource_denial", "0");
            Cvar_Set("bot_team_fire_avoidance", "0");
            Cvar_Set("bot_team_role_combat", "0");
            Cvar_Set("bot_ctf_role_route", "0");
            Cvar_Set("bot_ctf_role_combat", "0");
            Cvar_Set("bot_ctf_dropped_flag_route", "0");
            Cvar_Set("bot_ctf_carrier_support_route", "0");
            Cvar_Set("bot_ctf_base_return_route", "0");
            Cvar_Set("bot_ctf_objective_route", "0");
            Cvar_Set("bot_ctf_objective_transitions", "0");
            Cvar_Set("bot_ctf_item_roles", "0");
        }
        forced_travel_type = SV_BotFrameCommandSmokeForcedTravelType();
        if (forced_travel_type > 0) {
            char forced_travel_type_value[16];
            Q_snprintf(forced_travel_type_value,
                       sizeof(forced_travel_type_value),
                       "%d",
                       forced_travel_type);
            Cvar_Set("bot_frame_command_smoke_travel_type",
                     forced_travel_type_value);
            Com_Printf("q3a_bot_frame_command_smoke_forced_travel_type=%d\n",
                       forced_travel_type);
        } else {
            Cvar_Set("bot_frame_command_smoke_travel_type", "0");
        }
        if (SV_BotFrameCommandSmokeUsesPositionGoal()) {
            Cvar_Set("bot_nav_position_goal_enable", "1");
            Cvar_Set("bot_nav_position_goal_x", "64");
            Cvar_Set("bot_nav_position_goal_y", "-304");
            Cvar_Set("bot_nav_position_goal_z", "82");
            Com_Printf("q3a_bot_frame_command_smoke_position_goal=64 -304 82\n");
        } else {
            Cvar_Set("bot_nav_position_goal_enable", "0");
        }
        travel_type_goal = SV_BotFrameCommandSmokeTravelTypeGoal();
        if (travel_type_goal > 0) {
            char travel_type_goal_value[16];
            Q_snprintf(travel_type_goal_value,
                       sizeof(travel_type_goal_value),
                       "%d",
                       travel_type_goal);
            Cvar_Set("bot_nav_travel_type_goal",
                     travel_type_goal_value);
            Cvar_Set("bot_nav_travel_type_goal_warp", "1");
            Com_Printf("q3a_bot_frame_command_smoke_travel_type_goal=%d\n",
                       travel_type_goal);
        } else {
            Cvar_Set("bot_nav_travel_type_goal", "0");
            Cvar_Set("bot_nav_travel_type_goal_warp", "0");
        }
        settle_frames = 0;
        stage = 1;
        return;
    }

    if (stage == 1) {
        if (SV_BotCount() != 0) {
            SV_BotRemoveAll();
            return;
        }

        added = SV_BotAdd(SV_BotFrameCommandSmokeBotName(0), NULL);
        Com_Printf("q3a_bot_frame_command_smoke_after_add_request "
                   "added=%d count=%d target=%d\n",
                   added, SV_BotCount(), target_bots);
        stage = 2;
        return;
    }

    if (SV_BotCount() < target_bots) {
        const int count = SV_BotCount();
        const int pending = bot_add_queue_count();
        const int requested = count + pending;

        if (requested >= target_bots) {
            return;
        }

        added = SV_BotAdd(SV_BotFrameCommandSmokeBotName(requested), NULL);
        Com_Printf("q3a_bot_frame_command_smoke_after_extra_add_request "
                   "index=%d added=%d count=%d pending=%d target=%d\n",
                   requested, added, count, pending, target_bots);
        return;
    }

    if (SV_BotFrameCommandSmokeIsSoak()) {
        const int duration_ms = SV_BotFrameCommandSmokeSoakMilliseconds();
        const int progress_ms = SV_BotFrameCommandSmokeSoakProgressMilliseconds();
        const unsigned now = svs.realtime;
        unsigned elapsed_ms;

        if (!soak_active) {
            soak_active = true;
            soak_start_realtime = now;
            soak_last_progress_realtime = now;
            soak_progress_reports = 0;
            Com_Printf("q3a_bot_frame_command_smoke_soak=begin "
                       "target=%d duration_ms=%d progress_ms=%d count=%d\n",
                       target_bots, duration_ms, progress_ms, SV_BotCount());
            return;
        }

        elapsed_ms = now - soak_start_realtime;
        if (elapsed_ms < (unsigned)duration_ms) {
            if (now - soak_last_progress_realtime >= (unsigned)progress_ms) {
                soak_progress_reports++;
                soak_last_progress_realtime = now;
                Com_Printf("q3a_bot_frame_command_smoke_soak_progress "
                           "elapsed_ms=%u duration_ms=%d count=%d reports=%d\n",
                           elapsed_ms, duration_ms, SV_BotCount(),
                           soak_progress_reports);
            }
            return;
        }

        Com_Printf("q3a_bot_frame_command_smoke_soak=complete "
                   "elapsed_ms=%u duration_ms=%d count=%d reports=%d\n",
                   elapsed_ms, duration_ms, SV_BotCount(), soak_progress_reports);
    } else if (++settle_frames < SV_BotFrameCommandSmokeSettleFrames()) {
        return;
    }

    if (SV_BotFrameCommandSmokeIsBotChatLiveMatchResult() &&
        !match_result_intermission_requested) {
        const int begin_success = SV_BotIntermissionSmokeBegin();
        const int settle_target = SV_BotFrameCommandSmokeSettleFrames();

        match_result_intermission_requested = true;
        settle_frames = settle_target > 8 ? settle_target - 8 : 0;
        Com_Printf("q3a_bot_frame_command_smoke_match_result_intermission_requested "
                   "count=%d success=%d\n",
                   SV_BotCount(), begin_success);
        SV_BotIntermissionSmokeStatus(
            target_bots, 0, target_bots, begin_success ? 1 : -1,
            begin_success ? target_bots : -1, 0, target_bots);
        return;
    }

    if (SV_BotFrameCommandSmokeIsMapRepeat()) {
        const char *phase = SV_BotFrameCommandSmokeMapRepeatPhase(
            map_repeat_completed_cycles);

        Com_Printf("q3a_bot_frame_command_smoke_map_repeat_cycle_status_requested "
                   "cycle=%d phase=%s target_cycles=%d count=%d "
                   "map=%s spawncount=%d status_line=next\n",
                   map_repeat_cycle, phase,
                   SV_BotFrameCommandSmokeMapRepeatCycles(), SV_BotCount(),
                   sv.name, sv.spawncount);
    }
    Com_Printf("q3a_bot_frame_command_smoke_status_requested count=%d\n",
               SV_BotCount());

    if (SV_BotFrameCommandSmokeIsMapRepeat()) {
        const int expected_commands =
            SV_BotFrameCommandSmokeExpectedCommands(target_bots);
        const char *captured_status = SV_BotFrameCommandStatusCapture(
            target_bots, expected_commands);
        const char *phase = SV_BotFrameCommandSmokeMapRepeatPhase(
            map_repeat_completed_cycles);
        bool official_pass;
        int status_pass;

        SV_BotFrameCommandStatusPrintCaptured(captured_status);

        status_pass = SV_BotFrameCommandStatusCapturedPass(
            captured_status, target_bots, expected_commands, &official_pass);

        Com_Printf("\nq3a_bot_frame_command_smoke_map_repeat_cycle_status_complete "
                   "cycle=%d phase=%s target_cycles=%d "
                   "status_line=previous pass=%d pass_source=%s "
                   "official_pass=%d\n",
                   map_repeat_cycle, phase,
                   SV_BotFrameCommandSmokeMapRepeatCycles(),
                   status_pass,
                   official_pass ? "q3a_bot_frame_command_status" :
                       (status_pass >= 0 ? "server_summary" : "unavailable"),
                   official_pass ? 1 : 0);
        if (status_pass == 0) {
            const int removed = SV_BotRemoveAll();

            SV_BotFrameCommandSmokeResetRuntimeCvars();
            Com_Printf("q3a_bot_frame_command_smoke_map_repeat_cleanup "
                       "cycle=%d phase=%s reason=cycle_status_failed "
                       "removed=%d final_count=%d\n",
                       map_repeat_cycle, phase, removed, SV_BotCount());
            Com_Printf("q3a_bot_frame_command_smoke_map_repeat=failed "
                       "reason=cycle_status_failed cycle=%d phase=%s "
                       "completed_cycles=%d map_changes=%d final_map=%s "
                       "final_spawncount=%d final_count=%d pass=0\n",
                       map_repeat_cycle, phase, map_repeat_completed_cycles,
                       map_repeat_map_changes, sv.name, sv.spawncount,
                       SV_BotCount());
            stage = 4;
            soak_active = false;
            if (bot_frame_command_smoke->integer >= 2) {
                Com_Quit(NULL, ERR_DISCONNECT);
            }
            return;
        }
        map_repeat_completed_cycles++;
        Com_Printf("q3a_bot_frame_command_smoke_map_repeat_cycle=complete "
                   "cycle=%d phase=%s completed_cycles=%d target_cycles=%d "
                   "map_changes=%d map=%s spawncount=%d count=%d\n",
                   map_repeat_cycle, phase, map_repeat_completed_cycles,
                   SV_BotFrameCommandSmokeMapRepeatCycles(),
                   map_repeat_map_changes, sv.name, sv.spawncount,
                   SV_BotCount());
    } else {
        const int expected_commands =
            SV_BotFrameCommandSmokeExpectedCommands(target_bots);
        const char *captured_status = NULL;

        if (SV_BotFrameCommandSmokeUsesScenarioCvars()) {
            captured_status = SV_BotFrameCommandStatusCapture(
                target_bots, expected_commands);
            if (SV_BotFrameCommandSmokeStatusWaitingForRuntime(
                    captured_status) &&
                settle_frames < SV_BotFrameCommandSmokeRuntimeWarmupLimit()) {
                if (settle_frames == SV_BotFrameCommandSmokeSettleFrames() ||
                    (settle_frames % 10) == 0 ||
                    settle_frames + 1 >=
                        SV_BotFrameCommandSmokeRuntimeWarmupLimit()) {
                    int frames = 0;
                    int skipped_runtime = 0;

                    SV_BotFrameCommandStatusReadInt(
                        captured_status, "frames", &frames);
                    SV_BotFrameCommandStatusReadInt(
                        captured_status, "skipped_runtime", &skipped_runtime);
                    Com_Printf(
                        "q3a_bot_frame_command_smoke_runtime_wait "
                        "mode=%d frames=%d skipped_runtime=%d "
                        "settle_frames=%d limit=%d\n",
                        SV_BotFrameCommandSmokeMode(), frames,
                        skipped_runtime, settle_frames,
                        SV_BotFrameCommandSmokeRuntimeWarmupLimit());
                }
                return;
            }
        }

        if (captured_status) {
            SV_BotFrameCommandStatusPrintCaptured(captured_status);
        } else {
            SV_BotFrameCommandStatus(target_bots, expected_commands);
        }
    }

    if (SV_BotFrameCommandSmokeUsesBotChatPolicy()) {
        if (SV_BotFrameCommandSmokeIsBotChatLiveMatchResult()) {
            SV_BotIntermissionSmokeStatus(target_bots, 0, target_bots, 1,
                                          target_bots, 0, target_bots);
        }
        SV_BotChatPolicySmokeStatus(target_bots, target_bots, 1, 1);
    }

    SV_BotRemoveAll();
    Com_Printf("q3a_bot_frame_command_smoke_removed_all count=%d\n",
               SV_BotCount());
    SV_BotFrameCommandSmokeResetRuntimeCvars();
    if (SV_BotFrameCommandSmokeIsMapRepeat()) {
        const int target_cycles = SV_BotFrameCommandSmokeMapRepeatCycles();
        const char *cleanup_phase =
            SV_BotFrameCommandSmokeMapRepeatCyclePhase(map_repeat_cycle);
        const char *cleanup_reason =
            map_repeat_completed_cycles < target_cycles ?
                "before_reload" : "final_cycle_complete";

        if (!SV_BotFrameCommandSmokeMapRepeatCleanupStatus(
                map_repeat_cycle, cleanup_phase, cleanup_reason)) {
            Com_Printf("q3a_bot_frame_command_smoke_map_repeat_cleanup "
                       "cycle=%d phase=%s reason=cleanup_status_failed "
                       "final_count=%d\n",
                       map_repeat_cycle, cleanup_phase, SV_BotCount());
            Com_Printf("q3a_bot_frame_command_smoke_map_repeat=failed "
                       "reason=cleanup_status_failed cycle=%d phase=%s "
                       "completed_cycles=%d map_changes=%d final_map=%s "
                       "final_spawncount=%d final_count=%d pass=0\n",
                       map_repeat_cycle, cleanup_phase,
                       map_repeat_completed_cycles, map_repeat_map_changes,
                       sv.name, sv.spawncount, SV_BotCount());
            stage = 4;
            soak_active = false;
            if (bot_frame_command_smoke->integer >= 2) {
                Com_Quit(NULL, ERR_DISCONNECT);
            }
            return;
        }
    }
    Com_Printf("q3a_bot_frame_command_smoke=end final_count=%d\n",
               SV_BotCount());
    stage = 3;
    soak_active = false;

    if (SV_BotFrameCommandSmokeIsMapRepeat()) {
        const int target_cycles = SV_BotFrameCommandSmokeMapRepeatCycles();

        if (map_repeat_completed_cycles < target_cycles) {
            const int reload_timeout_ms =
                SV_BotFrameCommandSmokeMapRepeatReloadTimeoutMilliseconds();

            Q_strlcpy(map_repeat_map, sv.name, sizeof(map_repeat_map));
            map_repeat_map_changes++;
            map_repeat_reload_pending = true;
            map_repeat_reload_spawncount = sv.spawncount;
            map_repeat_reload_request_realtime = svs.realtime;
            Com_Printf("q3a_bot_frame_command_smoke_map_repeat_map_change_request "
                       "completed_cycles=%d target_cycles=%d map_changes=%d "
                       "from_spawncount=%d map=%s timeout_ms=%d "
                       "command=%s restart=%d\n",
                       map_repeat_completed_cycles, target_cycles,
                       map_repeat_map_changes, sv.spawncount, map_repeat_map,
                       reload_timeout_ms,
                       SV_BotFrameCommandSmokeMapRepeatReloadCommand(),
                       SV_BotFrameCommandSmokeMapRepeatRestartReload() ? 1 : 0);
            Com_Printf("q3a_bot_frame_command_smoke_map_repeat_reload=queued "
                       "cycle=%d next_cycle=%d completed_cycles=%d "
                       "target_cycles=%d map_changes=%d "
                       "from_spawncount=%d map=%s timeout_ms=%d "
                       "command=%s restart=%d\n",
                       map_repeat_cycle, map_repeat_cycle + 1,
                       map_repeat_completed_cycles, target_cycles,
                       map_repeat_map_changes, sv.spawncount, map_repeat_map,
                       reload_timeout_ms,
                       SV_BotFrameCommandSmokeMapRepeatReloadCommand(),
                       SV_BotFrameCommandSmokeMapRepeatRestartReload() ? 1 : 0);
            if (SV_BotFrameCommandSmokeMapRepeatRestartReload()) {
                Cbuf_AddText(&cmd_buffer, va("map \"%s\" force\n",
                                             map_repeat_map));
            } else {
                Cbuf_AddText(&cmd_buffer, va("gamemap \"%s\"\n",
                                             map_repeat_map));
            }
            return;
        }

        Com_Printf("q3a_bot_frame_command_smoke_map_repeat_cleanup "
                   "cycle=%d phase=%s reason=final_cycle_complete "
                   "final_count=%d\n",
                   map_repeat_cycle,
                   SV_BotFrameCommandSmokeMapRepeatCyclePhase(
                       map_repeat_cycle),
                   SV_BotCount());
        Com_Printf("q3a_bot_frame_command_smoke_map_repeat=complete "
                   "cycles=%d map_changes=%d final_map=%s "
                   "final_spawncount=%d final_count=%d\n",
                   map_repeat_completed_cycles, map_repeat_map_changes,
                   sv.name, sv.spawncount, SV_BotCount());
    }

    if (bot_frame_command_smoke->integer >= 2) {
        Com_Quit(NULL, ERR_DISCONNECT);
    }
}

static void SV_BotSlotSmokeFrame(void)
{
    static int seen_spawncount;
    static int stage;
    client_t *alpha;
    bool added_alpha;
    bool added_bravo;
    bool added_charlie;
    int removed_all;

    if (!bot_slot_smoke || bot_slot_smoke->integer <= 0) {
        return;
    }

    if (!svs.initialized || sv.state != ss_game) {
        return;
    }

    if (seen_spawncount != sv.spawncount) {
        seen_spawncount = sv.spawncount;
        stage = 0;
    } else if (stage > 1) {
        return;
    }

    if (stage == 1) {
        Com_Printf("q3a_bot_slot_smoke_after_deferred_pair count=%d\n",
                   SV_BotCount());

        removed_all = SV_BotRemoveAll();
        Com_Printf("q3a_bot_slot_smoke_removed_all=%d\n", removed_all);
        Com_Printf("q3a_bot_slot_smoke_after_remove_all count=%d\n", SV_BotCount());
        Com_Printf("q3a_bot_slot_smoke=end final_count=%d\n", SV_BotCount());

        stage = 2;
        if (bot_slot_smoke->integer >= 2) {
            Com_Quit(NULL, ERR_DISCONNECT);
        }
        return;
    }

    Com_Printf("q3a_bot_slot_smoke=begin\n");

    added_alpha = SV_BotAdd("Alpha", NULL);
    Com_Printf("q3a_bot_slot_smoke_after_alpha added=%d count=%d maxclients=%d soft=%d\n",
               added_alpha, SV_BotCount(), svs.maxclients, svs.maxclients_soft);

    alpha = SV_BotGetClient("Alpha", true);
    if (alpha) {
        SV_BotRemove(alpha);
    }
    Com_Printf("q3a_bot_slot_smoke_after_remove_alpha count=%d\n", SV_BotCount());

    added_bravo = SV_BotAdd("Bravo", NULL);
    added_charlie = SV_BotAdd("Charlie", NULL);
    Com_Printf("q3a_bot_slot_smoke_after_pair added_bravo=%d added_charlie=%d count=%d\n",
               added_bravo, added_charlie, SV_BotCount());

    stage = 1;
}

static void send_connect_packet(client_t *newcl, int nctype)
{
    const char *ncstring    = "";
    const char *acstring    = "";
    const char *dlstring1   = "";
    const char *dlstring2   = "";

    if (nctype == NETCHAN_NEW)
        ncstring = " nc=1";
    else
        ncstring = " nc=0";

    if (!sv_force_reconnect->string[0] || newcl->reconnect_var[0])
        acstring = AC_ClientConnect(newcl);

    if (sv_downloadserver->string[0]) {
        dlstring1 = " dlserver=";
        dlstring2 = sv_downloadserver->string;
    }

    Netchan_OutOfBand(NS_SERVER, &net_from, "client_connect%s%s%s%s map=%s",
                      ncstring, acstring, dlstring1, dlstring2, newcl->mapname);
}

// converts all the extra positional parameters to `connect' command into an
// infostring appended to normal userinfo after terminating NUL. game mod can
// then access these parameters in ClientConnect callback.
static void append_extra_userinfo(conn_params_t *params, char *userinfo)
{
    if (!(g_features->integer & GMF_EXTRA_USERINFO)) {
        userinfo[strlen(userinfo) + 1] = 0;
        return;
    }

    Q_snprintf(userinfo + strlen(userinfo) + 1, MAX_INFO_STRING,
               "\\challenge\\%d\\ip\\%s"
               "\\major\\%d\\minor\\%d\\netchan\\%d"
               "\\packetlen\\%d\\qport\\%d\\zlib\\%d",
               params->challenge, userinfo_ip_string(),
               params->protocol, params->version, params->nctype,
               params->maxlength, params->qport, params->has_zlib);
}

static void SVC_DirectConnect(void)
{
    char            userinfo[MAX_INFO_STRING * 2];
    conn_params_t   params;
    client_t        *newcl;
    int             number;
    qboolean        allow;
    char            *reason;

    q2proto_connect_t parsed_connect;
    q2proto_error_t parse_err = q2proto_parse_connect(Cmd_Args(), q2repro_accepted_protocols, q_countof(q2repro_accepted_protocols), &svs.server_info, &parsed_connect);
    if (parse_err != Q2P_ERR_SUCCESS) {
        if (parse_err == Q2P_ERR_PROTOCOL_NOT_SUPPORTED) {
            reject_printf("Unsupported protocol %d.\n", parsed_connect.protocol);
            return;
        } else {
            reject_printf("'connect' parse error %s.\n", q2proto_error_string(parse_err));
            return;
        }
    }

    memset(&params, 0, sizeof(params));

    // parse and validate parameters
    if (!parse_basic_params(&parsed_connect, &params))
        return;
    if (!permit_connection(&params))
        return;
    if (!parse_packet_length(&parsed_connect, &params))
        return;
    if (!parse_enhanced_params(&parsed_connect, &params))
        return;
    if (!parse_userinfo(&parsed_connect, &params, userinfo))
        return;

    // find a free client slot
    newcl = find_client_slot(&params);
    if (!newcl)
        return;

    number = newcl - svs.client_pool;

    // build a new connection
    // accept the new client
    // this is the only place a client_t is ever initialized
    memset(newcl, 0, sizeof(*newcl));
    newcl->number = newcl->infonum = number;
    newcl->challenge = params.challenge; // save challenge for checksumming
    newcl->protocol = params.protocol;
    newcl->version = params.version;
    newcl->edict = EDICT_NUM(number + 1);
    newcl->gamedir = fs_game->string;
    newcl->mapname = sv.name;
    newcl->configstrings = sv.configstrings;
    newcl->csr = &svs.csr;
    newcl->ge = ge;
    newcl->cm = &sv.cm;
    newcl->spawncount = sv.spawncount;
    newcl->maxclients = svs.maxclients;
    Q_strlcpy(newcl->reconnect_var, params.reconnect_var, sizeof(newcl->reconnect_var));
    Q_strlcpy(newcl->reconnect_val, params.reconnect_val, sizeof(newcl->reconnect_val));
#if USE_FPS
    // Rerelease protocol assumes client framerate is always in sync
    newcl->framediv = 1;
    newcl->settings[CLS_FPS] = sv.framerate;
#endif
#if USE_ZLIB
    newcl->q2proto_deflate.z_buffer = svs.z_buffer;
    newcl->q2proto_deflate.z_buffer_size = svs.z_buffer_size;
    newcl->q2proto_deflate.z_raw = &svs.z;
#endif

    q2proto_error_t err = q2proto_init_servercontext(&newcl->q2proto_ctx, &svs.server_info, &parsed_connect);
    if (err != Q2P_ERR_SUCCESS) {
        Com_EPrintf("failed to initialize connection context: %s\n", q2proto_error_string(err));
        return;
    }

    init_pmove_and_es_flags(newcl);

    append_extra_userinfo(&params, userinfo);

    // get the game a chance to reject this connection or modify the userinfo
    sv_client = newcl;
    sv_player = newcl->edict;
    allow = ge->ClientConnect(newcl->edict, userinfo, "", false);
    sv_client = NULL;
    sv_player = NULL;
    if (!allow) {
        reason = Info_ValueForKey(userinfo, "rejmsg");
        if (*reason) {
            reject_printf("%s\nConnection refused.\n", reason);
        } else {
            reject_printf("Connection refused.\n");
        }
        return;
    }

    // setup netchan
    Netchan_Setup(&newcl->netchan, NS_SERVER, params.nctype, &net_from,
                  params.qport, params.maxlength, params.protocol);
    newcl->numpackets = 1;

    newcl->io_data.sz_read = &msg_read;
    newcl->io_data.sz_write = &msg_write;
    newcl->io_data.max_msg_len = newcl->netchan.maxpacketlen;

    // parse some info from the info strings
    Q_strlcpy(newcl->userinfo, userinfo, sizeof(newcl->userinfo));
    SV_UserinfoChanged(newcl);

    /* Default-off means no allocation, hooks, or wire-visible behavior.  The
     * private pilot is connection-scoped and snapshots the cvar only here. */
    if (sv_worr_native_shadow && sv_worr_native_shadow->integer != 0 &&
        params.nctype == NETCHAN_NEW && !newcl->worr_capability_failed &&
        (newcl->protocol == PROTOCOL_VERSION_Q2PRO ||
         newcl->protocol == PROTOCOL_VERSION_R1Q2 ||
         newcl->protocol == PROTOCOL_VERSION_RERELEASE) &&
        newcl->worr_capabilities_offered ==
            WORR_NET_CAP_LEGACY_STAGE_MASK) {
        const bool event_mode =
            sv_worr_native_event_shadow &&
            sv_worr_native_event_shadow->integer != 0;
        const bool snapshot_mode =
            sv_worr_native_snapshot_shadow &&
            sv_worr_native_snapshot_shadow->integer != 0;

        /* This slice deliberately has one server-originated DATA owner.
         * A combined event+snapshot sender will be negotiated separately;
         * enabling both current pilots is an ambiguous configuration and
         * therefore leaves the private protocol entirely disabled. */
        if (!(event_mode && snapshot_mode)) {
            sv_native_shadow_peer_v1 *pilot =
                SV_Mallocz(sizeof(*pilot));
            const sv_native_shadow_mode_v1 native_shadow_mode =
                snapshot_mode
                    ? SV_NATIVE_SHADOW_MODE_SNAPSHOT
                    : (event_mode ? SV_NATIVE_SHADOW_MODE_EVENT
                                  : SV_NATIVE_SHADOW_MODE_COMMAND);
            if (SV_NativeShadowPeerInitModeV1(
                    pilot, &newcl->netchan, svs.realtime,
                    native_shadow_mode)) {
                newcl->worr_native_shadow = pilot;
            } else {
                Z_Free(pilot);
            }
        }
    }

    // send the connect packet to the client
    send_connect_packet(newcl, params.nctype);

    SV_RateInit(&newcl->ratelimit_namechange, sv_namechange_limit->string);

    SV_InitClientSend(newcl);

    // loopback client doesn't need to reconnect
    if (NET_IsLocalAddress(&net_from)) {
        newcl->reconnected = true;
    }

    // add them to the linked list of connected clients
    List_SeqAdd(&sv_clientlist, &newcl->entry);

    Com_DPrintf("Going from cs_free to cs_assigned for %s\n", newcl->name);
    newcl->state = cs_assigned;
    newcl->framenum = 1; // frame 0 can't be used
    newcl->last_built_client_frame = 0;
    newcl->last_built_server_frame = 0;
    newcl->lastframe = -1;
    newcl->last_acked_server_frame = 0;
    newcl->last_acked_server_frame_delta = 0;
    newcl->last_acked_server_time_us = 0;
    newcl->lastmessage = svs.realtime;    // don't timeout
    newcl->lastactivity = svs.realtime;
    newcl->min_ping = 9999;
}

typedef enum {
    RCON_BAD,
    RCON_OK,
    RCON_LIMITED
} rcon_type_t;

static rcon_type_t rcon_validate(void)
{
    if (rcon_password->string[0] && !strcmp(Cmd_Argv(1), rcon_password->string))
        return RCON_OK;

    if (sv_lrcon_password->string[0] && !strcmp(Cmd_Argv(1), sv_lrcon_password->string))
        return RCON_LIMITED;

    return RCON_BAD;
}

static bool lrcon_validate(const char *s)
{
    stuffcmd_t *cmd;

    LIST_FOR_EACH(stuffcmd_t, cmd, &sv_lrconlist, entry)
        if (!strncmp(s, cmd->string, strlen(cmd->string)))
            return true;

    return false;
}

/*
===============
SVC_RemoteCommand

A client issued an rcon command.
Redirect all printfs.
===============
*/
static void SVC_RemoteCommand(void)
{
    rcon_type_t type;
    char *s;

    if (SV_RateLimited(&svs.ratelimit_rcon)) {
        Com_DPrintf("Dropping rcon from %s\n",
                    NET_AdrToString(&net_from));
        return;
    }

    type = rcon_validate();
    s = Cmd_RawArgsFrom(2);
    if (type == RCON_BAD) {
        Com_Printf("Invalid rcon from %s:\n%s\n",
                   NET_AdrToString(&net_from), s);
        OOB_PRINT(NS_SERVER, &net_from, "print\nBad rcon_password.\n");
        return;
    }

    // authenticated rcon packets are not rate limited
    SV_RateRecharge(&svs.ratelimit_rcon);

    if (type == RCON_LIMITED && lrcon_validate(s) == false) {
        Com_Printf("Invalid limited rcon from %s:\n%s\n",
                   NET_AdrToString(&net_from), s);
        OOB_PRINT(NS_SERVER, &net_from,
                  "print\nThis command is not permitted.\n");
        return;
    }

    if (type == RCON_LIMITED) {
        Com_NPrintf("Limited rcon from %s:\n%s\n",
                    NET_AdrToString(&net_from), s);
    } else {
        Com_NPrintf("Rcon from %s:\n%s\n",
                    NET_AdrToString(&net_from), s);
    }

    SV_PacketRedirect();
    if (type == RCON_LIMITED) {
        // shift args down
        Cmd_Shift();
        Cmd_Shift();
    } else {
        // macro expand args
        Cmd_TokenizeString(s, true);
    }
    Cmd_ExecuteCommand(&cmd_buffer);
    Com_EndRedirect();
}

static const ucmd_t svcmds[] = {
    { "ping",           SVC_Ping          },
    { "ack",            SVC_Ack           },
    { "status",         SVC_Status        },
    { "info",           SVC_Info          },
    { "getchallenge",   SVC_GetChallenge  },
    { "connect",        SVC_DirectConnect },
    { NULL }
};

/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
static void SV_ConnectionlessPacket(void)
{
    char    string[MAX_STRING_CHARS];
    char    *c;
    int     i;

    if (SV_MatchAddress(&sv_blacklist, &net_from)) {
        Com_DPrintf("ignored blackholed connectionless packet\n");
        return;
    }

    MSG_BeginReading();
    MSG_ReadLong();        // skip the -1 marker

    if (MSG_ReadStringLine(string, sizeof(string)) >= sizeof(string)) {
        Com_DPrintf("ignored oversize connectionless packet\n");
        return;
    }

    Cmd_TokenizeString(string, false);

    c = Cmd_Argv(0);
    Com_DPrintf("ServerPacket[%s]: %s\n", NET_AdrToString(&net_from), c);

    if (!strcmp(c, "rcon")) {
        SVC_RemoteCommand();
        return; // accept rcon commands even if not active
    }

    if (!svs.initialized) {
        Com_DPrintf("ignored connectionless packet\n");
        return;
    }

    for (i = 0; svcmds[i].name; i++) {
        if (!strcmp(c, svcmds[i].name)) {
            svcmds[i].func();
            return;
        }
    }

    Com_DPrintf("bad connectionless packet\n");
}


//============================================================================

int SV_CountClients(void)
{
    client_t *cl;
    int count = 0;

    FOR_EACH_CLIENT(cl) {
        if (cl->state > cs_zombie && !cl->bot) {
            count++;
        }
    }

    return count;
}

static int ping_nop(const client_t *cl)
{
    return 0;
}

static int ping_min(const client_t *cl)
{
    const client_frame_t *frame;
    int i, j, count = INT_MAX;

    for (i = 0; i < UPDATE_BACKUP; i++) {
        j = cl->framenum - i - 1;
        frame = &cl->frames[j & UPDATE_MASK];
        if (frame->number != j)
            continue;
        if (frame->latency == -1)
            continue;
        if (count > frame->latency)
            count = frame->latency;
    }

    return count == INT_MAX ? 0 : count;
}

static int ping_avg(const client_t *cl)
{
    const client_frame_t *frame;
    int i, j, total = 0, count = 0;

    for (i = 0; i < UPDATE_BACKUP; i++) {
        j = cl->framenum - i - 1;
        frame = &cl->frames[j & UPDATE_MASK];
        if (frame->number != j)
            continue;
        if (frame->latency == -1)
            continue;
        count++;
        total += frame->latency;
    }

    return count ? total / count : 0;
}

/*
===================
SV_CalcPings

Updates the cl->ping and cl->fps variables
===================
*/
static void SV_CalcPings(void)
{
    client_t    *cl;
    int         (*calc)(const client_t *);
    int         res;

    switch (sv_calcpings_method->integer) {
        case 0:  calc = ping_nop; break;
        case 2:  calc = ping_min; break;
        default: calc = ping_avg; break;
    }

    // update avg ping and fps every 10 seconds
    res = sv.framenum % (10 * SV_FRAMERATE);

    FOR_EACH_CLIENT(cl) {
        if (cl->state == cs_spawned) {
            cl->ping = calc(cl);
            if (cl->ping) {
                if (cl->ping < cl->min_ping) {
                    cl->min_ping = cl->ping;
                } else if (cl->ping > cl->max_ping) {
                    cl->max_ping = cl->ping;
                }
                if (!res) {
                    cl->avg_ping_time += cl->ping;
                    cl->avg_ping_count++;
                }
            }
            if (!res) {
                cl->moves_per_sec = cl->num_moves / 10;
                cl->num_moves = 0;
            }
        } else {
            cl->ping = 0;
            cl->moves_per_sec = 0;
            cl->num_moves = 0;
        }

        // let the game dll know about the ping
        SV_SetClient_Ping(cl, cl->ping);
    }
}


/*
===================
SV_GiveMsec

Every few frames, gives all clients an allotment of milliseconds
for their command moves.  If they exceed it, assume cheating.
===================
*/
static void SV_GiveMsec(void)
{
    client_t    *cl;

    if (!(sv.framenum % (16 * SV_FRAMEDIV))) {
        FOR_EACH_CLIENT(cl) {
            cl->command_msec = 1800; // 1600 + some slop
        }
    }

    if (svs.realtime - svs.last_timescale_check < sv_timescale_time->integer)
        return;

    float d = svs.realtime - svs.last_timescale_check;
    svs.last_timescale_check = svs.realtime;

    FOR_EACH_CLIENT(cl) {
        cl->timescale = cl->cmd_msec_used / d;
        cl->cmd_msec_used = 0;

        if (sv_timescale_warn->value > 1.0f && cl->timescale > sv_timescale_warn->value) {
            Com_Printf("%s[%s]: detected time skew: %.3f\n", cl->name,
                       NET_AdrToString(&cl->netchan.remote_address), cl->timescale);
        }

        if (sv_timescale_kick->value > 1.0f && cl->timescale > sv_timescale_kick->value) {
            SV_DropClient(cl, "time skew too high");
        }
    }
}


/*
=================
SV_PacketEvent
=================
*/
static void SV_PacketEvent(void)
{
    client_t    *client;
    netchan_t   *netchan;
    int         qport;

    if (msg_read.cursize < 4) {
        return;
    }

    // check for connectionless packet (0xffffffff) first
    // connectionless packets are processed even if the server is down
    if (*(int *)msg_read.data == -1) {
        SV_ConnectionlessPacket();
        return;
    }

    if (!svs.initialized) {
        return;
    }

    // check for packets from connected clients
    FOR_EACH_CLIENT(client) {
        netchan = &client->netchan;
        if (!NET_IsEqualBaseAdr(&net_from, &netchan->remote_address)) {
            continue;
        }

        // read the qport out of the message so we can fix up
        // stupid address translating routers
        if (netchan->qport) {
            if (msg_read.cursize < PACKET_HEADER - 1) {
                continue;
            }
            qport = msg_read.data[8];
            if (netchan->qport != qport) {
                continue;
            }
        } else {
            if (netchan->remote_address.port != net_from.port) {
                continue;
            }
        }

        if (netchan->remote_address.port != net_from.port) {
            Com_DPrintf("Fixing up a translated port for %s: %d --> %d\n",
                        client->name, netchan->remote_address.port, net_from.port);
            netchan->remote_address.port = net_from.port;
        }

        if (client->worr_native_shadow) {
            /* The application RX hook retains DATA using the pilot clock.
             * Stamp it before admission so the immediately following legacy
             * reconciliation cannot expire a just-arrived native record. */
            (void)SV_NativeShadowAdvanceAdmissionClockV1(
                client->worr_native_shadow, svs.realtime);
        }
        const netchan_process_result_t process_result =
            Netchan_ProcessEx(netchan);
        if (process_result == NETCHAN_PROCESS_NO_APPLICATION)
            break;
        if (process_result == NETCHAN_PROCESS_APPLICATION_REJECTED) {
            SV_DropClient(client, "rejected application payload");
            break;
        }

        if (client->state == cs_zombie)
            break;

        // this is a valid, sequenced packet, so process it
        client->lastmessage = svs.realtime;    // don't timeout
#if USE_ICMP
        client->unreachable = false; // don't drop
#endif
        if (netchan->dropped > 0)
            client->frameflags |= FF_CLIENTDROP;

        SV_ExecuteClientMessage(client);
        break;
    }
}

#if USE_PMTUDISC
// We are doing path MTU discovery and got ICMP fragmentation-needed.
// Update MTU for connecting clients only to minimize spoofed ICMP interference.
// Total 64 bytes of headers is assumed.
static void update_client_mtu(client_t *client, int ee_info)
{
    netchan_t *netchan = &client->netchan;
    unsigned newpacketlen;

    // sanity check discovered MTU
    if (ee_info < 576 || ee_info > 4096)
        return;

    if (client->state != cs_primed)
        return;

    // TODO: old clients require entire queue flush :(
    if (netchan->type == NETCHAN_OLD)
        return;

    if (!netchan->reliable_length)
        return;

    newpacketlen = ee_info - 64;
    if (newpacketlen >= netchan->maxpacketlen)
        return;

    Com_Printf("Fixing up maxmsglen for %s: %u --> %u\n",
               client->name, netchan->maxpacketlen, newpacketlen);
    netchan->maxpacketlen = newpacketlen;
}
#endif

#if USE_ICMP
/*
=================
SV_ErrorEvent
=================
*/
void SV_ErrorEvent(const netadr_t *from, int ee_errno, int ee_info)
{
    client_t    *client;
    netchan_t   *netchan;

    if (!svs.initialized) {
        return;
    }

    // check for errors from connected clients
    FOR_EACH_CLIENT(client) {
        if (client->state == cs_zombie) {
            continue; // already a zombie
        }
        netchan = &client->netchan;
        if (!NET_IsEqualBaseAdr(from, &netchan->remote_address)) {
            continue;
        }
        if (from->port && netchan->remote_address.port != from->port) {
            continue;
        }
#if USE_PMTUDISC
        // for EMSGSIZE ee_info should hold discovered MTU
        if (ee_errno == EMSGSIZE) {
            update_client_mtu(client, ee_info);
            continue;
        }
#endif
        client->unreachable = true; // drop them soon
        break;
    }
}
#endif

/*
==================
SV_CheckTimeouts

If a packet has not been received from a client for timeout->value
seconds, drop the connection.

When a client is normally dropped, the client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
static void SV_CheckTimeouts(void)
{
    client_t    *client;
    unsigned    delta;

    FOR_EACH_CLIENT(client) {
        if (client->bot) {
            if (client->state == cs_zombie) {
                SV_RemoveClient(client);
            }
            continue;
        }

        if (Netchan_SeqTooBig(&client->netchan)) {
            SV_DropClient(client, "outgoing sequence too big");
            continue;
        }

        // never timeout local clients
        if (NET_IsLocalAddress(&client->netchan.remote_address)) {
            continue;
        }
        // NOTE: delta calculated this way is not sensitive to overflow
        delta = svs.realtime - client->lastmessage;
        if (client->state == cs_zombie) {
            if (delta > sv_zombietime->integer) {
                SV_RemoveClient(client);
            }
            continue;
        }
        if (client->drop_hack) {
            SV_DropClient(client, NULL);
            continue;
        }
#if USE_ICMP
        if (client->unreachable) {
            if (delta > sv_ghostime->integer) {
                SV_DropClient(client, "connection reset by peer");
                SV_RemoveClient(client);      // don't bother with zombie state
                continue;
            }
        }
#endif
        if (delta > sv_timeout->integer || (client->state == cs_assigned && delta > sv_ghostime->integer)) {
            SV_DropClient(client, "?timed out");
            SV_RemoveClient(client);      // don't bother with zombie state
            continue;
        }

        if (client->frames_nodelta > 64 && !sv_allow_nodelta->integer) {
            SV_DropClient(client, "too many nodelta frames");
            continue;
        }

        if (sv_idlekick->integer && svs.realtime - client->lastactivity > sv_idlekick->integer) {
            SV_DropClient(client, "idling");
            continue;
        }
    }
}

/*
================
SV_PrepWorldFrame

This has to be done before the world logic, because
player processing happens outside RunWorldFrame
================
*/
static void SV_PrepWorldFrame(void)
{
    if (!SV_FRAMESYNC)
        return;

    ge->PrepFrame();
}

// pause if there is only local client on the server
static inline bool check_paused(void)
{
#if USE_CLIENT
    if (dedicated->integer)
        goto resume;

    if (!cl_paused->integer)
        goto resume;

    if (com_timedemo->integer)
        goto resume;

    if (!LIST_SINGLE(&sv_clientlist))
        goto resume;

#if USE_MVD_CLIENT
    if (!LIST_EMPTY(&mvd_gtv_list))
        goto resume;
#endif

    if (!sv_paused->integer) {
        Cvar_Set("sv_paused", "1");
        IN_Activate();
    }

    return true; // don't run if paused

resume:
    if (sv_paused->integer) {
        Cvar_Set("sv_paused", "0");
        IN_Activate();
    }
#endif

    return false;
}

/*
=================
SV_RunGameFrame
=================
*/
static void SV_RunGameFrame(void)
{
    // save the entire world state if recording a serverdemo
    SV_MvdBeginFrame();

#if USE_CLIENT
    if (host_speeds->integer)
        time_before_game = Sys_Milliseconds();

    // debug stuff is pushed via the game, so it needs
    // to look at server time for expiry, not client time
    GL_ExpireDebugObjects();
#endif

    // run nav stuff before frame runs
    Nav_Frame();

    ge->RunFrame(true);

#if USE_CLIENT
    if (host_speeds->integer)
        time_after_game = Sys_Milliseconds();
#endif

    if (msg_write.overflowed)
        Com_Error(ERR_DROP, "%s: message buffer overflowed", __func__);

    if (msg_write.cursize) {
        Com_WPrintf("Game left %u bytes "
                    "in multicast buffer, cleared.\n",
                    msg_write.cursize);
        SZ_Clear(&msg_write);
    }

    // save the entire world state if recording a serverdemo
    SV_MvdEndFrame();
}

/* Publish the time of the game state that just finished simulating before any
 * snapshot captures it. Accumulation preserves exact map time if a future
 * server changes its interval without starting a new snapshot epoch. */
static void SV_AdvanceSimulationTime(void)
{
    const uint64_t interval_us =
        (uint64_t)SV_FRAMETIME * UINT64_C(1000);

    if (sv.worr_server_time_us > UINT64_MAX - interval_us)
        Com_Error(ERR_FATAL, "server simulation clock exhausted");
    sv.worr_server_time_us += interval_us;
}

/* Spawn and savegame warm-up frames do not emit snapshots, so those paths can
 * advance both halves of the simulation identity together. */
void SV_AdvanceSimulationClock(void)
{
    SV_AdvanceSimulationTime();
    sv.framenum++;
}

/*
================
SV_MasterHeartbeat

Send a message to the master every few minutes to
let it know we are alive, and log information
================
*/
static void SV_MasterHeartbeat(void)
{
    char    buffer[MAX_PACKETLEN_DEFAULT];
    size_t  len;
    master_t *send = NULL;

    if (!COM_DEDICATED)
        return;        // only dedicated servers send heartbeats

    if (!sv_public->integer)
        return;        // a private dedicated game

    if (svs.realtime - svs.last_heartbeat < HEARTBEAT_SECONDS * 1000)
        return;        // not time to send yet

    // find the next master to send to
    while (svs.heartbeat_index < MAX_MASTERS) {
        master_t *m = &sv_masters[svs.heartbeat_index++];
        if (m->adr.type) {
            send = m;
            break;
        }
    }
    if (svs.heartbeat_index == MAX_MASTERS ||
        sv_masters[svs.heartbeat_index].name == NULL) {
        svs.last_heartbeat = svs.realtime;
        svs.heartbeat_index = 0;
    }
    if (!send)
        return;

    // write the packet header
    memcpy(buffer, "\xff\xff\xff\xffheartbeat\n", 14);
    len = 14;

    // send the same string that we would give for a status OOB command
    len += SV_StatusString(buffer + len);

    // send to group master
    Com_DPrintf("Sending heartbeat to %s\n", NET_AdrToString(&send->adr));
    NET_SendPacket(NS_SERVER, buffer, len, &send->adr);
}

/*
=================
SV_MasterShutdown

Informs all masters that this server is going down
=================
*/
static void SV_MasterShutdown(void)
{
    int i;

    // reset ack times
    for (i = 0; i < MAX_MASTERS; i++) {
        sv_masters[i].last_ack = 0;
    }

    if (!COM_DEDICATED)
        return;        // only dedicated servers send heartbeats

    if (!sv_public || !sv_public->integer)
        return;        // a private dedicated game

    // send to group master
    for (i = 0; i < MAX_MASTERS; i++) {
        master_t *m = &sv_masters[i];
        if (m->adr.type) {
            Com_DPrintf("Sending shutdown to %s\n",
                        NET_AdrToString(&m->adr));
            OOB_PRINT(NS_SERVER, &m->adr, "shutdown");
        }
    }
}

/*
==================
SV_Frame

Some things like MVD client connections and command buffer
processing are run even when server is not yet initialized.

Returns amount of extra frametime available for sleeping on IO.
==================
*/
unsigned SV_Frame(unsigned msec)
{
#if USE_CLIENT
    time_before_game = time_after_game = 0;
#endif

    // advance local server time
    svs.realtime += msec;

    if (COM_DEDICATED) {
        // process console commands if not running a client
        Cbuf_Execute(&cmd_buffer);
    }

#if USE_MVD_CLIENT
    // run connections to MVD/GTV servers
    MVD_Frame();
#endif

    // read packets from UDP clients
    NET_GetPackets(NS_SERVER, SV_PacketEvent);

    if (svs.initialized) {
        // run connection to the anticheat server
        AC_Run();

        // run connections from MVD/GTV clients
        SV_MvdRunClients();

        // deliver fragments and reliable messages for connecting clients
        SV_SendAsyncPackets();
    }

    // move autonomous things around if enough time has passed
    sv.frameresidual += msec;
    if (sv.frameresidual < SV_FRAMETIME) {
        return SV_FRAMETIME - sv.frameresidual;
    }

    if (svs.initialized) {
        // Bot slot lifecycle is server-owned and should react to console/cvar
        // changes even while the local game simulation is paused.
        SV_BotProcessAddQueue();
        SV_BotMaintainMinPlayers();
    }

    if (svs.initialized && !check_paused()) {
        // check timeouts
        SV_CheckTimeouts();

        // update ping based on the last known frame from all clients
        SV_CalcPings();

        // give the clients some timeslices
        SV_GiveMsec();

        // feed game-authored commands to spawned local bot clients
        SV_BotRunFrameCommands();

        // let everything in the world think and move
        SV_RunGameFrame();

        // Snapshots emitted below describe the state that just ran, so publish
        // its exact accumulated time before building any client frame. Keep
        // sv.framenum stable until after emission because it is that state's
        // zero-based server-frame identity.
        SV_AdvanceSimulationTime();

        // run deterministic local bot slot smoke after a game map is active
        SV_BotSlotSmokeFrame();
        SV_BotMinPlayersSmokeFrame();
        SV_BotProfileSmokeFrame();
        SV_BotTeamPolicySmokeFrame();
        SV_BotWarmupSmokeFrame();
        SV_BotVoteSmokeFrame();
        SV_BotAdminAuditSmokeFrame();
        SV_BotTournamentSmokeFrame();
        SV_BotMapVoteSmokeFrame();
        SV_BotMyMapSmokeFrame();
        SV_BotIntermissionSmokeFrame();
        SV_BotNextMapSmokeFrame();
        SV_BotScoreboardSmokeFrame();
        SV_BotMatchLogSmokeFrame();
        SV_BotFrameCommandSmokeFrame();

        // send messages back to the UDP clients
        SV_SendClientMessages();

        // send a heartbeat to the master if needed
        SV_MasterHeartbeat();

        // clear teleport flags, etc for next frame
        SV_PrepWorldFrame();

        // advance the identity used by the next game frame
        sv.framenum++;
    }

    if (COM_DEDICATED) {
        // run cmd buffer in dedicated mode
        Cbuf_Frame(&cmd_buffer);
    }

    // decide how long to sleep next frame
    sv.frameresidual -= SV_FRAMETIME;
    if (sv.frameresidual < SV_FRAMETIME) {
        return SV_FRAMETIME - sv.frameresidual;
    }

    // don't accumulate bogus residual
    if (sv.frameresidual > 250) {
        Com_DDDPrintf("Reset residual %u\n", sv.frameresidual);
        sv.frameresidual = 100;
    }

    return 0;
}

//============================================================================

/*
=================
SV_UserinfoChanged

Pull specific info from a newly changed userinfo string
into a more C friendly form.
=================
*/
void SV_UserinfoChanged(client_t *cl)
{
    char    name[MAX_CLIENT_NAME];
    char    *val;
    size_t  len;
    int     i;

    // call prog code to allow overrides
    ge->ClientUserinfoChanged(cl->edict, cl->userinfo);

    // name for C code
    val = Info_ValueForKey(cl->userinfo, "name");
    len = Q_strlcpy(name, val, sizeof(name));
    if (len >= sizeof(name)) {
        len = sizeof(name) - 1;
    }
    // mask off high bit
    for (i = 0; i < len; i++)
        name[i] &= 127;
    if (cl->name[0] && strcmp(cl->name, name)) {
        if (COM_DEDICATED) {
            Com_Printf("%s[%s] changed name to %s\n", cl->name,
                       NET_AdrToString(&cl->netchan.remote_address), name);
        }
#if USE_MVD_CLIENT
        if (sv.state == ss_broadcast) {
            MVD_GameClientNameChanged(cl->edict, name);
        } else
#endif
        if (sv_show_name_changes->integer > 1 ||
            (sv_show_name_changes->integer == 1 && cl->state == cs_spawned)) {
            SV_BroadcastPrintf(PRINT_HIGH, "%s changed name to %s\n",
                               cl->name, name);
        }
    }
    memcpy(cl->name, name, len + 1);

    // rate command
    val = Info_ValueForKey(cl->userinfo, "rate");
    if (*val) {
        cl->rate = Q_clip(Q_atoi(val), sv_min_rate->integer, sv_max_rate->integer);
    } else {
        cl->rate = 5000;
    }

    // never drop over the loopback
    if (NET_IsLocalAddress(&cl->netchan.remote_address)) {
        cl->rate = 0;
    }

    // don't drop over LAN connections
    if (sv_lan_force_rate->integer &&
        NET_IsLanAddress(&cl->netchan.remote_address)) {
        cl->rate = 0;
    }

    // msg command
    val = Info_ValueForKey(cl->userinfo, "msg");
    if (*val) {
        cl->messagelevel = Q_clip(Q_atoi(val), PRINT_LOW, 256);
    }

    /* Capability offers are connection-bound.  A post-confirmation userinfo
     * mutation can only disable the modern path; it never renegotiates an
     * already active legacy packet stream in place. */
    val = Info_ValueForKey(cl->userinfo,
                           WORR_NET_CAPABILITY_USERINFO_KEY);
    uint32_t offered = 0;
    const worr_net_capability_result_v1 capability_result =
        *val ? Worr_NetCapabilitiesParseV1(val, &offered)
             : WORR_NET_CAPABILITY_OK;
    if (capability_result != WORR_NET_CAPABILITY_OK) {
        offered = 0;
        cl->worr_capability_failed = true;
    }
    if (!cl->worr_capability_confirm_sent) {
        cl->worr_capabilities_offered = offered;
    } else if (offered != cl->worr_capabilities_offered) {
        cl->worr_capability_failed = true;
        SV_DropClient(cl, "WORR capability offer changed");
        return;
    }
}


//============================================================================

void SV_RestartFilesystem(void)
{
    if (g_restart_fs && g_restart_fs->RestartFilesystem)
        g_restart_fs->RestartFilesystem();
}

#if USE_SYSCON
void SV_SetConsoleTitle(void)
{
    Sys_SetConsoleTitle(va("%s (port %d%s)",
        sv_hostname->string, net_port->integer,
        sv_running->integer ? "" : ", down"));
}
#endif

static void sv_status_limit_changed(cvar_t *self)
{
    SV_RateInit(&svs.ratelimit_status, self->string);
}

static void sv_auth_limit_changed(cvar_t *self)
{
    SV_RateInit(&svs.ratelimit_auth, self->string);
}

static void sv_rcon_limit_changed(cvar_t *self)
{
    SV_RateInit(&svs.ratelimit_rcon, self->string);
}

static void init_rate_limits(void)
{
    SV_RateInit(&svs.ratelimit_status, sv_status_limit->string);
    SV_RateInit(&svs.ratelimit_auth, sv_auth_limit->string);
    SV_RateInit(&svs.ratelimit_rcon, sv_rcon_limit->string);
}

static void sv_rate_changed(cvar_t *self)
{
    Cvar_ClampInteger(sv_min_rate, 1500, Cvar_ClampInteger(sv_max_rate, 1500, INT_MAX));
}

void sv_sec_timeout_changed(cvar_t *self)
{
    self->integer = 1000 * Cvar_ClampValue(self, 0, 24 * 24 * 60 * 60);
}

void sv_min_timeout_changed(cvar_t *self)
{
    self->integer = 1000 * 60 * Cvar_ClampValue(self, 0, 24 * 24 * 60);
}

static void sv_namechange_limit_changed(cvar_t *self)
{
    client_t *client;

    FOR_EACH_CLIENT(client) {
        SV_RateInit(&client->ratelimit_namechange, self->string);
    }
}

#if USE_SYSCON
static void sv_hostname_changed(cvar_t *self)
{
    SV_SetConsoleTitle();
}
#endif

#if USE_ZLIB
voidpf SV_zalloc(voidpf opaque, uInt items, uInt size)
{
    return SV_Malloc((size_t)items * size);
}

void SV_zfree(voidpf opaque, voidpf address)
{
    Z_Free(address);
}
#endif

/*
===============
SV_Init

Only called at quake2.exe startup, not for each game
===============
*/
void SV_Init(void)
{
    SV_InitOperatorCommands();

    SV_MvdRegister();

#if USE_MVD_CLIENT
    MVD_Register();
#endif

    AC_Register();

    SV_RegisterSavegames();

    Cvar_Get("protocol", STRINGIFY(PROTOCOL_VERSION_DEFAULT), CVAR_SERVERINFO | CVAR_ROM);

    Cvar_Get("skill", "1", CVAR_LATCH);
    Cvar_Get("deathmatch", COM_DEDICATED ? "1" : "0", CVAR_SERVERINFO | CVAR_LATCH);
    Cvar_Get("coop", "0", /*CVAR_SERVERINFO|*/CVAR_LATCH);
    Cvar_Get("cheats", "0", CVAR_SERVERINFO | CVAR_LATCH);
    Cvar_Get("dmflags", va("%i", DF_INSTANT_ITEMS), CVAR_SERVERINFO);
    Cvar_Get("fraglimit", "0", CVAR_SERVERINFO);
    Cvar_Get("timelimit", "0", CVAR_SERVERINFO);

    sv_maxclients = Cvar_Get("maxclients", "8", CVAR_SERVERINFO | CVAR_LATCH);
    sv_reserved_slots = Cvar_Get("sv_reserved_slots", "0", CVAR_LATCH);
    sv_hostname = Cvar_Get("hostname", "noname", CVAR_SERVERINFO | CVAR_ARCHIVE);
#if USE_SYSCON
    sv_hostname->changed = sv_hostname_changed;
#endif
    sv_timeout = Cvar_Get("timeout", "90", 0);
    sv_timeout->changed = sv_sec_timeout_changed;
    sv_timeout->changed(sv_timeout);
    sv_zombietime = Cvar_Get("zombietime", "2", 0);
    sv_zombietime->changed = sv_sec_timeout_changed;
    sv_zombietime->changed(sv_zombietime);
    sv_ghostime = Cvar_Get("sv_ghostime", "6", 0);
    sv_ghostime->changed = sv_sec_timeout_changed;
    sv_ghostime->changed(sv_ghostime);
    sv_idlekick = Cvar_Get("sv_idlekick", "0", 0);
    sv_idlekick->changed = sv_sec_timeout_changed;
    sv_idlekick->changed(sv_idlekick);
    sv_enforcetime = Cvar_Get("sv_enforcetime", "1", 0);
    sv_timescale_time = Cvar_Get("sv_timescale_time", "16", 0);
    sv_timescale_time->changed = sv_sec_timeout_changed;
    sv_timescale_time->changed(sv_timescale_time);
    sv_timescale_warn = Cvar_Get("sv_timescale_warn", "0", 0);
    sv_timescale_kick = Cvar_Get("sv_timescale_kick", "0", 0);
    sv_allow_nodelta = Cvar_Get("sv_allow_nodelta", "1", 0);
    sv_fps = Cvar_Get("sv_fps", "40", CVAR_LATCH);
    sv_force_reconnect = Cvar_Get("sv_force_reconnect", "", CVAR_LATCH);
    sv_show_name_changes = Cvar_Get("sv_show_name_changes", "0", 0);

    sv_airaccelerate = Cvar_Get("sv_airaccelerate", "0", CVAR_LATCH);
    sv_qwmod = Cvar_Get("sv_qwmod", "0", CVAR_LATCH);   //atu QWMod
    sv_public = Cvar_Get("public", "0", CVAR_LATCH);
    sv_password = Cvar_Get("sv_password", "", CVAR_PRIVATE);
    sv_reserved_password = Cvar_Get("sv_reserved_password", "", CVAR_PRIVATE);
    sv_locked = Cvar_Get("sv_locked", "0", 0);
    sv_novis = Cvar_Get("sv_novis", "0", 0);
    sv_shadow_strict_replication = Cvar_Get("sv_shadow_strict_replication", "0", 0);
    sv_worr_native_shadow = Cvar_Get("sv_worr_native_shadow", "0", 0);
    sv_worr_native_event_shadow = Cvar_Get(
        "sv_worr_native_event_shadow", "0", 0);
    sv_worr_native_snapshot_shadow = Cvar_Get(
        "sv_worr_native_snapshot_shadow", "0", 0);
    sv_downloadserver = Cvar_Get("sv_downloadserver", "", 0);
    sv_redirect_address = Cvar_Get("sv_redirect_address", "", 0);

#if USE_DEBUG
    sv_debug = Cvar_Get("sv_debug", "0", 0);
    sv_pad_packets = Cvar_Get("sv_pad_packets", "0", 0);
#endif
    sv_lan_force_rate = Cvar_Get("sv_lan_force_rate", "0", CVAR_LATCH);
    sv_min_rate = Cvar_Get("sv_min_rate", "15000", CVAR_LATCH);
    sv_max_rate = Cvar_Get("sv_max_rate", "60000", CVAR_LATCH);
    sv_max_rate->changed = sv_min_rate->changed = sv_rate_changed;
    sv_max_rate->changed(sv_max_rate);
    sv_calcpings_method = Cvar_Get("sv_calcpings_method", "2", 0);
    sv_changemapcmd = Cvar_Get("sv_changemapcmd", "", 0);
    sv_max_download_size = Cvar_Get("sv_max_download_size", "8388608", 0);
    sv_max_packet_entities = Cvar_Get("sv_max_packet_entities", "0", 0);
    sv_trunc_packet_entities = Cvar_Get("sv_trunc_packet_entities", "1", 0);
    sv_prioritize_entities = Cvar_Get("sv_prioritize_entities", "0", 0);

    sv_strafejump_hack = Cvar_Get("sv_strafejump_hack", "1", CVAR_LATCH);
    sv_waterjump_hack = Cvar_Get("sv_waterjump_hack", "1", CVAR_LATCH);

#if USE_PACKETDUP
    sv_packetdup_hack = Cvar_Get("sv_packetdup_hack", "0", 0);
#endif

    sv_allow_map = Cvar_Get("sv_allow_map", COM_DEDICATED ? "0" : "1", 0);
    sv_cinematics = Cvar_Get("sv_cinematics", "1", 0);

#if USE_SERVER
    sv_recycle = Cvar_Get("sv_recycle", "0", 0);
#endif

    sv_enhanced_setplayer = Cvar_Get("sv_enhanced_setplayer", "0", 0);
    bot_slot_smoke = Cvar_Get("bot_slot_smoke", "0", 0);
    bot_min_players_smoke = Cvar_Get("bot_min_players_smoke", "0", 0);
    bot_profile_smoke = Cvar_Get("bot_profile_smoke", "0", 0);
    bot_profile_smoke_target =
        Cvar_Get("bot_profile_smoke_target", "smoke", 0);
    bot_team_policy_smoke = Cvar_Get("bot_team_policy_smoke", "0", 0);
    bot_warmup_smoke = Cvar_Get("bot_warmup_smoke", "0", 0);
    bot_vote_smoke = Cvar_Get("bot_vote_smoke", "0", 0);
    bot_admin_audit_smoke =
        Cvar_Get("bot_admin_audit_smoke", "0", 0);
    bot_tournament_smoke = Cvar_Get("bot_tournament_smoke", "0", 0);
    bot_mapvote_smoke = Cvar_Get("bot_mapvote_smoke", "0", 0);
    bot_mymap_smoke = Cvar_Get("bot_mymap_smoke", "0", 0);
    bot_intermission_smoke =
        Cvar_Get("bot_intermission_smoke", "0", 0);
    bot_nextmap_smoke = Cvar_Get("bot_nextmap_smoke", "0", 0);
    bot_scoreboard_smoke = Cvar_Get("bot_scoreboard_smoke", "0", 0);
    bot_matchlog_smoke = Cvar_Get("bot_matchlog_smoke", "0", 0);
    bot_frame_command_smoke = Cvar_Get("bot_frame_command_smoke", "0", 0);
    bot_frame_command_smoke_soak_ms =
        Cvar_Get("bot_frame_command_smoke_soak_ms", "600000", 0);
    bot_frame_command_smoke_map_repeat_cycles =
        Cvar_Get("bot_frame_command_smoke_map_repeat_cycles", "2", 0);
    bot_frame_command_smoke_map_repeat_reload_timeout_ms =
        Cvar_Get("bot_frame_command_smoke_map_repeat_reload_timeout_ms",
                 "10000", 0);
    bot_frame_command_smoke_map_repeat_restart =
        Cvar_Get("bot_frame_command_smoke_map_repeat_restart", "0", 0);
    bot_enable = Cvar_Get("bot_enable", "1", 0);
    bot_min_players = Cvar_Get("bot_min_players", "0", 0);
    bot_profile = Cvar_Get("bot_profile", "", 0);

    sv_iplimit = Cvar_Get("sv_iplimit", "3", 0);

    sv_status_show = Cvar_Get("sv_status_show", "2", 0);

    sv_status_limit = Cvar_Get("sv_status_limit", "15", 0);
    sv_status_limit->changed = sv_status_limit_changed;

    sv_uptime = Cvar_Get("sv_uptime", "0", 0);

    sv_auth_limit = Cvar_Get("sv_auth_limit", "1", 0);
    sv_auth_limit->changed = sv_auth_limit_changed;

    sv_rcon_limit = Cvar_Get("sv_rcon_limit", "1", 0);
    sv_rcon_limit->changed = sv_rcon_limit_changed;

    sv_namechange_limit = Cvar_Get("sv_namechange_limit", "5/min", 0);
    sv_namechange_limit->changed = sv_namechange_limit_changed;

    sv_allow_unconnected_cmds = Cvar_Get("sv_allow_unconnected_cmds", "0", 0);

    sv_lrcon_password = Cvar_Get("lrcon_password", "", CVAR_PRIVATE);

    Cvar_Get("sv_features", va("%d", SV_FEATURES), CVAR_ROM);

    sv_tick_rate = Cvar_Get("sv_tick_rate", "40", CVAR_LATCH);

    g_features = Cvar_Get("g_features", "0", CVAR_ROM);

    init_rate_limits();

#if USE_FPS
    // set up default frametime for main loop
    sv.framerate = BASE_FRAMERATE;
    sv.frametime = Com_ComputeFrametime(sv.framerate);
#endif

    // set up default pmove parameters
    PmoveInit(&svs.pmp);
    svs.pmp.extended_server_ver = 1;

    Nav_Init();

#if USE_SYSCON
    SV_SetConsoleTitle();
#endif

    sv_registered = true;
}

/*
==================
SV_FinalMessage

Used by SV_Shutdown to send a final message to all
connected clients before the server goes down. The messages are sent
immediately, not just stuck on the outgoing message list, because the
server is going to totally exit after returning from this function.

Also responsible for freeing all clients.
==================
*/
static void SV_FinalMessage(const char *message, error_type_t type)
{
    client_t    *client;
    netchan_t   *netchan;
    int         i;

    if (LIST_EMPTY(&sv_clientlist))
        return;

    /* Both immediate shutdown transmit loops must be byte-identical legacy
     * traffic, and every callback opaque remains live until later cleanup. */
    FOR_EACH_CLIENT(client) {
        if (client->worr_native_shadow)
            SV_NativeShadowPeerDetachV1(client->worr_native_shadow);
    }

    if (message) {
        q2proto_svc_message_t print_msg = {.type = Q2P_SVC_PRINT, .print = {0}};
        print_msg.print.level = PRINT_HIGH;
        print_msg.print.string = q2proto_make_string(message);
        q2proto_server_multicast_write(Q2P_PROTOCOL_MULTICAST_FLOAT, Q2PROTO_IOARG_SERVER_WRITE_MULTICAST, &print_msg);
    }

    q2proto_svc_message_t goodbye_msg = {.type = type == ERR_RECONNECT ? Q2P_SVC_RECONNECT : Q2P_SVC_DISCONNECT};
    q2proto_server_multicast_write(Q2P_PROTOCOL_MULTICAST_FLOAT, Q2PROTO_IOARG_SERVER_WRITE_MULTICAST, &goodbye_msg);

    // send it twice
    // stagger the packets to crutch operating system limited buffers
    for (i = 0; i < 2; i++) {
        FOR_EACH_CLIENT(client) {
            if (client->state == cs_zombie || client->bot) {
                continue;
            }
            netchan = &client->netchan;
            while (netchan->fragment_pending) {
                Netchan_TransmitNextFragment(netchan);
            }
            Netchan_Transmit(netchan, msg_write.cursize, msg_write.data, 1);
        }
    }

    SZ_Clear(&msg_write);

    // free any data dynamically allocated
    FOR_EACH_CLIENT(client) {
        if (client->state != cs_zombie) {
            SV_CleanClient(client);
        }
        SV_RemoveClient(client);
    }

    List_Init(&sv_clientlist);
}

/*
================
SV_Shutdown

Called when each game quits, from Com_Quit or Com_Error.
Should be safe to call even if server is not fully initialized yet.
================
*/
void SV_Shutdown(const char *finalmsg, error_type_t type)
{
    if (!sv_registered)
        return;

    R_ClearDebugLines();    // for local system

#if USE_MVD_CLIENT
    if (ge != &mvd_ge && !(type & MVD_SPAWN_INTERNAL)) {
        // shutdown MVD client now if not already running the built-in MVD game module
        // don't shutdown if called from internal MVD spawn function (ugly hack)!
        MVD_Shutdown();
    }
    type &= ~MVD_SPAWN_MASK;
#endif

    AC_Disconnect();

    SV_MvdShutdown(type);

    SV_BotRemoveAll();

    SV_FinalMessage(finalmsg, type);
    SV_MasterShutdown();
    SV_ShutdownGameProgs();

    // free current level
    CM_FreeMap(&sv.cm);
    Nav_Unload();
    memset(&sv, 0, sizeof(sv));

    // free server static data
    Z_Free(svs.client_pool);
#if USE_ZLIB
    deflateEnd(&svs.z);
    Z_Free(svs.z_buffer);
#endif
    memset(&svs, 0, sizeof(svs));

    // reset rate limits
    init_rate_limits();

#if USE_FPS
    // set up default frametime for main loop
    sv.framerate = BASE_FRAMERATE;
    sv.frametime = Com_ComputeFrametime(sv.framerate);
#endif

    sv_client = NULL;
    sv_player = NULL;

    Cvar_Set("sv_running", "0");
    Cvar_Set("sv_paused", "0");

#if USE_SYSCON
    SV_SetConsoleTitle();
#endif

    Z_LeakTest(TAG_SERVER);
}
