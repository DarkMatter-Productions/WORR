#!/usr/bin/env python3
"""Structural coverage for native Vulkan model frustum culling."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
DEBUG = (ROOT / "src/rend_vk/vk_debug.c").read_text(encoding="utf-8")
DEBUG_HEADER = (ROOT / "src/rend_vk/vk_debug.h").read_text(encoding="utf-8")
ANALYZER = (
    ROOT / "tools/renderer_parity/analyze_renderer_perf.py"
).read_text(encoding="utf-8")
FIXTURE_MANIFEST = (
    ROOT / "assets/renderer_parity/fr01_bmodel_cull_manifest.json"
).read_text(encoding="utf-8")


class VulkanEntityModelCullingSourceTests(unittest.TestCase):
    def test_cached_md2_frame_bounds_replace_per_vertex_bound_scans(self) -> None:
        self.assertIn("vk_md2_frame_bounds_t", ENTITY)
        self.assertIn("vk_md2_frame_bounds_t *frame_bounds", ENTITY)
        self.assertIn('"MD2 frame bounds"', ENTITY)
        self.assertIn("model->md2.frame_bounds = frame_bounds", ENTITY)
        self.assertIn("VK_Entity_ModelBoundsForRefdef", ENTITY)
        self.assertIn("&model->md2.frame_bounds[frame]", ENTITY)
        self.assertIn("&model->md2.frame_bounds[oldframe]", ENTITY)

    def test_culler_uses_conservative_transformed_bounds_and_gl_side_planes(self) -> None:
        self.assertIn('Cvar_Get("vk_cull_models", "1", 0)', ENTITY)
        self.assertIn("VK_Entity_CullModelBounds", ENTITY)
        self.assertIn("VK_Entity_BuildCullFrustum", ENTITY)
        self.assertIn("active_cull_frustum", ENTITY)
        self.assertIn("(ent->flags & RF_WEAPONMODEL)", ENTITY)
        self.assertIn("VectorEmpty(ent->angles)", ENTITY)
        self.assertIn("for (uint32_t corner = 0; corner < 8; corner++)", ENTITY)
        self.assertIn("VK_Entity_TransformPointWithTransform", ENTITY)
        self.assertIn("AnglesToAxis(fd->viewangles, view_axis)", ENTITY)
        self.assertIn("BoxOnPlaneSide(world_mins, world_maxs", ENTITY)
        self.assertIn("BOX_BEHIND", ENTITY)
        self.assertNotIn('#include "rend_gl', ENTITY)

    def test_bsp_and_alias_submission_skip_only_after_origin_diagnostics(self) -> None:
        render_frame = ENTITY.split("void VK_Entity_RenderFrame", 1)[1]
        self.assertIn("const mmodel_t *model = &world_bsp->models[model_index]", render_frame)
        self.assertIn("VK_Entity_CullModelBounds(active_cull_frustum, ent, model->mins", render_frame)
        self.assertIn("VK_Entity_AddOriginAxes(ent);", render_frame)
        self.assertIn("VK_Entity_ModelBoundsForRefdef(model, ent, fd", render_frame)
        self.assertGreater(
            render_frame.index("VK_Entity_AddOriginAxes(ent);"),
            render_frame.index("const vk_model_t *model ="),
        )
        self.assertGreater(
            render_frame.index("VK_Entity_ModelBoundsForRefdef(model, ent, fd"),
            render_frame.index("VK_Entity_AddOriginAxes(ent);"),
        )
        self.assertLess(
            render_frame.index("VK_Entity_BuildCullFrustum(fd, &cull_frustum)"),
            render_frame.index("for (int i = 0; i < fd->num_entities; i++)"),
        )

    def test_cull_counter_is_serialized_and_aggregated(self) -> None:
        self.assertIn("VK_Debug_RecordEntityModelCull", DEBUG_HEADER)
        self.assertIn("entity_models_culled", DEBUG)
        self.assertIn("entity_models_culled=%u", DEBUG)
        self.assertIn("entity_models_culled", ANALYZER)

    def test_fixture_captures_default_and_cull_disabled_paths(self) -> None:
        self.assertIn("mixed_visible_offscreen_inline_bsp_models_cull_disabled",
                      FIXTURE_MANIFEST)
        self.assertIn('"vk_cull_models": "0"', FIXTURE_MANIFEST)


if __name__ == "__main__":
    unittest.main()
