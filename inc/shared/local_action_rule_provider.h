/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "shared/local_action_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FR-10-T08 ratified predictable-state provider.
 *
 * The 22-weapon catalog/profile/semantics tables describe the legacy weapons
 * exactly but hold no authority (every entry carries a non-zero v2 blocker
 * mask, so none is representable by the local-action v2 core wholesale).  This
 * provider ratifies the subset of *local* state that is nonetheless
 * client-predictable and derives the deterministic v2 weapon rule for it,
 * entirely from the frozen tables.  Correctness does not depend on the derived
 * rule being bit-exact with the legacy driver: predicted state is always
 * reconciled against server authority, which wins, so an imperfect rule costs
 * correction frequency, never gameplay correctness.
 *
 * Predictable local state: weapon phase/animation timing, the muzzle/fire
 * presentation event, and a fixed per-shot ammo debit.  Not predictable and
 * left fully server-authoritative: projectile/entity lifecycle, hit
 * determination, spread/damage rolls, conditional ammo, throw/windup
 * resolution, and anything reading external client state.
 */

#define WORR_LOCAL_ACTION_RULE_PROVIDER_ABI_VERSION 1u
#define WORR_LOCAL_ACTION_RULE_PROVIDER_MODEL_REVISION 1u

/*
 * Legacy weapon-animation frame cadence in milliseconds.  This is the single
 * timing input the derivation cannot take from the frozen tables (they store
 * animation frame indices, not durations).  Its exact value is the one
 * runtime-tuned parameter of this provider; a wrong value produces phase-timer
 * corrections that authority immediately resolves, not desynchronisation.
 */
#define WORR_LOCAL_ACTION_RULE_PROVIDER_FRAME_MS 100u

/*
 * True iff the weapon's local phase/presentation and fixed ammo debit are
 * client-predictable: a single-press GENERIC-driver weapon whose v2 blocker
 * mask excludes every input that makes the local animation timeline
 * non-deterministic on the client (held/spin repeat, hold-release, staged
 * fire, mode override, external client state) and whose per-shot ammo debit is
 * fixed (semantics min == max).  Returns false for an invalid catalog id.
 */
bool Worr_LocalActionRuleProviderPredictableV1(uint32_t catalog_id);

/*
 * Builds the deterministic v2 weapon rule for a predictable weapon, derived
 * from the frozen catalog/profile/semantics tables plus the frame cadence.
 * Fails (returns false) for any non-predictable weapon so the caller keeps it
 * fully authoritative.  `rule_out` must be entirely zero on entry; every
 * failure leaves it byte-identical.  The produced rule always validates under
 * Worr_LocalActionWeaponRuleValidateV2.
 */
bool Worr_LocalActionRuleProviderBuildRuleV1(
    uint32_t catalog_id, worr_local_action_weapon_rule_v2 *rule_out);

/*
 * Adapts a decoded command's attack/switch intent into the v2 intent ABI.
 * `requested_weapon_id`/`requested_ammo_units` are ignored unless a switch is
 * requested.  `intent_out` must be entirely zero on entry; every failure
 * leaves it byte-identical.  The produced intent always validates under
 * Worr_LocalActionIntentValidateV2.
 */
bool Worr_LocalActionRuleProviderBuildIntentV1(
    bool attack_held, bool switch_requested, uint32_t requested_weapon_id,
    uint32_t requested_ammo_units, worr_local_action_intent_v2 *intent_out);

/*
 * Ordered FNV digest over every predictable weapon's derived rule.  Freezes
 * the provider's derivation so cross-endpoint drift is detectable.
 * `digest_out` must point to zero and is untouched on failure.
 */
bool Worr_LocalActionRuleProviderDigestV1(uint64_t *digest_out);

#ifdef __cplusplus
}
#endif
