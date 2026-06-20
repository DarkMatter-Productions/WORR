# Q3A BotLib Public Cvar Docs Round

Date: 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T07`, `FR-04-T11`, `FR-04-T14`, `FR-04-T15`, `FR-04-T16`, `DV-07-T06`

## Summary

This docs-only worker lane updates the public/user-facing bot documentation for
the completed item-timer fairness helper and available-reference map validation
work.

No source, tools, q2proto, plan, roadmap, or bot profile assets were edited.
`docs-dev/plans/q3a-botlib-aas-port.md` and
`docs-dev/proposals/swot-feature-development-roadmap-2026-02-27.md` remain for
the main thread to reconcile.

## Changed Paths

- `docs-user/bots.md`
- `docs-user/bot-map-readiness.md`
- `docs-dev/q3a-botlib-public-cvar-docs-round-2026-06-18.md`

## User-Facing Scope

`docs-user/bots.md` now documents the practical item-timer cvars:

- `sg_bot_allow_item_timers`
- `sg_bot_item_timer_fuzz_ms`

The guide explains the default behavior, how to disable bot item timing
knowledge, how to keep timing enabled with stable fuzz, and that these controls
only affect bot decisions rather than server item respawn rules.

`docs-user/bot-map-readiness.md` now explains available-reference validation in
operator language. It calls out selected map IDs, runtime-ready map IDs, omitted
or missing maps, and the current local result that `mm-rage` is the available
runtime-ready reference map while broader coverage still needs staged data.

## Source Context

This round was based on:

- `docs-dev/plans/q3a-botlib-aas-port.md`
- `docs-dev/q3a-botlib-item-timer-fairness-2026-06-18.md`
- `docs-dev/q2aas-reference-map-validation-expansion-2026-06-18.md`

The item-timer documentation maps to `FR-04-T03`, `FR-04-T07`, and
`FR-04-T15`. The available-reference validation documentation maps to
`FR-04-T11`, `FR-04-T14`, and `FR-04-T16`. This implementation note also keeps
the documentation/provenance lane tied to `DV-07-T06`.

## Validation

Validation was limited to documentation checks because this lane intentionally
does not change runtime behavior.

Commands run:

```powershell
git diff --check -- docs-user/bots.md docs-user/bot-map-readiness.md docs-dev/q3a-botlib-public-cvar-docs-round-2026-06-18.md
rg -n "sg_bot_allow_item_timers|sg_bot_item_timer_fuzz_ms|Available Reference Validation|selected map IDs|runtime-ready map IDs|FR-04-T03|FR-04-T11" docs-user/bots.md docs-user/bot-map-readiness.md docs-dev/q3a-botlib-public-cvar-docs-round-2026-06-18.md
$files = @('docs-user/bots.md', 'docs-user/bot-map-readiness.md', 'docs-dev/q3a-botlib-public-cvar-docs-round-2026-06-18.md'); $matches = Select-String -LiteralPath $files -Pattern '[ \t]$'; if ($matches) { $matches; exit 1 }
```

Results:

- Diff whitespace check exited `0`.
- Targeted text search found the new public cvar coverage, available-reference
  section, and task IDs.
- Direct trailing-whitespace check exited `0`.
