/*
 * Headless production-wrapper coverage for the native canonical-snapshot
 * path.  The test installs the real client/server netchan callbacks, performs
 * the four-message readiness handshake, queues an exact final server
 * snapshot, and independently reconstructs the matching legacy expectation
 * through the production client snapshot shadow.
 *
 * No socket, renderer, window, input backend, or interactive client is
 * created.  All impairment is applied to copied application payloads.
 */

/* The engine and cgame export same-spelled prediction entry points with
 * different module linkage. This executable intentionally links the cgame
 * implementation, so keep the engine declarations from assigning C linkage
 * to the calls below. */
#define CL_PredictMovement CL_EnginePredictMovement
#define CL_CheckPredictionError CL_EngineCheckPredictionError
#include "client.h"
#undef CL_CheckPredictionError
#undef CL_PredictMovement
#include "client/cgame_entity.h"
#include "client/cgame_event_runtime.h"
#include "client/cgame_prediction_input.h"
#include "client/command_identity.h"
#include "client/consumed_cursor.h"
#include "client/net_capability.h"
#include "client/native_readiness_pilot.h"
#include "client/snapshot_shadow.h"
#include "server/native_shadow.h"
#include "server/snapshot_shadow.h"

#include "common/net/native_carrier.h"
#include "common/net/native_codec.h"
#include "common/net/native_readiness_sideband.h"
#include "common/net/native_snapshot_receiver.h"
#include "common/net/usercmd_delta.h"
#include "cg_canonical_snapshot_timeline.hpp"
#include "cg_event_runtime.hpp"
#include "cg_prediction_authority.hpp"
#include "cg_prediction_config.hpp"
#include "cg_snapshot_timeline.hpp"
#include "shared/prediction_abi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

client_static_t cls{};
client_state_t cl{};
unsigned com_localTime{};
cgame_import_t cgi{};
const cgame_entity_import_t *cgei{};

extern "C" void Worr_TestSetCGamePredictCvar(cvar_t *value);
extern "C" void CG_PredictionInputSetImport(
    const worr_cgame_prediction_input_import_v1 *import);
extern "C" void CG_PredictionInputSetImportV2(
    const worr_cgame_prediction_input_import_v2 *import);
void CG_PredictionAuthority_InitCvars();
void CL_PredictMovement();
void CL_CheckPredictionError();

#if USE_DEBUG
cvar_t *developer{};
#endif

