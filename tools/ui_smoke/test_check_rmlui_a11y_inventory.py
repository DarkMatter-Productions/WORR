from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_a11y_inventory as a11y_inventory  # noqa: E402


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
                "schema": a11y_inventory.EXPECTED_SCHEMA,
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
    result = a11y_inventory.main(
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


def test_a11y_and_localization_hooks_are_inventoried(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/session/join.rml",
        """
<rml>
  <body>
    <main id="join" role="main" aria-labelledby="join-title">
      <h1 id="join-title" data-loc-key="m_join_match">Join Match</h1>
      <button id="settings"
              type="button"
              data-l10n="m_settings"
              aria-label="Open settings"
              tabindex="0"
              accesskey="s">Settings</button>
      <p id="hint" data-localization-key="m_join_hint" aria-describedby="join-title">
        Pick a team.
      </p>
    </main>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("join", "assets/ui/rml/session/join.rml")],
    )

    assert result == 0
    assert "Routes known: 1" in captured.out
    assert "Documents checked: present=1, missing=0" in captured.out
    assert "Total a11y/localization refs: 9" in captured.out
    assert "data-l10n: 1" in captured.out
    assert "data-loc-key: 1" in captured.out
    assert "data-localization-key: 1" in captured.out
    assert "aria-label: 1" in captured.out
    assert "aria-labelledby: 1" in captured.out
    assert "aria-describedby: 1" in captured.out
    assert "role: 1" in captured.out
    assert "tabindex: 1" in captured.out
    assert "accesskey: 1" in captured.out
    assert "Routes with a11y/localization hooks: 1" in captured.out
    assert "Unique localization keys: 3" in captured.out
    assert "Unique roles: 1" in captured.out
    assert "Malformed/empty hooks: 0" in captured.out
    assert "Result: RmlUi accessibility/localization inventory check passed." in captured.out


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
    assert "Result: RmlUi accessibility/localization inventory check failed." in captured.out


def test_empty_hooks_and_bad_tabindex_fail(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/bad.rml",
        """
<rml>
  <body>
    <button id="empty-label" aria-label=""></button>
    <button id="empty-loc" data-loc-key="  "></button>
    <button id="bad-tab" tabindex="first"></button>
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
    assert "Total a11y/localization refs: 3" in captured.out
    assert "Malformed/empty hooks: 3" in captured.out
    assert "aria-label is malformed: hook value must not be empty" in captured.out
    assert "data-loc-key is malformed: hook value must not be empty" in captured.out
    assert "tabindex is malformed: tabindex must be an integer string" in captured.out
    assert "Result: RmlUi accessibility/localization inventory check failed." in captured.out


def test_signed_integer_tabindex_values_are_accepted(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/settings/video.rml",
        """
<rml>
  <body>
    <button id="skip" tabindex="-1">Skip</button>
    <button id="next" tabindex="+1">Next</button>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("video", "assets/ui/rml/settings/video.rml")],
    )

    assert result == 0
    assert "tabindex: 2" in captured.out
    assert "Malformed/empty hooks: 0" in captured.out


def test_json_output_reports_a11y_inventory_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/session/leave_match_confirm.rml",
        """
<rml>
  <body>
    <dialog id="confirm" role="dialog" aria-labelledby="title" aria-describedby="prompt">
      <h1 id="title" data-loc-key="m_confirm_leave_match">Confirm</h1>
      <p id="prompt" data-loc-key="m_confirm_leave_match_prompt">Leave?</p>
    </dialog>
  </body>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("leave_match_confirm", "assets/ui/rml/session/leave_match_confirm.rml")],
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload["ok"] is True
    assert payload["route_count"] == 1
    assert payload["documents_checked"] == 1
    assert payload["documents_missing"] == 0
    assert payload["total_hook_refs"] == 5
    assert payload["refs_by_attribute"]["data-loc-key"] == 2
    assert payload["refs_by_attribute"]["role"] == 1
    assert payload["refs_by_attribute"]["aria-labelledby"] == 1
    assert payload["refs_by_attribute"]["aria-describedby"] == 1
    assert payload["routes_with_a11y_localization_hooks"]["routes"] == ["leave_match_confirm"]
    assert payload["unique_localization_keys"]["keys"] == [
        "m_confirm_leave_match",
        "m_confirm_leave_match_prompt",
    ]
    assert payload["unique_roles"]["roles"] == ["dialog"]
    assert payload["malformed_hooks"] == []
    assert payload["errors"] == []


def test_current_repository_a11y_inventory_is_broadly_stable() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    report = a11y_inventory.build_a11y_inventory(
        a11y_inventory.load_manifest(manifest_path),
        repo_root,
    )

    assert report.ok()
    assert report.stats.route_count >= 57
    assert report.stats.documents_checked >= 57
    assert report.stats.total_hook_refs >= 1
    assert len(report.routes_with_hooks) >= 1
    assert len(report.unique_localization_keys) >= 1
