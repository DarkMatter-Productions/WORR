#!/usr/bin/env python3
"""Headless structural regression checks for FR-01-T12 native Vulkan fog."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_SHADOW = (ROOT / "src/rend_vk/vk_shadow.c").read_text(encoding="utf-8")
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
GL_TESS = (ROOT / "src/rend_gl/tess.c").read_text(encoding="utf-8")
WORLD_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_world_shadow.frag"
).read_text(encoding="utf-8")
ENTITY_SHADER = (
    ROOT / "src/rend_vk/shaders/vk_entity.frag"
).read_text(encoding="utf-8")
FOG_CONFIG = (ROOT / "assets/renderer_parity/fr01_global_fog.cfg").read_text(
    encoding="utf-8"
)
FOG_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_global_fog_manifest.json").read_text(
        encoding="utf-8"
    )
)
HEIGHT_FOG_CONFIG = (ROOT / "assets/renderer_parity/fr01_height_fog.cfg").read_text(
    encoding="utf-8"
)
HEIGHT_FOG_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_height_fog_manifest.json").read_text(
        encoding="utf-8"
    )
)
SKY_FOG_CONFIG = (ROOT / "assets/renderer_parity/fr01_sky_fog.cfg").read_text(
    encoding="utf-8"
)
SKY_FOG_ENTITY = (ROOT / "assets/renderer_parity/fr01_sky_fog/base1.ent").read_text(
    encoding="utf-8"
)
SKY_FOG_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_sky_fog_manifest.json").read_text(
        encoding="utf-8"
    )
)
TRANSPARENT_FOG_CONFIG = (
    ROOT / "assets/renderer_parity/fr01_transparent_fog.cfg"
).read_text(encoding="utf-8")
TRANSPARENT_FOG_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_transparent_fog_manifest.json").read_text(
        encoding="utf-8"
    )
)
PARTICLE_FOG_CONFIG = (ROOT / "assets/renderer_parity/fr01_particle_fog.cfg").read_text(
    encoding="utf-8"
)
PARTICLE_FOG_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_particle_fog_manifest.json").read_text(
        encoding="utf-8"
    )
)
BEAM_FOG_CONFIG = (ROOT / "assets/renderer_parity/fr01_beam_fog.cfg").read_text(
    encoding="utf-8"
)
BEAM_FOG_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_beam_fog_manifest.json").read_text(
        encoding="utf-8"
    )
)
SPRITE_FOG_CONFIG = (ROOT / "assets/renderer_parity/fr01_sprite_fog.cfg").read_text(
    encoding="utf-8"
)
SPRITE_FOG_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_sprite_fog_manifest.json").read_text(
        encoding="utf-8"
    )
)
FLARE_FOG_CONFIG = (ROOT / "assets/renderer_parity/fr01_flare_fog.cfg").read_text(
    encoding="utf-8"
)
FLARE_FOG_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_flare_fog_manifest.json").read_text(
        encoding="utf-8"
    )
)


class VulkanFogSourceTests(unittest.TestCase):
    def test_refdef_fog_is_uploaded_once_in_the_native_receiver_uniform(self) -> None:
        self.assertIn('Cvar_Get("vk_fog", "1", 0)', VK_SHADOW)
        self.assertIn("void VK_Shadow_UpdateFog", VK_SHADOW)
        self.assertIn("fd->fog.density * (1.0f / 64.0f)", VK_SHADOW)
        self.assertIn("fd->heightfog.density", VK_SHADOW)
        self.assertIn("fd->fog.sky_factor", VK_SHADOW)
        self.assertIn("VK_Shadow_UpdateFog(fd)", VK_SHADOW)
        self.assertNotIn('#include "rend_gl', VK_SHADOW)

    def test_world_has_a_distinct_sky_fog_path(self) -> None:
        self.assertIn("VK_WORLD_VERTEX_SKY = BIT(7)", VK_WORLD)
        self.assertIn("VK_WORLD_VERTEX_FULLBRIGHT | VK_WORLD_VERTEX_SKY", VK_WORLD)
        self.assertIn("VK_FOG_SKY", WORLD_SHADER)
        self.assertIn("void apply_fog(inout vec3 diffuse, bool sky)", WORLD_SHADER)
        self.assertIn("shadow_fog_params.z", WORLD_SHADER)

    def test_world_and_entities_apply_global_and_height_fog_after_material_lighting(self) -> None:
        for shader in (WORLD_SHADER, ENTITY_SHADER):
            self.assertIn("shadow_fog_color_density", shader)
            self.assertIn("shadow_heightfog_start", shader)
            self.assertIn("frag_depth = gl_FragCoord.z / max(gl_FragCoord.w, 1e-6)", shader)
            self.assertIn("1.0 - exp(-(d * d))", shader)
            self.assertIn("exp(-shadow_fog_params.y * eye)", shader)
            self.assertIn("shadow_fog_params.x * frag_depth", shader)
        self.assertIn("apply_fog(color.rgb", WORLD_SHADER)
        self.assertIn("apply_fog(out_color.rgb,", ENTITY_SHADER)

    def test_authored_global_fog_fixture_requires_real_fog_coverage_and_exact_parity(self) -> None:
        self.assertIn("set gl_fog 1", FOG_CONFIG)
        self.assertIn("set vk_fog 1", FOG_CONFIG)
        self.assertIn("map worr_fr01_global_fog", FOG_CONFIG)
        self.assertNotIn("\nfog ", FOG_CONFIG)
        scene = FOG_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "global_fog_fixed_view")
        self.assertEqual(scene["metrics"]["pixel_threshold"], 0)
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [0, 0, 0])
        probe = scene["probes"][0]
        self.assertEqual(probe["min_color"], [74, 125, 172])
        self.assertEqual(probe["max_color"], [78, 129, 182])
        self.assertEqual(probe["min_pixels_per_backend"], 180000)
        self.assertEqual(probe["min_backend_intersection_over_union"], 1.0)

    def test_authored_height_fog_fixture_requires_gradient_coverage_and_exact_parity(self) -> None:
        self.assertIn("set gl_fog 1", HEIGHT_FOG_CONFIG)
        self.assertIn("set vk_fog 1", HEIGHT_FOG_CONFIG)
        self.assertIn("map worr_fr01_height_fog", HEIGHT_FOG_CONFIG)
        self.assertNotIn("\nfog ", HEIGHT_FOG_CONFIG)
        scene = HEIGHT_FOG_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "height_fog_fixed_view")
        self.assertEqual(scene["metrics"]["pixel_threshold"], 0)
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [0, 0, 0])
        probe = scene["probes"][0]
        self.assertEqual(probe["min_color"], [20, 35, 55])
        self.assertEqual(probe["max_color"], [35, 45, 70])
        self.assertEqual(probe["min_pixels_per_backend"], 100000)
        self.assertEqual(probe["min_backend_intersection_over_union"], 1.0)

    def test_authored_sky_fog_fixture_uses_normal_map_override_replication(self) -> None:
        self.assertIn("set gl_fog 1", SKY_FOG_CONFIG)
        self.assertIn("set vk_fog 1", SKY_FOG_CONFIG)
        self.assertIn("set map_override_path renderer_parity/fr01_sky_fog", SKY_FOG_CONFIG)
        self.assertIn("map base1", SKY_FOG_CONFIG)
        self.assertNotIn("\nfog ", SKY_FOG_CONFIG)
        self.assertIn('"fog_color" "0.30 0.50 0.70"', SKY_FOG_ENTITY)
        self.assertIn('"fog_density" "0.50"', SKY_FOG_ENTITY)
        self.assertIn('"fog_sky_factor" "0.60"', SKY_FOG_ENTITY)
        scene = SKY_FOG_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "sky_only_fog_fixed_view")
        self.assertEqual(scene["metrics"]["pixel_threshold"], 2)
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [0.05, 0.08, 0.02])
        probe = scene["probes"][0]
        self.assertEqual(probe["min_pixels_per_backend"], 74000)
        self.assertEqual(probe["max_backend_count_delta_percent"], 0.2)
        self.assertEqual(probe["min_backend_intersection_over_union"], 0.998)

    def test_transparent_world_fog_fixture_retains_blend_and_rounding_contract(self) -> None:
        self.assertIn("set gl_fog 1", TRANSPARENT_FOG_CONFIG)
        self.assertIn("set vk_fog 1", TRANSPARENT_FOG_CONFIG)
        self.assertIn("map worr_fr01_transparent_fog", TRANSPARENT_FOG_CONFIG)
        scene = TRANSPARENT_FOG_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "transparent_world_global_fog")
        self.assertEqual(scene["metrics"]["pixel_threshold"], 1)
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [0, 1, 1])
        probe = scene["probes"][0]
        self.assertEqual(probe["color"], [93, 119, 163])
        self.assertEqual(probe["tolerance"], 1)
        self.assertEqual(probe["min_pixels_per_backend"], 50000)
        self.assertEqual(probe["min_backend_intersection_over_union"], 1.0)

    def test_particle_fog_fixture_requires_the_deterministic_effect_receiver(self) -> None:
        self.assertIn("set gl_fog 1", PARTICLE_FOG_CONFIG)
        self.assertIn("set vk_fog 1", PARTICLE_FOG_CONFIG)
        self.assertIn("set gl_partscale 2", PARTICLE_FOG_CONFIG)
        self.assertIn("set vk_partscale 2", PARTICLE_FOG_CONFIG)
        self.assertIn("set gl_partshape 0", PARTICLE_FOG_CONFIG)
        self.assertIn("set vk_particle_shape 0", PARTICLE_FOG_CONFIG)
        self.assertIn("set cl_testparticles 1", PARTICLE_FOG_CONFIG)
        self.assertIn("map worr_fr01_global_fog", PARTICLE_FOG_CONFIG)
        scene = PARTICLE_FOG_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "particle_global_fog_fixed_view")
        self.assertEqual(scene["metrics"]["pixel_threshold"], 1)
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [0.03, 0.01, 0.03])
        probe = scene["probes"][0]
        self.assertEqual(probe["color"], [75, 132, 174])
        self.assertEqual(probe["tolerance"], 0)
        self.assertEqual(probe["min_pixels_per_backend"], 24000)
        self.assertEqual(probe["max_backend_count_delta_percent"], 0.1)
        self.assertEqual(probe["min_backend_intersection_over_union"], 0.998)

    def test_target_laser_beam_fixture_requires_native_fog_receiver_coverage(self) -> None:
        self.assertIn("if (ent->flags & RF_BEAM)", VK_ENTITY)
        self.assertIn("VK_Entity_AddBeam(ent, fd)", VK_ENTITY)
        self.assertNotIn('#include "rend_gl', VK_ENTITY)
        self.assertIn("set gl_fog 1", BEAM_FOG_CONFIG)
        self.assertIn("set vk_fog 1", BEAM_FOG_CONFIG)
        self.assertIn("map worr_fr01_beam_fog", BEAM_FOG_CONFIG)
        scene = BEAM_FOG_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "beam_global_fog_fixed_view")
        self.assertEqual(scene["crop"], [560, 400, 400, 320])
        self.assertEqual(scene["metrics"]["pixel_threshold"], 7)
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [0.25, 1.0, 0.25])
        probe = scene["probes"][0]
        self.assertEqual(probe["name"], "fogged_target_laser_receiver")
        self.assertEqual(probe["min_color"], [80, 110, 130])
        self.assertEqual(probe["max_color"], [145, 150, 172])
        self.assertEqual(probe["min_pixels_per_backend"], 39000)
        self.assertEqual(probe["min_backend_intersection_over_union"], 0.98)

    def test_bfg_sprite_fixture_requires_native_fog_receiver_coverage(self) -> None:
        self.assertIn("if (model->type == VK_MODEL_SPRITE)", VK_ENTITY)
        self.assertIn("VK_Entity_AddSprite(ent, view_axis, model, depth_hack, weapon_model)", VK_ENTITY)
        self.assertNotIn('#include "rend_gl', VK_ENTITY)
        self.assertIn("set gl_fog 1", SPRITE_FOG_CONFIG)
        self.assertIn("set vk_fog 1", SPRITE_FOG_CONFIG)
        self.assertIn("map worr_fr01_sprite_fog", SPRITE_FOG_CONFIG)
        scene = SPRITE_FOG_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "sprite_global_fog_fixed_view")
        self.assertEqual(scene["crop"], [440, 220, 360, 400])
        self.assertEqual(scene["metrics"]["pixel_threshold"], 1)
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [0.1, 0.1, 0.35])
        probe = scene["probes"][0]
        self.assertEqual(probe["name"], "fogged_bfg_sprite_receiver")
        self.assertEqual(probe["min_color"], [74, 124, 173])
        self.assertEqual(probe["max_color"], [76, 128, 177])
        self.assertEqual(probe["min_pixels_per_backend"], 44000)
        self.assertEqual(probe["min_backend_intersection_over_union"], 0.98)

    def test_flares_explicitly_preserve_the_opengl_no_fog_contract(self) -> None:
        flare_draw = GL_TESS[
            GL_TESS.index("static void GL_FlushFlares(void)"):
            GL_TESS.index("void GL_DrawFlares(void)")
        ]
        self.assertIn("GLS_BLEND_ADD | tess.flags", flare_draw)
        self.assertNotIn("glr.fog_bits", flare_draw)
        self.assertIn("VK_ENTITY_VERTEX_NO_FOG = BIT(11)", VK_ENTITY)
        self.assertGreaterEqual(VK_ENTITY.count("VK_ENTITY_VERTEX_NO_FOG"), 3)
        self.assertIn("#define VK_ENTITY_VERTEX_NO_FOG 2048u", ENTITY_SHADER)
        self.assertIn("void apply_fog(inout vec3 diffuse, bool no_fog)", ENTITY_SHADER)
        self.assertIn("if (no_fog)", ENTITY_SHADER)
        self.assertIn("set gl_fog 1", FLARE_FOG_CONFIG)
        self.assertIn("set vk_fog 1", FLARE_FOG_CONFIG)
        self.assertIn("set cl_flares 1", FLARE_FOG_CONFIG)
        self.assertIn("map worr_fr01_flare_fog", FLARE_FOG_CONFIG)
        scene = FLARE_FOG_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "flare_global_fog_fixed_view")
        self.assertEqual(scene["crop"], [400, 220, 400, 400])
        self.assertEqual(scene["metrics"]["pixel_threshold"], 0)
        self.assertEqual(scene["metrics"]["max_mean_absolute_rgb"], [0, 0, 0])
        probe = scene["probes"][0]
        self.assertEqual(probe["name"], "unfogged_visible_flare_over_fogged_world")
        self.assertEqual(probe["min_color"], [204, 0, 0])
        self.assertEqual(probe["max_color"], [204, 255, 255])
        self.assertEqual(probe["min_pixels_per_backend"], 98000)
        self.assertEqual(probe["min_backend_intersection_over_union"], 1)


if __name__ == "__main__":
    unittest.main()
