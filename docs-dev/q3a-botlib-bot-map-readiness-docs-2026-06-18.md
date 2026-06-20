# Q3A BotLib Bot Map Readiness Docs

Date: 2026-06-18

Tasks: `FR-04-T07`, `FR-04-T11`, `FR-04-T16`, `DV-07-T06`

## Summary

This docs-only slice adds a concise server-operator guide for deciding whether a
map is ready for route-driven bots. It explains the practical AAS file layout,
current q2aas-generated asset expectations, runtime log/status checks, common
failure messages, and the present `mm-rage` reference-map limitation.

No code, q2proto, Vulkan renderer paths, plan, roadmap, credits, or botfiles
were edited.

## Changed Paths

- `docs-user/bot-map-readiness.md`
- `docs-user/README.md`
- `docs-dev/q3a-botlib-bot-map-readiness-docs-2026-06-18.md`

## User-Facing Scope

The new guide covers:

- loose staged AAS under `basew/maps/<map>.aas`
- packaged AAS as `maps/<map>.aas` inside `basew/pak0.pkz`
- local staged development layout under `.install/basew/`
- checking staged files with cheap PowerShell commands
- confirming runtime load with `Bot AAS: loaded maps/<map>.aas`
- using `sg_bot_debug_aas`, `botlib_lifecycle_status`, and
  `q3a_bot_frame_command_status` fields at a practical level
- interpreting missing, invalid, and BotLib-rejected AAS messages
- explaining that q2aas-generated files must match the map being hosted
- calling out that current reference coverage is centered on `mm-rage`, while
  broader map categories are still pending

## Validation

Validation was limited to documentation checks because this lane intentionally
does not change runtime behavior:

- `rg` source/doc inspection for current AAS paths, runtime log strings, status
  command names, and nearby task IDs.
- `rg` link/text checks after editing.
- `git diff --check` on the owned documentation paths.
