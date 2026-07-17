// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "cg_entity_local.h"
#include "cg_prediction_authority.hpp"
#include "cg_event_runtime.hpp"
#include "cg_local_interaction.hpp"
#include "cg_snapshot_timeline.hpp"
#include "shared/cgame_prediction.h"
#include "shared/prediction_abi.h"

#include <climits>
#include <cstdint>

#if USE_DEBUG
#define CG_SHOWMISS(...) \
    do { \
        cvar_t *showmiss = Cvar_FindVar("cl_showmiss"); \
        if (showmiss && showmiss->integer) { \
            Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); \
        } \
    } while (0)
#else
#define CG_SHOWMISS(...)
#endif

extern "C" void CG_GetPredictionConfigV1(
    worr_prediction_config_v1 *config);

static const worr_cgame_prediction_input_import_v1 *prediction_input_import;
static const worr_cgame_prediction_input_import_v2 *prediction_input_import_v2;
static cvar_t *cg_prediction_snapshot_authority;

enum : std::uint32_t {
    CG_PREDICTION_SNAPSHOT_AUTHORITY_LEGACY = 0,
    CG_PREDICTION_SNAPSHOT_AUTHORITY_AUDIT = 1,
    CG_PREDICTION_SNAPSHOT_AUTHORITY_PROMOTE = 2,
};

struct cg_prediction_frame_authority_t {
    worr_cgame_prediction_input_range_v1 input;
    worr_prediction_state_v1 movement;
    float view_angles[3];
    float view_offset[3];
    float screen_blend[4];
    std::uint32_t controlled_entity_index;
    std::uint32_t discontinuity_flags;
    std::uint32_t input_result;
    cg_prediction_authority_result_v1 canonical_result;
    worr_snapshot_id_v2 snapshot_id;
    std::uint8_t rdflags;
    bool canonical;
    bool blocked;
    bool history_reset_required;
};

struct cg_prediction_authority_stats_t {
    std::uint64_t attempts;
    std::uint64_t canonical;
    std::uint64_t unavailable;
    std::uint64_t audit_matches;
    std::uint64_t audit_mismatches;
    std::uint64_t blocked;
    std::uint32_t last_result;
    std::uint32_t last_debug_time_ms;
};

static cg_prediction_authority_stats_t prediction_authority_stats;
static worr_snapshot_id_v2 prediction_discontinuity_reset_snapshot;

extern "C" void CG_PredictionInputSetImport(
    const worr_cgame_prediction_input_import_v1 *import)
{
    prediction_input_import = import;
}

extern "C" void CG_PredictionInputSetImportV2(
    const worr_cgame_prediction_input_import_v2 *import)
{
    prediction_input_import_v2 = import;
}

void CG_PredictionAuthority_InitCvars()
{
    if (!cgei)
        return;
    cg_prediction_snapshot_authority = Cvar_Get(
        "cg_prediction_snapshot_authority", "0", CVAR_NOARCHIVE);
}

static void CG_PredictionPlaneFromTrace(worr_prediction_plane_v1 *out,
                                        const cplane_t *plane)
{
    memset(out, 0, sizeof(*out));
    VectorCopy(plane->normal, out->normal);
    out->distance = plane->dist;
    out->type = plane->type;
    out->sign_bits = plane->signbits;
}

static void CG_TracePlaneFromPrediction(cplane_t *out,
                                        const worr_prediction_plane_v1 *plane)
{
    memset(out, 0, sizeof(*out));
    VectorCopy(plane->normal, out->normal);
    out->dist = plane->distance;
    out->type = plane->type;
    out->signbits = plane->sign_bits;
}

static uint32_t CG_PredictionEntityId(const struct edict_s *entity)
{
    if (!entity || !cl_entities)
        return WORR_PREDICTION_NO_ENTITY;

    const uintptr_t base = reinterpret_cast<uintptr_t>(cl_entities);
    const uintptr_t value = reinterpret_cast<uintptr_t>(entity);
    const uintptr_t bytes = sizeof(*cl_entities) * uintptr_t{MAX_EDICTS};
    if (value < base || value - base >= bytes)
        return WORR_PREDICTION_NO_ENTITY;

    const uintptr_t offset = value - base;
    if (offset % sizeof(*cl_entities))
        return WORR_PREDICTION_NO_ENTITY;
    return static_cast<uint32_t>(offset / sizeof(*cl_entities));
}

static struct edict_s *CG_PredictionEntityPointer(uint32_t entity_id)
{
    if (!cl_entities || entity_id == WORR_PREDICTION_NO_ENTITY ||
        entity_id >= MAX_EDICTS) {
        return nullptr;
    }
    return reinterpret_cast<struct edict_s *>(&cl_entities[entity_id]);
}

