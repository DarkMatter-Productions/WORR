# Q3A BotLib Profile Behavior Validation - 2026-06-18

Task IDs: FR-04-T01, FR-04-T07, DV-07-T04, DV-07-T06

## Scope

Worker G ownership was limited to bot profile validation and behavior metadata
tooling. This pass avoided runtime bot brain/action/objective files, q2aas
validation files, and release packaging tools while the parallel implementation
round was active.

## Changes

- Added semantic validator coverage for behavior metadata labels:
  `preferred_weapon`, `chat_personality`, `role`, and `movement_style`.
- Kept existing numeric safeguards for `reaction`, `aggression`, and
  `aim_error`, and added focused tests so the coverage is explicit.
- Accepted known value aliases for weapon preferences, team roles, and movement
  styles. Examples include `rocket launcher`/`rocket_launcher`/`rl`, role
  aliases such as `offense` and `defense`, and movement aliases such as
  `circle strafe`.
- Added hard errors for malformed behavior labels and warnings for unknown but
  syntactically safe labels. This keeps local experimentation possible while
  making packaged metadata drift visible during validation.
- Added a packaged-Q3 safeguard for `assets/botfiles/bots/*_c.c`: once a skill
  block uses any behavior tuning metadata, the validator warns if the full
  behavior family is incomplete. The expected family is reaction, aggression,
  aim error, preferred weapon, chat personality, team role, and movement style.
- Added a focused companion-family test proving the existing validator catches
  missing Q3 companion references and missing companion markers, complementing
  the existing missing-file coverage.
- Updated `docs-user/bot-profiles.md` with practical author-facing validation
  notes for behavior ranges, known labels, malformed label failures, unknown
  label warnings, and packaged Q3-style behavior family completeness.

## Validation

- `python -m pytest tools\bot_profiles\test_validate_bot_profiles.py`
  - Result: 18 passed.
- `python -m py_compile tools\bot_profiles\validate_bot_profiles.py tools\bot_profiles\test_validate_bot_profiles.py`
  - Result: passed with no output.
- `python tools\bot_profiles\validate_bot_profiles.py assets\botfiles\bots --format json`
  - Result: passed, 5 profiles, 0 errors, 0 warnings.
