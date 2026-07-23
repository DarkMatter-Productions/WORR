#!/usr/bin/env python3
"""Structural regression checks for native Vulkan entity phase elision."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")


class VulkanEntityPhaseElisionSourceTests(unittest.TestCase):
    def test_submit_phase_mask_is_reset_and_marked_by_every_batch_family(self) -> None:
        self.assertIn("uint32_t frame_submit_phase_mask;", VK_ENTITY)
        self.assertIn(
            "uint8_t frame_submit_pass_masks[VK_ENTITY_SUBMIT_COUNT];", VK_ENTITY
        )
        self.assertIn("vk_entity.frame_submit_phase_mask = 0;", VK_ENTITY)
        self.assertIn("memset(vk_entity.frame_submit_pass_masks, 0,", VK_ENTITY)
        self.assertIn(
            "vk_entity.frame_submit_phase_mask |= BIT(vk_entity.current_submit_phase);",
            VK_ENTITY,
        )
        self.assertGreaterEqual(
            VK_ENTITY.count("VK_Entity_MarkCurrentSubmitPass(alpha, vertex_flags);"),
            3,
        )
        self.assertIn("VK_Entity_MarkCurrentSubmitPass(alpha, a->flags);", VK_ENTITY)
        self.assertIn("VK_Entity_MarkCurrentSubmitPhase();", VK_ENTITY)

    def test_liquid_record_phases_are_mapped_to_the_native_submit_phases(self) -> None:
        self.assertIn("static bool VK_Entity_HasRecordPhase(", VK_ENTITY)
        self.assertIn("case VK_ENTITY_RECORD_BEFORE_LIQUID:", VK_ENTITY)
        self.assertIn("BIT(VK_ENTITY_SUBMIT_OPAQUE)", VK_ENTITY)
        self.assertIn("BIT(VK_ENTITY_SUBMIT_ALPHA_BACK)", VK_ENTITY)
        self.assertIn("case VK_ENTITY_RECORD_POST_LIQUID:", VK_ENTITY)
        self.assertIn("BIT(VK_ENTITY_SUBMIT_POST_LIQUID)", VK_ENTITY)
        self.assertIn("case VK_ENTITY_RECORD_ALPHA_FRONT:", VK_ENTITY)
        self.assertIn("BIT(VK_ENTITY_SUBMIT_ALPHA_FRONT)", VK_ENTITY)

    def test_empty_phase_work_is_elided_from_scene_and_bloom_recording(self) -> None:
        self.assertGreaterEqual(
            VK_ENTITY.count("if (!VK_Entity_HasRecordPhase(phase)) {"), 3
        )
        self.assertIn(
            "!VK_Entity_HasRecordPhase(VK_ENTITY_RECORD_ALPHA_FRONT)",
            VK_ENTITY,
        )
        self.assertIn("static uint8_t VK_Entity_RecordPassMask(", VK_ENTITY)
        self.assertIn(
            "const uint8_t record_pass_mask = VK_Entity_RecordPassMask(phase);",
            VK_ENTITY,
        )
        self.assertIn("if ((record_pass_mask & BIT(pass)) == 0) {", VK_ENTITY)
        self.assertNotIn('#include "rend_gl', VK_ENTITY)


if __name__ == "__main__":
    unittest.main()
