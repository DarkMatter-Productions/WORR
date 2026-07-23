#!/usr/bin/env python3
"""Run repeatable shadowmapping smoke scenes against the staged install."""

from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import sys
from typing import Iterable


FILTERS = {
    "pcf": 1,
    "vsm": 2,
    "evsm": 3,
    "pcss": 4,
}


SCENES = {
    "off-pvs-light": ["base1"],
    "moving-bmodel": ["base2"],
    "translated-md2": ["q2dm1"],
    "projectile-self-shadow": ["q2dm1"],
    "sun-cascade": ["q2dm1"],
    "hom-regression": ["base1"],
    "flashlight-owner": ["test/base1_flashlight"],
}


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[1]


def default_exe(install_dir: pathlib.Path) -> pathlib.Path:
    exe = install_dir / "worr_x86_64.exe"
    if exe.exists():
        return exe
    return install_dir / "worr_x86_64"


def iter_jobs(args: argparse.Namespace) -> Iterable[tuple[str, str, str, int]]:
    scenes = args.scene or sorted(SCENES)
    filters = args.filter or ["pcf"]
    for renderer in args.renderer:
        for filter_name in filters:
            filter_value = FILTERS[filter_name]
            for scene in scenes:
                for map_name in SCENES[scene]:
                    yield renderer, scene, map_name, filter_value


def job_home(args: argparse.Namespace, renderer: str, scene: str,
             filter_value: int) -> pathlib.Path:
    return args.run_root / renderer / scene / f"filter-{filter_value}"


def job_log(args: argparse.Namespace, renderer: str, scene: str,
            filter_value: int) -> pathlib.Path:
    return args.run_root / "process-logs" / f"{renderer}-{scene}-f{filter_value}.log"


