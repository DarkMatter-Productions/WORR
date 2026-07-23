#!/usr/bin/env python3
"""Structural regression checks for native optional entity-pass elision."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")


class VulkanEntityOptionalPassElisionSourceTests(unittest.TestCase):
    def test_depth_hack_and_outline_presence_are_recorded_per_frame(self) -> None:
        self.assertIn("bool frame_has_depth_hack_batches;", VK_ENTITY)
        self.assertIn("bool frame_has_outline_batches;", VK_ENTITY)
        self.assertIn("vk_entity.frame_has_depth_hack_batches = false;", VK_ENTITY)
        self.assertIn("vk_entity.frame_has_outline_batches = false;", VK_ENTITY)
        self.assertIn("vk_entity.frame_has_depth_hack_batches |= depth_hack;", VK_ENTITY)
        self.assertIn("vk_entity.frame_has_outline_batches = true;", VK_ENTITY)

    def test_empty_optional_batch_scans_are_elided(self) -> None:
        self.assertIn("if (vk_entity.frame_has_depth_hack_batches) {", VK_ENTITY)
        self.assertIn(
            "if (vk_entity.stencil_available && vk_entity.frame_has_outline_batches) {",
            VK_ENTITY,
        )
        self.assertNotIn('#include "rend_gl', VK_ENTITY)


if __name__ == "__main__":
    unittest.main()