namespace {

constexpr uint32_t kPublicCapabilities =
    WORR_NET_CAP_NATIVE_SNAPSHOT_PUBLIC_MASK;
constexpr uint32_t kPrivateSnapshotCapabilities =
    WORR_NET_CAP_NATIVE_SNAPSHOT_PRIVATE_MASK;
constexpr uint32_t kProjectionEntityCapacity = 64;
constexpr uint32_t kLegacyEntityIndexLimit = MAX_EDICTS_OLD;
constexpr uint32_t kRereleaseEntityIndexLimit = MAX_EDICTS;
constexpr uint32_t kMaxModels = 64;
constexpr uint32_t kMaxSounds = 64;
constexpr uint32_t kEntityCount = 48;
constexpr uint32_t kFirstEntity = 2;
constexpr uint32_t kShadowSlots = 4;
constexpr uint32_t kAreaBytesPerSlot = 8;
constexpr uint32_t kApplicationCeiling =
    WORR_NATIVE_CARRIER_MAX_PACKET_BYTES;
constexpr uint32_t kKeyframeNumber = 10;
constexpr uint64_t kKeyframeTimeUs = UINT64_C(160000);
constexpr uint32_t kCommandEpoch = 37;

static_assert(kEntityCount < kProjectionEntityCapacity);
static_assert(kLegacyEntityIndexLimit >
              kProjectionEntityCapacity);
static_assert(kRereleaseEntityIndexLimit >
              kLegacyEntityIndexLimit);
static_assert(kPrivateSnapshotCapabilities == UINT32_C(0x57));

cvar_t native_shadow_cvar{};
cvar_t input_batch_cvar{};
cvar_t input_batch_offer_cvar{};
cvar_t event_shadow_cvar{};
cvar_t event_presentation_owned_cvar{};
cvar_t snapshot_shadow_mode_cvar{};
cvar_t capability_offer_cvar{};
cvar_t probe_hold_cvar{};
cvar_t projection_shadow_cvar{};
cvar_t projection_debug_cvar{};
cvar_t prediction_authority_cvar{};
cvar_t prediction_debug_cvar{};
cvar_t prediction_enabled_cvar{};
cvar_t paused_cvar{};
cvar_t snapshot_timeline_owned_cvar{};
std::array<byte, 1024> reliable_storage{};

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(
        stderr,
        "native_snapshot_production_virtual_link_test:%d: %s\n",
        line, expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            fail(#expression, __LINE__);                                      \
    } while (0)

bool snapshot_id_equal(worr_snapshot_id_v2 left,
                       worr_snapshot_id_v2 right)
{
    return left.epoch == right.epoch &&
           left.sequence == right.sequence;
}

bool consumed_command_equal(
    worr_snapshot_consumed_command_v2 left,
    worr_snapshot_consumed_command_v2 right)
{
    return left.cursor.epoch == right.cursor.epoch &&
           left.cursor.contiguous_sequence ==
               right.cursor.contiguous_sequence &&
           left.provenance == right.provenance &&
           left.reserved0 == right.reserved0;
}

bool hashes_equal(const worr_snapshot_projection_hashes_v2 &left,
                  const worr_snapshot_projection_hashes_v2 &right)
{
    return left.struct_size == right.struct_size &&
           left.schema_version == right.schema_version &&
           left.endpoint_hash == right.endpoint_hash &&
           left.legacy_parity_hash == right.legacy_parity_hash &&
           left.semantic_player_hash == right.semantic_player_hash &&
           left.semantic_entity_hash == right.semantic_entity_hash &&
           left.semantic_area_hash == right.semantic_area_hash &&
           left.semantic_event_hash == right.semantic_event_hash;
}

bool expectation_parity_equal(
    const worr_snapshot_projection_hashes_v2 &left,
    const worr_snapshot_projection_hashes_v2 &right)
{
    return left.struct_size == right.struct_size &&
           left.schema_version == right.schema_version &&
           left.legacy_parity_hash == right.legacy_parity_hash &&
           left.semantic_player_hash == right.semantic_player_hash &&
           left.semantic_entity_hash == right.semantic_entity_hash &&
           left.semantic_area_hash == right.semantic_area_hash &&
           left.semantic_event_hash == right.semantic_event_hash;
}

struct wire_packet_t {
    std::array<byte, WORR_NATIVE_CARRIER_MAX_PACKET_BYTES> bytes{};
    size_t count{};
    netchan_app_tx_prepare_output_v1_t completion{};
};

struct frame_carrier_t {
    q2proto_svc_frame_t wire{};
    std::array<q2proto_svc_frame_entity_delta_t,
               kEntityCount + 1>
        deltas{};
    uint32_t delta_count{};
    std::array<uint8_t, 4> area{1, 2, 3, 4};
    player_state_t legacy_player{};
    std::array<entity_state_t, kEntityCount> legacy_entities{};
    worr_snapshot_consumed_command_v2 consumed_command{};
    uint32_t canonical_movement_type{PM_NORMAL};
    uint32_t canonical_movement_flags{};
};

struct server_projection_t {
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
};

struct fixture_t {
    sv_native_shadow_peer_v1 server{};
    netchan_t server_channel{};
    sv_snapshot_shadow_peer_v1 *server_snapshot_shadow{};
    bool server_live{};
    uint32_t client_rx_sequence{1};
    uint32_t server_rx_sequence{1};
    uint32_t official_epoch{};
    uint32_t snapshot_epoch{};
    uint32_t transport_epoch{};
    uint32_t entity_index_limit{};
    int server_protocol{PROTOCOL_VERSION_RERELEASE};
    worr_native_readiness_record_v1 deferred_active_confirm{};
    bool active_confirm_deferred{};
};

struct metrics_t {
    uint32_t fragmented_messages{};
    uint32_t server_to_client_losses{};
    uint32_t reordered_deliveries{};
    uint32_t duplicate_deliveries{};
    uint32_t lost_acknowledgements{};
    uint32_t repeat_revalidations{};
    uint32_t exact_cgame_consumes{};
    uint32_t hash_quarantines{};
    uint32_t wrong_epoch_rejections{};
    uint32_t real_domain_activations{};
    uint32_t expectation_window_rollovers{};
    uint32_t complete_timeout_recoveries{};
    uint32_t prediction_authorities{};
    uint32_t prediction_receipt_fence_blocks{};
    uint32_t v2_prediction_replays{};
    uint32_t pending_finalized_matches{};
    uint32_t oracle_matches{};
    uint32_t bounded_corrections{};
    uint32_t threshold_corrections{};
    uint32_t range_127_successes{};
    uint32_t range_128_resets{};
    uint32_t collision_traces{};
    uint32_t point_contents_queries{};
};

metrics_t metrics{};

uint64_t metrics_digest()
{
    uint64_t digest = UINT64_C(1469598103934665603);
    const auto *bytes = reinterpret_cast<const uint8_t *>(&metrics);
    for (size_t index = 0; index < sizeof(metrics); ++index) {
        digest ^= bytes[index];
        digest *= UINT64_C(1099511628211);
    }
    return digest;
}

std::array<centity_t, 2> prediction_entities{};
bsp_t prediction_bsp{};
csurface_t prediction_ground_surface{};
csurface_t prediction_wall_surface{};
cgame_entity_import_t prediction_host{};
uint64_t last_prediction_state_hash{};
uint64_t last_prediction_collision_hash{};
uint64_t last_prediction_replay_hash{};

struct analytic_hit_t {
    bool hit{};
    bool start_solid{};
    bool all_solid{};
    float fraction{1.0f};
    float normal[3]{};
    float distance{};
    csurface_t *surface{};
};

float support_minimum(const float mins[3], const float maxs[3],
                      const float normal[3])
{
    float result = 0.0f;
    for (uint32_t axis = 0; axis < 3; ++axis)
        result += normal[axis] >= 0.0f
                      ? normal[axis] * mins[axis]
                      : normal[axis] * maxs[axis];
    return result;
}

analytic_hit_t trace_plane(const float start[3], const float mins[3],
                           const float maxs[3], const float end[3],
                           const float normal[3], float distance,
                           csurface_t *surface)
{
    analytic_hit_t result{};
    const float expanded = distance -
                           support_minimum(mins, maxs, normal);
    float start_distance = -expanded;
    float end_distance = -expanded;
    for (uint32_t axis = 0; axis < 3; ++axis) {
        start_distance += start[axis] * normal[axis];
        end_distance += end[axis] * normal[axis];
    }
    result.start_solid = start_distance < 0.0f;
    result.all_solid = result.start_solid && end_distance < 0.0f;
    if (result.all_solid) {
        result.hit = true;
        result.fraction = 0.0f;
    } else if (start_distance >= 0.0f && end_distance < 0.0f) {
        result.hit = true;
        result.fraction = start_distance /
                          (start_distance - end_distance);
    }
    if (result.hit) {
        VectorCopy(normal, result.normal);
        result.distance = distance;
        result.surface = surface;
    }
    return result;
}

trace_t analytic_trace(const float start[3], const float mins[3],
                       const float maxs[3], const float end[3],
                       contents_t contents_mask, bool world_entity)
{
    trace_t trace{};
    trace.fraction = 1.0f;
    VectorCopy(end, trace.endpos);
    if ((contents_mask & CONTENTS_SOLID) == 0)
        return trace;

    constexpr float ground_normal[3] = {0.0f, 0.0f, 1.0f};
    constexpr float wall_normal[3] = {-1.0f, 0.0f, 0.0f};
    const analytic_hit_t ground = trace_plane(
        start, mins, maxs, end, ground_normal, 0.0f,
        &prediction_ground_surface);
    const analytic_hit_t wall = trace_plane(
        start, mins, maxs, end, wall_normal, -64.0f,
        &prediction_wall_surface);

    const analytic_hit_t *best = nullptr;
    for (const analytic_hit_t *candidate : {&ground, &wall}) {
        trace.startsolid = trace.startsolid || candidate->start_solid;
        trace.allsolid = trace.allsolid || candidate->all_solid;
        if (candidate->hit &&
            (!best || candidate->fraction < best->fraction)) {
            best = candidate;
        }
    }
    if (!best)
        return trace;

    trace.fraction = std::clamp(best->fraction, 0.0f, 1.0f);
    for (uint32_t axis = 0; axis < 3; ++axis) {
        trace.endpos[axis] = start[axis] +
            (end[axis] - start[axis]) * trace.fraction;
    }
    VectorCopy(best->normal, trace.plane.normal);
    trace.plane.dist = best->distance;
    trace.plane.type = best->normal[0] != 0.0f
                           ? PLANE_X
                           : (best->normal[1] != 0.0f ? PLANE_Y
                                                      : PLANE_Z);
    trace.plane.signbits = best->normal[0] < 0.0f ? 1u : 0u;
    trace.surface = best->surface;
    trace.contents = CONTENTS_SOLID;
    if (world_entity) {
        trace.ent = reinterpret_cast<struct edict_s *>(
            &prediction_entities[0]);
    }
    return trace;
}

void prediction_client_trace(trace_t *trace, const vec3_t start,
                             const vec3_t end, const vec3_t mins,
                             const vec3_t maxs,
                             const struct edict_s *,
                             contents_t contents_mask)
{
    ++metrics.collision_traces;
    *trace = analytic_trace(start, mins, maxs, end, contents_mask, true);
}

void prediction_box_trace(trace_t *trace, const vec3_t start,
                          const vec3_t end, const vec3_t mins,
                          const vec3_t maxs, const mnode_t *, int mask,
                          bool)
{
    ++metrics.collision_traces;
    *trace = analytic_trace(start, mins, maxs, end,
                            static_cast<contents_t>(mask), false);
}

contents_t analytic_point_contents(const float point[3])
{
    return point[2] < 0.0f || point[0] > 64.0f
               ? CONTENTS_SOLID
               : static_cast<contents_t>(0);
}

contents_t prediction_point_contents(const vec3_t point)
{
    ++metrics.point_contents_queries;
    return analytic_point_contents(point);
}

void oracle_trace(void *, worr_prediction_trace_v1 *result,
                  const float start[3], const float mins[3],
                  const float maxs[3], const float end[3],
                  uint32_t, uint32_t contents_mask, uint32_t query_flags)
{
    CHECK(result != nullptr);
    CHECK((query_flags & ~WORR_PREDICTION_TRACE_WORLD_ONLY) == 0);
    const trace_t trace = analytic_trace(
        start, mins, maxs, end, static_cast<contents_t>(contents_mask),
        (query_flags & WORR_PREDICTION_TRACE_WORLD_ONLY) == 0);
    *result = {};
    result->struct_size = sizeof(*result);
    result->schema_version = WORR_PREDICTION_ABI_VERSION;
    result->all_solid = trace.allsolid;
    result->start_solid = trace.startsolid;
    result->fraction = trace.fraction;
    VectorCopy(trace.endpos, result->end);
    VectorCopy(trace.plane.normal, result->plane.normal);
    result->plane.distance = trace.plane.dist;
    result->plane.type = trace.plane.type;
    result->plane.sign_bits = trace.plane.signbits;
    result->contents = trace.contents;
    result->entity_id = trace.ent ? 0u : WORR_PREDICTION_NO_ENTITY;
    result->surface_id = trace.surface
                             ? trace.surface->id
                             : WORR_PREDICTION_NO_ENTITY;
    result->surface_flags = trace.surface
                                ? static_cast<uint32_t>(trace.surface->flags)
                                : 0u;
    result->surface2_id = WORR_PREDICTION_NO_ENTITY;
}

uint32_t oracle_point_contents(void *, const float point[3])
{
    return static_cast<uint32_t>(analytic_point_contents(point));
}

cvar_t *prediction_find_cvar(const char *)
{
    return nullptr;
}

void install_prediction_host()
{
    prediction_entities = {};
    prediction_bsp = {};
    prediction_ground_surface = {};
    prediction_ground_surface.id = 1;
    prediction_wall_surface = {};
    prediction_wall_surface.id = 2;
    prediction_host = {};
    prediction_host.api_version = CGAME_ENTITY_API_VERSION;
    prediction_host.cl = &cl;
    prediction_host.cls = &cls;
    prediction_host.cl_entities = prediction_entities.data();
    prediction_host.Com_LPrintf = Com_LPrintf;
    prediction_host.Com_Error = Com_Error;
    prediction_host.Cvar_Get = Cvar_Get;
    prediction_host.Cvar_FindVar = prediction_find_cvar;
    prediction_host.sv_paused = &paused_cvar;
    prediction_host.CL_Trace = prediction_client_trace;
    prediction_host.CL_PointContents = prediction_point_contents;
    prediction_host.CM_BoxTrace = prediction_box_trace;
    cgei = &prediction_host;
    cl.bsp = &prediction_bsp;
    cls.state = ca_active;
    cls.demo.playback = false;
    prediction_enabled_cvar.integer = 1;
    prediction_authority_cvar.integer = 2;
    Worr_TestSetCGamePredictCvar(&prediction_enabled_cvar);
    CG_PredictionInputSetImport(CL_GetCGamePredictionInputImportV1());
    CG_PredictionInputSetImportV2(CL_GetCGamePredictionInputImportV2());
    CG_PredictionAuthority_InitCvars();
    CHECK(prediction_authority_cvar.integer == 2);
}

usercmd_t prediction_command(uint32_t sequence)
{
    usercmd_t command{};
    command.msec = static_cast<byte>(16u + (sequence % 3u));
    command.angles[1] = static_cast<float>((sequence % 5u) * 3u);
    command.forwardmove = 220.0f +
                          static_cast<float>((sequence % 7u) * 4u);
    command.sidemove = (sequence & 1u) ? 24.0f : -24.0f;
    return command;
}

void set_pending_command(const usercmd_t &command)
{
    cl.cmd = command;
    cl.localmove[0] = command.forwardmove;
    cl.localmove[1] = command.sidemove;
}

void clear_pending_command()
{
    cl.cmd = {};
    cl.localmove[0] = 0.0f;
    cl.localmove[1] = 0.0f;
}

void finalize_prediction_command(uint32_t sequence,
                                 const usercmd_t &command)
{
    CHECK(sequence == cl.cmdNumber + 1u);
    cl.cmds[sequence & CMD_MASK] = command;
    CHECK(CL_CommandIdentityFinalize(sequence));
    worr_prediction_command_v1 canonical{};
    CHECK(NetUsercmd_ToPredictionCommandV1(&command, &canonical));
    CHECK(CL_CommandIdentityRetainCommand(sequence, &canonical));
    worr_command_record_v1 retained{};
    CHECK(CL_CommandIdentityRecordForNumber(sequence, &retained));
    CHECK(retained.command_id.epoch != 0);
    CHECK(retained.command_id.sequence == sequence);
    CHECK(Worr_PredictionHashCommandV1(&retained.command) ==
          Worr_PredictionHashCommandV1(&canonical));
    cl.cmdNumber = sequence;
}

worr_cgame_prediction_input_range_v1 resolve_prediction_range(
    const worr_snapshot_consumed_command_v2 &consumed)
{
    const auto *import = CL_GetCGamePredictionInputImportV2();
    CHECK(import != nullptr);
    worr_cgame_prediction_input_request_v2 request{};
    request.struct_size = sizeof(request);
    request.api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION_V2;
    request.flags =
        WORR_CGAME_PREDICTION_INPUT_REQUEST_CANONICAL_REQUIRED;
    request.consumed_command = consumed;
    worr_cgame_prediction_input_range_v1 range{};
    const uint32_t result = import->ResolveInputRangeForCursor(
        &request, &range);
    CHECK(result == range.result);
    return range;
}

worr_prediction_step_v1 replay_oracle(
    const cg_canonical_prediction_snapshot_v2 &authority,
    const worr_cgame_prediction_input_range_v1 &range)
{
    worr_prediction_step_v1 step{};
    step.struct_size = sizeof(step);
    step.schema_version = WORR_PREDICTION_ABI_VERSION;
    step.state = authority.player.movement;
    CG_GetPredictionConfigV1(&step.config);
    step.snap_initial = 1;
    step.player_entity_id =
        authority.snapshot.controlled_entity.identity.index;
    VectorCopy(authority.player.view_offset, step.view_offset);
    step.trace = oracle_trace;
    step.point_contents = oracle_point_contents;

    for (uint32_t index = 0; index < range.command_count; ++index) {
        step.command = range.commands[index].command;
        CHECK(Worr_PredictionStepV1(&step));
        step.snap_initial = 0;
    }
    if ((range.flags & WORR_CGAME_PREDICTION_INPUT_HAS_PENDING) != 0) {
        step.command = range.pending_command.command;
        CHECK(Worr_PredictionStepV1(&step));
    }
    return step;
}

void require_prediction_record_matches_oracle(
    uint32_t sequence,
    const worr_snapshot_consumed_command_v2 &consumed)
{
    cg_canonical_prediction_snapshot_v2 authority{};
    CHECK(CG_CanonicalSnapshotTimelineCopyPredictionSnapshot(
              static_cast<uint32_t>(cl.frame.number) + 1u,
              &authority) == WORR_SNAPSHOT_TIMELINE_OK);
    const auto range = resolve_prediction_range(consumed);
    CHECK(range.result == WORR_CGAME_PREDICTION_INPUT_OK);
    const auto oracle = replay_oracle(authority, range);
    const uint32_t slot = sequence & CMD_MASK;
    CHECK(cl.predicted_sequences[slot] == sequence);
    CHECK(cl.predicted_state_hashes[slot] == oracle.state_hash);
    CHECK(cl.predicted_collision_hashes[slot] == oracle.collision_hash);
    CHECK(Worr_PredictionHashStateV1(&cl.predicted_states[slot]) ==
          oracle.state_hash);
    CHECK(Worr_PredictionHashStateV1(&oracle.state) ==
          oracle.state_hash);
    last_prediction_state_hash = cl.predicted_state_hashes[slot];
    last_prediction_collision_hash =
        cl.predicted_collision_hashes[slot];
    last_prediction_replay_hash =
        cl.predicted_replay_chain_hashes[slot];
    ++metrics.oracle_matches;
}

q2proto_svc_playerstate_t full_wire_player()
{
    q2proto_svc_playerstate_t player{};
    player.delta_bits = Q2P_PSD_PM_TYPE | Q2P_PSD_PM_FLAGS |
                        Q2P_PSD_PM_GRAVITY | Q2P_PSD_FOV |
                        Q2P_PSD_PM_VIEWHEIGHT;
    player.pm_type = PM_NORMAL;
    player.pm_flags = PMF_ON_GROUND;
    player.pm_gravity = 800;
    player.pm_viewheight = 22;
    player.fov = 100;
    player.statbits = UINT64_C(1) << 2;
    player.stats[2] = 77;
    return player;
}

player_state_t full_legacy_player()
{
    player_state_t player{};
    player.pmove.pm_type = PM_NORMAL;
    player.pmove.pm_flags = PMF_ON_GROUND;
    player.pmove.gravity = 800;
    player.pmove.viewheight = 22;
    player.fov = 100.0f;
    player.stats[2] = 77;
    return player;
}

q2proto_entity_state_delta_t entity_delta(uint16_t model,
                                           uint16_t frame)
{
    q2proto_entity_state_delta_t delta{};
    delta.delta_bits = Q2P_ESD_MODELINDEX | Q2P_ESD_FRAME;
    delta.modelindex = model;
    delta.frame = frame;
    return delta;
}

frame_carrier_t make_keyframe(
    uint32_t entity_index_limit,
    uint32_t frame_number = kKeyframeNumber,
    uint32_t command_epoch = kCommandEpoch,
    uint32_t consumed_sequence = UINT32_MAX)
{
    frame_carrier_t frame{};
    CHECK(entity_index_limit >
          kFirstEntity + kEntityCount);
    frame.wire.serverframe = static_cast<int32_t>(frame_number);
    frame.wire.deltaframe = -1;
    frame.wire.playerstate = full_wire_player();
    frame.legacy_player = full_legacy_player();
    frame.canonical_movement_type = PM_NORMAL;
    frame.canonical_movement_flags = PMF_ON_GROUND;
    frame.consumed_command.cursor.epoch = command_epoch;
    frame.consumed_command.cursor.contiguous_sequence =
        consumed_sequence == UINT32_MAX ? frame_number : consumed_sequence;
    frame.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;

    for (uint32_t index = 0; index < kEntityCount; ++index) {
        const uint32_t entity_number =
            index + 1u == kEntityCount
                ? entity_index_limit - 1u
                : kFirstEntity + index;
        const uint16_t model =
            static_cast<uint16_t>((entity_number % (kMaxModels - 1u)) + 1u);
        auto &carrier = frame.deltas[index];
        carrier.newnum = static_cast<uint16_t>(entity_number);
        carrier.entity_delta = entity_delta(model, 1);

        auto &legacy = frame.legacy_entities[index];
        legacy.number = static_cast<int>(entity_number);
        legacy.modelindex = model;
        legacy.frame = 1;
    }
    frame.delta_count = kEntityCount + 1u;
    frame.deltas[kEntityCount] = {};
    return frame;
}

frame_carrier_t make_delta_frame(const frame_carrier_t &base,
                                 bool client_variant,
                                 uint32_t frame_number =
                                     kKeyframeNumber + 1u,
                                 uint32_t delta_frame_number =
                                     kKeyframeNumber)
{
    frame_carrier_t frame{};
    frame.wire.serverframe = static_cast<int32_t>(frame_number);
    frame.wire.deltaframe = static_cast<int32_t>(delta_frame_number);
    frame.area = base.area;
    frame.legacy_player = base.legacy_player;
    frame.legacy_entities = base.legacy_entities;
    frame.consumed_command = base.consumed_command;
    frame.consumed_command.cursor.contiguous_sequence = frame_number;
    frame.canonical_movement_type = base.canonical_movement_type;
    frame.canonical_movement_flags = base.canonical_movement_flags;

    const uint16_t model =
        static_cast<uint16_t>(client_variant ? 61u : 60u);
    const uint16_t animation =
        static_cast<uint16_t>(client_variant ? 3u : 2u);
    frame.deltas[0].newnum = kFirstEntity;
    frame.deltas[0].entity_delta =
        entity_delta(model, animation);
    frame.deltas[1] = {};
    frame.delta_count = 2;
    frame.legacy_entities[0].modelindex = model;
    frame.legacy_entities[0].frame = animation;
    return frame;
}

void set_frame_consumed_sequence(frame_carrier_t &frame,
                                 uint32_t command_epoch,
                                 uint32_t consumed_sequence)
{
    frame.consumed_command.cursor.epoch = command_epoch;
    frame.consumed_command.cursor.contiguous_sequence = consumed_sequence;
    frame.consumed_command.provenance =
        WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    frame.consumed_command.reserved0 = 0;
}

void set_frame_movement(frame_carrier_t &frame, const float origin[3],
                        const float velocity[3], uint32_t movement_flags,
                        uint32_t movement_type = PM_NORMAL)
{
    frame.canonical_movement_type = movement_type;
    frame.canonical_movement_flags = movement_flags;
    frame.wire.playerstate.delta_bits |=
        Q2P_PSD_PM_TYPE | Q2P_PSD_PM_FLAGS;
    frame.wire.playerstate.pm_type = static_cast<uint8_t>(movement_type);
    frame.wire.playerstate.pm_flags =
        static_cast<uint16_t>(movement_flags);
    frame.wire.playerstate.pm_origin.read.value.delta_bits = UINT8_C(7);
    frame.wire.playerstate.pm_origin.read.diff_bits = 0;
    q2proto_var_coords_set_float(
        &frame.wire.playerstate.pm_origin.read.value.values, origin);
    frame.wire.playerstate.pm_velocity.read.value.delta_bits = UINT8_C(7);
    frame.wire.playerstate.pm_velocity.read.diff_bits = 0;
    q2proto_var_coords_set_float(
        &frame.wire.playerstate.pm_velocity.read.value.values, velocity);

    frame.legacy_player.pmove.pm_type =
        static_cast<pmtype_t>(movement_type);
    frame.legacy_player.pmove.pm_flags =
        static_cast<uint16_t>(movement_flags);
    VectorCopy(origin, frame.legacy_player.pmove.origin);
    VectorCopy(velocity, frame.legacy_player.pmove.velocity);
}

sv_snapshot_shadow_config_v1 snapshot_shadow_config(
    uint32_t snapshot_epoch, uint32_t entity_index_limit)
{
    sv_snapshot_shadow_config_v1 config{};
    config.struct_size = sizeof(config);
    config.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    config.snapshot_epoch = snapshot_epoch;
    config.max_entities = entity_index_limit;
    config.max_models = kMaxModels;
    config.max_sounds = kMaxSounds;
    config.slot_capacity = kShadowSlots;
    config.entities_per_slot =
        kProjectionEntityCapacity - 1u;
    config.area_bytes_per_slot = kAreaBytesPerSlot;
    config.beam_renderfx_mask = RF_BEAM;
    config.legacy_renderfx_allowed_mask = RF_SHELL_LITE_GREEN - 1u;
    config.legacy_beam_clear_mask = RF_GLOW;
    config.extended_entity_state = 1;
    return config;
}

sv_snapshot_shadow_ref_v1 commit_server_frame(
    sv_snapshot_shadow_peer_v1 *shadow, frame_carrier_t &frame,
    uint64_t server_time_us)
{
    CHECK(shadow != nullptr);
    frame.wire.areabits = frame.area.data();
    frame.wire.areabits_len =
        static_cast<uint32_t>(frame.area.size());
    /* q2proto's frame carrier is direction-sensitive: the server writer owns
     * the write union while the independently reconstructed client owns the
     * read union. Keep this shared fixture honest by presenting a server-only
     * copy with the exact absolute authoritative movement values. */
    q2proto_svc_frame_t server_wire = frame.wire;
    q2proto_var_coords_set_float(
        &server_wire.playerstate.pm_origin.write.prev,
        frame.legacy_player.pmove.origin);
    q2proto_var_coords_set_float(
        &server_wire.playerstate.pm_origin.write.current,
        frame.legacy_player.pmove.origin);
    q2proto_var_coords_set_float(
        &server_wire.playerstate.pm_velocity.write.prev,
        frame.legacy_player.pmove.velocity);
    q2proto_var_coords_set_float(
        &server_wire.playerstate.pm_velocity.write.current,
        frame.legacy_player.pmove.velocity);

    sv_snapshot_shadow_frame_v1 input{};
    input.struct_size = sizeof(input);
    input.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    input.wire_frame = &server_wire;
    input.authoritative_server_tick =
        static_cast<uint32_t>(frame.wire.serverframe);
    input.authoritative_tick_delta =
        frame.wire.deltaframe > 0 ? 1u : 0u;
    input.authoritative_server_time_us = server_time_us;
    input.controlled_entity_index = 1;
    input.canonical_movement_type = frame.canonical_movement_type;
    input.canonical_movement_flags = frame.canonical_movement_flags;
    input.team_id = 0;
    input.consumed_command = frame.consumed_command;
    CHECK(SV_SnapshotShadowBeginFrameV1(shadow, &input) ==
          SV_SNAPSHOT_SHADOW_OK);
    for (uint32_t index = 0; index < frame.delta_count; ++index) {
        CHECK(SV_SnapshotShadowCaptureEntityDeltaV1(
                  shadow, &frame.deltas[index]) ==
              SV_SNAPSHOT_SHADOW_OK);
    }
    sv_snapshot_shadow_ref_v1 ref{
        SV_SNAPSHOT_SHADOW_NO_SLOT, 0};
    CHECK(SV_SnapshotShadowCommitFrameV1(shadow, &ref) ==
          SV_SNAPSHOT_SHADOW_OK);
    return ref;
}

server_projection_t server_projection(
    sv_snapshot_shadow_peer_v1 *shadow,
    sv_snapshot_shadow_ref_v1 ref,
    uint32_t entity_index_limit)
{
    server_projection_t projection{};
    CHECK(SV_SnapshotShadowViewV1(
              shadow, ref, &projection.view,
              &projection.hashes) == SV_SNAPSHOT_SHADOW_OK);
    CHECK(projection.view.snapshot != nullptr);
    worr_snapshot_projection_hashes_v2 recomputed{};
    CHECK(Worr_SnapshotProjectionHashesV2(
        &projection.view, entity_index_limit, &recomputed));
    CHECK(hashes_equal(projection.hashes, recomputed));
    CHECK(projection.view.entity_count <=
          WORR_NATIVE_CODEC_MAX_SNAPSHOT_ENTITIES);
    CHECK(projection.view.entity_count == kEntityCount);
    CHECK(projection.view.entities[kEntityCount - 1u]
              .generation.identity.index ==
          entity_index_limit - 1u);
    return projection;
}

void install_legacy_frame(const frame_carrier_t &frame)
{
    cl.frame = {};
    cl.frame.valid = true;
    cl.frame.number = frame.wire.serverframe;
    cl.frame.delta = frame.wire.deltaframe;
    cl.frame.areabytes = static_cast<int>(frame.area.size());
    std::memcpy(cl.frame.areabits, frame.area.data(),
                frame.area.size());
    cl.frame.ps = frame.legacy_player;
    cl.frame.clientNum = 0;
    cl.frame.canonical_server_time_valid = false;
    cl.frame.canonical_server_time_us = 0;
    cl.frame.consumed_command = frame.consumed_command;
    cl.frame.numEntities = static_cast<int>(kEntityCount);
    cl.frame.firstEntity = 0;
    for (uint32_t index = 0; index < kEntityCount; ++index)
        cl.entityStates[index] = frame.legacy_entities[index];
}

worr_native_snapshot_expectation_v1 publish_client_expectation(
    frame_carrier_t &frame, uint64_t server_time_us)
{
    frame.wire.areabits = frame.area.data();
    frame.wire.areabits_len =
        static_cast<uint32_t>(frame.area.size());
    CL_SnapshotShadowBeginFrame(&frame.wire);
    CHECK(CL_SnapshotShadowSetConsumedCommand(
        &frame.consumed_command));
    for (uint32_t index = 0; index < frame.delta_count; ++index)
        CL_SnapshotShadowCaptureEntityDelta(&frame.deltas[index]);
    install_legacy_frame(frame);
    cl.frame.canonical_server_time_us = server_time_us;
    cl.frame.canonical_server_time_valid = true;
    CHECK(CL_SnapshotShadowAcceptFrameEx(
        server_time_us, 1, frame.canonical_movement_type,
        frame.canonical_movement_flags, 0,
        CL_SNAPSHOT_SHADOW_ACCEPT_COMPARE_LEGACY));

    cl_snapshot_shadow_status_v1 status{};
    CHECK(CL_SnapshotShadowGetStatus(&status));
    CHECK(status.last_parity_mismatch == 0);
    CHECK(status.last_accept_flags ==
          CL_SNAPSHOT_SHADOW_ACCEPT_COMPARE_LEGACY);

    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    worr_snapshot_ref_v2 ref{};
    CHECK(CL_SnapshotShadowLatest(&view, &hashes, &ref));
    CHECK(view.snapshot != nullptr);
    worr_native_snapshot_expectation_v1 expectation{};
    CHECK(CL_SnapshotShadowGetNativeExpectation(
              view.snapshot->snapshot_id, &expectation) ==
          CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_AVAILABLE);
    CHECK(hashes_equal(expectation.hashes, hashes));
    return expectation;
}

const worr_cgame_snapshot_timeline_export_v2 *cgame_timeline()
{
    const auto *api = CG_GetCanonicalSnapshotTimelineAPI();
    CHECK(api != nullptr);
    CHECK(api->struct_size == sizeof(*api));
    CHECK(api->api_version ==
          WORR_CGAME_SNAPSHOT_TIMELINE_API_VERSION);
    return api;
}

worr_cgame_snapshot_timeline_status_v2 cgame_status()
{
    worr_cgame_snapshot_timeline_status_v2 status{};
    CHECK(cgame_timeline()->GetStatus(&status));
    CHECK(status.struct_size == sizeof(status));
    CHECK(status.api_version ==
          WORR_CGAME_SNAPSHOT_TIMELINE_API_VERSION);
    return status;
}

void require_prediction_authority_ready(
    const server_projection_t &projection)
{
    CHECK(projection.view.snapshot != nullptr);
    CHECK(projection.view.player != nullptr);
    const auto &expected_snapshot = *projection.view.snapshot;
    const auto &expected_player = *projection.view.player;

    cg_prediction_authority_candidate_v1 candidate{};
    CHECK(CG_CanonicalSnapshotTimelineCopyPredictionSnapshot(
              expected_snapshot.snapshot_id.sequence,
              &candidate.timeline) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    candidate.input.struct_size = sizeof(candidate.input);
    candidate.input.api_version =
        WORR_CGAME_PREDICTION_INPUT_API_VERSION;
    candidate.input.result = WORR_CGAME_PREDICTION_INPUT_OK;
    candidate.input.source =
        WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR;
    candidate.input.flags = WORR_CGAME_PREDICTION_INPUT_CANONICAL;
    candidate.input.consumed_command =
        expected_snapshot.consumed_command;
    candidate.input.authoritative_legacy_sequence =
        expected_snapshot.consumed_command.cursor.contiguous_sequence;
    candidate.input.current_legacy_sequence =
        candidate.input.authoritative_legacy_sequence;

    cg_prediction_authority_expectation_v1 expectation{};
    expectation.snapshot_sequence =
        expected_snapshot.snapshot_id.sequence;
    expectation.server_tick = expected_snapshot.server_tick;
    expectation.server_time_us = expected_snapshot.server_time_us;
    expectation.controlled_entity_index =
        expected_snapshot.controlled_entity.identity.index;

    cg_prediction_authority_v1 authority{};
    CHECK(CG_PredictionAuthoritySelectV1(
              &expectation, &candidate, &authority) ==
          cg_prediction_authority_result_v1::canonical);
    CHECK(authority.result ==
          cg_prediction_authority_result_v1::canonical);
    CHECK(snapshot_id_equal(
        authority.timeline.snapshot.snapshot_id,
        expected_snapshot.snapshot_id));
    CHECK(authority.timeline.snapshot.server_tick ==
          expected_snapshot.server_tick);
    CHECK(authority.timeline.snapshot.server_time_us ==
          expected_snapshot.server_time_us);
    CHECK(consumed_command_equal(
        authority.timeline.snapshot.consumed_command,
        expected_snapshot.consumed_command));
    CHECK(authority.timeline.snapshot.player_hash ==
          expected_snapshot.player_hash);
    CHECK(authority.timeline.snapshot.entity_hash ==
          expected_snapshot.entity_hash);
    CHECK(authority.timeline.snapshot.area_hash ==
          expected_snapshot.area_hash);
    CHECK(authority.timeline.snapshot.event_hash ==
          expected_snapshot.event_hash);
    CHECK(authority.timeline.snapshot.snapshot_hash ==
          expected_snapshot.snapshot_hash);
    CHECK(std::memcmp(
              &authority.timeline.player, &expected_player,
              sizeof(expected_player)) == 0);
    CHECK(consumed_command_equal(
        authority.input.consumed_command,
        expected_snapshot.consumed_command));
    CHECK(authority.input.command_count == 0);
    ++metrics.prediction_authorities;
}

cl_native_readiness_pilot_test_state_t client_state()
{
    cl_native_readiness_pilot_test_state_t state{};
    CHECK(CL_NativeReadinessPilotGetTestState(&state));
    return state;
}

sv_native_shadow_snapshot_status_v1 server_status(
    fixture_t &fixture, uint32_t now)
{
    sv_native_shadow_snapshot_status_v1 status{};
    CHECK(SV_NativeShadowGetSnapshotStatusV1(
        &fixture.server, now, &status));
    CHECK(status.struct_size == sizeof(status));
    CHECK(status.schema_version ==
          SV_NATIVE_SHADOW_SNAPSHOT_STATUS_VERSION);
    return status;
}

void feed_record_to_client(
    const worr_native_readiness_record_v1 &record)
{
    std::array<worr_native_readiness_setting_pair_v1,
               WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT>
        pairs{};
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs.data(),
        static_cast<uint32_t>(pairs.size())));
    CL_NativeReadinessPilotPacketBegin();
    for (const auto &pair : pairs) {
        CHECK(CL_NativeReadinessPilotObserveSetting(
            static_cast<int32_t>(pair.index),
            static_cast<int32_t>(pair.value)));
    }
    CL_NativeReadinessPilotPacketEnd();
}

worr_native_readiness_record_v1 decode_client_record(
    size_t offset)
{
    constexpr size_t kClcSettingWireBytes = 5u;
    constexpr size_t kRecordWireBytes =
        WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT *
        kClcSettingWireBytes;
    CHECK(cls.netchan.message.cursize >=
          offset + kRecordWireBytes);

    worr_native_readiness_sideband_parser_v1 parser{};
    CHECK(Worr_NativeReadinessSidebandParserInitV1(&parser));
    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED);
    const byte *wire = cls.netchan.message.data + offset;
    for (uint32_t index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT;
         ++index) {
        const byte *field = wire + index * kClcSettingWireBytes;
        CHECK(field[0] == static_cast<byte>(clc_setting));
        const auto setting_index = static_cast<int16_t>(
            static_cast<uint16_t>(field[1]) |
            (static_cast<uint16_t>(field[2]) << 8));
        const auto setting_value = static_cast<int16_t>(
            static_cast<uint16_t>(field[3]) |
            (static_cast<uint16_t>(field[4]) << 8));
        const auto result =
            Worr_NativeReadinessSidebandObservePairV1(
                &parser, setting_index, setting_value);
        CHECK(result ==
                  WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED ||
              result ==
                  WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED);
    }
    worr_native_readiness_record_v1 record{};
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(
              &parser, &record) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED);
    return record;
}