static void CG_PredictionTrace(void *, worr_prediction_trace_v1 *result,
                               const float start[3], const float mins[3],
                               const float maxs[3], const float end[3],
                               uint32_t pass_entity_id,
                               uint32_t contents_mask, uint32_t query_flags)
{
    if (!result)
        return;

    *result = {};
    result->struct_size = sizeof(*result);
    result->schema_version = WORR_PREDICTION_ABI_VERSION;
    result->fraction = 1.0f;
    VectorCopy(end, result->end);
    result->surface_id = WORR_PREDICTION_NO_ENTITY;
    result->surface2_id = WORR_PREDICTION_NO_ENTITY;
    result->entity_id = WORR_PREDICTION_NO_ENTITY;

    if (query_flags & ~WORR_PREDICTION_TRACE_WORLD_ONLY) {
        result->schema_version = 0;
        return;
    }

    trace_t trace{};
    if (query_flags & WORR_PREDICTION_TRACE_WORLD_ONLY) {
        CM_BoxTrace(&trace, start, end, mins, maxs, cl.bsp->nodes,
                    static_cast<int>(contents_mask), cl.csr.extended);
    } else {
        CL_Trace(&trace, start, end, mins, maxs,
                 CG_PredictionEntityPointer(pass_entity_id),
                 static_cast<contents_t>(contents_mask));
    }

    result->all_solid = trace.allsolid;
    result->start_solid = trace.startsolid;
    result->fraction = trace.fraction;
    VectorCopy(trace.endpos, result->end);
    CG_PredictionPlaneFromTrace(&result->plane, &trace.plane);
    result->contents = static_cast<uint32_t>(trace.contents);
    result->entity_id = CG_PredictionEntityId(trace.ent);

    if (trace.surface) {
        result->surface_flags = static_cast<uint32_t>(trace.surface->flags);
        result->surface_id = trace.surface->id;
    }
    if (trace.surface2) {
        result->has_second_surface = 1;
        CG_PredictionPlaneFromTrace(&result->plane2, &trace.plane2);
        result->surface2_flags =
            static_cast<uint32_t>(trace.surface2->flags);
        result->surface2_id = trace.surface2->id;
    }
}

static uint32_t CG_PredictionPointContents(void *, const float point[3])
{
    if (!cgei || !CL_PointContents)
        return 0;
    return static_cast<uint32_t>(CL_PointContents(point));
}

static worr_prediction_state_v1 CG_PredictionState(
    const pmove_state_t &state)
{
    worr_prediction_state_v1 out{};
    out.struct_size = sizeof(out);
    out.schema_version = WORR_PREDICTION_ABI_VERSION;
    out.movement_type = state.pm_type;
    VectorCopy(state.origin, out.origin);
    VectorCopy(state.velocity, out.velocity);
    out.movement_flags = state.pm_flags;
    out.movement_time_ms = state.pm_time;
    out.gravity = state.gravity;
    out.view_height = state.viewheight;
    VectorCopy(state.delta_angles, out.delta_angles);
    return out;
}

static bool CG_PredictionStateValid(const worr_prediction_state_v1 &state)
{
    return state.struct_size == sizeof(state) &&
           state.schema_version == WORR_PREDICTION_ABI_VERSION;
}

static bool CG_PredictionStateMatches(const worr_prediction_state_v1 &a,
                                      const worr_prediction_state_v1 &b)
{
    if (!CG_PredictionStateValid(a) || !CG_PredictionStateValid(b) ||
        a.movement_type != b.movement_type ||
        a.movement_flags != b.movement_flags ||
        a.movement_time_ms != b.movement_time_ms ||
        a.gravity != b.gravity || a.view_height != b.view_height) {
        return false;
    }

    for (unsigned i = 0; i < 3; ++i) {
        if (a.origin[i] != b.origin[i] ||
            a.velocity[i] != b.velocity[i] ||
            a.delta_angles[i] != b.delta_angles[i]) {
            return false;
        }
    }
    return true;
}

static bool CG_PredictionPlaneMatches(const cplane_t &a, const cplane_t &b)
{
    return a.normal[0] == b.normal[0] && a.normal[1] == b.normal[1] &&
           a.normal[2] == b.normal[2] && a.dist == b.dist &&
           a.type == b.type && a.signbits == b.signbits;
}

static void CG_RecordPrediction(uint32_t command_sequence,
                                const worr_prediction_step_v1 &step,
                                uint64_t config_hash,
                                uint64_t replay_chain_hash)
{
    const unsigned slot = command_sequence & CMD_MASK;
    cl.predicted_sequences[slot] = command_sequence;
    cl.predicted_states[slot] = step.state;
    cl.predicted_state_hashes[slot] = step.state_hash;
    cl.predicted_collision_hashes[slot] = step.collision_hash;
    cl.predicted_config_hashes[slot] = config_hash;
    cl.predicted_replay_chain_hashes[slot] = replay_chain_hash;
    VectorCopy(step.state.origin, cl.predicted_origins[slot]);
}

