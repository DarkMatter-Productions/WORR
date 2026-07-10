from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_parity_manifest as parity  # noqa: E402


def write_json(path: Path, data: object) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return path


def smoke_manifest(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": routes,
    }


def smoke_route(route_id: str, migration_phase: str = "starter") -> dict[str, str]:
    return {
        "id": route_id,
        "migration_phase": migration_phase,
    }


def status(value: str) -> dict[str, str]:
    return {
        "status": value,
        "evidence": "test fixture",
    }


def phase_defaults(
    *,
    starter_controller_bindings: str = "pending",
    runtime_controller_bindings: str = "complete",
) -> dict[str, dict[str, dict[str, str]]]:
    defaults: dict[str, dict[str, dict[str, str]]] = {}
    for phase in parity.MIGRATION_PHASES:
        defaults[phase] = {
            category: status("pending")
            for category in parity.CANONICAL_CATEGORIES
        }
        defaults[phase]["document_load"] = status("complete")

    defaults["starter"]["controller_bindings"] = status(starter_controller_bindings)
    defaults["controller_stub"]["controller_bindings"] = status("complete")
    defaults["runtime_stub"]["controller_bindings"] = status(runtime_controller_bindings)
    defaults["runtime_stub"]["legacy_fallback"] = status("complete")
    defaults["parity_ready"] = {
        category: status("pending")
        for category in parity.CANONICAL_CATEGORIES
    }
    return defaults


def parity_manifest(
    routes: list[dict[str, Any]],
    *,
    defaults: dict[str, Any] | None = None,
) -> dict[str, Any]:
    return {
        "schema": parity.EXPECTED_SCHEMA,
        "evidence_categories": [
            {
                "id": category,
                "kind": "test",
                "description": "test category",
            }
            for category in parity.CANONICAL_CATEGORIES
        ],
        "phase_defaults": defaults if defaults is not None else phase_defaults(),
        "routes": routes,
    }


def parity_route(route_id: str, *, evidence: dict[str, Any] | None = None) -> dict[str, Any]:
    route: dict[str, Any] = {
        "id": route_id,
    }
    if evidence is not None:
        route["evidence"] = evidence
    return route


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    smoke_routes: list[dict[str, Any]],
    parity_routes: list[dict[str, Any]],
    defaults: dict[str, Any] | None = None,
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    smoke_path = write_json(
        repo_root / "tools/ui_smoke/rmlui_manifest.json",
        smoke_manifest(smoke_routes),
    )
    parity_path = write_json(
        repo_root / "tools/ui_smoke/rmlui_parity_manifest.json",
        parity_manifest(parity_routes, defaults=defaults),
    )

    result = parity.main(
        [
            "--smoke-manifest",
            str(smoke_path),
            "--parity-manifest",
            str(parity_path),
            "--repo-root",
            str(repo_root),
            "--format",
            output_format,
        ]
    )
    return result, capsys.readouterr()


def test_complete_manifest_passes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        smoke_routes=[
            smoke_route("main", "starter"),
            smoke_route("game", "runtime_stub"),
        ],
        parity_routes=[
            parity_route("main"),
            parity_route("game"),
        ],
    )

    assert result == 0
    assert "Routes checked: 2" in captured.out
    assert "Categories checked: 9" in captured.out
    assert "Result: RmlUi parity checklist check passed." in captured.out


def test_missing_route_coverage_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        smoke_routes=[
            smoke_route("main"),
            smoke_route("game"),
        ],
        parity_routes=[
            parity_route("main"),
        ],
    )

    assert result == 1
    assert "missing parity checklist coverage for route 'game'" in captured.out


def test_unknown_route_coverage_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        smoke_routes=[
            smoke_route("main"),
        ],
        parity_routes=[
            parity_route("main"),
            parity_route("bogus"),
        ],
    )

    assert result == 1
    assert "parity checklist contains unknown route 'bogus'" in captured.out


def test_parity_ready_without_completed_evidence_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        smoke_routes=[
            smoke_route("main", "parity_ready"),
        ],
        parity_routes=[
            parity_route("main"),
        ],
    )

    assert result == 1
    assert (
        "parity_ready route 'main' category 'document_load' is pending; "
        "completed evidence is required"
    ) in captured.out
    assert (
        "parity_ready route 'main' category 'renderer_vulkan' is pending; "
        "completed evidence is required"
    ) in captured.out


def test_pending_count_json_reports_categories(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        smoke_routes=[
            smoke_route("main", "starter"),
            smoke_route("game", "runtime_stub"),
        ],
        parity_routes=[
            parity_route("main"),
            parity_route("game"),
        ],
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["routes_checked"] == 2
    assert payload["pending_counts"]["document_load"] == 0
    assert payload["pending_counts"]["controller_bindings"] == 1
    assert payload["pending_counts"]["renderer_vulkan"] == 2
    assert payload["complete_counts"]["legacy_fallback"] == 1
