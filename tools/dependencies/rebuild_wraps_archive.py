#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import pathlib
import sys
import tempfile

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from tools.dependencies.verify_dependency_baseline import (  # noqa: E402
    DEFAULT_BASELINE,
    REPO_ROOT,
    archive_settings,
    build_wrap_archive_bytes,
    load_baseline,
)


def write_atomically(path: pathlib.Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: pathlib.Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            prefix=f".{path.name}.",
            suffix=".tmp",
            dir=path.parent,
            delete=False,
        ) as handle:
            temporary_path = pathlib.Path(handle.name)
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary_path, path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Rebuild subprojects/wraps.zip from the canonical dependency baseline."
    )
    parser.add_argument("--repo-root", default=str(REPO_ROOT))
    parser.add_argument("--baseline", default=str(DEFAULT_BASELINE))
    args = parser.parse_args()

    repo_root = pathlib.Path(args.repo_root).resolve()
    baseline = load_baseline(pathlib.Path(args.baseline))
    settings = archive_settings(baseline)
    archive_path = repo_root.joinpath(*settings.path.parts)
    payload = build_wrap_archive_bytes(repo_root, baseline)
    write_atomically(archive_path, payload)
    print(f"Wrote deterministic wraps archive: {archive_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
