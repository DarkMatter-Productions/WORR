# Bot Profiles for Server Operators

Bot profiles let a server add bots by a stable profile id instead of typing a
raw display name every time. A profile can set the bot's public name, skin,
default team, skill, and a few behavior hints.

Current WORR server packages are expected to include a complete first-party
botfile pack. The package keeps the canonical copy in `basew/pak0.pkz` and also
stages the same `botfiles/` tree as loose files under `basew/` so dedicated
servers can discover, edit, and reload profiles without unpacking the archive.
If you are using an older or incomplete development build without profile files,
the bot commands still work, but `addbot <profile>` falls back to adding a
bot using that text as its display name.

## Quick Start

Start a deathmatch server:

```powershell
.\worr_ded_x86_64.exe +set basedir . +set game basew +set deathmatch 1 +set maxclients 8 +map q2dm1
```

From the server console or rcon:

```text
bot_reload_profiles
addbot vanguard
botlist
removebot vanguard
```

Useful bot commands:

- `addbot [profile] [team]`: add one bot. The first argument is treated as a
  profile id or profile name when one exists.
- `removebot <name|slot|all>`: remove one bot by name/slot, or remove all
  bots.
- `kickbots`: remove all bots.
- `botlist`: show bot slots currently on the server.
- `bot_reload_profiles`: rescan profile files without restarting the server.

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
bot_reload_profiles
```

Reloading affects future bot adds and auto-filled bots. Bots already in the
match keep the settings they spawned with, so remove and re-add a bot if you
want to see identity or tuning changes immediately.

## Adding a Named Profile

Once profiles are present, add one by id:

```text
addbot vanguard
```

To force a team for this add, pass the team after the profile:

```text
addbot vanguard red
addbot bulwark blue
```

The command team overrides the team written in the profile. Team values are most
useful in team modes; `free` is appropriate for non-team profiles. In
free-for-all modes the match rules decide where the bot belongs. Duel and
match-size limits still apply, so extra bots may be placed as spectators instead
of active players.

If your package includes the first WORR profile seed set, practical gameplay
profiles include `vanguard`, `vector`, `bulwark`, `relay`, and `smoke`.

## Minimum Player Bots

`bot_min_players` keeps the server filled to a target population. Humans and
manually added bots both count toward the target. Auto-filled bots are removed
when humans join or when the target is lowered.

Example: keep a four-player practice server filled with the default `vanguard`
profile:

```text
set bot_profile vanguard
set bot_min_players 4
```

If `bot_profile` names a loaded profile, auto-filled bots use that profile.
If it is empty or does not match a loaded profile, WORR rotates through the
loaded first-party profiles exposed by `botfiles/bots.txt`.

To stop auto-fill without kicking manually added bots, lower the target:

```text
set bot_min_players 0
```

To remove every current bot:

```text
removebot all
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
  such as `rocketlauncher`. Common aliases such as `rocket launcher`,
  `rocket_launcher`, and `rl` are accepted by the validator.
- `WORR_PREFERRED_WEAPON`: Q3-style extension for WORR weapon preference.
- `chat_personality`, `chat`, or `personality`: short chat style label, such as
  `quiet`. Packaged profiles should use one of the known labels such as
  `quiet`, `direct`, `taunting`, `helpful`, or `steady`, or expect a validator
  warning until the new label is registered. The `bot_allow_chat` cvar is
  default-off; when enabled with `bot_chat_live_events`, the bot can select
  short, safe live-event phrases from its chat personality bucket.
  `bot_chat_team_only` can route supported callouts through team chat, and
  `bot_chat_min_interval_ms <ms>` can require a global minimum interval between
  submitted bot chat lines.
- `WORR_CHAT_PERSONALITY`: Q3-style extension for WORR chat style.
- `role` or `team_role`: team behavior hint, such as `attacker`, `defender`, or
  `support`. Known aliases include `attack`, `offense`, `defense`, `duelist`,
  `anchor`, `relay`, `midfielder`, `midfield`, `roamer`, and `returner`.
  Supported match roles now feed FFA/TDM/CTF match-policy selection when no
  stronger role request is active.
- `WORR_ROLE`: Q3-style extension for WORR team role.
- `movement_style`, `movement`, or `move_style`: movement flavor hint, such as
  `strafe`. Known labels include `strafe`, `pressure`, `anchor`, `kite`,
  `patrol`, `roam`, `rush`, `camp`, `circle strafe`, `flank`, and `retreat`.
  Supported movement styles now feed match-policy helpers: strafe, pressure,
  rush, and circle-strafe styles favor attack and major-item pressure; anchor
  and camp styles favor defense and team resource sharing; patrol, roam, and
  flank styles favor midfield/roam behavior; kite, retreat, and evasive styles
  favor roam and recovery collection.
- `WORR_MOVEMENT_STYLE`: Q3-style extension for WORR movement flavor.
- `reaction_jitter_ms`: extra reaction variation in milliseconds. Higher values
  make a bot less clockwork even when its base reaction is fast.
- `WORR_REACTION_JITTER_MS`: Q3-style extension for WORR reaction variation.
- `aim_tracking_noise`: extra aim wobble in degrees for tracking and live aim
  policy.
- `WORR_AIM_TRACKING_NOISE`: Q3-style extension for WORR aim tracking noise.
- `aim_lead_scale`: projectile-leading scale. `1.0` means normal lead, lower
  values under-lead, and higher values slightly over-lead.
