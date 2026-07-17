#!/usr/bin/env python3
"""Regression contract for cgame prediction-invariant recovery wiring."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PREDICT = (ROOT / "src/game/cgame/cg_predict.cpp").read_text(encoding="utf-8")
TIMELINE = (
    ROOT / "src/game/cgame/cg_snapshot_timeline.cpp"
).read_text(encoding="utf-8")
INPUT_ABI = (ROOT / "inc/shared/cgame_prediction.h").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening_brace = source.index("{", start)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function body for {signature}")


class CgamePredictionFailClosedContractTests(unittest.TestCase):
    def test_recovery_results_are_append_only_and_named(self) -> None:
        self.assertIn("WORR_CGAME_PREDICTION_INPUT_REPLAY_REJECTED = 10", INPUT_ABI)
        self.assertIn(
            "WORR_CGAME_PREDICTION_INPUT_RETAINED_STATE_MISSING = 11", INPUT_ABI
        )
        self.assertIn(
            "WORR_CGAME_PREDICTION_INPUT_CONFIG_DISCONTINUITY = 12", INPUT_ABI
        )

    def test_v2_input_resolution_is_bound_to_the_exact_snapshot_cursor(self) -> None:
        for contract in (
            "WORR_CGAME_PREDICTION_INPUT_IMPORT_V2",
            "WORR_CGAME_PREDICTION_INPUT_API_VERSION_V2 2u",
            "WORR_CGAME_PREDICTION_INPUT_REQUEST_CANONICAL_REQUIRED",
            "worr_cgame_prediction_input_request_v2",
            "ResolveInputRangeForCursor",
        ):
            self.assertIn(contract, INPUT_ABI)

        cursor_body = function_body(
            PREDICT, "static uint32_t CG_ResolvePredictionInputForCursor("
        )
        self.assertIn(
            "WORR_CGAME_PREDICTION_INPUT_REQUEST_CANONICAL_REQUIRED",
            cursor_body,
        )
        self.assertIn("request.consumed_command = consumed_command;", cursor_body)
        self.assertIn(
            "prediction_input_import_v2->ResolveInputRangeForCursor(",
            cursor_body,
        )
        self.assertNotIn(
            "prediction_input_import->ResolveInputRange(", cursor_body
        )

        authority_body = function_body(
            PREDICT, "CG_ResolveCanonicalPredictionAuthority("
        )
        self.assertIn(
            "CG_CanonicalSnapshotTimelineCopyPredictionSnapshot(",
            authority_body,
        )
        self.assertIn(
            "candidate.timeline.snapshot.consumed_command",
            authority_body,
        )
        self.assertIn(
            "CG_ResolvePredictionInputForCursor(", authority_body
        )
        self.assertIn("CG_PredictionAuthoritySelectV1(", authority_body)
        self.assertIn(
            "expectation.server_time_us =\n        cl.frame.canonical_server_time_us;",
            authority_body,
        )
        self.assertNotIn("cl.frame.consumed_command", authority_body)

    def test_promotion_is_default_off_and_fails_closed_before_replay(self) -> None:
        self.assertIn(
            '"cg_prediction_snapshot_authority", "0", CVAR_NOARCHIVE',
            PREDICT,
        )
        resolve_body = function_body(
            PREDICT, "static bool CG_ResolvePredictionFrameAuthority("
        )
        normalized_body = " ".join(resolve_body.split())
        self.assertIn(
            "if (!canonical_ok) { frame.blocked = true; "
            "frame.input_result = "
            "WORR_CGAME_PREDICTION_INPUT_CANONICAL_METADATA_REQUIRED; "
            "return false; }",
            normalized_body,
        )
        self.assertIn("frame.input = canonical.input;", resolve_body)
        self.assertIn(
            "frame.movement = canonical.timeline.player.movement;",
            resolve_body,
        )
        self.assertIn("frame.canonical = true;", resolve_body)
        self.assertLess(
            resolve_body.index("return false;"),
            resolve_body.index("frame.input = canonical.input;"),
        )

    def test_hard_resync_discards_all_retained_movement_ring_identity(self) -> None:
        clear_body = function_body(
            PREDICT, "static void CG_ClearPredictionHistory("
        )
        normalized_clear_body = " ".join(clear_body.split())
        for member in (
            "cl.predicted_sequences",
            "cl.predicted_states",
            "cl.predicted_origins",
            "cl.predicted_state_hashes",
            "cl.predicted_collision_hashes",
            "cl.predicted_config_hashes",
            "cl.predicted_replay_chain_hashes",
        ):
            self.assertIn(
                f"memset({member}, 0, sizeof({member}));",
                normalized_clear_body,
            )

        hard_resync_body = function_body(
            PREDICT, "static void CG_PredictionHardResync("
        )
        normalized_hard_resync = " ".join(hard_resync_body.split())
        self.assertIn(
            "const cg_prediction_frame_authority_t &authority",
            normalized_hard_resync,
        )
        self.assertIn(
            "cg_prediction_correction_reason_t reason",
            normalized_hard_resync,
        )
        self.assertIn("CG_ClearPredictionHistory();", hard_resync_body)
        self.assertIn(
            "CG_ApplyPredictionAuthority(authority);", hard_resync_body
        )
        self.assertLess(
            hard_resync_body.index("CG_ClearPredictionHistory();"),
            hard_resync_body.index("CG_ApplyPredictionAuthority(authority);"),
        )
        self.assertNotIn("memset(cl.predicted_", hard_resync_body)
        self.assertIn(
            "CG_SnapshotTimeline_NotePredictionCorrection(", hard_resync_body
        )
        self.assertIn("reason);", hard_resync_body)

    def test_invariant_failures_route_through_hard_resync(self) -> None:
        check_body = function_body(PREDICT, "void CL_CheckPredictionError(")
        for contract in (
            "CG_PredictionAuthorityFailureReason(",
            "WORR_CGAME_PREDICTION_INPUT_RETAINED_STATE_MISSING",
            "WORR_CGAME_PREDICTION_INPUT_CONFIG_DISCONTINUITY",
            "snapshot_discontinuity",
            "retained_state_missing",
            "config_discontinuity",
        ):
            self.assertIn(contract, check_body)
        self.assertGreaterEqual(
            check_body.count("CG_PredictionHardResync("), 4
        )
        self.assertLess(
            check_body.index("CG_ConsumePredictionDiscontinuity(authority)"),
            check_body.index(
                "authority.movement.movement_flags & PMF_NO_PREDICTION"
            ),
        )

        movement_body = function_body(PREDICT, "void CL_PredictMovement(")
        for contract in (
            "CG_PredictionAuthorityFailureReason(",
            "snapshot_discontinuity",
            "replay_rejected",
        ):
            self.assertIn(contract, movement_body)
        self.assertEqual(
            movement_body.count(
                "WORR_CGAME_PREDICTION_INPUT_REPLAY_REJECTED"
            ),
            2,
        )
        self.assertGreaterEqual(
            movement_body.count("CG_PredictionHardResync("), 4
        )
        self.assertLess(
            movement_body.index(
                "CG_ConsumePredictionDiscontinuity(authority)"
            ),
            movement_body.index(
                "authority.movement.movement_flags & PMF_NO_PREDICTION"
            ),
        )

    def test_canonical_movement_seeds_correction_and_replay(self) -> None:
        check_body = function_body(PREDICT, "void CL_CheckPredictionError(")
        movement_body = function_body(PREDICT, "void CL_PredictMovement(")

        self.assertIn(
            "const worr_prediction_state_v1 authoritative =\n"
            "        authority.movement;",
            check_body,
        )
        self.assertIn("step.state = authority.movement;", movement_body)
        self.assertIn(
            "step.player_entity_id = authority.controlled_entity_index;",
            movement_body,
        )
        self.assertIn(
            "VectorCopy(authority.view_offset, step.view_offset);",
            movement_body,
        )
        self.assertNotIn(
            "CG_PredictionState(cl.frame.ps.pmove)", check_body
        )
        self.assertNotIn("cl.frame.ps.pmove.pm_flags", check_body)
        self.assertNotIn(
            "CG_PredictionState(cl.frame.ps.pmove)", movement_body
        )
        self.assertNotIn(
            "step.state = CG_PredictionState(cl.frame.ps.pmove)",
            PREDICT,
        )

    def test_telemetry_carries_a_machine_readable_correction_reason(self) -> None:
        for reason in (
            "input_range_invalid",
            "retained_state_missing",
            "config_discontinuity",
            "replay_rejected",
            "state_divergence",
            "correction_threshold_exceeded",
            "canonical_authority_unavailable",
            "canonical_authority_mismatch",
            "snapshot_discontinuity",
        ):
            self.assertIn(f'return "{reason}";', TIMELINE)
        self.assertIn("reason=%s", TIMELINE)


if __name__ == "__main__":
    unittest.main()
