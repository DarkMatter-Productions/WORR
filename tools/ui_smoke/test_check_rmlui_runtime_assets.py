from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

import pytest


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_runtime_assets as runtime_assets  # noqa: E402


def write_text(path: Path, text: str = "<rml></rml>\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def write_manifest(repo_root: Path, routes: list[dict[str, Any]]) -> Path:
    manifest_path = repo_root / "tools/ui_smoke/rmlui_manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(
            {
                "schema": runtime_assets.EXPECTED_SCHEMA,
                "routes": routes,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return manifest_path


def route(route_id: str, document: str, *, required_now: bool = True) -> dict[str, Any]:
    return {
        "id": route_id,
        "document": document,
        "required_now": required_now,
    }


def run_checker(
    repo_root: Path,
    capsys: pytest.CaptureFixture[str],
    routes: list[dict[str, Any]],
    *,
    install_dir: Path | None = None,
    base_game: str = "basew",
    include_imports: bool = False,
    output_format: str = "text",
    write_manifest_path: Path | None = None,
) -> tuple[int, pytest.CaptureResult[str]]:
    manifest_path = write_manifest(repo_root, routes)
    args = [
        "--manifest",
        str(manifest_path),
        "--repo-root",
        str(repo_root),
        "--format",
        output_format,
    ]
    if include_imports:
        args.append("--include-imports")
    if install_dir is not None:
        args.extend(["--install-dir", str(install_dir), "--base-game", base_game])
    if write_manifest_path is not None:
        args.extend(["--write-manifest", str(write_manifest_path)])

    result = runtime_assets.main(args)
    return result, capsys.readouterr()


def test_valid_mapping_derives_runtime_path(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(repo_root / "assets/ui/rml/shell/main.rml")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
    )

    assert result == 0
    assert runtime_assets.derive_runtime_path(
        "assets/ui/rml/shell/main.rml"
    ).as_posix() == "ui/rml/shell/main.rml"
    assert "Routes checked: 1" in captured.out
    assert "Source documents: present=1, missing=0" in captured.out
    assert "Runtime paths derived: 1" in captured.out
    assert "Result: RmlUi runtime asset path check passed." in captured.out


def test_json_success_without_imports_reports_runtime_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(repo_root / "assets/ui/rml/shell/main.rml")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload == {
        "ok": True,
        "routes_checked": 1,
        "source_documents": {
            "present": 1,
            "missing": 0,
        },
        "imported_assets": {
            "discovered": 0,
            "present": 0,
            "missing": 0,
        },
        "runtime_paths": {
            "total": 1,
            "route_documents": 1,
            "imported_assets": 0,
        },
        "staging_requested": False,
        "staged_loose_files": {
            "present": 0,
            "missing": 0,
        },
        "errors": [],
    }


def test_invalid_non_rml_path_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(repo_root / "assets/ui/rml/shell/main.txt", "not rml\n")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.txt")],
    )

    assert result == 1
    assert "document path must point to an .rml file" in captured.out
    assert "Runtime paths derived: 0" in captured.out


def test_missing_required_source_doc_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
    )

    assert result == 1
    assert "Source documents: present=0, missing=1" in captured.out
    assert "route 'main' missing required source document assets/ui/rml/shell/main.rml" in captured.out


def test_staged_loose_file_check_reports_missing_required_doc(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".tmp/package"
    write_text(repo_root / "assets/ui/rml/shell/main.rml")
    write_text(repo_root / "assets/ui/rml/shell/options.rml")
    write_text(install_dir / "basew/ui/rml/shell/main.rml")

    result, captured = run_checker(
        repo_root,
        capsys,
        [
            route("main", "assets/ui/rml/shell/main.rml"),
            route("options", "assets/ui/rml/shell/options.rml"),
        ],
        install_dir=install_dir,
        base_game="basew",
    )

    assert result == 1
    assert "Source documents: present=2, missing=0" in captured.out
    assert "Staged loose files: present=1, missing=1" in captured.out
    assert (
        "route 'options' missing required staged loose file "
        ".tmp/package/basew/ui/rml/shell/options.rml"
    ) in captured.out


def test_include_imports_counts_local_rml_and_rcss_assets(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        """
<rml>
  <head>
    <link href="../shared/theme.rcss" />
    <link href="panel.rml" />
  </head>
</rml>
""",
    )
    write_text(
        repo_root / "assets/ui/rml/shell/panel.rml",
        """
<rml>
  <head>
    <link href="../shared/controls.rcss" />
  </head>
</rml>
""",
    )
    write_text(repo_root / "assets/ui/rml/shared/theme.rcss", "body {}\n")
    write_text(repo_root / "assets/ui/rml/shared/controls.rcss", "button {}\n")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
        include_imports=True,
    )

    assert result == 0
    assert "Source documents: present=1, missing=0" in captured.out
    assert "Imported assets: discovered=3, present=3, missing=0" in captured.out
    assert "Runtime paths derived: 4 (route documents=1, imported assets=3)" in captured.out