- `WORR_AIM_LEAD_SCALE`: Q3-style extension for WORR projectile lead scale.
- `combat_fov`: combat awareness cone in degrees.
- `WORR_COMBAT_FOV`: Q3-style extension for WORR combat field of view.
- `teamplay_bias`: how strongly the bot should favor team coordination. This
  now feeds supported team match-policy helpers.
- `objective_bias`: how strongly the bot should favor match objectives over
  wandering or dueling. This now feeds supported CTF objective policy helpers.
- `friendly_fire_care`: how careful the bot should be about firing through
  teammates. This now feeds supported team friendly-fire policy helpers.
- `WORR_TEAMPLAY_BIAS`, `WORR_OBJECTIVE_BIAS`, and
  `WORR_FRIENDLY_FIRE_CARE`: Q3-style extensions for WORR team policy hints.
- `item_greed`: how strongly the bot should favor pickups for itself.
- `item_denial`: how strongly the bot should take items to deny them to enemies.
- `powerup_timing`: how strongly the bot should care about major item timing.
- `retreat_health`: health threshold where survival and recovery items should
  become more attractive.
- `WORR_ITEM_GREED`, `WORR_ITEM_DENIAL`, `WORR_POWERUP_TIMING`, and
  `WORR_RETREAT_HEALTH`: Q3-style extensions for WORR item policy hints.

Some behavior fields are parsed and preserved before every bot policy uses them.
Role, teamplay, objective, friendly-fire-care, item-greed, item-denial,
powerup-timing, retreat-health, and movement-style hints already affect
supported match-policy helpers. The item-policy hints are used when match
item/resource policy is active: greed favors self pickups, denial favors
deny-enemy pickups in team modes, powerup timing favors major items, and retreat
health raises survival-item priority once the bot is at or below that health
threshold. Treat chat fields as safe profile metadata and tuning hints for the
supported conservative chat system. `bot_allow_chat` gates output,
`bot_chat_live_events` enables gameplay-triggered callouts, `bot_chat_team_only`
can limit supported callouts to team chat, and `bot_chat_min_interval_ms <ms>`
sets a global cooldown. See [Bot Chat](bot-chat.md) for the supported event
families.

The profile validator checks behavior metadata before packaging:

- `reaction` is validated as `0` to `5000` milliseconds after Q3
  `CHARACTERISTIC_REACTIONTIME` seconds are normalized.
- `aggression` is validated as `0.0` to `1.0`.
- `aim_error` is validated as `0` to `90` degrees.
- `reaction_jitter_ms` is validated as `0` to `2000` milliseconds.
- `aim_tracking_noise` is validated as `0` to `90` degrees.
- `aim_lead_scale` is validated as `0.0` to `2.0`.
- `combat_fov` is validated as `1` to `360` degrees.
- Team and item bias values are validated as `0.0` to `1.0`.
- `retreat_health` is validated as `0` to `200`.
- Behavior labels must be simple short labels using letters, numbers, spaces,
  underscores, or hyphens. Malformed labels fail validation.
- Unknown but well-formed weapon, chat, role, or movement labels produce
  warnings so authors can either use a known alias or add the new value to the
  validator.
- Packaged Q3-style `botfiles/bots/*_c.c` profiles should either omit behavior
  tuning metadata from a skill block or provide the full behavior family:
  reaction, aggression, aim error, preferred weapon, chat personality, team
  role, movement style, reaction jitter, aim noise, projectile lead, combat FOV,
  team/objective/friendly-fire policy, item greed/denial/powerup timing, and
  retreat health.

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
    WORR_AIM_ERROR 4.8
    WORR_PREFERRED_WEAPON "chaingun"
    WORR_CHAT_PERSONALITY "direct"
    WORR_ROLE "attacker"
    WORR_MOVEMENT_STYLE "pressure"
    WORR_REACTION_JITTER_MS 160
    WORR_AIM_TRACKING_NOISE 3.8
    WORR_AIM_LEAD_SCALE 0.85
    WORR_COMBAT_FOV 115
    WORR_TEAMPLAY_BIAS 0.55
    WORR_OBJECTIVE_BIAS 0.70
    WORR_FRIENDLY_FIRE_CARE 0.50
    WORR_ITEM_GREED 0.62
    WORR_ITEM_DENIAL 0.65
    WORR_POWERUP_TIMING 0.70
    WORR_RETREAT_HEALTH 35
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
    WORR_REACTION_JITTER_MS 60
    WORR_AIM_TRACKING_NOISE 1.5
    WORR_AIM_LEAD_SCALE 1.10
    WORR_COMBAT_FOV 145
    WORR_TEAMPLAY_BIAS 0.70
    WORR_OBJECTIVE_BIAS 0.86
    WORR_FRIENDLY_FIRE_CARE 0.72
    WORR_ITEM_GREED 0.74
    WORR_ITEM_DENIAL 0.88
    WORR_POWERUP_TIMING 0.92
    WORR_RETREAT_HEALTH 25
}
```

After adding or changing profile files on a running server:

```text
bot_reload_profiles
addbot vanguard
```

## Operator Notes

- Bots are enabled by default; check for `set bot_enable 0` in old configs if
  bots stop receiving navigation commands.
- Keep `maxclients` high enough for humans plus any manual or auto-filled bots.
- Bot profile files are server-side, but referenced skins must still exist in
  the game data players use.
- Use profile files from the WORR package or from sources you are allowed to
  run and distribute.
- Avoid putting private information in profile files. Profile values can appear
  in configs, packages, or server logs.