sv_native_shadow_observe_result_v1 feed_record_to_server(
    fixture_t &fixture,
    const worr_native_readiness_record_v1 &record,
    uint32_t now,
    worr_native_readiness_record_v1 *server_active_out = nullptr)
{
    std::array<worr_native_readiness_setting_pair_v1,
               WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT>
        pairs{};
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs.data(),
        static_cast<uint32_t>(pairs.size())));
    CHECK(SV_NativeShadowPacketBeginV1(
        &fixture.server, now));
    sv_native_shadow_observe_result_v1 result =
        SV_NATIVE_SHADOW_OBSERVE_NOT_SIDEBAND;
    worr_native_readiness_record_v1 scratch{};
    for (const auto &pair : pairs) {
        result = SV_NativeShadowObserveSettingV1(
            &fixture.server, pair.index, pair.value,
            server_active_out ? server_active_out : &scratch);
    }
    CHECK(SV_NativeShadowPacketEndV1(&fixture.server));
    return result;
}

void confirm_public_capability(uint32_t official_epoch)
{
    CL_NetCapabilityReset(official_epoch);
    CHECK(CL_NetCapabilityPacketBegin());
    CHECK(CL_NetCapabilityObserveSetting(
        WORR_NET_CAPABILITY_CONFIRM_EPOCH_SETTING,
        static_cast<int32_t>(official_epoch)));
    CHECK(CL_NetCapabilityObserveSetting(
        WORR_NET_CAPABILITY_CONFIRM_SUPPORTED_SETTING,
        static_cast<int32_t>(kPublicCapabilities)));
    CHECK(CL_NetCapabilityObserveSetting(
        WORR_NET_CAPABILITY_CONFIRM_NEGOTIATED_SETTING,
        static_cast<int32_t>(kPublicCapabilities)));
    CHECK(CL_NetCapabilityPacketEnd());

    worr_net_capability_state_v1 state{};
    CHECK(CL_NetCapabilityGetState(&state));
    CHECK(state.connection_epoch == official_epoch);
    CHECK(state.phase == WORR_NET_CAPABILITY_CONFIRMED);
    CHECK(state.negotiated == kPublicCapabilities);
    CHECK(CL_NetCapabilityHas(
        WORR_NET_CAP_CONSUMED_COMMAND_CURSOR_V1));
}

