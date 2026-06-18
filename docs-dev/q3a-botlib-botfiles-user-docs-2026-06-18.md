# Q3A BotLib Botfiles User Docs

Date: 2026-06-18

Tasks: `FR-04-T07`, `FR-04-T13`, `DV-07-T04`, `DV-07-T06`

## Summary

This documentation slice adds operator-facing guidance for using bot profiles
once repository-owned botfiles are available in server packages. It does not add
or modify bot profile assets, source code, tools, scenarios, roadmap entries, or
credits.

## User-Facing Scope

- Added `docs-user/bot-profiles.md`.
- Linked the guide from `docs-user/README.md`.
- Added a short bot-profile pointer to `docs-user/server-quickstart.md`.

The guide covers:

- enabling bot support with `sg_bot_enable`;
- adding a named profile with `sg_bot_add [profile] [team]`;
- reloading profile files with `sg_bot_reload_profiles`;
- removing bots with `sg_bot_remove`;
- using `sg_bot_min_players` and `sg_bot_profile` for auto-filled practice
  servers;
- profile search locations and id naming, including the Q3-style `*_c.c`
  profile entry point and companion `_w/_i/_t.c` files;
- the first repository-owned seed profile ids (`vanguard`, `bulwark`, `relay`,
  `vector`) while reserving `smoke` as a validation-oriented profile;
- plain-language descriptions of supported Q3-style `CHARACTERISTIC_*` fields,
  WORR extension fields, and legacy key/value aliases;
- safe examples plus fallback wording for builds without packaged profiles.

## Notes

The user guide intentionally says that behavior fields are metadata and tuning
hints while the remaining bot-brain policy work continues. This matches the
current loader behavior: profile values are parsed, preserved, and exposed to
bot userinfo, but some policy consumers are still under active BotLib work.

The guide also calls out that builds without repository-owned profile files can
still use the commands, but `sg_bot_add <profile>` falls back to display-name
behavior when the profile id cannot be resolved.

## Validation

No build or runtime smoke was needed for this docs-only slice. Final validation
was limited to repository diff review and markdown/whitespace checks.
