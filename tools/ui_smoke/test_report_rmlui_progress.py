from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest

from tools.ui_smoke import report_rmlui_progress as progress_report


main = progress_report.main


PARITY_CATEGORIES = (
    "document_load",
    "navigation",
    "controller_bindings",
    "renderer_open_gl",
    "renderer_vulkan",
    "renderer_rtx_vkpt",
    "screenshot_layout",
    "input_escape_back",
    "legacy_fallback",
)
PARITY_PHASES = (
    "starter",
    "controller_stub",
    "runtime_stub",
    "parity_pending",
    "parity_ready",
)


@pytest.fixture(autouse=True)
def missing_optional_checkers(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        progress_report,
        "DATA_MODEL_INVENTORY_SCRIPT",
        tmp_path / "missing_data_model_inventory.py",
    )
    monkeypatch.setattr(
        progress_report,
        "CONDITION_INVENTORY_SCRIPT",
        tmp_path / "missing_condition_inventory.py",
    )
    monkeypatch.setattr(
        progress_report,
        "METADATA_SYNC_SCRIPT",
        tmp_path / "missing_metadata_sync.py",
    )
    monkeypatch.setattr(
        progress_report,
        "EVENT_INVENTORY_SCRIPT",
        tmp_path / "missing_event_inventory.py",
    )
    monkeypatch.setattr(
        progress_report,
        "A11Y_INVENTORY_SCRIPT",
        tmp_path / "missing_a11y_inventory.py",
    )
    monkeypatch.setattr(
        progress_report,
        "LEGACY_REMOVAL_SCRIPT",
        tmp_path / "missing_legacy_removal.py",
    )


def write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_document(repo_root: Path, relative_path: str) -> None:
    path = repo_root / relative_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("<rml><body /></rml>\n", encoding="utf-8")


def route(
    route_id: str,
    document: str,
    *,
    wave: str,
    owner: str,
    status: str,
    migration_phase: str,
    required_now: bool = True,
) -> dict[str, Any]:
    return {
        "id": route_id,
        "wave": wave,
        "owner": owner,
        "document": document,
        "required_now": required_now,
        "status": status,
        "migration_phase": migration_phase,
    }


def controller_contract(category: str) -> dict[str, str]:
    return {
        "category": category,
        "contract": f"worr.rml.controller.{category}.mock",
        "fixture": f"{category}.mock.json",
        "model": f"ui.{category}",
        "status": "mock_fixture",
    }


def parity_status(value: str) -> dict[str, str]:
    return {
        "status": value,
        "evidence": "test fixture",
    }


def parity_phase_defaults() -> dict[str, dict[str, dict[str, str]]]:
    defaults: dict[str, dict[str, dict[str, str]]] = {}
    for phase in PARITY_PHASES:
        defaults[phase] = {
            category: parity_status("pending")
            for category in PARITY_CATEGORIES
        }
        defaults[phase]["document_load"] = parity_status("complete")

    defaults["controller_stub"]["controller_bindings"] = parity_status("complete")
    defaults["runtime_stub"]["controller_bindings"] = parity_status("complete")
    defaults["runtime_stub"]["legacy_fallback"] = parity_status("complete")
    defaults["parity_ready"] = {
        category: parity_status("pending")
        for category in PARITY_CATEGORIES
    }
    return defaults


def parity_manifest(routes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema": "worr.rmlui.parity_manifest.v1",
        "evidence_categories": [
            {
                "id": category,
                "kind": "test",
                "description": "test fixture",
            }
            for category in PARITY_CATEGORIES
        ],
        "phase_defaults": parity_phase_defaults(),
        "routes": routes,
    }


def parity_route(route_id: str, evidence: dict[str, Any] | None = None) -> dict[str, Any]:
    route_data: dict[str, Any] = {"id": route_id}
    if evidence is not None:
        route_data["evidence"] = evidence
    return route_data


def run_report(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    manifest: dict[str, Any],
    *args: str,
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    write_json(manifest_path, manifest)

    result = main(
        [
            "--manifest",
            str(manifest_path),
            "--repo-root",
            str(repo_root),
            *args,
        ]
    )
    return result, capsys.readouterr()


def test_text_report_summarizes_progress_counts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_document(tmp_path, "assets/ui/rml/shell/main.rml")
    write_document(tmp_path, "assets/ui/rml/settings/video.rml")

    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="starter",
            ),
            route(
                "video",
                "assets/ui/rml/settings/video.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="controller_stub",
            ),
            route(
                "scoreboard",
                "assets/ui/rml/hud/scoreboard.rml",
                wave="B",
                owner="agent3",
                status="runtime_stub",
                migration_phase="runtime_stub",
                required_now=False,
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest)

    assert result == 0
    assert "Total routes: 3" in captured.out
    assert "Documents: 2/3 present, 1 missing" in captured.out
    assert "Required documents: 2/2 present, 0 missing" in captured.out
    assert "By wave: A=2, B=1" in captured.out
    assert "By owner: agent3=1, agent4=2" in captured.out
    assert "By status: runtime_stub=1, starter=2" in captured.out
    assert "By migration_phase: starter=1, controller_stub=1, runtime_stub=1" in captured.out
    assert (
        "Phase progression: starter=1, controller_stub=1, runtime_stub=1, "
        "parity_pending=0, parity_ready=0, advanced_routes=2, advanced_percent=66.7%"
        in captured.out
    )


