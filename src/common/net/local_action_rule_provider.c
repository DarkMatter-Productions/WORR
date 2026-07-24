/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/local_action_rule_provider.h"

#include <string.h>

#include "shared/local_action_catalog.h"
#include "shared/local_action_weapon_profile.h"
#include "shared/local_action_weapon_semantics.h"

/*
 * v2 blocker bits that make a weapon's *local* animation timeline
 * non-deterministic on the client and therefore not phase-predictable.  The
 * remaining blockers (world authority, asset mapping, projectile/entity
 * lifecycle, historical query, random stream, ruleset branch) gate world
 * effects only and are compatible with predicting local phase/presentation.
 */
#define LOCAL_ACTION_RULE_PROVIDER_PHASE_BLOCKERS                              \
    (WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_HELD_REPEAT |                           \
     WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_HOLD_RELEASE |                          \
     WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_STAGED_FIRE |                           \
     WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_EXTERNAL_CLIENT_STATE |                 \
     WORR_LOCAL_ACTION_WEAPON_V2_BLOCK_MODE_OVERRIDE)

/* Stable, distinct, non-zero synthetic asset lanes derived from catalog id. */
#define LOCAL_ACTION_RULE_PROVIDER_FIRE_AUDIO_BASE   UINT32_C(0x1000)
#define LOCAL_ACTION_RULE_PROVIDER_DRY_AUDIO_BASE    UINT32_C(0x2000)
#define LOCAL_ACTION_RULE_PROVIDER_FIRE_EFFECT_BASE  UINT32_C(0x3000)
#define LOCAL_ACTION_RULE_PROVIDER_SWITCH_EFFECT_BASE UINT32_C(0x4000)

