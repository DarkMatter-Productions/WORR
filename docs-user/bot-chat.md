# Bot Chat

Bot chat is optional and off by default. It is meant to add small match-aware
callouts without turning bots into a constant text stream.

## Quick Setup

```text
set bot_allow_chat 1
set bot_chat_live_events 1
set bot_chat_min_interval_ms 60000
set bot_chat_team_only 0
```

Use a nonzero `bot_chat_min_interval_ms` on public servers. `60000` gives bots
at most one submitted chat line per minute across the server.

Set `bot_chat_team_only 1` when you want supported callouts to use team chat
where the match mode allows it.

## Cvars

| Cvar | Default | Use |
|---|---|---|
| `bot_allow_chat` | `0` | Master switch for bot chat output. |
| `bot_chat_live_events` | `0` | Allows supported gameplay events to trigger chat when `bot_allow_chat` is enabled. |
| `bot_chat_min_interval_ms` | `0` | Global cooldown between submitted bot chat lines. |
| `bot_chat_team_only` | `0` | Sends supported chat through team chat where applicable. |

`bot_chat_live_events` does nothing unless `bot_allow_chat` is also enabled.

## Supported Live Events

When live events are enabled, bots can choose short, safe phrases for these
event families:

| Event | Meaning |
|---|---|
| `spawn` | A bot enters active play. |
| `team_ready` | Team setup or readiness changes. |
| `route_ready` | A bot has committed to a route or useful movement goal. |
| `item_taken` | A bot observes or completes a useful pickup. |
| `item_denied` | A team-mode bot takes an item to deny enemy resources. |
| `enemy_sighted` | A bot has a visible enemy fact. |
| `objective_changed` | CTF objective state changes, such as pickup/drop/return flow. |
| `flag_state` | CTF flag state changes. |
| `low_health` | A bot reaches a weak survival state. |
| `blocked` | A bot route fails or becomes blocked. |
| `victory_defeat` | Intermission or match result state is reached. |

Match-result chat distinguishes win, loss, tie, abort, and unknown outcomes.
For example, a bot on the winning team can use a win phrase while a bot on the
losing team uses a regroup phrase.

## Personality

Profiles can set `chat_personality`, `chat`, `personality`, or
`WORR_CHAT_PERSONALITY`. Packaged profiles currently use styles such as:

- `quiet`
- `direct`
- `taunting`
- `helpful`
- `steady`

The personality changes which safe phrase bucket a bot picks from. It does not
override the global chat cvars or cooldown.

## Practical Defaults

For local testing:

```text
set bot_allow_chat 1
set bot_chat_live_events 1
set bot_chat_min_interval_ms 15000
```

For public practice servers:

```text
set bot_allow_chat 1
set bot_chat_live_events 1
set bot_chat_min_interval_ms 60000
set bot_chat_team_only 1
```

For silent bots:

```text
set bot_allow_chat 0
```

## Notes

Bot chat is intentionally conservative. Bots use short generated phrases from
known event and personality buckets, and rate-limited attempts are skipped
instead of queued. Keep chat disabled when you are isolating route, combat, or
performance issues and want a quieter console.

Use [Bot Cvars](bot-cvars.md) for the full public bot cvar table, and
[Bot Profiles](bot-profiles.md) for profile chat fields.