def test_markdown_report_emits_copy_paste_table(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_document(tmp_path, "assets/ui/rml/shell/main.rml")

    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="starter",
            ),
            route(
                "serverlist",
                "assets/ui/rml/session/serverlist.rml",
                wave="C",
                owner="agent5",
                status="pending",
                migration_phase="parity_pending",
                required_now=False,
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest, "--format", "markdown")

    assert result == 0
    assert captured.out.startswith("| Group | Counts |\n| --- | --- |\n")
    assert "| Total routes | 2 |" in captured.out
    assert "| Documents | present=1, missing=1 |" in captured.out
    assert "| Required documents | present=1, missing=0, total=1 |" in captured.out
    assert "| Wave | A=1, C=1 |" in captured.out
    assert "| Owner | agent4=1, agent5=1 |" in captured.out
    assert "| Status | pending=1, starter=1 |" in captured.out
    assert "| Migration phase | starter=1, parity_pending=1 |" in captured.out
    assert (
        "| Phase progression | starter=1, controller_stub=0, runtime_stub=0, "
        "parity_pending=1, parity_ready=0, advanced_routes=1, advanced_percent=50.0% |"
        in captured.out
    )


def test_json_report_emits_machine_readable_counts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_document(tmp_path, "assets/ui/rml/shell/main.rml")

    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="B",
                owner="agent5",
                status="ready",
                migration_phase="controller_stub",
            ),
            route(
                "video",
                "assets/ui/rml/settings/video.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="starter",
            ),
            route(
                "serverlist",
                "assets/ui/rml/session/serverlist.rml",
                wave="A",
                owner="agent5",
                status="starter",
                migration_phase="parity_pending",
                required_now=False,
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest, "--format", "json")
    payload = json.loads(captured.out)

    assert result == 0
    assert captured.err == ""
    assert set(payload) == {
        "manifest_path",
        "total_routes",
        "documents",
        "required_documents",
        "grouped_counts",
        "phase_progression",
        "routes_by_phase",
        "controller_contracts",
        "command_inventory",
        "cvar_inventory",
        "data_model_inventory",
        "condition_inventory",
        "metadata_sync",
        "event_inventory",
        "a11y_inventory",
        "legacy_removal",
    }
    assert payload["manifest_path"] == "tools/ui_smoke/rmlui_manifest.json"
    assert payload["total_routes"] == 3
    assert payload["documents"] == {"present": 1, "missing": 2}
    assert payload["required_documents"] == {"present": 1, "missing": 1, "total": 2}
    assert payload["grouped_counts"] == {
        "wave": {"A": 2, "B": 1},
        "owner": {"agent4": 1, "agent5": 2},
        "status": {"ready": 1, "starter": 2},
        "migration_phase": {
            "starter": 1,
            "controller_stub": 1,
            "parity_pending": 1,
        },
    }
    assert payload["phase_progression"] == {
        "starter": 1,
        "controller_stub": 1,
        "runtime_stub": 0,
        "parity_pending": 1,
        "parity_ready": 0,
        "advanced_routes": 2,
        "advanced_percent": 66.7,
    }
    assert payload["routes_by_phase"] == [
        {"phase": "starter", "route_ids": ["video"]},
        {"phase": "controller_stub", "route_ids": ["main"]},
        {"phase": "runtime_stub", "route_ids": []},
        {"phase": "parity_pending", "route_ids": ["serverlist"]},
        {"phase": "parity_ready", "route_ids": []},
    ]
    assert payload["controller_contracts"] == {
        "total_references": 0,
        "routes_with_contracts": 0,
        "by_category": {},
        "by_migration_phase": {},
    }
    assert payload["command_inventory"] == {
        "ok": False,
        "route_count": 3,
        "documents_checked": 1,
        "documents_missing": 2,
        "direct_command_refs": 0,
        "cvar_command_refs": 0,
        "unique_command_tokens": 0,
        "unique_cvar_command_refs": 0,
        "malformed_command_attributes": 0,
        "routes_with_command_hooks": 0,
        "errors": [
            "route 'video' document file does not exist: assets/ui/rml/settings/video.rml",
            "route 'serverlist' document file does not exist: assets/ui/rml/session/serverlist.rml",
        ],
    }
    assert payload["cvar_inventory"] == {
        "ok": False,
        "route_count": 3,
        "documents_checked": 1,
        "documents_missing": 2,
        "references": {
            "direct": 0,
            "label": 0,
            "command": 0,
            "condition": 0,
            "total": 0,
        },
        "unique_cvars": 0,
        "routes_with_cvar_hooks": 0,
        "dynamic_values_skipped": 0,
        "unknown_or_bad_tokens": 0,
        "errors": [
            "route 'video' missing route document assets/ui/rml/settings/video.rml",
            "route 'serverlist' missing route document assets/ui/rml/session/serverlist.rml",
        ],
    }
    assert payload["data_model_inventory"] == {
        "available": False,
        "ok": False,
        "status": "unavailable",
        "route_count": 0,
        "documents_checked": 0,
        "documents_missing": 0,
        "total_data_binding_refs": 0,
        "unique_model_tokens": 0,
        "routes_with_data_model_hooks": 0,
        "references": {
            "component": 0,
            "controller": 0,
            "action_type": 0,
            "slot": 0,
        },
        "malformed_tokens": 0,
        "errors": [
            "data-model inventory checker not found: missing_data_model_inventory.py",
        ],
    }
    assert payload["condition_inventory"] == {
        "available": False,
        "ok": False,
        "status": "unavailable",
        "route_count": 0,
        "documents_checked": 0,
        "documents_missing": 0,
        "total_condition_refs": 0,
        "routes_with_condition_hooks": 0,
        "unique_expressions": 0,
        "unique_tokens": 0,
        "malformed_conditions": 0,
        "errors": [
            "condition inventory checker not found: missing_condition_inventory.py",
        ],
    }
    assert payload["metadata_sync"] == {
        "available": False,
        "ok": False,
        "status": "unavailable",
        "metadata_files": 0,
        "metadata_routes": 0,
        "matched_routes": 0,
        "support_metadata_routes": 0,
        "central_routes_without_metadata": 0,
        "advanced_missing_metadata": 0,
        "phase_mismatches": 0,
        "document_mismatches": 0,
        "duplicate_metadata_routes": 0,
        "errors": [
            "metadata sync checker not found: missing_metadata_sync.py",
        ],
    }
    assert payload["event_inventory"] == {
        "available": False,
        "ok": False,
        "status": "unavailable",
        "route_count": 0,
        "documents_checked": 0,
        "documents_missing": 0,
        "total_event_refs": 0,
        "routes_with_event_hooks": 0,
        "unique_events": 0,
        "malformed_events": 0,
        "errors": [
            "event inventory checker not found: missing_event_inventory.py",
        ],
    }
    assert payload["a11y_inventory"] == {
        "available": False,
        "ok": False,
        "status": "unavailable",
        "route_count": 0,
        "documents_checked": 0,
        "documents_missing": 0,
        "total_a11y_refs": 0,
        "routes_with_a11y_hooks": 0,
        "unique_localization_keys": 0,
        "unique_roles": 0,
        "malformed_hooks": 0,
        "errors": [
            "a11y inventory checker not found: missing_a11y_inventory.py",
        ],
    }
    assert payload["legacy_removal"] == {
        "available": False,
        "ok": False,
        "status": "unavailable",
        "items_checked": 0,
        "categories_checked": 0,
        "status_counts": {},
        "category_counts": {},
        "missing_task_ids": [],
        "ready_or_complete_items": {
            "count": 0,
            "items": [],
        },
        "parity_gate": {
            "open": False,
            "state": "closed",
            "ok": False,
            "parity_ready_routes": 0,
            "pending_evidence": {},
            "closed_reasons": [],
            "errors": [],
        },
        "errors": [
            "legacy-removal checker not found: missing_legacy_removal.py",
        ],
    }
    assert "parity_checklist" not in payload


