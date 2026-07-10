from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_event_inventory as event_inventory  # noqa: E402


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
                "schema": event_inventory.EXPECTED_SCHEMA,
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
    result = event_inventory.main(
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


def test_event_action_attrs_are_inventoried(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/utility/keys.rml",
        """
<rml>
  <body>
    <button id="capture" data-bind-command="+attack" data-event-click="keybind.capture">Attack</button>
    <input id="volume" data-event-change="cvar.commit" data-command="set s_volume 1; echo &quot;hi;there&quot;" />
    <button id="options" data-command="pushmenu options" data-route-target="options">Options</button>
    <section id="save" data-action-type="savegame">
      <button id="slot" data-command-cvar="ui_list_item_cmd_0">Slot</button>
    </section>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("keys", "assets/ui/rml/utility/keys.rml")],
    )

    assert result == 0
    assert "Routes known: 1" in captured.out
    assert "Documents checked: present=1, missing=0" in captured.out
    assert "Total event/action refs: 8" in captured.out
    assert "data-event-click: 1" in captured.out
    assert "data-event-change: 1" in captured.out
    assert "data-command: 2" in captured.out
    assert "data-route-target: 1" in captured.out
    assert "data-action-type: 1" in captured.out
    assert "data-bind-command: 1" in captured.out
    assert "data-command-cvar: 1" in captured.out
    assert "Routes with event hooks: 1" in captured.out
    assert "Unique event tokens: 2" in captured.out
    assert "Unique action tokens: 1" in captured.out
    assert "Unique route-target tokens: 1" in captured.out
    assert "Unique command tokens: 3" in captured.out
    assert "Unique bind-command refs: 1" in captured.out
    assert "Unique command-cvar refs: 1" in captured.out
    assert "Malformed/empty event attributes: 0" in captured.out
    assert "Command tokens: echo, pushmenu, set" in captured.out
    assert "Result: RmlUi event/action inventory check passed." in captured.out


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
    assert "Result: RmlUi event/action inventory check failed." in captured.out


def test_empty_event_action_attrs_fail(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/bad.rml",
        """
<rml>
  <body>
    <button id="empty-event" data-event-click=""></button>
    <button id="empty-route" data-route-target="   "></button>
    <button id="empty-command-cvar" data-command-cvar=""></button>
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
    assert "Total event/action refs: 3" in captured.out
    assert "Malformed/empty event attributes: 3" in captured.out
    assert "empty interaction attribute" in captured.out
    assert "data-event-click is malformed" in captured.out
    assert "data-route-target is malformed" in captured.out
    assert "data-command-cvar is malformed" in captured.out
    assert "Result: RmlUi event/action inventory check failed." in captured.out


def test_json_output_reports_event_inventory_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/session/admin_menu.rml",
        """
<rml>
  <body>
    <button id="admin" data-command="pushmenu admin_commands" data-route-target="admin_commands">Admin</button>
    <button id="capture" data-bind-command="+attack" data-event-click="keybind.capture">Attack</button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("admin_menu", "assets/ui/rml/session/admin_menu.rml")],
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload["ok"] is True
    assert payload["route_count"] == 1
    assert payload["documents_checked"] == 1
    assert payload["documents_missing"] == 0
    assert payload["total_event_action_refs"] == 4
    assert payload["refs_by_attribute"]["data-command"] == 1
    assert payload["refs_by_attribute"]["data-route-target"] == 1
    assert payload["refs_by_attribute"]["data-bind-command"] == 1
    assert payload["refs_by_attribute"]["data-event-click"] == 1
    assert payload["routes_with_event_hooks"]["routes"] == ["admin_menu"]
    assert payload["unique_event_tokens"]["tokens"] == ["keybind.capture"]
    assert payload["unique_action_tokens"]["tokens"] == []
    assert payload["unique_route_target_tokens"]["tokens"] == ["admin_commands"]
    assert payload["unique_command_tokens"]["tokens"] == ["pushmenu"]
    assert payload["unique_bind_command_refs"]["refs"] == ["+attack"]
    assert payload["unique_command_cvars"]["cvars"] == []
    assert payload["malformed_event_attributes"] == []
    assert payload["errors"] == []


def test_current_repository_event_inventory_is_broadly_stable() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    report = event_inventory.build_event_inventory(
        event_inventory.load_manifest(manifest_path),
        repo_root,
    )

    assert report.ok()
    assert report.stats.route_count >= 57
    assert report.stats.documents_checked >= 57
    assert report.stats.total_event_action_refs > 0
    assert len(report.routes_with_event_hooks) > 0
    assert len(report.unique_command_tokens) > 0
