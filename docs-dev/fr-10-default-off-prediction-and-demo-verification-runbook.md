# FR-10 default-off prediction & native-demo verification runbook

Status: **verification procedure**, not a claim of results. Every feature below
is default-off and ships cold. This document specifies exactly how a human
running an actual game/dedicated-server build closes the behavioral evidence
gates that no unit test can reach. Record outcomes against the numbered pass
criteria; do not mark a gate closed without the captured evidence named here.

Scope: the FR-10-T08 client weapon prediction carriage and the FR-10-T13 native
demo record/playback cores that were built and unit-verified in this line of
work. The shared/testable seams already pass in the suite
(`meson test -C builddir-win`, suite `networking`); what remains is running-game
behavior.

---

## 0. Preconditions

- A `debugoptimized` or release build of `worr` (client) and `worr_ded`
  (dedicated) from the current `main`.
- Asserts on (`debugoptimized`) for the first pass so any invariant trip aborts
  loudly.
- Two machines or two client instances for the T08 two-party test (LAN is fine;
  a loopback listen server also works but hides real RTT — prefer a real hop).

All cvars below default to `0`/off. Nothing in this runbook changes unless you
set them, and setting them touches only the native side-channel — the legacy
wire and every frozen ABI are unaffected.

---

## 1. Default-off guarantee (do this FIRST, both features)

Goal: prove that with the feature cvars at their defaults the build is
behaviorally identical to before the FR-10-T08/T13 work — no extra bytes, no
extra events, no gameplay change.

1. Confirm defaults after a clean start (dedicated console):
   - `sv_worr_weapon_predict` → `0`
   - (client) `cg_predict_weapon` → `0`
2. Play a normal legacy session (legacy Q2 client against `worr_ded`, and a
   `worr` client against a stock server). Fire every weapon, switch weapons,
   run out of ammo.
3. Record a legacy `.dm2` demo and play it back.

**Pass criteria (P1):**
- P1.1 No `kind-14` / `AUTHORITY_RECEIPT` weapon-seed traffic appears on any wire
  (verify with the native shadow diagnostics / a packet capture: the seed
  mailbox is never published to, so `SV_WeaponPredictSeedTake` never fires).
- P1.2 Legacy `.dm2` record + playback is bit-for-bit unchanged vs a pre-FR-10
  build.
- P1.3 No new assert trips; weapon timing/ammo behavior matches stock.

If any of P1 fails, stop — a default-off path is leaking and must be fixed
before enabling anything.

---

## 2. FR-10-T08 — client weapon prediction (behavioral gate)

The predictor, producer, seed transport, and reconciliation are built and
unit-proven to agree hash-for-hash. This gate confirms they agree **in a live
session** for the client-predictable weapon set.

The predictable set is exactly the 8 weapons that resolve to a single-press
GENERIC driver with `(v2_blockers & 0xAB0) == 0` and a fixed ammo debit
(`nominal_ammo_debit_min == max`). Held/staged/mode-override weapons are
deliberately excluded and stay fully server-authoritative.

### Enable

- Server: `sv_worr_weapon_predict 1`
- Client: `cg_predict_weapon 1`
- Optionally raise the native shadow diagnostics verbosity to surface the
  reconciliation correction counter.

### Scenario

1. Two clients connect to `worr_ded` over a real (non-loopback) hop.
2. For **each** of the 8 predictable weapons, on the shooting client:
   - single-fire repeatedly at a steady cadence,
   - fire to ammo exhaustion (dry-fire), then pick up ammo and refire,
   - switch to the weapon fresh (READY-boundary seed) and fire immediately,
   - fire while strafing/turning (movement + attack same command).
3. Repeat step 2 under induced latency and light loss (use the existing
   impairment tooling) to exercise reconciliation, not just the happy path.
4. Fire a **non-predictable** weapon (e.g. a held/charged one) and confirm it
   stays server-driven (no client prediction, no seed emitted for it).

### What to capture

- The reconciliation **correction count** per weapon over the run.
- Any `HardResync` classification events.
- Muzzle/fire presentation vs. the authoritative frame (visual: no double
  muzzle, no rubber-banding of the view weapon).