worr_native_readiness_record_v1 activate_snapshot_epoch_client(
    fixture_t &fixture, uint32_t now)
{
    com_localTime = now;
    confirm_public_capability(fixture.official_epoch);

    worr_native_readiness_record_v1 challenge{};
    CHECK(SV_NativeShadowBeginEpochBoundV1(
        &fixture.server, fixture.official_epoch,
        kPublicCapabilities, kPublicCapabilities,
        fixture.snapshot_epoch, now, &challenge));
    CHECK(challenge.record_kind ==
          WORR_NATIVE_READINESS_RECORD_CHALLENGE);
    CHECK(challenge.negotiated_capabilities ==
          kPrivateSnapshotCapabilities);
    CHECK(challenge.snapshot_epoch == fixture.snapshot_epoch);
    CHECK(challenge.snapshot_epoch != fixture.official_epoch);
    fixture.transport_epoch = challenge.transport_epoch;
    CHECK(fixture.transport_epoch != 0 &&
          fixture.transport_epoch != fixture.snapshot_epoch);

    const size_t ready_offset = cls.netchan.message.cursize;
    feed_record_to_client(challenge);
    const auto client_ready =
        decode_client_record(ready_offset);
    CHECK(client_ready.record_kind ==
          WORR_NATIVE_READINESS_RECORD_CLIENT_READY);
    CHECK(client_ready.snapshot_epoch ==
          fixture.snapshot_epoch);

    worr_native_readiness_record_v1 server_active{};
    CHECK(feed_record_to_server(
              fixture, client_ready, now + 1u,
              &server_active) ==
          SV_NATIVE_SHADOW_OBSERVE_SERVER_ACTIVE_READY);
    CHECK(server_active.record_kind ==
          WORR_NATIVE_READINESS_RECORD_SERVER_ACTIVE);
    CHECK(server_active.snapshot_epoch ==
          fixture.snapshot_epoch);
    CHECK(SV_NativeShadowServerActiveQueuedV1(
        &fixture.server));

    const size_t confirm_offset =
        cls.netchan.message.cursize;
    com_localTime = now + 1u;
    feed_record_to_client(server_active);
    const auto active_confirm =
        decode_client_record(confirm_offset);
    CHECK(active_confirm.record_kind ==
          WORR_NATIVE_READINESS_RECORD_CLIENT_ACTIVE_CONFIRM);
    CHECK(active_confirm.snapshot_epoch ==
          fixture.snapshot_epoch);

    const auto state = client_state();
    CHECK(state.snapshot_enabled);
    CHECK(state.snapshot_epoch == fixture.snapshot_epoch);
    CHECK(state.private_capabilities ==
          kPrivateSnapshotCapabilities);
    CHECK((state.snapshot_receiver_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_INITIALIZED) != 0);
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    const auto status = server_status(fixture, now + 2u);
    CHECK(status.mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT);
    CHECK(status.sender_initialized == 1);
    CHECK(status.tx_open == 0);
    CHECK(status.snapshot_epoch == fixture.snapshot_epoch);
    return active_confirm;
}

void complete_snapshot_epoch_server(
    fixture_t &fixture,
    const worr_native_readiness_record_v1 &active_confirm,
    uint32_t now)
{
    CHECK(feed_record_to_server(
              fixture, active_confirm, now) ==
          SV_NATIVE_SHADOW_OBSERVE_CLIENT_ACTIVE_CONFIRMED);
    const auto status = server_status(fixture, now);
    CHECK(status.mode == SV_NATIVE_SHADOW_MODE_SNAPSHOT);
    CHECK(status.sender_initialized == 1);
    CHECK(status.tx_open == 1);
    CHECK(status.snapshot_epoch == fixture.snapshot_epoch);
}

void activate_snapshot_epoch(fixture_t &fixture, uint32_t now)
{
    const auto active_confirm =
        activate_snapshot_epoch_client(fixture, now);
    complete_snapshot_epoch_server(
        fixture, active_confirm, now + 2u);
}

void cleanup_fixture(fixture_t &fixture)
{
    if (fixture.server_live) {
        SV_NativeShadowPeerDestroyV1(&fixture.server);
        fixture.server_live = false;
    }
    if (fixture.server_snapshot_shadow) {
        SV_SnapshotShadowDestroyV1(
            fixture.server_snapshot_shadow);
        fixture.server_snapshot_shadow = nullptr;
    }
    CL_NativeReadinessPilotBeforeNetchanClose(&cls.netchan);
    CL_SnapshotShadowShutdown();
    CHECK(CL_SnapshotShadowSetConsumer(nullptr));
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
}

void reset_fixture(fixture_t &fixture, uint32_t now,
                   uint32_t official_epoch,
                   uint32_t snapshot_epoch,
                   uint32_t entity_index_limit =
                       kRereleaseEntityIndexLimit,
                   int server_protocol =
                       PROTOCOL_VERSION_RERELEASE,
                   bool defer_active_confirm = false)
{
    cleanup_fixture(fixture);
    fixture = {};
    std::memset(&cls, 0, sizeof(cls));
    std::memset(&cl, 0, sizeof(cl));
    std::memset(&native_shadow_cvar, 0,
                sizeof(native_shadow_cvar));
    std::memset(&input_batch_cvar, 0,
                sizeof(input_batch_cvar));
    std::memset(&input_batch_offer_cvar, 0,
                sizeof(input_batch_offer_cvar));
    std::memset(&event_shadow_cvar, 0,
                sizeof(event_shadow_cvar));
    std::memset(&event_presentation_owned_cvar, 0,
                sizeof(event_presentation_owned_cvar));
    std::memset(&snapshot_shadow_mode_cvar, 0,
                sizeof(snapshot_shadow_mode_cvar));
    std::memset(&capability_offer_cvar, 0,
                sizeof(capability_offer_cvar));
    std::memset(&probe_hold_cvar, 0,
                sizeof(probe_hold_cvar));
    std::memset(&projection_shadow_cvar, 0,
                sizeof(projection_shadow_cvar));
    std::memset(&projection_debug_cvar, 0,
                sizeof(projection_debug_cvar));
    std::memset(&prediction_authority_cvar, 0,
                sizeof(prediction_authority_cvar));
    std::memset(&prediction_debug_cvar, 0,
                sizeof(prediction_debug_cvar));
    std::memset(&prediction_enabled_cvar, 0,
                sizeof(prediction_enabled_cvar));
    std::memset(&paused_cvar, 0, sizeof(paused_cvar));
    std::memset(&snapshot_timeline_owned_cvar, 0,
                sizeof(snapshot_timeline_owned_cvar));
    reliable_storage.fill(0);
    com_localTime = now;
    fixture.official_epoch = official_epoch;
    fixture.snapshot_epoch = snapshot_epoch;
    fixture.entity_index_limit = entity_index_limit;
    fixture.server_protocol = server_protocol;

    native_shadow_cvar.integer = 1;
    snapshot_shadow_mode_cvar.integer = 1;
    projection_shadow_cvar.integer = 1;
    cls.netchan.type = NETCHAN_NEW;
    cls.netchan.maxpacketlen = kApplicationCeiling;
    cls.serverProtocol = server_protocol;
    cls.realtime = now;
    cl.csr.max_edicts =
        static_cast<uint16_t>(entity_index_limit);
    SZ_InitWrite(&cls.netchan.message, reliable_storage.data(),
                 reliable_storage.size());
    fixture.server_channel.type = NETCHAN_NEW;
    fixture.server_channel.maxpacketlen = kApplicationCeiling;

    CG_EventRuntimeSetAuditEnabled(false);
    CHECK(!CG_EventRuntimeAuditEnabled());
    CHECK(CL_CGameEventRuntimeSetConsumer(nullptr));
    CHECK(CL_CGameEventRuntimeResetConnection() ==
          WORR_CGAME_EVENT_RUNTIME_OK);
    CHECK(CL_SnapshotShadowSetConsumer(cgame_timeline()));
    CL_SnapshotShadowBeginConnection(
        entity_index_limit, kMaxModels, kMaxSounds,
        server_protocol == PROTOCOL_VERSION_RERELEASE);
    cl_snapshot_shadow_status_v1 shadow_status{};
    CHECK(CL_SnapshotShadowGetStatus(&shadow_status));
    CHECK(shadow_status.active == 1);
    CHECK(shadow_status.consumer_attached == 1);

    CL_NativeReadinessPilotRegisterCvar();
    CL_NetCapabilityRegisterOffer();
    CHECK(CL_NativeReadinessPilotBeginConnection(&cls.netchan));
    CHECK(SV_NativeShadowPeerInitModeV1(
        &fixture.server, &fixture.server_channel, now,
        SV_NATIVE_SHADOW_MODE_SNAPSHOT));
    fixture.server_live = true;

    const auto config =
        snapshot_shadow_config(
            snapshot_epoch, entity_index_limit);
    fixture.server_snapshot_shadow =
        SV_SnapshotShadowCreateV1(&config);
    CHECK(fixture.server_snapshot_shadow != nullptr);
    if (defer_active_confirm) {
        fixture.deferred_active_confirm =
            activate_snapshot_epoch_client(fixture, now + 1u);
        fixture.active_confirm_deferred = true;
    } else {
        activate_snapshot_epoch(fixture, now + 1u);
    }
}

wire_packet_t prepare_packet(netchan_t &channel, uint32_t now)
{
    com_localTime = now;
    cls.realtime = now;
    wire_packet_t packet{};
    netchan_app_tx_prepare_info_v1_t info{};
    info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.outgoing_sequence = channel.outgoing_sequence++;
    info.max_application_bytes = kApplicationCeiling;
    info.packet_copies = 1;
    packet.completion.abi_version =
        NETCHAN_APP_TX_HOOK_ABI_V1;
    packet.completion.struct_size =
        sizeof(packet.completion);
    CHECK(channel.app_tx_prepare != nullptr);
    CHECK(channel.app_tx_prepare(
              channel.app_tx_opaque, &info, nullptr,
              packet.bytes.data(), &packet.completion) ==
          NETCHAN_APP_TX_PREPARE_PREPARED);
    packet.count = packet.completion.application_bytes;
    CHECK(packet.count != 0 &&
          packet.count <= packet.bytes.size());
    return packet;
}

