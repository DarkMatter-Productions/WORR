/* Focused FR-10-T07 canonical entity -> cgame render-state coverage. */

#include "cg_canonical_entity_adapter.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

constexpr std::uint32_t kMaxEntities = 8192u;
constexpr std::uint32_t kMaxModels = 4096u;
constexpr std::uint32_t kMaxSounds = 512u;

constexpr cg_canonical_entity_adapter_limits_v1 kLimits{kMaxEntities,
                                                        kMaxModels, kMaxSounds};

[[noreturn]] void fail(const char *expression, int line) {
  std::fprintf(stderr, "cgame_canonical_entity_adapter_test:%d: %s\n", line,
               expression);
  std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression))                                                         \
      fail(#expression, __LINE__);                                             \
  } while (0)

worr_snapshot_entity_generation_v2 generation(std::uint32_t index,
                                              std::uint32_t value) {
  worr_snapshot_entity_generation_v2 result{};
  result.identity.index = index;
  result.identity.generation = value;
  result.provenance_flags = WORR_SNAPSHOT_GENERATION_AUTHORITATIVE;
  return result;
}

worr_snapshot_entity_v2 make_entity() {
  worr_snapshot_entity_v2 entity{};
  entity.struct_size = sizeof(entity);
  entity.schema_version = WORR_SNAPSHOT_ABI_VERSION;
  entity.model_revision = WORR_SNAPSHOT_MODEL_REVISION;
  entity.generation = generation(17u, 29u);
  entity.component_mask = WORR_SNAPSHOT_ENTITY_COMPONENTS_V2;
  entity.origin[0] = -1024.25f;
  entity.origin[1] = 0.125f;
  entity.origin[2] = 8192.5f;
  entity.angles[0] = -45.0f;
  entity.angles[1] = 180.0f;
  entity.angles[2] = 359.75f;
  entity.old_origin[0] = -1025.0f;
  entity.old_origin[1] = -0.5f;
  entity.old_origin[2] = 8191.0f;
  entity.model_index[0] = 1u;
  entity.model_index[1] = 255u;
  entity.model_index[2] = 1024u;
  entity.model_index[3] = kMaxModels - 1u;
  entity.frame = UINT16_MAX;
  entity.sound = kMaxSounds - 1u;
  entity.skin = UINT32_C(0xf1234567);
  entity.solid = UINT32_C(0x8fedcba9);
  entity.effects = UINT64_C(0xfedcba9876543210);
  entity.renderfx = UINT32_C(0x87654321);
  entity.alpha = 0.375f;
  entity.scale = 1.25f;
  entity.loop_volume = 0.625f;
  entity.loop_attenuation = -1.0f;
  entity.owner.index = 3u;
  entity.owner.generation = 11u;
  entity.old_frame = std::numeric_limits<std::int32_t>::min() + 7;
  entity.instance_bits = UINT8_C(0xa5);
  return entity;
}

void check_vec(const float *actual, const float *expected) {
  CHECK(std::memcmp(actual, expected, sizeof(float) * 3u) == 0);
}

void check_full_mapping(const worr_snapshot_entity_v2 &entity,
                        const entity_state_t &state) {
  CHECK(state.number == static_cast<int>(entity.generation.identity.index));
  check_vec(state.origin, entity.origin);
  check_vec(state.angles, entity.angles);
  check_vec(state.old_origin, entity.old_origin);
  CHECK(state.modelindex == entity.model_index[0]);
  CHECK(state.modelindex2 == entity.model_index[1]);
  CHECK(state.modelindex3 == entity.model_index[2]);
  CHECK(state.modelindex4 == entity.model_index[3]);
  CHECK(state.frame == entity.frame);
  CHECK(std::bit_cast<std::uint32_t>(state.skinnum) == entity.skin);
  CHECK(state.effects == entity.effects);
  CHECK(state.renderfx == entity.renderfx);
  CHECK(std::bit_cast<std::uint32_t>(state.solid) == entity.solid);
  CHECK(state.sound == entity.sound);
  CHECK(state.event == 0u);
  CHECK(std::bit_cast<std::uint32_t>(state.alpha) ==
        std::bit_cast<std::uint32_t>(entity.alpha));
  CHECK(std::bit_cast<std::uint32_t>(state.scale) ==
        std::bit_cast<std::uint32_t>(entity.scale));
  CHECK(state.instance_bits == entity.instance_bits);
  CHECK(std::bit_cast<std::uint32_t>(state.loop_volume) ==
        std::bit_cast<std::uint32_t>(entity.loop_volume));
  CHECK(std::bit_cast<std::uint32_t>(state.loop_attenuation) ==
        std::bit_cast<std::uint32_t>(entity.loop_attenuation));
  CHECK(state.owner == static_cast<std::int32_t>(entity.owner.index));
  CHECK(state.old_frame == entity.old_frame);
}

