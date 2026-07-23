# FR-10-T10 Bounded Rewind Acceptance Closure

Date: 2026-07-19

Project task: `FR-10-T10`

## Outcome

`FR-10-T10` is complete. Its dependencies `FR-10-T02`, `FR-10-T06`, and
`FR-10-T09` are complete, and direct evidence now covers the last published
gap: bounded maximum-capacity memory, query-count, allocation, and server-frame
CPU behavior. Existing command-time mapping, player/mover history, lifecycle,
immutable scene, transformed brush collision, observability, and live
historical-mover gates remain part of the task decision.

This closure does not enable historical hits by default, complete every
projectile/melee/radius interaction, replace the T14 concurrent-client stress
and soak matrix, or satisfy release/platform promotion. Those obligations stay
with `FR-10-T11/T12/T14/T15` as assigned by the roadmap.

## Definition-of-Done evidence

1. **Bounded collision-relevant history.** Production owns 32 player tracks of
   512 poses and 64 live `SOLID_BSP` mover tracks of 64 poses. Records carry
   generation-qualified identity, map epoch, server tick/time, lifecycle,
   origin/angles/bounds/solid/clip/collision asset, discontinuity flags, and
   player-to-mover relative provenance. Existing capture, rider, rotated-brush,
   sealed-scene, and real-command mover-occlusion gates exercise those owners.
2. **Authenticated deterministic time selection.** The live command scope maps
   retained server snapshot time rather than trusting client-authored time or
   reconstructing the current interval. The 40-case matrix runs each of eight
   production policy tags at 0/50/100/200 ms and covers cap, stale, future,
   history miss, teleport, death/respawn, slot reuse, and disable outcomes. Its
   120-invocation post-optimization run has zero semantic mismatch and zero
   authoritative mutation.
3. **Non-mutating historical scenes.** Each accepted command builds a bounded,
   sealed value scene from generation-matched player and mover poses. Player
   bounds and transformed immutable BSP assets are traced without moving,
   unlinking, relinking, or restoring an authoritative edict. Any missing or
   invalid candidate abandons the historical scene for a complete current-
   world query. The load fixture hashes live entity and every production
   history/scene owner before and after all work; all five processes report
   `authority_unchanged=1`.
4. **Observable deterministic failures.** Requested/mapped/applied times,
   policy and query reasons, path/outcome/fallback, scene/candidate/hit fields,
   duration, and collision-authority guard remain in the bounded observation
   journal. The exact 200 ms load query and all 96 candidates produce the same
   scene workload hash `4973674365086484083` in every fresh process.
5. **Memory, allocation, query, and CPU budgets.** The production fixed owner is
   4,370,192 bytes. The independent maximum-capacity fixture is 3,580,384
   bytes, so the deliberately stronger combined bound is 7,950,576/8,388,608
   bytes. Each run fills every ring, performs 32 warmups plus 256 individually
   timed complete server-frame workloads, and records exactly 27,648 appends,
   queries, and bounded ring overwrites. Each measured frame contains all 32
   player and 64 mover captures, 96 exact 200 ms queries, 96 scene additions,
   and one seal. There is no batching or frame averaging, heap-backed storage,
   capacity overflow, rejected append, unsealed scene, or live-authority
   mutation.

## Production query optimization

The first honest budget runs exposed a real hot path rather than a fixture
limit. Full histories used a runtime integer division for every chronological
ring access, and the query walked from the oldest record even though bounded
rewind normally selects a recent record. The accepted common-core change:

- replaces modulo indexing with exact one-wrap arithmetic under the already
  validated `head < capacity` and `index < count <= capacity` invariants;
- validates the physical ring cursor and overflow-safe tail arithmetic once,
  then uses that validated layout throughout the ordered query;
- uses the validated `head == 0` invariant for non-full append placement;
- walks queries newest-to-oldest, preserving the newest duplicate exact sample,
  nearest interpolation bracket, and fail-closed map/generation boundary; and
- keeps internal pose and history-boundary checks inline, hoists invariant
  teleport-distance work, and caches repeated entity/mover identity tests while
  retaining the public `Worr_RewindPoseValidateV1` ABI;
- avoids a duplicate pose hash for a newly constructed scene candidate while
  continuing to validate and hash every pre-existing scene slot; and
- gives the server-owned builder an O(1) append seam through
  `Worr_RewindSceneAddOwnedResultV1`. That seam validates the scene envelope,
  new result, aliasing, capacity, and ordering without rescanning prior slots.
  It cannot publish a scene: the mandatory `Worr_RewindSceneSealV1` transaction
  still deeply validates every candidate, mover, and stored hash before setting
  the sealed flag. The legacy public add path retains its complete validation.

Core tests cover duplicate pause samples, generation/discontinuity behavior,
wrapped exact/interpolated/too-old/future queries, hostile layouts, and queries
on both sides of a map-epoch transition. They also prove that legacy and owned
builders seal byte-identical slots and hashes, while corruption inserted before
the owned seal rejects transactionally and remains unsealed. The checked-in
40-case matrix preserves normalized forward/reverse semantic SHA-256
`3aa4c5085355c40dfd9a0a716930570829cda6f2885bb7e36335a87be7cf6931`.
An independent fixed-seed differential comparison executes 16,384 queries
against the forward and reverse cores; both produce digest
`11bdd1d63c88fd99`.

## Parent-level acceptance gate

`networking-fr10-t10-acceptance` launches only the staged dedicated server and
publishes `.tmp/networking/fr10_t10_acceptance.json` with schema
`worr.networking.rewind-budget-runtime.v1`. Five fresh processes report:

| Run | p50 (ns) | p95 (ns) | p99 (ns) |
| ---: | ---: | ---: | ---: |
| 1 | 843,800 | 1,075,300 | 1,288,500 |
| 2 | 657,500 | 730,100 | 794,000 |
| 3 | 653,800 | 757,300 | 833,800 |
| 4 | 655,100 | 701,300 | 767,500 |
| 5 | 656,200 | 677,600 | 762,000 |

Worst p95 is **1,075,300 ns**, 591,300 ns below the 1,666,600 ns limit, leaving
35.5% headroom within the budget (10% of WORR's minimum 60 Hz authoritative-
frame interval). The evidence binds the staged dedicated executable SHA-256
`2cb54523fd2ec1a1533fe787ac6d9a23580d0bf01354a6fbef61e86a239fb54f`
and sgame module SHA-256
`ad709944f4ec4c03236266b7d78e359c89124c970be66dce8201ba1ca904aad2`.

Focused commands:

```powershell
meson compile -C builddir-win rewind_core_test rewind_acceptance_probe sgame_x86_64
builddir-win\rewind_core_test.exe
python -m unittest tools.networking.test_lag_compensation_t10_budget_contract tools.networking.test_run_rewind_budget_runtime_gate
meson compile -C builddir-win networking-fr10-t10-acceptance
```

The runtime command uses the dedicated binary, no client process, no input
initialization, and no visible window. The final `.install/` refresh and stage
validation passed after the common-core and sgame build.

## Accounting

Closing exactly `FR-10-T10` moves the strategic roadmap from 77/190 to
**78/190 complete (41.1%)**, with 112 tasks open. FR-10 moves from 6/16 to
**7/16 complete (43.75%)**, with 9 tasks open. `q2proto/` remains unchanged.
