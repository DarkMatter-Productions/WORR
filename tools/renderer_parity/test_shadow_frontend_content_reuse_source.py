#!/usr/bin/env python3
"""Structural guardrails for exact shared shadow-page reuse."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "inc/renderer/shadow_frontend.h").read_text(encoding="utf-8")
FRONTEND = (ROOT / "src/renderer/shadow_frontend.c").read_text(encoding="utf-8")


class ShadowFrontendContentReuseSourceTests(unittest.TestCase):
    def test_resident_and_pending_views_track_rendered_content(self) -> None:
        self.assertGreaterEqual(HEADER.count("uint32_t content_hash;"), 2)
        self.assertIn("static uint32_t ShadowFrontend_HashFloatBits", FRONTEND)
        self.assertIn("static uint32_t ShadowFrontend_ViewContentHash", FRONTEND)
        self.assertIn(
            "view->content_hash = ShadowFrontend_ViewContentHash(view);", FRONTEND
        )
        self.assertIn(
            "state->resident[resident].content_hash = view->content_hash;", FRONTEND
        )
        self.assertIn("state->resident[i].content_hash = 0;", FRONTEND)
        self.assertIn("state->resident[victim].content_hash = 0;", FRONTEND)

    def test_exact_projection_and_caster_changes_dirty_a_reused_page(self) -> None:
        for member in (
            "view->origin[i]",
            "view->axis[i][j]",
            "view->fov_x",
            "view->fov_y",
            "view->ortho_size",
            "view->near_z",
            "view->far_z",
        ):
            self.assertIn(f"ShadowFrontend_HashFloatBits(hash, {member})", FRONTEND)
        self.assertIn("slot->content_hash != view->content_hash", FRONTEND)
        self.assertIn("view->dirty_reasons |= SHADOW_DIRTY_LIGHT_PARAMS;", FRONTEND)
        self.assertIn("ShadowFrontend_HashFloatBits(hash, caster->entity.origin[j])", FRONTEND)
        self.assertIn("ShadowFrontend_HashFloatBits(hash, caster->entity.angles[j])", FRONTEND)
        self.assertIn("ShadowFrontend_HashFloatBits(hash, caster->bounds[0][j])", FRONTEND)

    def test_dynamic_category_is_not_an_unconditional_dirty_reason(self) -> None:
        select_casters = FRONTEND.split("static int ShadowFrontend_SelectViewCasters", 1)[1].split(
            "static shadow_storage_family_t", 1
        )[0]
        add_view = FRONTEND.split("static void ShadowFrontend_AddView", 1)[1].split(
            "static void ShadowFrontend_BuildViews", 1
        )[0]
        self.assertNotIn("dirty_reasons", select_casters)
        self.assertNotIn("source_shadow == DL_SHADOW_DYNAMIC", add_view)


if __name__ == "__main__":
    unittest.main()
