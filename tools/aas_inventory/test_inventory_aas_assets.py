#!/usr/bin/env python3
"""Regression tests for the WORR AAS asset inventory tool."""

from __future__ import annotations

import json
import pathlib
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import inventory_aas_assets as inventory


def write_file(path: pathlib.Path, payload: bytes = b"data") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def write_pak(path: pathlib.Path, members: dict[str, bytes]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = bytearray()
    directory = bytearray()
    for name, data in members.items():
        offset = len(payload) + inventory.PAK_HEADER.size
        payload.extend(data)
        encoded = name.encode("ascii")
        if len(encoded) > 55:
            raise ValueError(f"pak member name too long: {name}")
        directory.extend(struct.pack("<56sii", encoded, offset, len(data)))

    directory_offset = inventory.PAK_HEADER.size + len(payload)
    header = inventory.PAK_HEADER.pack(b"PACK", directory_offset, len(directory))
    path.write_bytes(header + payload + directory)


def write_q2_bsp(
    path: pathlib.Path,
    *,
    brush_contents: int = 0,
    entities: bytes = b"",
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lump_count = 19
    header_size = 8 + lump_count * 8
    lumps = [(0, 0)] * lump_count
    payload = bytearray()

    def add_lump(index: int, data: bytes) -> None:
        if not data:
            return
        offset = header_size + len(payload)
        payload.extend(data)
        lumps[index] = (offset, len(data))

    add_lump(0, entities)
    add_lump(14, struct.pack("<iii", 0, 0, brush_contents))
    header = bytearray(b"IBSP")
    header.extend(struct.pack("<i", 38))
    for offset, length in lumps:
        header.extend(struct.pack("<ii", offset, length))
    path.write_bytes(header + payload)


class AasAssetInventoryTests(unittest.TestCase):
    def test_loose_and_zip_assets_are_classified(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            write_file(root / ".install" / "basew" / "maps" / "ready.bsp")
            write_file(root / ".install" / "basew" / "maps" / "ready.aas")
            write_file(root / "assets" / "maps" / "source_only.map")

            archive = root / ".install" / "basew" / "pak0.pkz"
            archive.parent.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(archive, "w") as package:
                package.writestr("maps/needs.bsp", b"bsp")

            report = inventory.build_inventory(
                root,
                [".install", "assets"],
                None,
                [],
            )
            maps = {entry["id"]: entry for entry in report["maps"]}

            self.assertEqual(maps["ready"]["status"], "ready")
            self.assertEqual(maps["needs"]["status"], "needs_conversion")
            self.assertEqual(maps["needs"]["conversion_action"], "generate_aas_from_bsp")
            self.assertEqual(maps["source_only"]["status"], "source_only")
            self.assertEqual(report["summary"]["ready"], 1)
            self.assertEqual(report["summary"]["needs_conversion"], 1)
            self.assertEqual(report["summary"]["source_only"], 1)

    def test_quake_ii_pak_directory_members_are_scanned(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            write_pak(
                root / ".install" / "basew" / "pak0.pak",
                {
                    "maps/pakready.bsp": b"bsp",
                    "maps/pakready.aas": b"aas",
                    "maps/pakneeds.bsp": b"bsp",
                },
            )

            report = inventory.build_inventory(root, [".install"], None, [])
            maps = {entry["id"]: entry for entry in report["maps"]}

            self.assertEqual(maps["pakready"]["status"], "ready")
            self.assertEqual(maps["pakready"]["bsp_locations"][0]["container"], "pak")
            self.assertEqual(maps["pakneeds"]["status"], "needs_conversion")

    def test_manifest_required_and_pending_reference_status_are_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            write_file(root / ".install" / "basew" / "maps" / "mm-rage.bsp")
            manifest = {
                "maps": [
                    {
                        "id": "mm-rage",
                        "path": ".install/basew/maps/mm-rage.bsp",
                        "required": True,
                        "coverage_categories": ["worr_current_dm"],
                    },
                    {
                        "id": "missing",
                        "path": ".install/basew/maps/missing.bsp",
                        "required": True,
                        "coverage_categories": ["missing_reference"],
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
                        "id": "missing_reference",
                        "map_ids": ["missing"],
                        "minimum_validated_maps": 1,
                    },
                    {
                        "id": "water_reference",
                        "map_ids": [],
                        "minimum_validated_maps": 1,
                        "required_features": ["water"],
                    },
                ],
                "pending_reference_maps": ["q2dm1", "mm-rage", "capture-the-flag map"],
            }
            manifest_path = root / "tools" / "q2aas" / "validation_manifest.json"
            manifest_path.parent.mkdir(parents=True, exist_ok=True)
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            report = inventory.build_inventory(
                root,
                [".install"],
                "tools/q2aas/validation_manifest.json",
                [],
            )
            maps = {entry["id"]: entry for entry in report["maps"]}
            pending = {entry["label"]: entry for entry in report["manifest"]["pending_reference_status"]}

            self.assertTrue(maps["mm-rage"]["manifest_required"])
            self.assertEqual(maps["mm-rage"]["coverage_categories"], ["worr_current_dm"])
            self.assertIn("missing", report["manifest"]["missing_required_maps"])
            self.assertEqual(pending["mm-rage"]["status"], "found")
            self.assertEqual(pending["q2dm1"]["status"], "not_staged")
            self.assertEqual(pending["q2dm1"]["matched_map_ids"], [])
            reference_coverage = report["manifest"]["reference_coverage"]
            self.assertEqual(reference_coverage["status"], "incomplete")
            self.assertEqual(reference_coverage["missing_map_count"], 1)
            self.assertEqual(reference_coverage["category_status_counts"]["passed"], 1)
            self.assertEqual(reference_coverage["category_status_counts"]["incomplete"], 2)
            self.assertIn("missing_reference", reference_coverage["incomplete_categories"])
            self.assertIn("water_reference", reference_coverage["incomplete_categories"])
            self.assertIn("water_reference", reference_coverage["strict_failed_categories"])
            categories = {entry["id"]: entry for entry in reference_coverage["categories"]}
            diagnostics = {
                entry["id"]: entry
                for entry in reference_coverage["missing_category_diagnostics"]
            }
            self.assertEqual(
                categories["water_reference"]["candidate_absences"][0]["status"],
                "no_candidate_declared",
            )
            self.assertEqual(categories["water_reference"]["required_features"], ["water"])
            self.assertEqual(
                diagnostics["water_reference"]["primary_reason"],
                "no_candidate_declared",
            )
            self.assertEqual(
                reference_coverage["feature_coverage"]["water"]["missing_category_ids"],
                ["water_reference"],
            )

    def test_available_reference_manifest_selects_staged_manifest_bsps(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            write_file(root / ".install" / "basew" / "maps" / "mm-rage.bsp")
            write_file(root / ".install" / "basew" / "maps" / "mm-rage.aas")
            manifest = {
                "schema": "worr-q2aas-validation-manifest-v1",
                "version": 1,
                "task_ids": ["FR-04-T11", "FR-04-T16"],
                "maps": [
                    {
                        "id": "mm-rage",
                        "path": ".install/basew/maps/mm-rage.bsp",
                        "required": True,
                        "require_reachability": True,
                        "coverage_categories": ["worr_current_dm", "elevator_reference"],
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
                    {
                        "id": "elevator_reference",
                        "map_ids": ["mm-rage"],
                        "minimum_validated_maps": 1,
                        "required_features": ["elevator"],
                    },
                ],
            }
            manifest_path = root / "tools" / "q2aas" / "validation_manifest.json"
            manifest_path.parent.mkdir(parents=True, exist_ok=True)
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            report = inventory.build_inventory(
                root,
                [".install"],
                "tools/q2aas/validation_manifest.json",
                [],
                ".tmp/q2aas/available-reference-validation-manifest.json",
            )
            available = report["available_reference_validation"]
            payload = available["manifest_payload"]

            self.assertEqual(available["status"], "ready")
            self.assertEqual(available["selected_map_ids"], ["mm-rage"])
            self.assertEqual(available["runtime_ready_map_ids"], ["mm-rage"])
            self.assertEqual(available["needs_aas_map_ids"], [])
            self.assertEqual(available["omitted_declared_maps"][0]["id"], "q2dm1")
            self.assertEqual(payload["maps"][0]["id"], "mm-rage")
            self.assertTrue(payload["maps"][0]["required"])
            self.assertEqual(payload["maps"][0]["path"], ".install/basew/maps/mm-rage.bsp")
            self.assertEqual(
                [entry["id"] for entry in payload["reference_coverage"]],
                ["worr_current_dm", "elevator_reference"],
            )

            output_manifest = root / ".tmp" / "q2aas" / "available-reference-validation-manifest.json"
            inventory.write_available_reference_manifest(
                pathlib.Path(".tmp/q2aas/available-reference-validation-manifest.json"),
                root,
                report,
            )
            self.assertEqual(json.loads(output_manifest.read_text())["maps"][0]["id"], "mm-rage")

    def test_bsp_feature_discovery_suggests_unmanifested_water_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            write_q2_bsp(
                root / ".install" / "basew" / "maps" / "waterbox.bsp",
                brush_contents=0x00000020,
            )
            manifest = {
                "schema": "worr-q2aas-validation-manifest-v1",
                "version": 1,
                "task_ids": ["FR-04-T11"],
                "maps": [],
                "reference_coverage": [
                    {
                        "id": "water_reference",
                        "map_ids": [],
                        "minimum_validated_maps": 1,
                        "required_features": ["water"],
                    },
                ],
            }
            manifest_path = root / "tools" / "q2aas" / "validation_manifest.json"
            manifest_path.parent.mkdir(parents=True, exist_ok=True)
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            report = inventory.build_inventory(
                root,
                [".install"],
                "tools/q2aas/validation_manifest.json",
                [],
            )
            maps = {entry["id"]: entry for entry in report["maps"]}
            available = report["available_reference_validation"]
            suggestions = {
                entry["id"]: entry for entry in available["reference_feature_suggestions"]
            }

            self.assertTrue(maps["waterbox"]["bsp_features"]["features"]["water"]["present"])
            self.assertEqual(available["feature_candidate_map_ids"]["water"], ["waterbox"])
            self.assertEqual(
                suggestions["water_reference"]["discovered_candidate_map_ids"],
                ["waterbox"],
            )
            self.assertEqual(suggestions["water_reference"]["status"], "candidate_found")
            self.assertEqual(
                available["summary"]["feature_candidate_counts"]["water"],
                1,
            )
            self.assertEqual(available["summary"]["reference_feature_gap_count"], 0)

    def test_reference_feature_categories_require_observed_bsp_features(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            write_q2_bsp(root / ".install" / "basew" / "maps" / "drybox.bsp")
            write_q2_bsp(
                root / ".install" / "basew" / "maps" / "waterbox.bsp",
                brush_contents=0x00000020,
            )
            manifest = {
                "schema": "worr-q2aas-validation-manifest-v1",
                "version": 1,
                "task_ids": ["FR-04-T11"],
                "maps": [
                    {
                        "id": "drybox",
                        "path": ".install/basew/maps/drybox.bsp",
                        "required": False,
                    },
                    {
                        "id": "waterbox",
                        "path": ".install/basew/maps/waterbox.bsp",
                        "required": False,
                    },
                ],
                "reference_coverage": [
                    {
                        "id": "water_missing",
                        "map_ids": ["drybox"],
                        "minimum_validated_maps": 1,
                        "required_features": ["water"],
                    },
                    {
                        "id": "water_ready",
                        "map_ids": ["waterbox"],
                        "minimum_validated_maps": 1,
                        "required_features": ["water"],
                    },
                ],
            }
            manifest_path = root / "tools" / "q2aas" / "validation_manifest.json"
            manifest_path.parent.mkdir(parents=True, exist_ok=True)
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            report = inventory.build_inventory(
                root,
                [".install"],
                "tools/q2aas/validation_manifest.json",
                [],
            )
            coverage = report["manifest"]["reference_coverage"]
            categories = {entry["id"]: entry for entry in coverage["categories"]}
            diagnostics = {
                entry["id"]: entry
                for entry in coverage["missing_category_diagnostics"]
            }

            self.assertEqual(categories["water_missing"]["status"], "incomplete")
            self.assertEqual(categories["water_missing"]["readiness"], "feature_absent")
            self.assertEqual(categories["water_missing"]["feature_ready_map_count"], 0)
            self.assertEqual(
                categories["water_missing"]["candidate_maps"][0]["feature_status"],
                "missing_features",
            )
            self.assertEqual(
                categories["water_missing"]["candidate_maps"][0]["missing_features"],
                ["water"],
            )
            self.assertEqual(categories["water_ready"]["status"], "passed")
            self.assertEqual(categories["water_ready"]["readiness"], "feature_ready")
            self.assertEqual(categories["water_ready"]["feature_ready_map_count"], 1)
            self.assertEqual(coverage["category_status_counts"]["passed"], 1)
            self.assertEqual(coverage["category_status_counts"]["incomplete"], 1)
            self.assertEqual(coverage["required_feature_category_count"], 2)
            self.assertEqual(coverage["feature_ready_category_count"], 1)
            self.assertEqual(coverage["missing_feature_map_count"], 1)
            self.assertEqual(
                coverage["feature_coverage"]["water"]["ready_map_ids"],
                ["waterbox"],
            )
            self.assertEqual(
                coverage["feature_coverage"]["water"]["missing_map_ids"],
                ["drybox"],
            )
            self.assertEqual(
                diagnostics["water_missing"]["primary_reason"],
                "missing_required_features",
            )


if __name__ == "__main__":
    unittest.main()
