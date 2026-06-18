# Bot Profiles for Server Operators

Bot profiles let a server add bots by a stable profile id instead of typing a
raw display name every time. A profile can set the bot's public name, skin,
default team, skill, and a few behavior hints.

Current WORR server packages are expected to include a complete first-party
botfile pack. The package keeps the canonical copy in `basew/pak0.pkz` and also
stages the same `botfiles/` tree as loose files under `basew/` so dedicated
servers can discover, edit, and reload profiles without unpacking the archive.
If you are using an older or incomplete development build without profile files,
the bot commands still work, but `sg_bot_add <profile>` falls back to adding a
bot using that text as its display name.

## Quick Start

Start a deathmatch server with bot support enabled:

```powershell
.\worr_ded_x86_64.exe +set basedir . +set game basew +set deathmatch 1 +set maxclients 8 +set sg_bot_enable 1 +map q2dm1
```

From the server console or rcon:

```text
sg_bot_reload_profiles
sg_bot_add vanguard
sg_bot_list
sg_bot_remove vanguard
```

Useful bot commands:

- `sg_bot_add [profile] [team]`: add one bot. The first argument is treated as a
  profile id or profile name when one exists.
- `sg_bot_remove <name|slot|all>`: remove one bot by name/slot, or remove all
  bots.
- `sg_bot_kick_all`: remove all bots.
- `sg_bot_list`: show bot slots currently on the server.
- `sg_bot_reload_profiles`: rescan profile files without restarting the server.

## Where Profiles Live After Install

In a normal release install, the active game directory is `basew/`. Local staged
builds use the same layout under `.install/basew/`.

- `basew/pak0.pkz`: packaged copy of the WORR asset pack.
- `basew/botfiles/`: loose mirror of the packaged botfiles. This is the
  operator-friendly place to inspect, edit, or override bot profiles.
- `basew/botfiles/bots/*_c.c`: Q3-style character/profile entry points.
- `basew/botfiles/bots/*_w.c`, `*_i.c`, and `*_t.c`: companion files for
  weapon weights, item weights, and chat metadata.
- `basew/botfiles/scripts/*_s.c`: script companions staged with the same
  profile family.
- `basew/bots/profiles/*.bot` and `basew/bots/*.bot`: older plain profile
  locations that are still accepted.

If you launch with another game directory, use the same paths inside that game
directory instead of `basew/`.

The Q3-style character filename without `_c.c` is the profile id. For example,
`basew/botfiles/bots/vanguard_c.c` creates the profile id `vanguard`. Companion
files such as `vanguard_w.c`, `vanguard_i.c`, `vanguard_t.c`, and
`vanguard_s.c` travel with that profile, but the server does not load those
companions as separate profiles.

Keep profile ids unique. If two files use the same id, only one can be loaded.
When adding bots manually, prefer the profile id over the display name because
display names are allowed to contain spaces or color codes.

## Editing and Overrides

For day-to-day server customization, edit the loose files under
`basew/botfiles/`. You usually do not need to open or modify `pak0.pkz`.

To override a packaged profile, keep the same id and edit the matching loose
files. To add a new profile, copy a complete family with a new id:

```text
basew/botfiles/bots/<id>_c.c
basew/botfiles/bots/<id>_w.c
basew/botfiles/bots/<id>_i.c
basew/botfiles/bots/<id>_t.c
basew/botfiles/scripts/<id>_s.c
```

Release packaging checks that botfile families are complete before a build is
published. In plain terms: a release should not ship half of a bot profile.
Keeping your custom profiles together in the same family shape makes them easier
to test, copy between servers, and package later.

After adding or changing profile files on a running server, reload the profile
table:

```text
sg_bot_reload_profiles
```

Reloading affects future bot adds and auto-filled bots. Bots already in the
match keep the settings they spawned with, so remove and re-add a bot if you
want to see identity or tuning changes immediately.

## Adding a Named Profile

Once profiles are present, add one by id:

```text
sg_bot_add vanguard
```

To force a team for this add, pass the team after the profile:

```text
sg_bot_add vanguard red
sg_bot_add bulwark blue
```

The command team overrides the team written in the profile. Team values are most
useful in team modes; `free` is appropriate for non-team profiles. In
free-for-all modes the match rules decide where the bot belongs. Duel and
match-size limits still apply, so extra bots may be placed as spectators instead
of active players.

If your package includes the first WORR profile seed set, practical gameplay
profiles include `vanguard`, `bulwark`, `relay`, and `vector`. The `smoke`
profile is mostly for validation and smoke tests.

## Minimum Player Bots

`sg_bot_min_players` keeps the server filled to a target population while
`sg_bot_enable` is on. Humans and manually added bots both count toward the
target. Auto-filled bots are removed when humans join, when the target is
lowered, or when bot support is disabled.

Example: keep a four-player practice server filled with the default `vanguard`
profile:

```text
set sg_bot_enable 1
set sg_bot_profile vanguard
set sg_bot_min_players 4
```

If `sg_bot_profile` names a loaded profile, auto-filled bots use that profile.
If it is empty or does not match a loaded profile, WORR uses generated bot names
such as `bot1`, `bot2`, and `bot3`.

To stop auto-fill without kicking manually added bots, lower the target:

```text
set sg_bot_min_players 0
```

To remove every current bot:

