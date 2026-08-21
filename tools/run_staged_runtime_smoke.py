#!/usr/bin/env python3
"""Launch the staged WORR dedicated server and require engine readiness.

This gate intentionally never starts the client runtime.  The public dedicated
launcher loads the staged dedicated engine in-process, and the engine writes a
one-use readiness token only after Qcommon initialization has completed.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import secrets
import stat
import subprocess
import sys
import tempfile
from typing import Sequence


TOOLS_ROOT = Path(__file__).resolve().parent
if str(TOOLS_ROOT) not in sys.path:
    sys.path.insert(0, str(TOOLS_ROOT))

from networking import headless_process  # noqa: E402
from release.targets import (  # noqa: E402
    BASE_GAME,
    RUNTIME_COORDINATION_LOCK,
    game_module_required_path,
    get_target,
)


DEFAULT_TIMEOUT_SECONDS = 30
MAX_DIAGNOSTIC_CHARACTERS = 12_000


class StagedRuntimeSmokeError(RuntimeError):
    """Raised when the staged dedicated runtime does not prove readiness."""


def snapshot_install_tree(install_dir: Path) -> dict[str, tuple[int, int, int]]:
    """Capture metadata that detects any staged-runtime write or new path."""
    snapshot: dict[str, tuple[int, int, int]] = {}
    for path in install_dir.rglob("*"):
        metadata = path.lstat()
        snapshot[path.relative_to(install_dir).as_posix()] = (
            stat.S_IFMT(metadata.st_mode),
            metadata.st_size,
            metadata.st_mtime_ns,
        )
    return snapshot


def describe_install_changes(
    before: dict[str, tuple[int, int, int]],
    after: dict[str, tuple[int, int, int]],
) -> str:
    added = sorted(after.keys() - before.keys())
    removed = sorted(before.keys() - after.keys())
    modified = sorted(path for path in before.keys() & after.keys() if before[path] != after[path])
    details = [
        *(f"added:{path}" for path in added),
        *(f"removed:{path}" for path in removed),
        *(f"modified:{path}" for path in modified),
    ]
    if len(details) > 20:
        details = [*details[:20], f"... and {len(details) - 20} more"]
    return ", ".join(details)


def build_command(
    launcher: Path,
    install_dir: Path,
    runtime_home: Path,
) -> list[str]:
    """Build the input-free, dedicated-only smoke command."""
    return [
        str(launcher),
        "--bootstrap-skip-update-check",
        "--bootstrap-quiet-status",
        "+set",
        "basedir",
        str(install_dir),
        "+set",
        "homedir",
        str(runtime_home),
        "+set",
        "game",
        BASE_GAME,
        "+set",
        "net_enable_ipv6",
        "0",
        "+set",
        "net_ip",
        "127.0.0.1",
        "+set",
        "net_port",
        "0",
        "+wait",
        "2",
        "+quit",
    ]


def _diagnostic_tail(label: str, value: str) -> str:
    if not value:
        return f"{label}: <empty>"
    return f"{label} (tail):\n{value[-MAX_DIAGNOSTIC_CHARACTERS:]}"


def run_smoke(
    *,
    launcher: Path,
    install_dir: Path,
    runtime_root: Path,
    timeout_seconds: int,
    ready_token: str | None = None,
) -> None:
    """Run one isolated dedicated smoke and validate the engine-owned token."""
    runtime_home = runtime_root / "home"
    (runtime_home / BASE_GAME).mkdir(parents=True, exist_ok=False)
    ready_path = runtime_root / "engine-ready.token"
    token = ready_token or secrets.token_hex(32)
    environment = dict(os.environ)
    environment["WORR_BOOTSTRAP_READY_FILE"] = str(ready_path)
    environment["WORR_BOOTSTRAP_READY_TOKEN"] = token

    process: subprocess.Popen[str] | None = None
    stdout = ""
    stderr = ""
    install_before = snapshot_install_tree(install_dir)
    try:
        process = headless_process.start_headless_process(
            build_command(launcher, install_dir, runtime_home),
            cwd=install_dir,
            env=environment,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        try:
            stdout, stderr = process.communicate(timeout=timeout_seconds)
        except subprocess.TimeoutExpired as error:
            raise StagedRuntimeSmokeError(
                f"staged dedicated runtime timed out after {timeout_seconds}s"
            ) from error

        if process.returncode != 0:
            raise StagedRuntimeSmokeError(
                "staged dedicated runtime exited with "
                f"code {process.returncode}\n"
                f"{_diagnostic_tail('stdout', stdout)}\n"
                f"{_diagnostic_tail('stderr', stderr)}"
            )
        if not ready_path.is_file():
            raise StagedRuntimeSmokeError(
                "staged dedicated engine did not create its readiness token\n"
                f"{_diagnostic_tail('stdout', stdout)}\n"
                f"{_diagnostic_tail('stderr', stderr)}"
            )
        observed_token = ready_path.read_text(encoding="utf-8")
        if not secrets.compare_digest(observed_token, token):
            raise StagedRuntimeSmokeError(
                "staged dedicated engine readiness token did not match this run"
            )
    finally:
        # On Windows this also closes the kill-on-close job after a clean exit,
        # ensuring a launcher child can never outlive the smoke harness.
        headless_process.terminate_process_tree(process)
        install_after = snapshot_install_tree(install_dir)
        if install_after != install_before:
            raise StagedRuntimeSmokeError(
                "staged dedicated runtime modified the immutable install tree: "
                + describe_install_changes(install_before, install_after)
            )


def _required_staged_paths(install_dir: Path, platform_id: str) -> tuple[Path, ...]:
    target = get_target(platform_id)
    if target["server"]["role"] != "server":
        raise StagedRuntimeSmokeError(
            f"release target {platform_id!r} does not describe a dedicated server"
        )
    return (
        install_dir / target["server"]["launch_exe"],
        install_dir / target["server"]["engine_library"],
        install_dir / game_module_required_path(target, "sgame"),
        install_dir / BASE_GAME / "pak0.pkz",
        install_dir / RUNTIME_COORDINATION_LOCK,
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Smoke the staged public dedicated launcher in an isolated, "
            "input-free runtime directory and require engine readiness."
        )
    )
    parser.add_argument("--install-dir", type=Path, required=True)
    parser.add_argument("--platform-id", required=True)
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_SECONDS)
    args = parser.parse_args(argv)

    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    install_dir = args.install_dir.resolve()
    if not install_dir.is_dir():
        raise StagedRuntimeSmokeError(f"staged install directory not found: {install_dir}")
    required_paths = _required_staged_paths(install_dir, args.platform_id)
    missing = [path for path in required_paths if not path.is_file()]
    if missing:
        raise StagedRuntimeSmokeError(
            "staged dedicated runtime is incomplete: "
            + ", ".join(str(path) for path in missing)
        )

    scratch_parent = Path.cwd() / ".tmp"
    scratch_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=f"staged-runtime-{args.platform_id}-",
        dir=scratch_parent,
    ) as temporary:
        run_smoke(
            launcher=required_paths[0],
            install_dir=install_dir,
            runtime_root=Path(temporary),
            timeout_seconds=args.timeout,
        )

    print(
        "staged dedicated runtime smoke passed: "
        f"platform={args.platform_id} launcher={required_paths[0].name}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
