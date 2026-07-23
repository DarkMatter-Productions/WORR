#!/usr/bin/env python3
"""Lock the shared Video MSAA control to native OpenGL and Vulkan paths."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GL_MAIN = (ROOT / "src/rend_gl/main.c").read_text(encoding="utf-8")
VK_MAIN = (ROOT / "src/rend_vk/vk_main.c").read_text(encoding="utf-8")
VK_LOCAL = (ROOT / "src/rend_vk/vk_local.h").read_text(encoding="utf-8")
VK_UI = (ROOT / "src/rend_vk/vk_ui.c").read_text(encoding="utf-8")
VK_WORLD = (ROOT / "src/rend_vk/vk_world.c").read_text(encoding="utf-8")
VK_ENTITY = (ROOT / "src/rend_vk/vk_entity.c").read_text(encoding="utf-8")
VK_DEBUG = (ROOT / "src/rend_vk/vk_debug.c").read_text(encoding="utf-8")
LEGACY_MENU = (ROOT / "src/client/ui/worr.menu").read_text(encoding="utf-8")
C_GAME_MENU = json.loads(
    (ROOT / "src/game/cgame/ui/worr.json").read_text(encoding="utf-8")
)
RML_VIDEO = (ROOT / "assets/ui/rml/settings/video.rml").read_text(encoding="utf-8")
MSAA_CONFIG = (ROOT / "assets/renderer_parity/fr01_multisample_static_world.cfg").read_text(
    encoding="utf-8"
)
MSAA_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_multisample_static_world_manifest.json").read_text(
        encoding="utf-8"
    )
)
MSAA_DOF_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_multisample_depth_dof_manifest.json").read_text(
        encoding="utf-8"
    )
)
MSAA_DOF_MATRIX_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_multisample_depth_dof_matrix_manifest.json").read_text(
        encoding="utf-8"
    )
)
MSAA_DOF_SCALE_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_multisample_depth_dof_resolution_scale_manifest.json").read_text(
        encoding="utf-8"
    )
)
MSAA_DOF_SCALE_MENU_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_multisample_depth_dof_resolution_scale_menu_manifest.json").read_text(
        encoding="utf-8"
    )
)
MSAA_DOF_SCALE_HDR_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_hdr_multisample_depth_dof_resolution_scale_manifest.json").read_text(
        encoding="utf-8"
    )
)
MSAA_DOF_REFRACTION_CONFIG = (
    ROOT / "assets/renderer_parity/fr01_dof_refraction.cfg"
).read_text(encoding="utf-8")
MSAA_DOF_REFRACTION_MANIFEST = json.loads(
    (ROOT / "assets/renderer_parity/fr01_multisample_depth_dof_refraction_manifest.json")
    .read_text(encoding="utf-8")
)


class SharedMultisampleControlSourceTests(unittest.TestCase):
    def test_every_video_ui_binds_the_shared_renderer_cvar(self) -> None:
        self.assertIn('pairs "anti-aliasing" r_multisamples', LEGACY_MENU)
        self.assertNotIn('pairs "anti-aliasing" gl_multisamples', LEGACY_MENU)

        video_menu = next(
            menu for menu in C_GAME_MENU["menus"] if menu.get("name") == "video"
        )
        anti_aliasing = next(
            item
            for item in video_menu["items"]
            if item.get("label") == "anti-aliasing"
        )
        self.assertEqual(anti_aliasing["cvar"], "r_multisamples")
        self.assertIn('data-cvar="r_multisamples"', RML_VIDEO)
        self.assertNotIn('data-cvar="gl_multisamples"', RML_VIDEO)

    def test_opengl_uses_the_shared_cvar_and_keeps_legacy_config_compatibility(self) -> None:
        self.assertIn('Cvar_Get("r_multisamples", gl_multisamples->string,', GL_MAIN)
        self.assertIn('CVAR_ARCHIVE | CVAR_RENDERER', GL_MAIN)
        self.assertIn('CVAR_RENDERER | CVAR_NOARCHIVE', GL_MAIN)
        self.assertIn("gl_sync_multisample_defaults", GL_MAIN)
        self.assertIn("gl_multisample_changed", GL_MAIN)
        self.assertIn("gl_register_multisample_cvars", GL_MAIN)
        self.assertIn(".multisamples = Cvar_ClampInteger(r_multisamples, 0, 32)", GL_MAIN)
        self.assertIn('Com_Printf("GL_RENDERER: %s\\n", qglGetString(GL_RENDERER));', GL_MAIN)
        self.assertIn("OpenGL: framebuffer MSAA %dx%s", GL_MAIN)

    def test_vulkan_selects_a_supported_native_sample_count(self) -> None:
        self.assertIn("static void VK_RegisterMultisampleCvars(void)", VK_MAIN)
        self.assertIn('Cvar_Get("r_multisamples",', VK_MAIN)
        self.assertIn("static void VK_MultisampleChanged", VK_MAIN)
        self.assertIn("VK_SelectSceneSamples", VK_MAIN)
        self.assertIn("framebufferColorSampleCounts", VK_MAIN)
        self.assertIn("framebufferDepthSampleCounts", VK_MAIN)
        self.assertIn("VK_ImageSampleCounts", VK_MAIN)
        self.assertIn("VK_SAMPLE_COUNT_8_BIT", VK_MAIN)
        self.assertIn("Cvar_SetByVar(vk_r_multisamples", VK_MAIN)
        self.assertIn(".multisamples = vk_state.initialized", VK_MAIN)

    def test_vulkan_resolves_native_multisample_color_and_depth(self) -> None:
        self.assertIn("VkImage msaa_color_image;", VK_LOCAL)
        self.assertIn("VkImage msaa_depth_image;", VK_LOCAL)
        self.assertIn("VkFramebuffer *msaa_framebuffers;", VK_LOCAL)
        self.assertIn("VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME", VK_MAIN)
        self.assertIn("VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME", VK_MAIN)
        self.assertIn("VkSubpassDescriptionDepthStencilResolve", VK_MAIN)
        self.assertIn("VK_RESOLVE_MODE_SAMPLE_ZERO_BIT", VK_MAIN)
        self.assertIn("VK_CreateMSAASceneRenderPass", VK_MAIN)
        self.assertIn("pResolveAttachments = &color_resolve_ref", VK_MAIN)
        self.assertIn("pDepthStencilResolveAttachment = resolve_depth", VK_MAIN)
        self.assertIn("scene_no_depth_resolve_render_pass", VK_MAIN)
        self.assertIn("VK_RESOLVE_MODE_NONE", VK_MAIN)
        self.assertIn("elide_msaa_depth_resolve", VK_MAIN)
        self.assertIn("!liquid_refraction && !depth_dof && !sampled_rim_bloom", VK_MAIN)
        self.assertIn("VK_Debug_RecordMSAADepthResolveElision", VK_MAIN)
        self.assertIn("msaa_depth_resolve_elisions", VK_DEBUG)
        self.assertIn("msaa_single_sample_dof_scene_frames", VK_DEBUG)
        self.assertIn("msaa_single_sample_scaled_scene_frames", VK_DEBUG)

    def test_headless_static_world_fixture_requests_four_samples_before_startup(self) -> None:
        self.assertIn("map worr_fr01_world_texture_replace", MSAA_CONFIG)
        self.assertIn("screenshottga fr01_multisample_static_world", MSAA_CONFIG)
        self.assertEqual(MSAA_MANIFEST["task_id"], "FR-02-T13")
        scene = MSAA_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "native_multisample_static_world")
        self.assertEqual(scene["launch_cvars"], {"r_multisamples": "4"})
        self.assertEqual(scene["crop"], [200, 140, 560, 420])

    def test_depth_consuming_dof_fixture_keeps_four_samples_and_dof_active(self) -> None:
        self.assertEqual(MSAA_DOF_MANIFEST["task_id"], "FR-02-T13")
        scene = MSAA_DOF_MANIFEST["scenes"][0]
        self.assertEqual(scene["id"], "native_multisample_depth_aware_dof")
        self.assertEqual(scene["launch_cvars"], {
            "r_dof": "1",
            "r_multisamples": "4",
        })

    def test_dof_matrix_preserves_strict_metrics_at_four_samples(self) -> None:
        self.assertEqual(MSAA_DOF_MATRIX_MANIFEST["task_id"], "FR-02-T13")
        self.assertEqual(len(MSAA_DOF_MATRIX_MANIFEST["scenes"]), 8)
        for scene in MSAA_DOF_MATRIX_MANIFEST["scenes"]:
            self.assertEqual(scene["launch_cvars"]["r_multisamples"], "4")
            self.assertEqual(scene["metrics"]["pixel_threshold"], 1)
            self.assertEqual(scene["metrics"]["max_pixels_over_threshold_percent"], 0)

    def test_scaled_dof_fixture_retains_the_shared_four_sample_request(self) -> None:
        self.assertEqual(MSAA_DOF_SCALE_MANIFEST["task_id"], "FR-02-T13")
        self.assertEqual(len(MSAA_DOF_SCALE_MANIFEST["scenes"]), 2)
        scene, disabled = MSAA_DOF_SCALE_MANIFEST["scenes"]
        self.assertEqual(scene["launch_cvars"]["r_multisamples"], "4")
        self.assertEqual(scene["launch_cvars"]["r_resolutionscale"], "1")
        self.assertEqual(scene["launch_cvars"]["r_resolutionscale_fixedscale_w"], "0.5")
        self.assertEqual(scene["launch_cvars"]["r_resolutionscale_fixedscale_h"], "0.5")
        self.assertEqual(scene["metrics"]["max_pixels_over_threshold_percent"], 0.3)
        self.assertEqual(disabled["launch_cvars"]["r_dof"], "0")
        self.assertEqual(disabled["metrics"]["max_pixels_over_threshold_percent"], 0)

    def test_scaled_menu_dof_fixture_covers_the_explicit_rectangle_and_control(self) -> None:
        self.assertEqual(MSAA_DOF_SCALE_MENU_MANIFEST["task_id"], "FR-02-T13")
        self.assertEqual(len(MSAA_DOF_SCALE_MENU_MANIFEST["scenes"]), 2)
        scene, disabled = MSAA_DOF_SCALE_MENU_MANIFEST["scenes"]
        self.assertIn("menu_rect", scene["id"])
        self.assertEqual(scene["launch_cvars"]["r_multisamples"], "4")
        self.assertEqual(scene["launch_cvars"]["r_resolutionscale_fixedscale_w"], "0.5")
        self.assertEqual(scene["metrics"]["max_pixels_over_threshold_percent"], 0.3)
        self.assertEqual(disabled["launch_cvars"]["r_dof"], "0")
        self.assertEqual(disabled["metrics"]["max_pixels_over_threshold_percent"], 0)

    def test_scaled_hdr_dof_fixture_keeps_hdr_and_msaa_contracts(self) -> None:
        self.assertEqual(MSAA_DOF_SCALE_HDR_MANIFEST["task_id"], "FR-02-T13")
        self.assertEqual(len(MSAA_DOF_SCALE_HDR_MANIFEST["scenes"]), 2)
        scene, disabled = MSAA_DOF_SCALE_HDR_MANIFEST["scenes"]
        self.assertTrue(scene["id"].startswith("float_hdr_"))
        self.assertEqual(scene["config"], "renderer_parity/fr01_hdr_dof.cfg")
        self.assertEqual(scene["launch_cvars"]["r_dof"], "1")
        self.assertEqual(scene["launch_cvars"]["r_multisamples"], "4")
        self.assertEqual(scene["launch_cvars"]["r_resolutionscale"], "1")
        self.assertEqual(scene["metrics"]["pixel_threshold"], 1)
        self.assertEqual(scene["metrics"]["max_pixels_over_threshold_percent"], 0)
        self.assertEqual(disabled["launch_cvars"]["r_dof"], "0")
        self.assertEqual(disabled["metrics"]["max_pixels_over_threshold_percent"], 0)

    def test_dof_refraction_fixture_is_gun_free_and_keeps_four_samples(self) -> None:
        self.assertIn("set cl_gun 0", MSAA_DOF_REFRACTION_CONFIG)
        map_marker = "map worr_fr01_viewweapon_shell_bloom_refraction\nwait 60\n"
        refraction_marker = (
            "set gl_warp_refraction 0.1\n"
            "set vk_warp_refraction 0.1\n"
            "wait 10\n"
        )
        self.assertIn("set gl_warp_refraction 0\n", MSAA_DOF_REFRACTION_CONFIG)
        self.assertIn("set vk_warp_refraction 0\n", MSAA_DOF_REFRACTION_CONFIG)
        self.assertLess(
            MSAA_DOF_REFRACTION_CONFIG.index(map_marker),
            MSAA_DOF_REFRACTION_CONFIG.index(refraction_marker),
        )
        self.assertEqual(MSAA_DOF_REFRACTION_MANIFEST["task_id"], "FR-02-T13")
        scene = MSAA_DOF_REFRACTION_MANIFEST["scenes"][0]
        self.assertEqual(scene["launch_cvars"], {
            "r_dof": "1",
            "r_multisamples": "4",
        })
        self.assertEqual(scene["metrics"]["pixel_threshold"], 1)
        self.assertEqual(scene["metrics"]["max_pixels_over_threshold_percent"], 0.5)

    def test_scene_pipelines_and_ui_match_the_native_scene_sample_count(self) -> None:
        self.assertIn(": ctx->scene_samples", VK_WORLD)
        self.assertIn(": ctx->scene_samples", VK_ENTITY)
        self.assertIn(".rasterizationSamples = ctx->scene_samples", VK_DEBUG)
        self.assertIn("VkPipeline scene_pipeline;", VK_UI)
        self.assertIn("VkPipeline scene_showtris_pipeline;", VK_UI)
        self.assertIn("multisample.rasterizationSamples = ctx->scene_samples", VK_UI)
        self.assertIn("VK_UI_RecordScene", VK_UI)
        self.assertIn("VK_UI_RecordScene(cmd, &ctx->swapchain.extent)", VK_MAIN)
        self.assertIn("entity_overlay && !linear_scene && scene_multisampled", VK_MAIN)
        self.assertIn("scene_single_sample_render_pass", VK_MAIN)
        self.assertIn("scene_single_sample_load_render_pass", VK_MAIN)
        self.assertIn("VK_RegisterScenePipelineVariant", VK_MAIN)
        self.assertIn("VK_SelectScenePipeline", VK_MAIN)
        self.assertIn("scene_single_sample_active", VK_LOCAL)
        self.assertIn("linear_scene_single_sample_framebuffer", VK_LOCAL)
        self.assertIn("VK_Debug_RecordMSAASingleSampleDofScene", VK_MAIN)
        self.assertIn("single_sample_scaled_scene", VK_MAIN)
        self.assertIn("VK_Debug_RecordMSAASingleSampleScaledScene", VK_MAIN)


if __name__ == "__main__":
    unittest.main()