static uint32_t CG_ValidatePredictionInputRange(
    worr_cgame_prediction_input_range_v1 &range,
    std::uint32_t result)
{
    if (range.struct_size != sizeof(range) ||
        range.api_version != WORR_CGAME_PREDICTION_INPUT_API_VERSION ||
        range.result != result ||
        range.command_count >= WORR_CGAME_PREDICTION_INPUT_CAPACITY) {
        return WORR_CGAME_PREDICTION_INPUT_INVALID_ARGUMENT;
    }
    if (result != WORR_CGAME_PREDICTION_INPUT_OK)
        return result;
    if (range.command_count !=
        range.current_legacy_sequence -
            range.authoritative_legacy_sequence) {
        return WORR_CGAME_PREDICTION_INPUT_INVALID_ARGUMENT;
    }

    const uint32_t known_flags =
        WORR_CGAME_PREDICTION_INPUT_CANONICAL |
        WORR_CGAME_PREDICTION_INPUT_LEGACY_FALLBACK |
        WORR_CGAME_PREDICTION_INPUT_HAS_PENDING |
        WORR_CGAME_PREDICTION_INPUT_CANONICAL_BOOTSTRAP;
    const uint32_t source_flags =
        range.flags & (WORR_CGAME_PREDICTION_INPUT_CANONICAL |
                       WORR_CGAME_PREDICTION_INPUT_LEGACY_FALLBACK);
    if ((range.flags & ~known_flags) != 0 ||
        (range.source !=
             WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR &&
         range.source !=
             WORR_CGAME_PREDICTION_INPUT_SOURCE_LEGACY_PACKET_ACK &&
         range.source !=
             WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_BOOTSTRAP) ||
        (range.source ==
             WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR &&
         (source_flags != WORR_CGAME_PREDICTION_INPUT_CANONICAL ||
          (range.flags &
           WORR_CGAME_PREDICTION_INPUT_CANONICAL_BOOTSTRAP) != 0)) ||
        (range.source ==
             WORR_CGAME_PREDICTION_INPUT_SOURCE_LEGACY_PACKET_ACK &&
         (source_flags != WORR_CGAME_PREDICTION_INPUT_LEGACY_FALLBACK ||
          (range.flags &
           WORR_CGAME_PREDICTION_INPUT_CANONICAL_BOOTSTRAP) != 0)) ||
        (range.source ==
             WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_BOOTSTRAP &&
         (source_flags != WORR_CGAME_PREDICTION_INPUT_LEGACY_FALLBACK ||
          !(range.flags &
            WORR_CGAME_PREDICTION_INPUT_CANONICAL_BOOTSTRAP))) ||
        (range.flags & WORR_CGAME_PREDICTION_INPUT_HARD_RESYNC_REQUIRED)) {
        return WORR_CGAME_PREDICTION_INPUT_INVALID_ARGUMENT;
    }

    for (uint32_t index = 0; index < range.command_count; ++index) {
        const auto &entry = range.commands[index];
        if (entry.legacy_sequence !=
                range.authoritative_legacy_sequence + index + 1u ||
            entry.reserved0 != 0 ||
            entry.command.struct_size != sizeof(entry.command) ||
            entry.command.schema_version != WORR_PREDICTION_ABI_VERSION ||
            entry.command.reserved0 != 0) {
            return WORR_CGAME_PREDICTION_INPUT_INVALID_ARGUMENT;
        }
    }

    if (range.flags & WORR_CGAME_PREDICTION_INPUT_HAS_PENDING) {
        if (range.pending_command.legacy_sequence !=
                range.current_legacy_sequence + 1u ||
            range.pending_command.command_id.epoch != 0 ||
            range.pending_command.command_id.sequence != 0 ||
            range.pending_command.reserved0 != 0 ||
            range.pending_command.command.struct_size !=
                sizeof(range.pending_command.command) ||
            range.pending_command.command.schema_version !=
                WORR_PREDICTION_ABI_VERSION ||
            range.pending_command.command.reserved0 != 0) {
            return WORR_CGAME_PREDICTION_INPUT_INVALID_ARGUMENT;
        }
    }
    return WORR_CGAME_PREDICTION_INPUT_OK;
}

static uint32_t CG_ResolvePredictionInput(
    worr_cgame_prediction_input_range_v1 &range)
{
    range = {};
    if (!prediction_input_import ||
        prediction_input_import->struct_size !=
            sizeof(*prediction_input_import) ||
        prediction_input_import->api_version !=
            WORR_CGAME_PREDICTION_INPUT_API_VERSION ||
        !prediction_input_import->ResolveInputRange) {
        return WORR_CGAME_PREDICTION_INPUT_INVALID_ARGUMENT;
    }

    const uint32_t result =
        prediction_input_import->ResolveInputRange(&range);
    return CG_ValidatePredictionInputRange(range, result);
}

static uint32_t CG_ResolvePredictionInputForCursor(
    const worr_snapshot_consumed_command_v2 &consumed_command,
    worr_cgame_prediction_input_range_v1 &range)
{
    range = {};
    if (!prediction_input_import_v2 ||
        prediction_input_import_v2->struct_size !=
            sizeof(*prediction_input_import_v2) ||
        prediction_input_import_v2->api_version !=
            WORR_CGAME_PREDICTION_INPUT_API_VERSION_V2 ||
        !prediction_input_import_v2->ResolveInputRangeForCursor) {
        return WORR_CGAME_PREDICTION_INPUT_INVALID_ARGUMENT;
    }

    worr_cgame_prediction_input_request_v2 request{};
    request.struct_size = sizeof(request);
    request.api_version = WORR_CGAME_PREDICTION_INPUT_API_VERSION_V2;
    request.flags =
        WORR_CGAME_PREDICTION_INPUT_REQUEST_CANONICAL_REQUIRED;
    request.consumed_command = consumed_command;
    const uint32_t result =
        prediction_input_import_v2->ResolveInputRangeForCursor(
            &request, &range);
    return CG_ValidatePredictionInputRange(range, result);
}

static void CG_IncrementSaturated(std::uint64_t &value)
{
    if (value != UINT64_MAX)
        ++value;
}

