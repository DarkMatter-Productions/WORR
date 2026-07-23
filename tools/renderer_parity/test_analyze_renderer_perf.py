#!/usr/bin/env python3
"""Tests for renderer telemetry comparison parsing and aggregation."""

from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("analyze_renderer_perf.py")
ROOT = SCRIPT.parents[2]
DENSE_INLINE_BSP_GPU_BUDGET = json.loads(
    (ROOT / "assets/renderer_parity/fr01_renderer_perf_bmodel_instances_gpu_budget.json")
    .read_text(encoding="utf-8")
)
DENSE_INLINE_BSP_CPU_SUBMISSION_BUDGET = json.loads(
    (ROOT / "assets/renderer_parity/fr01_renderer_perf_bmodel_instances_cpu_submission_budget.json")
    .read_text(encoding="utf-8")
)
SPEC = importlib.util.spec_from_file_location("renderer_perf", SCRIPT)
assert SPEC and SPEC.loader
PERF = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PERF)


class RendererPerfTests(unittest.TestCase):
    @staticmethod
    def write_paired_logs(root: Path) -> tuple[Path, Path]:
        vk = root / "vk.log"
        gl = root / "gl.log"
        vk.write_text(
            "VK_STATS frame=1 draws=10 uploads=100 cpu_ms=4.0 gpu_ms=3.0 gpu_valid=1\n",
            encoding="utf-8",
        )
        gl.write_text(
            "GL_STATS frame=1 draws=10 uploads=100 cpu_ms=5.0 gpu_ms=4.0 gpu_valid=1\n",
            encoding="utf-8",
        )
        return vk, gl

    def test_parse_and_summarize_like_for_like_stats(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vk = root / "vk.log"
            gl = root / "gl.log"
            vk.write_text(
                "VK_STATS frame=1 draws=10 post_draws=2 uploads=100 cpu_ms=4.0 gpu_ms=3.0 gpu_frame_ms=3.0 gpu_opaque_world_ms=1.0 gpu_opaque_entity_ms=0.5 gpu_scene_ms=0.75 gpu_post_ms=2.0 world_fast_lit_draws=1 world_fast_lit_no_fog_draws=1 world_texture_replace_draws=2 world_texture_replace_no_fog_draws=2 msaa_depth_resolve_elisions=1 msaa_single_sample_dof_scene_frames=0 msaa_single_sample_scaled_scene_frames=0 entity_fast_lit_draws=2 entity_fast_lit_no_fog_draws=2 entity_texture_replace_draws=3 entity_texture_replace_no_fog_draws=3 world_fast_lit_candidates=4 world_fast_lit_material_ineligible=1 gpu_frame_valid=1 gpu_valid=1\n"
                "VK_STATS frame=2 draws=12 post_draws=4 uploads=120 cpu_ms=2.0 gpu_ms=1.0 gpu_frame_ms=1.0 gpu_opaque_world_ms=0.5 gpu_opaque_entity_ms=0.25 gpu_scene_ms=0.25 gpu_post_ms=1.0 world_fast_lit_draws=3 world_fast_lit_no_fog_draws=3 world_texture_replace_draws=4 world_texture_replace_no_fog_draws=4 msaa_depth_resolve_elisions=3 msaa_single_sample_dof_scene_frames=1 msaa_single_sample_scaled_scene_frames=1 entity_fast_lit_draws=4 entity_fast_lit_no_fog_draws=4 entity_texture_replace_draws=5 entity_texture_replace_no_fog_draws=5 world_fast_lit_candidates=6 world_fast_lit_material_ineligible=3 gpu_frame_valid=1 gpu_valid=1\n",
                encoding="utf-8",
            )
            gl.write_text(
                "GL_STATS frame=1 draws=10 uploads=100 cpu_ms=5.0 gpu_ms=4.0 gpu_frame_ms=2.0 gpu_post_ms=3.0 gpu_frame_valid=1 gpu_valid=1\n"
                "GL_STATS frame=2 draws=12 uploads=120 cpu_ms=4.0 gpu_ms=2.0 gpu_frame_ms=1.0 gpu_post_ms=1.0 gpu_frame_valid=1 gpu_valid=1\n",
                encoding="utf-8",
            )
            vk_summary = PERF.summarize(PERF.parse_stats(vk, "VK_STATS"), 0)
            gl_summary = PERF.summarize(PERF.parse_stats(gl, "GL_STATS"), 0)
            self.assertEqual(vk_summary["samples"], 2)
            self.assertEqual(vk_summary["gpu_valid_samples"], 2)
            self.assertEqual(vk_summary["gpu_frame_valid_samples"], 2)
            self.assertEqual(vk_summary["cpu_ms_mean"], 3.0)
            self.assertEqual(vk_summary["gpu_frame_ms_p50"], 2.0)
            self.assertEqual(vk_summary["post_draws_mean"], 3.0)
            self.assertEqual(vk_summary["world_fast_lit_draws_mean"], 2.0)
            self.assertEqual(vk_summary["world_fast_lit_no_fog_draws_mean"], 2.0)
            self.assertEqual(vk_summary["world_texture_replace_draws_mean"], 3.0)
            self.assertEqual(
                vk_summary["world_texture_replace_no_fog_draws_mean"], 3.0
            )
            self.assertEqual(vk_summary["msaa_depth_resolve_elisions_mean"], 2.0)
            self.assertEqual(vk_summary["msaa_single_sample_dof_scene_frames_mean"], 0.5)
            self.assertEqual(vk_summary["msaa_single_sample_scaled_scene_frames_mean"], 0.5)
            self.assertEqual(vk_summary["entity_fast_lit_draws_mean"], 3.0)
            self.assertEqual(vk_summary["entity_fast_lit_no_fog_draws_mean"], 3.0)
            self.assertEqual(vk_summary["entity_texture_replace_draws_mean"], 4.0)
            self.assertEqual(
                vk_summary["entity_texture_replace_no_fog_draws_mean"], 4.0
            )
            self.assertEqual(vk_summary["world_fast_lit_candidates_mean"], 5.0)
            self.assertEqual(
                vk_summary["world_fast_lit_material_ineligible_mean"], 2.0
            )
            self.assertEqual(vk_summary["gpu_opaque_world_ms_mean"], 0.75)
            self.assertEqual(vk_summary["gpu_opaque_entity_ms_mean"], 0.375)
            self.assertEqual(vk_summary["gpu_scene_ms_mean"], 0.5)
            self.assertEqual(vk_summary["gpu_post_ms_mean"], 1.5)
            ratios = PERF.ratios(vk_summary, gl_summary)
            self.assertEqual(ratios["gpu_ms_mean"], 2 / 3)
            self.assertEqual(ratios["gpu_frame_ms_mean"], 4 / 3)
            self.assertEqual(ratios["gpu_post_ms_mean"], 0.75)

    def test_summarizes_comparable_cpu_render_and_vulkan_sync_metrics(self) -> None:
        records = [
            {"cpu_ms": 8.0, "cpu_render_ms": 1.0, "cpu_sync_wait_ms": 7.0},
            {"cpu_ms": 6.0, "cpu_render_ms": 2.0, "cpu_sync_wait_ms": 4.0},
        ]
        summary = PERF.summarize(records, warmup=0)
        self.assertEqual(summary["cpu_render_ms_mean"], 1.5)
        self.assertEqual(summary["cpu_sync_wait_ms_p50"], 5.5)

    def test_warmup_cannot_remove_all_samples(self) -> None:
        with self.assertRaisesRegex(ValueError, "after warmup"):
            PERF.summarize([{"cpu_ms": 1.0}], 1)

    def test_budget_rejects_regression_and_missing_gpu_samples(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            budget = Path(temp) / "budget.json"
            budget.write_text(json.dumps({
                "schema_version": 1,
                "min_samples": 30,
                "require_gpu_valid": True,
                "vulkan_max": {"cpu_ms_mean": 3.5},
                "vulkan_over_opengl_max": {"gpu_ms_mean": 0.95},
            }), encoding="utf-8")
            result = {
                "vulkan": {"samples": 30, "gpu_valid_samples": 29, "cpu_ms_mean": 4.0},
                "opengl": {"samples": 30, "gpu_valid_samples": 30},
                "ratios_vulkan_over_opengl": {"gpu_ms_mean": 1.10},
            }
            failures = PERF.evaluate_budget(result, budget)
            self.assertEqual(len(failures), 3)
            self.assertIn("vulkan is missing valid GPU timing", failures[0])
            self.assertIn("Vulkan cpu_ms_mean=4.0000", failures[1])
            self.assertIn("gpu_ms_mean ratio=1.1000", failures[2])

    def test_budget_requires_valid_full_frame_gpu_timing_when_requested(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            budget = Path(temp) / "budget.json"
            budget.write_text(json.dumps({
                "schema_version": 1,
                "require_gpu_frame_valid": True,
                "vulkan_over_opengl_max": {"gpu_frame_ms_mean": 1.1},
            }), encoding="utf-8")
            result = {
                "vulkan": {"samples": 30, "gpu_frame_valid_samples": 29},
                "opengl": {"samples": 30, "gpu_frame_valid_samples": 30},
                "ratios_vulkan_over_opengl": {"gpu_frame_ms_mean": 1.2},
            }
            failures = PERF.evaluate_budget(result, budget)
            self.assertEqual(len(failures), 2)
            self.assertIn("vulkan is missing valid full-frame GPU timing", failures[0])
            self.assertIn("gpu_frame_ms_mean ratio=1.2000", failures[1])

    def test_dense_inline_bsp_gpu_budget_is_provenance_bound(self) -> None:
        self.assertEqual(DENSE_INLINE_BSP_GPU_BUDGET["schema_version"], 1)
        self.assertEqual(DENSE_INLINE_BSP_GPU_BUDGET["min_samples"], 100)
        self.assertTrue(DENSE_INLINE_BSP_GPU_BUDGET["require_gpu_valid"])
        self.assertTrue(DENSE_INLINE_BSP_GPU_BUDGET["require_gpu_frame_valid"])
        contract = DENSE_INLINE_BSP_GPU_BUDGET["capture_contract"]
        self.assertEqual(contract["scenario_id"], "fr01-bmodel-instance-grid-current")
        self.assertEqual(contract["hardware_id"], "Intel(R) Iris(R) Xe Graphics")
        self.assertEqual(contract["driver"], "local-headless")
        limits = DENSE_INLINE_BSP_GPU_BUDGET["vulkan_max"]
        self.assertEqual(limits["gpu_frame_ms_p50"], 0.65)
        self.assertEqual(limits["draws_p95"], 18)
        self.assertEqual(limits["uploads_p95"], 4800)

    def test_dense_inline_bsp_cpu_submission_budget_is_provenance_bound(self) -> None:
        self.assertEqual(DENSE_INLINE_BSP_CPU_SUBMISSION_BUDGET["schema_version"], 1)
        self.assertEqual(DENSE_INLINE_BSP_CPU_SUBMISSION_BUDGET["min_samples"], 100)
        contract = DENSE_INLINE_BSP_CPU_SUBMISSION_BUDGET["capture_contract"]
        self.assertEqual(contract["scenario_id"], "fr01-bmodel-instance-grid-current")
        self.assertEqual(contract["hardware_id"], "Intel(R) Iris(R) Xe Graphics")
        self.assertEqual(contract["driver"], "local-headless")
        limits = DENSE_INLINE_BSP_CPU_SUBMISSION_BUDGET["vulkan_max"]
        self.assertEqual(limits["cpu_render_ms_mean"], 1.75)
        self.assertEqual(limits["cpu_render_ms_p95"], 3.25)
        ratios = DENSE_INLINE_BSP_CPU_SUBMISSION_BUDGET["vulkan_over_opengl_max"]
        self.assertEqual(ratios["cpu_render_ms_mean"], 0.6)
        self.assertEqual(ratios["cpu_render_ms_p95"], 0.6)

    def test_budget_rejects_capture_contract_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            budget = Path(temp) / "budget.json"
            budget.write_text(json.dumps({
                "schema_version": 1,
                "capture_contract": {
                    "scenario_id": "fr01-grid",
                    "fixture_sha256": "a" * 64,
                    "config_sha256": "b" * 64,
                    "hardware_id": "gpu-1234",
                    "driver": "driver-a",
                },
            }), encoding="utf-8")
            result = {
                "vulkan": {"samples": 1, "gpu_valid_samples": 1},
                "opengl": {"samples": 1, "gpu_valid_samples": 1},
                "ratios_vulkan_over_opengl": {},
                "capture_manifest": {
                    "scenario_id": "fr01-grid",
                    "fixture_sha256": "a" * 64,
                    "config_sha256": "b" * 64,
                    "hardware_id": "gpu-other",
                    "driver": "driver-a",
                },
            }
            failures = PERF.evaluate_budget(result, budget)
            self.assertEqual(1, len(failures))
            self.assertIn("capture hardware_id='gpu-other'", failures[0])

    def test_capture_manifest_binds_logs_to_one_reproducible_scenario(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vk, gl = self.write_paired_logs(root)
            manifest = root / "capture.json"
            manifest.write_text(json.dumps({
                "schema_version": 1,
                "scenario": {
                    "id": "fr01-model-burst",
                    "fixture_sha256": "a" * 64,
                    "config_sha256": "b" * 64,
                },
                "environment": {
                    "hardware_id": "gpu-1234",
                    "driver": "test-driver",
                },
                "vulkan": {
                    "renderer": "vulkan",
                    "log_sha256": PERF.file_sha256(vk),
                },
                "opengl": {
                    "renderer": "opengl",
                    "log_sha256": PERF.file_sha256(gl),
                },
            }), encoding="utf-8")
            evidence = PERF.validate_capture_manifest(manifest, vk, gl)
            self.assertEqual(evidence["scenario_id"], "fr01-model-burst")
            self.assertEqual(evidence["vulkan_log_sha256"], PERF.file_sha256(vk))

    def test_capture_manifest_rejects_a_log_changed_after_collection(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vk, gl = self.write_paired_logs(root)
            manifest = root / "capture.json"
            manifest.write_text(json.dumps({
                "schema_version": 1,
                "scenario": {
                    "id": "fr01-model-burst",
                    "fixture_sha256": "a" * 64,
                    "config_sha256": "b" * 64,
                },
                "environment": {
                    "hardware_id": "gpu-1234",
                    "driver": "test-driver",
                },
                "vulkan": {
                    "renderer": "vulkan",
                    "log_sha256": PERF.file_sha256(vk),
                },
                "opengl": {
                    "renderer": "opengl",
                    "log_sha256": PERF.file_sha256(gl),
                },
            }), encoding="utf-8")
            vk.write_text("VK_STATS frame=2 draws=20 uploads=200 cpu_ms=9.0 gpu_ms=8.0 gpu_valid=1\n",
                          encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "log hash"):
                PERF.validate_capture_manifest(manifest, vk, gl)

    def test_budget_requires_paired_capture_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vk, gl = self.write_paired_logs(root)
            budget = root / "budget.json"
            budget.write_text(json.dumps({
                "schema_version": 1,
                "min_samples": 1,
                "vulkan_over_opengl_max": {"cpu_ms_mean": 2.0},
            }), encoding="utf-8")
            result = PERF.main([
                "--vulkan", str(vk), "--opengl", str(gl),
                "--min-samples", "1", "--budget", str(budget),
            ])
            self.assertEqual(result, 2)


if __name__ == "__main__":
    unittest.main()