def build_command(args: argparse.Namespace, renderer: str, scene: str,
                  map_name: str, filter_value: int) -> list[str]:
    log_name = f"shadow_{renderer}_{scene}_f{filter_value}"
    command = [
        str(args.exe),
        "+set", "basedir", str(args.install_dir),
        "+set", "homedir", str(job_home(args, renderer, scene, filter_value)),
        "+set", "r_renderer", renderer,
        "+set", "vid_renderer", renderer,
        "+set", "r_fullscreen", "0",
        "+set", "win_headless", "1",
        "+set", "in_enable", "0",
        "+set", "in_grab", "0",
        "+set", "s_enable", "0",
        "+set", "logfile", "1",
        "+set", "logfile_flush", "1",
        "+set", "logfile_name", log_name,
        "+set", "r_shadowmaps", "1",
        "+set", "r_shadowmap_cache_mode", str(args.cache_mode),
        "+set", "vk_shadow_shrink_frames", str(args.vk_shadow_shrink_frames),
        "+set", "r_shadowmap_size", str(args.shadow_size),
        "+set", "r_shadow_filter", str(filter_value),
        "+set", "r_shadow_sun", "1" if scene == "sun-cascade" else "0",
        "+set", "r_shadow_sun_resolution", str(args.sun_shadow_size),
        "+set", "r_shadow_draw_debug", str(args.debug_draw),
    ]
    if args.inject_shadow_recreate_failure and renderer == "vulkan":
        command += [
            "+set", "vk_shadow_test_fail_recreate", "1",
        ]
    if (args.inject_sun_resolution_drop_after_frames is not None and
            renderer == "vulkan"):
        command += [
            "+set", "vk_shadow_test_sun_resolution_drop_after_frames",
            str(args.inject_sun_resolution_drop_after_frames),
        ]
    if scene == "flashlight-owner":
        command += [
            "+set", "cl_shadowlights", "1",
            "+set", "cheats", "1",
        ]
    command += [
        "+map", map_name,
    ]
    if scene == "flashlight-owner":
        command += [
            "+wait", "60",
            "+give", "item_flashlight",
            "+use", "Flashlight",
        ]
    if (args.shadow_size_after_wait is not None or
            args.sun_shadow_size_after_wait is not None):
        if args.shadow_size_transition_wait > 0:
            command += ["+wait", str(args.shadow_size_transition_wait)]
        if args.shadow_size_after_wait is not None:
            command += ["+set", "r_shadowmap_size", str(args.shadow_size_after_wait)]
        if args.sun_shadow_size_after_wait is not None:
            command += ["+set", "r_shadow_sun_resolution",
                        str(args.sun_shadow_size_after_wait)]
    if args.pre_dump_wait > 0:
        command += [
            "+wait", str(args.pre_dump_wait),
            "+r_shadow_dump",
        ]
    elif args.dump_before_wait:
        command += [
            "+r_shadow_dump",
        ]
    command += [
        "+wait", str(args.wait),
        "+r_shadow_dump",
        "+quit",
    ]
    return command


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run WORR shadowmapping repro/smoke scenes from .install.")
    parser.add_argument("--install-dir", default=".install",
                        help="Staged install root")
    parser.add_argument("--run-root", default=".tmp/shadowmapping-repro",
                        help="Isolated per-scene runtime root")
    parser.add_argument("--exe", help="Client executable path")
    parser.add_argument("--renderer", action="append",
                        choices=("opengl", "vulkan"),
                        default=None,
                        help="Renderer to test; repeatable")
    parser.add_argument("--scene", action="append", choices=sorted(SCENES),
                        help="Scene family to run; repeatable")
    parser.add_argument("--filter", action="append", choices=sorted(FILTERS),
                        help="Shadow filter family; repeatable")
    parser.add_argument("--wait", type=int, default=180,
                        help="Frames to wait after map load before dump/quit")
    parser.add_argument("--cache-mode", type=int, choices=(0, 1, 2), default=1,
                        help="Shadow cache mode: 0=none, 1=static reuse, 2=world only")
    parser.add_argument("--vk-shadow-shrink-frames", type=int, default=180,
                        help="Frames below a smaller Vulkan shadow bucket before shrinking")
    parser.add_argument("--shadow-size", type=int, default=512,
                        help="Initial shared shadow resolution in texels")
    parser.add_argument("--shadow-size-after-wait", type=int,
                        help="Set r_shadowmap_size after the optional transition wait")
    parser.add_argument("--shadow-size-transition-wait", type=int, default=0,
                        help="Frames to retain the initial shadow size before changing it")
    parser.add_argument("--sun-shadow-size", type=int, default=1024,
                        help="Initial sun shadow resolution in texels")
    parser.add_argument("--sun-shadow-size-after-wait", type=int,
                        help="Set r_shadow_sun_resolution after the optional transition wait")
    parser.add_argument("--dump-before-wait", action="store_true",
                        help="Emit a shadow dump before the post-map wait interval")
    parser.add_argument("--pre-dump-wait", type=int, default=0,
                        help="Frames to wait before an additional pre-settle shadow dump")
    parser.add_argument("--inject-shadow-recreate-failure", action="store_true",
                        help="Test transactional Vulkan shadow-resource rollback")
    parser.add_argument("--inject-sun-resolution-drop-after-frames", type=int,
                        help="Vulkan test-only: lower sun shadow resolution after N frames")
    parser.add_argument("--debug-draw", type=int, default=0,
                        help="r_shadow_draw_debug bitmask for overlay repros")
    parser.add_argument("--vulkan-validation", action="store_true",
                        help="enable VK_LAYER_KHRONOS_validation for Vulkan jobs")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print commands without launching")
    args = parser.parse_args()

    args.install_dir = pathlib.Path(args.install_dir).resolve()
    args.run_root = pathlib.Path(args.run_root).resolve()
    args.exe = pathlib.Path(args.exe).resolve() if args.exe else default_exe(args.install_dir)
    args.renderer = args.renderer or ["opengl", "vulkan"]

    if not args.exe.exists() and not args.dry_run:
        parser.error(f"client executable not found: {args.exe}")

    cwd = repo_root()
    failed = 0
    for renderer, scene, map_name, filter_value in iter_jobs(args):
        command = build_command(args, renderer, scene, map_name, filter_value)
        print(" ".join(command), flush=True)
        if args.dry_run:
            continue
        job_home(args, renderer, scene, filter_value).mkdir(parents=True,
                                                             exist_ok=True)
        environment = os.environ.copy()
        if args.vulkan_validation and renderer == "vulkan":
            environment["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"
        result = subprocess.run(
            command,
            cwd=cwd,
            env=environment,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            creationflags=(getattr(subprocess, "CREATE_NO_WINDOW", 0)
                           if sys.platform == "win32" else 0),
        )
        log_path = job_log(args, renderer, scene, filter_value)
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(
            "COMMAND\n" + subprocess.list2cmdline(command) +
            "\n\nSTDOUT\n" + result.stdout +
            "\nSTDERR\n" + result.stderr,
            encoding="utf-8",
        )
        if result.returncode != 0:
            failed += 1
            print(f"FAILED {renderer}/{scene}/{map_name}/filter={filter_value}: "
                  f"{result.returncode}; see {log_path}", file=sys.stderr)

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