void accept_packet(netchan_t &channel,
                   const wire_packet_t &packet)
{
    netchan_app_tx_completion_info_v1_t info{};
    info.abi_version = NETCHAN_APP_TX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.result = NETCHAN_APP_TX_COMPLETION_ACCEPTED;
    info.packet_copies = 1;
    info.accepted_copies = 1;
    info.application_bytes =
        static_cast<uint32_t>(packet.count);
    info.token = packet.completion.token;
    CHECK(channel.app_tx_completion != nullptr);
    channel.app_tx_completion(
        channel.app_tx_opaque, &info, packet.bytes.data());
}

netchan_app_rx_result_t deliver_to_client(
    fixture_t &fixture, const wire_packet_t &packet,
    uint32_t now)
{
    com_localTime = now;
    cls.realtime = now;
    netchan_app_rx_info_v1_t info{};
    info.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.incoming_sequence = fixture.client_rx_sequence++;
    info.message_bytes = static_cast<uint32_t>(packet.count);
    info.application_bytes =
        static_cast<uint32_t>(packet.count);
    netchan_app_rx_output_v1_t output{};
    output.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output.struct_size = sizeof(output);
    CHECK(cls.netchan.app_rx != nullptr);
    const auto result = cls.netchan.app_rx(
        cls.netchan.app_rx_opaque, &info, packet.bytes.data(),
        &output);
    if (result == NETCHAN_APP_RX_EXPOSE_LEGACY)
        CHECK(output.legacy_bytes == 0);
    return result;
}

netchan_app_rx_result_t deliver_to_server(
    fixture_t &fixture, const wire_packet_t &packet,
    uint32_t now)
{
    CHECK(SV_NativeShadowAdvanceAdmissionClockV1(
        &fixture.server, now));
    netchan_app_rx_info_v1_t info{};
    info.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    info.struct_size = sizeof(info);
    info.incoming_sequence = fixture.server_rx_sequence++;
    info.message_bytes = static_cast<uint32_t>(packet.count);
    info.application_bytes =
        static_cast<uint32_t>(packet.count);
    netchan_app_rx_output_v1_t output{};
    output.abi_version = NETCHAN_APP_RX_HOOK_ABI_V1;
    output.struct_size = sizeof(output);
    CHECK(fixture.server_channel.app_rx != nullptr);
    const auto result = fixture.server_channel.app_rx(
        fixture.server_channel.app_rx_opaque, &info,
        packet.bytes.data(), &output);
    if (result == NETCHAN_APP_RX_EXPOSE_LEGACY)
        CHECK(output.legacy_bytes == 0);
    return result;
}

worr_native_carrier_view_v1 carrier_view(
    const wire_packet_t &packet)
{
    worr_native_carrier_view_v1 view{};
    CHECK(Worr_NativeCarrierDecodeV1(
              packet.bytes.data(), packet.count, &view) ==
          WORR_NATIVE_CARRIER_OK);
    return view;
}

worr_native_envelope_frame_info_v1 snapshot_frame(
    const wire_packet_t &packet)
{
    const auto view = carrier_view(packet);
    const worr_native_carrier_entry_v1 *data = nullptr;
    for (uint16_t index = 0; index < view.entry_count; ++index) {
        if (view.entries[index].entry_type !=
            WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
            continue;
        }
        CHECK(data == nullptr);
        data = &view.entries[index];
    }
    CHECK(data != nullptr);
    worr_native_envelope_frame_info_v1 frame{};
    CHECK(Worr_NativeEnvelopeDecodeV1(
              packet.bytes.data() + data->data_offset,
              data->data_bytes, &frame) ==
          WORR_NATIVE_ENVELOPE_DECODE_OK);
    CHECK(frame.record.record_class ==
          WORR_NATIVE_RECORD_SNAPSHOT_V1);
    return frame;
}

std::vector<wire_packet_t> collect_server_burst(
    fixture_t &fixture, uint32_t now,
    worr_snapshot_id_v2 expected_snapshot)
{
    std::vector<wire_packet_t> burst;
    uint16_t fragment_count = 0;
    uint32_t message_sequence = 0;
    for (uint32_t guard = 0; guard < 512; ++guard) {
        CHECK(SV_NativeShadowOutputDueV1(
            &fixture.server, now));
        auto packet =
            prepare_packet(fixture.server_channel, now);
        const auto view = carrier_view(packet);
        CHECK(view.transport_epoch == fixture.transport_epoch);
        uint16_t data_count = 0;
        for (uint16_t index = 0; index < view.entry_count;
             ++index) {
            if (view.entries[index].entry_type ==
                WORR_NATIVE_CARRIER_ENTRY_DATA_V1) {
                ++data_count;
            }
        }
        CHECK(data_count == 1);
        const auto frame = snapshot_frame(packet);
        CHECK(frame.record.object_epoch ==
              expected_snapshot.epoch);
        CHECK(frame.record.object_sequence ==
              expected_snapshot.sequence);
        if (burst.empty()) {
            fragment_count = frame.fragment_count;
            message_sequence = frame.message_sequence;
            CHECK(fragment_count > 1);
        } else {
            CHECK(frame.fragment_count == fragment_count);
            CHECK(frame.message_sequence == message_sequence);
        }
        CHECK(frame.fragment_index == burst.size());
        accept_packet(fixture.server_channel, packet);
        burst.push_back(packet);
        if (frame.fragment_index + 1u == fragment_count)
            break;
    }
    CHECK(!burst.empty());
    CHECK(burst.size() == fragment_count);
    ++metrics.fragmented_messages;
    return burst;
}

