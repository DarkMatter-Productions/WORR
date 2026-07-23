/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "cg_canonical_entity_adapter.hpp"

#include <bit>
#include <cstring>
#include <limits>
#include <type_traits>

namespace {

static_assert(sizeof(int) == sizeof(std::uint32_t));
static_assert(sizeof(effects_t) == sizeof(std::uint64_t));
static_assert(sizeof(renderfx_t) == sizeof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<entity_state_t>);

bool limits_valid(cg_canonical_entity_adapter_limits_v1 limits) {
  return limits.max_entities > 1u && limits.max_models != 0u &&
         limits.max_sounds != 0u;
}

bool model_indices_valid(const worr_snapshot_entity_v2 &entity,
                         std::uint32_t max_models) {
  for (std::uint16_t model_index : entity.model_index) {
    if (model_index >= max_models)
      return false;
  }
  return true;
}

} // namespace

cg_canonical_entity_adapter_result_v1
CG_CanonicalEntityToRenderStateV1(const worr_snapshot_entity_v2 *entity,
                                  cg_canonical_entity_adapter_limits_v1 limits,
                                  entity_state_t *state_out) {
  if (entity == nullptr || state_out == nullptr)
    return CG_CANONICAL_ENTITY_ADAPTER_INVALID_ARGUMENT;
  if (!limits_valid(limits))
    return CG_CANONICAL_ENTITY_ADAPTER_INVALID_LIMITS;

  const std::uint32_t entity_index = entity->generation.identity.index;
  if (entity_index == 0u || entity_index == WORR_EVENT_NO_ENTITY ||
      entity_index >= limits.max_entities) {
    return CG_CANONICAL_ENTITY_ADAPTER_ENTITY_OUT_OF_RANGE;
  }
  if (!Worr_SnapshotEntityValidateV2(entity, limits.max_entities))
    return CG_CANONICAL_ENTITY_ADAPTER_INVALID_ENTITY;
  if ((entity->component_mask &
       CG_CANONICAL_ENTITY_RENDER_REQUIRED_COMPONENTS_V1) !=
      CG_CANONICAL_ENTITY_RENDER_REQUIRED_COMPONENTS_V1) {
    return CG_CANONICAL_ENTITY_ADAPTER_MISSING_RENDER_COMPONENTS;
  }
  if (!model_indices_valid(*entity, limits.max_models))
    return CG_CANONICAL_ENTITY_ADAPTER_MODEL_OUT_OF_RANGE;
  if (entity->sound >= limits.max_sounds)
    return CG_CANONICAL_ENTITY_ADAPTER_SOUND_OUT_OF_RANGE;

  constexpr std::uint32_t max_legacy_index =
      static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
  if (entity_index > max_legacy_index) {
    return CG_CANONICAL_ENTITY_ADAPTER_UNREPRESENTABLE;
  }

  std::int32_t owner = 0;
  if ((entity->component_mask & WORR_SNAPSHOT_ENTITY_OWNER) != 0u) {
    /* Legacy zero means no owner, so a live canonical world-slot owner
     * cannot be represented without aliasing absence. */
    if (entity->owner.index == 0u || entity->owner.index > max_legacy_index) {
      return CG_CANONICAL_ENTITY_ADAPTER_UNREPRESENTABLE;
    }
    owner = static_cast<std::int32_t>(entity->owner.index);
  }

  entity_state_t candidate{};
  candidate.number = static_cast<int>(entity_index);
  std::memcpy(candidate.origin, entity->origin, sizeof(candidate.origin));
  std::memcpy(candidate.angles, entity->angles, sizeof(candidate.angles));
  std::memcpy(candidate.old_origin, entity->old_origin,
              sizeof(candidate.old_origin));
  candidate.modelindex = static_cast<int>(entity->model_index[0]);
  candidate.modelindex2 = static_cast<int>(entity->model_index[1]);
  candidate.modelindex3 = static_cast<int>(entity->model_index[2]);
  candidate.modelindex4 = static_cast<int>(entity->model_index[3]);
  candidate.frame = static_cast<int>(entity->frame);
  candidate.skinnum = std::bit_cast<int>(entity->skin);
  candidate.effects = static_cast<effects_t>(entity->effects);
  candidate.renderfx = static_cast<renderfx_t>(entity->renderfx);
  candidate.solid = std::bit_cast<int>(entity->solid);
  candidate.sound = static_cast<int>(entity->sound);
  candidate.event = 0;
  candidate.alpha = entity->alpha;
  candidate.scale = entity->scale;
  candidate.instance_bits = entity->instance_bits;
  candidate.loop_volume = entity->loop_volume;
  candidate.loop_attenuation = entity->loop_attenuation;
  candidate.owner = owner;
  candidate.old_frame = entity->old_frame;

  *state_out = candidate;
  return CG_CANONICAL_ENTITY_ADAPTER_OK;
}
