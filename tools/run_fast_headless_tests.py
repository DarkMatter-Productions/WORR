#!/usr/bin/env python3
"""Run the bounded Meson tests that are safe for pull-request CI."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any, Sequence


INTERACTIVE_RUNTIME_NAMES = {
    "client",
    "q2pro",
    "worr",
    "worr-bootstrap",
    "worr-host",
}
INTERACTIVE_RUNTIME_PREFIXES = (
    "worr_aarch",
    "worr_arm",
    "worr_i386",
    "worr_x86",
)


def _load_tests(meson: str, build_dir: Path) -> list[dict[str, Any]]:
    result = subprocess.run(
        [meson, "introspect", "--tests", str(build_dir)],
        check=True,
        capture_output=True,
        encoding="utf-8",
    )
    tests = json.loads(result.stdout)
    if not isinstance(tests, list):
        raise ValueError("Meson test introspection did not return a list")
    return tests


def _select_tests(
    tests: Sequence[dict[str, Any]], max_timeout: int
) -> list[dict[str, Any]]:
    selected = [
        test
        for test in tests
        if isinstance(test.get("timeout"), (int, float))
        and 0 < test["timeout"] <= max_timeout
    ]
    selected.sort(key=lambda test: str(test.get("name", "")))
    return selected


def _assert_headless(selected: Sequence[dict[str, Any]]) -> None:
    for test in selected:
        command = test.get("cmd")
        if not isinstance(command, list) or not command:
            raise ValueError(f"Meson test {test.get('name')!r} has no command")
        executable_name = str(command[0]).replace("\\", "/").rsplit("/", 1)[-1]
        executable = Path(executable_name).stem.casefold()
        if executable in INTERACTIVE_RUNTIME_NAMES or executable.startswith(
            INTERACTIVE_RUNTIME_PREFIXES
        ):
            raise ValueError(
                f"refusing interactive runtime test {test.get('name')!r}: "
                f"{command[0]}"
            )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Run Meson tests whose declared timeout is bounded, after rejecting "
            "commands that directly launch an interactive client."
        )
    )
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--max-timeout", type=int, default=30)
    parser.add_argument("--meson", default="meson")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    if args.max_timeout <= 0:
        parser.error("--max-timeout must be positive")

    tests = _load_tests(args.meson, args.build_dir)
    selected = _select_tests(tests, args.max_timeout)
    if not selected:
        raise ValueError(
            f"no Meson tests declare a timeout at or below {args.max_timeout}s"
        )
    _assert_headless(selected)

    print(
        f"Selected {len(selected)} of {len(tests)} Meson tests "
        f"(declared timeout <= {args.max_timeout}s).",
        flush=True,
    )
    if args.dry_run:
        for test in selected:
            print(test["name"])
        return 0

    command = [
        args.meson,
        "test",
        "-C",
        str(args.build_dir),
        "--no-rebuild",
        "--print-errorlogs",
        *[str(test["name"]) for test in selected],
    ]
    return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
