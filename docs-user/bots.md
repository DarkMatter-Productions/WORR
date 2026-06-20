# Bots

WORR bots are server-owned players. They use the normal server slot, team, and
match rules, so a dedicated server can mix humans and bots without special
client setup.

Bot support is still being expanded. Current builds are best for local practice,
server smoke testing, and early multiplayer bot experiments.

## Enable Bots

Add this to your server config or type it in the server console:

```text
set sg_bot_enable 1
```

For a small practice server:

```text
set deathmatch 1
set maxclients 8
set sg_bot_enable 1
set sg_bot_profile vanguard
set sg_bot_min_players 4
map q2dm1
```

`sg_bot_min_players` keeps the server filled to that population. Humans count
toward the target, so auto-filled bots leave as humans join.

## Useful Commands

```text
sg_bot_add [profile] [team]
sg_bot_remove <name|slot|all>
sg_bot_kick_all
sg_bot_list
sg_bot_reload_profiles
```

Examples:

```text
sg_bot_add vanguard
sg_bot_add bulwark blue
sg_bot_list
sg_bot_remove all
```

Use [Bot Profiles](bot-profiles.md) for profile editing, installed botfile
layout, and safe profile examples.

## Recommended Cvars

```text
set sg_bot_enable 1
set sg_bot_min_players 4
set sg_bot_profile vanguard
set sg_bot_skill 3
set sg_bot_allow_item_timers 1
set sg_bot_item_timer_fuzz_ms 0
set sg_bot_allow_rocketjump 0
```

Start with rocket jumping disabled. It is useful for route testing, but real
combat use is still being developed.

Item timing controls how much bots can use pickup timing knowledge. The default
keeps timers enabled for normal practice play:

- `sg_bot_allow_item_timers 1`: bots may use item timing facts they have
  observed.
- `sg_bot_allow_item_timers 0`: bots ignore item timer knowledge, which can make
  them feel less precise around powerups and other timed pickups.
- `sg_bot_item_timer_fuzz_ms 0`: use the observed timing exactly.
- `sg_bot_item_timer_fuzz_ms 5000`: keep timing enabled, but add up to about
  five seconds of stable fuzz so bots do not play from a perfect clock.

Timer fuzz is deterministic for the same match facts, so repeated tests stay
repeatable. It only changes bot decision-making; it does not change the actual
item respawn rules for players or the server.

For development or diagnosis:

```text
set sg_bot_debug 1
set sg_bot_debug_aas 1
set sg_bot_debug_route 1
set sg_bot_debug_goal 1
set sg_bot_debug_client 0
```

Debug cvars can produce noisy console output. Keep them off on public servers
unless you are testing a specific issue.

## Profiles and Botfiles

Release packages include a first-party botfile set under `basew/botfiles/`.
Local staged builds use the same layout under `.install/basew/botfiles/`.

The packaged copy lives in `basew/pak0.pkz`, and a loose mirror is staged beside
it so dedicated server operators can inspect or edit profiles without unpacking
the archive.

After editing botfiles on a running server:

```text
sg_bot_reload_profiles
```

Reloading affects future bot adds. Existing bots keep the profile values they
spawned with, so remove and re-add a bot to see identity changes immediately.

## AAS and Maps

Bots need AAS navigation data for a map. WORR packages validated generated AAS
files when they are available. Current development builds stage and package the
validated `mm-rage` AAS file; more reference maps are being added over time.

If a map has no AAS data, bots may still spawn, but route-driven behavior will
be limited or unavailable. Check the server console for AAS or bot route
messages when testing a new map.

## Current Limits

The current bot stack can spawn, join matches, load profiles, route through AAS,
seek items, run smoke-proven combat/objective helpers, and expose detailed debug
status. The remaining work is mostly behavior depth:

- aiming and firing are still conservative
- weapon and inventory command ownership is still being completed
- team roles are visible in status but not fully autonomous in live CTF/TDM flow
- coop behavior is a later phase
- map coverage depends on generated AAS files

For public servers, keep bot counts modest until you have tested the map,
profiles, and match rules you plan to run.

## High-Bot Testing

Eight-bot long soaks are treated as manual validation runs. They are useful for
testing command throughput, route stability, and performance, but they can churn
item reservations over time as bots consume and reassign goals.

For normal practice servers, prefer `sg_bot_min_players 2` through `4` first,
then raise the target after the map has proven stable.
