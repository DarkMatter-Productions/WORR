#!/usr/bin/env python3
"""Stage the real-BSP fixture under .tmp and execute the native parity probe."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

from generate_rewind_collision_bsp_fixture import build_fixture


EXPECTED_SHA256 = "ed5542288b5efb08b846e39afcd7fccf3359e036efa4364be84f08ab8855fbde"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--repo-root", type=Path, required=True)
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    scratch = repo_root / ".tmp" / "networking" / "rewind_collision_real_bsp"
    fixture = scratch / "basew" / "maps" / "rewind_collision_parity.bsp"
    report_path = scratch / "report.json"

    first = build_fixture()
    second = build_fixture()
    if first != second:
        raise RuntimeError("fixture generator produced non-deterministic bytes")
    digest = hashlib.sha256(first).hexdigest()
    if digest != EXPECTED_SHA256:
        raise RuntimeError(
            f"fixture hash changed: expected {EXPECTED_SHA256}, observed {digest}"
        )
    fixture.parent.mkdir(parents=True, exist_ok=True)
    fixture.write_bytes(first)

    command = [str(args.probe.resolve()), str(fixture)]
    completed = subprocess.run(
        command,
        cwd=repo_root,
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    report = {
        "schema": "worr.rewind-collision-real-bsp-parity.v4",
        "fixture": {
            "path": str(fixture.relative_to(repo_root)).replace("\\", "/"),
            "bytes": len(first),
            "sha256": digest,
            "inline_models": 2,
            "deathmatch_spawns": 4,
            "gameplay_areas": 1,
            "visibility_clusters": 1,
        },
        "probe": {
            "path": str(args.probe.resolve()),
            "returncode": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        },
        "gate": {
            "passed": completed.returncode == 0,
            "reference_path": "SV_Clip",
            "provider_path": "TraceTransformed",
            "geometric_cases": 10,
            "identity_rejection_classes": 4,
            "gameplay_area_connectivity": True,
            "gameplay_pvs_phs_visibility": True,
            "exact_trace_fields": [
                "allsolid",
                "startsolid",
                "fraction",
                "endpos",
                "plane",
                "surface",
                "contents",
                "plane2",
                "surface2",
            ],
            "live_edict_and_link_state_byte_guard": True,
        },
    }
    scratch.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    if completed.stdout:
        print(completed.stdout, end="")
    if completed.stderr:
        print(completed.stderr, end="", file=sys.stderr)
    print(f"fixture sha256={digest} bytes={len(first)}")
    print(f"evidence {report_path.relative_to(repo_root)}")
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
