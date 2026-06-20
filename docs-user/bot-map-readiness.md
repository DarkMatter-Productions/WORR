# Bot Map Readiness

Bots can only route well on maps that have matching AAS navigation data. AAS is
server-side map navigation data generated from the map BSP; players do not need
to install a separate bot route file.

Use this page when choosing maps for a bot-enabled server.

## Quick Check

A map is ready for route-driven bots when all of these are true:

- the server can load the map BSP, such as `maps/mm-rage.bsp`
- the same game data has `maps/<map>.aas`
- the server log says `Bot AAS: loaded maps/<map>.aas`
- route status, when enabled, shows route commands without repeated route
  failures

`sg_bot_list` only proves that bot slots exist. Bots can spawn on a map without
AAS, but they will not have dependable route-driven behavior there.

## Where AAS Files Live

In a release install, use the active game directory, normally `basew/`.

For local staged builds in this repository, use `.install/basew/`.

Supported AAS locations are:

- loose staging: `basew/maps/<map>.aas`
- release package member: `basew/pak0.pkz` containing `maps/<map>.aas`

For current local development builds, the validated reference AAS is:

```text
.install/basew/maps/mm-rage.aas
.install/basew/pak0.pkz -> maps/mm-rage.aas
```

The loose staged file is useful while developing and checking hashes. The
packaged `pak0.pkz` member is what release validation now expects after the
q2aas package step has run.

## Checking a Staged Install

From the repository root, check the loose staged file:

```powershell
Test-Path .\.install\basew\maps\mm-rage.aas
```

Check the packaged archive member:

```powershell
tar -tf .\.install\basew\pak0.pkz | Select-String '^maps/mm-rage\.aas$'
```

From a copied release install, drop the `.install\` prefix:

```powershell
Test-Path .\basew\maps\mm-rage.aas
tar -tf .\basew\pak0.pkz | Select-String '^maps/mm-rage\.aas$'
```

One valid packaged copy is enough for normal play. A loose copy is also useful
for local staging and troubleshooting.

## Server Log Check

Start a bot-enabled dedicated server with AAS debug output:

```powershell
.\worr_ded_x86_64.exe +set basedir . +set game basew +set deathmatch 1 +set sg_bot_enable 1 +set sg_bot_debug_aas 1 +map mm-rage
```

A ready map prints a line like:

```text
Bot AAS: loaded maps/mm-rage.aas (areas=428, reachability=562, clusters=4)
```

With `sg_bot_debug_aas 2`, WORR also prints a longer `BotLib adapter:` status
line. For practical server operation, the important parts are:

- `q3a_aas=... passed`: the AAS file loaded into BotLib
- `q3a_route=... passed`: a basic route query worked
- `q3a_clusters=...` and `q3a_reachability=...`: the file contains route data

Use `sg_bot_debug_aas 2` briefly while testing. It is noisy enough that public
servers should normally run with it off.

## Status Commands

From the dedicated server console, this command prints the current BotLib/AAS
lifecycle line:

```text
botlib_lifecycle_status
```

Useful fields:

- `sg_bot_enable=0`: bot runtime is disabled, so map readiness was not tested
- `runtime_state=2`: current map AAS is loaded
- `runtime_state=3`: current map AAS failed to load
- `map=<name>` and `aas=maps/<name>.aas`: the map and AAS path being checked
- `q3a_lifecycle_load_successes`: how many successful AAS loads BotLib has seen

When scenario or diagnostic status is running, a
`q3a_bot_frame_command_status` line can also appear. For map readiness, scan for:

- `route_commands` increasing: bots are receiving route-steered commands
- `route_failures=0`: route lookups are not repeatedly failing
- `skipped_runtime=0`: bot frames are not being skipped because AAS is inactive

## Common Messages

`Bot AAS: failed to load maps/<map>.aas: could not load maps/<map>.aas`

The map has no AAS in the active game data. Bots may spawn, but this map is not
route-ready yet.

`AAS header ident is not EAAS`, `unsupported AAS version`, or `AAS lump ...`

The file exists, but it is not a valid WORR/BotLib AAS file for this runtime.
Regenerate it with the WORR q2aas toolchain instead of copying or renaming an
unrelated file.

`Q3A AAS load failed`

The file passed the first file check, but the imported BotLib runtime rejected
it or one of the required AAS smoke queries failed. Treat the map as not
route-ready for this build.

`q3a_bot_frame_command_status ... route_failures=...`

One or more route requests failed during bot command generation. Occasional
failures can happen while a bot changes goals, but repeated failures during a
quiet test usually mean the map, current goal, or travel type is not ready.

## q2aas-Generated Assets

WORR uses its `worr_q2aas`/q2aas tooling to generate `.aas` files from Quake II
BSP maps. The development validation flow writes scratch reports under
`.tmp/q2aas/`, stages validated output under `.install/basew/maps/`, and can
package that output into `.install/basew/pak0.pkz`.

For server operators, the practical rule is simple: use AAS files produced and
validated by the WORR toolchain for the same map you plan to run. Do not rename
another map's `.aas` file to make the warning disappear; BotLib needs route data
that matches the map geometry.

## Available Reference Validation

Development builds can also run an "available reference" validation pass. This
checks only the reference maps that are actually present in the staged install
instead of pretending that every planned reference map is already available.

For operators and testers, read the report in plain terms:

- selected map IDs are the maps the validation actually tested
- runtime-ready map IDs have both the BSP and matching AAS staged
- omitted or missing map IDs still need real map data before they can prove bot
  readiness
- a passed available-reference run applies to that selected set, not to every
  map name on the future coverage list

In the current local validation set, `mm-rage` is the selected and runtime-ready
reference map. If you stage more BSP and AAS files later, rerun the validation
and check which maps moved into the selected and runtime-ready lists before
adding them to a public bot rotation.

## Current Reference Map Limits

Current development validation is centered on `mm-rage`. It has validated AAS
with walk, jump, ladder, walk-off-ledge, barrier-jump, elevator, and
default-off rocket-jump route data.

Wider reference coverage is still being built. In the current local validation
set, water, slime, lava, teleport, door-heavy, CTF, campaign, and broader id
deathmatch reference maps are not all staged yet. That means a map can be a
perfectly playable WORR map and still not be bot route-ready today.

When adding bots to public rotations, test each map individually before leaving
it in the cycle.
