#!/usr/bin/env python3
"""Fixture contract for paired alpha-tested shadow caster coverage."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG = (ROOT / "assets/renderer_parity/fr01_alpha_shadow.cfg").read_text(
    encoding="utf-8"
)
MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_alpha_shadow_manifest.json").read_text(
        encoding="utf-8"
    )
)


class VulkanShadowAlphaCasterFixtureTests(unittest.TestCase):
    def test_flashlight_fixture_enables_native_dynamic_moment_shadows(self) -> None:
        for line in (
            "set r_fullbright 0",
            "set r_shadowmaps 1",
            "set r_shadow_filter 3",
            "set cl_shadowlights 1",
            "give item_flashlight",
            "use Flashlight",
        ):
            self.assertIn(line, CONFIG)

    def test_manifest_uses_the_cutout_world_and_inline_bsp_map(self) -> None:
        self.assertEqual("FR-02-T14", MANIFEST["task_id"])
        scene = MANIFEST["scenes"][0]
        self.assertEqual("alpha_cutout_shadow_casters", scene["id"])
        self.assertEqual("renderer_parity/fr01_alpha_shadow.cfg", scene["config"])
        self.assertEqual("fr01_alpha_shadow.tga", scene["capture"])


if __name__ == "__main__":
    unittest.main()
