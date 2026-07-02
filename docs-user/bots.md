# Bots

WORR bots are server-owned players. They use the normal server slot, team, and
match rules, so a dedicated server can mix humans and bots without special
client setup.

Bot support is still being expanded. Current builds are best for local practice,
server smoke testing, and early multiplayer bot experiments.

## Enable Bots

Bots are enabled by default. Set `bot_enable 0` only if you want to disable the
bot runtime for a test or server configuration.

For a small practice server:

```text
set deathmatch 1
set maxclients 8
set bot_profile vanguard
set bot_min_players 4
map q2dm1
```

`bot_min_players` keeps the server filled to that population. Humans count
toward the target, so auto-filled bots leave as humans join. If `bot_profile`
is empty, auto-fill rotates through the loaded first-party bot profiles.

Public bot controls use the `bot_` prefix and the commands below. Older
server- or game-prefixed bot aliases are not the supported operator surface.

## Useful Commands

```text
addbot [profile] [team]
removebot <name|slot|all>
kickbots
botlist
bot_reload_profiles
```

Examples:

```text
addbot vanguard
addbot bulwark blue
botlist
removebot all
```

Use [Bot Profiles](bot-profiles.md) for profile editing, installed botfile
layout, and safe profile examples.

Use [Bot Cvars](bot-cvars.md) for the complete public bot cvar/default table.

Use [Bot Chat](bot-chat.md) for supported bot chat cvars, live event families,
and profile personality behavior.

Use [Bot Multiplayer Playtest](bot-playtest.md) for a repeatable FFA, Duel,
TDM, and CTF checklist when validating roaming, close-range combat, retreating,
and objective behavior.

## Recommended Cvars

```text
set bot_min_players 4
set bot_profile vanguard
set bot_skill 3
set bot_allow_item_timers 1
set bot_item_timer_fuzz_ms 0
set bot_allow_rocketjump 0
set bot_allow_chat 0
```

Start with rocket jumping disabled. It is useful for route testing, but real
combat use is still being developed.

Item timing controls how much bots can use pickup timing knowledge. The default
keeps timers enabled for normal practice play:

- `bot_allow_item_timers 1`: bots may use item timing facts they have
  observed.
- `bot_allow_item_timers 0`: bots ignore item timer knowledge, which can make
  them feel less precise around powerups and other timed pickups.
- `bot_item_timer_fuzz_ms 0`: use the observed timing exactly.
- `bot_item_timer_fuzz_ms 5000`: keep timing enabled, but add up to about
  five seconds of stable fuzz so bots do not play from a perfect clock.

Timer fuzz is deterministic for the same match facts, so repeated tests stay
repeatable. It only changes bot decision-making; it does not change the actual
item respawn rules for players or the server.

`bot_allow_chat` is a default-off chat gate. Current builds preserve profile
chat metadata and can emit short live-event callouts when `bot_allow_chat 1`
and `bot_chat_live_events 1` are both enabled. Supported events include spawn,
route-ready, enemy-sighted, low-health, item pickup, item denial, CTF objective
and flag-state changes, blocked routes, and match results. Set
`bot_chat_team_only 1` to prefer team chat where applicable. Set
`bot_chat_min_interval_ms <ms>` to require a global minimum interval between
submitted bot chat lines; rate-limited attempts are skipped instead of queued.
See [Bot Chat](bot-chat.md) for the event list and practical defaults.

## Behavior Experiments

The integrated behavior policy set is enabled by default. It drives roaming,
item-role pickup scoring, close-threat spacing, survival retreat, and the
current FFA/TDM/CTF/coop helper policies. Set `bot_behavior_enable 0` only when
you need to isolate a narrower development cvar during debugging.

For development or diagnosis:

```text
set bot_debug 1
set bot_debug_aas 1
set bot_debug_route 1
set bot_debug_goal 1
set bot_debug_client 0
```

Debug cvars can produce noisy console output. Keep them off on public servers
unless you are testing a specific issue.

## Profiles and Botfiles

Release packages include a first-party botfile set under `basew/botfiles/`.
Local staged builds use the same layout under `.install/basew/botfiles/`.

The packaged copy lives in `basew/pak0.pkz`, and a loose mirror is staged beside
it so dedicated server operators can inspect or edit profiles without unpacking
the archive.

Profile role, team-policy, item-policy, and movement-policy hints now feed supported
match-policy helpers. For example, packaged attacker, defender, and support
profiles can bias the bot's FFA/TDM/CTF match policy toward attack, defense, or
midfield/support behavior. Teamplay, objective, and friendly-fire-care profile
hints can raise team coordination, CTF objective, and friendly-fire caution
priorities, while item greed, item denial, powerup timing, and retreat-health
hints can shape pickup priorities when match item/resource policy is active.
Movement styles such as pressure, anchor, roam, and retreat can also bias
attack, defense, roam, resource-sharing, and recovery collection priorities.
Chat personality fields are preserved as profile metadata and now select the
current safe chat phrase bucket when `bot_allow_chat` is enabled. Live event
chat can use spawn, route, combat, item, objective, blocked-route, low-health,
and match-result events when `bot_chat_live_events` is also enabled. The chat
system is intentionally conservative and rate-limited rather than
conversational.

After editing botfiles on a running server:

```text
bot_reload_profiles
```

Reloading affects future bot adds. Existing bots keep the profile values they
spawned with, so remove and re-add a bot to see identity changes immediately.

## AAS and Maps

Bots need AAS navigation data for a map. WORR packages validated generated AAS
files when they are available. Current development builds stage and package the
validated reference set for `mm-rage`, `q2dm1`, `q2dm2`, `q2dm8`, `q2ctf1`,
`base1`, `base2`, and `train`.

If a map has no AAS data, bots may still spawn, but route-driven behavior will
be limited or unavailable. Check the server console for AAS or bot route
messages when testing a new map.

## Current Limits

The current bot stack can spawn, join matches, load profiles, route through AAS,
seek items, run smoke-proven combat/objective helpers, and expose detailed debug
status. Profile role, teamplay, objective, friendly-fire-care, item-policy, and
movement-style hints already influence supported match-policy helpers, but the
remaining work is mostly behavior depth:

- aiming and firing are still conservative
- weapon and inventory command ownership is still being completed
- team and objective behavior is still experimental, but the integrated
  `bot_behavior_enable 1` policy set is the normal practice-server default
- coop behavior is a later phase
- map coverage depends on generated AAS files

For public servers, keep bot counts modest until you have tested the map,
profiles, and match rules you plan to run.

## High-Bot Testing

Eight-bot long soaks are treated as manual validation runs. They are useful for
testing command throughput, route stability, and performance, but they can churn
item reservations over time as bots consume and reassign goals.

For normal practice servers, prefer `bot_min_players 2` through `4` first,
then raise the target after the map has proven stable.
