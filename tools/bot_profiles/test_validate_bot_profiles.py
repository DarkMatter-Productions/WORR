#!/usr/bin/env python3
"""Tests for the WORR bot profile validator."""

from __future__ import annotations

import contextlib
import io
import json
import os
import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import validate_bot_profiles as validator


class BotProfileValidatorTests(unittest.TestCase):
    def write_profile(self, root: pathlib.Path, name: str, text: str) -> pathlib.Path:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def validate(
        self,
        root: pathlib.Path,
        *paths: pathlib.Path,
        allow_unknown: bool = False,
        fail_on_empty: bool = False,
        check_companions: bool = True,
    ) -> dict:
        return validator.validate_paths(
            [str(path) for path in paths],
            validator.ValidationOptions(
                allow_unknown=allow_unknown,
                fail_on_empty=fail_on_empty,
                check_companions=check_companions,
            ),
            cwd=root,
        )

    def issue_codes(self, report: dict) -> list[str]:
        return [issue["code"] for issue in report["issues"]]

    def write_valid_script_companion(self, botfiles: pathlib.Path, bot_id: str) -> None:
        self.write_profile(
            botfiles / "scripts",
            f"{bot_id}_s.c",
            f"""
            script "main"
            {{
                point("{bot_id} start", 0, 0, 24);
                point("{bot_id} goal", 64, 0, 32);
                box("{bot_id} start box", -16, -16, -24, 16, 16, 40);
                box("{bot_id} goal box", -16, -16, -24, 16, 16, 40);
                movebox("{bot_id} start box", "{bot_id} start");
                movebox("{bot_id} goal box", "{bot_id} goal");
                say("{bot_id} script ready.", NULL);
                selectweapon(7);
                moveto("{bot_id} start box");
                wait(time(0.20));
                aim("{bot_id} goal");
                moveto("{bot_id} goal box");
                wait(touch(0, "{bot_id} goal box"));
            }}
            """,
        )

    def write_minimal_q3_botfile_pack(
        self,
        root: pathlib.Path,
        bot_id: str,
        script_text: str | None = None,
    ) -> pathlib.Path:
        botfiles = root / "assets" / "botfiles"
        self.write_profile(botfiles, "chars.h", "#define CHARACTERISTIC_NAME 0\n")
        self.write_profile(botfiles, "inv.h", "#define INVENTORY_HEALTH 29\n")
        self.write_profile(botfiles, "fw_weap.c", 'weight "Blaster"\n{\n\treturn 10;\n}\n')
        self.write_profile(botfiles, "fw_items.c", 'weight "item_health"\n{\n\treturn 10;\n}\n')
        self.write_profile(botfiles, "teamplay.h", '#define WORR_HELLO0 "Ready."\n')
        self.write_profile(
            botfiles / "bots",
            f"{bot_id}_c.c",
            f"""
            #include "chars.h"
            skill 2
            {{
                name {bot_id}
                skin male/grunt
                CHARACTERISTIC_WEAPONWEIGHTS "bots/{bot_id}_w.c"
                CHARACTERISTIC_ITEMWEIGHTS "bots/{bot_id}_i.c"
                CHARACTERISTIC_CHAT_FILE "bots/{bot_id}_t.c"
            }}
            """,
        )
        self.write_profile(
            botfiles / "bots",
            f"{bot_id}_w.c",
            '#include "inv.h"\n#define W_ROCKETLAUNCHER 300\n#include "fw_weap.c"\n',
        )
        self.write_profile(
            botfiles / "bots",
            f"{bot_id}_i.c",
            '#include "inv.h"\n#define GWW_ROCKETLAUNCHER 195\n#include "fw_items.c"\n',
        )
        self.write_profile(
            botfiles / "bots",
            f"{bot_id}_t.c",
            f"""
            chat "{bot_id}"
            {{
                #include "teamplay.h"
                type "game_enter" {{ "in"; }}
                type "game_exit" {{ "out"; }}
                type "level_start" {{ "go"; }}
                type "level_end" {{ "done"; }}
                type "hit_talking" {{ "wait"; }}
                type "damaged_nokill" {{ "hit"; }}
                type "kill_insult" {{ "down"; }}
                type "random_misc" {{ "move"; }}
            }}
            """,
        )
        if script_text is None:
            self.write_valid_script_companion(botfiles, bot_id)
        else:
            self.write_profile(botfiles / "scripts", f"{bot_id}_s.c", script_text)
        return botfiles

    def test_valid_simple_profile_with_aliases_equals_quotes_and_punctuation(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile = self.write_profile(
                root,
                "smoke.c",
                """
                {
                    name = "B|Smoke";
                    skin male/grunt,
                    skill 4
                    reaction_ms 250
                    aggression_bias 0.65
                    aimerror 2.5
                    weapon rocketlauncher
                    chat quiet
                    team_role attacker
                    move_style strafe
                }
                """,
            )

            report = self.validate(root, profile)

            self.assertEqual(report["summary"]["status"], "passed")
            self.assertEqual(report["summary"]["errors"], 0)
            fields = report["profiles"][0]["fields"]
            self.assertEqual(fields["name"], "B|Smoke")
            self.assertEqual(fields["skin"], "male/grunt")
            self.assertEqual(fields["reaction"], "250")
            self.assertEqual(fields["aggression"], "0.65")
            self.assertEqual(fields["aim_error"], "2.5")
            self.assertEqual(fields["preferred_weapon"], "rocketlauncher")
            self.assertEqual(fields["role"], "attacker")
            self.assertEqual(fields["movement_style"], "strafe")

    def test_q3_style_character_file_strips_suffix_and_normalizes_reactiontime(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile = self.write_profile(
                root,
                "smoke_c.c",
                """
                #include "chars.h"

                skill 4
                {
                    CHARACTERISTIC_NAME "Smoke"
                    CHARACTERISTIC_ATTACK_SKILL 0.70
                    CHARACTERISTIC_REACTIONTIME 0.250
                    CHARACTERISTIC_AGGRESSION 0.65
                    WORR_SKIN "male/grunt"
                    WORR_TEAM "free"
                    WORR_AIM_ERROR 2.5
                    WORR_PREFERRED_WEAPON "rocketlauncher"
                    WORR_CHAT_PERSONALITY "quiet"
                    WORR_ROLE "attacker"
                    WORR_MOVEMENT_STYLE "strafe"
                }
                """,
            )

            report = self.validate(root, profile)

            self.assertEqual(report["summary"]["status"], "passed")
            self.assertEqual(report["summary"]["errors"], 0)
            self.assertEqual(report["summary"]["warnings"], 0)
            self.assertEqual(report["profiles"][0]["id"], "smoke")
            fields = report["profiles"][0]["fields"]
            self.assertEqual(fields["name"], "Smoke")
            self.assertEqual(fields["skin"], "male/grunt")
            self.assertEqual(fields["skill"], "4")
            self.assertEqual(fields["reaction"], "250")

    def test_q3_style_multi_skill_blocks_do_not_warn_as_duplicates(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile = self.write_profile(
                root,
                "smoke_c.c",
                """
                #include "chars.h"

                skill 1
                {
                    CHARACTERISTIC_NAME "Smoke"
                    CHARACTERISTIC_REACTIONTIME 1.150
                    CHARACTERISTIC_AGGRESSION 0.38
                    WORR_SKIN "male/grunt"
                }

                skill 4
                {
                    CHARACTERISTIC_NAME "Smoke"
                    CHARACTERISTIC_REACTIONTIME 0.250
                    CHARACTERISTIC_AGGRESSION 0.65
                    WORR_SKIN "male/grunt"
                }
                """,
            )

            report = self.validate(root, profile)

            self.assertEqual(report["summary"]["status"], "passed")
            self.assertEqual(report["summary"]["warnings"], 0)
            self.assertEqual(report["profiles"][0]["skill_blocks"], ["1", "4"])
            fields = report["profiles"][0]["fields"]
            self.assertEqual(fields["skill"], "4")
            self.assertEqual(fields["reaction"], "250")
            self.assertEqual(fields["aggression"], "0.65")

    def test_missing_identity_fields_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile = self.write_profile(root, "anonymous.bot", "skill 3\n")

            report = self.validate(root, profile)

            self.assertEqual(report["summary"]["status"], "failed")
            self.assertEqual(
                self.issue_codes(report).count("missing_required_field"),
                2,
            )

    def test_unknown_keys_fail_by_default_and_warn_when_allowed(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile = self.write_profile(
                root,
                "custom.bot",
                "name Custom\nskin female/athena\nmystery_value yes\n",
            )

            strict = self.validate(root, profile)
            permissive = self.validate(root, profile, allow_unknown=True)

            self.assertEqual(strict["summary"]["status"], "failed")
            self.assertIn("unknown_key", self.issue_codes(strict))
            self.assertEqual(permissive["summary"]["status"], "passed")
            self.assertEqual(permissive["summary"]["warnings"], 1)
            self.assertIn("unknown_key", self.issue_codes(permissive))

    def test_numeric_fields_must_be_numbers_and_within_ranges(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile = self.write_profile(
                root,
                "bad_numbers.c",
                "\n".join((
                    "name BadNumbers",
                    "skin male/grunt",
                    "skill 9",
                    "reaction -1",
                    "aggression 1.5",
                    "aim_error nope",
                )),
            )

            report = self.validate(root, profile)

            self.assertEqual(report["summary"]["errors"], 4)
            self.assertEqual(
                self.issue_codes(report).count("numeric_out_of_range"),
                3,
            )
            self.assertIn("invalid_numeric", self.issue_codes(report))

    def test_behavior_metadata_value_aliases_are_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile = self.write_profile(
                root,
                "aliases.bot",
                "\n".join((
                    "name AliasBot",
                    "skin female/athena",
                    "reaction_time 320",
                    "aggression_bias 0.70",
                    "accuracy_error 3",
                    'favorite_weapon "rocket launcher"',
                    "personality Direct",
                    "team_role offense",
                    'movement_style "circle strafe"',
                    "reaction_jitter 120",
                    "aim_noise 2.75",
                    "lead_scale 0.95",
                    "view_fov 132",
                    "support_bias 0.40",
                    "goal_bias 0.65",
                    "ff_care 0.70",
                    "pickup_greed 0.55",
                    "denial_bias 0.45",
                    "powerup_timing_bias 0.80",
                    "retreat_health_threshold 35",
                )),
            )

            report = self.validate(root, profile)

            self.assertEqual(report["summary"]["status"], "passed")
            self.assertEqual(report["summary"]["warnings"], 0)
            fields = report["profiles"][0]["fields"]
            self.assertEqual(fields["preferred_weapon"], "rocket launcher")
            self.assertEqual(fields["chat_personality"], "Direct")
            self.assertEqual(fields["role"], "offense")
            self.assertEqual(fields["movement_style"], "circle strafe")
            self.assertEqual(fields["reaction_jitter_ms"], "120")
            self.assertEqual(fields["aim_tracking_noise"], "2.75")
            self.assertEqual(fields["aim_lead_scale"], "0.95")
            self.assertEqual(fields["combat_fov"], "132")
            self.assertEqual(fields["teamplay_bias"], "0.40")
            self.assertEqual(fields["objective_bias"], "0.65")
            self.assertEqual(fields["friendly_fire_care"], "0.70")
            self.assertEqual(fields["item_greed"], "0.55")
            self.assertEqual(fields["item_denial"], "0.45")
            self.assertEqual(fields["powerup_timing"], "0.80")
            self.assertEqual(fields["retreat_health"], "35")

    def test_behavior_metadata_rejects_malformed_values_and_warns_unknown_labels(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile = self.write_profile(
                root,
                "bad_behavior.bot",
                "\n".join((
                    "name BadBehavior",
                    "skin male/grunt",
                    "preferred_weapon weapon/railgun",
                    "chat_personality cryptic",
                    "movement_style teleport",
                )),
            )

            report = self.validate(root, profile)

            self.assertEqual(report["summary"]["status"], "failed")
            self.assertIn("invalid_behavior_value", self.issue_codes(report))
            self.assertEqual(
                self.issue_codes(report).count("unknown_behavior_value"),
                2,
            )

    def test_behavior_policy_metadata_ranges_are_validated(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile = self.write_profile(
                root,
                "bad_policy.bot",
                "\n".join((
                    "name BadPolicy",
                    "skin male/grunt",
                    "worr_reaction_jitter_ms 2500",
                    "worr_aim_tracking_noise 91",
                    "worr_aim_lead_scale 2.5",
                    "worr_combat_fov 0",
                    "worr_teamplay_bias -0.1",
                    "worr_objective_bias 1.1",
                    "worr_friendly_fire_care 1.2",
                    "worr_item_greed 1.2",
                    "worr_item_denial 1.2",
                    "worr_powerup_timing 1.2",
                    "worr_retreat_health 250",
                )),
            )

            report = self.validate(root, profile)

            self.assertEqual(report["summary"]["status"], "failed")
            self.assertEqual(
                self.issue_codes(report).count("numeric_out_of_range"),
                11,
            )

    def test_packaged_q3_behavior_metadata_family_warns_when_skill_block_is_partial(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            botfiles = self.write_minimal_q3_botfile_pack(root, "partial")
            self.write_profile(
                botfiles / "bots",
                "partial_c.c",
                """
                #include "chars.h"
                skill 2
                {
                    CHARACTERISTIC_NAME "Partial"
                    WORR_SKIN "male/grunt"
                    CHARACTERISTIC_WEAPONWEIGHTS "bots/partial_w.c"
                    CHARACTERISTIC_ITEMWEIGHTS "bots/partial_i.c"
                    CHARACTERISTIC_CHAT_FILE "bots/partial_t.c"
                    CHARACTERISTIC_REACTIONTIME 0.320
                    CHARACTERISTIC_AGGRESSION 0.60
                }
                """,
            )

            report = validator.validate_paths([], validator.ValidationOptions(), cwd=root)

            self.assertEqual(report["summary"]["status"], "passed")
            self.assertIn("incomplete_behavior_metadata_family", self.issue_codes(report))
            messages = " ".join(issue["message"] for issue in report["issues"])
            self.assertIn("reaction_jitter_ms", messages)
            self.assertIn("retreat_health", messages)

    def test_first_party_q3_profiles_include_full_behavior_policy_metadata(self) -> None:
        repo_root = pathlib.Path(__file__).resolve().parents[2]

        report = validator.validate_paths(
            ["assets/botfiles/bots"],
            validator.ValidationOptions(),
            cwd=repo_root,
        )

        self.assertEqual(report["summary"]["status"], "passed")
        self.assertEqual(report["summary"]["warnings"], 0)
        profiles = {profile["id"]: profile for profile in report["profiles"]}
        expected_fields = {
            "reaction",
            "aggression",
            "aim_error",
            "preferred_weapon",
            "chat_personality",
            "role",
            "movement_style",
            "reaction_jitter_ms",
            "aim_tracking_noise",
            "aim_lead_scale",
            "combat_fov",
            "teamplay_bias",
            "objective_bias",
            "friendly_fire_care",
            "item_greed",
            "item_denial",
            "powerup_timing",
            "retreat_health",
        }
        for bot_id in ("bulwark", "relay", "smoke", "vanguard", "vector"):
            self.assertIn(bot_id, profiles)
            self.assertTrue(expected_fields.issubset(profiles[bot_id]["fields"]))
        self.assertEqual(profiles["smoke"]["fields"]["reaction_jitter_ms"], "80")
        self.assertEqual(profiles["vanguard"]["fields"]["aim_lead_scale"], "1.10")

    def test_duplicate_ids_are_case_insensitive_across_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            first = self.write_profile(root / "first", "Ranger.c", "name Ranger\nskin male/grunt\n")
            second = self.write_profile(root / "second", "ranger.bot", "name Other\nskin male/grunt\n")

            report = self.validate(root, first.parent, second.parent)

            self.assertEqual(report["summary"]["status"], "failed")
            self.assertIn("duplicate_profile_id", self.issue_codes(report))

    def test_duplicate_key_is_warning_because_runtime_uses_last_value(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile = self.write_profile(
                root,
                "dupe.c",
                "name First\nname Second\nskin male/grunt\n",
            )

            report = self.validate(root, profile)

            self.assertEqual(report["summary"]["status"], "passed")
            self.assertEqual(report["summary"]["warnings"], 1)
            self.assertIn("duplicate_key", self.issue_codes(report))
            self.assertEqual(report["profiles"][0]["fields"]["name"], "Second")

    def test_default_assets_botfiles_bots_root_is_validated_when_present(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            botfiles = root / "assets" / "botfiles"
            self.write_profile(botfiles, "chars.h", "#define CHARACTERISTIC_NAME 0\n")
            self.write_profile(botfiles, "inv.h", "#define INVENTORY_HEALTH 29\n")
            self.write_profile(botfiles, "fw_weap.c", 'weight "Blaster"\n{\n\treturn 10;\n}\n')
            self.write_profile(botfiles, "fw_items.c", 'weight "item_health"\n{\n\treturn 10;\n}\n')
            self.write_profile(botfiles, "teamplay.h", '#define WORR_HELLO0 "Ready."\n')
            self.write_profile(
                botfiles / "bots",
                "assetbot_c.c",
                """
                #include "chars.h"
                skill 2
                {
                    name AssetBot
                    skin male/grunt
                    CHARACTERISTIC_WEAPONWEIGHTS "bots/assetbot_w.c"
                    CHARACTERISTIC_ITEMWEIGHTS "bots/assetbot_i.c"
                    CHARACTERISTIC_CHAT_FILE "bots/assetbot_t.c"
                }
                """,
            )
            self.write_profile(
                botfiles / "bots",
                "assetbot_w.c",
                '#include "inv.h"\n#define W_ROCKETLAUNCHER 300\n#include "fw_weap.c"\n',
            )
            self.write_profile(
                botfiles / "bots",
                "assetbot_i.c",
                '#include "inv.h"\n#define GWW_ROCKETLAUNCHER 195\n#include "fw_items.c"\n',
            )
            self.write_profile(
                botfiles / "bots",
                "assetbot_t.c",
                """
                chat "assetbot"
                {
                    #include "teamplay.h"
                    type "game_enter" { "in"; }
                    type "game_exit" { "out"; }
                    type "level_start" { "go"; }
                    type "level_end" { "done"; }
                    type "hit_talking" { "wait"; }
                    type "damaged_nokill" { "hit"; }
                    type "kill_insult" { "down"; }
                    type "random_misc" { "move"; }
                }
                """,
            )
            self.write_valid_script_companion(botfiles, "assetbot")

            report = validator.validate_paths([], validator.ValidationOptions(), cwd=root)

            self.assertEqual(report["summary"]["status"], "passed")
            self.assertEqual(report["summary"]["profiles"], 1)
            self.assertEqual(report["profiles"][0]["id"], "assetbot")

    def test_assets_botfiles_script_companion_is_required(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            botfiles = self.write_minimal_q3_botfile_pack(root, "noscript")
            (botfiles / "scripts" / "noscript_s.c").unlink()

            report = validator.validate_paths([], validator.ValidationOptions(), cwd=root)

            self.assertEqual(report["summary"]["status"], "failed")
            self.assertIn("missing_script_companion", self.issue_codes(report))

    def test_assets_botfiles_script_companion_is_parsed(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.write_minimal_q3_botfile_pack(
                root,
                "badscript",
                """
                script "main"
                {
                    point("start", 0, 0, 24);
                    dance("bad");
                }
                """,
            )

            report = validator.validate_paths([], validator.ValidationOptions(), cwd=root)

            self.assertEqual(report["summary"]["status"], "failed")
            self.assertIn("unknown_script_command", self.issue_codes(report))
            self.assertIn("missing_script_command", self.issue_codes(report))

    def test_assets_botfiles_companion_checks_report_missing_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile_root = root / "assets" / "botfiles" / "bots"
            self.write_profile(
                profile_root,
                "lonely_c.c",
                "name Lonely\nskin male/grunt\n",
            )

            report = validator.validate_paths([], validator.ValidationOptions(), cwd=root)
            skipped = validator.validate_paths(
                [],
                validator.ValidationOptions(check_companions=False),
                cwd=root,
            )

            self.assertEqual(report["summary"]["status"], "failed")
            self.assertIn("missing_companion", self.issue_codes(report))
            self.assertEqual(skipped["summary"]["status"], "passed")

    def test_assets_botfiles_companion_family_checks_references_and_markers(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            botfiles = self.write_minimal_q3_botfile_pack(root, "brokenfamily")
            self.write_profile(
                botfiles / "bots",
                "brokenfamily_c.c",
                """
                #include "chars.h"
                skill 2
                {
                    name BrokenFamily
                    skin male/grunt
                    CHARACTERISTIC_WEAPONWEIGHTS "bots/brokenfamily_w.c"
                    CHARACTERISTIC_ITEMWEIGHTS "bots/brokenfamily_i.c"
                }
                """,
            )
            self.write_profile(
                botfiles / "bots",
                "brokenfamily_w.c",
                '#include "inv.h"\n#define W_ROCKETLAUNCHER 300\n',
            )

            report = validator.validate_paths([], validator.ValidationOptions(), cwd=root)

            codes = self.issue_codes(report)
            self.assertEqual(report["summary"]["status"], "failed")
            self.assertIn("missing_companion_reference", codes)
            self.assertIn("missing_companion_marker", codes)

    def test_cli_json_exit_code_and_output_shape(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            profile = self.write_profile(root, "cli.bot", "name CliBot\nskin male/grunt\n")
            previous_cwd = pathlib.Path.cwd()
            stdout = io.StringIO()
            try:
                os.chdir(root)
                with contextlib.redirect_stdout(stdout):
                    exit_code = validator.main(["--format", "json", str(profile)])
            finally:
                os.chdir(previous_cwd)

            payload = json.loads(stdout.getvalue())
            self.assertEqual(exit_code, 0)
            self.assertEqual(payload["summary"]["status"], "passed")
            self.assertEqual(payload["profiles"][0]["id"], "cli")

    def test_fail_on_empty_makes_missing_profiles_ci_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)

            report = validator.validate_paths(
                [],
                validator.ValidationOptions(fail_on_empty=True),
                cwd=root,
            )

            self.assertEqual(report["summary"]["status"], "failed")
            self.assertIn("no_profiles", self.issue_codes(report))


if __name__ == "__main__":
    unittest.main(verbosity=2)
