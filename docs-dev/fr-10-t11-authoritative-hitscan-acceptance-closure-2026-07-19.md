# FR-10-T11 Authoritative Hitscan Acceptance Closure

Date: 2026-07-19

Project task: `FR-10-T11`

## Outcome

`FR-10-T11` is complete. Its sole dependency, `FR-10-T10`, is complete under
the companion bounded-rewind closure. One fail-closed parent gate now joins the
common deterministic policy matrix to real command, weapon, collision, damage,
mover, spectator, spawn-protection, cap, disable, and fallback execution.

Historical collision remains default-off. This closure does not enable the
feature by default and does not absorb projectile/melee/radius interaction work
from `FR-10-T12`, demo/spectator recording work from `FR-10-T13`, simultaneous
1/8/16/32-client stress and soak from `FR-10-T14`, or release promotion from
`FR-10-T15`.

## Definition-of-Done evidence

1. **One authoritative policy per supported weapon.** Machinegun, Chaingun,
   Shotgun, Super Shotgun, Railgun, Disruptor convergence, Plasma Beam, and
   Thunderbolt use policy IDs 1 through 8. The 40-case matrix exercises each
   policy at 0, 50, 100, and 200 ms and records requested/applied time, policy
   reason, path, outcome, fallback, query result, and the authority guard. The
   live gate reaches each normal production weapon callback through an admitted
   client command and proves exact damage of 8, 18, 48, 120, 80, 45, 8, and 8.
2. **The server owns hit and time authority.** The live runner supplies only
   ordinary client input and transport acknowledgement state. Sgame obtains the
   authenticated command scope, maps the retained server snapshot time, builds
   the sealed scene, performs the weapon trace, and applies current-authority
   damage. No fixture API accepts a client target, hit result, or arbitrary
   rewind timestamp. A rejected canonical command cannot downgrade to the
   legacy estimate.
3. **Shooter, target, spectator, mover, and lifecycle semantics.** Eleven live
   modes run three times each. The ordinary policy modes prove the real shooter
   and target path. The mover mode requires a clear substituted current
   baseline, a generation-matched sealed historical brush hit, unchanged live
   collision authority, and zero target damage. A three-client mode proves the
   third client remains on the spectator team before and after firing while
   the production scene retains exactly two playing and eligible players. The
   spawn-protection mode still records a positive-age canonical historical
   Railgun hit, path 1/outcome 1/no fallback, and unchanged geometry, but normal
   current-authority damage resolves to the exact accepted zero-damage range.
   The matrix separately proves teleport, death/respawn, slot-reuse, history-
   miss, stale, and future-command boundaries without authoritative mutation.
   The headless fixture assigns explicit byte-sized protocol identities to the
   shooter, target, and spectator (`qport` 101, 102, and 103). The parent
   rejects missing, duplicate, zero, out-of-range, repeated, or post-connect
   identities, preventing same-millisecond client startup from aliasing two
   independently admitted clients.
4. **Fairness caps, disable, and fallback.** The server default is 200 ms and
   the hard limit is 250 ms. The matrix proves exact cap selection and a fully
   disabled/opt-out row with no policy or query evaluation. The dedicated
   fallback fixture proves an invalid current acknowledgement produces a
   current-world miss with zero damage, while near, in-budget, and over-budget
   legacy mappings produce three bounded historical hits and exact 30 total
   damage; the over-budget row uses the cap. Public guidance documents the
   master opt-out, zero-window disable, spectator/lifecycle rules, server
   authority, and fail-closed current-world behavior.
5. **Bounded abuse and repeat floor.** Six hostile/discontinuity matrix classes
   and invalid-authority, spectator, and spawn-protection live cases are
   mandatory. The parent runs 120 deterministic matrix invocations and 36
   fresh, isolated game-process live repetitions with fixed timeouts. The
   canonical Python runner executes in the parent process so Windows cannot
   orphan or prematurely terminate a nested runner/job hierarchy; each repeat
   still launches a new dedicated server and two or three hidden clients.
   Every repeated semantic projection is identical, every authority guard is
   unchanged, and all 12 parent decisions pass. The companion T10 gate owns
   maximum-capacity query, memory, allocation, and server-frame CPU proof;
   broader concurrent load and soak remain explicitly assigned to T14.