void expect_transactional_failure(
    const worr_snapshot_entity_v2 &entity,
    cg_canonical_entity_adapter_limits_v1 limits,
    cg_canonical_entity_adapter_result_v1 expected) {
  entity_state_t state{};
  std::memset(&state, 0xa5, sizeof(state));
  std::array<std::uint8_t, sizeof(state)> before{};
  std::memcpy(before.data(), &state, sizeof(state));
  CHECK(CG_CanonicalEntityToRenderStateV1(&entity, limits, &state) == expected);
  CHECK(std::memcmp(&state, before.data(), sizeof(state)) == 0);
}

void check_success_mapping() {
  const auto entity = make_entity();
  CHECK(Worr_SnapshotEntityValidateV2(&entity, kMaxEntities));

  entity_state_t state{};
  std::memset(&state, 0xa5, sizeof(state));
  CHECK(CG_CanonicalEntityToRenderStateV1(&entity, kLimits, &state) ==
        CG_CANONICAL_ENTITY_ADAPTER_OK);
  check_full_mapping(entity, state);
}

void check_optional_components() {
  auto entity = make_entity();
  entity.component_mask &=
      ~(WORR_SNAPSHOT_ENTITY_OWNER | WORR_SNAPSHOT_ENTITY_INSTANCE);
  entity.owner.index = WORR_EVENT_NO_ENTITY;
  entity.owner.generation = 0u;
  entity.instance_bits = 0u;
  CHECK(Worr_SnapshotEntityValidateV2(&entity, kMaxEntities));

  entity_state_t state{};
  CHECK(CG_CanonicalEntityToRenderStateV1(&entity, kLimits, &state) ==
        CG_CANONICAL_ENTITY_ADAPTER_OK);
  CHECK(state.owner == 0);
  CHECK(state.instance_bits == 0u);
  CHECK(state.event == 0u);
}

void clear_component_payload(worr_snapshot_entity_v2 &entity,
                             std::uint64_t component) {
  entity.component_mask &= ~component;
  switch (component) {
  case WORR_SNAPSHOT_ENTITY_INTERPOLATION:
    std::memset(entity.old_origin, 0, sizeof(entity.old_origin));
    entity.old_frame = 0;
    break;
  case WORR_SNAPSHOT_ENTITY_MODELS:
    std::memset(entity.model_index, 0, sizeof(entity.model_index));
    break;
  case WORR_SNAPSHOT_ENTITY_ANIMATION:
    entity.frame = 0;
    break;
  case WORR_SNAPSHOT_ENTITY_APPEARANCE:
    entity.skin = 0;
    entity.alpha = 0.0f;
    entity.scale = 0.0f;
    break;
  case WORR_SNAPSHOT_ENTITY_EFFECTS:
    entity.effects = 0;
    entity.renderfx = 0;
    break;
  case WORR_SNAPSHOT_ENTITY_COLLISION:
    entity.solid = 0;
    break;
  case WORR_SNAPSHOT_ENTITY_LOOP_SOUND:
    entity.sound = 0;
    entity.loop_volume = 0.0f;
    entity.loop_attenuation = 0.0f;
    break;
  default:
    CHECK(false);
  }
}

