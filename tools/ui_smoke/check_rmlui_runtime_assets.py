#!/usr/bin/env python3
"""Validate RmlUi route document source paths and runtime asset paths."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any
from urllib.parse import unquote, urlsplit
from xml.etree import ElementTree


EXPECTED_SCHEMA = "worr.rmlui.smoke_manifest.v1"
ASSET_MANIFEST_SCHEMA = "worr.rmlui.runtime_asset_manifest.v1"
SOURCE_ROOT = PurePosixPath("assets/ui/rml")
RUNTIME_ROOT = PurePosixPath("ui/rml")
IMPORT_SUFFIXES = {".rml", ".rcss"}


@dataclass(frozen=True)
class RouteAsset:
    route_id: str
    document: str
    source_path: Path
    runtime_path: PurePosixPath
    required_now: bool


@dataclass
class ImportedAsset:
    route_id: str
    href: str
    importer_path: Path
    source_path: Path
    runtime_path: PurePosixPath
    required_now: bool


@dataclass
class RuntimeAssetStats:
    routes_checked: int = 0
    source_documents_present: int = 0
    source_documents_missing: int = 0
    runtime_paths_derived: int = 0
    staged_loose_files_present: int = 0
    staged_loose_files_missing: int = 0
    imported_assets_discovered: int = 0
    source_imports_present: int = 0
    source_imports_missing: int = 0
    imported_runtime_paths_derived: int = 0
    staged_loose_import_files_present: int = 0
    staged_loose_import_files_missing: int = 0


@dataclass
class RuntimeAssetReport:
    stats: RuntimeAssetStats = field(default_factory=RuntimeAssetStats)
    assets: list[RouteAsset] = field(default_factory=list)
    imported_assets: list[ImportedAsset] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    staging_requested: bool = False
    include_imports: bool = False

    def ok(self) -> bool:
        return not self.errors


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError("manifest root must be a JSON object")
    return data


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.relative_to(repo_root).as_posix()
    except ValueError:
        return str(path)


def is_within_path(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def repo_relative_posix_path(path: Path, repo_root: Path) -> PurePosixPath:
    return PurePosixPath(*path.relative_to(repo_root).parts)


def tag_name(tag: str) -> str:
    if "}" in tag:
        return tag.rsplit("}", 1)[1]
    return tag


def is_windows_absolute(value: str) -> bool:
    return PureWindowsPath(value).is_absolute()


def is_repo_relative_path(value: str) -> bool:
    return not (
        "\\" in value
        or ":" in value
        or value.startswith("/")
        or PurePosixPath(value).is_absolute()
        or is_windows_absolute(value)
    )


def validate_manifest_document_path(value: Any, label: str, errors: list[str]) -> PurePosixPath | None:
    if not isinstance(value, str) or not value:
        errors.append(f"{label} document path must be a non-empty string")
        return None
    if not is_repo_relative_path(value):
        errors.append(f"{label} document path must be repo-relative and use '/' separators: {value}")
        return None

    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        errors.append(f"{label} document path must not contain empty, '.', or '..' segments: {value}")
        return None

    document_path = PurePosixPath(value)
    if document_path.suffix.lower() != ".rml":
        errors.append(f"{label} document path must point to an .rml file: {value}")
        return None

    try:
        document_path.relative_to(SOURCE_ROOT)
    except ValueError:
        errors.append(f"{label} document path must be under {SOURCE_ROOT.as_posix()}: {value}")
        return None

    return document_path


def derive_runtime_path(document: str | PurePosixPath) -> PurePosixPath:
    document_path = PurePosixPath(document)
    return RUNTIME_ROOT / document_path.relative_to(SOURCE_ROOT)


def local_relative_import_href_path(href: str) -> PurePosixPath | None:
    stripped = href.strip()
    if (
        not stripped
        or stripped.startswith("#")
        or stripped.startswith("{{")
        or "{{" in stripped
        or "}}" in stripped
    ):
        return None

    parsed = urlsplit(stripped)
    if parsed.scheme or parsed.netloc:
        return None

    path = unquote(parsed.path).strip()
    if not path or path.startswith("#") or path.startswith("{{") or "\\" in path:
        return None

    href_path = PurePosixPath(path)
    if href_path.is_absolute() or href_path.suffix.lower() not in IMPORT_SUFFIXES:
        return None
    return href_path


def path_from_posix(base: Path, posix_path: PurePosixPath) -> Path:
    return base.joinpath(*posix_path.parts)


def route_label(route: dict[str, Any], index: int) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return f"route {route_id!r}"
    return f"route at index {index}"


def route_required_now(route: dict[str, Any], label: str, errors: list[str]) -> bool:
    required_now = route.get("required_now", True)
    if not isinstance(required_now, bool):
        errors.append(f"{label} field 'required_now' must be a boolean when present")
        return True
    return required_now


def validate_base_game(value: str) -> str:
    if (
        not value
        or "\\" in value
        or "/" in value
        or ":" in value
        or value in (".", "..")
    ):
        raise ValueError(f"base game must be a single directory name: {value!r}")
    return value


def resolve_install_dir(install_dir: Path, repo_root: Path) -> Path:
    if install_dir.is_absolute():
        return install_dir.resolve()
    return (repo_root / install_dir).resolve()


def import_missing_message(
    label: str,
    href: str,
    importer_path: Path,
    repo_root: Path,
) -> str:
    return (
        f"{label} missing required local href import {href} "
        f"referenced by {display_path(importer_path, repo_root)}"
    )


def import_staged_missing_message(
    label: str,
    staged_path: Path,
    repo_root: Path,
) -> str:
    return f"{label} missing required staged loose import {display_path(staged_path, repo_root)}"


def record_import_asset(
    report: RuntimeAssetReport,
    import_index: dict[Path, ImportedAsset],
    repo_root: Path,
    staged_root: Path | None,
    *,
    route_id: str,
    label: str,
    required_now: bool,
    href: str,
    importer_path: Path,
    source_path: Path,
) -> bool:
    stats = report.stats
    resolved_source_path = source_path.resolve(strict=False)

    existing = import_index.get(resolved_source_path)
    if existing is not None:
        if required_now and not existing.required_now:
            existing.required_now = True
            if not resolved_source_path.is_file():
                report.errors.append(import_missing_message(label, href, importer_path, repo_root))
            if staged_root is not None:
                staged_path = path_from_posix(staged_root, existing.runtime_path)
                if not staged_path.is_file():
                    report.errors.append(import_staged_missing_message(label, staged_path, repo_root))
        return resolved_source_path.is_file()

    source_posix_path = repo_relative_posix_path(resolved_source_path, repo_root)
    runtime_path = derive_runtime_path(source_posix_path)
    asset = ImportedAsset(
        route_id=route_id,
        href=href,
        importer_path=importer_path,
        source_path=resolved_source_path,
        runtime_path=runtime_path,
        required_now=required_now,
    )
    import_index[resolved_source_path] = asset
    report.imported_assets.append(asset)

    stats.imported_assets_discovered += 1
    stats.imported_runtime_paths_derived += 1
    stats.runtime_paths_derived += 1

    if resolved_source_path.is_file():
        stats.source_imports_present += 1
    else:
        stats.source_imports_missing += 1
        if required_now:
            report.errors.append(import_missing_message(label, href, importer_path, repo_root))

    if staged_root is not None:
        staged_path = path_from_posix(staged_root, runtime_path)
        if staged_path.is_file():
            stats.staged_loose_files_present += 1
            stats.staged_loose_import_files_present += 1
        else:
            stats.staged_loose_files_missing += 1
            stats.staged_loose_import_files_missing += 1
            if required_now:
                report.errors.append(import_staged_missing_message(label, staged_path, repo_root))

    return resolved_source_path.is_file()


def collect_import_assets_from_rml(
    document_path: Path,
    repo_root: Path,
    source_root_path: Path,
    report: RuntimeAssetReport,
    staged_root: Path | None,
    import_index: dict[Path, ImportedAsset],
    parsed_rml: dict[Path, bool],
    *,
    route_id: str,
    label: str,
    required_now: bool,
) -> None:
    resolved_document_path = document_path.resolve(strict=False)
    parsed_required = parsed_rml.get(resolved_document_path)
    if parsed_required is True or (parsed_required is False and not required_now):
        return
    parsed_rml[resolved_document_path] = required_now or bool(parsed_required)

    try:
        root = ElementTree.parse(resolved_document_path).getroot()
    except ElementTree.ParseError as exc:
        report.errors.append(
            f"{label} has malformed RML {display_path(resolved_document_path, repo_root)}: {exc}"
        )
        return
    except OSError as exc:
        report.errors.append(
            f"{label} cannot read RML {display_path(resolved_document_path, repo_root)}: {exc}"
        )
        return

    for element in root.iter():
        if tag_name(element.tag) != "link":
            continue

        href = element.attrib.get("href", "")
        href_path = local_relative_import_href_path(href)
        if href_path is None:
            continue

        import_path = resolved_document_path.parent.joinpath(*href_path.parts).resolve(strict=False)
        if not is_within_path(import_path, source_root_path):
            report.errors.append(
                f"{label} local href import escapes {SOURCE_ROOT.as_posix()} in "
                f"{display_path(resolved_document_path, repo_root)}: {href}"
            )
            continue

        import_present = record_import_asset(
            report,
            import_index,
            repo_root,
            staged_root,
            route_id=route_id,
            label=label,
            required_now=required_now,
            href=href,
            importer_path=resolved_document_path,
            source_path=import_path,
        )
        if href_path.suffix.lower() == ".rml" and import_present:
            collect_import_assets_from_rml(
                import_path,
                repo_root,
                source_root_path,
                report,
                staged_root,
                import_index,
                parsed_rml,
                route_id=route_id,
                label=label,
                required_now=required_now,
            )


def validate_runtime_assets(
    data: dict[str, Any],
    repo_root: Path,
    *,
    install_dir: Path | None = None,
    base_game: str = "basew",
    include_imports: bool = False,
) -> RuntimeAssetReport:
    report = RuntimeAssetReport(
        staging_requested=install_dir is not None,
        include_imports=include_imports,
    )
    repo_root = repo_root.resolve()
    source_root_path = path_from_posix(repo_root, SOURCE_ROOT).resolve(strict=False)

    schema = data.get("schema")
    if schema != EXPECTED_SCHEMA:
        report.errors.append(f"unexpected schema {schema!r}; expected {EXPECTED_SCHEMA!r}")

    routes = data.get("routes")
    if not isinstance(routes, list):
        report.errors.append("manifest field 'routes' must be a list")
        return report

    staged_root: Path | None = None
    if install_dir is not None:
        staged_root = resolve_install_dir(install_dir, repo_root) / validate_base_game(base_game)

    import_index: dict[Path, ImportedAsset] = {}
    parsed_rml: dict[Path, bool] = {}

    for index, route in enumerate(routes):
        report.stats.routes_checked += 1
        if not isinstance(route, dict):
            report.errors.append(f"route at index {index} must be an object")
            continue

        label = route_label(route, index)
        route_id_value = route.get("id")
        if not isinstance(route_id_value, str) or not route_id_value:
            report.errors.append(f"{label} field 'id' must be a non-empty string")
            route_id = f"<index {index}>"
        else:
            route_id = route_id_value

        required_now = route_required_now(route, label, report.errors)
        document_path = validate_manifest_document_path(route.get("document"), label, report.errors)
        if document_path is None:
            continue

        runtime_path = derive_runtime_path(document_path)
        report.stats.runtime_paths_derived += 1
        source_path = path_from_posix(repo_root, document_path)
        asset = RouteAsset(
            route_id=route_id,
            document=document_path.as_posix(),
            source_path=source_path,
            runtime_path=runtime_path,
            required_now=required_now,
        )
        report.assets.append(asset)

        if source_path.is_file():
            report.stats.source_documents_present += 1
        else:
            report.stats.source_documents_missing += 1
            if required_now:
                report.errors.append(
                    f"{label} missing required source document "
                    f"{display_path(source_path, repo_root)}"
                )

        if staged_root is not None:
            staged_path = path_from_posix(staged_root, runtime_path)
            if staged_path.is_file():
                report.stats.staged_loose_files_present += 1
            else:
                report.stats.staged_loose_files_missing += 1
                if required_now:
                    report.errors.append(
                        f"{label} missing required staged loose file "
                        f"{display_path(staged_path, repo_root)}"
                    )

        if include_imports and source_path.is_file():
            collect_import_assets_from_rml(
                source_path,
                repo_root,
                source_root_path,
                report,
                staged_root,
                import_index,
                parsed_rml,
                route_id=route_id,
                label=label,
                required_now=required_now,
            )

    return report


def print_report(report: RuntimeAssetReport) -> None:
    stats = report.stats
    print("RmlUi runtime assets:")
    print(f"  Routes checked: {stats.routes_checked}")
    print(
        "  Source documents: "
        f"present={stats.source_documents_present}, missing={stats.source_documents_missing}"
    )
    if report.include_imports:
        print(
            "  Imported assets: "
            f"discovered={stats.imported_assets_discovered}, "
            f"present={stats.source_imports_present}, missing={stats.source_imports_missing}"
        )
        print(
            "  Runtime paths derived: "
            f"{stats.runtime_paths_derived} "
            f"(route documents={stats.runtime_paths_derived - stats.imported_runtime_paths_derived}, "
            f"imported assets={stats.imported_runtime_paths_derived})"
        )
    else:
        print(f"  Runtime paths derived: {stats.runtime_paths_derived}")
    if report.staging_requested:
        print(
            "  Staged loose files: "
            f"present={stats.staged_loose_files_present}, "
            f"missing={stats.staged_loose_files_missing}"
        )
        if report.include_imports:
            print(
                "  Staged loose imported assets: "
                f"present={stats.staged_loose_import_files_present}, "
                f"missing={stats.staged_loose_import_files_missing}"
            )

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
    else:
        print("\nResult: RmlUi runtime asset path check passed.")


def json_report_payload(report: RuntimeAssetReport) -> dict[str, Any]:
    stats = report.stats
    route_runtime_paths = stats.runtime_paths_derived - stats.imported_runtime_paths_derived
    payload: dict[str, Any] = {
        "ok": report.ok(),
        "routes_checked": stats.routes_checked,
        "source_documents": {
            "present": stats.source_documents_present,
            "missing": stats.source_documents_missing,
        },
        "imported_assets": {
            "discovered": stats.imported_assets_discovered,
            "present": stats.source_imports_present,
            "missing": stats.source_imports_missing,
        },
        "runtime_paths": {
            "total": stats.runtime_paths_derived,
            "route_documents": route_runtime_paths,
            "imported_assets": stats.imported_runtime_paths_derived,
        },
        "staging_requested": report.staging_requested,
        "staged_loose_files": {
            "present": stats.staged_loose_files_present,
            "missing": stats.staged_loose_files_missing,
        },
        "errors": report.errors,
    }
    if report.include_imports and report.staging_requested:
        payload["staged_loose_imported_assets"] = {
            "present": stats.staged_loose_import_files_present,
            "missing": stats.staged_loose_import_files_missing,
        }
    return payload


def print_json_report(report: RuntimeAssetReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def staged_loose_asset_fields(
    runtime_path: PurePosixPath,
    staged_root: Path | None,
    repo_root: Path,
) -> dict[str, Any]:
    if staged_root is None:
        return {}

    staged_path = path_from_posix(staged_root, runtime_path)
    return {
        "staged_loose_path": display_path(staged_path, repo_root),
        "staged_loose_present": staged_path.is_file(),
    }


def route_asset_manifest_entry(
    asset: RouteAsset,
    repo_root: Path,
    staged_root: Path | None,
) -> dict[str, Any]:
    entry: dict[str, Any] = {
        "document": asset.document,
        "required_now": asset.required_now,
        "route_id": asset.route_id,
        "runtime_path": asset.runtime_path.as_posix(),
        "source_path": display_path(asset.source_path.resolve(strict=False), repo_root),
        "source_present": asset.source_path.is_file(),
    }
    entry.update(staged_loose_asset_fields(asset.runtime_path, staged_root, repo_root))
    return entry


def imported_asset_manifest_entry(
    asset: ImportedAsset,
    repo_root: Path,
    staged_root: Path | None,
) -> dict[str, Any]:
    entry: dict[str, Any] = {
        "href": asset.href,
        "importer_path": display_path(asset.importer_path.resolve(strict=False), repo_root),
        "required_now": asset.required_now,
        "route_id": asset.route_id,
        "runtime_path": asset.runtime_path.as_posix(),
        "source_path": display_path(asset.source_path.resolve(strict=False), repo_root),
        "source_present": asset.source_path.is_file(),
    }
    entry.update(staged_loose_asset_fields(asset.runtime_path, staged_root, repo_root))
    return entry


def asset_manifest_payload(
    report: RuntimeAssetReport,
    repo_root: Path,
    *,
    install_dir: Path | None = None,
    base_game: str = "basew",
) -> dict[str, Any]:
    repo_root = repo_root.resolve()
    staged_root: Path | None = None
    install_dir_value: str | None = None
    base_game_value: str | None = None

    if install_dir is not None:
        resolved_install_dir = resolve_install_dir(install_dir, repo_root)
        base_game_value = validate_base_game(base_game)
        staged_root = resolved_install_dir / base_game_value
        install_dir_value = display_path(resolved_install_dir, repo_root)

    route_documents = [
        route_asset_manifest_entry(asset, repo_root, staged_root)
        for asset in sorted(
            report.assets,
            key=lambda asset: (
                asset.runtime_path.as_posix(),
                asset.route_id,
                asset.document,
            ),
        )
    ]
    imported_assets = [
        imported_asset_manifest_entry(asset, repo_root, staged_root)
        for asset in sorted(
            report.imported_assets,
            key=lambda asset: (
                asset.runtime_path.as_posix(),
                display_path(asset.source_path.resolve(strict=False), repo_root),
                asset.route_id,
                asset.href,
            ),
        )
    ]

    return {
        "schema": ASSET_MANIFEST_SCHEMA,
        "ok": report.ok(),
        "include_imports": report.include_imports,
        "staging_requested": report.staging_requested,
        "install_dir": install_dir_value,
        "base_game": base_game_value,
        "summary": json_report_payload(report),
        "route_documents": route_documents,
        "imported_assets": imported_assets,
        "errors": report.errors,
    }


def resolve_asset_manifest_output_path(path: Path, repo_root: Path) -> Path:
    output_path = path if path.is_absolute() else repo_root / path
    output_path = output_path.resolve()
    if output_path.exists() and output_path.is_dir():
        raise ValueError(f"asset manifest output path must be a file: {output_path}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    return output_path


def write_asset_manifest(
    report: RuntimeAssetReport,
    output_path: Path,
    repo_root: Path,
    *,
    install_dir: Path | None = None,
    base_game: str = "basew",
) -> None:
    output_path = resolve_asset_manifest_output_path(output_path, repo_root.resolve())
    payload = asset_manifest_payload(
        report,
        repo_root,
        install_dir=install_dir,
        base_game=base_game,
    )
    output_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).with_name("rmlui_manifest.json"),
        help="Path to the RmlUi smoke manifest JSON.",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve manifest source document paths.",
    )
    parser.add_argument(
        "--install-dir",
        type=Path,
        default=None,
        help="Optional repo-relative or absolute install staging directory to check.",
    )
    parser.add_argument(
        "--base-game",
        default="basew",
        help="Base game directory inside --install-dir when staged loose files are checked.",
    )
    parser.add_argument(
        "--include-imports",
        action="store_true",
        help="Include local .rml/.rcss href imports in source/runtime/staged validation.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format. Defaults to the existing text report.",
    )
    parser.add_argument(
        "--write-manifest",
        type=Path,
        default=None,
        help=(
            "Optional output path for a detailed runtime/staging asset manifest JSON. "
            "Repo-relative paths are resolved from --repo-root."
        ),
    )
    args = parser.parse_args(argv)

    try:
        repo_root = args.repo_root.resolve()
        data = load_manifest(args.manifest.resolve())
        report = validate_runtime_assets(
            data,
            repo_root,
            install_dir=args.install_dir,
            base_game=args.base_game,
            include_imports=args.include_imports,
        )
        if args.write_manifest is not None:
            write_asset_manifest(
                report,
                args.write_manifest,
                repo_root,
                install_dir=args.install_dir,
                base_game=args.base_game,
            )
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        if args.format == "json":
            report = RuntimeAssetReport(
                staging_requested=args.install_dir is not None,
                include_imports=args.include_imports,
            )
            report.errors.append(f"Failed to validate runtime assets: {exc}")
            print_json_report(report)
            return 1
        print(f"Failed to validate runtime assets: {exc}", file=sys.stderr)
        return 1

    if args.format == "json":
        print_json_report(report)
    else:
        print_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