def test_json_success_with_imports_and_staging_reports_import_facts(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".tmp/package"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        """
<rml>
  <head>
    <link href="../shared/theme.rcss" />
  </head>
</rml>
""",
    )
    write_text(repo_root / "assets/ui/rml/shared/theme.rcss", "body {}\n")
    write_text(install_dir / "basew/ui/rml/shell/main.rml")
    write_text(install_dir / "basew/ui/rml/shared/theme.rcss", "body {}\n")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
        install_dir=install_dir,
        base_game="basew",
        include_imports=True,
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 0
    assert captured.err == ""
    assert payload == {
        "ok": True,
        "routes_checked": 1,
        "source_documents": {
            "present": 1,
            "missing": 0,
        },
        "imported_assets": {
            "discovered": 1,
            "present": 1,
            "missing": 0,
        },
        "runtime_paths": {
            "total": 2,
            "route_documents": 1,
            "imported_assets": 1,
        },
        "staging_requested": True,
        "staged_loose_files": {
            "present": 2,
            "missing": 0,
        },
        "staged_loose_imported_assets": {
            "present": 1,
            "missing": 0,
        },
        "errors": [],
    }


def test_write_manifest_without_staging_records_source_and_runtime_assets(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    output_arg = Path(".tmp/rmlui/runtime-assets.json")
    output_path = repo_root / output_arg
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        """
<rml>
  <head>
    <link href="../shared/theme.rcss" />
  </head>
</rml>
""",
    )
    write_text(repo_root / "assets/ui/rml/shared/theme.rcss", "body {}\n")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
        include_imports=True,
        write_manifest_path=output_arg,
    )

    payload = json.loads(output_path.read_text(encoding="utf-8"))
    assert result == 0
    assert "Result: RmlUi runtime asset path check passed." in captured.out
    assert payload["schema"] == runtime_assets.ASSET_MANIFEST_SCHEMA
    assert payload["ok"] is True
    assert payload["include_imports"] is True
    assert payload["staging_requested"] is False
    assert payload["install_dir"] is None
    assert payload["base_game"] is None
    assert payload["route_documents"] == [
        {
            "document": "assets/ui/rml/shell/main.rml",
            "required_now": True,
            "route_id": "main",
            "runtime_path": "ui/rml/shell/main.rml",
            "source_path": "assets/ui/rml/shell/main.rml",
            "source_present": True,
        }
    ]
    assert payload["imported_assets"] == [
        {
            "href": "../shared/theme.rcss",
            "importer_path": "assets/ui/rml/shell/main.rml",
            "required_now": True,
            "route_id": "main",
            "runtime_path": "ui/rml/shared/theme.rcss",
            "source_path": "assets/ui/rml/shared/theme.rcss",
            "source_present": True,
        }
    ]


def test_write_manifest_with_staging_records_staged_presence(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".tmp/package"
    output_path = repo_root / ".tmp/rmlui/runtime-assets.json"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        """
<rml>
  <head>
    <link href="../shared/theme.rcss" />
  </head>
</rml>
""",
    )
    write_text(repo_root / "assets/ui/rml/shared/theme.rcss", "body {}\n")
    write_text(install_dir / "basew/ui/rml/shell/main.rml")
    write_text(install_dir / "basew/ui/rml/shared/theme.rcss", "body {}\n")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
        install_dir=install_dir,
        base_game="basew",
        include_imports=True,
        write_manifest_path=output_path,
    )

    payload = json.loads(output_path.read_text(encoding="utf-8"))
    assert result == 0
    assert "Staged loose files: present=2, missing=0" in captured.out
    assert payload["staging_requested"] is True
    assert payload["install_dir"] == ".tmp/package"
    assert payload["base_game"] == "basew"
    assert payload["route_documents"][0]["staged_loose_path"] == (
        ".tmp/package/basew/ui/rml/shell/main.rml"
    )
    assert payload["route_documents"][0]["staged_loose_present"] is True
    assert payload["imported_assets"][0]["staged_loose_path"] == (
        ".tmp/package/basew/ui/rml/shared/theme.rcss"
    )
    assert payload["imported_assets"][0]["staged_loose_present"] is True