static bool bytes_are_zero(const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t index;
    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

static uint64_t fnv1a_u32(uint64_t hash, uint32_t value)
{
    unsigned shift;
    for (shift = 0; shift < 32u; shift += 8u) {
        hash ^= (uint64_t)((value >> shift) & 0xffu);
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

/*
 * A predictable weapon is a single-press GENERIC-driver weapon whose blocker
 * mask excludes every local-timeline hazard and whose per-shot ammo debit is
 * fixed.  Everything is read from the frozen tables; there is no hand table.
 */
bool Worr_LocalActionRuleProviderPredictableV1(uint32_t catalog_id)
{
    worr_local_action_catalog_entry_v1 entry;
    worr_local_action_weapon_semantics_v1 semantics;

    if (!Worr_LocalActionCatalogIdValidV1(catalog_id))
        return false;

    memset(&entry, 0, sizeof(entry));
    if (!Worr_LocalActionCatalogCopyEntryV1(catalog_id, &entry))
        return false;
    if (entry.legacy_driver != WORR_LOCAL_ACTION_CATALOG_DRIVER_GENERIC)
        return false;

    memset(&semantics, 0, sizeof(semantics));
    if (!Worr_LocalActionWeaponSemanticsCopyV1(catalog_id, &semantics))
        return false;
    if (semantics.trigger_model != WORR_LOCAL_ACTION_WEAPON_TRIGGER_SINGLE_PRESS)
        return false;
    if ((semantics.v2_blockers & LOCAL_ACTION_RULE_PROVIDER_PHASE_BLOCKERS) != 0)
        return false;
    if (semantics.nominal_ammo_debit_min != semantics.nominal_ammo_debit_max)
        return false;
    if (semantics.nominal_ammo_debit_min > WORR_LOCAL_ACTION_MAX_AMMO_UNITS)
        return false;
    return true;
}

static bool derive_phase_ms(int32_t first_frame, int32_t last_frame,
                            uint32_t *duration_ms)
{
    int64_t frames;
    int64_t ms;
    if (first_frame < 0 || last_frame < first_frame)
        return false;
    frames = (int64_t)last_frame - (int64_t)first_frame;
    ms = frames * (int64_t)WORR_LOCAL_ACTION_RULE_PROVIDER_FRAME_MS;
    if (ms < 0 || ms > (int64_t)WORR_LOCAL_ACTION_MAX_PHASE_DURATION_MS)
        return false;
    *duration_ms = (uint32_t)ms;
    return true;
}

bool Worr_LocalActionRuleProviderBuildRuleV1(
    uint32_t catalog_id, worr_local_action_weapon_rule_v2 *rule_out)
{
    worr_local_action_weapon_profile_v1 profile;
    worr_local_action_weapon_semantics_v1 semantics;
    worr_local_action_weapon_rule_v2 candidate;
    uint32_t raise_ms = 0;
    uint32_t refire_ms = 0;
    uint32_t lower_ms = 0;
    uint32_t ammo_per_shot;

    if (!rule_out || !bytes_are_zero(rule_out, sizeof(*rule_out)))
        return false;
    if (!Worr_LocalActionRuleProviderPredictableV1(catalog_id))
        return false;

    memset(&profile, 0, sizeof(profile));
    if (!Worr_LocalActionWeaponProfileCopyV1(catalog_id, &profile))
        return false;
    memset(&semantics, 0, sizeof(semantics));
    if (!Worr_LocalActionWeaponSemanticsCopyV1(catalog_id, &semantics))
        return false;

    /* GENERIC single-press weapons expose a fully ordered raise/fire/idle/
     * deactivate skeleton plus at least one muzzle fire frame. */
    if (profile.activate_last < 1 || profile.fire_last <= profile.activate_last ||
        profile.idle_last <= profile.fire_last ||
        profile.deactivate_last <= profile.idle_last ||
        profile.fire_frame_count < 1 || profile.fire_frames[0] < 1) {
        return false;
    }
    if (!derive_phase_ms(0, profile.activate_last, &raise_ms) ||
        !derive_phase_ms(profile.activate_last, profile.fire_last, &refire_ms) ||
        !derive_phase_ms(profile.idle_last, profile.deactivate_last, &lower_ms)) {
        return false;
    }
    if (raise_ms == 0 || refire_ms == 0 || lower_ms == 0)
        return false;

    ammo_per_shot = semantics.nominal_ammo_debit_min;

    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
    candidate.model_revision = WORR_LOCAL_ACTION_MODEL_REVISION;
    /* Single-press semi-automatic: never AUTOMATIC.  Audio and effect are
     * predicted and reconciled; ammo is predicted only when it is a fixed
     * debit (which the predictable gate already guaranteed). */
    candidate.flags = WORR_LOCAL_ACTION_WEAPON_PREDICT_AUDIO |
                      WORR_LOCAL_ACTION_WEAPON_PREDICT_EFFECT;
    if (ammo_per_shot != 0)
        candidate.flags |= WORR_LOCAL_ACTION_WEAPON_USES_AMMO;
    candidate.weapon_id = catalog_id;
    candidate.ammo_per_shot = ammo_per_shot;
    candidate.raise_duration_ms = raise_ms;
    candidate.lower_duration_ms = lower_ms;
    candidate.refire_duration_ms = refire_ms;
    candidate.ready_frame = (uint32_t)profile.activate_last;
    candidate.fire_frame = (uint32_t)profile.fire_frames[0];
    candidate.fire_audio_asset_id =
        LOCAL_ACTION_RULE_PROVIDER_FIRE_AUDIO_BASE + catalog_id;
    candidate.dry_audio_asset_id =
        LOCAL_ACTION_RULE_PROVIDER_DRY_AUDIO_BASE + catalog_id;
    candidate.fire_effect_id =
        LOCAL_ACTION_RULE_PROVIDER_FIRE_EFFECT_BASE + catalog_id;
    candidate.switch_effect_id =
        LOCAL_ACTION_RULE_PROVIDER_SWITCH_EFFECT_BASE + catalog_id;

    if (!Worr_LocalActionWeaponRuleValidateV2(&candidate))
        return false;
    *rule_out = candidate;
    return true;
}

bool Worr_LocalActionRuleProviderBuildIntentV1(
    bool attack_held, bool switch_requested, uint32_t requested_weapon_id,
    uint32_t requested_ammo_units, worr_local_action_intent_v2 *intent_out)
{
    worr_local_action_intent_v2 candidate;

    if (!intent_out || !bytes_are_zero(intent_out, sizeof(*intent_out)))
        return false;

    memset(&candidate, 0, sizeof(candidate));
    candidate.struct_size = sizeof(candidate);
    candidate.schema_version = WORR_LOCAL_ACTION_ABI_VERSION;
    if (attack_held)
        candidate.flags |= WORR_LOCAL_ACTION_INTENT_ATTACK_HELD;
    if (switch_requested) {
        candidate.flags |= WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST;
        candidate.requested_weapon_id = requested_weapon_id;
        candidate.requested_ammo_units = requested_ammo_units;
    }

    if (!Worr_LocalActionIntentValidateV2(&candidate))
        return false;
    *intent_out = candidate;
    return true;
}

bool Worr_LocalActionRuleProviderDigestV1(uint64_t *digest_out)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    uint32_t catalog_id;

    if (!digest_out || *digest_out != 0)
        return false;

    hash = fnv1a_u32(hash, WORR_LOCAL_ACTION_RULE_PROVIDER_ABI_VERSION);
    hash = fnv1a_u32(hash, WORR_LOCAL_ACTION_RULE_PROVIDER_MODEL_REVISION);
    hash = fnv1a_u32(hash, WORR_LOCAL_ACTION_RULE_PROVIDER_FRAME_MS);
    for (catalog_id = 1u; catalog_id <= WORR_LOCAL_ACTION_CATALOG_COUNT;
         ++catalog_id) {
        worr_local_action_weapon_rule_v2 rule;
        if (!Worr_LocalActionRuleProviderPredictableV1(catalog_id))
            continue;
        memset(&rule, 0, sizeof(rule));
        if (!Worr_LocalActionRuleProviderBuildRuleV1(catalog_id, &rule))
            return false;
        hash = fnv1a_u32(hash, rule.weapon_id);
        hash = fnv1a_u32(hash, rule.flags);
        hash = fnv1a_u32(hash, rule.ammo_per_shot);
        hash = fnv1a_u32(hash, rule.raise_duration_ms);
        hash = fnv1a_u32(hash, rule.lower_duration_ms);
        hash = fnv1a_u32(hash, rule.refire_duration_ms);
        hash = fnv1a_u32(hash, rule.ready_frame);
        hash = fnv1a_u32(hash, rule.fire_frame);
        hash = fnv1a_u32(hash, rule.fire_audio_asset_id);
        hash = fnv1a_u32(hash, rule.dry_audio_asset_id);
        hash = fnv1a_u32(hash, rule.fire_effect_id);
        hash = fnv1a_u32(hash, rule.switch_effect_id);
    }
    *digest_out = hash;
    return true;
}
