#!/usr/bin/env python3
"""Structural contract for Vulkan's native entity-origin diagnostic."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")


class VulkanDebugOriginsSourceTests(unittest.TestCase):
    def test_origin_axes_match_the_gl_diagnostic_contract_natively(self) -> None:
        self.assertIn('Cvar_Get("vk_showorigins", "0", CVAR_CHEAT)', ENTITY)
        self.assertIn("VK_Entity_AddOriginAxes", ENTITY)
        self.assertIn("if (vk_showorigins && vk_showorigins->integer)", ENTITY)
        self.assertIn("VK_Entity_BuildTransform(ent, &transform)", ENTITY)
        self.assertIn("RF_WEAPONMODEL", ENTITY)
        self.assertIn("VectorMA(ent->origin, 16.0f", ENTITY)
        self.assertIn("transform.scaled_axis[0]", ENTITY)
        self.assertIn("COLOR_RED", ENTITY)
        self.assertIn("COLOR_GREEN", ENTITY)
        self.assertIn("COLOR_BLUE", ENTITY)
        self.assertIn("R_AddDebugLine", ENTITY)
        self.assertNotIn('#include "rend_gl', ENTITY)


if __name__ == "__main__":
    unittest.main()