def test_text_report_includes_optional_parity_checklist_summary(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    parity_path = tmp_path / "tools/ui_smoke/rmlui_parity_manifest.json"
    write_json(
        parity_path,
        parity_manifest(
            [
                parity_route("main"),
                parity_route("game"),
            ]
        ),
    )
    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="starter",
                required_now=False,
            ),
            route(
                "game",
                "assets/ui/rml/shell/game.rml",
                wave="A",
                owner="agent4",
                status="runtime_stub",
                migration_phase="runtime_stub",
                required_now=False,
            ),
        ],
    }

    result, captured = run_report(
        tmp_path,
        capsys,
        manifest,
        "--parity-manifest",
        str(parity_path),
    )

    assert result == 0
    assert "Parity checklist: 2 routes, categories=9, parity_ready_routes=0" in captured.out
    assert "pending: document_load=0, navigation=2, controller_bindings=1" in captured.out
    assert "renderer_vulkan=2" in captured.out
    assert "complete: document_load=2, navigation=0, controller_bindings=1" in captured.out
    assert "legacy_fallback=1" in captured.out


def test_json_report_includes_optional_parity_checklist_counts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    parity_path = tmp_path / "tools/ui_smoke/rmlui_parity_manifest.json"
    write_json(
        parity_path,
        parity_manifest(
            [
                parity_route(
                    "main",
                    {
                        "navigation": parity_status("complete"),
                    },
                ),
                parity_route("game"),
            ]
        ),
    )
    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="starter",
                required_now=False,
            ),
            route(
                "game",
                "assets/ui/rml/shell/game.rml",
                wave="A",
                owner="agent4",
                status="runtime_stub",
                migration_phase="runtime_stub",
                required_now=False,
            ),
        ],
    }

    result, captured = run_report(
        tmp_path,
        capsys,
        manifest,
        "--format",
        "json",
        "--parity-manifest",
        str(parity_path),
    )
    payload = json.loads(captured.out)

    assert result == 0
    assert captured.err == ""
    assert payload["parity_checklist"] == {
        "manifest_path": "tools/ui_smoke/rmlui_parity_manifest.json",
        "routes_checked": 2,
        "categories_checked": 9,
        "parity_ready_routes": 0,
        "pending_counts": {
            "document_load": 0,
            "navigation": 1,
            "controller_bindings": 1,
            "renderer_open_gl": 2,
            "renderer_vulkan": 2,
            "renderer_rtx_vkpt": 2,
            "screenshot_layout": 2,
            "input_escape_back": 2,
            "legacy_fallback": 1,
        },
        "complete_counts": {
            "document_load": 2,
            "navigation": 1,
            "controller_bindings": 1,
            "renderer_open_gl": 0,
            "renderer_vulkan": 0,
            "renderer_rtx_vkpt": 0,
            "screenshot_layout": 0,
            "input_escape_back": 0,
            "legacy_fallback": 1,
        },
    }


def test_missing_parity_manifest_keeps_report_usable(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="starter",
                required_now=False,
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest)

    assert result == 0
    assert captured.err == ""
    assert "Parity checklist:" not in captured.out


