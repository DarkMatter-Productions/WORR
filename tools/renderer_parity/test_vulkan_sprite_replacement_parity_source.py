#!/usr/bin/env python3
"""Regression checks for native PCX-to-PNG sprite replacement selection."""

from __future__ import annotations

import json
import unittest
from pathlib import Path

try:
    from . import generate_sprite_replacement_fixture as fixture
except ImportError:
    import generate_sprite_replacement_fixture as fixture


ROOT = Path(__file__).resolve().parents[2]
VK_UI = (ROOT / "src" / "rend_vk" / "vk_ui.c").read_text(encoding="utf-8")
CONFIG = (ROOT / "assets" / "renderer_parity" / "fr02_sprite_replacement.cfg").read_text(
    encoding="utf-8"
)
MANIFEST = json.loads(
    (ROOT / "assets" / "renderer_parity" / "fr02_sprite_replacement_manifest.json").read_text(
        encoding="utf-8"
    )
)


class VulkanSpriteReplacementParityTests(unittest.TestCase):
    def test_native_vulkan_prefers_truecolour_overrides_for_pcx_requests(self) -> None:
        loader = VK_UI[VK_UI.index("static bool VK_UI_LoadImageData("):]
        self.assertIn("bool paletted = ext", loader)
        self.assertIn('override_exts[] = { ".png", ".tga", ".dds" }', loader)
        self.assertIn("if (paletted)", loader)
        self.assertIn("VK_UI_LoadRgbaFromFile(candidate", loader)

    def test_headless_fixture_uses_a_legacy_sprite_name_and_distinct_png_replacement(self) -> None:
        self.assertIn("map worr_fr02_sprite_replacement", CONFIG)
        self.assertIn("teleport 0 0 -22 0 0 0", CONFIG)
        self.assertEqual(".pcx", Path(fixture.SPRITE_SOURCE).suffix)
        self.assertNotEqual(fixture.SOURCE_PCX_RGB, fixture.REPLACEMENT_RGB)
        self.assertIn(fixture.SPRITE_SOURCE.encode("ascii"), fixture.sprite_model())

    def test_strict_manifest_proves_the_png_override_core(self) -> None:
        self.assertEqual("FR-02-T05", MANIFEST["task_id"])
        scene = MANIFEST["scenes"][0]
        self.assertEqual([250, 220, 460, 400], scene["crop"])
        self.assertEqual(2, scene["metrics"]["pixel_threshold"])
        self.assertEqual([0.0001, 0, 0.00005], scene["metrics"]["max_mean_absolute_rgb"])
        self.assertEqual(0, scene["metrics"]["max_pixels_over_threshold_percent"])
        self.assertEqual(
            {
                "name": "png_replacement_opaque_core",
                "color": [188, 40, 232],
                "tolerance": 0,
                "min_pixels_per_backend": 123000,
                "max_backend_count_delta_percent": 0.01,
                "min_backend_intersection_over_union": 1.0,
            },
            scene["probes"][0],
        )


if __name__ == "__main__":
    unittest.main()