void check_component_and_shape_rejections() {
  constexpr std::array<std::uint64_t, 7> optional_in_canonical_but_required{
      WORR_SNAPSHOT_ENTITY_INTERPOLATION, WORR_SNAPSHOT_ENTITY_MODELS,
      WORR_SNAPSHOT_ENTITY_ANIMATION,     WORR_SNAPSHOT_ENTITY_APPEARANCE,
      WORR_SNAPSHOT_ENTITY_EFFECTS,       WORR_SNAPSHOT_ENTITY_COLLISION,
      WORR_SNAPSHOT_ENTITY_LOOP_SOUND};
  for (std::uint64_t component : optional_in_canonical_but_required) {
    auto missing = make_entity();
    clear_component_payload(missing, component);
    CHECK(Worr_SnapshotEntityValidateV2(&missing, kMaxEntities));
    expect_transactional_failure(
        missing, kLimits,
        CG_CANONICAL_ENTITY_ADAPTER_MISSING_RENDER_COMPONENTS);
  }

  auto missing_transform = make_entity();
  missing_transform.component_mask &= ~WORR_SNAPSHOT_ENTITY_TRANSFORM;
  expect_transactional_failure(missing_transform, kLimits,
                               CG_CANONICAL_ENTITY_ADAPTER_INVALID_ENTITY);

  auto invalid_schema = make_entity();
  ++invalid_schema.schema_version;
  expect_transactional_failure(invalid_schema, kLimits,
                               CG_CANONICAL_ENTITY_ADAPTER_INVALID_ENTITY);

  const auto entity = make_entity();
  expect_transactional_failure(entity, {1u, kMaxModels, kMaxSounds},
                               CG_CANONICAL_ENTITY_ADAPTER_INVALID_LIMITS);
  expect_transactional_failure(entity, {kMaxEntities, 0u, kMaxSounds},
                               CG_CANONICAL_ENTITY_ADAPTER_INVALID_LIMITS);
  expect_transactional_failure(entity, {kMaxEntities, kMaxModels, 0u},
                               CG_CANONICAL_ENTITY_ADAPTER_INVALID_LIMITS);

  entity_state_t state{};
  std::memset(&state, 0xa5, sizeof(state));
  const entity_state_t before = state;
  CHECK(CG_CanonicalEntityToRenderStateV1(nullptr, kLimits, &state) ==
        CG_CANONICAL_ENTITY_ADAPTER_INVALID_ARGUMENT);
  CHECK(std::memcmp(&state, &before, sizeof(state)) == 0);
  CHECK(CG_CanonicalEntityToRenderStateV1(&entity, kLimits, nullptr) ==
        CG_CANONICAL_ENTITY_ADAPTER_INVALID_ARGUMENT);
}

void check_generation_rejections() {
  auto entity = make_entity();
  entity.generation.identity.index = 0u;
  expect_transactional_failure(entity, kLimits,
                               CG_CANONICAL_ENTITY_ADAPTER_ENTITY_OUT_OF_RANGE);

  entity = make_entity();
  entity.generation.identity.index = kMaxEntities;
  expect_transactional_failure(entity, kLimits,
                               CG_CANONICAL_ENTITY_ADAPTER_ENTITY_OUT_OF_RANGE);

  entity = make_entity();
  entity.generation.identity.generation = 0u;
  expect_transactional_failure(entity, kLimits,
                               CG_CANONICAL_ENTITY_ADAPTER_INVALID_ENTITY);

  entity = make_entity();
  entity.generation.provenance_flags = 0u;
  expect_transactional_failure(entity, kLimits,
                               CG_CANONICAL_ENTITY_ADAPTER_INVALID_ENTITY);

  entity = make_entity();
  entity.generation.identity.index =
      static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) + 1u;
  expect_transactional_failure(entity, {UINT32_MAX, kMaxModels, kMaxSounds},
                               CG_CANONICAL_ENTITY_ADAPTER_UNREPRESENTABLE);
}

void check_resource_rejections() {
  for (std::size_t i = 0; i < 4u; ++i) {
    auto entity = make_entity();
    entity.model_index[i] = kMaxModels;
    expect_transactional_failure(
        entity, kLimits, CG_CANONICAL_ENTITY_ADAPTER_MODEL_OUT_OF_RANGE);
  }

  auto entity = make_entity();
  entity.sound = kMaxSounds;
  expect_transactional_failure(entity, kLimits,
                               CG_CANONICAL_ENTITY_ADAPTER_SOUND_OUT_OF_RANGE);
}

void check_owner_rejections() {
  auto entity = make_entity();
  entity.owner.index = 0u;
  CHECK(Worr_SnapshotEntityValidateV2(&entity, kMaxEntities));
  expect_transactional_failure(entity, kLimits,
                               CG_CANONICAL_ENTITY_ADAPTER_UNREPRESENTABLE);

  entity = make_entity();
  entity.owner.index = kMaxEntities;
  expect_transactional_failure(entity, kLimits,
                               CG_CANONICAL_ENTITY_ADAPTER_INVALID_ENTITY);

  entity = make_entity();
  entity.owner.index =
      static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) + 1u;
  expect_transactional_failure(entity, {UINT32_MAX, kMaxModels, kMaxSounds},
                               CG_CANONICAL_ENTITY_ADAPTER_UNREPRESENTABLE);
}

} // namespace

int main() {
  check_success_mapping();
  check_optional_components();
  check_component_and_shape_rejections();
  check_generation_rejections();
  check_resource_rejections();
  check_owner_rejections();
  return EXIT_SUCCESS;
}
