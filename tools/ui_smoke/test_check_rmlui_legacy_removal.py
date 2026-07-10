from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_legacy_removal as legacy_removal  # noqa: E402
import check_rmlui_parity_manifest as parity  # noqa: E402


def write_json(path: Path, data: object) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    return path


def status(value: str) -> dict[str, str]:
    return {
        "status": value,
        "evidence": "test fixture",
    }


def phase_defaults(default_status: str = "pending") -> dict[str, dict[str, dict[str, str]]]:
    defaults: dict[str, dict[str, dict[str, str]]] = {}
    for phase in parity.MIGRATION_PHASES:
        defaults[phase] = {
            category: status(default_status)
            for category in parity.CANONICAL_CATEGORIES
        }
        defaults[phase]["document_load"] = status("complete")
    return defaults


def smoke_manifest(route_phase: str = "starter") -> dict[str, Any]:
    return {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            {
                "id": "main",
                "migration_phase": route_phase,
            }
        ],
    }


def parity_manifest(
    *,
    route_phase: str = "starter",
    defaults: dict[str, Any] | None = None,
) -> tuple[dict[str, Any], dict[str, Any]]:
    return smoke_manifest(route_phase), {
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
        "routes": [
            {
                "id": "main",
            }
        ],
    }


def open_parity_manifest() -> tuple[dict[str, Any], dict[str, Any]]:
    defaults = phase_defaults("complete")
    return parity_manifest(route_phase="parity_ready", defaults=defaults)


def removal_item(
    item_id: str,
    category: str,
    *,
    status_value: str = "blocked",
    task_ids: list[str] | None = None,
) -> dict[str, Any]:
    return {
        "id": item_id,
        "category": category,
        "status": status_value,
        "task_ids": task_ids if task_ids is not None else ["FR-09-T10", "FR-09-T09"],
        "targets": [
            {
                "kind": "test_surface",
                "path": f"src/{item_id}.cpp",
            }
        ],
        "blockers": [
            "test parity gate blocker",
        ],
    }


def removal_manifest(
    *,
    first_status: str = "blocked",
    omit_category: str | None = None,
) -> dict[str, Any]:
    item_specs = [
        ("json_surfaces", "json_menu_surfaces", first_status, ["FR-09-T10", "FR-09-T09"]),
        (
            "bridge_fallback",
            "legacy_bridge_runtime_fallback",
            "blocked",
            ["FR-09-T10", "DV-03-T07", "DV-07-T04"],
        ),
        ("package_cleanup", "package_staging_cleanup", "pending", ["FR-09-T10"]),
        ("docs_update", "docs_update", "pending", ["FR-09-T10", "DV-07-T04"]),
        (
            "renderer_input",
            "renderer_input_smoke_evidence",
            "blocked",
            ["FR-09-T09", "DV-03-T07", "DV-07-T04"],
        ),
    ]
    items = [
        removal_item(item_id, category, status_value=status_value, task_ids=task_ids)
        for item_id, category, status_value, task_ids in item_specs
        if category != omit_category
    ]
    return {
        "schema": legacy_removal.EXPECTED_SCHEMA,
        "required_task_ids": list(legacy_removal.REQUIRED_TASK_IDS),
        "allowed_statuses": list(legacy_removal.ALLOWED_STATUSES),
        "cutover_gate": {
            "parity_ready_routes_required": True,
            "required_evidence_categories": list(parity.CANONICAL_CATEGORIES),
        },
        "items": items,
    }


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    *,
    manifest: dict[str, Any] | None = None,
    smoke: dict[str, Any] | None = None,
    parity_data: dict[str, Any] | None = None,
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    if smoke is None or parity_data is None:
        smoke, parity_data = parity_manifest()

    manifest_path = write_json(
        repo_root / "tools/ui_smoke/rmlui_legacy_removal_manifest.json",
        manifest if manifest is not None else removal_manifest(),
    )
    smoke_path = write_json(repo_root / "tools/ui_smoke/rmlui_manifest.json", smoke)
    parity_path = write_json(repo_root / "tools/ui_smoke/rmlui_parity_manifest.json", parity_data)

    result = legacy_removal.main(
        [
            "--manifest",
            str(manifest_path),
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


def test_blocked_inventory_passes_when_parity_gate_closed(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(tmp_path / "repo", capsys)

    assert result == 0
    assert "Items checked: 5" in captured.out
    assert "Statuses: blocked=3, pending=2, ready=0, complete=0" in captured.out
    assert "Open: no" in captured.out
    assert "Parity-ready routes: 0" in captured.out
    assert "Result: RmlUi legacy removal inventory check passed." in captured.out


def test_ready_item_fails_with_zero_parity_ready_routes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        tmp_path / "repo",
        capsys,
        manifest=removal_manifest(first_status="ready"),
    )

    assert result == 1
    assert "Ready/complete removal items: 1" in captured.out
    assert "removal item 'json_surfaces' cannot be 'ready'" in captured.out
    assert "parity manifest has zero parity_ready routes" in captured.out


def test_complete_item_fails_with_incomplete_required_evidence(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    defaults = phase_defaults("complete")
    defaults["parity_ready"]["renderer_vulkan"] = status("pending")
    smoke, parity_data = parity_manifest(route_phase="parity_ready", defaults=defaults)

    result, captured = run_checker(
        tmp_path / "repo",
        capsys,
        manifest=removal_manifest(first_status="complete"),
        smoke=smoke,
        parity_data=parity_data,
    )

    assert result == 1
    assert "removal item 'json_surfaces' cannot be 'complete'" in captured.out
    assert "required parity evidence is incomplete: renderer_vulkan=1" in captured.out
    assert "parity_ready route 'main' category 'renderer_vulkan' is pending" in captured.out


def test_ready_item_passes_when_parity_gate_open(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    smoke, parity_data = open_parity_manifest()

    result, captured = run_checker(
        tmp_path / "repo",
        capsys,
        manifest=removal_manifest(first_status="ready"),
        smoke=smoke,
        parity_data=parity_data,
    )

    assert result == 0
    assert "Ready/complete removal items: 1" in captured.out
    assert "Open: yes" in captured.out
    assert "Result: RmlUi legacy removal inventory check passed." in captured.out


def test_missing_required_category_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        tmp_path / "repo",
        capsys,
        manifest=removal_manifest(omit_category="docs_update"),
    )

    assert result == 1
    assert "missing required removal category 'docs_update'" in captured.out


def test_invalid_status_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(
        tmp_path / "repo",
        capsys,
        manifest=removal_manifest(first_status="deleted"),
    )

    assert result == 1
    assert "removal item 'json_surfaces' status must be one of" in captured.out


def test_json_output_reports_inventory_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    result, captured = run_checker(tmp_path / "repo", capsys, output_format="json")

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload["ok"] is True
    assert payload["items_checked"] == 5
    assert payload["status_counts"]["blocked"] == 3
    assert payload["status_counts"]["pending"] == 2
    assert payload["missing_task_ids"] == []
    assert payload["parity_gate"]["open"] is False
    assert payload["parity_gate"]["parity_ready_routes"] == 0
    assert payload["ready_or_complete_items"] == []
    assert payload["errors"] == []