def test_report_includes_data_model_inventory_in_all_formats(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    write_document(tmp_path, "assets/ui/rml/shell/main.rml")

    def fake_data_model_inventory(
        repo_root: Path,
        smoke_data: dict[str, Any],
        manifest_path: Path,
    ) -> progress_report.DataModelInventorySummary:
        assert repo_root == tmp_path
        assert smoke_data["routes"][0]["id"] == "main"
        assert manifest_path.name == "rmlui_manifest.json"
        return progress_report.DataModelInventorySummary(
            route_count=2,
            documents_checked=2,
            documents_missing=0,
            total_data_binding_refs=14,
            unique_model_tokens=5,
            component_refs=4,
            controller_refs=3,
            action_type_refs=2,
            slot_refs=1,
            routes_with_data_model_hooks=2,
            malformed_tokens=0,
        )

    monkeypatch.setattr(
        progress_report,
        "load_data_model_inventory_summary",
        fake_data_model_inventory,
    )

    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="controller_stub",
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest)

    assert result == 0
    assert "Command inventory:" in captured.out
    assert "Cvar inventory:" in captured.out
    assert (
        "Data-model inventory: status=ok, ok=true, routes=2, "
        "documents_checked=2, documents_missing=0, total_refs=14, "
        "unique_model_tokens=5, routes_with_hooks=2, components=4, "
        "controllers=3, action_types=2, slots=1, malformed=0"
        in captured.out
    )

    result, captured = run_report(
        tmp_path,
        capsys,
        manifest,
        "--format",
        "markdown",
    )

    assert result == 0
    assert "| Command inventory |" in captured.out
    assert "| Cvar inventory |" in captured.out
    assert (
        "| Data-model inventory | status=ok, ok=true, routes=2, "
        "documents_checked=2, documents_missing=0, total_refs=14, "
        "unique_model_tokens=5, routes_with_hooks=2, components=4, "
        "controllers=3, action_types=2, slots=1, malformed=0 |"
        in captured.out
    )

    result, captured = run_report(tmp_path, capsys, manifest, "--format", "json")
    payload = json.loads(captured.out)

    assert result == 0
    assert captured.err == ""
    assert "command_inventory" in payload
    assert "cvar_inventory" in payload
    assert payload["data_model_inventory"] == {
        "available": True,
        "ok": True,
        "status": "ok",
        "route_count": 2,
        "documents_checked": 2,
        "documents_missing": 0,
        "total_data_binding_refs": 14,
        "unique_model_tokens": 5,
        "routes_with_data_model_hooks": 2,
        "references": {
            "component": 4,
            "controller": 3,
            "action_type": 2,
            "slot": 1,
        },
        "malformed_tokens": 0,
        "errors": [],
    }


def test_missing_data_model_inventory_checker_keeps_report_usable(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="starter",
                required_now=False,
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest)

    assert result == 0
    assert captured.err == ""
    assert (
        "Data-model inventory: status=unavailable, ok=false, "
        "error=data-model inventory checker not found: missing_data_model_inventory.py"
        in captured.out
    )

    result, captured = run_report(tmp_path, capsys, manifest, "--format", "json")
    payload = json.loads(captured.out)

    assert result == 0
    assert captured.err == ""
    assert payload["data_model_inventory"]["available"] is False
    assert payload["data_model_inventory"]["status"] == "unavailable"
    assert payload["data_model_inventory"]["errors"] == [
        "data-model inventory checker not found: missing_data_model_inventory.py",
    ]


def test_report_includes_round12_guardrail_summaries_in_all_formats(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    write_document(tmp_path, "assets/ui/rml/shell/main.rml")

    def fake_condition_inventory(
        repo_root: Path,
        smoke_data: dict[str, Any],
        manifest_path: Path,
    ) -> progress_report.ConditionInventorySummary:
        assert repo_root == tmp_path
        assert smoke_data["routes"][0]["id"] == "main"
        assert manifest_path.name == "rmlui_manifest.json"
        return progress_report.ConditionInventorySummary(
            route_count=4,
            documents_checked=4,
            documents_missing=0,
            total_condition_refs=9,
            routes_with_condition_hooks=3,
            unique_expressions=7,
            unique_tokens=5,
            malformed_conditions=0,
        )

    def fake_metadata_sync(
        repo_root: Path,
        smoke_data: dict[str, Any],
        manifest_path: Path,
    ) -> progress_report.MetadataSyncSummary:
        assert repo_root == tmp_path
        assert smoke_data["routes"][0]["id"] == "main"
        assert manifest_path.name == "rmlui_manifest.json"
        return progress_report.MetadataSyncSummary(
            metadata_files=8,
            metadata_routes=58,
            matched_routes=57,
            support_metadata_routes=1,
            central_routes_without_metadata=0,
            advanced_missing_metadata=0,
            phase_mismatches=0,
            document_mismatches=0,
            duplicate_metadata_routes=0,
        )

    monkeypatch.setattr(
        progress_report,
        "load_condition_inventory_summary",
        fake_condition_inventory,
    )
    monkeypatch.setattr(
        progress_report,
        "load_metadata_sync_summary",
        fake_metadata_sync,
    )

    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="controller_stub",
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest)

    assert result == 0
    assert "Command inventory:" in captured.out
    assert "Cvar inventory:" in captured.out
    assert "Data-model inventory:" in captured.out
    assert (
        "Condition inventory: status=ok, ok=true, routes=4, "
        "documents_checked=4, documents_missing=0, total_refs=9, "
        "routes_with_hooks=3, unique_expressions=7, unique_tokens=5, malformed=0"
        in captured.out
    )
    assert (
        "Metadata sync: status=ok, ok=true, metadata_files=8, metadata_routes=58, "
        "matched_routes=57, support_metadata_routes=1, central_without_metadata=0, "
        "advanced_missing_metadata=0, phase_mismatches=0, document_mismatches=0, "
        "duplicate_metadata_routes=0"
        in captured.out
    )

    result, captured = run_report(
        tmp_path,
        capsys,
        manifest,
        "--format",
        "markdown",
    )

    assert result == 0
    assert "| Condition inventory | status=ok, ok=true, routes=4" in captured.out
    assert "| Metadata sync | status=ok, ok=true, metadata_files=8" in captured.out

    result, captured = run_report(tmp_path, capsys, manifest, "--format", "json")
    payload = json.loads(captured.out)

    assert result == 0
    assert captured.err == ""
    assert payload["condition_inventory"] == {
        "available": True,
        "ok": True,
        "status": "ok",
        "route_count": 4,
        "documents_checked": 4,
        "documents_missing": 0,
        "total_condition_refs": 9,
        "routes_with_condition_hooks": 3,
        "unique_expressions": 7,
        "unique_tokens": 5,
        "malformed_conditions": 0,
        "errors": [],
    }
    assert payload["metadata_sync"] == {
        "available": True,
        "ok": True,
        "status": "ok",
        "metadata_files": 8,
        "metadata_routes": 58,
        "matched_routes": 57,
        "support_metadata_routes": 1,
        "central_routes_without_metadata": 0,
        "advanced_missing_metadata": 0,
        "phase_mismatches": 0,
        "document_mismatches": 0,
        "duplicate_metadata_routes": 0,
        "errors": [],
    }


