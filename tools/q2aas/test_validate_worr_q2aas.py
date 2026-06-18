#!/usr/bin/env python3
"""Regression tests for WORR q2aas validation manifest handling."""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import validate_worr_q2aas as validator


def write_manifest(path: pathlib.Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


class Q2AasManifestTests(unittest.TestCase):
    def test_reference_coverage_reports_skipped_missing_maps(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            staged_map = root / ".install" / "basew" / "maps" / "mm-rage.bsp"
            staged_map.parent.mkdir(parents=True, exist_ok=True)
            staged_map.write_bytes(b"fake bsp presence is enough for manifest loading")

            manifest_path = root / "tools" / "q2aas" / "validation_manifest.json"
            write_manifest(
                manifest_path,
                {
                    "schema": validator.VALIDATION_MANIFEST_SCHEMA,
                    "version": validator.VALIDATION_MANIFEST_VERSION,
                    "task_ids": ["FR-04-T11", "FR-04-T16", "DV-07-T06"],
                    "maps": [
                        {
                            "id": "mm-rage",
                            "path": ".install/basew/maps/mm-rage.bsp",
                            "required": True,
                            "coverage_categories": ["worr_current_dm"],
                        },
                        {
                            "id": "q2dm1",
                            "path": ".install/basew/maps/q2dm1.bsp",
                            "required": False,
                            "coverage_categories": ["id_deathmatch_reference"],
                        },
                    ],
                    "reference_coverage": [
                        {
                            "id": "worr_current_dm",
                            "map_ids": ["mm-rage"],
                            "minimum_validated_maps": 1,
                        },
                        {
                            "id": "id_deathmatch_reference",
                            "map_ids": ["q2dm1"],
                            "minimum_validated_maps": 1,
                        },
                    ],
                },
            )

            maps, ok, report = validator.load_manifest(
                root,
                manifest_path,
                skip_missing=True,
                packaged_map_cache_dir=root / ".tmp" / "q2aas" / "packaged-maps",
            )

            self.assertTrue(ok, report["errors"])
            self.assertEqual([entry["id"] for entry in maps], ["mm-rage"])
            self.assertEqual(report["loaded_map_count"], 1)
            self.assertEqual(report["skipped_maps"][0]["id"], "q2dm1")
            self.assertEqual(
                report["skipped_maps"][0]["coverage_categories"],
                ["id_deathmatch_reference"],
            )

            coverage = report["reference_coverage"]
            self.assertEqual(coverage["status"], "incomplete")
            self.assertIn("id_deathmatch_reference", coverage["incomplete_categories"])
            categories = {entry["id"]: entry for entry in coverage["categories"]}
            self.assertEqual(categories["worr_current_dm"]["status"], "passed")
            self.assertEqual(categories["id_deathmatch_reference"]["status"], "incomplete")
            self.assertEqual(coverage["missing_maps"][0]["id"], "q2dm1")

    def test_reference_coverage_schema_errors_are_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            manifest_path = root / "tools" / "q2aas" / "validation_manifest.json"
            write_manifest(
                manifest_path,
                {
                    "schema": validator.VALIDATION_MANIFEST_SCHEMA,
                    "version": validator.VALIDATION_MANIFEST_VERSION,
                    "task_ids": ["FR-04-T11"],
                    "maps": [],
                    "reference_coverage": [
                        {
                            "id": "bad-minimum",
                            "map_ids": ["q2dm1"],
                            "minimum_validated_maps": 0,
                        },
                        {
                            "id": "bad-map-list",
                            "map_ids": "q2dm1",
                        },
                    ],
                },
            )

            _, ok, report = validator.load_manifest(
                root,
                manifest_path,
                skip_missing=True,
                packaged_map_cache_dir=root / ".tmp" / "q2aas" / "packaged-maps",
            )

            self.assertFalse(ok)
            errors = "\n".join(str(error) for error in report["errors"])
            self.assertIn("reference_coverage[0].minimum_validated_maps", errors)
            self.assertIn("map_ids for reference_coverage[1]", errors)


if __name__ == "__main__":
    unittest.main()