## Parent-level acceptance gate

`networking-fr10-t11-acceptance` performs one bounded decision:

- runs the checked-in 40-case, eight-policy common matrix three times (120
  invocations) and rejects any semantic mismatch or authoritative mutation;
- runs all eight production weapon policies plus historical-mover occlusion,
  spectator exclusion, and spawn protection three times each (33 live runs);
- runs the invalid-authority and near/in-budget/capped fallback fixture three
  times; and
- binds the manifest, matrix, runners, rewind probe, staged client, dedicated
  server, sgame module, and packaged assets by SHA-256 and rejects any artifact
  change while the live gate is running.

The final evidence is
`.tmp/networking/fr10_t11_acceptance.json` with schema
`worr.networking.fr10-t11-acceptance-evidence.v1`. Its semantic SHA-256 is
`aa8aba98e1bc4e514badf7aed8a04e7e026afa2bc4f26d333d022cb9b5364bc1`.
The matrix projection SHA-256 is
`1e0fcb57a12402d0532d749349c21d2a70d6a1e1fb8c8af8cbe1ee04bbd4eca8`;
the fallback projection SHA-256 is
`fbc403c84596abd1ac3bb2d335ffff72d501cd54894036272179196a2d636ec1`.
The final 2026-07-20 run required no transient process retries and bound these
runtime artifacts:

- canonical runner:
  `128761f477374df833db25552904cb63d3716c0c028b968d19fcc78346b9ea34`;
- client executable:
  `72d76b9d72879ce265ef01f66ea855f84359758873d220d16c9023d487f7ffcb`;
- dedicated executable:
  `2cb54523fd2ec1a1533fe787ac6d9a23580d0bf01354a6fbef61e86a239fb54f`;
- sgame module:
  `ad709944f4ec4c03236266b7d78e359c89124c970be66dce8201ba1ca904aad2`;
- rewind probe:
  `ea6bf1f1e0f4a1a9999a2193ee5313436a5f945ce9bb77cb41917d8e5f0308ff`;
  and
- the refreshed 601-file `pak0.pkz`:
  `2dc3e6c016e196c6a171854b0d7210c8f7002da3c33e0c97c4491f5d0e166217`.

Focused commands:

```powershell
python -m unittest tools.networking.test_run_fr10_t11_acceptance tools.networking.test_lag_compensation_t11_acceptance_contract tools.networking.test_run_canonical_rail_damage_runtime_gate
meson compile -C builddir-win networking-fr10-t11-acceptance
```

All client processes are explicit hidden, input-disabled, no-grab, silent
headless launches. Server-only fallback work uses the dedicated binary. No
interactive client or mouse/input initialization is part of the gate.

## Final verification

On 2026-07-20:

- the focused T11 Python contracts passed 76/76;
- `networking-fr10-t11-acceptance` passed all 40 matrix cases (120
  invocations), 11 canonical modes (33 live repetitions), and three fallback
  repetitions, with zero orchestration retries;
- the complete serial Meson networking suite passed 170/170;
- asset packaging tests passed 16/16, release unit tests passed 12/12, and the
  headless bootstrap contract passed 1/1; and
- the bounded build had no pending work, then `refresh_install.py` staged 16
  root runtime files, one runtime dependency, and 601 packaged assets and
  validated the Windows x86-64 payload.

## Accounting

This closure follows T10. Closing exactly `FR-10-T11` moves the strategic
roadmap from 78/190 to **79/190 complete (41.6%)**, with 111 tasks open. FR-10
moves from 7/16 to **8/16 complete (50.0%)**, with 8 tasks open. `q2proto/`
remains unchanged.