static std::uint32_t CG_PredictionAuthorityMode()
{
    const int configured = cg_prediction_snapshot_authority
        ? Q_clip(cg_prediction_snapshot_authority->integer,
                 static_cast<int>(
                     CG_PREDICTION_SNAPSHOT_AUTHORITY_LEGACY),
                 static_cast<int>(
                     CG_PREDICTION_SNAPSHOT_AUTHORITY_PROMOTE))
        : CG_PREDICTION_SNAPSHOT_AUTHORITY_LEGACY;
    return static_cast<std::uint32_t>(configured);
}

static bool CG_SnapshotIdEqual(worr_snapshot_id_v2 left,
                               worr_snapshot_id_v2 right)
{
    return left.epoch == right.epoch &&
           left.sequence == right.sequence;
}

static void CG_FillLegacyPredictionAuthority(
    cg_prediction_frame_authority_t &authority)
{
    authority.movement = CG_PredictionState(cl.frame.ps.pmove);
    VectorCopy(cl.frame.ps.viewangles, authority.view_angles);
    VectorCopy(cl.frame.ps.viewoffset, authority.view_offset);
    Vector4Copy(cl.frame.ps.screen_blend, authority.screen_blend);
    authority.rdflags = cl.frame.ps.rdflags;
    authority.controlled_entity_index =
        cl.frame.clientNum >= 0 && cl.frame.clientNum + 1 < MAX_EDICTS
            ? static_cast<std::uint32_t>(cl.frame.clientNum + 1)
            : WORR_PREDICTION_NO_ENTITY;
}

static bool CG_PredictionAuthorityAuditMatchesLegacy(
    const cg_prediction_authority_v1 &canonical,
    const worr_cgame_prediction_input_range_v1 &legacy)
{
    const auto &snapshot = canonical.timeline.snapshot;
    const auto &player = canonical.timeline.player;
    const std::uint32_t comparable_flags =
        WORR_CGAME_PREDICTION_INPUT_CANONICAL |
        WORR_CGAME_PREDICTION_INPUT_HAS_PENDING;
    if (!CG_PredictionStateMatches(
            player.movement, CG_PredictionState(cl.frame.ps.pmove)) ||
        snapshot.consumed_command.cursor.epoch !=
            cl.frame.consumed_command.cursor.epoch ||
        snapshot.consumed_command.cursor.contiguous_sequence !=
            cl.frame.consumed_command.cursor.contiguous_sequence ||
        snapshot.consumed_command.provenance !=
            cl.frame.consumed_command.provenance ||
        canonical.input.authoritative_legacy_sequence !=
            legacy.authoritative_legacy_sequence ||
        canonical.input.current_legacy_sequence !=
            legacy.current_legacy_sequence ||
        canonical.input.command_count != legacy.command_count ||
        (canonical.input.flags & comparable_flags) !=
            (legacy.flags & comparable_flags) ||
        canonical.input.source != legacy.source ||
        canonical.input.consumed_command.cursor.epoch !=
            legacy.consumed_command.cursor.epoch ||
        canonical.input.consumed_command.cursor.contiguous_sequence !=
            legacy.consumed_command.cursor.contiguous_sequence ||
        canonical.input.consumed_command.provenance !=
            legacy.consumed_command.provenance) {
        return false;
    }
    if (!CG_PredictionStateValid(player.movement))
        return false;
    for (unsigned index = 0; index < 3; ++index) {
        if (player.view_angles[index] != cl.frame.ps.viewangles[index] ||
            player.view_offset[index] != cl.frame.ps.viewoffset[index])
            return false;
    }
    for (unsigned index = 0; index < 4; ++index) {
        if (player.screen_blend[index] !=
            cl.frame.ps.screen_blend[index]) {
            return false;
        }
    }
    if (player.rdflags != cl.frame.ps.rdflags)
        return false;
    for (std::uint32_t index = 0;
         index < canonical.input.command_count; ++index) {
        const auto &left = canonical.input.commands[index];
        const auto &right = legacy.commands[index];
        if (left.legacy_sequence != right.legacy_sequence ||
            left.command_id.epoch != right.command_id.epoch ||
            left.command_id.sequence != right.command_id.sequence ||
            Worr_PredictionHashCommandV1(&left.command) !=
                Worr_PredictionHashCommandV1(&right.command)) {
            return false;
        }
    }
    if ((canonical.input.flags &
         WORR_CGAME_PREDICTION_INPUT_HAS_PENDING) != 0) {
        const auto &left = canonical.input.pending_command;
        const auto &right = legacy.pending_command;
        if (left.legacy_sequence != right.legacy_sequence ||
            Worr_PredictionHashCommandV1(&left.command) !=
                Worr_PredictionHashCommandV1(&right.command)) {
            return false;
        }
    }
    return true;
}

