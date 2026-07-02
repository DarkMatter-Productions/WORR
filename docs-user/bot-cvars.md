# Bot Cvars

This page lists the supported public bot cvars for players and server
operators. Development-only proof and diagnostic cvars are intentionally not
listed here.

## Defaults

| Cvar | Default | Use |
|---|---|---|
| `bot_enable` | `1` | Enables the bot runtime. Set `0` only when isolating a server or map issue. |
| `bot_min_players` | `0` | Keeps the server filled to the target total of humans plus bots. |
| `bot_profile` | `""` | Selects one profile for added/auto-filled bots; empty rotates through loaded first-party profiles. |
| `bot_skill` | `3` | Baseline bot skill used when a profile does not override it. |
| `bot_behavior_enable` | `1` | Enables the integrated roaming, item, combat, retreat, and mode-policy behavior set. |
| `bot_allow_item_timers` | `1` | Allows bots to use item timing facts they have observed. |
| `bot_item_timer_fuzz_ms` | `0` | Adds stable timing fuzz when item timers are enabled. |
| `bot_allow_rocketjump` | `0` | Allows rocket-jump route behavior when supported by the map and behavior path. |
| `bot_allow_chat` | `0` | Enables conservative bot chat output. |
| `bot_chat_live_events` | `0` | Allows live event-triggered chat when bot chat is enabled. |
| `bot_chat_min_interval_ms` | `0` | Sets a global minimum interval between bot chat submissions. |
| `bot_chat_team_only` | `0` | Sends supported bot chat through team chat where applicable. |
| `bot_name_prefix` | `B\|` | Prefix used when the server generates bot names. |

## Common Practice Setup

```text
set deathmatch 1
set maxclients 8
set bot_min_players 4
set bot_profile ""
set bot_skill 3
set bot_behavior_enable 1
set bot_allow_item_timers 1
set bot_item_timer_fuzz_ms 0
set bot_allow_rocketjump 0
set bot_allow_chat 0
map q2dm1
```

Leave `bot_profile` empty when you want auto-fill to rotate through the bundled
first-party profiles. Set it to a profile name only when you want to isolate a
specific character.

## Chat Cvars

Bot chat is off by default. To enable the current conservative live-event chat
behavior:

```text
set bot_allow_chat 1
set bot_chat_live_events 1
set bot_chat_min_interval_ms 60000
set bot_chat_team_only 0
```

`bot_chat_live_events` only matters when `bot_allow_chat` is also enabled. Use a
nonzero `bot_chat_min_interval_ms` on public servers so event bursts do not
produce noisy output. Supported event families include `spawn`, `team_ready`,
`route_ready`, `item_taken`, `item_denied`, `enemy_sighted`,
`objective_changed`, `flag_state`, `low_health`, `blocked`, and
`victory_defeat`.

## Related Commands

```text
addbot [profile] [team]
removebot <name|slot|all>
kickbots
botlist
bot_reload_profiles
```

Use [Bot Chat](bot-chat.md) for supported chat events and setup, [Bot
Profiles](bot-profiles.md) for profile fields and installed botfile layout, and
[Bot Multiplayer Playtest](bot-playtest.md) for release-style FFA, Duel, TDM,
and CTF checks.