def test_round12_guardrail_unavailable_error_and_skip_behavior(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    write_document(tmp_path, "assets/ui/rml/shell/main.rml")
    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="starter",
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest)

    assert result == 0
    assert captured.err == ""
    assert (
        "Condition inventory: status=unavailable, ok=false, "
        "error=condition inventory checker not found: missing_condition_inventory.py"
        in captured.out
    )
    assert (
        "Metadata sync: status=unavailable, ok=false, "
        "error=metadata sync checker not found: missing_metadata_sync.py"
        in captured.out
    )
    assert (
        "Event inventory: status=unavailable, ok=false, "
        "error=event inventory checker not found: missing_event_inventory.py"
        in captured.out
    )
    assert (
        "A11y inventory: status=unavailable, ok=false, "
        "error=a11y inventory checker not found: missing_a11y_inventory.py"
        in captured.out
    )
    assert (
        "Legacy removal: status=unavailable, ok=false, "
        "error=legacy-removal checker not found: missing_legacy_removal.py"
        in captured.out
    )

    monkeypatch.setattr(
        progress_report,
        "load_condition_inventory_summary",
        lambda *_args: progress_report.ConditionInventorySummary(
            available=True,
            ok=False,
            status="error",
            errors=["condition parse failed"],
        ),
    )
    monkeypatch.setattr(
        progress_report,
        "load_metadata_sync_summary",
        lambda *_args: progress_report.MetadataSyncSummary(
            available=True,
            ok=False,
            status="error",
            errors=["metadata duplicate failed"],
        ),
    )
    monkeypatch.setattr(
        progress_report,
        "load_event_inventory_summary",
        lambda *_args: progress_report.EventInventorySummary(
            available=True,
            ok=False,
            status="error",
            errors=["event parse failed"],
        ),
    )
    monkeypatch.setattr(
        progress_report,
        "load_a11y_inventory_summary",
        lambda *_args: progress_report.A11yInventorySummary(
            available=True,
            ok=False,
            status="error",
            errors=["a11y parse failed"],
        ),
    )
    monkeypatch.setattr(
        progress_report,
        "load_legacy_removal_summary",
        lambda *_args: progress_report.LegacyRemovalSummary(
            available=True,
            ok=False,
            status="error",
            errors=["legacy removal parse failed"],
        ),
    )

    result, captured = run_report(
        tmp_path,
        capsys,
        manifest,
        "--format",
        "markdown",
    )

    assert result == 0
    assert "| Condition inventory | status=error, ok=false, error=condition parse failed |" in captured.out
    assert "| Metadata sync | status=error, ok=false, error=metadata duplicate failed |" in captured.out
    assert "| Event inventory | status=error, ok=false, error=event parse failed |" in captured.out
    assert "| A11y inventory | status=error, ok=false, error=a11y parse failed |" in captured.out
    assert "| Legacy removal | status=error, ok=false, error=legacy removal parse failed |" in captured.out

    result, captured = run_report(
        tmp_path,
        capsys,
        manifest,
        "--format",
        "json",
    )
    payload = json.loads(captured.out)

    assert result == 0
    assert payload["condition_inventory"]["status"] == "error"
    assert payload["condition_inventory"]["errors"] == ["condition parse failed"]
    assert payload["metadata_sync"]["status"] == "error"
    assert payload["metadata_sync"]["errors"] == ["metadata duplicate failed"]
    assert payload["event_inventory"]["status"] == "error"
    assert payload["event_inventory"]["errors"] == ["event parse failed"]
    assert payload["a11y_inventory"]["status"] == "error"
    assert payload["a11y_inventory"]["errors"] == ["a11y parse failed"]
    assert payload["legacy_removal"]["status"] == "error"
    assert payload["legacy_removal"]["errors"] == ["legacy removal parse failed"]

    result, captured = run_report(
        tmp_path,
        capsys,
        manifest,
        "--format",
        "json",
        "--no-inventory-summary",
    )
    payload = json.loads(captured.out)

    assert result == 0
    assert "command_inventory" not in payload
    assert "cvar_inventory" not in payload
    assert "data_model_inventory" not in payload
    assert "condition_inventory" not in payload
    assert "metadata_sync" not in payload
    assert "event_inventory" not in payload
    assert "a11y_inventory" not in payload
    assert "legacy_removal" not in payload


