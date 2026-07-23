#!/usr/bin/env python3
"""Regression checks for Vulkan's non-cutout sprite blend route and its gate."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_ENTITY = (ROOT / "src" / "rend_vk" / "vk_entity.c").read_text(encoding="utf-8")
CONFIG = (ROOT / "assets" / "renderer_parity" / "fr02_sprite_blend.cfg").read_text(encoding="utf-8")
MANIFEST = json.loads(
    (ROOT / "assets" / "renderer_parity" / "fr02_sprite_blend_manifest.json").read_text(
        encoding="utf-8"
    )
)


class VulkanSpriteBlendParityTests(unittest.TestCase):
    def test_native_sprite_path_selects_blending_for_truecolour_and_entity_alpha(self) -> None:
        add_sprite = VK_ENTITY[VK_ENTITY.index("static bool VK_Entity_AddSprite("):]
        self.assertIn("VK_UI_GetImageFlags(sf->image)", add_sprite)
        self.assertIn("(ent->flags & RF_TRANSLUCENT)", add_sprite)
        self.assertIn("(image_flags & IF_TRANSPARENT)", add_sprite)
        self.assertIn("VK_Entity_EmitTri(&v0, &v1, &v2, set, alpha", add_sprite)

    def test_headless_fixture_locks_standard_gameplay_entity_properties(self) -> None:
        self.assertIn("map worr_fr02_sprite_blend", CONFIG)
        self.assertIn("teleport 0 0 -22 0 0 0", CONFIG)
        self.assertIn('"alpha" "0.5"', (ROOT / "tools" / "renderer_parity" /
                                           "generate_sprite_blend_fixture.py").read_text(encoding="utf-8"))
        self.assertIn('"renderFX" "32"', (ROOT / "tools" / "renderer_parity" /
                                              "generate_sprite_blend_fixture.py").read_text(encoding="utf-8"))

    def test_manifest_proves_both_truecolour_coverage_and_entity_translucency(self) -> None:
        self.assertEqual("FR-02-T05", MANIFEST["task_id"])
        scene = MANIFEST["scenes"][0]
        self.assertEqual([100, 220, 760, 400], scene["crop"])
        self.assertEqual(2, scene["metrics"]["pixel_threshold"])
        self.assertEqual([0.003, 0, 0.002], scene["metrics"]["max_mean_absolute_rgb"])
        self.assertEqual(0, scene["metrics"]["max_pixels_over_threshold_percent"])
        self.assertEqual(
            [
                "truecolour_alpha_opaque_core",
                "truecolour_alpha_half_coverage",
                "explicit_entity_translucency_bfg_core",
            ],
            [probe["name"] for probe in scene["probes"]],
        )
        self.assertTrue(all(probe["min_backend_intersection_over_union"] == 1.0
                            for probe in scene["probes"]))


if __name__ == "__main__":
    unittest.main()
