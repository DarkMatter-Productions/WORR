from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_phase_consistency as phase_consistency  # noqa: E402


def write_json(path: Path, data: object) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return path


def write_cpp(path: Path, menu_returns: dict[str, list[str]]) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    menu_cases: list[str] = []
    for route_id, cases in menu_returns.items():
        for case_name in cases:
            menu_cases.append(f"    case {case_name}:")
        menu_cases.append(f'        return "{route_id}";')

    path.write_text(
        "\n".join(
            [
                "const char *UI_Rml_RouteForMenu(uiMenu_t menu)",
                "{",
                "    switch (menu) {",
                *menu_cases,
                "    default:",
                "        return NULL;",
                "    }",
                "}",
                "",
            ]
        ),
        encoding="utf-8",
    )
    return path


def smoke_manifest(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": routes,
    }


def smoke_route(route_id: str, migration_phase: str) -> dict[str, str]:
    return {
        "id": route_id,
        "document": f"assets/ui/rml/shell/{route_id}.rml",
        "migration_phase": migration_phase,
    }


def route_metadata(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": "worr.rml.test.routes.v1",
        "routes": routes,
    }


def metadata_route(
    route_id: str,
    migration_phase: str,
    *,
    controller_contracts: object | None = None,
) -> dict[str, Any]:
    if controller_contracts is None:
        controller_contracts = [
            {
                "category": "navigation",
                "contract": "worr.rml.controller.navigation.mock",
                "fixture": "navigation.mock.json",
                "model": "ui.navigation",
                "status": "mock_fixture",
            }
        ]
    return {
        "id": route_id,
        "document": f"shell/{route_id}.rml",
        "migration_phase": migration_phase,
        "controller_contracts": controller_contracts,
    }


def status(value: str) -> dict[str, str]:
    return {
        "status": value,
        "evidence": "test fixture",
    }


def phase_defaults(
    *,
    starter_controller_bindings: str = "pending",
    controller_stub_controller_bindings: str = "complete",
    runtime_stub_controller_bindings: str = "complete",
    parity_ready_status: str = "pending",
) -> dict[str, dict[str, dict[str, str]]]:
    defaults: dict[str, dict[str, dict[str, str]]] = {}
    for phase in phase_consistency.MIGRATION_PHASES:
        defaults[phase] = {
            category: status("pending")
            for category in ("document_load", "controller_bindings", "screenshot_layout")
        }

    defaults["starter"]["document_load"] = status("complete")
    defaults["starter"]["controller_bindings"] = status(starter_controller_bindings)
    defaults["controller_stub"]["document_load"] = status("complete")
    defaults["controller_stub"]["controller_bindings"] = status(
        controller_stub_controller_bindings
    )
    defaults["runtime_stub"]["document_load"] = status("complete")
    defaults["runtime_stub"]["controller_bindings"] = status(runtime_stub_controller_bindings)
    defaults["parity_ready"] = {
        category: status(parity_ready_status)
        for category in ("document_load", "controller_bindings", "screenshot_layout")
    }
    return defaults


def parity_manifest(
    routes: list[dict[str, Any]],
    *,
    defaults: dict[str, Any] | None = None,
) -> dict[str, Any]:
    return {
        "schema": "worr.rmlui.parity_manifest.v1",
        "evidence_categories": [
            {
                "id": category,
                "kind": "test",
                "description": "test category",
            }
            for category in ("document_load", "controller_bindings", "screenshot_layout")
        ],
        "phase_defaults": defaults if defaults is not None else phase_defaults(),
        "routes": routes,
    }


