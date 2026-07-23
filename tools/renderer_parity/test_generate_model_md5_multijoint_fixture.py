#!/usr/bin/env python3
"""Regression checks for the multi-joint GPU-MD5 renderer-parity fixture."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import generate_model_md5_multijoint_fixture as fixture


ROOT = Path(__file__).resolve().parents[2]


class ModelMd5MultiJointFixtureTests(unittest.TestCase):
    def test_authored_map_is_current(self) -> None:
        asset_root = ROOT / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_uses_the_registered_multi_joint_player_replacement(self) -> None:
        entity_text = "".join(fixture.MODEL_ENTITY)
        self.assertIn('"classname" "misc_model"', entity_text)
        self.assertIn('"model" "players/male/tris.md2"', entity_text)
        self.assertIn('"frame" "100"', entity_text)
        self.assertIn('"scale" "1"', entity_text)

    def test_generator_writes_only_its_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {asset_root / "maps" / fixture.MAP_NAME},
                set(fixture.generated_outputs(asset_root)),
            )

    def test_manifest_latches_native_gpu_skinning(self) -> None:
        manifest = json.loads(
            (ROOT / "assets/renderer_parity/fr01_model_gpu_md5_multijoint_manifest.json").read_text(
                encoding="utf-8"
            )
        )
        scene = manifest["scenes"][0]
        self.assertEqual("gpu_md5_multijoint_skinning", scene["id"])
        self.assertEqual("renderer_parity/fr01_model_gpu_md5_multijoint.cfg", scene["config"])
        self.assertEqual("1", scene["launch_cvars"]["vk_md5_gpu_skinning"])
        self.assertEqual([690, 365, 75, 120], scene["crop"])
        self.assertEqual("gpu_md5_multijoint_magenta_skin", scene["probes"][0]["name"])


if __name__ == "__main__":
    unittest.main()
