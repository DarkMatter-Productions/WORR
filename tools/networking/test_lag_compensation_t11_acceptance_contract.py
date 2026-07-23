#!/usr/bin/env python3
"""Source-level contracts for the FR-10-T11 parent acceptance seams."""

from __future__ import annotations

import importlib.util
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = ROOT / "tools/networking/run_canonical_rail_damage_runtime_gate.py"
SPEC = importlib.util.spec_from_file_location("canonical_t11_runtime", RUNNER_PATH)
assert SPEC and SPEC.loader
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)

MANIFEST = json.loads(
    (ROOT / "tools/networking/scenarios/fr10_t11_acceptance_manifest.json").read_text(
        encoding="utf-8"
    )
)
SVCMDS = (ROOT / "src/game/sgame/gameplay/g_svcmds.cpp").read_text(
    encoding="utf-8"
)
LAG = (ROOT / "src/game/sgame/network/lag_compensation.cpp").read_text(
    encoding="utf-8"
)
LAG_HPP = (ROOT / "src/game/sgame/network/lag_compensation.hpp").read_text(
    encoding="utf-8"
)


class LagCompensationT11AcceptanceContractTests(unittest.TestCase):
    def test_spectator_mode_reuses_production_rail_with_three_real_clients(self) -> None:
        rail = RUNNER.GATE_MODES["railgun"]
        spectator = RUNNER.GATE_MODES["railgun-spectator-exclusion"]
        self.assertEqual(spectator["arm_command"], rail["arm_command"])
        self.assertEqual(spectator["status_cvar"], rail["status_cvar"])
        self.assertEqual(spectator["required_client_count"], 3)
        self.assertEqual(spectator["expected_playing_candidates"], 2)
        self.assertEqual(spectator["expected_eligible_candidates"], 2)
        self.assertTrue(spectator["require_spectator_exclusion"])

    def test_spawn_mode_is_an_exact_zero_damage_range_contract(self) -> None:
        protection = RUNNER.GATE_MODES["railgun-spawn-protection"]
        self.assertEqual(protection["weapon_policy"], 5)
        self.assertEqual(protection["expected_damage"], 0)
        self.assertFalse(protection["require_damage"])
        self.assertTrue(protection["require_no_damage"])
        self.assertTrue(protection["require_historical_hit"])
        self.assertEqual(protection["expected_observation_fallback"], 0)

    def test_manifest_makes_spawn_source_availability_a_hard_prerequisite(self) -> None:
        rows = MANIFEST["required_source_tokens"]
        self.assertEqual({row["mode"] for row in rows}, {"railgun-spawn-protection"})
        self.assertEqual(
            {row["path"] for row in rows},
            {
                "src/game/sgame/gameplay/g_svcmds.cpp",
                "src/game/sgame/network/lag_compensation.cpp",
            },
        )

    def test_production_exports_and_routes_the_spawn_protection_fixture(self) -> None:
        arm = "worr_rewind_canonical_rail_spawn_protection_arm"
        status = "sg_worr_rewind_canonical_rail_spawn_protection_status"
        self.assertGreaterEqual(SVCMDS.count(arm), 2)
        self.assertGreaterEqual(LAG.count(status), 2)
        self.assertIn("SpawnProtection", LAG_HPP)
        self.assertIn("PowerupTimer::SpawnProtection", LAG)


if __name__ == "__main__":
    unittest.main()
