#!/usr/bin/env python3
"""Run repository-owned renderer scenes and apply their parity thresholds."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

import compare_captures


RENDERERS = ("opengl", "vulkan")


def scene_launch_cvars(scene: dict[str, Any]) -> tuple[tuple[str, str], ...]:
    raw = scene.get("launch_cvars", {})
    if raw is None:
        return ()
    if not isinstance(raw, dict):
        raise compare_captures.CaptureError(
            f"{scene['id']}: launch_cvars must be an object"
        )
    result: list[tuple[str, str]] = []
    for name, value in raw.items():
        if (not isinstance(name, str) or not name or not name.isascii() or
                not all(char.isalnum() or char == "_" for char in name)):
            raise compare_captures.CaptureError(
                f"{scene['id']}: launch cvar name must be ASCII alphanumeric/underscore"
            )
        if (not isinstance(value, str) or "\0" in value or "\r" in value or
                "\n" in value):
            raise compare_captures.CaptureError(
                f"{scene['id']}: launch cvar value must be a single line string"
            )
        result.append((name, value))
    return tuple(result)


def load_manifest(path: Path) -> dict[str, Any]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict) or raw.get("schema_version") != 1:
        raise compare_captures.CaptureError("manifest schema_version must be 1")
    scenes = raw.get("scenes")
    if not isinstance(scenes, list) or not scenes:
        raise compare_captures.CaptureError("manifest scenes must be a non-empty array")
    for scene in scenes:
        if not isinstance(scene, dict):
            raise compare_captures.CaptureError("manifest scene must be an object")
        for key in ("id", "config", "capture"):
            value = scene.get(key)
            if not isinstance(value, str) or not value:
                raise compare_captures.CaptureError(f"scene {key} must be non-empty")
        if Path(scene["config"]).is_absolute() or ".." in Path(scene["config"]).parts:
            raise compare_captures.CaptureError(
                f"{scene['id']}: config must be a game-relative path"
            )
        if Path(scene["capture"]).name != scene["capture"]:
            raise compare_captures.CaptureError(
                f"{scene['id']}: capture must be a plain filename"
            )
        scene_launch_cvars(scene)
    return raw


def discover_executable(install_dir: Path, override: Path | None) -> Path:
    if override:
        executable = override.resolve()
        if not executable.is_file():
            raise compare_captures.CaptureError(f"executable not found: {executable}")
        return executable

    candidates = (
        "worr_x86_64.exe",
        "worr.exe",
        "worr_x86_64",
        "worr",
    )
    for name in candidates:
        candidate = install_dir / name
        if candidate.is_file():
            return candidate
    raise compare_captures.CaptureError(
        f"no WORR client executable found under {install_dir}"
    )


def _contained(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def run_capture(
    executable: Path,
    install_dir: Path,
    run_root: Path,
    renderer: str,
    scene: dict[str, Any],
    timeout_seconds: float,
    validation: bool,
) -> tuple[Path | None, str | None]:
    scene_id = scene["id"]
    home = (run_root / "homes" / renderer / scene_id).resolve()
    if not _contained(home, run_root):
        raise compare_captures.CaptureError("computed capture home escaped run root")
    home.mkdir(parents=True, exist_ok=True)

    source_capture = home / "screenshots" / scene["capture"]
    destination = run_root / "captures" / renderer / scene["capture"]
    if source_capture.exists():
        if not _contained(source_capture, run_root):
            raise compare_captures.CaptureError("capture cleanup escaped run root")
        source_capture.unlink()
    if destination.exists():
        if not _contained(destination, run_root):
            raise compare_captures.CaptureError("result cleanup escaped run root")
        destination.unlink()

    command = [
        str(executable),
        "+set",
        "basedir",
        str(install_dir),
        "+set",
        "homedir",
        str(home),
        "+set",
        "game",
        "basew",
        "+set",
        "r_fullscreen",
        "0",
        "+set",
        "win_headless",
        "1",
        "+set",
        "in_enable",
        "0",
        "+set",
        "in_grab",
        "0",
        "+set",
        "r_geometry",
        "960x720",
        "+set",
        "bot_enable",
        "0",
        "+set",
        "r_screenshot_dir",
        str(source_capture.parent),
        "+set",
        "vid_renderer",
        renderer,
        "+set",
        "r_renderer",
        renderer,
        "+set",
        "s_enable",
        "0",
        "+set",
        "r_dof",
        "0",
    ]
    for name, value in scene_launch_cvars(scene):
        command.extend(("+set", name, value))
    command.extend([
        "+set",
        "ui_rml_enable",
        "0",
        "+set",
        "logfile_name",
        f"renderer_parity_{scene_id}_{renderer}",
        "+set",
        "logfile",
        "1",
        "+exec",
        scene["config"],
    ])
    environment = os.environ.copy()
    if validation and renderer == "vulkan":
        environment["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"

    try:
        completed = subprocess.run(
            command,
            cwd=install_dir,
            env=environment,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout_seconds,
            check=False,
            creationflags=(getattr(subprocess, "CREATE_NO_WINDOW", 0)
                           if os.name == "nt" else 0),
        )
    except subprocess.TimeoutExpired as exc:
        raise compare_captures.CaptureError(
            f"{scene_id}/{renderer}: timed out after {timeout_seconds:g}s"
        ) from exc

    log_dir = run_root / "process-logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    process_log = log_dir / f"{scene_id}-{renderer}.log"
    process_log.write_text(
        "COMMAND\n"
        + subprocess.list2cmdline(command)
        + "\n\nSTDOUT\n"
        + completed.stdout
        + "\nSTDERR\n"
        + completed.stderr,
        encoding="utf-8",
    )
    if not source_capture.is_file():
        suffix = (
            f" after client exit {completed.returncode}"
            if completed.returncode != 0
            else ""
        )
        return None, (
            f"{scene_id}/{renderer}: expected capture missing{suffix}: "
            f"{source_capture}; see {process_log}"
        )

    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_capture, destination)
    if completed.returncode != 0:
        return destination, (
            f"{scene_id}/{renderer}: capture completed, but client exited "
            f"{completed.returncode}; see {process_log}"
        )
    return destination, None


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, default=Path(".install"))
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("assets/renderer_parity/fr01_alias_outline_manifest.json"),
    )
    parser.add_argument(
        "--run-root", type=Path, default=Path(".tmp/renderer-parity/fr01-alias-outline")
    )
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--compare-only", action="store_true")
    parser.add_argument("--vulkan-validation", action="store_true")
    parser.add_argument(
        "--renderer",
        action="append",
        choices=RENDERERS,
        help="capture only this backend (repeatable; defaults to both)",
    )
    parser.add_argument(
        "--scene",
        action="append",
        help="capture only this manifest scene id (repeatable; defaults to all)",
    )
    parser.add_argument("--json-output", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        install_dir = args.install_dir.resolve()
        manifest_path = args.manifest.resolve()
        run_root = args.run_root.resolve()
        if not install_dir.is_dir():
            raise compare_captures.CaptureError(
                f"install directory not found: {install_dir}"
            )
        if args.timeout <= 0:
            raise compare_captures.CaptureError("timeout must be positive")
        manifest = load_manifest(manifest_path)
        run_root.mkdir(parents=True, exist_ok=True)

        renderers = tuple(dict.fromkeys(args.renderer or RENDERERS))
        selected_scene_ids = set(args.scene or ())
        available_scene_ids = {scene["id"] for scene in manifest["scenes"]}
        unknown_scene_ids = selected_scene_ids - available_scene_ids
        if unknown_scene_ids:
            raise compare_captures.CaptureError(
                "unknown scene id(s): " + ", ".join(sorted(unknown_scene_ids))
            )
        selected_scenes = [
            scene
            for scene in manifest["scenes"]
            if not selected_scene_ids or scene["id"] in selected_scene_ids
        ]

        process_failures: list[str] = []
        if not args.compare_only:
            executable = discover_executable(install_dir, args.executable)
            for renderer in renderers:
                for scene in selected_scenes:
                    try:
                        capture, failure = run_capture(
                            executable,
                            install_dir,
                            run_root,
                            renderer,
                            scene,
                            args.timeout,
                            args.vulkan_validation,
                        )
                    except compare_captures.CaptureError as exc:
                        capture = None
                        failure = str(exc)
                    if capture:
                        print(f"captured {scene['id']}/{renderer}: {capture}")
                    if failure:
                        print(f"capture process failure: {failure}", file=sys.stderr)
                        process_failures.append(failure)

        report = compare_captures.evaluate_manifest(
            manifest_path, run_root / "captures", selected_scene_ids
        )
        report["process_failures"] = process_failures
        if process_failures:
            report["failures"] = process_failures + report["failures"]
            report["passed"] = False
    except (compare_captures.CaptureError, OSError, json.JSONDecodeError) as exc:
        print(f"renderer parity run error: {exc}", file=sys.stderr)
        return 2

    serialized = json.dumps(report, indent=2, sort_keys=True)
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(serialized + "\n", encoding="utf-8")
    print(serialized)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