def test_round13_checker_files_present_are_reported_in_all_formats(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    write_document(tmp_path, "assets/ui/rml/shell/main.rml")

    event_checker = tmp_path / "check_rmlui_event_inventory.py"
    event_checker.write_text(
        """
def build_event_inventory(smoke_data, repo_root=None, manifest_path=None):
    return {
        "event_inventory": {
            "ok": True,
            "route_count": 5,
            "documents_checked": 5,
            "documents_missing": 0,
            "total_event_refs": 17,
            "routes_with_event_hooks": ["main", "options"],
            "unique_event_names": ["apply", "back", "open"],
            "malformed_event_attributes": [],
            "errors": [],
        }
    }
""".lstrip(),
        encoding="utf-8",
    )
    a11y_checker = tmp_path / "check_rmlui_a11y_inventory.py"
    a11y_checker.write_text(
        """
def build_a11y_inventory(smoke_data, repo_root=None, manifest_path=None):
    return {
        "a11y_inventory": {
            "ok": True,
            "route_count": 5,
            "documents_checked": 5,
            "documents_missing": 0,
            "total_a11y_refs": 21,
            "routes_with_a11y_hooks": ["main", "options", "keys"],
            "unique_localization_keys": {"count": 12},
            "unique_roles": ["button", "navigation"],
            "malformed_hooks": [],
            "errors": [],
        }
    }
""".lstrip(),
        encoding="utf-8",
    )
    monkeypatch.setattr(progress_report, "EVENT_INVENTORY_SCRIPT", event_checker)
    monkeypatch.setattr(
        progress_report,
        "EVENT_INVENTORY_MODULE",
        "tools.ui_smoke.missing_round13_event_inventory_for_test",
    )
    monkeypatch.setattr(progress_report, "A11Y_INVENTORY_SCRIPT", a11y_checker)
    monkeypatch.setattr(
        progress_report,
        "A11Y_INVENTORY_MODULE",
        "tools.ui_smoke.missing_round13_a11y_inventory_for_test",
    )

    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="controller_stub",
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest)

    assert result == 0
    assert (
        "Event inventory: status=ok, ok=true, routes=5, "
        "documents_checked=5, documents_missing=0, total_refs=17, "
        "routes_with_hooks=2, unique_events=3, malformed=0"
        in captured.out
    )
    assert (
        "A11y inventory: status=ok, ok=true, routes=5, "
        "documents_checked=5, documents_missing=0, total_refs=21, "
        "routes_with_hooks=3, unique_localization_keys=12, "
        "unique_roles=2, malformed=0"
        in captured.out
    )

    result, captured = run_report(
        tmp_path,
        capsys,
        manifest,
        "--format",
        "markdown",
    )

    assert result == 0
    assert "| Event inventory | status=ok, ok=true, routes=5" in captured.out
    assert "| A11y inventory | status=ok, ok=true, routes=5" in captured.out

    result, captured = run_report(tmp_path, capsys, manifest, "--format", "json")
    payload = json.loads(captured.out)

    assert result == 0
    assert captured.err == ""
    assert payload["event_inventory"] == {
        "available": True,
        "ok": True,
        "status": "ok",
        "route_count": 5,
        "documents_checked": 5,
        "documents_missing": 0,
        "total_event_refs": 17,
        "routes_with_event_hooks": 2,
        "unique_events": 3,
        "malformed_events": 0,
        "errors": [],
    }
    assert payload["a11y_inventory"] == {
        "available": True,
        "ok": True,
        "status": "ok",
        "route_count": 5,
        "documents_checked": 5,
        "documents_missing": 0,
        "total_a11y_refs": 21,
        "routes_with_a11y_hooks": 3,
        "unique_localization_keys": 12,
        "unique_roles": 2,
        "malformed_hooks": 0,
        "errors": [],
    }


def test_round14_legacy_removal_checker_present_is_reported_in_all_formats(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    write_document(tmp_path, "assets/ui/rml/shell/main.rml")

    legacy_checker = tmp_path / "check_rmlui_legacy_removal.py"
    legacy_checker.write_text(
        """
def build_legacy_removal_inventory(smoke_data, repo_root=None, manifest_path=None):
    return {
        "legacy_removal": {
            "ok": True,
            "items_checked": 6,
            "categories_checked": 5,
            "status_counts": {
                "blocked": 4,
                "pending": 2,
                "ready": 0,
                "complete": 0,
            },
            "category_counts": {
                "json_menu_surfaces": 2,
                "legacy_bridge_runtime_fallback": 1,
                "package_staging_cleanup": 1,
                "docs_update": 1,
                "renderer_input_smoke_evidence": 1,
            },
            "missing_task_ids": [],
            "ready_or_complete_items": ["json_cleanup", "bridge_cleanup"],
            "parity_gate": {
                "open": False,
                "ok": True,
                "parity_ready_routes": 2,
                "pending_evidence": {
                    "navigation": 1,
                    "renderer_vulkan": 2,
                },
                "closed_reasons": ["required parity evidence is incomplete"],
                "errors": [],
            },
            "errors": [],
        }
    }
""".lstrip(),
        encoding="utf-8",
    )
    monkeypatch.setattr(progress_report, "LEGACY_REMOVAL_SCRIPT", legacy_checker)
    monkeypatch.setattr(
        progress_report,
        "LEGACY_REMOVAL_MODULE",
        "tools.ui_smoke.missing_round14_legacy_removal_for_test",
    )

    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="controller_stub",
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest)

    assert result == 0
    assert (
        "Legacy removal: status=ok, ok=true, items_checked=6, "
        "categories_checked=5"
        in captured.out
    )
    assert "status_counts=(blocked=4, complete=0, pending=2, ready=0)" in captured.out
    assert "missing_task_ids=0" in captured.out
    assert "ready_or_complete=2 [json_cleanup,bridge_cleanup]" in captured.out
    assert "parity_gate=closed, parity_ready_routes=2" in captured.out

    result, captured = run_report(
        tmp_path,
        capsys,
        manifest,
        "--format",
        "markdown",
    )

    assert result == 0
    assert "| Legacy removal | status=ok, ok=true, items_checked=6" in captured.out
    assert "parity_gate=closed, parity_ready_routes=2 |" in captured.out

    result, captured = run_report(tmp_path, capsys, manifest, "--format", "json")
    payload = json.loads(captured.out)

    assert result == 0
    assert captured.err == ""
    assert payload["legacy_removal"] == {
        "available": True,
        "ok": True,
        "status": "ok",
        "items_checked": 6,
        "categories_checked": 5,
        "status_counts": {
            "blocked": 4,
            "pending": 2,
            "ready": 0,
            "complete": 0,
        },
        "category_counts": {
            "json_menu_surfaces": 2,
            "legacy_bridge_runtime_fallback": 1,
            "package_staging_cleanup": 1,
            "docs_update": 1,
            "renderer_input_smoke_evidence": 1,
        },
        "missing_task_ids": [],
        "ready_or_complete_items": {
            "count": 2,
            "items": ["json_cleanup", "bridge_cleanup"],
        },
        "parity_gate": {
            "open": False,
            "state": "closed",
            "ok": True,
            "parity_ready_routes": 2,
            "pending_evidence": {
                "navigation": 1,
                "renderer_vulkan": 2,
            },
            "closed_reasons": ["required parity evidence is incomplete"],
            "errors": [],
        },
        "errors": [],
    }


