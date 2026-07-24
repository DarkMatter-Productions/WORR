/*
 * FR-10-T08 ratified predictable-state rule provider tests: the derived
 * predictable set and every derived v2 weapon rule.
 */

#include "shared/local_action_rule_provider.h"

#include "shared/local_action_abi.h"
#include "shared/local_action_catalog.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "local_action_rule_provider_test:%d: %s\n", __LINE__,   \
              #condition);                                                     \
      return false;                                                            \
    }                                                                          \
  } while (0)

/*
 * Independently derived from the frozen semantics table: the single-press
 * GENERIC-driver weapons whose blocker mask carries none of held/spin repeat,
 * hold-release, staged fire, mode override, or external client state, and whose
 * per-shot ammo debit is fixed (nominal min == max).  Projectile weapons
 * qualify for LOCAL phase/ammo/muzzle prediction; their projectile and damage
 * remain server-authoritative.
 */
static bool is_expected_predictable(uint32_t catalog_id) {
  switch (catalog_id) {
  case WORR_LOCAL_ACTION_CATALOG_BLASTER:
  case WORR_LOCAL_ACTION_CATALOG_SHOTGUN:
  case WORR_LOCAL_ACTION_CATALOG_SUPER_SHOTGUN:
  case WORR_LOCAL_ACTION_CATALOG_GRENADE_LAUNCHER:
  case WORR_LOCAL_ACTION_CATALOG_PROX_LAUNCHER:
  case WORR_LOCAL_ACTION_CATALOG_ROCKET_LAUNCHER:
  case WORR_LOCAL_ACTION_CATALOG_RAILGUN:
  case WORR_LOCAL_ACTION_CATALOG_DISRUPTOR:
    return true;
  default:
    return false;
  }
}

static bool test_predictable_set(void) {
  uint32_t catalog_id;
  uint32_t predictable_count = 0;

  /* Invalid ids are never predictable. */
  CHECK(!Worr_LocalActionRuleProviderPredictableV1(0));
  CHECK(!Worr_LocalActionRuleProviderPredictableV1(
      WORR_LOCAL_ACTION_CATALOG_COUNT + 1u));

  for (catalog_id = 1u; catalog_id <= WORR_LOCAL_ACTION_CATALOG_COUNT;
       ++catalog_id) {
    const bool predictable =
        Worr_LocalActionRuleProviderPredictableV1(catalog_id);
    CHECK(predictable == is_expected_predictable(catalog_id));
    if (predictable)
      ++predictable_count;
  }
  CHECK(predictable_count == 8);
  return true;
}

static bool test_rules_valid(void) {
  uint32_t catalog_id;

  for (catalog_id = 1u; catalog_id <= WORR_LOCAL_ACTION_CATALOG_COUNT;
       ++catalog_id) {
    worr_local_action_weapon_rule_v2 rule;

    memset(&rule, 0, sizeof(rule));
    if (!Worr_LocalActionRuleProviderPredictableV1(catalog_id)) {
      /* Non-predictable weapons must fail closed and leave the output zero. */
      CHECK(!Worr_LocalActionRuleProviderBuildRuleV1(catalog_id, &rule));
      worr_local_action_weapon_rule_v2 zero;
      memset(&zero, 0, sizeof(zero));
      CHECK(memcmp(&rule, &zero, sizeof(rule)) == 0);
      continue;
    }
    CHECK(Worr_LocalActionRuleProviderBuildRuleV1(catalog_id, &rule));
    /* Every derived rule must satisfy the v2 core's own validator. */
    CHECK(Worr_LocalActionWeaponRuleValidateV2(&rule));
    CHECK(rule.weapon_id == catalog_id);
    CHECK(rule.refire_duration_ms != 0);
    CHECK(rule.raise_duration_ms != 0);
    CHECK(rule.lower_duration_ms != 0);
    CHECK((rule.flags & WORR_LOCAL_ACTION_WEAPON_PREDICT_AUDIO) != 0);
    CHECK((rule.flags & WORR_LOCAL_ACTION_WEAPON_PREDICT_EFFECT) != 0);
    /* Single-press semi-automatic: never AUTOMATIC. */
    CHECK((rule.flags & WORR_LOCAL_ACTION_WEAPON_AUTOMATIC) == 0);
    CHECK(rule.fire_frame != 0);

    /* The output-zero precondition is enforced: a non-zero output rejects. */
    CHECK(!Worr_LocalActionRuleProviderBuildRuleV1(catalog_id, &rule));
  }
  return true;
}

