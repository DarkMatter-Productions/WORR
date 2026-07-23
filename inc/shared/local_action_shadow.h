/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/local_action_catalog.h"
#include "shared/local_action_observation.h"
#include "shared/local_action_weapon_profile.h"
#include "shared/local_action_weapon_semantics.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Descriptor-complete shadow evidence for one observed legacy weapon action.
 * This is not worr_local_action_transaction_v2 and cannot predict gameplay or
 * presentation. It binds the live observation to the exact frozen facts that
 * explain why that catalog entry is still blocked from V2 promotion.
 */
#define WORR_LOCAL_ACTION_SHADOW_ABI_VERSION 1u
#define WORR_LOCAL_ACTION_SHADOW_MODEL_REVISION 1u
#define WORR_LOCAL_ACTION_SHADOW_AUTHORITY_IMPORT_V1                           \
  "WORR_LOCAL_ACTION_SHADOW_AUTHORITY_IMPORT_V1"
#define WORR_LOCAL_ACTION_SHADOW_AUTHORITY_API_VERSION 1u
/* Process-local sgame -> server receipt staging. The client evidence bound
 * includes this full published capacity rather than relying on current drain
 * cadence. */
#define WORR_LOCAL_ACTION_SHADOW_AUTHORITY_MAILBOX_CAPACITY 32u

enum {
  WORR_LOCAL_ACTION_SHADOW_DESCRIPTORS_COMPLETE = UINT32_C(1) << 0,
  WORR_LOCAL_ACTION_SHADOW_PROMOTION_SHADOW_ONLY = UINT32_C(1) << 1,
  WORR_LOCAL_ACTION_SHADOW_V2_BLOCKED = UINT32_C(1) << 2,
  WORR_LOCAL_ACTION_SHADOW_ATTACK_HELD_BEFORE = UINT32_C(1) << 3,
  WORR_LOCAL_ACTION_SHADOW_ATTACK_HELD_AFTER = UINT32_C(1) << 4,
  WORR_LOCAL_ACTION_SHADOW_ACTIVE_WEAPON_STABLE = UINT32_C(1) << 5,
  WORR_LOCAL_ACTION_SHADOW_AMMO_COMPARABLE = UINT32_C(1) << 6,
};

typedef struct worr_local_action_shadow_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t model_revision;
  uint32_t catalog_id;
  uint32_t flags;
  int32_t observed_ammo_delta;
  uint32_t reserved0;
  uint32_t reserved1;
  uint64_t catalog_digest;
  uint64_t profile_digest;
  uint64_t semantics_digest;
  uint64_t descriptor_hash;
  worr_local_action_catalog_entry_v1 catalog;
  worr_local_action_weapon_profile_v1 profile;
  worr_local_action_weapon_semantics_v1 semantics;
  worr_local_action_observation_record_v1 observation;
  uint64_t record_hash;
} worr_local_action_shadow_v1;

/* Compact, non-presentable receipt for the authenticated owner. */
typedef struct worr_local_action_shadow_authority_receipt_v1_s {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t model_revision;
  uint32_t reserved0;
  worr_command_id_v1 command_id;
  uint32_t catalog_id;
  uint32_t flags;
  uint32_t v2_blockers;
  uint32_t reserved1;
  uint64_t command_hash;
  uint64_t descriptor_hash;
  uint64_t record_hash;
} worr_local_action_shadow_authority_receipt_v1;

/* Optional, process-local sgame -> server mailbox. */
typedef struct worr_local_action_shadow_authority_import_v1_s {
  uint32_t struct_size;
  uint32_t api_version;
  bool (*PublishReceipt)(
      uint32_t client_index,
      const worr_local_action_shadow_authority_receipt_v1 *receipt);
} worr_local_action_shadow_authority_import_v1;

/* output must be entirely zero; failures leave it byte-identical. */
bool Worr_LocalActionShadowBuildV1(
    uint32_t catalog_id,
    const worr_local_action_observation_record_v1 *observation,
    worr_local_action_shadow_v1 *shadow_out);
bool Worr_LocalActionShadowValidateV1(
    const worr_local_action_shadow_v1 *shadow);
bool Worr_LocalActionShadowAuthorityReceiptBuildV1(
    const worr_local_action_shadow_v1 *shadow,
    worr_local_action_shadow_authority_receipt_v1 *receipt_out);
bool Worr_LocalActionShadowAuthorityReceiptValidateV1(
    const worr_local_action_shadow_authority_receipt_v1 *receipt);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define WORR_LOCAL_ACTION_SHADOW_STATIC_ASSERT(condition, message)             \
  static_assert((condition), message)
#else
#define WORR_LOCAL_ACTION_SHADOW_STATIC_ASSERT(condition, message)             \
  _Static_assert((condition), message)
#endif

WORR_LOCAL_ACTION_SHADOW_STATIC_ASSERT(sizeof(worr_local_action_shadow_v1) ==
                                           528,
                                       "local-action shadow v1 layout changed");
WORR_LOCAL_ACTION_SHADOW_STATIC_ASSERT(
    offsetof(worr_local_action_shadow_v1, catalog) == 64,
    "local-action shadow catalog offset changed");
WORR_LOCAL_ACTION_SHADOW_STATIC_ASSERT(
    offsetof(worr_local_action_shadow_v1, observation) == 264,
    "local-action shadow observation offset changed");
WORR_LOCAL_ACTION_SHADOW_STATIC_ASSERT(
    offsetof(worr_local_action_shadow_v1, record_hash) == 520,
    "local-action shadow hash offset changed");
WORR_LOCAL_ACTION_SHADOW_STATIC_ASSERT(
    sizeof(worr_local_action_shadow_authority_receipt_v1) == 64,
    "local-action shadow authority receipt layout changed");
WORR_LOCAL_ACTION_SHADOW_STATIC_ASSERT(
    offsetof(worr_local_action_shadow_authority_receipt_v1, command_hash) == 40,
    "local-action shadow receipt command hash offset changed");
WORR_LOCAL_ACTION_SHADOW_STATIC_ASSERT(
    sizeof(worr_local_action_shadow_authority_import_v1) ==
        sizeof(uint32_t) * 2 + sizeof(void *),
    "local-action shadow authority import layout changed");

#undef WORR_LOCAL_ACTION_SHADOW_STATIC_ASSERT
