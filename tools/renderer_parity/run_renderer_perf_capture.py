#!/usr/bin/env python3
"""Collect paired GL/VK telemetry through the hidden native capture surface."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from collections.abc import Mapping
from pathlib import Path


STAT_RE = re.compile(r"^(VK_STATS|GL_STATS)\s+.*$", re.MULTILINE)
EXEC_RE = re.compile(r'^\s*exec\s+(?:"([^"]+)"|(\S+))', re.IGNORECASE)
ERROR_PATTERNS = (
    re.compile(r"\bVUID-[A-Za-z0-9_-]+"),
    re.compile(r"validation error", re.IGNORECASE),
    re.compile(r"device lost", re.IGNORECASE),
    re.compile(r"fatal error", re.IGNORECASE),
)
ADAPTER_PATTERNS = {
    "vulkan": re.compile(r"^Vulkan device:\s*(.+)$", re.MULTILINE),
    "opengl": re.compile(r"^GL_RENDERER:\s*(.+)$", re.MULTILINE),
}

# Renderer-independent settings that are part of the timed scenario. Renderer
# selection itself intentionally differs between the two capture commands.
CAPTURE_PROFILE_BASE = (
    "game=basew",
    "r_fullscreen=0",
    "win_headless=1",
    "in_enable=0",
    "in_grab=0",
    "r_geometry=960x720",
    "developer=1",
    "bot_enable=0",
    "s_enable=0",
    "cl_maxfps=62",
    "r_maxfps=62",
    "gl_swapinterval=1",
)
DEFAULT_LAUNCH_CVARS = {
    # r_dof is latched at renderer initialization. Keep the non-DOF
    # fixed-view lane equivalent to the screenshot-capture profile so a
    # host/default archived value cannot silently add a post-process workload
    # to only one renderer.
    "r_dof": "0",
    "gl_bloom": "0",
    "vk_bloom": "0",
    "gl_color_correction": "0",
    "vk_color_correction": "0",
    "r_crtmode": "0",
}
RMLUI_CAPTURE_MARKER = "RmlUi guarded capture route is active"
CVAR_NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def resolve_launch_cvars(overrides: Mapping[str, str] | None = None) -> dict[str, str]:
    """Return a canonical, renderer-independent launch-cvar profile."""
    resolved = dict(DEFAULT_LAUNCH_CVARS)
    if overrides:
        for name, value in overrides.items():
            if not CVAR_NAME_RE.fullmatch(name):
                raise ValueError(f"invalid launch cvar name: {name!r}")
            if not value or "\x00" in value or "\n" in value or "\r" in value:
                raise ValueError(f"invalid launch cvar value for {name!r}")
            resolved[name] = value
    return dict(sorted(resolved.items()))


def capture_profile(rmlui: bool,
                    launch_cvars: Mapping[str, str] | None = None) -> tuple[str, ...]:
    resolved = resolve_launch_cvars(launch_cvars)
    return (CAPTURE_PROFILE_BASE +
            tuple(f"{name}={value}" for name, value in resolved.items()) +
            (f"ui_rml_enable={int(rmlui)}",))


def parse_launch_cvars(values: list[str]) -> dict[str, str]:
    overrides: dict[str, str] = {}
    for value in values:
        name, separator, cvar_value = value.partition("=")
        if not separator:
            raise ValueError(
                f"--launch-cvar must use NAME=VALUE syntax, got {value!r}"
            )
        if name in overrides:
            raise ValueError(f"duplicate --launch-cvar: {name}")
        overrides[name] = cvar_value
    return resolve_launch_cvars(overrides)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def discover_executable(install_dir: Path, override: Path | None) -> Path:
    if override:
        executable = override.resolve()
        if not executable.is_file():
            raise RuntimeError(f"executable not found: {executable}")
        return executable
    for name in ("worr_x86_64.exe", "worr.exe"):
        candidate = install_dir / name
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"no WORR client executable found under {install_dir}")


def resolve_game_file(base_game: Path, relative: str) -> tuple[Path, str]:
    normalized = relative.replace("\\", "/")
    if not normalized or normalized.startswith("/") or ".." in normalized.split("/"):
        raise RuntimeError(f"invalid game-relative config path: {relative!r}")
    candidate = (base_game / normalized).resolve()
    try:
        candidate.relative_to(base_game.resolve())
    except ValueError as exc:
        raise RuntimeError(f"config path escapes base game: {relative!r}") from exc
    if not candidate.is_file():
        raise RuntimeError(f"config not found: {candidate}")
    return candidate, normalized


def config_tree_sha256(install_dir: Path, config: str, rmlui: bool = False,
                       launch_cvars: Mapping[str, str] | None = None) -> str:
    """Hash the executed config and every repository-owned `exec` include."""
    base_game = (install_dir / "basew").resolve()
    pending = [config]
    contents: dict[str, bytes] = {}

    while pending:
        requested = pending.pop()
        path, relative = resolve_game_file(base_game, requested)
        if relative in contents:
            continue
        data = path.read_bytes()
        contents[relative] = data
        for line in data.decode("utf-8", errors="replace").splitlines():
            match = EXEC_RE.match(line)
            if not match:
                continue
            included = match.group(1) or match.group(2)
            if included:
                pending.append(included)

    digest = hashlib.sha256()
    for value in capture_profile(rmlui, launch_cvars):
        digest.update(value.encode("utf-8"))
        digest.update(b"\0")
    for relative in sorted(contents):
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(contents[relative])
        digest.update(b"\0")
    return digest.hexdigest()


def build_command(executable: Path, install_dir: Path, home: Path,
                  renderer: str, config: str, rmlui: bool = False,
                  launch_cvars: Mapping[str, str] | None = None) -> list[str]:
    command = [
        str(executable),
        "+set", "basedir", str(install_dir),
        "+set", "homedir", str(home),
        "+set", "game", "basew",
        "+set", "r_fullscreen", "0",
        "+set", "win_headless", "1",
        "+set", "in_enable", "0",
        "+set", "in_grab", "0",
        "+set", "r_geometry", "960x720",
        "+set", "developer", "1",
        "+set", "bot_enable", "0",
        "+set", "s_enable", "0",
    ]
    for name, value in resolve_launch_cvars(launch_cvars).items():
        command.extend(("+set", name, value))
    command.extend((
        "+set", "ui_rml_enable", "1" if rmlui else "0",
        "+set", "cl_maxfps", "62",
        "+set", "r_maxfps", "62",
        "+set", "gl_swapinterval", "1",
        "+set", "vid_renderer", renderer,
        "+set", "r_renderer", renderer,
        "+exec", config,
    ))
    return command


def count_stats(log: str, prefix: str) -> tuple[int, int, int]:
    records = [match.group(0) for match in STAT_RE.finditer(log)
               if match.group(1) == prefix]
    return (
        len(records),
        sum("gpu_valid=1" in record for record in records),
        sum("gpu_frame_valid=1" in record for record in records),
    )


def renderer_adapter(log: str, renderer: str) -> str | None:
    match = ADAPTER_PATTERNS[renderer].search(log)
    return match.group(1).strip() if match else None


def run_renderer(executable: Path, install_dir: Path, run_root: Path,
                 renderer: str, config: str, timeout: float, validation: bool,
                 min_samples: int, rmlui: bool,
                 launch_cvars: Mapping[str, str] | None = None) -> dict[str, object]:
    home = run_root / "homes" / renderer
    home.mkdir(parents=True, exist_ok=True)
    command = build_command(executable, install_dir, home, renderer, config,
                            rmlui, launch_cvars)
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
            timeout=timeout,
            check=False,
            creationflags=(getattr(subprocess, "CREATE_NO_WINDOW", 0)
                           if os.name == "nt" else 0),
        )
        exit_code: int | None = completed.returncode
        stdout = completed.stdout
        stderr = completed.stderr
        failures: list[str] = []
    except subprocess.TimeoutExpired as exc:
        exit_code = None
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode("utf-8", errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", errors="replace")
        failures = [f"{renderer} timed out after {timeout:g}s"]

    log = ("COMMAND\n" + subprocess.list2cmdline(command) + "\n\nSTDOUT\n" +
           stdout + "\nSTDERR\n" + stderr)
    log_path = run_root / f"{renderer}.log"
    log_path.write_text(log, encoding="utf-8")
    prefix = "VK_STATS" if renderer == "vulkan" else "GL_STATS"
    samples, gpu_valid_samples, gpu_frame_valid_samples = count_stats(log, prefix)
    if exit_code not in (0, None):
        failures.append(f"{renderer} client exited with status {exit_code}")
    if samples < min_samples:
        failures.append(
            f"{renderer} recorded {samples} {prefix} sample(s); expected at least {min_samples}"
        )
    if rmlui:
        if RMLUI_CAPTURE_MARKER not in log:
            failures.append(f"{renderer} did not open the guarded RmlUi workload")
        if renderer == "vulkan" and not re.search(r"\bui_uploads=[1-9][0-9]*\b", log):
            failures.append("vulkan RmlUi workload recorded no UI upload bytes")
    for pattern in ERROR_PATTERNS:
        match = pattern.search(log)
        if match:
            failures.append(f"{renderer} log matched error pattern: {match.group(0)}")

    return {
        "renderer": renderer,
        "command": command,
        "exit_code": exit_code,
        "log": str(log_path),
        "log_sha256": file_sha256(log_path),
        "samples": samples,
        "gpu_valid_samples": gpu_valid_samples,
        "gpu_frame_valid_samples": gpu_frame_valid_samples,
        "adapter": renderer_adapter(log, renderer),
        "failures": failures,
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, default=Path(".install"))
    parser.add_argument("--executable", type=Path)
    parser.add_argument(
        "--config", default="renderer_parity/fr01_renderer_perf_bmodel.cfg",
        help="basew-relative shared scenario config",
    )
    parser.add_argument(
        "--fixture", type=Path,
        default=Path("assets/maps/worr_fr01_bmodel_first_frame.bsp"),
    )
    parser.add_argument(
        "--scenario-id", default="fr01-bmodel-fixed-view-telemetry",
        help="stable identifier recorded in the paired capture manifest",
    )
    parser.add_argument(
        "--run-root", type=Path,
        default=Path(".tmp/renderer-parity/fr01-renderer-perf"),
    )
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--min-samples", type=int, default=100)
    parser.add_argument("--hardware-id", required=True)
    parser.add_argument("--driver", required=True)
    parser.add_argument("--vulkan-validation", action="store_true")
    parser.add_argument("--rmlui", action="store_true",
                        help="enable and require the guarded RmlUi overlay workload")
    parser.add_argument(
        "--launch-cvar", action="append", default=[], metavar="NAME=VALUE",
        help=("override a renderer-independent cvar before renderer startup; "
              "repeatable and included in the capture profile hash"),
    )
    parser.add_argument("--json-output", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.timeout <= 0 or args.min_samples < 1:
        print("renderer perf capture: timeout must be positive and min-samples must be >= 1",
              file=sys.stderr)
        return 2
    if not args.hardware_id.strip() or not args.driver.strip():
        print("renderer perf capture: hardware-id and driver must be non-empty",
              file=sys.stderr)
        return 2
    try:
        install_dir = args.install_dir.resolve()
        run_root = args.run_root.resolve()
        fixture = args.fixture.resolve()
        if not fixture.is_file():
            raise RuntimeError(f"fixture not found: {fixture}")
        executable = discover_executable(install_dir, args.executable)
        launch_cvars = parse_launch_cvars(args.launch_cvar)
        config_sha256 = config_tree_sha256(install_dir, args.config, args.rmlui,
                                           launch_cvars)
        run_root.mkdir(parents=True, exist_ok=True)
        captures = {
            renderer: run_renderer(executable, install_dir, run_root, renderer,
                                   args.config, args.timeout,
                                   args.vulkan_validation, args.min_samples,
                                   args.rmlui, launch_cvars)
            for renderer in ("vulkan", "opengl")
        }
        vulkan_adapter = captures["vulkan"]["adapter"]
        opengl_adapter = captures["opengl"]["adapter"]
        adapter_failures: list[str] = []
        if not vulkan_adapter or not opengl_adapter:
            adapter_failures.append("paired capture could not identify both active render adapters")
        elif vulkan_adapter != opengl_adapter:
            adapter_failures.append(
                "paired capture used different render adapters: "
                f"vulkan={vulkan_adapter!r}, opengl={opengl_adapter!r}"
            )
        manifest = {
            "schema_version": 1,
            "scenario": {
                "id": args.scenario_id,
                "fixture_sha256": file_sha256(fixture),
                "config_sha256": config_sha256,
                "launch_cvars": launch_cvars,
            },
            "environment": {
                "hardware_id": args.hardware_id,
                "driver": args.driver,
                "adapter": vulkan_adapter or opengl_adapter or "unidentified",
            },
            "vulkan": {
                "renderer": "vulkan",
                "log_sha256": str(captures["vulkan"]["log_sha256"]),
            },
            "opengl": {
                "renderer": "opengl",
                "log_sha256": str(captures["opengl"]["log_sha256"]),
            },
        }
        manifest_path = run_root / "capture.json"
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n",
                                 encoding="utf-8")
        failures = adapter_failures + [failure for capture in captures.values()
                                       for failure in capture["failures"]]
        result = {
            "schema_version": 1,
            "passed": not failures,
            "fixture": str(fixture),
            "fixture_sha256": manifest["scenario"]["fixture_sha256"],
            "config": args.config,
            "config_sha256": config_sha256,
            "capture_manifest": str(manifest_path),
            "captures": captures,
            "failures": failures,
        }
        output = args.json_output.resolve() if args.json_output else run_root / "results.json"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0 if result["passed"] else 1
    except (OSError, RuntimeError) as exc:
        print(f"renderer perf capture: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