void deliver_incomplete_reordered_burst(
    fixture_t &fixture,
    const std::vector<wire_packet_t> &burst, uint32_t now)
{
    CHECK(burst.size() > 2);
    /* Fragment zero is lost after the real server completion callback
     * accepted the local handoff.  Every other fragment arrives in reverse. */
    ++metrics.server_to_client_losses;
    for (size_t index = burst.size(); index-- > 1;) {
        CHECK(deliver_to_client(
                  fixture, burst[index], now) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
        ++metrics.reordered_deliveries;
    }
}

void deliver_retry_reordered_with_duplicate(
    fixture_t &fixture,
    const std::vector<wire_packet_t> &burst, uint32_t now)
{
    CHECK(burst.size() > 1);
    CHECK(deliver_to_client(
              fixture, burst.back(), now) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    ++metrics.duplicate_deliveries;
    for (size_t index = burst.size(); index-- > 0;) {
        CHECK(deliver_to_client(
                  fixture, burst[index], now) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
        ++metrics.reordered_deliveries;
    }
}

void check_ack_only(const wire_packet_t &packet,
                    uint32_t transport_epoch,
                    uint32_t expected_message_sequence)
{
    const auto view = carrier_view(packet);
    CHECK(view.transport_epoch == transport_epoch);
    CHECK(view.entry_count == 1);
    CHECK(view.entries[0].entry_type ==
          WORR_NATIVE_CARRIER_ENTRY_ACK_V1);
    CHECK(view.entries[0].first_message_sequence ==
          expected_message_sequence);
    CHECK(view.entries[0].last_message_sequence ==
          expected_message_sequence);
}

server_projection_t admit_exact_snapshot(
    fixture_t &fixture, frame_carrier_t &frame,
    uint64_t server_time_us, uint32_t queue_time)
{
    const auto ref = commit_server_frame(
        fixture.server_snapshot_shadow, frame, server_time_us);
    const auto projection = server_projection(
        fixture.server_snapshot_shadow, ref,
        fixture.entity_index_limit);
    const auto expectation = publish_client_expectation(
        frame, server_time_us);
    CHECK(snapshot_id_equal(
        expectation.snapshot_id,
        projection.view.snapshot->snapshot_id));
    if (!expectation_parity_equal(expectation.hashes, projection.hashes)) {
        std::fprintf(
            stderr,
            "admit_exact_snapshot parity mismatch id=%u:%u "
            "legacy=%016llx/%016llx player=%016llx/%016llx "
            "entity=%016llx/%016llx area=%016llx/%016llx "
            "event=%016llx/%016llx endpoint=%016llx/%016llx\n",
            expectation.snapshot_id.epoch,
            expectation.snapshot_id.sequence,
            static_cast<unsigned long long>(
                expectation.hashes.legacy_parity_hash),
            static_cast<unsigned long long>(
                projection.hashes.legacy_parity_hash),
            static_cast<unsigned long long>(
                expectation.hashes.semantic_player_hash),
            static_cast<unsigned long long>(
                projection.hashes.semantic_player_hash),
            static_cast<unsigned long long>(
                expectation.hashes.semantic_entity_hash),
            static_cast<unsigned long long>(
                projection.hashes.semantic_entity_hash),
            static_cast<unsigned long long>(
                expectation.hashes.semantic_area_hash),
            static_cast<unsigned long long>(
                projection.hashes.semantic_area_hash),
            static_cast<unsigned long long>(
                expectation.hashes.semantic_event_hash),
            static_cast<unsigned long long>(
                projection.hashes.semantic_event_hash),
            static_cast<unsigned long long>(expectation.hashes.endpoint_hash),
            static_cast<unsigned long long>(projection.hashes.endpoint_hash));
    }
    CHECK(expectation_parity_equal(
        expectation.hashes, projection.hashes));
    CL_NativeReadinessPilotSnapshotExpectationReady();

    const auto before = cgame_status();
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        ref, queue_time));
    const auto burst = collect_server_burst(
        fixture, queue_time,
        projection.view.snapshot->snapshot_id);
    for (size_t index = burst.size(); index-- > 0;) {
        CHECK(deliver_to_client(
                  fixture, burst[index], queue_time) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
    }
    const auto after = cgame_status();
    CHECK(after.accepted == before.accepted + 1u);
    CHECK(after.consume_attempts == before.consume_attempts + 1u);
    CHECK(after.admission_generation ==
          before.admission_generation + 1u);
    CHECK(after.receipt_flags ==
          (WORR_CGAME_SNAPSHOT_RECEIPT_TIMELINE_ACCEPTED |
           WORR_CGAME_SNAPSHOT_RECEIPT_EVENT_FENCE_ACCEPTED));
    CHECK(snapshot_id_equal(
        after.last_snapshot_id,
        projection.view.snapshot->snapshot_id));
    ++metrics.exact_cgame_consumes;

    CHECK(CL_NativeReadinessPilotOutputDue());
    const auto acknowledgement = prepare_packet(
        cls.netchan, queue_time + 1u);
    check_ack_only(
        acknowledgement, fixture.transport_epoch,
        snapshot_frame(burst.front()).message_sequence);
    accept_packet(cls.netchan, acknowledgement);
    CHECK(deliver_to_server(
              fixture, acknowledgement, queue_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    CHECK(server_status(fixture, queue_time + 1u)
              .retained_count == 0);
    return projection;
}

wire_packet_t craft_projection_packet(
    const server_projection_t &projection,
    uint32_t entity_index_limit, uint32_t transport_epoch,
    uint32_t message_sequence)
{
    uint32_t preflight_bytes = 0;
    CHECK(Worr_NativeCodecSnapshotPreflightV1(
              &projection.view, entity_index_limit,
              &preflight_bytes) == WORR_NATIVE_CODEC_OK);
    std::vector<uint8_t> encoded(preflight_bytes);
    size_t encoded_bytes = 0;
    CHECK(Worr_NativeCodecSnapshotEncodeV1(
              &projection.view, entity_index_limit, encoded.data(),
              encoded.size(), &encoded_bytes) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(encoded_bytes == encoded.size());
    worr_native_codec_info_v1 info{};
    worr_native_record_ref_v1 record{};
    CHECK(Worr_NativeCodecInspectV1(
              encoded.data(), encoded.size(), &info) ==
          WORR_NATIVE_CODEC_OK);
    CHECK(Worr_NativeCodecInfoRecordRefV1(&info, &record));

    worr_native_envelope_fragmenter_v1 fragmenter{};
    CHECK(Worr_NativeEnvelopeFragmenterInitV1(
        &fragmenter, transport_epoch, message_sequence, record, 2,
        encoded.data(), static_cast<uint32_t>(encoded.size()),
        SV_NATIVE_SHADOW_SNAPSHOT_MAX_DATAGRAM_BYTES));
    std::array<byte, WORR_NATIVE_ENVELOPE_MAX_DATAGRAM_BYTES>
        datagram{};
    size_t datagram_bytes = 0;
    CHECK(Worr_NativeEnvelopeFragmentNextV1(
              &fragmenter, encoded.data(),
              static_cast<uint32_t>(encoded.size()),
              datagram.data(), datagram.size(),
              &datagram_bytes) ==
          WORR_NATIVE_ENVELOPE_EMIT_OK);

    wire_packet_t packet{};
    worr_native_carrier_entry_v1 entry{};
    entry.struct_size = sizeof(entry);
    entry.schema_version = WORR_NATIVE_CARRIER_ABI_VERSION;
    entry.entry_type = WORR_NATIVE_CARRIER_ENTRY_DATA_V1;
    entry.data_bytes = static_cast<uint32_t>(datagram_bytes);
    CHECK(Worr_NativeCarrierEncodeV1(
              transport_epoch, nullptr, 0, datagram.data(),
              datagram_bytes, &entry, 1, packet.bytes.data(),
              packet.bytes.size(), &packet.count) ==
          WORR_NATIVE_CARRIER_OK);
    return packet;
}

void test_production_snapshot_loss_reorder_ack_loss_and_mismatch()
{
    fixture_t fixture{};
    constexpr uint32_t kOfficialEpoch = 401;
    constexpr uint32_t kSnapshotEpoch = 7001;
    constexpr uint32_t kQueueTime = 1010;
    reset_fixture(
        fixture, 1000, kOfficialEpoch, kSnapshotEpoch);
    CHECK(fixture.entity_index_limit ==
          kRereleaseEntityIndexLimit);
    CHECK(cl.csr.max_edicts ==
          kRereleaseEntityIndexLimit);
    ++metrics.real_domain_activations;

    auto keyframe = make_keyframe(
        fixture.entity_index_limit);
    const auto keyframe_ref = commit_server_frame(
        fixture.server_snapshot_shadow, keyframe,
        kKeyframeTimeUs);
    const auto keyframe_projection = server_projection(
        fixture.server_snapshot_shadow, keyframe_ref,
        fixture.entity_index_limit);
    CHECK(keyframe_projection.view.snapshot->snapshot_id.epoch ==
          kSnapshotEpoch);
    CHECK(keyframe_projection.view.snapshot->snapshot_id.sequence ==
          kKeyframeNumber + 1u);
    CHECK(keyframe_projection.view.entity_count == kEntityCount);
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        keyframe_ref, kQueueTime));

    const auto first_burst = collect_server_burst(
        fixture, kQueueTime,
        keyframe_projection.view.snapshot->snapshot_id);
    deliver_incomplete_reordered_burst(
        fixture, first_burst, kQueueTime);
    auto state = client_state();
    CHECK(state.snapshot_rx_occupied == 1);
    CHECK(state.snapshot_ack_receipts == 0);
    const auto cgame_before = cgame_status();

    const uint32_t retry_time =
        kQueueTime + SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS;
    const auto retry_burst = collect_server_burst(
        fixture, retry_time,
        keyframe_projection.view.snapshot->snapshot_id);
    deliver_retry_reordered_with_duplicate(
        fixture, retry_burst, retry_time);
    state = client_state();
    CHECK(state.snapshot_rx_occupied == 1);
    CHECK(state.snapshot_ack_receipts == 0);
    CHECK(cgame_status().accepted == cgame_before.accepted);
    CHECK(!CL_NativeReadinessPilotOutputDue());

    const auto client_expectation = publish_client_expectation(
        keyframe, kKeyframeTimeUs);
    CHECK(snapshot_id_equal(
        client_expectation.snapshot_id,
        keyframe_projection.view.snapshot->snapshot_id));
    CHECK(expectation_parity_equal(
        client_expectation.hashes,
        keyframe_projection.hashes));
    CL_NativeReadinessPilotSnapshotExpectationReady();

    state = client_state();
    CHECK(state.mode == 2);
    CHECK(state.snapshot_rx_occupied == 0);
    CHECK(state.snapshot_ack_receipts == 1);
    const auto cgame_after = cgame_status();
    CHECK(cgame_after.accepted == cgame_before.accepted + 1u);
    CHECK(cgame_after.consume_attempts ==
          cgame_before.consume_attempts + 1u);
    CHECK(snapshot_id_equal(
        cgame_after.last_snapshot_id,
        keyframe_projection.view.snapshot->snapshot_id));
    CHECK(cgame_after.last_endpoint_hash ==
          keyframe_projection.hashes.endpoint_hash);
    ++metrics.exact_cgame_consumes;
    require_prediction_authority_ready(keyframe_projection);

    /* The first exact reverse receipt is accepted locally and lost on the
     * virtual link. */
    com_localTime = retry_time + 1u;
    CHECK(CL_NativeReadinessPilotOutputDue());
    auto lost_ack =
        prepare_packet(cls.netchan, retry_time + 1u);
    const uint32_t message_sequence =
        snapshot_frame(first_burst.front()).message_sequence;
    check_ack_only(
        lost_ack, fixture.transport_epoch, message_sequence);
    accept_packet(cls.netchan, lost_ack);
    ++metrics.lost_acknowledgements;
    CHECK(server_status(fixture, retry_time + 1u)
              .retained_count == 1);

    /* A complete third real server retry is handed to the transport.  Only
     * its first fragment reaches the client: that exact committed repeat
     * revalidates the live cgame receipt and rearms the same ACK without a
     * second timeline consume. */
    const uint32_t repeat_time =
        retry_time + SV_NATIVE_SHADOW_SNAPSHOT_RESEND_MS;
    const auto repeat_burst = collect_server_burst(
        fixture, repeat_time,
        keyframe_projection.view.snapshot->snapshot_id);
    const auto before_repeat = cgame_status();
    CHECK(deliver_to_client(
              fixture, repeat_burst.front(), repeat_time) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    const auto after_repeat = cgame_status();
    CHECK(after_repeat.accepted == before_repeat.accepted);
    CHECK(after_repeat.consume_attempts ==
          before_repeat.consume_attempts);
    CHECK(client_state().snapshot_ack_receipts == 1);
    ++metrics.repeat_revalidations;

    com_localTime = repeat_time + 1u;
    CHECK(CL_NativeReadinessPilotOutputDue());
    auto final_ack =
        prepare_packet(cls.netchan, repeat_time + 1u);
    check_ack_only(
        final_ack, fixture.transport_epoch, message_sequence);
    accept_packet(cls.netchan, final_ack);
    CHECK(deliver_to_server(
              fixture, final_ack, repeat_time + 1u) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    const auto released =
        server_status(fixture, repeat_time + 1u);
    CHECK(released.retained_count == 0);
    CHECK(released.active_payload_bytes == 0);
    CHECK(released.acknowledgements_applied == 1);
    CHECK(released.payloads_released == 1);
    const uint32_t prior_ack_receipts =
        client_state().snapshot_ack_receipts;
    CHECK(prior_ack_receipts == 1);

    /* The next server final view and the independently accepted legacy view
     * intentionally disagree while retaining the same exact snapshot ID.
     * No consumer call or ACK is authorized; the native receiver quarantines
     * and ownership remains latched through DRAIN. */
    auto server_delta = make_delta_frame(keyframe, false);
    auto client_delta = make_delta_frame(keyframe, true);
    constexpr uint64_t kDeltaTimeUs =
        kKeyframeTimeUs + UINT64_C(16000);
    const auto delta_ref = commit_server_frame(
        fixture.server_snapshot_shadow, server_delta,
        kDeltaTimeUs);
    const auto delta_projection = server_projection(
        fixture.server_snapshot_shadow, delta_ref,
        fixture.entity_index_limit);
    const uint32_t delta_queue_time = repeat_time + 2u;
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        delta_ref, delta_queue_time));
    const auto delta_burst = collect_server_burst(
        fixture, delta_queue_time,
        delta_projection.view.snapshot->snapshot_id);
    for (size_t index = delta_burst.size(); index-- > 0;) {
        CHECK(deliver_to_client(
                  fixture, delta_burst[index],
                  delta_queue_time) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
    }
    CHECK(client_state().snapshot_rx_occupied == 1);
    CHECK(client_state().snapshot_ack_receipts ==
          prior_ack_receipts);

    const auto divergent_expectation =
        publish_client_expectation(client_delta, kDeltaTimeUs);
    CHECK(snapshot_id_equal(
        divergent_expectation.snapshot_id,
        delta_projection.view.snapshot->snapshot_id));
    CHECK(!expectation_parity_equal(
        divergent_expectation.hashes,
        delta_projection.hashes));
    const auto before_mismatch = cgame_status();
    CL_NativeReadinessPilotSnapshotExpectationReady();
    state = client_state();
    CHECK(state.mode == 3);
    CHECK((state.snapshot_receiver_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) != 0);
    CHECK(state.snapshot_ack_receipts == prior_ack_receipts);
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    CHECK(!CL_NativeReadinessPilotOutputDue());
    const auto after_mismatch = cgame_status();
    CHECK(after_mismatch.accepted == before_mismatch.accepted);
    CHECK(after_mismatch.consume_attempts ==
          before_mismatch.consume_attempts);
    CHECK(server_status(fixture, delta_queue_time)
              .retained_count == 1);
    ++metrics.hash_quarantines;

    cleanup_fixture(fixture);
}

void test_production_expectation_window_with_delayed_confirm()
{
    fixture_t fixture{};
    constexpr uint32_t kOfficialEpoch = 403;
    constexpr uint32_t kSnapshotEpoch = 7201;
    constexpr uint32_t kStartTime = 4000;
    constexpr uint32_t kExpectationTotal =
        WORR_NATIVE_SNAPSHOT_RECEIVER_EXPECTATION_CAPACITY + 8u;
    constexpr uint64_t kStartServerTimeUs = UINT64_C(320000);
    sv_snapshot_shadow_ref_v1 latest_ref{
        SV_SNAPSHOT_SHADOW_NO_SLOT, 0};
    worr_native_snapshot_expectation_v1 latest_expectation{};
    server_projection_t latest_projection{};

    reset_fixture(
        fixture, kStartTime, kOfficialEpoch, kSnapshotEpoch,
        kRereleaseEntityIndexLimit, PROTOCOL_VERSION_RERELEASE,
        true);
    CHECK(fixture.active_confirm_deferred);
    CHECK(server_status(fixture, kStartTime + 3u).tx_open == 0);
    const auto cgame_before = cgame_status();

    for (uint32_t index = 0; index < kExpectationTotal; ++index) {
        const uint32_t frame_number = kKeyframeNumber + index;
        const uint64_t server_time_us =
            kStartServerTimeUs +
            static_cast<uint64_t>(index) * UINT64_C(16000);
        auto frame = make_keyframe(
            fixture.entity_index_limit, frame_number);
        latest_ref = commit_server_frame(
            fixture.server_snapshot_shadow, frame,
            server_time_us);
        const auto projection = server_projection(
            fixture.server_snapshot_shadow, latest_ref,
            fixture.entity_index_limit);
        latest_projection = projection;
        latest_expectation =
            publish_client_expectation(frame, server_time_us);
        CHECK(snapshot_id_equal(
            latest_expectation.snapshot_id,
            projection.view.snapshot->snapshot_id));
        CHECK(expectation_parity_equal(
            latest_expectation.hashes,
            projection.hashes));
        if (index != 0) {
            CHECK(latest_expectation.hashes.endpoint_hash !=
                  projection.hashes.endpoint_hash);
        }
        CL_NativeReadinessPilotSnapshotExpectationReady();

        const auto state = client_state();
        CHECK(state.mode == 2);
        CHECK((state.snapshot_receiver_flags &
               WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0);
        CHECK(state.snapshot_rx_occupied == 0);
        CHECK(state.snapshot_ack_receipts == 0);
        CHECK(cgame_status().accepted == cgame_before.accepted);
    }
    ++metrics.expectation_window_rollovers;

    complete_snapshot_epoch_server(
        fixture, fixture.deferred_active_confirm,
        kStartTime + 4u);
    fixture.active_confirm_deferred = false;
    CHECK(server_status(fixture, kStartTime + 4u).tx_open == 1);
    const uint32_t queue_time = kStartTime + 5u;
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        latest_ref, queue_time));
    const auto burst = collect_server_burst(
        fixture, queue_time, latest_expectation.snapshot_id);
    for (size_t index = burst.size(); index-- > 0;) {
        CHECK(deliver_to_client(
                  fixture, burst[index], queue_time) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
    }

    const auto state = client_state();
    CHECK(state.mode == 2);
    CHECK(state.snapshot_rx_occupied == 0);
    CHECK(state.snapshot_ack_receipts == 1);
    const auto cgame_after = cgame_status();
    CHECK(cgame_after.accepted == cgame_before.accepted + 1u);
    CHECK(cgame_after.consume_attempts ==
          cgame_before.consume_attempts + 1u);
    CHECK(snapshot_id_equal(
        cgame_after.last_snapshot_id,
        latest_expectation.snapshot_id));
    ++metrics.exact_cgame_consumes;
    require_prediction_authority_ready(latest_projection);
    ++metrics.real_domain_activations;
    cleanup_fixture(fixture);
}

void test_production_complete_timeout_reuses_pending_slot()
{
    fixture_t fixture{};
    constexpr uint32_t kOfficialEpoch = 405;
    constexpr uint32_t kSnapshotEpoch = 7401;
    constexpr uint32_t kStartTime = 6000;
    constexpr uint32_t kAQueueTime = 6010;
    constexpr uint32_t kBQueueTime =
        kAQueueTime +
        WORR_NATIVE_SNAPSHOT_RECEIVER_COMPLETE_TIMEOUT_TICKS + 1u;
    constexpr uint64_t kAServerTimeUs = UINT64_C(640000);
    constexpr uint64_t kBServerTimeUs = UINT64_C(656000);

    reset_fixture(
        fixture, kStartTime, kOfficialEpoch, kSnapshotEpoch);
    const auto cgame_before = cgame_status();

    auto frame_a = make_keyframe(
        fixture.entity_index_limit, kKeyframeNumber);
    const auto ref_a = commit_server_frame(
        fixture.server_snapshot_shadow, frame_a,
        kAServerTimeUs);
    const auto projection_a = server_projection(
        fixture.server_snapshot_shadow, ref_a,
        fixture.entity_index_limit);
    worr_native_snapshot_expectation_v1 absent{};
    CHECK(CL_SnapshotShadowGetNativeExpectation(
              projection_a.view.snapshot->snapshot_id, &absent) ==
          CL_SNAPSHOT_SHADOW_NATIVE_EXPECTATION_PENDING);
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        ref_a, kAQueueTime));
    const auto burst_a = collect_server_burst(
        fixture, kAQueueTime,
        projection_a.view.snapshot->snapshot_id);
    CHECK(burst_a.size() > 1);
    for (const auto &packet : burst_a) {
        CHECK(deliver_to_client(
                  fixture, packet, kAQueueTime) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
    }
    auto state = client_state();
    CHECK(state.mode == 2);
    CHECK(state.snapshot_rx_occupied == 1);
    CHECK(state.snapshot_ack_receipts == 0);
    CHECK(cgame_status().accepted == cgame_before.accepted);
    CHECK(cgame_status().consume_attempts ==
          cgame_before.consume_attempts);
    CHECK(!CL_NativeReadinessPilotOutputDue());

    auto frame_b = make_keyframe(
        fixture.entity_index_limit, kKeyframeNumber + 1u);
    const auto ref_b = commit_server_frame(
        fixture.server_snapshot_shadow, frame_b,
        kBServerTimeUs);
    const auto projection_b = server_projection(
        fixture.server_snapshot_shadow, ref_b,
        fixture.entity_index_limit);
    const auto expectation_b = publish_client_expectation(
        frame_b, kBServerTimeUs);
    CHECK(snapshot_id_equal(
        expectation_b.snapshot_id,
        projection_b.view.snapshot->snapshot_id));
    CHECK(expectation_parity_equal(
        expectation_b.hashes, projection_b.hashes));
    CL_NativeReadinessPilotSnapshotExpectationReady();
    CHECK(client_state().snapshot_rx_occupied == 1);

    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        ref_b, kBQueueTime));
    const auto burst_b = collect_server_burst(
        fixture, kBQueueTime, expectation_b.snapshot_id);
    CHECK(burst_b.size() > 1);
    CHECK(deliver_to_client(
              fixture, burst_b.front(), kBQueueTime) ==
          NETCHAN_APP_RX_EXPOSE_LEGACY);
    state = client_state();
    CHECK(state.mode == 2);
    CHECK((state.snapshot_receiver_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0);
    CHECK(state.snapshot_rx_occupied == 1);
    CHECK(state.snapshot_ack_receipts == 0);
    CHECK(cgame_status().accepted == cgame_before.accepted);
    CHECK(cgame_status().consume_attempts ==
          cgame_before.consume_attempts);

    for (size_t index = 1; index < burst_b.size(); ++index) {
        CHECK(deliver_to_client(
                  fixture, burst_b[index], kBQueueTime) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
    }
    state = client_state();
    CHECK(state.mode == 2);
    CHECK((state.snapshot_receiver_flags &
           WORR_NATIVE_SNAPSHOT_RECEIVER_QUARANTINED) == 0);
    CHECK(state.snapshot_rx_occupied == 0);
    CHECK(state.snapshot_ack_receipts == 1);
    const auto cgame_after = cgame_status();
    CHECK(cgame_after.accepted == cgame_before.accepted + 1u);
    CHECK(cgame_after.consume_attempts ==
          cgame_before.consume_attempts + 1u);
    CHECK(snapshot_id_equal(
        cgame_after.last_snapshot_id,
        expectation_b.snapshot_id));
    CHECK(cgame_after.last_endpoint_hash ==
          projection_b.hashes.endpoint_hash);

    const uint32_t message_sequence =
        snapshot_frame(burst_b.front()).message_sequence;
    com_localTime = kBQueueTime + 1u;
    CHECK(CL_NativeReadinessPilotOutputDue());
    const auto acknowledgement =
        prepare_packet(cls.netchan, kBQueueTime + 1u);
    check_ack_only(
        acknowledgement, fixture.transport_epoch,
        message_sequence);
    accept_packet(cls.netchan, acknowledgement);

    ++metrics.complete_timeout_recoveries;
    ++metrics.exact_cgame_consumes;
    require_prediction_authority_ready(projection_b);
    ++metrics.real_domain_activations;
    cleanup_fixture(fixture);
}

void test_serialized_v2_prediction_replay_and_reconciliation()
{
    fixture_t fixture{};
    constexpr uint32_t kOfficialEpoch = 8101;
    constexpr uint32_t kSnapshotEpoch = 18101;
    constexpr uint32_t kStartTime = 8000;
    constexpr uint64_t kAuthorityTimeUs = UINT64_C(800000);
    constexpr float initial_origin[3] = {0.0f, 0.0f, 24.0f};
    constexpr float initial_velocity[3] = {0.0f, 0.0f, 0.0f};

    reset_fixture(
        fixture, kStartTime, kOfficialEpoch, kSnapshotEpoch);
    auto authority_frame = make_keyframe(
        fixture.entity_index_limit, kKeyframeNumber,
        kOfficialEpoch, 0);
    set_frame_movement(
        authority_frame, initial_origin, initial_velocity,
        PMF_ON_GROUND);
    (void)admit_exact_snapshot(
        fixture, authority_frame, kAuthorityTimeUs,
        kStartTime + 10u);
    CHECK(authority_frame.consumed_command.cursor.epoch ==
          kOfficialEpoch);
    CHECK(authority_frame.consumed_command.cursor.contiguous_sequence == 0);

    install_prediction_host();
    CG_SnapshotTimeline_Reset(cls.realtime);
    CL_ResetCGamePredictionInputDiagnostics();
    CHECK(cl.cmdNumber == 0);
    const usercmd_t first = prediction_command(1);
    set_pending_command(first);
    const uint32_t trace_count_before = metrics.collision_traces;
    CL_PredictMovement();
    ++metrics.v2_prediction_replays;
    CHECK(metrics.collision_traces > trace_count_before);
    CHECK(metrics.point_contents_queries != 0);
    auto pending_range = resolve_prediction_range(
        authority_frame.consumed_command);
    CHECK(pending_range.result == WORR_CGAME_PREDICTION_INPUT_OK);
    CHECK(pending_range.command_count == 0);
    CHECK((pending_range.flags &
           WORR_CGAME_PREDICTION_INPUT_HAS_PENDING) != 0);
    require_prediction_record_matches_oracle(
        1, authority_frame.consumed_command);
    const worr_prediction_state_v1 pending_state =
        cl.predicted_states[1u & CMD_MASK];
    const uint64_t pending_state_hash =
        cl.predicted_state_hashes[1u & CMD_MASK];
    const uint64_t pending_collision_hash =
        cl.predicted_collision_hashes[1u & CMD_MASK];
    const uint64_t pending_replay_hash =
        cl.predicted_replay_chain_hashes[1u & CMD_MASK];

    finalize_prediction_command(1, first);
    clear_pending_command();
    CL_PredictMovement();
    ++metrics.v2_prediction_replays;
    const auto finalized_range = resolve_prediction_range(
        authority_frame.consumed_command);
    CHECK(finalized_range.result == WORR_CGAME_PREDICTION_INPUT_OK);
    CHECK(finalized_range.command_count == 1);
    CHECK((finalized_range.flags &
           WORR_CGAME_PREDICTION_INPUT_HAS_PENDING) == 0);
    CHECK(std::memcmp(
              &pending_state, &cl.predicted_states[1u & CMD_MASK],
              sizeof(pending_state)) == 0);
    CHECK(pending_state_hash ==
          cl.predicted_state_hashes[1u & CMD_MASK]);
    CHECK(pending_collision_hash ==
          cl.predicted_collision_hashes[1u & CMD_MASK]);
    CHECK(pending_replay_hash ==
          cl.predicted_replay_chain_hashes[1u & CMD_MASK]);
    ++metrics.pending_finalized_matches;
    require_prediction_record_matches_oracle(
        1, authority_frame.consumed_command);

    float bounded_origin[3];
    float bounded_velocity[3];
    VectorCopy(pending_state.origin, bounded_origin);
    VectorCopy(pending_state.velocity, bounded_velocity);
    bounded_origin[1] += 5.0f;
    auto bounded_frame = make_delta_frame(
        authority_frame, false, kKeyframeNumber + 1u,
        kKeyframeNumber);
    set_frame_consumed_sequence(
        bounded_frame, kOfficialEpoch, 1);
    set_frame_movement(
        bounded_frame, bounded_origin, bounded_velocity,
        pending_state.movement_flags, pending_state.movement_type);
    (void)admit_exact_snapshot(
        fixture, bounded_frame, kAuthorityTimeUs + UINT64_C(16000),
        kStartTime + 20u);
    const auto before_bounded =
        CG_SnapshotTimeline_PredictionTelemetry();
    CL_CheckPredictionError();
    const auto after_bounded =
        CG_SnapshotTimeline_PredictionTelemetry();
    CHECK(after_bounded.correction_count ==
          before_bounded.correction_count + 1u);
    CHECK(after_bounded.hard_reset_count ==
          before_bounded.hard_reset_count);
    CHECK(after_bounded.last_correction_reason ==
          cg_prediction_correction_reason_t::state_divergence);
    CHECK(std::fabs(cl.prediction_error[0]) +
              std::fabs(cl.prediction_error[1]) +
              std::fabs(cl.prediction_error[2]) >
          0.0f);
    CHECK(std::fabs(cl.prediction_error[0]) +
              std::fabs(cl.prediction_error[1]) +
              std::fabs(cl.prediction_error[2]) <=
          80.0f);
    ++metrics.bounded_corrections;

    float threshold_origin[3];
    VectorCopy(bounded_origin, threshold_origin);
    threshold_origin[0] += 100.0f;
    auto threshold_frame = make_delta_frame(
        bounded_frame, false, kKeyframeNumber + 2u,
        kKeyframeNumber + 1u);
    set_frame_consumed_sequence(
        threshold_frame, kOfficialEpoch, 1);
    set_frame_movement(
        threshold_frame, threshold_origin, bounded_velocity,
        pending_state.movement_flags, pending_state.movement_type);
    (void)admit_exact_snapshot(
        fixture, threshold_frame,
        kAuthorityTimeUs + UINT64_C(32000),
        kStartTime + 30u);
    const auto before_threshold =
        CG_SnapshotTimeline_PredictionTelemetry();
    CL_CheckPredictionError();
    const auto after_threshold =
        CG_SnapshotTimeline_PredictionTelemetry();
    CHECK(after_threshold.correction_count ==
          before_threshold.correction_count + 1u);
    CHECK(after_threshold.hard_reset_count ==
          before_threshold.hard_reset_count + 1u);
    CHECK(after_threshold.last_correction_reason ==
          cg_prediction_correction_reason_t::
              correction_threshold_exceeded);
    CHECK(cl.prediction_error[0] == 0.0f &&
          cl.prediction_error[1] == 0.0f &&
          cl.prediction_error[2] == 0.0f);
    ++metrics.threshold_corrections;
    cleanup_fixture(fixture);

    constexpr uint32_t kBoundaryOfficialEpoch = 8102;
    constexpr uint32_t kBoundarySnapshotEpoch = 18102;
    reset_fixture(
        fixture, kStartTime + 100u, kBoundaryOfficialEpoch,
        kBoundarySnapshotEpoch);
    auto boundary_frame = make_keyframe(
        fixture.entity_index_limit, kKeyframeNumber,
        kBoundaryOfficialEpoch, 0);
    set_frame_movement(
        boundary_frame, initial_origin, initial_velocity,
        PMF_ON_GROUND);
    (void)admit_exact_snapshot(
        fixture, boundary_frame,
        kAuthorityTimeUs + UINT64_C(100000),
        kStartTime + 110u);
    install_prediction_host();
    CL_ResetCGamePredictionInputDiagnostics();
    for (uint32_t sequence = 1; sequence <= 127; ++sequence) {
        finalize_prediction_command(
            sequence, prediction_command(sequence));
    }
    clear_pending_command();
    const auto range_127 = resolve_prediction_range(
        boundary_frame.consumed_command);
    CHECK(range_127.result == WORR_CGAME_PREDICTION_INPUT_OK);
    CHECK(range_127.command_count == 127);
    CL_PredictMovement();
    ++metrics.v2_prediction_replays;
    CHECK(cl.predicted_sequences[127u & CMD_MASK] == 127u);
    require_prediction_record_matches_oracle(
        127, boundary_frame.consumed_command);
    ++metrics.range_127_successes;

    finalize_prediction_command(128, prediction_command(128));
    const auto range_128 = resolve_prediction_range(
        boundary_frame.consumed_command);
    CHECK(range_128.result ==
          WORR_CGAME_PREDICTION_INPUT_RANGE_EXHAUSTED);
    CHECK((range_128.flags &
           WORR_CGAME_PREDICTION_INPUT_HARD_RESYNC_REQUIRED) != 0);
    CL_PredictMovement();
    ++metrics.v2_prediction_replays;
    for (uint32_t slot = 0; slot < CMD_BACKUP; ++slot)
        CHECK(cl.predicted_sequences[slot] == 0);
    cl_cgame_prediction_input_diagnostics_v1 diagnostics{};
    CHECK(CL_GetCGamePredictionInputDiagnostics(&diagnostics));
    CHECK(diagnostics.last_result ==
          WORR_CGAME_PREDICTION_INPUT_RANGE_EXHAUSTED);
    CHECK(diagnostics.failures != 0);
    ++metrics.range_128_resets;
    cleanup_fixture(fixture);
}

void test_prediction_receipt_requires_event_fence()
{
    fixture_t fixture{};
    constexpr uint32_t kOfficialEpoch = 8103;
    constexpr uint32_t kSnapshotEpoch = 18103;
    constexpr uint32_t kStartTime = 8200;
    constexpr uint64_t kServerTimeUs = UINT64_C(900000);

    reset_fixture(
        fixture, kStartTime, kOfficialEpoch, kSnapshotEpoch);
    auto frame = make_keyframe(
        fixture.entity_index_limit, kKeyframeNumber,
        kOfficialEpoch, 0);
    const auto ref = commit_server_frame(
        fixture.server_snapshot_shadow, frame, kServerTimeUs);
    const auto projection = server_projection(
        fixture.server_snapshot_shadow, ref,
        fixture.entity_index_limit);
    CHECK(projection.view.snapshot != nullptr);

    const auto before = cgame_status();
    CHECK(CG_EventRuntimeResetSnapshot(kSnapshotEpoch + 1u) ==
          CG_EVENT_RUNTIME_OK);
    CHECK(cgame_timeline()->ConsumeCanonicalSnapshot(
        &projection.view, &projection.hashes,
        kServerTimeUs + UINT64_C(1000)));

    const auto after = cgame_status();
    CHECK(after.accepted == before.accepted + 1u);
    CHECK(after.receipt_flags ==
          WORR_CGAME_SNAPSHOT_RECEIPT_TIMELINE_ACCEPTED);
    CHECK(after.last_event_fence_result ==
          CG_EVENT_RUNTIME_WRONG_EPOCH);

    cg_canonical_prediction_snapshot_v2 authority{};
    CHECK(CG_CanonicalSnapshotTimelineCopyPredictionSnapshot(
              projection.view.snapshot->snapshot_id.sequence,
              &authority) == WORR_SNAPSHOT_TIMELINE_NOT_FOUND);

    cg_canonical_snapshot_timeline_diagnostics_v1 diagnostics{};
    CHECK(CG_CanonicalSnapshotTimelineGetDiagnostics(&diagnostics));
    worr_snapshot_v2 copied{};
    CHECK(CG_CanonicalSnapshotTimelineCopySnapshot(
              diagnostics.latest_ref, &copied) ==
          WORR_SNAPSHOT_TIMELINE_OK);
    CHECK(snapshot_id_equal(
        copied.snapshot_id,
        projection.view.snapshot->snapshot_id));

    ++metrics.prediction_receipt_fence_blocks;
    cleanup_fixture(fixture);
}

void test_legacy_entity_domain_activation_and_admission()
{
    fixture_t fixture{};
    constexpr uint32_t kOfficialEpoch = 404;
    constexpr uint32_t kSnapshotEpoch = 7301;
    constexpr uint32_t kStartTime = 5000;
    constexpr uint64_t kServerTimeUs = UINT64_C(480000);

    reset_fixture(
        fixture, kStartTime, kOfficialEpoch, kSnapshotEpoch,
        kLegacyEntityIndexLimit, PROTOCOL_VERSION_DEFAULT);
    CHECK(fixture.entity_index_limit ==
          kLegacyEntityIndexLimit);
    CHECK(cl.csr.max_edicts ==
          kLegacyEntityIndexLimit);

    auto frame = make_keyframe(fixture.entity_index_limit);
    const auto ref = commit_server_frame(
        fixture.server_snapshot_shadow, frame, kServerTimeUs);
    const auto projection = server_projection(
        fixture.server_snapshot_shadow, ref,
        fixture.entity_index_limit);
    const auto expectation =
        publish_client_expectation(frame, kServerTimeUs);
    CHECK(snapshot_id_equal(
        expectation.snapshot_id,
        projection.view.snapshot->snapshot_id));
    CHECK(expectation_parity_equal(
        expectation.hashes, projection.hashes));
    CL_NativeReadinessPilotSnapshotExpectationReady();

    const auto before = cgame_status();
    const uint32_t queue_time = kStartTime + 10u;
    CHECK(SV_NativeShadowQueueSnapshotV1(
        &fixture.server, fixture.server_snapshot_shadow,
        ref, queue_time));
    const auto burst = collect_server_burst(
        fixture, queue_time, expectation.snapshot_id);
    for (const auto &packet : burst) {
        CHECK(deliver_to_client(
                  fixture, packet, queue_time) ==
              NETCHAN_APP_RX_EXPOSE_LEGACY);
    }
    const auto state = client_state();
    CHECK(state.mode == 2);
    CHECK(state.snapshot_rx_occupied == 0);
    CHECK(state.snapshot_ack_receipts == 1);
    const auto after = cgame_status();
    CHECK(after.accepted == before.accepted + 1u);
    CHECK(after.consume_attempts ==
          before.consume_attempts + 1u);
    ++metrics.exact_cgame_consumes;
    require_prediction_authority_ready(projection);
    ++metrics.real_domain_activations;
    cleanup_fixture(fixture);
}

void test_wrong_snapshot_epoch_fails_closed()
{
    fixture_t fixture{};
    constexpr uint32_t kOfficialEpoch = 402;
    constexpr uint32_t kSnapshotEpoch = 7101;
    constexpr uint32_t kWrongSnapshotEpoch = 7102;
    reset_fixture(
        fixture, 3000, kOfficialEpoch, kSnapshotEpoch);

    const auto wrong_config =
        snapshot_shadow_config(
            kWrongSnapshotEpoch, fixture.entity_index_limit);
    auto *wrong_shadow =
        SV_SnapshotShadowCreateV1(&wrong_config);
    CHECK(wrong_shadow != nullptr);
    auto wrong_frame = make_keyframe(
        fixture.entity_index_limit);
    const auto wrong_ref = commit_server_frame(
        wrong_shadow, wrong_frame, kKeyframeTimeUs);
    const auto wrong_projection =
        server_projection(
            wrong_shadow, wrong_ref,
            fixture.entity_index_limit);
    CHECK(wrong_projection.view.snapshot->snapshot_id.epoch ==
          kWrongSnapshotEpoch);

    const auto before = cgame_status();
    const auto wrong_packet = craft_projection_packet(
        wrong_projection, fixture.entity_index_limit,
        fixture.transport_epoch, 77);
    CHECK(deliver_to_client(
              fixture, wrong_packet, 3010) ==
          NETCHAN_APP_RX_REJECT);
    const auto state = client_state();
    CHECK(state.mode == 3);
    CHECK(state.snapshot_ack_receipts == 0);
    CHECK(CL_NativeReadinessPilotOwnsSnapshotTimeline());
    CHECK(!CL_NativeReadinessPilotOutputDue());
    const auto after = cgame_status();
    CHECK(after.accepted == before.accepted);
    CHECK(after.consume_attempts == before.consume_attempts);
    ++metrics.wrong_epoch_rejections;

    SV_SnapshotShadowDestroyV1(wrong_shadow);
    cleanup_fixture(fixture);
}

} // namespace

extern "C" cvar_t *Cvar_Get(
    const char *name, const char *, int)
{
    if (name &&
        std::strcmp(name, "cg_prediction_snapshot_authority") == 0) {
        return &prediction_authority_cvar;
    }
    if (name &&
        std::strcmp(name, "cl_worr_native_event_shadow") == 0) {
        return &event_shadow_cvar;
    }
    if (name &&
        std::strcmp(name, "cl_worr_native_input_batch") == 0) {
        return &input_batch_cvar;
    }
    if (name &&
        std::strcmp(name, WORR_NATIVE_INPUT_BATCH_USERINFO_KEY) == 0) {
        return &input_batch_offer_cvar;
    }
    if (name &&
        std::strcmp(
            name, "cl_worr_native_event_presentation_owned") == 0) {
        return &event_presentation_owned_cvar;
    }
    if (name &&
        std::strcmp(name, "cl_worr_native_snapshot_shadow") == 0) {
        return &snapshot_shadow_mode_cvar;
    }
    if (name &&
        std::strcmp(name, WORR_NET_CAPABILITY_USERINFO_KEY) == 0) {
        return &capability_offer_cvar;
    }
    if (name &&
        std::strcmp(name, "cl_worr_native_shadow_probe_hold") == 0) {
        return &probe_hold_cvar;
    }
    if (name &&
        std::strcmp(
            name, "cl_worr_native_snapshot_timeline_owned") == 0) {
        return &snapshot_timeline_owned_cvar;
    }
    if (name &&
        std::strcmp(name, "cl_snapshot_shadow_debug") == 0) {
        return &projection_debug_cvar;
    }
    if (name &&
        std::strcmp(name, "cl_snapshot_shadow") == 0) {
        return &projection_shadow_cvar;
    }
    return &native_shadow_cvar;
}

extern "C" bool CL_AdaptiveInputGetOutputV1(
    worr_adaptive_input_output_v1 *output_out)
{
    if (output_out)
        *output_out = {};
    return false;
}

extern "C" void Cvar_SetByVar(cvar_t *var, const char *value, from_t)
{
    CHECK(var != nullptr && value != nullptr);
    var->integer = static_cast<int32_t>(std::strtoul(value, nullptr, 10));
    var->value = static_cast<float>(var->integer);
}

extern "C" bool Netchan_SetApplicationTxHook(
    netchan_t *channel, netchan_app_tx_prepare_fn prepare,
    netchan_app_tx_completion_fn completion, void *opaque)
{
    if (!channel || channel->type != NETCHAN_NEW ||
        (!!prepare != !!completion)) {
        return false;
    }
    channel->app_tx_prepare = prepare;
    channel->app_tx_completion = completion;
    channel->app_tx_opaque = prepare ? opaque : nullptr;
    return true;
}

extern "C" bool Netchan_SetApplicationRxHook(
    netchan_t *channel, netchan_app_rx_fn receive, void *opaque)
{
    if (!channel || channel->type != NETCHAN_NEW)
        return false;
    channel->app_rx = receive;
    channel->app_rx_opaque = receive ? opaque : nullptr;
    return true;
}

extern "C" void SZ_Init(sizebuf_t *buffer, void *data, size_t size,
                         const char *tag)
{
    std::memset(buffer, 0, sizeof(*buffer));
    buffer->data = static_cast<byte *>(data);
    buffer->maxsize = static_cast<uint32_t>(size);
    buffer->tag = tag;
}

extern "C" void SZ_InitWrite(
    sizebuf_t *buffer, void *data, size_t size)
{
    SZ_Init(
        buffer, data, size,
        "native snapshot production virtual link test");
    buffer->allowoverflow = true;
}

extern "C" void SZ_Clear(sizebuf_t *buffer)
{
    buffer->cursize = 0;
    buffer->readcount = 0;
    buffer->overflowed = false;
}

extern "C" void *SZ_GetSpace(sizebuf_t *buffer, size_t size)
{
    CHECK(buffer && buffer->data &&
          size <= buffer->maxsize);
    if (size > buffer->maxsize - buffer->cursize) {
        CHECK(buffer->allowoverflow);
        SZ_Clear(buffer);
        buffer->overflowed = true;
    }
    byte *result = buffer->data + buffer->cursize;
    buffer->cursize += static_cast<uint32_t>(size);
    return result;
}

extern "C" q2proto_error_t q2proto_client_write(
    q2proto_clientcontext_t *, uintptr_t io_argument,
    const q2proto_clc_message_t *message)
{
    if (!io_argument || !message ||
        message->type != Q2P_CLC_SETTING) {
        return Q2P_ERR_BAD_COMMAND;
    }
    auto *io =
        reinterpret_cast<q2protoio_ioarg_t *>(io_argument);
    if (!io->sz_write)
        return Q2P_ERR_BAD_DATA;
    byte *wire =
        static_cast<byte *>(SZ_GetSpace(io->sz_write, 5));
    wire[0] = static_cast<byte>(clc_setting);
    const uint16_t index =
        static_cast<uint16_t>(message->setting.index);
    const uint16_t value =
        static_cast<uint16_t>(message->setting.value);
    wire[1] = static_cast<byte>(index);
    wire[2] = static_cast<byte>(index >> 8);
    wire[3] = static_cast<byte>(value);
    wire[4] = static_cast<byte>(value >> 8);
    return Q2P_ERR_SUCCESS;
}

extern "C" void *Z_Mallocz(size_t size)
{
    return std::calloc(1, size);
}

extern "C" void Z_Free(void *pointer)
{
    std::free(pointer);
}

extern "C" void Com_LPrintf(
    print_type_t, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
}

extern "C" void Com_Error(
    error_type_t, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
    std::exit(EXIT_FAILURE);
}

int main()
{
    test_production_snapshot_loss_reorder_ack_loss_and_mismatch();
    test_production_expectation_window_with_delayed_confirm();
    test_production_complete_timeout_reuses_pending_slot();
    test_serialized_v2_prediction_replay_and_reconciliation();
    test_prediction_receipt_requires_event_fence();
    test_legacy_entity_domain_activation_and_admission();
    test_wrong_snapshot_epoch_fails_closed();
    std::printf(
        "native_snapshot_production_virtual_link_test: ok "
        "fragmented=%u s2c_loss=%u reordered=%u duplicates=%u "
        "ack_loss=%u repeat_revalidate=%u cgame_once=%u "
        "hash_quarantine=%u wrong_epoch=%u real_domains=%u "
        "expectation_rollovers=%u timeout_recoveries=%u "
        "prediction_ready=%u receipt_fence_blocks=%u "
        "v2_replays=%u pending_finalized=%u "
        "oracle_matches=%u bounded_corrections=%u "
        "threshold_corrections=%u range127=%u range128_reset=%u "
        "collision_traces=%u point_contents=%u "
        "state_hash=%016llx collision_hash=%016llx "
        "replay_hash=%016llx digest=%016llx\n",
        metrics.fragmented_messages,
        metrics.server_to_client_losses,
        metrics.reordered_deliveries,
        metrics.duplicate_deliveries,
        metrics.lost_acknowledgements,
        metrics.repeat_revalidations,
        metrics.exact_cgame_consumes,
        metrics.hash_quarantines,
        metrics.wrong_epoch_rejections,
        metrics.real_domain_activations,
        metrics.expectation_window_rollovers,
        metrics.complete_timeout_recoveries,
        metrics.prediction_authorities,
        metrics.prediction_receipt_fence_blocks,
        metrics.v2_prediction_replays,
        metrics.pending_finalized_matches,
        metrics.oracle_matches,
        metrics.bounded_corrections,
        metrics.threshold_corrections,
        metrics.range_127_successes,
        metrics.range_128_resets,
        metrics.collision_traces,
        metrics.point_contents_queries,
        static_cast<unsigned long long>(last_prediction_state_hash),
        static_cast<unsigned long long>(last_prediction_collision_hash),
        static_cast<unsigned long long>(last_prediction_replay_hash),
        static_cast<unsigned long long>(metrics_digest()));
    return EXIT_SUCCESS;
}