def test_round12_guardrail_coercion_accepts_checker_payload_aliases() -> None:
    condition = progress_report.coerce_condition_inventory_summary(
        {
            "ok": True,
            "route_count": 57,
            "documents_checked": 57,
            "documents_missing": 0,
            "total_condition_refs": 141,
            "routes_with_condition_hooks": {"count": 22, "routes": ["game"]},
            "unique_condition_expressions": {
                "count": 114,
                "expressions": ["deathmatch=0"],
            },
            "unique_condition_tokens": {"count": 111, "tokens": ["deathmatch"]},
            "malformed_condition_attributes": [],
            "errors": [],
        }
    )

    assert condition.ok is True
    assert condition.total_condition_refs == 141
    assert condition.routes_with_condition_hooks == 22
    assert condition.unique_expressions == 114
    assert condition.unique_tokens == 111
    assert condition.malformed_conditions == 0

    metadata = progress_report.coerce_metadata_sync_summary(
        {
            "ok": False,
            "metadata_file_count": 5,
            "metadata_route_count": 58,
            "matched_route_count": 57,
            "support_metadata_routes": {
                "count": 1,
                "routes": ["core.runtime_smoke"],
            },
            "central_routes_without_feature_metadata": {
                "count": 0,
                "routes": [],
            },
            "advanced_central_routes_without_feature_metadata": {
                "count": 0,
                "routes": [],
            },
            "phase_mismatch_count": 0,
            "document_mismatch_count": 0,
            "duplicate_count": 0,
            "errors": ["metadata route 'core.runtime_smoke' has no central route"],
        }
    )

    assert metadata.ok is False
    assert metadata.metadata_files == 5
    assert metadata.metadata_routes == 58
    assert metadata.matched_routes == 57
    assert metadata.support_metadata_routes == 1
    assert metadata.central_routes_without_metadata == 0
    assert metadata.advanced_missing_metadata == 0
    assert metadata.phase_mismatches == 0
    assert metadata.document_mismatches == 0
    assert metadata.duplicate_metadata_routes == 0
    assert metadata.errors == ["metadata route 'core.runtime_smoke' has no central route"]

    event = progress_report.coerce_event_inventory_summary(
        {
            "ok": True,
            "routes_checked": 57,
            "documents_checked": 57,
            "documents_missing": 0,
            "event_references": {"count": 93},
            "routes_with_events": ["main", "game"],
            "unique_event_names": ["apply", "back", "open"],
            "malformed_event_attributes": [],
            "errors": [],
        }
    )

    assert event.ok is True
    assert event.route_count == 57
    assert event.total_event_refs == 93
    assert event.routes_with_event_hooks == 2
    assert event.unique_events == 3
    assert event.malformed_events == 0

    a11y = progress_report.coerce_a11y_inventory_summary(
        {
            "ok": False,
            "routes_checked": 57,
            "documents_checked": 57,
            "documents_missing": 0,
            "accessibility_refs": {"count": 44},
            "routes_with_accessibility_hooks": ["accessibility", "keys"],
            "unique_localization_keys": {"count": 18},
            "roles": ["button", "navigation", "region"],
            "malformed_accessibility_attributes": ["bad tabindex"],
            "errors": ["bad tabindex"],
        }
    )

    assert a11y.ok is False
    assert a11y.status == "error"
    assert a11y.route_count == 57
    assert a11y.total_a11y_refs == 44
    assert a11y.routes_with_a11y_hooks == 2
    assert a11y.unique_localization_keys == 18
    assert a11y.unique_roles == 3
    assert a11y.malformed_hooks == 1
    assert a11y.errors == ["bad tabindex"]

    legacy = progress_report.coerce_legacy_removal_summary(
        {
            "legacy_removal_inventory": {
                "ok": True,
                "items": 6,
                "categories": {"count": 5},
                "statuses": {"blocked": "4", "pending": 2},
                "categories_by_id": {
                    "json_menu_surfaces": 2,
                    "docs_update": 1,
                },
                "missing_tasks": ["FR-09-T10"],
                "ready_items": ["json_cleanup"],
                "parity_gate": {
                    "open": False,
                    "ok": True,
                    "ready_routes": 3,
                    "pending_counts": {"navigation": 1},
                    "closed_reason": "navigation pending",
                },
                "errors": [],
            }
        }
    )

    assert legacy.ok is True
    assert legacy.status == "ok"
    assert legacy.items_checked == 6
    assert legacy.categories_checked == 5
    assert legacy.status_counts == {"blocked": 4, "pending": 2}
    assert legacy.category_counts == {
        "json_menu_surfaces": 2,
        "docs_update": 1,
    }
    assert legacy.missing_task_ids == ["FR-09-T10"]
    assert legacy.ready_or_complete_items == ["json_cleanup"]
    assert legacy.parity_gate_open is False
    assert legacy.parity_gate_ok is True
    assert legacy.parity_ready_routes == 3
    assert legacy.parity_gate_pending_evidence == {"navigation": 1}
    assert legacy.parity_gate_closed_reasons == ["navigation pending"]