static cg_prediction_authority_result_v1
CG_ResolveCanonicalPredictionAuthority(
    cg_prediction_authority_v1 &authority)
{
    authority = {};
    if (cl.frame.number < 0 ||
        static_cast<std::uint32_t>(cl.frame.number) == UINT32_MAX ||
        !cl.frame.canonical_server_time_valid ||
        cl.frame.clientNum < 0 ||
        cl.frame.clientNum + 1 >= MAX_EDICTS) {
        return cg_prediction_authority_result_v1::invalid_argument;
    }

    const std::uint32_t snapshot_sequence =
        static_cast<std::uint32_t>(cl.frame.number) + 1u;
    cg_prediction_authority_candidate_v1 candidate{};
    const auto timeline_result =
        CG_CanonicalSnapshotTimelineCopyPredictionSnapshot(
            snapshot_sequence, &candidate.timeline);
    if (timeline_result != WORR_SNAPSHOT_TIMELINE_OK)
        return cg_prediction_authority_result_v1::timeline_unavailable;

    const std::uint32_t input_result =
        CG_ResolvePredictionInputForCursor(
            candidate.timeline.snapshot.consumed_command,
            candidate.input);
    if (input_result != WORR_CGAME_PREDICTION_INPUT_OK)
        return cg_prediction_authority_result_v1::input_range_invalid;

    cg_prediction_authority_expectation_v1 expectation{};
    expectation.snapshot_sequence = snapshot_sequence;
    expectation.server_tick =
        static_cast<std::uint32_t>(cl.frame.number);
    expectation.server_time_us =
        cl.frame.canonical_server_time_us;
    expectation.controlled_entity_index =
        static_cast<std::uint32_t>(cl.frame.clientNum + 1);
    return CG_PredictionAuthoritySelectV1(
        &expectation, &candidate, &authority);
}

static void CG_NotePredictionAuthorityResult(
    cg_prediction_authority_result_v1 result, bool audit_match,
    bool audit_sample, bool blocked)
{
    CG_IncrementSaturated(prediction_authority_stats.attempts);
    prediction_authority_stats.last_result =
        static_cast<std::uint32_t>(result);
    if (result == cg_prediction_authority_result_v1::canonical)
        CG_IncrementSaturated(prediction_authority_stats.canonical);
    else
        CG_IncrementSaturated(prediction_authority_stats.unavailable);
    if (audit_sample) {
        if (audit_match)
            CG_IncrementSaturated(prediction_authority_stats.audit_matches);
        else
            CG_IncrementSaturated(
                prediction_authority_stats.audit_mismatches);
    }
    if (blocked)
        CG_IncrementSaturated(prediction_authority_stats.blocked);

    if (cls.realtime - prediction_authority_stats.last_debug_time_ms <
        1000) {
        return;
    }
    prediction_authority_stats.last_debug_time_ms = cls.realtime;
    Com_LPrintf(
        PRINT_DEVELOPER,
        "cg_prediction_snapshot_authority: mode=%u result=%s "
        "attempts=%llu canonical=%llu unavailable=%llu "
        "audit=%llu/%llu blocked=%llu\n",
        CG_PredictionAuthorityMode(),
        CG_PredictionAuthorityResultName(result),
        static_cast<unsigned long long>(
            prediction_authority_stats.attempts),
        static_cast<unsigned long long>(
            prediction_authority_stats.canonical),
        static_cast<unsigned long long>(
            prediction_authority_stats.unavailable),
        static_cast<unsigned long long>(
            prediction_authority_stats.audit_matches),
        static_cast<unsigned long long>(
            prediction_authority_stats.audit_mismatches),
        static_cast<unsigned long long>(
            prediction_authority_stats.blocked));
}

static bool CG_ResolvePredictionFrameAuthority(
    cg_prediction_frame_authority_t &frame)
{
    frame = {};
    CG_FillLegacyPredictionAuthority(frame);

    const std::uint32_t mode = CG_PredictionAuthorityMode();
    if (mode == CG_PREDICTION_SNAPSHOT_AUTHORITY_LEGACY) {
        frame.input_result = CG_ResolvePredictionInput(frame.input);
        return frame.input_result == WORR_CGAME_PREDICTION_INPUT_OK;
    }

    cg_prediction_authority_v1 canonical{};
    frame.canonical_result =
        CG_ResolveCanonicalPredictionAuthority(canonical);
    const bool canonical_ok =
        frame.canonical_result ==
        cg_prediction_authority_result_v1::canonical;

    if (mode == CG_PREDICTION_SNAPSHOT_AUTHORITY_AUDIT) {
        frame.input_result = CG_ResolvePredictionInput(frame.input);
        const bool legacy_ok =
            frame.input_result == WORR_CGAME_PREDICTION_INPUT_OK;
        const bool audit_match =
            canonical_ok && legacy_ok &&
            CG_PredictionAuthorityAuditMatchesLegacy(
                canonical, frame.input);
        CG_NotePredictionAuthorityResult(
            frame.canonical_result, audit_match, canonical_ok, false);
        return legacy_ok;
    }

    CG_NotePredictionAuthorityResult(
        frame.canonical_result, false, false, !canonical_ok);
    if (!canonical_ok) {
        frame.blocked = true;
        frame.input_result =
            WORR_CGAME_PREDICTION_INPUT_CANONICAL_METADATA_REQUIRED;
        return false;
    }

    frame.input = canonical.input;
    frame.input_result = WORR_CGAME_PREDICTION_INPUT_OK;
    frame.movement = canonical.timeline.player.movement;
    VectorCopy(canonical.timeline.player.view_angles,
               frame.view_angles);
    VectorCopy(canonical.timeline.player.view_offset,
               frame.view_offset);
    Vector4Copy(canonical.timeline.player.screen_blend,
                frame.screen_blend);
    frame.rdflags = canonical.timeline.player.rdflags;
    frame.controlled_entity_index =
        canonical.timeline.snapshot.controlled_entity.identity.index;
    frame.discontinuity_flags =
        canonical.timeline.snapshot.discontinuity.flags;
    frame.snapshot_id = canonical.timeline.snapshot.snapshot_id;
    frame.history_reset_required =
        canonical.history_reset_required != 0;
    frame.canonical = true;
    return true;
}

