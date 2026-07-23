/* Focused FR-10-T07 native value-presenter dispatch coverage. */

#include "cg_native_event_presenter.hpp"

#include "cg_canonical_snapshot_timeline.hpp"
#include "cg_entity_local.h"
#include "cg_event_runtime.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#undef Com_DPrintf

#undef cl
#undef Cvar_Get
#undef S_GetPrecachedSound
#undef S_StartSound

const cgame_entity_import_t *cgei = nullptr;

extern "C" vec_t VectorNormalize(vec3_t value)
{
    const vec_t length = std::sqrt(DotProduct(value, value));
    if (length != 0.0f) {
        const vec_t inverse = 1.0f / length;
        VectorScale(value, inverse, value);
    }
    return length;
}

namespace {

constexpr std::uint32_t kMaxEntities = 64u;
constexpr std::uint32_t kMaxModels = 128u;
constexpr std::uint32_t kMaxSounds = 64u;
constexpr worr_snapshot_id_v2 kFenceId{91u, 7u};
constexpr worr_snapshot_timeline_ref_v1 kFenceRef{3u, 11u};
constexpr std::uint32_t kEntityIndex = 17u;
constexpr std::uint32_t kEntityGeneration = 29u;

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(stderr, "cgame_native_event_presenter_test:%d: %s\n",
                 line, expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            fail(#expression, __LINE__);                                      \
    } while (0)

cg_event_runtime_can_present_callback_v1 captured_can_present{};
cg_event_runtime_present_callback_v1 captured_present{};
client_state_t client_state{};
cgame_entity_import_t imports{};

void fake_debug_print(const char *, ...)
{
}
worr_snapshot_entity_v2 fence_entity{};
worr_snapshot_player_v2 fence_player{};
worr_snapshot_v2 fence_snapshot{};
bool fence_available{true};
bool fence_entity_present{true};
bool fence_player_available{true};
char probe_cvar_name[] = "cg_native_event_preflight_probe";
char probe_cvar_value[] = "0";
char probe_cvar_default[] = "0";
cvar_t probe_cvar{};
worr_cgame_native_event_probe_status_v1 raw_probe_status{};
cg_event_runtime_status_v1 runtime_status{};
std::uint32_t raw_begin_calls{};
std::uint32_t raw_end_calls{};
std::uint32_t raw_uninstall_calls{};
std::uint32_t raw_complete_calls{};
std::uint32_t raw_checkpoint_apply_calls{};
bool raw_checkpoint_ready{true};
bool runtime_checkpoint_ready{true};

struct keyed_poi_test_slot_t {
    int id{};
    std::uint64_t time{};
    bool infinite{};
    int color_index{};
    int flags{};
    int image_index{};
    char image_name[CG_KEYED_POI_IMAGE_NAME_CAPACITY]{};
    int width{};
    int height{};
    std::array<float, 3> position{};
};

struct effects_probe_t {
    std::uint32_t legacy_entity_calls{};
    int legacy_entity_number{};
    int legacy_entity_event{};
    entity_state_t legacy_entity_state{};

    std::uint32_t temp_calls{};
    tent_params_t temp{};
    bool temp_has_fixed_origin{};
    vec3_t temp_fixed_origin{};
    int temp_fixed_entity{};

    std::uint32_t muzzle_calls{};
    mz_params_t muzzle{};
    entity_state_t muzzle_state{};
    vec3_t muzzle_sound_origin{};
    std::uint32_t muzzle_generation{};
    bool muzzle_monster{};

    std::uint32_t sound_calls{};
    vec3_t sound_origin{};
    int sound_entity{};
    int sound_channel{};
    qhandle_t sound_handle{};
    float sound_volume{};
    float sound_attenuation{};
    float sound_time_offset{};

    std::uint32_t damage_calls{};
    int damage{};
    vec3_t damage_color{};
    vec3_t damage_direction{};

    std::uint32_t help_calls{};
    vec3_t help_origin{};
    vec3_t help_direction{};
    bool help_first{};

    std::uint32_t poi_preflight_calls{};
    std::uint32_t poi_present_calls{};
    cg_prepared_keyed_poi_v1 poi{};
    std::array<cg_prepared_keyed_poi_v1, 4> poi_history{};
};

effects_probe_t effects;
bool legacy_value_ready{true};
bool temp_value_ready{true};
bool muzzle_value_ready{true};
bool help_value_ready{true};
bool poi_state_ready{true};
std::uint32_t muzzle_readiness_calls{};

void reset_effects()
{
    effects = {};
}

void reset_value_readiness()
{
    legacy_value_ready = true;
    temp_value_ready = true;
    muzzle_value_ready = true;
    help_value_ready = true;
    poi_state_ready = true;
    muzzle_readiness_calls = 0;
}

cvar_t *fake_cvar_get(const char *name, const char *value, int flags)
{
    CHECK(name != nullptr);
    CHECK(value != nullptr);
    CHECK(std::strcmp(name, probe_cvar_name) == 0);
    CHECK(std::strcmp(value, probe_cvar_default) == 0);
    CHECK((flags & CVAR_NOARCHIVE) != 0);
    return &probe_cvar;
}

bool vec_equal(const vec3_t left, const vec3_t right)
{
    return std::memcmp(left, right, sizeof(vec3_t)) == 0;
}

worr_event_entity_ref_v1 absent_ref()
{
    return {WORR_EVENT_NO_ENTITY, 0u};
}

worr_event_record_v1 base_record(std::uint16_t payload_kind,
                                 std::uint16_t payload_size)
{
    worr_event_record_v1 record{};
    record.struct_size = sizeof(record);
    record.schema_version = WORR_EVENT_ABI_VERSION;
    record.model_revision = WORR_EVENT_MODEL_REVISION;
    record.flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                   WORR_EVENT_FLAG_PRESENT_ONCE;
    record.source_tick = 700u;
    record.source_time_us = UINT64_C(7000000);
    record.source_entity = {0u, 1u};
    record.subject_entity = absent_ref();
    switch (payload_kind) {
    case WORR_EVENT_PAYLOAD_DAMAGE:
        record.event_type = WORR_EVENT_TYPE_DAMAGE;
        break;
    case WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1:
    case WORR_EVENT_PAYLOAD_AUDIO:
        record.event_type = WORR_EVENT_TYPE_AUDIO_CUE;
        break;
    case WORR_EVENT_PAYLOAD_MUZZLE_V1:
        record.event_type = WORR_EVENT_TYPE_WEAPON_FIRE;
        break;
    case WORR_EVENT_PAYLOAD_EFFECT:
    case WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1:
    case WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1:
        record.event_type = WORR_EVENT_TYPE_VISUAL_EFFECT;
        break;
    case WORR_EVENT_PAYLOAD_ENTITY_REF:
        record.event_type = WORR_EVENT_TYPE_STATE_CHANGE;
        break;
    case WORR_EVENT_PAYLOAD_VEC3:
        record.event_type = WORR_EVENT_TYPE_MOVEMENT_CUE;
        break;
    default:
        record.event_type = WORR_EVENT_TYPE_GAMEPLAY_CUE;
        break;
    }
    record.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    record.prediction_class =
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    record.expiry_tick = record.source_tick + 1u;
    record.payload_kind = payload_kind;
    record.payload_size = payload_size;
    return record;
}

template <typename Payload>
worr_event_record_v1 record_with(std::uint16_t payload_kind,
                                 const Payload &payload)
{
    auto record = base_record(payload_kind, sizeof(payload));
    std::memcpy(record.payload, &payload, sizeof(payload));
    return record;
}

worr_event_record_v1 keyed_poi_record(
    const worr_event_payload_keyed_poi_v1 &payload)
{
    auto record = record_with(WORR_EVENT_PAYLOAD_KEYED_POI_V1, payload);
    record.event_type = WORR_EVENT_TYPE_STATE_CHANGE;
    record.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;
    record.expiry_tick = 0;
    record.subject_entity = {kEntityIndex, kEntityGeneration};
    return record;
}

cg_event_runtime_presentation_context_v1 authority_context()
{
    cg_event_runtime_presentation_context_v1 context{};
    context.struct_size = sizeof(context);
    context.schema_version = CG_EVENT_RUNTIME_PRESENTER_VERSION;
    context.provenance = CG_EVENT_RUNTIME_PRESENTATION_AUTHORITY;
    context.fence_snapshot_id = kFenceId;
    context.fence_tick = 700u;
    context.fence_time_us = UINT64_C(7000000);
    return context;
}

worr_snapshot_entity_v2 make_entity()
{
    worr_snapshot_entity_v2 entity{};
    entity.struct_size = sizeof(entity);
    entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    entity.generation.identity = {kEntityIndex, kEntityGeneration};
    entity.generation.provenance_flags =
        WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    entity.component_mask = WORR_SNAPSHOT_ENTITY_COMPONENTS_V2;
    entity.origin[0] = 101.25f;
    entity.origin[1] = -202.5f;
    entity.origin[2] = 303.75f;
    entity.angles[0] = 10.0f;
    entity.angles[1] = 20.0f;
    entity.angles[2] = 30.0f;
    VectorCopy(entity.origin, entity.old_origin);
    entity.model_index[0] = 1u;
    entity.frame = 2u;
    entity.sound = 3u;
    entity.skin = 4u;
    entity.solid = 5u;
    entity.effects = 6u;
    entity.renderfx = 7u;
    entity.alpha = 1.0f;
    entity.scale = 1.0f;
    entity.loop_volume = 1.0f;
    entity.loop_attenuation = 1.0f;
    entity.owner.index = 1u;
    entity.owner.generation = 1u;
    return entity;
}

worr_snapshot_player_v2 make_player()
{
    worr_snapshot_player_v2 player{};
    player.struct_size = sizeof(player);
    player.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    player.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    player.controlled_entity.identity = {
        kEntityIndex, kEntityGeneration};
    player.controlled_entity.provenance_flags =
        WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
    player.component_mask = WORR_SNAPSHOT_PLAYER_COMPONENTS_V2;
    player.movement.struct_size = sizeof(player.movement);
    player.movement.schema_version = WORR_PREDICTION_ABI_VERSION;
    player.movement.origin[0] = 401.25f;
    player.movement.origin[1] = -502.5f;
    player.movement.origin[2] = 603.75f;
    player.movement.gravity = 800;
    player.movement.view_height = 22;
    player.view_angles[PITCH] = 270.0f;
    player.view_angles[YAW] = 45.0f;
    player.view_angles[ROLL] = 17.0f;
    player.fov = 90.0f;
    return player;
}

qhandle_t fake_get_precached_sound(unsigned index)
{
    return index == 5u ? 505 : 0;
}

void fake_start_sound(const vec3_t origin, int entnum, int entchannel,
                      qhandle_t sound, float volume, float attenuation,
                      float time_offset)
{
    ++effects.sound_calls;
    CHECK(origin != nullptr);
    VectorCopy(origin, effects.sound_origin);
    effects.sound_entity = entnum;
    effects.sound_channel = entchannel;
    effects.sound_handle = sound;
    effects.sound_volume = volume;
    effects.sound_attenuation = attenuation;
    effects.sound_time_offset = time_offset;
}

std::uint32_t total_effect_calls()
{
    return effects.legacy_entity_calls + effects.temp_calls +
           effects.muzzle_calls + effects.sound_calls +
           effects.damage_calls + effects.help_calls +
           effects.poi_present_calls;
}

void present_once(const worr_event_record_v1 &record,
                  const cg_event_runtime_presentation_context_v1 &context)
{
    CHECK(captured_can_present != nullptr);
    CHECK(captured_present != nullptr);
    CHECK(captured_can_present(&record, &context));
    captured_present(&record, &context);
}

void test_default_audit_only_authority()
{
    CHECK(!CG_NativeEventPresenterEffectAuthorityEnabled());
    reset_effects();
    reset_value_readiness();

    worr_event_payload_muzzle_v1 payload{};
    payload.family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
    payload.flash_id = WORR_EVENT_PLAYER_MUZZLE_BLASTER;
    auto record = record_with(WORR_EVENT_PAYLOAD_MUZZLE_V1, payload);
    record.source_entity = {kEntityIndex, kEntityGeneration};

    /* Disabled effect authority still performs the exact fence/generation
     * lookup, but does not consult fallible effect resources or dispatch. */
    muzzle_value_ready = false;
    present_once(record, authority_context());
    CHECK(total_effect_calls() == 0u);

    auto stale = record;
    ++stale.source_entity.generation;
    const auto context = authority_context();
    CHECK(!captured_can_present(&stale, &context));
    CHECK(total_effect_calls() == 0u);

    worr_event_payload_keyed_poi_v1 poi{};
    poi.key = 77u;
    poi.image_index = 4u;
    const auto poi_record = keyed_poi_record(poi);
    present_once(poi_record, context);
    CHECK(effects.poi_preflight_calls == 0u);
    CHECK(effects.poi_present_calls == 0u);
    reset_value_readiness();
}

void test_invisible_controlled_player_fence_fallback()
{
    worr_event_payload_muzzle_v1 payload{};
    payload.family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
    payload.flash_id = WORR_EVENT_PLAYER_MUZZLE_BLASTER;
    auto record = record_with(WORR_EVENT_PAYLOAD_MUZZLE_V1, payload);
    record.source_entity = {kEntityIndex, kEntityGeneration};
    const auto context = authority_context();

    fence_entity_present = false;
    fence_player_available = true;
    reset_effects();
    reset_value_readiness();
    CG_NativeEventPresenterSetEffectAuthority(true);
    present_once(record, context);
    CHECK(effects.muzzle_calls == 1u);
    CHECK(effects.muzzle_state.number ==
          static_cast<int>(kEntityIndex));
    CHECK(vec_equal(effects.muzzle_state.origin,
                    fence_player.movement.origin));
    CHECK(effects.muzzle_state.angles[PITCH] == -30.0f);
    CHECK(effects.muzzle_state.angles[YAW] == 45.0f);
    CHECK(effects.muzzle_state.angles[ROLL] == 0.0f);
    CHECK(effects.muzzle_generation == kEntityGeneration);
    CHECK(muzzle_readiness_calls == 1u);

    reset_effects();
    reset_value_readiness();
    auto stale = record;
    ++stale.source_entity.generation;
    CHECK(!captured_can_present(&stale, &context));
    CHECK(muzzle_readiness_calls == 0u);
    auto wrong_index = record;
    ++wrong_index.source_entity.index;
    CHECK(!captured_can_present(&wrong_index, &context));
    CHECK(muzzle_readiness_calls == 0u);

    const auto valid_player = fence_player;
    fence_player.component_mask &= ~WORR_SNAPSHOT_PLAYER_VIEW;
    std::memset(fence_player.view_angles, 0,
                sizeof(fence_player.view_angles));
    CHECK(Worr_SnapshotPlayerValidateV2(&fence_player, kMaxEntities));
    CG_NativeEventPresenterSetEffectAuthority(false);
    CG_NativeEventPresenterSetEffectAuthority(true);
    CHECK(!captured_can_present(&record, &context));
    CHECK(muzzle_readiness_calls == 0u);

    fence_player = valid_player;
    fence_player_available = false;
    CG_NativeEventPresenterSetEffectAuthority(false);
    CG_NativeEventPresenterSetEffectAuthority(true);
    CHECK(!captured_can_present(&record, &context));
    CHECK(muzzle_readiness_calls == 0u);

    /* A complete exact-generation entity remains the preferred source even
     * if the player-only fallback is not eligible. */
    fence_player_available = true;
    fence_entity_present = true;
    fence_player.component_mask &= ~WORR_SNAPSHOT_PLAYER_VIEW;
    std::memset(fence_player.view_angles, 0,
                sizeof(fence_player.view_angles));
    CG_NativeEventPresenterSetEffectAuthority(false);
    CG_NativeEventPresenterSetEffectAuthority(true);
    reset_effects();
    reset_value_readiness();
    present_once(record, context);
    CHECK(effects.muzzle_calls == 1u);
    CHECK(vec_equal(effects.muzzle_state.origin, fence_entity.origin));
    CHECK(effects.muzzle_state.angles[PITCH] ==
          fence_entity.angles[PITCH]);

    fence_player = valid_player;
    fence_entity_present = true;
    fence_player_available = true;
    CG_NativeEventPresenterSetEffectAuthority(false);
    reset_effects();
    reset_value_readiness();
}

void test_map_latched_preflight_probe()
{
    cg_native_event_presenter_status_v1 status{};
    CHECK(CG_NativeEventPresenterGetStatus(&status));
    CHECK(status.struct_size == sizeof(status));
    CHECK(status.schema_version ==
          CG_NATIVE_EVENT_PRESENTER_STATUS_VERSION);
    CHECK(status.map_active == 1u);
    CHECK(status.preflight_probe_requested == 0u);
    CHECK(status.preflight_probe_latched == 0u);
    CHECK(status.preflight_probe_active == 0u);
    CHECK(status.resources_required == 0u);

    /* Mid-map changes are visible as requested state only. They cannot arm
     * preflight or cause partial resource registration until the next map. */
    probe_cvar.integer = 1;
    CHECK(CG_NativeEventPresenterGetStatus(&status));
    CHECK(status.preflight_probe_requested == 1u);
    CHECK(status.preflight_probe_latched == 0u);
    CHECK(!CG_NativeEventPresenterPreflightProbeEnabled());
    CHECK(!CG_NativeEventPresenterResourcesRequired());

    CG_NativeEventPresenterBeginMap();
    CHECK(CG_NativeEventPresenterPreflightProbeEnabled());
    CHECK(CG_NativeEventPresenterResourcesRequired());
    CHECK(CG_NativeEventPresenterGetStatus(&status));
    CHECK(status.map_generation == 2u);
    CHECK(status.preflight_probe_latched == 1u);
    CHECK(status.preflight_probe_active == 1u);
    CHECK(status.probe_commits == 0u);

    /* Once latched, a mid-map disable is deferred too. */
    probe_cvar.integer = 0;
    CHECK(CG_NativeEventPresenterGetStatus(&status));
    CHECK(status.preflight_probe_requested == 0u);
    CHECK(status.preflight_probe_latched == 1u);
    CHECK(status.preflight_probe_active == 1u);

    reset_effects();
    reset_value_readiness();
    worr_event_payload_muzzle_v1 muzzle{};
    muzzle.family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
    muzzle.flash_id = WORR_EVENT_PLAYER_MUZZLE_BLASTER;
    auto muzzle_record = record_with(WORR_EVENT_PAYLOAD_MUZZLE_V1,
                                     muzzle);
    muzzle_record.source_entity = {kEntityIndex, kEntityGeneration};
    const auto context = authority_context();

    /* Probe mode reaches the same lifecycle readiness check as effect
     * authority. Rejection is pre-commit and never dispatches an effect. */
    muzzle_value_ready = false;
    CHECK(!captured_can_present(&muzzle_record, &context));
    CHECK(muzzle_readiness_calls == 1u);
    CHECK(total_effect_calls() == 0u);
    CHECK(CG_NativeEventPresenterGetStatus(&status));
    CHECK(status.probe_commits == 0u);

    muzzle_value_ready = true;
    std::uint64_t muzzle_semantic_hash = 0;
    CHECK(Worr_EventRecordSemanticHashV1(
        &muzzle_record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
        &muzzle_semantic_hash));
    raw_probe_status.raw_action_records = 1u;
    raw_probe_status.raw_action_chain_hash =
        Worr_CGameNativeEventProbeChainAppendV1(
            0, WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE,
            muzzle_semantic_hash);
    raw_probe_status.raw_action_by_kind
        [WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE] = 1u;
    present_once(muzzle_record, context);
    CHECK(muzzle_readiness_calls == 2u);
    CHECK(total_effect_calls() == 0u);
    CHECK(CG_NativeEventPresenterGetStatus(&status));
    CHECK(status.probe_commits == 1u);
    CHECK(status.probe_effects_suppressed == 1u);
    CHECK(status.probe_nonvisual_commits == 0u);
    CHECK(status.probe_commits_by_kind
              [CG_NATIVE_EVENT_PRESENTER_KIND_MUZZLE] == 1u);

    const auto *probe_api = CG_GetNativeEventProbeAPI();
    CHECK(probe_api != nullptr);
    CHECK(probe_api->struct_size == sizeof(*probe_api));
    CHECK(probe_api->api_version ==
          WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION);
    worr_cgame_native_event_probe_status_v1 probe_status{};
    CHECK(probe_api->GetStatus(&probe_status));
    CHECK(probe_status.struct_size == sizeof(probe_status));
    CHECK(probe_status.schema_version ==
          WORR_CGAME_NATIVE_EVENT_PROBE_STATUS_VERSION);
    CHECK(probe_status.kind_count ==
          WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT);
    CHECK(probe_status.raw_action_records == 1u);
    CHECK(probe_status.probe_action_commits == 1u);
    CHECK(probe_status.raw_action_chain_hash ==
          probe_status.probe_action_chain_hash);
    CHECK(probe_status.native_effect_dispatches == 0u);
    CHECK(probe_status.legacy_owner_active == 1u);
    CHECK(probe_status.authority_epoch == runtime_status.authority_epoch);
    CHECK(probe_status.authority_requires_resync ==
          runtime_status.authority_requires_resync);
    CHECK(probe_status.authority_degraded ==
          runtime_status.authority_degraded);
    CHECK(probe_status.authoritative_presentations ==
          runtime_status.authoritative_presentations);
    CHECK(probe_status.authoritative_duplicates ==
          runtime_status.authoritative_duplicates);
    CHECK(probe_status.authoritative_conflicts ==
          runtime_status.authoritative_conflicts);
    CHECK(probe_status.authority_ref_body_joins ==
          runtime_status.authority_ref_body_joins);
    CHECK(probe_status.legacy_ref_body_mismatches ==
          runtime_status.legacy_ref_body_mismatches);

    /* The prepared plan is single-use, so a duplicate callback cannot
     * dispatch or inflate probe accounting. */
    captured_present(&muzzle_record, &context);
    CHECK(total_effect_calls() == 0u);
    CHECK(CG_NativeEventPresenterGetStatus(&status));
    CHECK(status.probe_commits == 1u);
    CHECK(probe_api->GetStatus(&probe_status));
    CHECK(probe_status.presenter_commit_mismatches == 1u);

    worr_event_payload_u32x4_v1 state{};
    state.value[0] = 77u;
    const auto state_record = record_with(WORR_EVENT_PAYLOAD_U32X4,
                                          state);
    present_once(state_record, context);
    CHECK(total_effect_calls() == 0u);
    CHECK(CG_NativeEventPresenterGetStatus(&status));
    CHECK(status.probe_commits == 2u);
    CHECK(status.probe_effects_suppressed == 1u);
    CHECK(status.probe_nonvisual_commits == 1u);
    CHECK(status.probe_commits_by_kind
              [CG_NATIVE_EVENT_PRESENTER_KIND_NONE] == 1u);

    worr_event_payload_spatial_audio_v1 missing_audio{};
    missing_audio.asset_id = 6u;
    missing_audio.flags = WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION;
    missing_audio.raw_entity = WORR_EVENT_NO_ENTITY;
    missing_audio.volume = 1.0f;
    missing_audio.attenuation = 1.0f;
    missing_audio.pitch = 1.0f;
    const auto missing_audio_record = record_with(
        WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1, missing_audio);
    CHECK(!captured_can_present(&missing_audio_record, &context));
    CHECK(CG_NativeEventPresenterGetStatus(&status));
    CHECK(status.probe_commits == 2u);
    std::printf(
        "native event probe schema=%u map=%u commits=%llu "
        "suppressed=%llu nonvisual=%llu muzzle=%llu none=%llu "
        "effect_calls=%u\n",
        status.schema_version, status.map_generation,
        static_cast<unsigned long long>(status.probe_commits),
        static_cast<unsigned long long>(
            status.probe_effects_suppressed),
        static_cast<unsigned long long>(status.probe_nonvisual_commits),
        static_cast<unsigned long long>(status.probe_commits_by_kind
            [CG_NATIVE_EVENT_PRESENTER_KIND_MUZZLE]),
        static_cast<unsigned long long>(status.probe_commits_by_kind
            [CG_NATIVE_EVENT_PRESENTER_KIND_NONE]),
        total_effect_calls());

    /* The existing explicit effect-authority test gate takes precedence over
     * a requested probe. Its commit dispatches and is not counted as a probe
     * suppression; disabling it returns to the immutable map latch. */
    CG_NativeEventPresenterSetEffectAuthority(true);
    CHECK(!CG_NativeEventPresenterPreflightProbeEnabled());
    reset_effects();
    present_once(muzzle_record, context);
    CHECK(effects.muzzle_calls == 1u);
    CHECK(CG_NativeEventPresenterGetStatus(&status));
    CHECK(status.effect_authority_enabled == 1u);
    CHECK(status.probe_commits == 2u);
    CG_NativeEventPresenterSetEffectAuthority(false);
    CHECK(CG_NativeEventPresenterPreflightProbeEnabled());
    reset_effects();

    CG_NativeEventPresenterEndMap();
    CHECK(CG_NativeEventPresenterGetStatus(&status));
    CHECK(status.map_active == 0u);
    CHECK(status.preflight_probe_latched == 0u);
    CHECK(status.preflight_probe_active == 0u);
    CHECK(status.probe_commits == 2u);
    CHECK(probe_api->GetStatus(&probe_status));
    CHECK(probe_status.map_end_count == 1u);
    CHECK(probe_status.map_active == 0u);

    CG_NativeEventPresenterBeginMap();
    CHECK(!CG_NativeEventPresenterPreflightProbeEnabled());
    CHECK(!CG_NativeEventPresenterResourcesRequired());
    CHECK(CG_NativeEventPresenterGetStatus(&status));
    CHECK(status.map_generation == 3u);
    CHECK(status.probe_commits == 0u);
    CHECK(probe_api->GetStatus(&probe_status));
    CHECK(probe_status.map_generation == 3u);
    CHECK(probe_status.map_end_count == 1u);
    CHECK(probe_status.probe_action_commits == 0u);
    CHECK(probe_status.probe_action_chain_hash == 0u);
    CHECK(probe_status.presenter_commit_mismatches == 0u);
    CHECK(probe_status.raw_action_records == 0u);
    reset_value_readiness();
}

void test_probe_abi_and_hash_contract()
{
    CHECK(sizeof(worr_cgame_native_event_probe_status_v1) == 336u);
    CHECK(offsetof(worr_cgame_native_event_probe_status_v1,
                   raw_action_records) == 64u);
    CHECK(offsetof(worr_cgame_native_event_probe_status_v1,
                   raw_action_by_kind) == 208u);
    CHECK(offsetof(worr_cgame_native_event_probe_status_v1,
                   probe_action_by_kind) == 272u);
    CHECK(WORR_CGAME_NATIVE_EVENT_PROBE_STATUS_VERSION == 3u);
    CHECK(WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT == 8u);
    CHECK(WORR_CGAME_NATIVE_EVENT_PROBE_KIND_KEYED_POI == 7u);
    CHECK(sizeof(worr_cgame_native_event_probe_checkpoint_receipt_v1) ==
          32u);
    CHECK(offsetof(worr_cgame_native_event_probe_checkpoint_receipt_v1,
                   checkpoint_id) == 24u);
    CHECK(sizeof(worr_cgame_native_event_probe_export_v1) ==
          8u + 2u * sizeof(void *));
    CHECK(sizeof(worr_cgame_native_event_probe_export_v2) ==
          8u + 3u * sizeof(void *));
    const std::uint64_t first =
        Worr_CGameNativeEventProbeChainAppendV1(
            0, WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE,
            UINT64_C(0x0123456789abcdef));
    CHECK(first == UINT64_C(0x1afd7a57d96c98ba));
    CHECK(Worr_CGameNativeEventProbeChainAppendV1(
              first,
              WORR_CGAME_NATIVE_EVENT_PROBE_KIND_SPATIAL_AUDIO,
              UINT64_C(0xfedcba9876543210)) ==
          UINT64_C(0x6ec89b5d02659d00));
}

void test_probe_checkpoint_contract()
{
    CG_NativeEventPresenterSetEffectAuthority(false);
    probe_cvar.integer = 1;
    CG_NativeEventPresenterBeginMap();

    runtime_status.authority_epoch = 71u;
    runtime_status.authority_requires_resync = 0u;
    runtime_status.authority_degraded = 0u;
    runtime_status.authoritative_presentations = 19u;
    runtime_status.authoritative_duplicates = 3u;
    runtime_status.authoritative_conflicts = 0u;
    runtime_status.authority_ref_body_joins = 17u;
    runtime_status.legacy_ref_body_mismatches = 0u;
    runtime_checkpoint_ready = true;
    raw_checkpoint_ready = true;
    raw_probe_status.raw_action_records = 1u;
    raw_probe_status.raw_action_chain_hash = UINT64_C(0x1111);
    raw_probe_status.raw_effect_dispatches = 1u;
    raw_probe_status.raw_effect_chain_hash = UINT64_C(0x1111);
    raw_probe_status.raw_action_by_kind
        [WORR_CGAME_NATIVE_EVENT_PROBE_KIND_SPATIAL_AUDIO] = 1u;

    const auto *v1 = CG_GetNativeEventProbeAPI();
    const auto *v2 = CG_GetNativeEventProbeAPIv2();
    CHECK(v1 != nullptr && v2 != nullptr);
    CHECK(v1->api_version == WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION);
    CHECK(v2->api_version ==
          WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION_V2);
    CHECK(v2->Checkpoint != nullptr);

    worr_cgame_native_event_probe_checkpoint_receipt_v1 receipt{};
    CHECK(!v2->Checkpoint(5u, 71u, UINT64_C(9001), nullptr));
    CHECK(v2->Checkpoint(0u, 71u, UINT64_C(9001), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_INVALID_ARGUMENT);
    CHECK(v2->Checkpoint(5u, 0u, UINT64_C(9001), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_INVALID_ARGUMENT);
    CHECK(v2->Checkpoint(5u, 71u, 0u, &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_INVALID_ARGUMENT);
    CG_NativeEventPresenterSetEffectAuthority(true);
    CHECK(v2->Checkpoint(5u, 71u, UINT64_C(9001), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_NOT_READY);
    CG_NativeEventPresenterSetEffectAuthority(false);
    CHECK(v2->Checkpoint(5u, 71u, UINT64_C(9001), &receipt));
    CHECK(receipt.struct_size == sizeof(receipt));
    CHECK(receipt.schema_version ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_VERSION);
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED);
    CHECK(receipt.observed_map_generation == 5u);
    CHECK(receipt.observed_authority_epoch == 71u);
    CHECK(receipt.checkpoint_id == UINT64_C(9001));
    CHECK(raw_checkpoint_apply_calls == 1u);

    worr_cgame_native_event_probe_status_v1 zero{};
    CHECK(v1->GetStatus(&zero));
    CHECK(zero.raw_action_records == 0u);
    CHECK(zero.raw_effect_dispatches == 0u);
    CHECK(zero.probe_action_commits == 0u);
    CHECK(zero.probe_effects_suppressed == 0u);
    CHECK(zero.authoritative_presentations == 0u);
    CHECK(zero.authoritative_duplicates == 0u);
    CHECK(zero.authoritative_conflicts == 0u);
    CHECK(zero.authority_ref_body_joins == 0u);
    CHECK(zero.legacy_ref_body_mismatches == 0u);

    worr_event_payload_muzzle_v1 muzzle{};
    muzzle.family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
    muzzle.flash_id = WORR_EVENT_PLAYER_MUZZLE_BLASTER;
    auto record = record_with(WORR_EVENT_PAYLOAD_MUZZLE_V1, muzzle);
    record.source_entity = {kEntityIndex, kEntityGeneration};
    std::uint64_t semantic_hash = 0;
    CHECK(Worr_EventRecordSemanticHashV1(
        &record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
        &semantic_hash));
    raw_probe_status.raw_action_records = 1u;
    raw_probe_status.raw_action_chain_hash =
        Worr_CGameNativeEventProbeChainAppendV1(
            0, WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE,
            semantic_hash);
    raw_probe_status.raw_effect_dispatches = 1u;
    raw_probe_status.raw_effect_chain_hash =
        raw_probe_status.raw_action_chain_hash;
    raw_probe_status.raw_action_by_kind
        [WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE] = 1u;
    present_once(record, authority_context());
    ++runtime_status.authoritative_presentations;
    ++runtime_status.authority_ref_body_joins;

    worr_cgame_native_event_probe_status_v1 after{};
    CHECK(v1->GetStatus(&after));
    CHECK(after.raw_action_records == 1u);
    CHECK(after.probe_action_commits == 1u);
    CHECK(after.raw_action_chain_hash == after.probe_action_chain_hash);
    CHECK(after.authoritative_presentations == 1u);
    CHECK(after.authority_ref_body_joins == 1u);

    CHECK(v2->Checkpoint(5u, 71u, UINT64_C(9001), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED);
    CHECK(raw_checkpoint_apply_calls == 1u);
    worr_cgame_native_event_probe_status_v1 duplicate{};
    CHECK(v1->GetStatus(&duplicate));
    CHECK(std::memcmp(&duplicate, &after, sizeof(after)) == 0);

    CHECK(v2->Checkpoint(5u, 71u, UINT64_C(9002), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_CONFLICT);
    CHECK(v2->Checkpoint(4u, 71u, UINT64_C(9001), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_STALE_MAP);
    CHECK(v2->Checkpoint(5u, 70u, UINT64_C(9001), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_STALE_AUTHORITY);
    CG_NativeEventPresenterEndMap();

    CG_NativeEventPresenterBeginMap();
    raw_probe_status.raw_pending_count = 1u;
    CHECK(v2->Checkpoint(6u, 71u, UINT64_C(9002), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_BUSY);
    raw_probe_status.raw_pending_count = 0u;
    raw_probe_status.raw_pair_failures = 1u;
    CHECK(v2->Checkpoint(6u, 71u, UINT64_C(9002), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_UNHEALTHY);
    raw_probe_status.raw_pair_failures = 0u;
    runtime_checkpoint_ready = false;
    CHECK(v2->Checkpoint(6u, 71u, UINT64_C(9002), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_BUSY);
    runtime_checkpoint_ready = true;

    const auto busy_context = authority_context();
    CHECK(captured_can_present(&record, &busy_context));
    CHECK(v2->Checkpoint(6u, 71u, UINT64_C(9002), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_BUSY);
    CG_NativeEventPresenterSetEffectAuthority(true);
    CG_NativeEventPresenterSetEffectAuthority(false);
    runtime_status.authority_requires_resync = 1u;
    CHECK(v2->Checkpoint(6u, 71u, UINT64_C(9002), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_UNHEALTHY);
    runtime_status.authority_requires_resync = 0u;
    CHECK(v2->Checkpoint(6u, 71u, UINT64_C(9002), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED);
    CHECK(raw_checkpoint_apply_calls == 2u);
    CG_NativeEventPresenterEndMap();

    CG_NativeEventPresenterBeginMap();
    CHECK(v2->Checkpoint(6u, 71u, UINT64_C(9002), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_STALE_MAP);
    CHECK(v2->Checkpoint(7u, 71u, UINT64_C(9003), &receipt));
    CHECK(receipt.result ==
          WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED);
    CHECK(raw_checkpoint_apply_calls == 3u);
    CG_NativeEventPresenterEndMap();
}

void test_legacy_entity_and_single_use()
{
    reset_effects();
    worr_event_payload_legacy_entity_v1 payload{};
    payload.raw_event = EV_ITEM_RESPAWN;
    payload.flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
    auto record = record_with(WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1,
                              payload);
    record.source_entity = {kEntityIndex, kEntityGeneration};
    const auto context = authority_context();
    present_once(record, context);
    CHECK(effects.legacy_entity_calls == 1u);
    CHECK(effects.legacy_entity_number == static_cast<int>(kEntityIndex));
    CHECK(effects.legacy_entity_event == EV_ITEM_RESPAWN);
    CHECK(effects.legacy_entity_state.number ==
          static_cast<int>(kEntityIndex));
    CHECK(vec_equal(effects.legacy_entity_state.origin,
                    fence_entity.origin));

    captured_present(&record, &context);
    CHECK(effects.legacy_entity_calls == 1u);
}

void test_temp_and_muzzle()
{
    reset_effects();
    worr_event_payload_legacy_temp_v1 temp_payload{};
    temp_payload.subtype = TE_GUNSHOT;
    temp_payload.raw_entity1 = 0;
    CHECK(Worr_EventLegacyTempFieldMaskV1(
        temp_payload.subtype, temp_payload.raw_entity1,
        &temp_payload.valid_fields));
    temp_payload.position1[0] = 1.0f;
    temp_payload.position1[1] = 2.0f;
    temp_payload.position1[2] = 3.0f;
    auto temp_record = record_with(WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
                                   temp_payload);
    present_once(temp_record, authority_context());
    CHECK(effects.temp_calls == 1u);
    CHECK(effects.temp.type == TE_GUNSHOT);
    CHECK(vec_equal(effects.temp.pos1, temp_payload.position1));
    CHECK(!effects.temp_has_fixed_origin);

    reset_effects();
    worr_event_payload_muzzle_v1 muzzle_payload{};
    muzzle_payload.family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
    muzzle_payload.flash_id = WORR_EVENT_PLAYER_MUZZLE_BLASTER;
    auto muzzle_record = record_with(WORR_EVENT_PAYLOAD_MUZZLE_V1,
                                     muzzle_payload);
    muzzle_record.source_entity = {kEntityIndex, kEntityGeneration};
    present_once(muzzle_record, authority_context());
    CHECK(effects.muzzle_calls == 1u);
    CHECK(effects.muzzle.entity == static_cast<int>(kEntityIndex));
    CHECK(effects.muzzle.weapon == WORR_EVENT_PLAYER_MUZZLE_BLASTER);
    CHECK(!effects.muzzle_monster);
    CHECK(effects.muzzle_generation == kEntityGeneration);
    CHECK(vec_equal(effects.muzzle_state.origin, fence_entity.origin));
    CHECK(vec_equal(effects.muzzle_sound_origin, fence_entity.origin));

    worr_cgame_native_event_probe_status_v1 status{};
    CHECK(CG_GetNativeEventProbeAPI()->GetStatus(&status));
    CHECK(status.native_effect_dispatches == 3u);
    CHECK(status.native_effect_chain_hash != 0u);
}

void test_spatial_audio()
{
    reset_effects();
    worr_event_payload_spatial_audio_v1 positioned{};
    positioned.asset_id = 5u;
    positioned.flags = WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION;
    positioned.raw_entity = WORR_EVENT_NO_ENTITY;
    positioned.origin[0] = 11.0f;
    positioned.origin[1] = 22.0f;
    positioned.origin[2] = 33.0f;
    positioned.volume = 0.75f;
    positioned.attenuation = 0.5f;
    positioned.time_offset = 0.125f;
    positioned.pitch = 1.0f;
    const auto positioned_record = record_with(
        WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1, positioned);
    present_once(positioned_record, authority_context());
    CHECK(effects.sound_calls == 1u);
    CHECK(effects.sound_handle == 505);
    CHECK(effects.sound_entity == -1);
    CHECK(effects.sound_channel == 0);
    CHECK(vec_equal(effects.sound_origin, positioned.origin));
    CHECK(effects.sound_volume == positioned.volume);
    CHECK(effects.sound_attenuation == positioned.attenuation);
    CHECK(effects.sound_time_offset == positioned.time_offset);

    reset_effects();
    worr_event_payload_spatial_audio_v1 attached{};
    attached.asset_id = 5u;
    attached.flags = WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL;
    attached.raw_entity = kEntityIndex;
    attached.channel = CHAN_WEAPON;
    attached.volume = 1.0f;
    attached.attenuation = 1.0f;
    attached.pitch = 1.0f;
    auto attached_record = record_with(
        WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1, attached);
    attached_record.source_entity = {kEntityIndex, kEntityGeneration};
    present_once(attached_record, authority_context());
    CHECK(effects.sound_calls == 1u);
    CHECK(effects.sound_entity == static_cast<int>(kEntityIndex));
    CHECK(effects.sound_channel == CHAN_WEAPON);
    CHECK(vec_equal(effects.sound_origin, fence_entity.origin));

    auto mismatched = attached_record;
    auto mismatched_payload = attached;
    mismatched_payload.raw_entity = kEntityIndex + 1u;
    std::memcpy(mismatched.payload, &mismatched_payload,
                sizeof(mismatched_payload));
    const auto context = authority_context();
    CHECK(!captured_can_present(&mismatched, &context));

    reset_effects();
    auto forced = attached;
    forced.flags |= WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION |
                    WORR_EVENT_SPATIAL_AUDIO_POSITION_FORCED;
    forced.origin[0] = -12.0f;
    forced.origin[1] = 34.0f;
    forced.origin[2] = 56.0f;
    auto forced_record = record_with(
        WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1, forced);
    forced_record.source_entity = {0u, 1u};
    present_once(forced_record, authority_context());
    CHECK(effects.sound_calls == 1u);
    CHECK(effects.sound_entity == static_cast<int>(forced.raw_entity));
    CHECK(effects.sound_channel == forced.channel);
    CHECK(vec_equal(effects.sound_origin, forced.origin));

    auto malformed_forced = forced_record;
    malformed_forced.source_entity = {kEntityIndex, kEntityGeneration};
    CHECK(!captured_can_present(&malformed_forced, &context));
}

void test_damage_help_and_no_effect()
{
    reset_effects();
    worr_event_payload_damage_v1 damage{};
    damage.amount = 12.0f;
    damage.damage_flags = WORR_EVENT_DAMAGE_FLAG_HEALTH;
    damage.direction[0] = -1.0f;
    damage.direction[1] = 2.0f;
    damage.direction[2] = -3.0f;
    auto damage_record = record_with(WORR_EVENT_PAYLOAD_DAMAGE, damage);
    damage_record.subject_entity = {kEntityIndex, kEntityGeneration};
    present_once(damage_record, authority_context());
    CHECK(effects.damage_calls == 1u);
    CHECK(effects.damage == 12);
    const vec3_t red{1.0f, 0.0f, 0.0f};
    CHECK(vec_equal(effects.damage_color, red));
    CHECK(vec_equal(effects.damage_direction, damage.direction));

    reset_effects();
    worr_event_payload_effect_v1 help{};
    help.effect_id = WORR_EVENT_EFFECT_HELP_PATH_MARKER;
    help.variant = WORR_EVENT_HELP_PATH_VARIANT_START;
    help.origin[0] = 4.0f;
    help.origin[1] = 5.0f;
    help.origin[2] = 6.0f;
    help.direction[0] = 0.0f;
    help.direction[1] = 1.0f;
    help.direction[2] = 0.0f;
    const auto help_record = record_with(WORR_EVENT_PAYLOAD_EFFECT, help);
    present_once(help_record, authority_context());
    CHECK(effects.help_calls == 1u);
    CHECK(effects.help_first);
    CHECK(vec_equal(effects.help_origin, help.origin));
    CHECK(vec_equal(effects.help_direction, help.direction));

    reset_effects();
    worr_event_payload_u32x4_v1 no_effect{};
    no_effect.value[0] = 42u;
    const auto no_effect_record = record_with(WORR_EVENT_PAYLOAD_U32X4,
                                              no_effect);
    present_once(no_effect_record, authority_context());
    CHECK(total_effect_calls() == 0u);
}

void test_keyed_poi_two_phase_dispatch()
{
    reset_effects();
    reset_value_readiness();
    worr_event_payload_keyed_poi_v1 payload{};
    payload.key = 901u;
    payload.lifetime_ms = 1500u;
    payload.position[0] = 10.25f;
    payload.position[1] = -20.5f;
    payload.position[2] = 30.75f;
    payload.image_index = 7u;
    payload.color_index = 3u;
    payload.flags = WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM;
    const auto record = keyed_poi_record(payload);
    present_once(record, authority_context());
    CHECK(effects.poi_preflight_calls == 1u);
    CHECK(effects.poi_present_calls == 1u);
    CHECK(std::memcmp(&effects.poi.payload, &payload, sizeof(payload)) == 0);
    CHECK(effects.poi.disposition ==
          CG_KEYED_POI_PRESENTATION_UPSERT_V1);
    CHECK(effects.poi.slot_index == 5u);

    auto second_payload = payload;
    second_payload.position[0] += 100.0f;
    second_payload.color_index += 1u;
    present_once(keyed_poi_record(second_payload), authority_context());
    CHECK(effects.poi_preflight_calls == 2u);
    CHECK(effects.poi_present_calls == 2u);
    CHECK(effects.poi_history[0].payload.key ==
          effects.poi_history[1].payload.key);
    CHECK(effects.poi_history[0].payload.position[0] ==
          payload.position[0]);
    CHECK(effects.poi_history[1].payload.position[0] ==
          second_payload.position[0]);

    reset_effects();
    worr_event_payload_keyed_poi_v1 removal{};
    removal.key = payload.key;
    removal.lifetime_ms = WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS;
    present_once(keyed_poi_record(removal), authority_context());
    CHECK(effects.poi_preflight_calls == 1u);
    CHECK(effects.poi_present_calls == 1u);
    CHECK(effects.poi.disposition ==
          CG_KEYED_POI_PRESENTATION_DELETE_V1);
    CHECK(std::memcmp(&effects.poi.payload, &removal,
                      sizeof(removal)) == 0);

    reset_effects();
    poi_state_ready = false;
    const auto context = authority_context();
    CHECK(!captured_can_present(&record, &context));
    CHECK(effects.poi_preflight_calls == 1u);
    CHECK(effects.poi_present_calls == 0u);
    poi_state_ready = true;

    reset_effects();
    auto missing_subject = record;
    missing_subject.subject_entity = absent_ref();
    CHECK(!captured_can_present(&missing_subject, &context));
    CHECK(effects.poi_preflight_calls == 0u);
    CHECK(effects.poi_present_calls == 0u);
}

void test_real_keyed_poi_sink_state()
{
    using slots_t = std::array<keyed_poi_test_slot_t, 4>;
    slots_t slots{};
    worr_event_payload_keyed_poi_v1 payload{};
    payload.key = 41u;
    payload.lifetime_ms = 500u;
    payload.position[0] = 1.25f;
    payload.position[1] = -2.5f;
    payload.position[2] = 3.75f;
    payload.image_index = 17u;
    payload.color_index = 4u;
    payload.flags = WORR_EVENT_KEYED_POI_FLAG_HIDE_ON_AIM;

    const auto empty_before = slots;
    cg_prepared_keyed_poi_v1 prepared{};
    CHECK(CG_PrepareKeyedPOIStateV1(
        slots, &payload, true, true, 1000u, &prepared));
    CHECK(std::memcmp(&slots, &empty_before, sizeof(slots)) == 0);
    CHECK(prepared.disposition ==
          CG_KEYED_POI_PRESENTATION_UPSERT_V1);
    CHECK(prepared.slot_index == 0u);
    CHECK(prepared.expiry_time == 1500u);
    CHECK(prepared.image_name[0] == '\0' &&
          prepared.width == 0 && prepared.height == 0);
    CHECK(CG_CommitKeyedPOIStateV1(slots, &prepared));
    CHECK(slots[0].id == payload.key);
    CHECK(slots[0].time == prepared.expiry_time);
    CHECK(!slots[0].infinite);
    CHECK(slots[0].image_index == payload.image_index);
    CHECK(slots[0].image_name[0] == '\0' &&
          slots[0].width == 0 && slots[0].height == 0);
    CHECK(slots[0].position[0] == payload.position[0] &&
          slots[0].position[1] == payload.position[1] &&
          slots[0].position[2] == payload.position[2]);

    const auto disabled_before = slots;
    payload.key = 42u;
    CHECK(CG_PrepareKeyedPOIStateV1(
        slots, &payload, false, false, 0u, &prepared));
    CHECK(prepared.disposition ==
          CG_KEYED_POI_PRESENTATION_NOOP_DISABLED_V1);
    CHECK(std::memcmp(&slots, &disabled_before, sizeof(slots)) == 0);
    CHECK(!CG_CommitKeyedPOIStateV1(slots, &prepared));
    CHECK(std::memcmp(&slots, &disabled_before, sizeof(slots)) == 0);

    for (std::uint32_t index = 0; index < slots.size(); ++index) {
        slots[index] = {};
        slots[index].id = static_cast<int>(100u + index);
        slots[index].infinite = true;
    }
    const auto capacity_before = slots;
    payload.key = 77u;
    CHECK(CG_PrepareKeyedPOIStateV1(
        slots, &payload, true, true, 2000u, &prepared));
    CHECK(prepared.disposition ==
          CG_KEYED_POI_PRESENTATION_NOOP_CAPACITY_V1);
    CHECK(std::memcmp(&slots, &capacity_before, sizeof(slots)) == 0);
    CHECK(!CG_CommitKeyedPOIStateV1(slots, &prepared));
    CHECK(std::memcmp(&slots, &capacity_before, sizeof(slots)) == 0);

    worr_event_payload_keyed_poi_v1 removal{};
    removal.key = 101u;
    removal.lifetime_ms = WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS;
    const auto delete_before = slots;
    CHECK(CG_PrepareKeyedPOIStateV1(
        slots, &removal, true, false, 0u, &prepared));
    CHECK(prepared.disposition ==
          CG_KEYED_POI_PRESENTATION_DELETE_V1);
    CHECK(prepared.slot_index == 1u);
    CHECK(std::memcmp(&slots, &delete_before, sizeof(slots)) == 0);
    CHECK(CG_CommitKeyedPOIStateV1(slots, &prepared));
    CHECK(slots[1].id == 0 && !slots[1].infinite &&
          slots[1].image_name[0] == '\0');
    CHECK(slots[0].id == delete_before[0].id &&
          slots[2].id == delete_before[2].id &&
          slots[3].id == delete_before[3].id);

    removal.key = 999u;
    const auto missing_delete_before = slots;
    CHECK(CG_PrepareKeyedPOIStateV1(
        slots, &removal, true, false, 0u, &prepared));
    CHECK(prepared.disposition ==
          CG_KEYED_POI_PRESENTATION_NOOP_MISSING_V1);
    CHECK(std::memcmp(&slots, &missing_delete_before,
                      sizeof(slots)) == 0);
    CHECK(!CG_CommitKeyedPOIStateV1(slots, &prepared));
    CHECK(std::memcmp(&slots, &missing_delete_before,
                      sizeof(slots)) == 0);
}

void test_fail_closed_inputs()
{
    reset_effects();
    worr_event_payload_audio_v1 generic_audio{};
    generic_audio.asset_id = 5u;
    generic_audio.volume = 1.0f;
    generic_audio.attenuation = 1.0f;
    generic_audio.pitch = 1.0f;
    const auto generic_record = record_with(WORR_EVENT_PAYLOAD_AUDIO,
                                            generic_audio);
    const auto context = authority_context();
    CHECK(!captured_can_present(&generic_record, &context));
    CHECK(total_effect_calls() == 0u);

    worr_event_payload_legacy_entity_v1 legacy{};
    legacy.raw_event = EV_FOOTSTEP;
    legacy.flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
    auto wrong_generation = record_with(
        WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1, legacy);
    wrong_generation.event_type = WORR_EVENT_TYPE_MOVEMENT_CUE;
    wrong_generation.source_entity = {kEntityIndex,
                                      kEntityGeneration + 1u};
    CHECK(!captured_can_present(&wrong_generation, &context));

    auto missing_context = authority_context();
    ++missing_context.fence_snapshot_id.sequence;
    auto exact_record = wrong_generation;
    exact_record.source_entity = {kEntityIndex, kEntityGeneration};
    CHECK(!captured_can_present(&exact_record, &missing_context));

    auto wrong_tick = context;
    ++wrong_tick.fence_tick;
    CHECK(!captured_can_present(&exact_record, &wrong_tick));
    auto wrong_time = context;
    ++wrong_time.fence_time_us;
    CHECK(!captured_can_present(&exact_record, &wrong_time));

    fence_available = false;
    CHECK(!captured_can_present(&exact_record, &context));
    fence_available = true;
    CHECK(total_effect_calls() == 0u);
}

void test_resource_and_lifecycle_rejection()
{
    reset_effects();
    reset_value_readiness();
    const auto context = authority_context();

    worr_event_payload_legacy_entity_v1 legacy{};
    legacy.raw_event = EV_ITEM_RESPAWN;
    legacy.flags = WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION;
    auto legacy_record = record_with(
        WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1, legacy);
    legacy_record.source_entity = {kEntityIndex, kEntityGeneration};
    legacy_value_ready = false;
    CHECK(!captured_can_present(&legacy_record, &context));
    legacy_value_ready = true;

    worr_event_payload_legacy_temp_v1 temp{};
    temp.subtype = TE_GUNSHOT;
    CHECK(Worr_EventLegacyTempFieldMaskV1(
        temp.subtype, temp.raw_entity1, &temp.valid_fields));
    const auto temp_record = record_with(
        WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1, temp);
    temp_value_ready = false;
    CHECK(!captured_can_present(&temp_record, &context));
    temp_value_ready = true;

    worr_event_payload_muzzle_v1 muzzle{};
    muzzle.family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
    muzzle.flash_id = WORR_EVENT_PLAYER_MUZZLE_BLASTER;
    auto muzzle_record = record_with(WORR_EVENT_PAYLOAD_MUZZLE_V1,
                                     muzzle);
    muzzle_record.source_entity = {kEntityIndex, kEntityGeneration};
    muzzle_value_ready = false;
    CHECK(!captured_can_present(&muzzle_record, &context));
    muzzle_value_ready = true;

    worr_event_payload_effect_v1 help{};
    help.effect_id = WORR_EVENT_EFFECT_HELP_PATH_MARKER;
    help.variant = WORR_EVENT_HELP_PATH_VARIANT_START;
    const auto help_record = record_with(WORR_EVENT_PAYLOAD_EFFECT, help);
    help_value_ready = false;
    CHECK(!captured_can_present(&help_record, &context));
    help_value_ready = true;

    worr_event_payload_spatial_audio_v1 audio{};
    audio.asset_id = 6u;
    audio.flags = WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION;
    audio.raw_entity = WORR_EVENT_NO_ENTITY;
    audio.volume = 1.0f;
    audio.attenuation = 1.0f;
    audio.pitch = 1.0f;
    const auto audio_record = record_with(
        WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1, audio);
    CHECK(!captured_can_present(&audio_record, &context));
    CHECK(total_effect_calls() == 0u);

    /* Toggling authority is a lifecycle boundary: it scrubs any prepared
     * value plan and defaults back to audit-only consumption. */
    CHECK(captured_can_present(&muzzle_record, &context));
    CG_NativeEventPresenterSetEffectAuthority(false);
    CHECK(!CG_NativeEventPresenterEffectAuthorityEnabled());
    captured_present(&muzzle_record, &context);
    CHECK(effects.muzzle_calls == 0u);
    CG_NativeEventPresenterSetEffectAuthority(true);
    CHECK(CG_NativeEventPresenterEffectAuthorityEnabled());
}

void test_native_semantic_lane_and_map_reuse()
{
    CG_NativeEventPresenterEndMap();
    CG_NativeEventPresenterBeginMap();
    CG_NativeEventPresenterSetEffectAuthority(true);
    reset_effects();
    reset_value_readiness();

    worr_event_payload_muzzle_v1 payload{};
    payload.family = WORR_EVENT_MUZZLE_FAMILY_PLAYER;
    payload.flash_id = WORR_EVENT_PLAYER_MUZZLE_BLASTER;
    auto record = record_with(WORR_EVENT_PAYLOAD_MUZZLE_V1, payload);
    record.source_entity = {kEntityIndex, kEntityGeneration};
    std::uint64_t semantic_hash = 0;
    CHECK(Worr_EventRecordSemanticHashV1(
        &record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2,
        &semantic_hash));

    present_once(record, authority_context());
    CHECK(effects.muzzle_calls == 1u);
    worr_cgame_native_event_probe_status_v1 status{};
    CHECK(CG_GetNativeEventProbeAPI()->GetStatus(&status));
    CHECK(status.map_generation == 4u);
    CHECK(status.map_end_count == 2u);
    CHECK(status.map_active == 1u);
    CHECK(status.legacy_owner_active == 0u);
    CHECK(status.probe_action_commits == 0u);
    CHECK(status.probe_action_chain_hash == 0u);
    CHECK(status.native_effect_dispatches == 1u);
    CHECK(status.native_effect_chain_hash ==
          Worr_CGameNativeEventProbeChainAppendV1(
              0, WORR_CGAME_NATIVE_EVENT_PROBE_KIND_MUZZLE,
              semantic_hash));
    CHECK(status.presenter_commit_mismatches == 0u);

    CG_NativeEventPresenterEndMap();
    CHECK(CG_GetNativeEventProbeAPI()->GetStatus(&status));
    CHECK(status.map_end_count == 3u);
    CHECK(status.map_active == 0u);
    CHECK(status.native_effect_dispatches == 1u);
    CHECK(status.native_effect_chain_hash != 0u);
}

} // namespace

void CG_NativeEventProbeRawBeginMap()
{
    ++raw_begin_calls;
    raw_probe_status = {};
}

void CG_NativeEventProbeRawEndMap()
{
    ++raw_end_calls;
    raw_probe_status.raw_pending_count = 0u;
}

void CG_NativeEventProbeRawUninstall()
{
    ++raw_uninstall_calls;
    raw_probe_status = {};
}

bool CG_NativeEventProbeAuthorityHash(
    const worr_event_record_v1 *record,
    const cg_event_runtime_presentation_context_v1 *,
    std::uint64_t *hash_out)
{
    return record && hash_out && Worr_EventRecordSemanticHashV1(
        record, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2, hash_out);
}

bool CG_NativeEventProbeCompleteLegacyDispatch(
    std::uint32_t,
    worr_cgame_native_event_probe_legacy_disposition_v1)
{
    ++raw_complete_calls;
    return true;
}

bool CG_NativeEventProbeFillRawStatus(
    worr_cgame_native_event_probe_status_v1 *status_out)
{
    if (!status_out)
        return false;
    status_out->raw_pending_count = raw_probe_status.raw_pending_count;
    status_out->raw_action_records = raw_probe_status.raw_action_records;
    status_out->raw_action_chain_hash =
        raw_probe_status.raw_action_chain_hash;
    status_out->raw_effect_dispatches =
        raw_probe_status.raw_effect_dispatches;
    status_out->raw_effect_chain_hash =
        raw_probe_status.raw_effect_chain_hash;
    status_out->raw_effect_suppressions =
        raw_probe_status.raw_effect_suppressions;
    status_out->raw_pair_failures = raw_probe_status.raw_pair_failures;
    std::memcpy(status_out->raw_action_by_kind,
                raw_probe_status.raw_action_by_kind,
                sizeof(status_out->raw_action_by_kind));
    return true;
}

bool CG_NativeEventProbeRawCheckpointReady()
{
    return raw_checkpoint_ready && raw_probe_status.raw_pending_count == 0u &&
           raw_probe_status.raw_pair_failures == 0u &&
           raw_probe_status.raw_effect_suppressions == 0u;
}

void CG_NativeEventProbeRawApplyCheckpoint()
{
    CHECK(CG_NativeEventProbeRawCheckpointReady());
    ++raw_checkpoint_apply_calls;
    raw_probe_status = {};
}

bool CG_EventRuntimeGetStatus(cg_event_runtime_status_v1 *status_out)
{
    if (!status_out)
        return false;
    *status_out = runtime_status;
    status_out->struct_size = sizeof(*status_out);
    status_out->schema_version = CG_EVENT_RUNTIME_VERSION;
    return true;
}

bool CG_EventRuntimeCheckpointReady(
    std::uint32_t expected_authority_epoch,
    cg_event_runtime_status_v1 *status_out)
{
    CHECK(status_out != nullptr);
    CHECK(CG_EventRuntimeGetStatus(status_out));
    return runtime_checkpoint_ready && expected_authority_epoch != 0u &&
           status_out->authority_epoch == expected_authority_epoch &&
           status_out->authority_requires_resync == 0u &&
           status_out->authority_degraded == 0u;
}

bool CG_EventRuntimeGetCheckpointBlock(
    std::uint32_t expected_authority_epoch,
    cg_event_runtime_checkpoint_block_v1 *block_out)
{
    if (!block_out)
        return false;
    *block_out = {};
    block_out->struct_size = sizeof(*block_out);
    block_out->reason = CG_EVENT_RUNTIME_CHECKPOINT_BLOCK_PENDING_HEAD;
    block_out->expected_authority_epoch = expected_authority_epoch;
    block_out->authority_epoch = runtime_status.authority_epoch;
    block_out->next_authority_sequence =
        runtime_status.next_authority_sequence;
    block_out->pending_sequence = runtime_status.next_authority_sequence;
    block_out->last_render_time_us = runtime_status.last_render_time_us;
    block_out->last_now_tick = runtime_status.last_now_tick;
    return true;
}

void CG_EventRuntimeSetPresenter(
    cg_event_runtime_can_present_callback_v1 can_present,
    cg_event_runtime_present_callback_v1 present)
{
    captured_can_present = can_present;
    captured_present = present;
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineFindSnapshot(
    worr_snapshot_id_v2 snapshot_id,
    worr_snapshot_timeline_ref_v1 *ref_out)
{
    if (!ref_out)
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    if (!fence_available || snapshot_id.epoch != kFenceId.epoch ||
        snapshot_id.sequence != kFenceId.sequence) {
        return WORR_SNAPSHOT_TIMELINE_NOT_FOUND;
    }
    *ref_out = kFenceRef;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineCopySnapshot(
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_v2 *snapshot_out)
{
    if (!snapshot_out)
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    if (ref.slot != kFenceRef.slot ||
        ref.generation != kFenceRef.generation) {
        return WORR_SNAPSHOT_TIMELINE_STALE_REF;
    }
    *snapshot_out = fence_snapshot;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineCopyEntities(
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_entity_v2 *entities_out, std::uint32_t capacity,
    std::uint32_t *count_out)
{
    if (!entities_out || !count_out)
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    if (ref.slot != kFenceRef.slot ||
        ref.generation != kFenceRef.generation) {
        return WORR_SNAPSHOT_TIMELINE_STALE_REF;
    }
    if (fence_entity_present && capacity < 1u)
        return WORR_SNAPSHOT_TIMELINE_BUFFER_TOO_SMALL;
    if (fence_entity_present)
        entities_out[0] = fence_entity;
    *count_out = fence_entity_present ? 1u : 0u;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

worr_snapshot_timeline_result_v1 CG_CanonicalSnapshotTimelineCopyPlayer(
    worr_snapshot_timeline_ref_v1 ref,
    worr_snapshot_player_v2 *player_out)
{
    if (!player_out)
        return WORR_SNAPSHOT_TIMELINE_INVALID_ARGUMENT;
    if (ref.slot != kFenceRef.slot ||
        ref.generation != kFenceRef.generation) {
        return WORR_SNAPSHOT_TIMELINE_STALE_REF;
    }
    if (!fence_player_available)
        return WORR_SNAPSHOT_TIMELINE_NOT_FOUND;
    *player_out = fence_player;
    return WORR_SNAPSHOT_TIMELINE_OK;
}

void CL_PresentLegacyEntityEventValue(int number, int raw_event,
                                      const entity_state_t *source_state)
{
    CHECK(source_state != nullptr);
    ++effects.legacy_entity_calls;
    effects.legacy_entity_number = number;
    effects.legacy_entity_event = raw_event;
    effects.legacy_entity_state = *source_state;
}

bool CL_CanPresentLegacyEntityEventValue(
    int number, int raw_event, const entity_state_t *source_state)
{
    CHECK(number > 0);
    CHECK(raw_event > EV_NONE);
    CHECK(source_state != nullptr);
    return legacy_value_ready;
}

bool CL_CanPresentTEntValue(const tent_params_t *params,
                            const vec3_t fixed_sound_origin,
                            int fixed_sound_entity)
{
    (void)fixed_sound_origin;
    (void)fixed_sound_entity;
    CHECK(params != nullptr);
    return temp_value_ready;
}

void CL_PresentTEntValue(const tent_params_t *params,
                         const vec3_t fixed_sound_origin,
                         int fixed_sound_entity)
{
    CHECK(params != nullptr);
    ++effects.temp_calls;
    effects.temp = *params;
    effects.temp_fixed_entity = fixed_sound_entity;
    if (fixed_sound_origin) {
        effects.temp_has_fixed_origin = true;
        VectorCopy(fixed_sound_origin, effects.temp_fixed_origin);
    }
}

void CL_PresentMuzzleFlashValue(const mz_params_t *params,
                                const entity_state_t *source_state,
                                const vec3_t sound_origin,
                                std::uint32_t source_generation,
                                bool monster_family)
{
    CHECK(params != nullptr);
    CHECK(source_state != nullptr);
    CHECK(sound_origin != nullptr);
    ++effects.muzzle_calls;
    effects.muzzle = *params;
    effects.muzzle_state = *source_state;
    VectorCopy(sound_origin, effects.muzzle_sound_origin);
    effects.muzzle_generation = source_generation;
    effects.muzzle_monster = monster_family;
}

bool CL_CanPresentMuzzleFlashValue(const mz_params_t *params,
                                   const entity_state_t *source_state,
                                   const vec3_t sound_origin,
                                   std::uint32_t source_generation,
                                   bool monster_family)
{
    (void)monster_family;
    CHECK(params != nullptr);
    CHECK(source_state != nullptr);
    CHECK(sound_origin != nullptr);
    CHECK(source_generation != 0u);
    ++muzzle_readiness_calls;
    return muzzle_value_ready;
}

extern "C" void CL_PresentDamageDisplayValue(int damage,
                                               const vec3_t color,
                                               const vec3_t direction)
{
    CHECK(color != nullptr);
    CHECK(direction != nullptr);
    ++effects.damage_calls;
    effects.damage = damage;
    VectorCopy(color, effects.damage_color);
    VectorCopy(direction, effects.damage_direction);
}

void CL_AddHelpPath(const vec3_t origin, const vec3_t direction, bool first)
{
    ++effects.help_calls;
    VectorCopy(origin, effects.help_origin);
    VectorCopy(direction, effects.help_direction);
    effects.help_first = first;
}

bool CL_CanPresentHelpPathValue(const vec3_t origin,
                                const vec3_t direction, bool first)
{
    (void)first;
    CHECK(origin != nullptr);
    CHECK(direction != nullptr);
    return help_value_ready;
}

bool CG_CanPresentKeyedPOIValue(
    const worr_event_payload_keyed_poi_v1 *payload,
    cg_prepared_keyed_poi_v1 *prepared_out)
{
    CHECK(payload != nullptr);
    CHECK(prepared_out != nullptr);
    ++effects.poi_preflight_calls;
    if (!poi_state_ready)
        return false;

    *prepared_out = {};
    prepared_out->payload = *payload;
    prepared_out->slot_index = 5u;
    prepared_out->expiry_time = payload->lifetime_ms == 0u
                                    ? 0u
                                    : 7000u + payload->lifetime_ms;
    prepared_out->disposition =
        payload->lifetime_ms == WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS
            ? CG_KEYED_POI_PRESENTATION_DELETE_V1
            : CG_KEYED_POI_PRESENTATION_UPSERT_V1;
    std::strcpy(prepared_out->image_name, "pics/test-poi.pcx");
    prepared_out->width = 32;
    prepared_out->height = 24;
    return true;
}

void CG_PresentKeyedPOIValue(const cg_prepared_keyed_poi_v1 *prepared)
{
    CHECK(prepared != nullptr);
    CHECK(effects.poi_present_calls < effects.poi_history.size());
    effects.poi_history[effects.poi_present_calls] = *prepared;
    ++effects.poi_present_calls;
    effects.poi = *prepared;
}

int main()
{
    client_state.csr.max_edicts = kMaxEntities;
    client_state.csr.max_models = kMaxModels;
    client_state.csr.max_sounds = kMaxSounds;
    imports.api_version = CGAME_ENTITY_API_VERSION;
    imports.cl = &client_state;
    imports.Cvar_Get = &fake_cvar_get;
    imports.Com_DPrintf = &fake_debug_print;
    imports.S_GetPrecachedSound = &fake_get_precached_sound;
    imports.S_StartSound = &fake_start_sound;
    cgei = &imports;

    probe_cvar.name = probe_cvar_name;
    probe_cvar.string = probe_cvar_value;
    probe_cvar.default_string = probe_cvar_default;
    probe_cvar.flags = CVAR_NOARCHIVE;
    probe_cvar.integer = 0;

    fence_entity = make_entity();
    CHECK(Worr_SnapshotEntityValidateV2(&fence_entity, kMaxEntities));
    fence_player = make_player();
    CHECK(Worr_SnapshotPlayerValidateV2(&fence_player, kMaxEntities));
    fence_snapshot.struct_size = sizeof(fence_snapshot);
    fence_snapshot.schema_version = WORR_SNAPSHOT_ABI_VERSION;
    fence_snapshot.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
    fence_snapshot.snapshot_id = kFenceId;
    fence_snapshot.server_tick = 700u;
    fence_snapshot.server_time_us = UINT64_C(7000000);

    runtime_status.authority_epoch = 71u;
    runtime_status.authority_requires_resync = 1u;
    runtime_status.authority_degraded = 1u;
    runtime_status.authoritative_presentations = 19u;
    runtime_status.authoritative_duplicates = 3u;
    runtime_status.authoritative_conflicts = 2u;
    runtime_status.authority_ref_body_joins = 17u;
    runtime_status.legacy_ref_body_mismatches = 1u;

    CG_NativeEventPresenterInitCvars();
    CG_NativeEventPresenterInstall();
    CG_NativeEventPresenterBeginMap();
    CHECK(captured_can_present != nullptr);
    CHECK(captured_present != nullptr);
    test_probe_abi_and_hash_contract();
    test_invisible_controlled_player_fence_fallback();
    test_default_audit_only_authority();
    test_map_latched_preflight_probe();
    CG_NativeEventPresenterSetEffectAuthority(true);
    CHECK(CG_NativeEventPresenterEffectAuthorityEnabled());
    test_legacy_entity_and_single_use();
    test_temp_and_muzzle();
    test_spatial_audio();
    test_damage_help_and_no_effect();
    test_keyed_poi_two_phase_dispatch();
    test_real_keyed_poi_sink_state();
    test_fail_closed_inputs();
    test_resource_and_lifecycle_rejection();
    test_native_semantic_lane_and_map_reuse();
    test_probe_checkpoint_contract();
    CG_NativeEventPresenterUninstall();
    CHECK(!CG_NativeEventPresenterEffectAuthorityEnabled());
    CHECK(captured_can_present == nullptr);
    CHECK(captured_present == nullptr);
    CHECK(raw_begin_calls == 7u);
    CHECK(raw_end_calls == 6u);
    CHECK(raw_uninstall_calls == 1u);
    CHECK(raw_complete_calls == 0u);

    std::puts("cgame native event presenter tests passed");
    return EXIT_SUCCESS;
}
