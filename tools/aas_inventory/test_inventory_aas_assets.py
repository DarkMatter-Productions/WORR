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
            reference_coverage = report["manifest"]["reference_coverage"]
            self.assertEqual(reference_coverage["status"], "incomplete")
            self.assertEqual(reference_coverage["missing_map_count"], 1)
            self.assertIn("missing_reference", reference_coverage["incomplete_categories"])


if __name__ == "__main__":
    unittest.main()
