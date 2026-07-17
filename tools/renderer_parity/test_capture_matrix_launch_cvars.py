#!/usr/bin/env python3
"""Regression checks for renderer capture launch-cvar validation."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import compare_captures
import run_capture_matrix


class CaptureMatrixLaunchCvarTests(unittest.TestCase):
    def manifest(self, directory: Path, launch_cvars: object) -> Path:
        path = directory / "manifest.json"
        path.write_text(json.dumps({
            "schema_version": 1,
            "scenes": [{
                "id": "fixture",
                "config": "renderer_parity/fixture.cfg",
                "capture": "fixture.tga",
                "launch_cvars": launch_cvars,
            }],
        }), encoding="utf-8")
        return path

    def test_manifest_accepts_latched_feature_override(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            scene = run_capture_matrix.load_manifest(
                self.manifest(Path(temp), {"r_dof": "1"})
            )["scenes"][0]
        self.assertEqual((("r_dof", "1"),),
                         run_capture_matrix.scene_launch_cvars(scene))

    def test_manifest_rejects_multiline_launch_value(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(compare_captures.CaptureError):
                run_capture_matrix.load_manifest(
                    self.manifest(Path(temp), {"r_dof": "1\nquit"})
                )

    def test_manifest_rejects_non_cvar_launch_name(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(compare_captures.CaptureError):
                run_capture_matrix.load_manifest(
                    self.manifest(Path(temp), {"r dof": "1"})
                )


if __name__ == "__main__":
    unittest.main()
