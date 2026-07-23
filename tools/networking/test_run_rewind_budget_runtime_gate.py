#!/usr/bin/env python3
"""Parser and launch contracts for the FR-10-T10 rewind budget gate."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/networking/run_rewind_budget_runtime_gate.py"
SPEC = importlib.util.spec_from_file_location("rewind_budget_runtime_gate", SCRIPT)
assert SPEC and SPEC.loader
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)

PASS_LINE = (
    'sg_worr_rewind_budget_selftest_status "pass:'
    '32:64:512:64:96:32:256:1:96:200000:27648:27648:27648:27648:0:'
    '1:1:1:1:4200000:3500000:7700000:8388608:'
    '500000:900000:1200000:1666600:123456789:0"'
)


class RewindBudgetRuntimeGateTests(unittest.TestCase):
    def test_command_is_headless_input_free_and_uses_the_t10_boundary(self) -> None:
        self.assertEqual(GATE.SGAME_MODULE.as_posix(), "basew/sgame_x86_64.dll")
        command = GATE.build_command(Path("C:/stage/worr_ded_x86_64.exe"))
        self.assertEqual(command[0], "C:\\stage\\worr_ded_x86_64.exe")
        self.assertEqual(command[command.index("+map") + 1], GATE.MAP_NAME)
        self.assertEqual(
            command[command.index("worr_rewind_mover_selftest") - 1], "+sv"
        )
        self.assertNotIn("+quit", command)
        self.assertNotIn("worr_x86_64.exe", " ".join(command))

    def test_complete_budget_evidence_parses_and_validates(self) -> None:
        status = GATE.parse_status("boot\n" + PASS_LINE + "\n")
        self.assertEqual(GATE.validate_status(status), status)
        self.assertEqual(status["query_count"], 27_648)
        self.assertEqual(status["capacity_overflows"], 0)
        self.assertEqual(status["p95_ns"], 900_000)

    def test_rejects_partial_over_budget_or_mutating_evidence(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "exactly one"):
            GATE.parse_status("")
        aggregate = PASS_LINE.replace(":32:256:1:96:", ":32:256:4:96:")
        with self.assertRaisesRegex(RuntimeError, "sample_batch_iterations"):
            GATE.validate_status(GATE.parse_status(aggregate))
        over_budget = PASS_LINE.replace(":900000:1200000:", ":1700000:1800000:")
        with self.assertRaisesRegex(RuntimeError, "p95 exceeds"):
            GATE.validate_status(GATE.parse_status(over_budget))
        mutated = PASS_LINE.replace(":1:1:1:1:4200000", ":1:1:0:1:4200000")
        with self.assertRaisesRegex(RuntimeError, "authority_unchanged"):
            GATE.validate_status(GATE.parse_status(mutated))

    def test_rejects_duplicate_or_inconsistent_storage_rows(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "exactly one"):
            GATE.parse_status(PASS_LINE + "\n" + PASS_LINE)
        inconsistent = PASS_LINE.replace(":7700000:8388608:", ":7800000:8388608:")
        with self.assertRaisesRegex(RuntimeError, "accounting"):
            GATE.validate_status(GATE.parse_status(inconsistent))


if __name__ == "__main__":
    unittest.main()
