from __future__ import annotations

import json
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import check_rmlui_route_contracts as route_contracts  # noqa: E402


def write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_text(path: Path, text: str = "<rml></rml>\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def write_contract(repo_root: Path, manifests: list[dict[str, str]]) -> Path:
    schema_path = Path("assets/ui/rml/contracts/route-contract.schema.json")
    write_json(
        repo_root / schema_path,
        {
            "$id": "test.route.contract",
            "$defs": {
                "route": {
                    "type": "object",
                    "required": ["id", "document"],
                    "properties": {
                        "id": {"type": "string"},
                        "document": {"type": "string"},
                    },
                }
            },
            "audit_contract": {
                "manifests": manifests,
            },
        },
    )
    return schema_path


def write_controller_fixture(repo_root: Path, name: str = "navigation.mock.json") -> None:
    write_json(
        repo_root / "assets/ui/rml/contracts" / name,
        {
            "schema": "worr.rml.controller.mock_fixture.v1",
            "category": "navigation",
            "models": {},
        },
    )


def controller_contract_ref(**overrides: object) -> dict[str, object]:
    contract_ref: dict[str, object] = {
        "category": "navigation",
        "contract": "worr.rml.controller.navigation.mock",
        "fixture": "navigation.mock.json",
        "model": "ui.navigation",
        "status": "mock_fixture",
        "notes": "Static test navigation controller contract fixture.",
    }
    contract_ref.update(overrides)
    return contract_ref


def controller_contract_manifest(contract_refs: object) -> dict[str, object]:
    return {
        "schema": "worr.rml.routes.v1",
        "routes": [
            {
                "id": "core.runtime_smoke",
                "owner": "agent1-platform-runtime",
                "document": "core/runtime_smoke.rml",
                "controller_contracts": contract_refs,
            }
        ],
    }


def test_audit_accepts_schema_manifest_profile_and_document_bases(tmp_path: Path) -> None:
    write_text(tmp_path / "assets/ui/rml/core/runtime_smoke.rml")
    write_text(tmp_path / "assets/ui/rml/shell/main.rml")

    write_json(
        tmp_path / "assets/ui/rml/core/routes.json",
        {
            "schema": "worr.rml.routes.v1",
            "routes": [
                {
                    "id": "core.runtime_smoke",
                    "owner": "agent1-platform-runtime",
                    "document": "core/runtime_smoke.rml",
                    "migration_phase": "starter",
                    "required_now": True,
                }
            ],
        },
    )
    write_json(
        tmp_path / "assets/ui/rml/shell/routes.json",
        {
            "schema": "worr.rml.agent4.routes.v1",
            "owner": "agent4-shell-settings-singleplayer",
            "routes": [
                {
                    "id": "main",
                    "document": "shell/main.rml",
                    "migration_phase": "controller_stub",
                    "status": "starter",
                    "required_now": True,
                }
            ],
        },
    )
    write_json(
        tmp_path / "tools/ui_smoke/rmlui_manifest.json",
        {
            "schema": "worr.rmlui.smoke_manifest.v1",
            "routes": [
                {
                    "id": "main",
                    "owner": "agent4-shell-settings-singleplayer",
                    "document": "assets/ui/rml/shell/main.rml",
                    "migration_phase": "runtime_stub",
                    "status": "starter",
                    "required_now": True,
                }
            ],
        },
    )
    schema_path = write_contract(
        tmp_path,
        [
            {
                "name": "core",
                "path": "assets/ui/rml/core/routes.json",
                "document_base": "assets/ui/rml",
                "schema": "worr.rml.routes.v1",
            },
            {
                "name": "shell",
                "path": "assets/ui/rml/shell/routes.json",
                "document_base": "assets/ui/rml",
                "schema": "worr.rml.agent4.routes.v1",
            },
            {
                "name": "smoke",
                "path": "tools/ui_smoke/rmlui_manifest.json",
                "document_base": ".",
                "schema": "worr.rmlui.smoke_manifest.v1",
            },
        ],
    )

    run = route_contracts.audit_route_contracts(tmp_path, schema_path)

    assert run.ok(), run.all_errors()
    reports = {report.spec.name: report for report in run.reports}
    assert reports["core"].required_now_present == 1
    assert reports["core"].migration_phases["starter"] == 1
    assert reports["shell"].owners["agent4-shell-settings-singleplayer"] == 1
    assert reports["shell"].migration_phases["controller_stub"] == 1
    assert reports["smoke"].required_now_present == 1
    assert reports["smoke"].migration_phases["runtime_stub"] == 1


def test_audit_discovers_feature_route_metadata_not_listed_in_contract(tmp_path: Path) -> None:
    write_text(tmp_path / "assets/ui/rml/core/runtime_smoke.rml")
    write_text(tmp_path / "assets/ui/rml/utility/keys.rml")
    write_json(
        tmp_path / "assets/ui/rml/core/routes.json",
        {
            "schema": "worr.rml.routes.v1",
            "routes": [
                {
                    "id": "core.runtime_smoke",
                    "owner": "agent1-platform-runtime",
                    "document": "core/runtime_smoke.rml",
                    "migration_phase": "starter",
                    "required_now": True,
                }
            ],
        },
    )
    write_json(
        tmp_path / "assets/ui/rml/utility/routes.json",
        {
            "schema": "worr.rml.agent5.routes.v1",
            "owner": "agent5-rich-tools-session-validation",
            "routes": [
                {
                    "id": "keys",
                    "document": "utility/keys.rml",
                    "migration_phase": "controller_stub",
                    "status": "scaffolded_round2",
                    "required_now": True,
                }
            ],
        },
    )
    schema_path = write_contract(
        tmp_path,
        [
            {
                "name": "core",
                "path": "assets/ui/rml/core/routes.json",
                "document_base": "assets/ui/rml",
                "schema": "worr.rml.routes.v1",
            }
        ],
    )

    run = route_contracts.audit_route_contracts(tmp_path, schema_path)

    assert run.ok(), run.all_errors()
    reports = {report.spec.name: report for report in run.reports}
    assert set(reports) == {"core", "utility"}
    assert reports["utility"].schema == "worr.rml.agent5.routes.v1"
    assert reports["utility"].required_now_present == 1
    assert reports["utility"].migration_phases["controller_stub"] == 1


def test_audit_reports_route_contract_failures(tmp_path: Path) -> None:
    spec = route_contracts.ManifestSpec(
        name="bad",
        path=Path("assets/ui/rml/bad/routes.json"),
        document_base=Path("assets/ui/rml"),
        expected_schema="worr.rml.routes.v1",
    )
    data = {
        "schema": "worr.rml.routes.v1",
        "routes": [
            {
                "id": "dupe",
                "owner": "agent1-platform-runtime",
                "document": "core/missing.rml",
                "required_now": True,
                "status": "starter",
                "migration_phase": "almost_done",
            },
            {
                "id": "dupe",
                "owner": "",
                "document": "../escape.rml",
                "status": "Bad Status",
            },
            {
                "id": "absolute_path",
                "owner": "agent1-platform-runtime",
                "document": "C:/absolute.rml",
                "required_now": "yes",
            },
        ],
    }

    report = route_contracts.audit_manifest_data(data, tmp_path, spec, ["id", "document"])

    errors = "\n".join(report.errors)
    assert "duplicate route id 'dupe'" in errors
    assert "required_now document does not exist: core/missing.rml" in errors
    assert "field 'owner' must be a non-empty string" in errors
    assert "field 'status' must use lowercase token characters" in errors
    assert "field 'migration_phase' must be one of" in errors
    assert "must not contain empty, '.', or '..' segments: ../escape.rml" in errors
    assert "field 'required_now' must be a boolean" in errors
    assert "document path must be relative, not absolute: C:/absolute.rml" in errors


def test_audit_requires_effective_owner(tmp_path: Path) -> None:
    write_text(tmp_path / "assets/ui/rml/core/runtime_smoke.rml")
    spec = route_contracts.ManifestSpec(
        name="ownerless",
        path=Path("assets/ui/rml/core/routes.json"),
        document_base=Path("assets/ui/rml"),
        expected_schema="worr.rml.routes.v1",
    )
    data = {
        "schema": "worr.rml.routes.v1",
        "routes": [
            {
                "id": "core.runtime_smoke",
                "document": "core/runtime_smoke.rml",
            }
        ],
    }

    report = route_contracts.audit_manifest_data(data, tmp_path, spec, ["id", "document"])

    assert "route 'core.runtime_smoke' is missing owner and the manifest has no owner" in report.errors


def test_audit_accepts_controller_contract_ref_with_fixture(tmp_path: Path) -> None:
    write_text(tmp_path / "assets/ui/rml/core/runtime_smoke.rml")
    write_controller_fixture(tmp_path)
    spec = route_contracts.ManifestSpec(
        name="core",
        path=Path("assets/ui/rml/core/routes.json"),
        document_base=Path("assets/ui/rml"),
        expected_schema="worr.rml.routes.v1",
    )
    data = controller_contract_manifest([controller_contract_ref()])

    report = route_contracts.audit_manifest_data(data, tmp_path, spec, ["id", "document"])

    assert not report.errors
    assert report.controller_contract_count == 1


def test_audit_reports_missing_controller_contract_fixture(tmp_path: Path) -> None:
    spec = route_contracts.ManifestSpec(
        name="core",
        path=Path("assets/ui/rml/core/routes.json"),
        document_base=Path("assets/ui/rml"),
        expected_schema="worr.rml.routes.v1",
    )
    data = controller_contract_manifest([controller_contract_ref(fixture="missing.mock.json")])

    report = route_contracts.audit_manifest_data(data, tmp_path, spec, ["id", "document"])

    errors = "\n".join(report.errors)
    assert "fixture file does not exist: missing.mock.json" in errors
    assert report.controller_contract_count == 1


def test_audit_reports_unsafe_controller_contract_fixture_path(tmp_path: Path) -> None:
    spec = route_contracts.ManifestSpec(
        name="core",
        path=Path("assets/ui/rml/core/routes.json"),
        document_base=Path("assets/ui/rml"),
        expected_schema="worr.rml.routes.v1",
    )
    data = controller_contract_manifest([controller_contract_ref(fixture="../escape.mock.json")])

    report = route_contracts.audit_manifest_data(data, tmp_path, spec, ["id", "document"])

    errors = "\n".join(report.errors)
    assert "fixture path must not contain empty, '.', or '..' segments: ../escape.mock.json" in errors
    assert report.controller_contract_count == 1


def test_audit_reports_invalid_controller_contract_shape_and_tokens(tmp_path: Path) -> None:
    write_controller_fixture(tmp_path)
    spec = route_contracts.ManifestSpec(
        name="core",
        path=Path("assets/ui/rml/core/routes.json"),
        document_base=Path("assets/ui/rml"),
        expected_schema="worr.rml.routes.v1",
    )
    data = controller_contract_manifest(
        [
            "navigation.mock.json",
            controller_contract_ref(
                category="Navigation",
                contract="",
                model="ui navigation",
                status="Mock Fixture",
                notes="",
            ),
        ]
    )

    report = route_contracts.audit_manifest_data(data, tmp_path, spec, ["id", "document"])

    errors = "\n".join(report.errors)
    assert "controller_contracts[0] must be an object" in errors
    assert "field 'category' must use lowercase token characters" in errors
    assert "field 'contract' must be a non-empty string" in errors
    assert "field 'model' must use lowercase token characters" in errors
    assert "field 'status' must use lowercase token characters" in errors
    assert "field 'notes' must be a non-empty string" in errors
    assert report.controller_contract_count == 2
