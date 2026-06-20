# Q3A BotLib User Docs Round

Date: 2026-06-18

Tasks: `FR-04-T07`, `DV-07-T04`, `DV-07-T06`

## Summary

This slice adds a practical user-facing bot guide separate from the detailed
profile-format reference. The new page is aimed at server operators who need to
enable bots, add/remove them, choose basic cvars, understand installed botfiles,
and know the current map/AAS limitations.

## Implementation

- Added `docs-user/bots.md`.
- Updated `docs-user/README.md` to link the broader bot guide before the profile
  deep dive.

The guide covers:

- enabling bot support with `sg_bot_enable`
- manual bot commands
- `sg_bot_min_players` practice-server setup
- recommended bot cvars
- debug cvars
- packaged and loose `basew/botfiles` behavior
- AAS map-data expectations
- current behavior limits
- high-bot soak guidance in operator language

## Validation

Markdown sanity check only. This is user documentation and does not affect build
or runtime code.