static void CG_PredictionAnglesFromAuthority(
    const cg_prediction_frame_authority_t &authority)
{
    if (authority.movement.movement_type < PM_DEAD &&
        cls.serverProtocol > PROTOCOL_VERSION_DEFAULT) {
        for (unsigned index = 0; index < 3; ++index) {
            cl.predicted_angles[index] =
                cl.viewangles[index] +
                authority.movement.delta_angles[index];
        }
    } else {
        VectorCopy(authority.view_angles, cl.predicted_angles);
    }
}

static void CG_ApplyPredictionAuthority(
    const cg_prediction_frame_authority_t &authority)
{
    VectorCopy(authority.movement.origin, cl.predicted_origin);
    VectorCopy(authority.movement.velocity, cl.predicted_velocity);
    CG_PredictionAnglesFromAuthority(authority);
    Vector4Copy(authority.screen_blend, cl.predicted_screen_blend);
    cl.predicted_rdflags = authority.rdflags;
}

static void CG_ClearPredictionHistory()
{
    VectorClear(cl.prediction_error);
    memset(cl.predicted_sequences, 0, sizeof(cl.predicted_sequences));
    memset(cl.predicted_states, 0, sizeof(cl.predicted_states));
    memset(cl.predicted_origins, 0, sizeof(cl.predicted_origins));
    memset(cl.predicted_state_hashes, 0,
           sizeof(cl.predicted_state_hashes));
    memset(cl.predicted_collision_hashes, 0,
           sizeof(cl.predicted_collision_hashes));
    memset(cl.predicted_config_hashes, 0,
           sizeof(cl.predicted_config_hashes));
    memset(cl.predicted_replay_chain_hashes, 0,
           sizeof(cl.predicted_replay_chain_hashes));
    cl.predicted_step = 0;
    cl.last_groundentity = nullptr;
    memset(&cl.last_groundplane, 0, sizeof(cl.last_groundplane));
}

static bool CG_ConsumePredictionDiscontinuity(
    const cg_prediction_frame_authority_t &authority)
{
    if (!authority.canonical || !authority.history_reset_required ||
        CG_SnapshotIdEqual(
            prediction_discontinuity_reset_snapshot,
            authority.snapshot_id)) {
        return false;
    }
    prediction_discontinuity_reset_snapshot = authority.snapshot_id;
    return true;
}

static void CG_PredictionHardResync(
    uint32_t result,
    const worr_cgame_prediction_input_range_v1 &range,
    const cg_prediction_frame_authority_t &authority,
    cg_prediction_correction_reason_t reason)
{
    static int last_noted_frame = INT_MIN;
    static uint32_t last_noted_result = UINT32_MAX;

    CG_ClearPredictionHistory();
    CG_ApplyPredictionAuthority(authority);

    CG_SnapshotTimeline_NotePredictionReplay(
        range.authoritative_legacy_sequence,
        range.current_legacy_sequence, 0);
    if (last_noted_frame != cl.frame.number ||
        last_noted_result != result) {
        CG_SnapshotTimeline_NotePredictionCorrection(
            range.authoritative_legacy_sequence,
            range.current_legacy_sequence, 0, true,
            reason);
        CG_SHOWMISS("%i: prediction input hard resync (result %u)\n",
                    cl.frame.number, result);
        last_noted_frame = cl.frame.number;
        last_noted_result = result;
    }
}

static cg_prediction_correction_reason_t
CG_PredictionAuthorityFailureReason(
    cg_prediction_authority_result_v1 result)
{
    return result ==
               cg_prediction_authority_result_v1::timeline_unavailable
        ? cg_prediction_correction_reason_t::
              canonical_authority_unavailable
        : cg_prediction_correction_reason_t::
              canonical_authority_mismatch;
}

void CL_PredictAngles(void)
{
    cg_prediction_frame_authority_t authority{};
    (void)CG_ResolvePredictionFrameAuthority(authority);
    CG_PredictionAnglesFromAuthority(authority);
}

