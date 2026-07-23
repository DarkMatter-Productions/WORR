#!/usr/bin/env python3
"""Structural guardrails for the deterministic additive-particle parity lane."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONFIG = (ROOT / "assets/renderer_parity/fr02_particle_additive.cfg").read_text(
    encoding="utf-8"
)
SQUARE_CONFIG = (
    ROOT / "assets/renderer_parity/fr02_particle_additive_square.cfg"
).read_text(encoding="utf-8")
SOFT_CONFIG = (ROOT / "assets/renderer_parity/fr02_particle_additive_soft.cfg").read_text(
    encoding="utf-8"
)
MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr02_particle_additive_manifest.json").read_text(
        encoding="utf-8"
    )
)
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
GL_TESS = (ROOT / "src/rend_gl/tess.c").read_text(encoding="utf-8")
CLIENT_VIEW = (ROOT / "src/client/view.cpp").read_text(encoding="utf-8")


class VulkanParticleParityFixtureTests(unittest.TestCase):
    def test_additive_config_selects_both_native_renderer_controls(self) -> None:
        self.assertIn("set gl_partstyle 1", CONFIG)
        self.assertIn("set vk_particle_style 1", CONFIG)
        self.assertIn("set gl_partshape 0", CONFIG)
        self.assertIn("set vk_particle_shape 0", CONFIG)
        self.assertIn("set gl_partscale 2", CONFIG)
        self.assertIn("set vk_partscale 2", CONFIG)
        self.assertIn("set cl_testparticles 1", CONFIG)
        self.assertIn("set gl_fog 0", CONFIG)
        self.assertIn("set vk_fog 0", CONFIG)

    def test_fixture_is_a_strict_paired_capture(self) -> None:
        self.assertEqual(MANIFEST["task_id"], "FR-02-T05")
        scene = MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "particle_additive_fixed_view")
        self.assertEqual(scene["crop"], [160, 100, 640, 500])
        self.assertEqual(scene["metrics"]["pixel_threshold"], 2)
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [0.01, 0.01, 0.01])
        self.assertEqual(scene["metrics"]["max_pixels_over_threshold_percent"], 0)
        probe = scene["probes"][0]
        self.assertEqual(probe["color"], [48, 220, 96])
        self.assertEqual(probe["tolerance"], 2)
        self.assertEqual(probe["min_pixels_per_backend"], 24000)
        self.assertEqual(probe["min_backend_intersection_over_union"], 0.999)

    def test_fixture_covers_each_legacy_particle_shape(self) -> None:
        for shape, config in ((0, CONFIG), (1, SQUARE_CONFIG), (2, SOFT_CONFIG)):
            self.assertIn(f"set gl_partshape {shape}", config)
            self.assertIn(f"set vk_particle_shape {shape}", config)
            self.assertIn("set gl_partstyle 1", config)
            self.assertIn("set vk_particle_style 1", config)
        self.assertEqual(len(MANIFEST["scenes"]), 3)
        square_probe = MANIFEST["scenes"][1]["probes"][0]
        self.assertEqual(square_probe["name"], "square_additive_particle_field")
        self.assertEqual(square_probe["min_pixels_per_backend"], 32000)
        self.assertEqual(square_probe["min_backend_intersection_over_union"], 1.0)
        soft_probe = MANIFEST["scenes"][2]["probes"][0]
        self.assertEqual(soft_probe["name"], "soft_additive_particle_field")
        self.assertEqual(soft_probe["min_pixels_per_backend"], 27000)
        self.assertEqual(soft_probe["min_backend_intersection_over_union"], 0.9999)

    def test_native_particle_submission_keeps_the_opengl_additive_contract(self) -> None:
        self.assertIn("gl_partstyle->integer ? GLS_BLEND_ADD", GL_TESS)
        self.assertIn("vk_particle_style && vk_particle_style->integer", VK_ENTITY)
        self.assertIn("vk_particle_shape", VK_ENTITY)
        self.assertIn("Cvar_ClampInteger(vk_particle_shape, 0, 2)", VK_ENTITY)
        self.assertIn("VK_Entity_ParticleShapeChanged", VK_ENTITY)
        self.assertIn("dst[3] = (byte)(255.0f * Q_clipf(alpha", VK_ENTITY)
        self.assertIn("VK_Entity_EmitTriBlend(&v0, &v1, &v2, set, true, additive", VK_ENTITY)
        self.assertNotIn('#include "rend_gl', VK_ENTITY)

    def test_debug_field_initializes_visible_particle_attributes(self) -> None:
        test_particles = CLIENT_VIEW.split("static void V_TestParticles(void)", 1)[1].split(
            "static void V_TestEntities(void)", 1
        )[0]
        self.assertIn("memset(p, 0, sizeof(*p));", test_particles)
        self.assertIn("p->scale = 1;", test_particles)
        self.assertIn("p->brightness = 1;", test_particles)


if __name__ == "__main__":
    unittest.main()