def test_write_manifest_records_missing_staged_asset(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".tmp/package"
    output_path = repo_root / ".tmp/rmlui/runtime-assets.json"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        """
<rml>
  <head>
    <link href="../shared/theme.rcss" />
  </head>
</rml>
""",
    )
    write_text(repo_root / "assets/ui/rml/shared/theme.rcss", "body {}\n")
    write_text(install_dir / "basew/ui/rml/shell/main.rml")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
        install_dir=install_dir,
        base_game="basew",
        include_imports=True,
        write_manifest_path=output_path,
    )

    payload = json.loads(output_path.read_text(encoding="utf-8"))
    assert result == 1
    assert "Staged loose imported assets: present=0, missing=1" in captured.out
    assert payload["ok"] is False
    assert payload["summary"]["staged_loose_imported_assets"] == {
        "present": 0,
        "missing": 1,
    }
    assert payload["route_documents"][0]["staged_loose_present"] is True
    assert payload["imported_assets"][0]["staged_loose_present"] is False
    assert payload["errors"] == [
        "route 'main' missing required staged loose import "
        ".tmp/package/basew/ui/rml/shared/theme.rcss"
    ]


def test_include_imports_avoids_recursive_duplicate_assets(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        """
<rml>
  <head>
    <link href="a.rml" />
    <link href="b.rml" />
    <link href="../shared/common.rcss" />
  </head>
</rml>
""",
    )
    write_text(
        repo_root / "assets/ui/rml/shell/a.rml",
        """
<rml>
  <head>
    <link href="../shared/common.rcss" />
  </head>
</rml>
""",
    )
    write_text(
        repo_root / "assets/ui/rml/shell/b.rml",
        """
<rml>
  <head>
    <link href="../shared/common.rcss" />
  </head>
</rml>
""",
    )
    write_text(repo_root / "assets/ui/rml/shared/common.rcss", "body {}\n")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
        include_imports=True,
    )

    assert result == 0
    assert "Imported assets: discovered=3, present=3, missing=0" in captured.out
    assert "Runtime paths derived: 4 (route documents=1, imported assets=3)" in captured.out


def test_include_imports_missing_required_import_fails(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        """
<rml>
  <head>
    <link href="missing.rcss" />
  </head>
</rml>
""",
    )

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
        include_imports=True,
    )

    assert result == 1
    assert "Imported assets: discovered=1, present=0, missing=1" in captured.out
    assert (
        "route 'main' missing required local href import missing.rcss "
        "referenced by assets/ui/rml/shell/main.rml"
    ) in captured.out


def test_include_imports_validates_staged_loose_imports(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".tmp/package"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        """
<rml>
  <head>
    <link href="../shared/theme.rcss" />
  </head>
</rml>
""",
    )
    write_text(repo_root / "assets/ui/rml/shared/theme.rcss", "body {}\n")
    write_text(install_dir / "basew/ui/rml/shell/main.rml")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
        install_dir=install_dir,
        base_game="basew",
        include_imports=True,
    )

    assert result == 1
    assert "Imported assets: discovered=1, present=1, missing=0" in captured.out
    assert "Staged loose files: present=1, missing=1" in captured.out
    assert "Staged loose imported assets: present=0, missing=1" in captured.out
    assert (
        "route 'main' missing required staged loose import "
        ".tmp/package/basew/ui/rml/shared/theme.rcss"
    ) in captured.out


def test_json_failure_with_missing_staged_import_reports_errors(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    repo_root = tmp_path / "repo"
    install_dir = repo_root / ".tmp/package"
    write_text(
        repo_root / "assets/ui/rml/shell/main.rml",
        """
<rml>
  <head>
    <link href="../shared/theme.rcss" />
  </head>
</rml>
""",
    )
    write_text(repo_root / "assets/ui/rml/shared/theme.rcss", "body {}\n")
    write_text(install_dir / "basew/ui/rml/shell/main.rml")

    result, captured = run_checker(
        repo_root,
        capsys,
        [route("main", "assets/ui/rml/shell/main.rml")],
        install_dir=install_dir,
        base_game="basew",
        include_imports=True,
        output_format="json",
    )

    payload = json.loads(captured.out)
    assert result == 1
    assert captured.err == ""
    assert payload["ok"] is False
    assert payload["routes_checked"] == 1
    assert payload["source_documents"] == {
        "present": 1,
        "missing": 0,
    }
    assert payload["imported_assets"] == {
        "discovered": 1,
        "present": 1,
        "missing": 0,
    }
    assert payload["runtime_paths"] == {
        "total": 2,
        "route_documents": 1,
        "imported_assets": 1,
    }
    assert payload["staging_requested"] is True
    assert payload["staged_loose_files"] == {
        "present": 1,
        "missing": 1,
    }
    assert payload["staged_loose_imported_assets"] == {
        "present": 0,
        "missing": 1,
    }
    assert payload["errors"] == [
        "route 'main' missing required staged loose import "
        ".tmp/package/basew/ui/rml/shared/theme.rcss"
    ]
