import sys
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import discover_reference_candidates as discover  # noqa: E402


class DiscoverReferenceCandidatesTest(unittest.TestCase):
    def test_candidate_from_metadata_scores_hazard_and_liquid_signals(self):
        header = {
            "valid_q2_bsp": True,
            "brush_contents": {
                "flag_counts": {
                    "water": 3,
                    "slime": 4,
                    "lava": 0,
                    "ladder": 1,
                    "clip": 2,
                }
            },
        }
        entities = [
            {"classname": "trigger_hurt"},
            {"classname": "func_door"},
            {"classname": "info_player_deathmatch"},
        ]

        candidate = discover.candidate_from_metadata(Path("maps/q2dm7.bsp"), header, entities)

        self.assertEqual(candidate["status"], "candidate")
        self.assertIn("slime_reference", candidate["reasons"])
        self.assertIn("runtime_hazard_entity", candidate["reasons"])
        self.assertIn("door_reference", candidate["reasons"])
        self.assertGreater(candidate["score"], 20)
        self.assertEqual(candidate["contents"]["slime"], 4)
        self.assertEqual(candidate["entities"]["hazard_entities"], 1)
        self.assertEqual(candidate["entities"]["doors"], 1)

    def test_invalid_bsp_is_not_selected(self):
        candidate = discover.candidate_from_metadata(
            Path("maps/not-q2.bsp"),
            {"valid_q2_bsp": False},
            [{"classname": "trigger_hurt"}],
        )

        self.assertEqual(candidate["status"], "invalid")
        self.assertEqual(candidate["score"], 0)
        self.assertNotIn(candidate, discover.select_conversion_candidates([candidate], 1))

    def test_select_conversion_candidates_prefers_highest_score(self):
        candidates = [
            {"name": "low.bsp", "path": "low.bsp", "status": "candidate", "score": 3},
            {"name": "high.bsp", "path": "high.bsp", "status": "candidate", "score": 30},
            {"name": "zero.bsp", "path": "zero.bsp", "status": "candidate", "score": 0},
        ]

        selected = discover.select_conversion_candidates(candidates, 2)

        self.assertEqual([candidate["name"] for candidate in selected], ["high.bsp", "low.bsp"])

    def test_markdown_includes_conversion_status_and_crouch_count(self):
        report = {
            "schema": discover.SCHEMA,
            "summary": {
                "scanned": 1,
                "valid_q2_bsp": 1,
                "selected_for_conversion": 1,
                "conversion_passed": 1,
                "top": {
                    "slime_reference": [
                        {
                            "name": "q2dm7.bsp",
                            "path": "q2dm7.bsp",
                            "score": 61,
                            "contents": {"water": 0, "slime": 48, "lava": 0},
                            "entities": {"hazard_entities": 0},
                            "conversion": {
                                "returncode": 0,
                                "travel_counts": {"crouch": 2},
                            },
                        }
                    ],
                    "lava_reference": [],
                    "runtime_hazard_entity": [],
                    "water_reference": [],
                },
            },
        }

        markdown = discover.render_markdown(report)

        self.assertIn("q2dm7.bsp", markdown)
        self.assertIn("| q2dm7.bsp | 61 | 0 | 48 | 0 | 0 | 2 | converted |", markdown)


if __name__ == "__main__":
    unittest.main()
