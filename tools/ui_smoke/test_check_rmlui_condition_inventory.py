from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_condition_inventory as condition_inventory  # noqa: E402


def write_text(path: Path, text: str = "<rml></rml>\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def route(route_id: str, document: str) -> dict[str, Any]:
    return {
        "id": route_id,
        "document": document,
        "required_now": True,
    }


def write_manifest(repo_root: Path, routes: list[dict[str, Any]]) -> Path:
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(
            {
                "schema": condition_inventory.EXPECTED_SCHEMA,
                "routes": routes,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return manifest_path


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    routes: list[dict[str, Any]],
    *,
    output_format: str = "text",
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = write_manifest(repo_root, routes)
    result = condition_inventory.main(
        [
            "--manifest",
            str(manifest_path),
            "--repo-root",
            str(repo_root),
            "--format",
            output_format,
        ]
    )
    return result, capsys.readouterr()


def test_supported_condition_attrs_are_inventoried(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/session/mymap_main.rml",
        """
<rml>
  <body>
    <section id="mode" data-show-if="coop=0;match_setup_type=tournament">
      <button id="save" data-enable-if="ingame;deathmatch=0">Save</button>
      <button id="visible" data-visible-if="ui_dm_show_join=1;ui_dm_teamplay=1">Join</button>
      <button id="enabled" data-enabled-if="ui_mymap_can_select=1">Select</button>
      <span id="badge" data-condition="ui_tourney_veto_turn">Turn</span>
    </section>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("mymap_main", "assets/ui/rml/session/mymap_main.rml")],
    )

    assert result == 0
    assert "Routes known: 1" in captured.out
    assert "Documents checked: present=1, missing=0" in captured.out
    assert "Total condition refs: 5" in captured.out
    assert "data-show-if: 1" in captured.out
    assert "data-enable-if: 1" in captured.out
    assert "data-visible-if: 1" in captured.out
    assert "data-enabled-if: 1" in captured.out
    assert "data-condition: 1" in captured.out
    assert "Routes with condition hooks: 1" in captured.out
    assert "Unique condition tokens/cvars: 8" in captured.out
    assert "Malformed/empty condition attributes: 0" in captured.out
    assert "Result: RmlUi condition inventory check passed." in captured.out


def test_negated_static_condition_attrs_are_supported(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/session/report.rml",
        """
<rml>
  <body>
    <section id="fallback" data-show-if="!ui_report_line_0">Fallback</section>
    <button id="action" data-enable-if="!ui_report_busy">Action</button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("report", "assets/ui/rml/session/report.rml")],
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert payload["ok"] is True
    assert payload["total_condition_refs"] == 2
    assert payload["unique_condition_expressions"]["expressions"] == [
        "!ui_report_busy",
        "!ui_report_line_0",
    ]
    assert payload["unique_condition_tokens"]["tokens"] == [
        "ui_report_busy",
        "ui_report_line_0",
    ]
    assert payload["malformed_condition_attributes"] == []


def test_missing_document_fails_with_manifest_context(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("missing", "assets/ui/rml/missing.rml")],
    )

    assert result == 1
    assert "Routes known: 1" in captured.out
    assert "Documents checked: present=0, missing=1" in captured.out
    assert "missing route document assets/ui/rml/missing.rml" in captured.out
    assert "Result: RmlUi condition inventory check failed." in captured.out


def test_empty_and_malformed_condition_attrs_fail(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/bad.rml",
        """
<rml>
  <body>
    <section id="empty" data-show-if=""></section>
    <section id="empty-clause" data-visible-if="foo;;bar"></section>
    <section id="bad-token" data-enabled-if="BadToken=1"></section>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("bad", "assets/ui/rml/bad.rml")],
    )

    assert result == 1
    assert "Total condition refs: 3" in captured.out
    assert "Malformed/empty condition attributes: 3" in captured.out
    assert "empty condition expression" in captured.out
    assert "empty condition clause(s): 2" in captured.out
    assert "condition token must use lowercase snake_case style" in captured.out
    assert "Result: RmlUi condition inventory check failed." in captured.out


def test_unsupported_dynamic_condition_is_reported_without_failing(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/dynamic.rml",
        """
<rml>
  <body>
    <section id="dynamic" data-condition="{{condition}}"></section>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("dynamic", "assets/ui/rml/dynamic.rml")],
    )

    assert result == 0
    assert "Unsupported non-static conditions: 1" in captured.out
    assert "dynamic template condition expression" in captured.out
    assert "Malformed/empty condition attributes: 0" in captured.out
    assert "Result: RmlUi condition inventory check passed." in captured.out


def test_json_output_reports_condition_inventory_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/session/join.rml",
        """
<rml>
  <body>
    <button id="join-red" data-visible-if="ui_dm_show_join=1;ui_dm_teamplay=1">Join Red</button>
    <button id="join-free" data-enabled-if="ui_dm_show_join=1;ui_dm_teamplay=0">Join</button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("join", "assets/ui/rml/session/join.rml")],
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload["ok"] is True
    assert payload["route_count"] == 1
    assert payload["documents_checked"] == 1
    assert payload["documents_missing"] == 0
    assert payload["total_condition_refs"] == 2
    assert payload["refs_by_attribute"]["data-visible-if"] == 1
    assert payload["refs_by_attribute"]["data-enabled-if"] == 1
    assert payload["routes_with_condition_hooks"]["routes"] == ["join"]
    assert payload["unique_condition_expressions"]["count"] == 2
    assert payload["unique_condition_tokens"]["tokens"] == [
        "ui_dm_show_join",
        "ui_dm_teamplay",
    ]
    assert payload["malformed_condition_attributes"] == []
    assert payload["unsupported_condition_attributes"] == []
    assert payload["errors"] == []


def test_current_repository_condition_inventory_is_broadly_stable() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    report = condition_inventory.build_condition_inventory(
        condition_inventory.load_manifest(manifest_path),
        repo_root,
    )

    assert report.ok()
    assert report.stats.route_count >= 57
    assert report.stats.documents_checked >= 57
    assert report.stats.total_condition_refs > 0
    assert len(report.routes_with_condition_hooks) > 0
    assert len(report.unique_condition_tokens) > 0
