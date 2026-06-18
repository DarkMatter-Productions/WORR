# Q3A BotLib Worker I Status Refresh - 2026-06-18

Tasks: `FR-04-T03`, `FR-04-T04`, `FR-04-T15`, `DV-03-T05`, `DV-07-T06`

## Scope

This documentation-only pass updates the canonical Q3A BotLib/AAS plan and the
SWOT roadmap after the latest parallel worker lanes. It does not edit source,
tools, assets, or user-facing docs.

## Completion Snapshot

The plan now records the combat proof, weapon-switch proof, health/armor proof,
team-objective proof, and pending-scenario promotion tooling lanes as completed
helper/tooling work. The runtime scenarios remain pending until the main
integration path wires those helpers into real command, pickup, damage, route,
and game-event owners.

Current measured plan completion after this status refresh:

- Total checklist completion: 497 of 673 items complete, or 73.8%.
- Phase checklist completion: 497 of 661 phase items complete, or 75.2%.

## Newly Reflected Completed Work

- Combat proof helpers: real enemy fact building, nearest visible/shootable
  target search, combat context enrichment, and filtered bot-attributed damage
  recording for mode `20`.
- Weapon-switch proof helpers: validated pending switch requests, duplicate and
  mismatch accounting, observed completion, and terminal failure events for mode
  `21`.
- Health/armor proof helpers: deterministic low-health/no-armor setup and
  pre/post pickup snapshots that record only real resource increases for mode
  `22`.
- Team-objective proof helpers: target-source facts, deterministic enemy-flag
  selection, assignment, route-goal handoff, and entity-aware event record hooks
  for mode `23`.
- Scenario promotion tooling: raw diagnostics now merge the latest split
  status markers from frame-command, blackboard, action, objective, and
  source-counter output before applying semantic promotion checks.

## Outstanding Work

The four pending scenarios are still blocked by runtime integration rather than
missing helper APIs:

- Mode `20` needs `bot_brain.*` to consume combat enemy facts, aim/apply attack
  commands, and produce real bot-attributed damage in a visible, shootable test
  setup.
- Mode `21` needs the weapon dispatch owner to submit accepted switch intents
  to the game weapon system and observe the actual current weapon item.
- Mode `22` needs the game pickup owner to wrap real health/armor pickups with
  the snapshot/observation helpers and the smoke setup owner to seed deterministic
  low-health/low-armor state.
- Mode `23` needs brain/nav/game-event owners to call objective assignment,
  route request, route command, reach, flag pickup, and capture hooks from real
  events.

Broader outstanding work remains in Phase 4 fairness/blackboard integration,
Phase 6 full command ownership and aim/firing behavior, Phase 7 tactical roles
and match/coop behavior, and Phase 9 reference-map, CI, performance, and release
packaging validation.