def parity_route(route_id: str, evidence: dict[str, Any] | None = None) -> dict[str, Any]:
    route: dict[str, Any] = {"id": route_id}
    if evidence is not None:
        route["evidence"] = evidence
    return route


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    manifest_routes: list[dict[str, Any]],
    metadata_routes: list[dict[str, Any]],
    parity_routes: list[dict[str, Any]],
    defaults: dict[str, Any] | None = None,
    menu_returns: dict[str, list[str]] | None = None,
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = write_json(
        repo_root / "tools/ui_smoke/rmlui_manifest.json",
        smoke_manifest(manifest_routes),
    )
    metadata_path = write_json(
        repo_root / "assets/ui/rml/shell/routes.json",
        route_metadata(metadata_routes),
    )
    parity_path = write_json(
        repo_root / "tools/ui_smoke/rmlui_parity_manifest.json",
        parity_manifest(parity_routes, defaults=defaults),
    )
    cpp_path = write_cpp(
        repo_root / "src/client/ui_rml/ui_rml.cpp",
        menu_returns if menu_returns is not None else {"main": ["UIMENU_MAIN"]},
    )

    result = phase_consistency.main(
        [
            "--manifest",
            str(manifest_path),
            "--route-metadata",
            str(metadata_path),
            "--parity-manifest",
            str(parity_path),
            "--cpp",
            str(cpp_path),
            "--repo-root",
            str(repo_root),
            "--format",
            output_format,
        ]
    )
    return result, capsys.readouterr()


def test_valid_controller_and_runtime_phases_pass(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            smoke_route("main", "runtime_stub"),
            smoke_route("options", "controller_stub"),
            smoke_route("downloads", "starter"),
        ],
        metadata_routes=[
            metadata_route("main", "runtime_stub"),
            metadata_route("options", "controller_stub"),
        ],
        parity_routes=[
            parity_route("main"),
            parity_route("options"),
            parity_route("downloads"),
        ],
    )

    assert result == 0
    assert "Metadata-backed advanced routes: 2" in captured.out
    assert "Runtime menu-mapped routes: 1" in captured.out
    assert "Result: RmlUi phase consistency check passed." in captured.out


def test_missing_controller_stub_metadata_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            smoke_route("options", "controller_stub"),
        ],
        metadata_routes=[],
        parity_routes=[
            parity_route("options"),
        ],
    )

    assert result == 1
    assert "route_metadata=1" in captured.out
    assert "controller_stub route 'options' is missing route metadata" in captured.out


def test_parity_ready_without_completed_evidence_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            smoke_route("main", "parity_ready"),
        ],
        metadata_routes=[],
        parity_routes=[
            parity_route("main"),
        ],
    )

    assert result == 1
    assert "Parity-ready routes: 1" in captured.out
    assert (
        "parity_ready route 'main' category 'document_load' is pending; "
        "completed evidence is required"
    ) in captured.out
    assert (
        "parity_ready route 'main' category 'screenshot_layout' is pending; "
        "completed evidence is required"
    ) in captured.out


def test_mismatched_controller_bindings_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            smoke_route("main", "starter"),
            smoke_route("options", "controller_stub"),
        ],
        metadata_routes=[
            metadata_route("options", "controller_stub"),
        ],
        parity_routes=[
            parity_route("main", {"controller_bindings": status("complete")}),
            parity_route("options", {"controller_bindings": status("pending")}),
        ],
    )

    assert result == 1
    assert "controller_bindings=2" in captured.out
    assert (
        "parity controller_bindings complete routes do not match "
        "controller_stub/runtime_stub routes"
    ) in captured.out
    assert "missing advanced routes ['options']" in captured.out
    assert "unexpected complete routes ['main']" in captured.out


def test_json_output_reports_phase_and_evidence_counts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        manifest_routes=[
            smoke_route("main", "runtime_stub"),
            smoke_route("options", "controller_stub"),
            smoke_route("downloads", "starter"),
        ],
        metadata_routes=[
            metadata_route("main", "runtime_stub"),
            metadata_route("options", "controller_stub"),
        ],
        parity_routes=[
            parity_route("main"),
            parity_route("options"),
            parity_route("downloads"),
        ],
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["routes_checked"] == 3
    assert payload["phase_counts"]["starter"] == 1
    assert payload["phase_counts"]["controller_stub"] == 1
    assert payload["phase_counts"]["runtime_stub"] == 1
    assert payload["metadata_backed_advanced_routes"] == 2
    assert payload["runtime_menu_mapped_routes"] == 1
    assert payload["parity_ready_routes"] == 0
    assert payload["missing_evidence_counts"] == {}