void CL_CheckPredictionError(void)
{
    if (cls.demo.playback)
        return;

    if (sv_paused && sv_paused->integer) {
        VectorClear(cl.prediction_error);
        return;
    }

    if (!cl_predict->integer) {
        return;
    }

    cg_prediction_frame_authority_t authority{};
    if (!CG_ResolvePredictionFrameAuthority(authority)) {
        const auto reason = authority.blocked
            ? CG_PredictionAuthorityFailureReason(
                  authority.canonical_result)
            : cg_prediction_correction_reason_t::
                  input_range_invalid;
        const std::uint32_t result = authority.blocked
            ? UINT32_C(0x100) +
                  static_cast<std::uint32_t>(
                      authority.canonical_result)
            : authority.input_result;
        CG_PredictionHardResync(
            result, authority.input, authority, reason);
        return;
    }

    auto &input_range = authority.input;
    if (CG_ConsumePredictionDiscontinuity(authority)) {
        CG_PredictionHardResync(
            UINT32_C(0x200), input_range, authority,
            cg_prediction_correction_reason_t::
                snapshot_discontinuity);
        return;
    }

    if (authority.movement.movement_flags & PMF_NO_PREDICTION) {
        VectorClear(cl.prediction_error);
        return;
    }

    /* Sequence zero names the authoritative pre-command state.  There is no
     * predicted command result to compare against in that state. */
    if (input_range.source ==
            WORR_CGAME_PREDICTION_INPUT_SOURCE_CANONICAL_CURSOR &&
        input_range.consumed_command.cursor.contiguous_sequence == 0) {
        VectorClear(cl.prediction_error);
        return;
    }

    const uint32_t command = input_range.authoritative_legacy_sequence;
    const unsigned slot = command & CMD_MASK;
    const worr_prediction_state_v1 authoritative =
        authority.movement;
    const uint64_t authoritative_hash =
        Worr_PredictionHashStateV1(&authoritative);

    if (cl.predicted_sequences[slot] != command ||
        !CG_PredictionStateValid(cl.predicted_states[slot])) {
        CG_PredictionHardResync(
            WORR_CGAME_PREDICTION_INPUT_RETAINED_STATE_MISSING,
            input_range, authority,
            cg_prediction_correction_reason_t::
                retained_state_missing);
        return;
    }

    worr_prediction_config_v1 config{};
    CG_GetPredictionConfigV1(&config);
    const uint64_t config_hash = Worr_PredictionHashConfigV1(&config);
    if (cl.predicted_config_hashes[slot] &&
        cl.predicted_config_hashes[slot] != config_hash) {
        CG_PredictionHardResync(
            WORR_CGAME_PREDICTION_INPUT_CONFIG_DISCONTINUITY,
            input_range, authority,
            cg_prediction_correction_reason_t::
                config_discontinuity);
        return;
    }

    float delta[3];
    VectorSubtract(authoritative.origin, cl.predicted_origins[slot], delta);
    const float len = fabsf(delta[0]) + fabsf(delta[1]) + fabsf(delta[2]);
    const bool state_matches =
        CG_PredictionStateMatches(authoritative, cl.predicted_states[slot]);
    const bool hash_matches =
        authoritative_hash == cl.predicted_state_hashes[slot];

    if (!state_matches || !hash_matches) {
        if (len > 80) {
            CG_SnapshotTimeline_NotePredictionCorrection(
                command, input_range.current_legacy_sequence, len, true,
                cg_prediction_correction_reason_t::
                    correction_threshold_exceeded);
            VectorClear(cl.prediction_error);
        } else {
            CG_SnapshotTimeline_NotePredictionCorrection(
                command, input_range.current_legacy_sequence, len, false,
                cg_prediction_correction_reason_t::state_divergence);
            VectorCopy(delta, cl.prediction_error);
        }
        CG_SHOWMISS(
            "prediction miss on %i: %f (%f %f %f), state %016llx != %016llx\n",
            cl.frame.number, len, delta[0], delta[1], delta[2],
            static_cast<unsigned long long>(cl.predicted_state_hashes[slot]),
            static_cast<unsigned long long>(authoritative_hash));
    } else {
        VectorClear(cl.prediction_error);
    }

    cl.predicted_states[slot] = authoritative;
    cl.predicted_state_hashes[slot] = authoritative_hash;
    cl.predicted_config_hashes[slot] = config_hash;
    VectorCopy(authoritative.origin, cl.predicted_origins[slot]);
}

#define MAX_STEP_CHANGE 32