```text
sg_bot_remove all
```

## Profile Fields

The packaged `botfiles/bots` profiles use a Q3-style character script shape:
one or more `skill N { ... }` blocks, `CHARACTERISTIC_*` fields, and WORR
extension fields. Legacy plain key/value `.bot` files are still accepted from
`bots/profiles` and `bots`.

All fields are optional. Use simple quoted values, especially for names or
anything with spaces.

- `name`: the public player name shown in-game. If omitted, the profile id is
  used.
- `CHARACTERISTIC_NAME`: Q3-style public player name.
- `skin`: the player skin, such as `male/grunt`. If omitted, WORR uses
  `male/grunt`.
- `WORR_SKIN` or `CHARACTERISTIC_SKIN`: Q3-style skin field.
- `team`: the default team for team modes, usually `red`, `blue`, or `free`.
- `WORR_TEAM` or `CHARACTERISTIC_TEAM`: Q3-style team field.
- `skill`: a numeric difficulty hint. Use small values such as `1` through `5`
  unless the botfile set documents something else. Q3-style character scripts
  may include several skill blocks. The current bridge uses the last authored
  block as the profile's default values.
- `reaction`, `reaction_time`, or `reaction_ms`: reaction-time hint, usually in
  milliseconds.
- `WORR_REACTION_MS`: Q3-style extension for WORR reaction time in
  milliseconds.
- `CHARACTERISTIC_REACTIONTIME`: Q3-style reaction-time hint in seconds. WORR
  normalizes it to milliseconds for the runtime smoke/userinfo bridge.
- `aggression` or `aggression_bias`: how eager the bot should be to fight. Keep
  this in a simple range such as `0.0` to `1.0`.
- `CHARACTERISTIC_AGGRESSION`: Q3-style aggression hint.
- `aim_error`, `aimerror`, or `accuracy_error`: aim imprecision hint. Higher
  values should mean less perfect aim.
- `WORR_AIM_ERROR`: Q3-style extension for WORR aim imprecision.
- `preferred_weapon`, `weapon`, or `favorite_weapon`: weapon preference hint,
  such as `rocketlauncher`.
- `WORR_PREFERRED_WEAPON`: Q3-style extension for WORR weapon preference.
- `chat_personality`, `chat`, or `personality`: short chat style label, such as
  `quiet`.
- `WORR_CHAT_PERSONALITY`: Q3-style extension for WORR chat style.
- `role` or `team_role`: team behavior hint, such as `attacker`, `defender`, or
  `support`.
- `WORR_ROLE`: Q3-style extension for WORR team role.
- `movement_style`, `movement`, or `move_style`: movement flavor hint, such as
  `strafe`.
- `WORR_MOVEMENT_STYLE`: Q3-style extension for WORR movement flavor.

Some behavior fields are parsed and preserved before every bot policy uses them.
Treat them as safe profile metadata and tuning hints; exact behavior can change
as the BotLib work continues.

## Script Companions

Packaged Q3-style profiles may include a small script companion for each
profile:

```text
basew/botfiles/scripts/vanguard_s.c
```

These files use a compact `script "main" { ... }` shape with simple commands
such as `point`, `box`, `movebox`, `moveto`, `aim`, `say`, `selectweapon`,
`fireweapon`, and `wait`. They are staged and validated with the profile pack so
future BotLib script work has predictable data to load.

The current server profile loader still uses the character file as the profile
entry point. Treat script companions as authored bot metadata rather than
commands you need to run from the console.

## Safe Profile Example

Example `basew/botfiles/bots/vanguard_c.c`:

```text
//===========================================================================
//
// Name:            vanguard_c.c
// Function:        vanguard character profile
//===========================================================================

#include "chars.h"

skill 1
{
    CHARACTERISTIC_NAME "Vanguard"
    CHARACTERISTIC_REACTIONTIME 1.080
    CHARACTERISTIC_AGGRESSION 0.48
    WORR_SKIN "male/major"
    WORR_TEAM "red"
}

skill 5
{
    CHARACTERISTIC_NAME "Vanguard"
    CHARACTERISTIC_REACTIONTIME 0.180
    CHARACTERISTIC_AGGRESSION 0.82
    CHARACTERISTIC_WEAPONWEIGHTS "bots/vanguard_w.c"
    CHARACTERISTIC_ITEMWEIGHTS "bots/vanguard_i.c"
    CHARACTERISTIC_CHAT_FILE "bots/vanguard_t.c"

    WORR_SKIN "male/major"
    WORR_TEAM "red"
    WORR_AIM_ERROR 1.8
    WORR_PREFERRED_WEAPON "chaingun"
    WORR_CHAT_PERSONALITY "direct"
    WORR_ROLE "attacker"
    WORR_MOVEMENT_STYLE "pressure"
}
```

After adding or changing profile files on a running server:

```text
sg_bot_reload_profiles
sg_bot_add vanguard
```

## Operator Notes

- Add `set sg_bot_enable 1` to your server config before relying on bots.
- Keep `maxclients` high enough for humans plus any manual or auto-filled bots.
- Bot profile files are server-side, but referenced skins must still exist in
  the game data players use.
- Use profile files from the WORR package or from sources you are allowed to
  run and distribute.
- Avoid putting private information in profile files. Profile values can appear
  in configs, packages, or server logs.