**Pass criteria (P2):**
- P2.1 For every predictable weapon, the client predictor and server producer
  agree: the correction count is **0** across the steady-cadence runs (the seed
  reconstructs the same state the server produced). A non-zero count under clean
  network is a defect — capture the command/seed that diverged.
- P2.2 Under induced latency/loss, any correction is **bounded and
  self-healing** (authority wins; the view weapon converges within a frame or
  few, no persistent desync, no assert).
- P2.3 The non-predictable weapon shows **no** client prediction and emits no
  seed — authority path unchanged.
- P2.4 A client that reconnects into a **reused slot** never receives the
  previous occupant's seed (this is the lifecycle fix in `277befa`; verify by
  A/B slot reuse: player A on a predictable weapon disconnects mid-fire, player
  B connects into the same slot on a different weapon — B's first predicted
  state must be B's weapon, never A's).
- P2.5 **Predicted-ammo presenter** (`CG_WeaponPredictPresentAmmo`): with
  prediction on, the HUD ammo counter decrements on the *predicted* shot
  (immediately, ahead of the authoritative snapshot) and matches the
  authoritative `STAT_AMMO` once it arrives — no lingering divergence, no wrong
  count after a weapon switch, and while **spectating/following** another player
  the counter shows that player's authoritative ammo (the override must be a
  pass-through when the local predictor is inactive). On ammo **pickup** the
  display may briefly lag until the predictor reseeds — confirm it self-corrects.
- P2.6 **Deferred predicted fire cue**: confirm there is exactly **one** muzzle
  flash/sound per shot. The predicted fire cue is intentionally not wired
  (present-once dedup with the authoritative cue is required first); if a future
  change adds it, this criterion guards against the double-flash regression.

### Tuning

The one value the tables cannot supply is the animation frame cadence
(`FRAME_MS`, currently 100 ms in `local_action_rule_provider`). If P2.1 shows a
consistent one-frame offset on an otherwise-agreeing weapon, adjust that
constant and re-run — it is isolated precisely so this is the only tuning knob.

---

## 3. FR-10-T13 — native demo record / present-once playback (behavioral gate)

The WDM1/WDR1 container, recorder, playback cursor, decode-at-index family,
present-once timeline planner, and present-once execution cursor are built and
unit-proven. This gate confirms a real recorded session round-trips and presents
each event exactly once through the live playback path.

### Enable / record

- Client (or server, per the recorder wiring): set the opt-in `native_record`
  path on for a session.
- Play a normal session with weapon fire, pickups, and world events so the
  stream contains interleaved snapshot/command/event records.
- Stop recording; confirm a `.wdm` is published only after flush/close/full scan
  (a `.wdm.tmp` that fails validation must be quarantined, not published).

### Play back

- Open the `.wdm` and drive playback:
  - forward play at normal timescale,
  - pause (clock held),
  - single-step,
  - seek forward and backward,
  - fast timescale.

**Pass criteria (P3):**
- P3.1 Playback reproduces the recorded snapshot/command/event order exactly;
  bodies are byte-identical (already unit-proven by the roundtrip test — confirm
  it holds for a real recorded stream).
- P3.2 **Present-once**: no event is presented twice. Specifically, a pause
  presents nothing new, and a seek/rewind followed by resume does not re-fire
  already-presented events except after an explicit timeline reset (this is the
  execution-cursor contract from `ae393bd`).
- P3.3 A seek lands on the correct base snapshot and then presents the events
  after it — no interpolation across a discontinuity, no duplicated event at the
  seam.
- P3.4 Legacy `.dm2`, MVD, and GTV playback are unaffected (they never touch the
  native path).

### Still open beyond this runbook (needs live wiring, not just this test)

- Binding the timeline planner + execution cursor into the **live client
  runtime** presenter (this runbook tests the cores against a recorded stream;
  the presenter integration is separate code + its own gate).
- MVD/GTV relay of native streams and spectator-view switching.
- The full native/legacy record/play/seek/relay compatibility matrix.

---

## 4. Reporting

For each gate, record: build hash, date, network conditions, the captured
counters/artifacts named above, and pass/fail per numbered criterion. File the
result next to this runbook. A gate is closed only when every criterion under it
passes with the evidence attached — not before.

The T14/T15 load, soak, multi-platform, and security floors are **out of scope
here**: they require multi-day and multi-platform runs and cannot be satisfied
by a single interactive session.
