from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_controller_stub_completion as completion  # noqa: E402


def write_json(path: Path, data: object) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return path


def route(route_id: str, migration_phase: str) -> dict[str, Any]:
    return {
        "id": route_id,
        "document": f"assets/ui/rml/{route_id}.rml",
        "migration_phase": migration_phase,
    }


def manifest(routes: object) -> dict[str, Any]:
    return {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": routes,
    }


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    routes: object,
    *args: str,
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = write_json(
        repo_root / "tools/ui_smoke/rmlui_manifest.json",
        manifest(routes),
    )
    result = completion.main(
        [
            "--manifest",
            str(manifest_path),
            "--repo-root",
            str(repo_root),
            *args,
        ]
    )
    return result, capsys.readouterr()


def test_default_report_accepts_remaining_starter_routes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        tmp_path,
        capsys,
        [
            route("main", "runtime_stub"),
            route("options", "controller_stub"),
            route("mymap_main", "starter"),
            route("admin_menu", "starter"),
        ],
    )

    assert result == 0
    assert captured.err == ""
    assert "Routes checked: 4" in captured.out
    assert (
        "Phase counts: starter=2, controller_stub=1, runtime_stub=1, "
        "parity_pending=0, parity_ready=0"
    ) in captured.out
    assert "Advanced routes: 2/4 (50.0%)" in captured.out
    assert "Non-runtime routes advanced: 1/3 (33.3%)" in captured.out
    assert "Remaining starter routes: 2" in captured.out
    assert "Starter route ids: admin_menu, mymap_main" in captured.out
    assert "Strict completion required: no" in captured.out
    assert "remaining starter routes are informational" in captured.out


def test_strict_completion_fails_when_starter_routes_remain(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        tmp_path,
        capsys,
        [
            route("main", "runtime_stub"),
            route("options", "controller_stub"),
            route("mymap_main", "starter"),
        ],
        "--require-complete-controller-stubs",
    )

    assert result == 1
    assert captured.err == ""
    assert "Strict completion required: yes" in captured.out
    assert "starter routes remain after controller-stub completion is required" in captured.out
    assert "mymap_main" in captured.out


def test_strict_completion_passes_when_all_routes_are_advanced_json(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        tmp_path,
        capsys,
        [
            route("main", "runtime_stub"),
            route("options", "controller_stub"),
            route("vote_menu", "parity_pending"),
            route("match_stats", "parity_ready"),
        ],
        "--require-complete-controller-stubs",
        "--format",
        "json",
    )
    payload = json.loads(captured.out)

    assert result == 0
    assert captured.err == ""
    assert payload["ok"] is True
    assert payload["strict"] is True
    assert payload["total_routes"] == 4
    assert payload["phase_counts"] == {
        "starter": 0,
        "controller_stub": 1,
        "runtime_stub": 1,
        "parity_pending": 1,
        "parity_ready": 1,
    }
    assert payload["starter_routes"] == {"count": 0, "route_ids": []}
    assert payload["controller_stub_routes"] == 1
    assert payload["runtime_stub_routes"] == 1
    assert payload["advanced_routes"] == {
        "count": 4,
        "total": 4,
        "percent": 100.0,
    }
    assert payload["non_runtime_routes"] == {
        "advanced": 3,
        "total": 3,
        "percent": 100.0,
    }
    assert payload["errors"] == []


def test_invalid_manifest_shape_returns_json_error(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        tmp_path,
        capsys,
        {"main": route("main", "controller_stub")},
        "--format",
        "json",
    )
    payload = json.loads(captured.out)

    assert result == 1
    assert captured.err == ""
    assert payload["ok"] is False
    assert payload["total_routes"] == 0
    assert payload["starter_routes"] == {"count": 0, "route_ids": []}
    assert payload["errors"] == ["manifest field 'routes' must be a list"]


def test_invalid_phase_and_duplicate_ids_fail_validation(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        tmp_path,
        capsys,
        [
            route("main", "controller_stub"),
            route("main", "runtime_stub"),
            route("unknown", "unsupported"),
        ],
    )

    assert result == 1
    assert "duplicate manifest route id 'main'" in captured.out
    assert "field 'migration_phase' must be one of" in captured.out
    assert "got 'unsupported'" in captured.out
