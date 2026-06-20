# Q3A BotLib Profile Behavior Depth Round - 2026-06-18

Task IDs: FR-04-T13, FR-04-T03, FR-04-T15, FR-04-T04, FR-04-T06, DV-07-T06

## Scope

This pass advanced first-party bot profile metadata only. It stayed inside the
botfile asset, profile-validator, and documentation lane, avoiding runtime
source, scenario tooling, roadmap, plan, and credit files while parallel BotLib
work remains active.

Q3A and Gladiator botfiles were used as format and density inspiration only.
The new policy values and names are WORR-authored metadata, not copied text or
ported runtime logic.

## Profile Metadata

The five first-party `assets/botfiles/bots/*_c.c` character profiles now carry
a fuller behavior-policy family in every authored skill block:

- Existing runtime-facing identity and behavior hints remain intact: reaction,
  aggression, aim error, preferred weapon, chat personality, team role, and
  movement style.
- New reaction/aim hints add `WORR_REACTION_JITTER_MS`,
  `WORR_AIM_TRACKING_NOISE`, `WORR_AIM_LEAD_SCALE`, and `WORR_COMBAT_FOV`.
- New team-policy hints add `WORR_TEAMPLAY_BIAS`, `WORR_OBJECTIVE_BIAS`, and
  `WORR_FRIENDLY_FIRE_CARE`.
- New item-policy hints add `WORR_ITEM_GREED`, `WORR_ITEM_DENIAL`,
  `WORR_POWERUP_TIMING`, and `WORR_RETREAT_HEALTH`.

Each bot uses distinct values per skill tier so later brain-owned policy can
vary reaction jitter, aim confidence, objective pressure, teammate caution, and
item interest without relying on one scripted behavior path:

- `bulwark`: high team/objective care, careful friendly fire, conservative
  retreat thresholds, and defensive denial values.
- `relay`: strongest teamplay and friendly-fire care, moderate item greed, and
  support-oriented objective pressure.
- `smoke`: lower team bias, higher pickup/powerup pressure, and rocket-friendly
  lead/noise progression.
- `vanguard`: pressure profile with strong objective, denial, and powerup
  timing as skill rises.
- `vector`: duelist profile with high FOV, improving tracking noise, and
  increasing denial/powerup timing at higher skill.

`assets/botfiles/chars.h` reserves high-range WORR extension constants for the
new fields so a future native character parser can distinguish them from the
idTech3 characteristic table.

## Validation

`tools/bot_profiles/validate_bot_profiles.py` now accepts and validates the new
metadata through plain keys and `WORR_*` aliases:

- millisecond range: `reaction_jitter_ms` is `0..2000`;
- degree ranges: `aim_tracking_noise` is `0..90`, `combat_fov` is `1..360`;
- scale range: `aim_lead_scale` is `0.0..2.0`;
- bias ranges: team/item policy values are `0.0..1.0`;
- health range: `retreat_health` is `0..200`.

The packaged-Q3 behavior-family completeness check now includes the new policy
fields. If a packaged skill block starts carrying behavior metadata, validation
expects the whole family rather than a partial mix of old and new knobs.

`tools/bot_profiles/test_validate_bot_profiles.py` adds coverage for:

- alias acceptance for the new policy fields;
- range failures across the full policy family;
- missing-field warnings mentioning the new behavior-family keys;
- real first-party profile validation, including normalized fields from the
  last authored skill block.

## User Documentation

`docs-user/bot-profiles.md` now describes the new fields in operator-facing
language, including what each hint is for, accepted ranges, Q3-style WORR
extension names, and a safe example whose skill blocks provide complete
behavior metadata.

## Verification

- `python -m pytest tools\bot_profiles\test_validate_bot_profiles.py`
  - Result: 20 passed.
- `python -m py_compile tools\bot_profiles\validate_bot_profiles.py tools\bot_profiles\test_validate_bot_profiles.py`
  - Result: passed with no output.
- `python tools\bot_profiles\validate_bot_profiles.py assets\botfiles\bots --format json`
  - Result: passed, 5 profiles, 0 errors, 0 warnings.
