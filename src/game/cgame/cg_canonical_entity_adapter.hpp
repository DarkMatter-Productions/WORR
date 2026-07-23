/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/shared.h"
#include "shared/snapshot_abi.h"

#include <cstdint>

constexpr std::uint32_t CG_CANONICAL_ENTITY_ADAPTER_VERSION = 1u;

/*
 * These components form the lossless render-state subset.  OWNER and INSTANCE
 * are optional: their canonical absent representations become the established
 * zero values in entity_state_t.  Generation values and component metadata
 * remain canonical control data and are validated rather than copied into the
 * legacy render record.
 */
constexpr std::uint64_t CG_CANONICAL_ENTITY_RENDER_REQUIRED_COMPONENTS_V1 =
    WORR_SNAPSHOT_ENTITY_TRANSFORM | WORR_SNAPSHOT_ENTITY_INTERPOLATION |
    WORR_SNAPSHOT_ENTITY_MODELS | WORR_SNAPSHOT_ENTITY_ANIMATION |
    WORR_SNAPSHOT_ENTITY_APPEARANCE | WORR_SNAPSHOT_ENTITY_EFFECTS |
    WORR_SNAPSHOT_ENTITY_COLLISION | WORR_SNAPSHOT_ENTITY_LOOP_SOUND;

enum cg_canonical_entity_adapter_result_v1 : std::uint32_t {
  CG_CANONICAL_ENTITY_ADAPTER_OK = 0,
  CG_CANONICAL_ENTITY_ADAPTER_INVALID_ARGUMENT = 1,
  CG_CANONICAL_ENTITY_ADAPTER_INVALID_LIMITS = 2,
  CG_CANONICAL_ENTITY_ADAPTER_INVALID_ENTITY = 3,
  CG_CANONICAL_ENTITY_ADAPTER_MISSING_RENDER_COMPONENTS = 4,
  CG_CANONICAL_ENTITY_ADAPTER_ENTITY_OUT_OF_RANGE = 5,
  CG_CANONICAL_ENTITY_ADAPTER_MODEL_OUT_OF_RANGE = 6,
  CG_CANONICAL_ENTITY_ADAPTER_SOUND_OUT_OF_RANGE = 7,
  CG_CANONICAL_ENTITY_ADAPTER_UNREPRESENTABLE = 8,
};

/* Value limits are supplied by the caller; the adapter reads no cgame globals.
 */
struct cg_canonical_entity_adapter_limits_v1 {
  std::uint32_t max_entities;
  std::uint32_t max_models;
  std::uint32_t max_sounds;
};

/*
 * Produces one pointer-free render value.  All pointers are borrowed only for
 * this call.  Failure is transactional and leaves state_out byte-for-byte
 * untouched.  Entity impulse events remain on the canonical T05 event path,
 * so the resulting entity_state_t always has event == 0.
 */
cg_canonical_entity_adapter_result_v1
CG_CanonicalEntityToRenderStateV1(const worr_snapshot_entity_v2 *entity,
                                  cg_canonical_entity_adapter_limits_v1 limits,
                                  entity_state_t *state_out);