static bool test_fixed_ammo(void) {
  worr_local_action_weapon_rule_v2 rule;

  /* Blaster uses no ammo; the others debit a fixed amount. */
  memset(&rule, 0, sizeof(rule));
  CHECK(Worr_LocalActionRuleProviderBuildRuleV1(
      WORR_LOCAL_ACTION_CATALOG_BLASTER, &rule));
  CHECK((rule.flags & WORR_LOCAL_ACTION_WEAPON_USES_AMMO) == 0);
  CHECK(rule.ammo_per_shot == 0);

  memset(&rule, 0, sizeof(rule));
  CHECK(Worr_LocalActionRuleProviderBuildRuleV1(
      WORR_LOCAL_ACTION_CATALOG_SUPER_SHOTGUN, &rule));
  CHECK((rule.flags & WORR_LOCAL_ACTION_WEAPON_USES_AMMO) != 0);
  CHECK(rule.ammo_per_shot == 2);

  memset(&rule, 0, sizeof(rule));
  CHECK(Worr_LocalActionRuleProviderBuildRuleV1(
      WORR_LOCAL_ACTION_CATALOG_RAILGUN, &rule));
  CHECK((rule.flags & WORR_LOCAL_ACTION_WEAPON_USES_AMMO) != 0);
  CHECK(rule.ammo_per_shot == 1);
  return true;
}

static bool test_intent_adapter(void) {
  worr_local_action_intent_v2 intent;

  memset(&intent, 0, sizeof(intent));
  CHECK(Worr_LocalActionRuleProviderBuildIntentV1(false, false, 0, 0, &intent));
  CHECK(Worr_LocalActionIntentValidateV2(&intent));
  CHECK(intent.flags == 0);

  memset(&intent, 0, sizeof(intent));
  CHECK(Worr_LocalActionRuleProviderBuildIntentV1(true, false, 0, 0, &intent));
  CHECK(Worr_LocalActionIntentValidateV2(&intent));
  CHECK((intent.flags & WORR_LOCAL_ACTION_INTENT_ATTACK_HELD) != 0);
  CHECK((intent.flags & WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST) == 0);

  memset(&intent, 0, sizeof(intent));
  CHECK(Worr_LocalActionRuleProviderBuildIntentV1(false, true, 3, 0, &intent));
  CHECK(Worr_LocalActionIntentValidateV2(&intent));
  CHECK((intent.flags & WORR_LOCAL_ACTION_INTENT_SWITCH_REQUEST) != 0);
  CHECK(intent.requested_weapon_id == 3);

  /* Non-zero output rejects (byte-atomic precondition). */
  CHECK(!Worr_LocalActionRuleProviderBuildIntentV1(true, false, 0, 0, &intent));
  return true;
}

static bool test_digest_stable(void) {
  uint64_t first = 0;
  uint64_t second = 0;

  CHECK(Worr_LocalActionRuleProviderDigestV1(&first));
  CHECK(first != 0);
  CHECK(Worr_LocalActionRuleProviderDigestV1(&second));
  CHECK(second == first);
  /* Non-zero output rejects. */
  CHECK(!Worr_LocalActionRuleProviderDigestV1(&first));
  return true;
}

int main(void) {
  if (!test_predictable_set() || !test_rules_valid() || !test_fixed_ammo() ||
      !test_intent_adapter() || !test_digest_stable()) {
    return 1;
  }
  puts("local_action_rule_provider_test: all checks passed");
  return 0;
}
