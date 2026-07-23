#!/usr/bin/env python3
"""Source contract for the production-bound FR-10-T10 budget fixture."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = (
    ROOT / "src/game/sgame/network/lag_compensation_t10_budget.inc"
).read_text(encoding="utf-8")
LAG_COMPENSATION = (
    ROOT / "src/game/sgame/network/lag_compensation.cpp"
).read_text(encoding="utf-8")


class LagCompensationT10BudgetContractTests(unittest.TestCase):
    def test_fixture_is_bound_to_production_owners_and_capacities(self) -> None:
        for requirement in (
            "std::array<PoseTrack, MAX_CLIENTS_KEX> players",
            "std::array<MoverTrack, kMoverTrackCapacity> movers",
            "FrozenSceneCache scene",
            "MAX_CLIENTS_KEX == 32",
            "kHistoryCapacity == 512",
            "kMoverHistoryCapacity == 64",
            "kMoverTrackCapacity == 64",
            "kSceneCandidateCapacity == 96",
            "kT10BudgetSampleBatchIterations = 1",
        ):
            self.assertIn(requirement, FIXTURE)

    def test_workload_uses_common_capture_query_and_seal_apis(self) -> None:
        for requirement in (
            "Worr_RewindHistoryAppendV1",
            "Worr_RewindHistoryQueryV1",
            "Worr_RewindSceneAddOwnedResultV1",
            "Worr_RewindSceneSealV1",
            "Worr_RewindSceneValidateV1",
            "kT10BudgetQueryAgeUs = UINT64_C(200000)",
            "queryTimeUs = timeUs - kT10BudgetQueryAgeUs",
            "kT10BudgetP95LimitNs = UINT64_C(1666600)",
            "kT10BudgetOwnerCapBytes = UINT64_C(8) * 1024u * 1024u",
            "static_assert(kT10CombinedFixedStorageBytes <=",
        ):
            self.assertIn(requirement, FIXTURE)

    def test_steady_state_is_fixed_storage_and_live_authority_is_guarded(self) -> None:
        for forbidden in ("malloc(", "calloc(", "realloc(", "new T10", "std::vector"):
            self.assertNotIn(forbidden, FIXTURE)
        self.assertIn("T10BudgetAuthorityHash()", FIXTURE)
        self.assertIn("authorityBefore == T10BudgetAuthorityHash()", FIXTURE)
        self.assertIn("result.capacityOverflows = 0", FIXTURE)
        self.assertIn("fixed at one so p95 cannot hide a slow frame", FIXTURE)
        self.assertIn("queries == result.expectedQueryCount", FIXTURE)
        self.assertIn("overwrites == result.expectedOverwriteCount", FIXTURE)

    def test_existing_dedicated_t10_command_publishes_a_distinct_status(self) -> None:
        self.assertIn('#include "lag_compensation_t10_budget.inc"', LAG_COMPENSATION)
        self.assertIn('"sg_worr_rewind_budget_selftest_status"', LAG_COMPENSATION)
        self.assertIn("(void)RunT10BudgetRuntimeProbe();", LAG_COMPENSATION)


if __name__ == "__main__":
    unittest.main()