def test_json_report_includes_shell_controller_contract_summary(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_document(tmp_path, "assets/ui/rml/shell/main.rml")
    write_json(
        tmp_path / "assets/ui/rml/shell/routes.json",
        {
            "schema": "worr.rml.agent4.routes.v1",
            "routes": [
                {
                    "id": "main",
                    "document": "shell/main.rml",
                    "migration_phase": "controller_stub",
                    "controller_contracts": [
                        controller_contract("navigation"),
                        controller_contract("command_action"),
                    ],
                },
                {
                    "id": "video",
                    "document": "settings/video.rml",
                    "migration_phase": "parity_pending",
                    "controller_contracts": [
                        controller_contract("cvar_binding"),
                        controller_contract("navigation"),
                    ],
                },
                {
                    "id": "downloads",
                    "document": "shell/downloads.rml",
                    "migration_phase": "starter",
                },
            ],
        },
    )
    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="controller_stub",
            ),
        ],
    }

    result, captured = run_report(
        tmp_path,
        capsys,
        manifest,
        "--format",
        "json",
        "--shell-routes",
        "assets/ui/rml/shell/routes.json",
    )
    payload = json.loads(captured.out)

    assert result == 0
    assert captured.err == ""
    assert payload["controller_contracts"] == {
        "total_references": 4,
        "routes_with_contracts": 2,
        "by_category": {
            "command_action": 1,
            "cvar_binding": 1,
            "navigation": 2,
        },
        "by_migration_phase": {
            "controller_stub": 1,
            "parity_pending": 1,
        },
    }


def test_json_report_discovers_all_route_metadata_controller_contracts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    write_document(tmp_path, "assets/ui/rml/shell/main.rml")
    write_json(
        tmp_path / "assets/ui/rml/shell/routes.json",
        {
            "schema": "worr.rml.agent4.routes.v1",
            "routes": [
                {
                    "id": "main",
                    "document": "shell/main.rml",
                    "migration_phase": "runtime_stub",
                    "controller_contracts": [
                        controller_contract("navigation"),
                        controller_contract("command_action"),
                    ],
                }
            ],
        },
    )
    write_json(
        tmp_path / "assets/ui/rml/utility/routes.json",
        {
            "schema": "worr.rml.agent5.routes.v1",
            "routes": [
                {
                    "id": "keys",
                    "document": "utility/keys.rml",
                    "migration_phase": "controller_stub",
                    "controller_contracts": [
                        controller_contract("keybind"),
                    ],
                }
            ],
        },
    )
    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="runtime_stub",
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest, "--format", "json")
    payload = json.loads(captured.out)

    assert result == 0
    assert captured.err == ""
    assert payload["controller_contracts"] == {
        "total_references": 3,
        "routes_with_contracts": 2,
        "by_category": {
            "command_action": 1,
            "keybind": 1,
            "navigation": 1,
        },
        "by_migration_phase": {
            "controller_stub": 1,
            "runtime_stub": 1,
        },
    }


def test_json_report_uses_empty_controller_contract_summary_without_shell_routes(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "main",
                "assets/ui/rml/shell/main.rml",
                wave="A",
                owner="agent4",
                status="starter",
                migration_phase="starter",
                required_now=False,
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest, "--format", "json")
    payload = json.loads(captured.out)

    assert result == 0
    assert captured.err == ""
    assert payload["controller_contracts"] == {
        "total_references": 0,
        "routes_with_contracts": 0,
        "by_category": {},
        "by_migration_phase": {},
    }


def test_json_report_includes_phase_progression_and_routes_by_phase_ordering(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    manifest = {
        "schema": "worr.rmlui.smoke_manifest.v1",
        "routes": [
            route(
                "runtime_z",
                "assets/ui/rml/runtime_z.rml",
                wave="A",
                owner="agent5",
                status="runtime_stub",
                migration_phase="runtime_stub",
                required_now=False,
            ),
            route(
                "starter_only",
                "assets/ui/rml/starter_only.rml",
                wave="A",
                owner="agent5",
                status="starter",
                migration_phase="starter",
                required_now=False,
            ),
            route(
                "runtime_a",
                "assets/ui/rml/runtime_a.rml",
                wave="A",
                owner="agent5",
                status="runtime_stub",
                migration_phase="runtime_stub",
                required_now=False,
            ),
            route(
                "controller",
                "assets/ui/rml/controller.rml",
                wave="A",
                owner="agent5",
                status="controller_stub",
                migration_phase="controller_stub",
                required_now=False,
            ),
            route(
                "parity_ready",
                "assets/ui/rml/parity_ready.rml",
                wave="A",
                owner="agent5",
                status="parity_ready",
                migration_phase="parity_ready",
                required_now=False,
            ),
        ],
    }

    result, captured = run_report(tmp_path, capsys, manifest, "--format", "json")
    payload = json.loads(captured.out)

    assert result == 0
    assert captured.err == ""
    assert payload["phase_progression"] == {
        "starter": 1,
        "controller_stub": 1,
        "runtime_stub": 2,
        "parity_pending": 0,
        "parity_ready": 1,
        "advanced_routes": 4,
        "advanced_percent": 80.0,
    }
    assert payload["routes_by_phase"] == [
        {"phase": "starter", "route_ids": ["starter_only"]},
        {"phase": "controller_stub", "route_ids": ["controller"]},
        {"phase": "runtime_stub", "route_ids": ["runtime_a", "runtime_z"]},
        {"phase": "parity_pending", "route_ids": []},
        {"phase": "parity_ready", "route_ids": ["parity_ready"]},
    ]


def test_invalid_manifest_shape_returns_nonzero(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    manifest = {"schema": "worr.rmlui.smoke_manifest.v1", "routes": {"main": {}}}

    result, captured = run_report(tmp_path, capsys, manifest)

    assert result == 1
    assert captured.out == ""
    assert "manifest field 'routes' must be a list" in captured.err