void CL_PredictMovement(void)
{
    if (!cgei || cls.state != ca_active || cls.demo.playback)
        return;
    if (sv_paused && sv_paused->integer)
        return;

    if (!cl_predict->integer) {
        CL_PredictAngles();
        return;
    }

    cg_prediction_frame_authority_t authority{};
    if (!CG_ResolvePredictionFrameAuthority(authority)) {
        const auto reason = authority.blocked
            ? CG_PredictionAuthorityFailureReason(
                  authority.canonical_result)
            : cg_prediction_correction_reason_t::
                  input_range_invalid;
        const std::uint32_t result = authority.blocked
            ? UINT32_C(0x100) +
                  static_cast<std::uint32_t>(
                      authority.canonical_result)
            : authority.input_result;
        CG_PredictionHardResync(
            result, authority.input, authority, reason);
        CG_SnapshotTimeline_DebugTick(cls.realtime);
        return;
    }

    auto &input_range = authority.input;
    if (CG_ConsumePredictionDiscontinuity(authority)) {
        CG_PredictionHardResync(
            UINT32_C(0x200), input_range, authority,
            cg_prediction_correction_reason_t::
                snapshot_discontinuity);
    }

    if (authority.movement.movement_flags & PMF_NO_PREDICTION) {
        CG_ApplyPredictionAuthority(authority);
        return;
    }

    const uint32_t acknowledged =
        input_range.authoritative_legacy_sequence;
    const uint32_t current = input_range.current_legacy_sequence;
    const uint32_t replay_count = input_range.command_count;
    const bool has_pending =
        (input_range.flags & WORR_CGAME_PREDICTION_INPUT_HAS_PENDING) != 0;

    /*
     * Independent off-hand interactions consume the same immutable canonical
     * records as movement but remain shadow-only until an authoritative
     * lifecycle transaction can be delivered and reconciled.
    */
    CG_LocalInteractionPredict(input_range);
    CG_EventRuntimeSynchronizeLocalInteractionHealth();

    if (!has_pending && replay_count == 0) {
        CG_ApplyPredictionAuthority(authority);
        CG_SnapshotTimeline_NotePredictionReplay(acknowledged, current, 0);
        CG_SnapshotTimeline_DebugTick(cls.realtime);
        CG_SHOWMISS("%i: not moved\n", cl.frame.number);
        return;
    }

    CG_SnapshotTimeline_NotePredictionReplay(acknowledged, current,
                                             replay_count);

    worr_prediction_step_v1 step{};
    step.struct_size = sizeof(step);
    step.schema_version = WORR_PREDICTION_ABI_VERSION;
    step.state = authority.movement;
    CG_GetPredictionConfigV1(&step.config);
    step.snap_initial = 1;
    step.player_entity_id = authority.controlled_entity_index;
    VectorCopy(authority.view_offset, step.view_offset);
    step.trace = CG_PredictionTrace;
    step.point_contents = CG_PredictionPointContents;
    const uint64_t config_hash = Worr_PredictionHashConfigV1(&step.config);
    uint64_t replay_chain_hash = config_hash;

    auto run_command = [&](uint32_t sequence,
                           const worr_prediction_command_v1 &command) {
        step.command = command;
        if (!Worr_PredictionStepV1(&step)) {
            CG_SHOWMISS("%i: prediction ABI rejected command %u\n",
                        cl.frame.number, sequence);
            return false;
        }
        step.snap_initial = 0;
        replay_chain_hash = Worr_PredictionReplayChainHashV1(
            replay_chain_hash, sequence,
            Worr_PredictionHashCommandV1(&step.command),
            step.collision_hash, step.state_hash);
        CG_RecordPrediction(sequence, step, config_hash, replay_chain_hash);
        return true;
    };

    for (uint32_t replayed = 0; replayed < replay_count; ++replayed) {
        const auto &input = input_range.commands[replayed];
        if (!run_command(input.legacy_sequence, input.command)) {
            CG_PredictionHardResync(
                WORR_CGAME_PREDICTION_INPUT_REPLAY_REJECTED,
                input_range, authority,
                cg_prediction_correction_reason_t::
                    replay_rejected);
            CG_SnapshotTimeline_DebugTick(cls.realtime);
            return;
        }
    }

    float step_reference_z = step.state.origin[2];
    bool step_reference_valid = false;
    if (has_pending) {
        step_reference_z = step.state.origin[2];
        step_reference_valid = true;
        if (!run_command(input_range.pending_command.legacy_sequence,
                         input_range.pending_command.command)) {
            CG_PredictionHardResync(
                WORR_CGAME_PREDICTION_INPUT_REPLAY_REJECTED,
                input_range, authority,
                cg_prediction_correction_reason_t::
                    replay_rejected);
            CG_SnapshotTimeline_DebugTick(cls.realtime);
            return;
        }
    } else {
        const uint32_t previous = current - 1u;
        const unsigned previous_slot = previous & CMD_MASK;
        if (cl.predicted_sequences[previous_slot] == previous &&
            CG_PredictionStateValid(cl.predicted_states[previous_slot])) {
            step_reference_z = cl.predicted_origins[previous_slot][2];
            step_reference_valid = true;
        }
    }

    if (step_reference_valid &&
        step.state.movement_type != PM_SPECTATOR) {
        const float step_delta = step.state.origin[2] - step_reference_z;
        const float abs_step = fabsf(step_delta);
        cplane_t ground_plane{};
        CG_TracePlaneFromPrediction(&ground_plane, &step.ground_plane);
        struct edict_s *ground_entity =
            CG_PredictionEntityPointer(step.ground_entity_id);
        const bool step_detected =
            abs_step > 1 && abs_step < 20 &&
            ((authority.movement.movement_flags & PMF_ON_GROUND) ||
             step.step_clip) &&
            ((step.state.movement_flags & PMF_ON_GROUND) &&
             step.state.movement_type <= PM_GRAPPLE) &&
            (!CG_PredictionPlaneMatches(cl.last_groundplane, ground_plane) ||
             cl.last_groundentity != ground_entity);
        if (step_detected) {
            const float elapsed = cls.realtime - cl.predicted_step_time;
            const float old_step =
                elapsed < STEP_TIME
                    ? cl.predicted_step * (STEP_TIME - elapsed) / STEP_TIME
                    : 0.0f;
            cl.predicted_step = Q_clip(old_step + step_delta,
                                       -MAX_STEP_CHANGE, MAX_STEP_CHANGE);
            cl.predicted_step_time = cls.realtime;
        }
    }

    VectorCopy(step.state.origin, cl.predicted_origin);
    VectorCopy(step.state.velocity, cl.predicted_velocity);
    VectorCopy(step.view_angles, cl.predicted_angles);
    Vector4Copy(step.screen_blend, cl.predicted_screen_blend);
    cl.predicted_rdflags = step.rd_flags;
    CG_TracePlaneFromPrediction(&cl.last_groundplane, &step.ground_plane);
    cl.last_groundentity =
        CG_PredictionEntityPointer(step.ground_entity_id);

    CG_SnapshotTimeline_DebugTick(cls.realtime);
}
