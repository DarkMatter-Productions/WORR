#!/usr/bin/env python3
"""Inventory static RmlUi document body identities for central route documents."""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path, PureWindowsPath
from typing import Any
from xml.etree import ElementTree


EXPECTED_SCHEMA = "worr.rmlui.smoke_manifest.v1"
DEFAULT_MANIFEST_PATH = Path("tools/ui_smoke/rmlui_manifest.json")
DEFAULT_ROUTE_METADATA_ROOT = Path("assets/ui/rml")
RML_ASSET_ROOT = Path("assets/ui/rml")


@dataclass(frozen=True)
class RouteDocument:
    route_id: str
    path: Path
    manifest_document: str
    location: str


@dataclass(frozen=True)
class MetadataRouteIdentity:
    route_id: str
    document_id: str | None
    location: str


@dataclass(frozen=True)
class BodyIdentity:
    route_id: str
    document: Path
    body_id: str | None
    body_route_id: str | None
    metadata_document_id: str | None


@dataclass(frozen=True)
class DocumentIdMismatch:
    route_id: str
    document: Path
    body_id: str | None
    metadata_document_id: str | None
    metadata_location: str
    reason: str


@dataclass(frozen=True)
class RouteIdMismatch:
    route_id: str
    document: Path
    body_route_id: str


@dataclass(frozen=True)
class DuplicateBodyId:
    body_id: str
    routes: tuple[str, ...]
    documents: tuple[Path, ...]


@dataclass(frozen=True)
class DocumentProblem:
    route_id: str
    document: Path
    reason: str


@dataclass
class DocumentIdInventoryStats:
    route_count: int = 0
    route_metadata_file_count: int = 0
    route_metadata_count: int = 0
    documents_checked: int = 0
    documents_missing: int = 0


@dataclass
class DocumentIdInventoryReport:
    repo_root: Path
    documents: list[RouteDocument] = field(default_factory=list)
    body_identities: list[BodyIdentity] = field(default_factory=list)
    metadata_by_route: dict[str, MetadataRouteIdentity] = field(default_factory=dict)
    missing_body_ids: list[DocumentProblem] = field(default_factory=list)
    missing_body_elements: list[DocumentProblem] = field(default_factory=list)
    malformed_documents: list[DocumentProblem] = field(default_factory=list)
    metadata_mismatches: list[DocumentIdMismatch] = field(default_factory=list)
    route_id_mismatches: list[RouteIdMismatch] = field(default_factory=list)
    duplicate_body_ids: list[DuplicateBodyId] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    stats: DocumentIdInventoryStats = field(default_factory=DocumentIdInventoryStats)

    def ok(self) -> bool:
        return not self.errors

    @property
    def body_ids(self) -> list[BodyIdentity]:
        return [identity for identity in self.body_identities if identity.body_id]

    @property
    def unique_body_ids(self) -> list[str]:
        return sorted({identity.body_id for identity in self.body_ids if identity.body_id})

    @property
    def metadata_document_ids(self) -> list[BodyIdentity]:
        return [
            identity
            for identity in self.body_identities
            if identity.metadata_document_id is not None
        ]

    @property
    def matched_document_ids(self) -> list[BodyIdentity]:
        return [
            identity
            for identity in self.body_identities
            if identity.body_id is not None
            and identity.metadata_document_id is not None
            and identity.body_id == identity.metadata_document_id
        ]


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_input_path(repo_root: Path, path: Path) -> Path:
    if path.is_absolute():
        return path.resolve()
    return (repo_root / path).resolve()


def read_json_object(path: Path, label: str) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{label} root must be a JSON object")
    return data


def display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)


def is_within_repo(path: Path, repo_root: Path) -> bool:
    try:
        path.resolve().relative_to(repo_root.resolve())
    except ValueError:
        return False
    return True


def tag_name(tag: str) -> str:
    if "}" in tag:
        return tag.rsplit("}", 1)[1]
    return tag


def route_label(route: dict[str, Any], index: int, prefix: str) -> str:
    route_id = route.get("id")
    if isinstance(route_id, str) and route_id:
        return f"{prefix} route {route_id!r}"
    return f"{prefix} route at index {index}"


def route_list(data: dict[str, Any], label: str, errors: list[str]) -> list[Any]:
    routes = data.get("routes")
    if not isinstance(routes, list):
        errors.append(f"{label} field 'routes' must be a list")
        return []
    return routes


def normalize_metadata_document_path(value: Any, label: str, errors: list[str]) -> str | None:
    if not isinstance(value, str) or not value:
        errors.append(f"{label} field 'document' must be a non-empty string")
        return None
    if "\\" in value:
        errors.append(f"{label} document path must use '/' separators: {value}")
        return None
    if ":" in value or PureWindowsPath(value).is_absolute() or value.startswith("/"):
        errors.append(f"{label} document path must be repo-relative: {value}")
        return None

    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        errors.append(f"{label} document path must not contain empty, '.', or '..' segments: {value}")
        return None

    document_path = Path(*parts)
    if not document_path.as_posix().startswith(RML_ASSET_ROOT.as_posix() + "/"):
        document_path = RML_ASSET_ROOT / document_path
    return document_path.as_posix()


def collect_manifest_documents(data: dict[str, Any], repo_root: Path) -> DocumentIdInventoryReport:
    report = DocumentIdInventoryReport(repo_root=repo_root)
    schema = data.get("schema")
    if schema != EXPECTED_SCHEMA:
        report.errors.append(f"unexpected schema {schema!r}; expected {EXPECTED_SCHEMA!r}")

    routes = route_list(data, "manifest", report.errors)
    report.stats.route_count = len(routes)
    seen_route_ids: set[str] = set()
    duplicate_route_ids: set[str] = set()

    for index, route in enumerate(routes):
        if not isinstance(route, dict):
            report.errors.append(f"manifest route at index {index} must be an object")
            continue

        label = route_label(route, index, "manifest")
        route_id = route.get("id")
        document = route.get("document")
        if not isinstance(route_id, str) or not route_id:
            report.errors.append(f"{label} field 'id' must be a non-empty string")
            continue
        if route_id in seen_route_ids:
            duplicate_route_ids.add(route_id)
            continue
        seen_route_ids.add(route_id)

        if not isinstance(document, str) or not document:
            report.errors.append(f"{label} field 'document' must be a non-empty string")
            continue

        document_path = Path(document)
        if document_path.is_absolute():
            report.errors.append(f"{label} document path must be repo-relative: {document}")
            continue

        resolved_document = (repo_root / document_path).resolve(strict=False)
        if not is_within_repo(resolved_document, repo_root):
            report.errors.append(f"{label} document path escapes repo root: {document}")
            continue
        if resolved_document.suffix.lower() != ".rml":
            report.errors.append(f"{label} document path must point to an .rml file: {document}")
            continue

        if resolved_document.is_file():
            report.documents.append(
                RouteDocument(
                    route_id=route_id,
                    path=resolved_document,
                    manifest_document=document,
                    location=label,
                )
            )
        else:
            report.stats.documents_missing += 1
            report.errors.append(f"{label} missing route document {document}")

    for route_id in sorted(duplicate_route_ids):
        report.errors.append(f"manifest route id {route_id!r} is duplicated")

    return report


def discover_route_metadata_paths(repo_root: Path, metadata_root: Path) -> list[Path]:
    resolved_root = resolve_input_path(repo_root, metadata_root)
    if not resolved_root.is_dir():
        return []
    return sorted(path.resolve() for path in resolved_root.glob("*/routes.json") if path.is_file())


def index_route_metadata(
    route_metadata_sets: list[tuple[Path, dict[str, Any]]],
    repo_root: Path,
    report: DocumentIdInventoryReport,
) -> None:
    report.stats.route_metadata_file_count = len(route_metadata_sets)
    duplicate_route_ids: set[str] = set()

    for metadata_path, data in route_metadata_sets:
        metadata_label = display_path(metadata_path, repo_root)
        for index, route in enumerate(route_list(data, metadata_label, report.errors)):
            if not isinstance(route, dict):
                report.errors.append(f"{metadata_label} route at index {index} must be an object")
                continue

            label = route_label(route, index, metadata_label)
            route_id = route.get("id")
            if not isinstance(route_id, str) or not route_id:
                report.errors.append(f"{label} field 'id' must be a non-empty string")
                continue

            report.stats.route_metadata_count += 1
            if route_id in report.metadata_by_route:
                duplicate_route_ids.add(route_id)
                continue

            normalize_metadata_document_path(route.get("document"), label, report.errors)
            document_id = route.get("document_id")
            if document_id is not None and not isinstance(document_id, str):
                report.errors.append(f"{label} field 'document_id' must be a string when present")
                document_id = None

            report.metadata_by_route[route_id] = MetadataRouteIdentity(
                route_id=route_id,
                document_id=document_id,
                location=label,
            )

    for route_id in sorted(duplicate_route_ids):
        report.errors.append(f"route metadata id {route_id!r} is duplicated")


def document_label(document: RouteDocument, repo_root: Path) -> str:
    return f"route {document.route_id!r} {display_path(document.path, repo_root)}"


def problem_payload(problem: DocumentProblem, repo_root: Path) -> dict[str, str]:
    return {
        "route": problem.route_id,
        "document": display_path(problem.document, repo_root),
        "reason": problem.reason,
    }


def find_body_elements(root: ElementTree.Element) -> list[ElementTree.Element]:
    return [element for element in root.iter() if tag_name(element.tag) == "body"]


def record_missing_body(
    report: DocumentIdInventoryReport,
    document: RouteDocument,
    reason: str,
) -> None:
    problem = DocumentProblem(route_id=document.route_id, document=document.path, reason=reason)
    report.missing_body_elements.append(problem)
    report.errors.append(f"{document_label(document, report.repo_root)} {reason}")


def record_missing_body_id(
    report: DocumentIdInventoryReport,
    document: RouteDocument,
) -> None:
    problem = DocumentProblem(
        route_id=document.route_id,
        document=document.path,
        reason="<body> id attribute is missing or empty",
    )
    report.missing_body_ids.append(problem)
    report.errors.append(
        f"{document_label(document, report.repo_root)} <body> id attribute is missing or empty"
    )


def record_route_id_mismatch(
    report: DocumentIdInventoryReport,
    document: RouteDocument,
    body_route_id: str,
) -> None:
    mismatch = RouteIdMismatch(
        route_id=document.route_id,
        document=document.path,
        body_route_id=body_route_id,
    )
    report.route_id_mismatches.append(mismatch)
    report.errors.append(
        f"{document_label(document, report.repo_root)} body data-route-id "
        f"{body_route_id!r} does not match manifest route id {document.route_id!r}"
    )


def record_metadata_mismatch(
    report: DocumentIdInventoryReport,
    document: RouteDocument,
    body_id: str | None,
    metadata: MetadataRouteIdentity,
    reason: str,
) -> None:
    mismatch = DocumentIdMismatch(
        route_id=document.route_id,
        document=document.path,
        body_id=body_id,
        metadata_document_id=metadata.document_id,
        metadata_location=metadata.location,
        reason=reason,
    )
    report.metadata_mismatches.append(mismatch)
    report.errors.append(
        f"{document_label(document, report.repo_root)} metadata document_id "
        f"{metadata.document_id!r} does not match body id {body_id!r} "
        f"({metadata.location}): {reason}"
    )


def scan_document(document: RouteDocument, report: DocumentIdInventoryReport) -> None:
    try:
        root = ElementTree.parse(document.path).getroot()
    except ElementTree.ParseError as exc:
        problem = DocumentProblem(
            route_id=document.route_id,
            document=document.path,
            reason=f"malformed RML: {exc}",
        )
        report.malformed_documents.append(problem)
        report.errors.append(f"{document_label(document, report.repo_root)} is malformed RML: {exc}")
        return
    except OSError as exc:
        problem = DocumentProblem(
            route_id=document.route_id,
            document=document.path,
            reason=f"cannot be read: {exc}",
        )
        report.malformed_documents.append(problem)
        report.errors.append(f"{document_label(document, report.repo_root)} cannot be read: {exc}")
        return

    report.stats.documents_checked += 1
    body_elements = find_body_elements(root)
    if not body_elements:
        record_missing_body(report, document, "missing <body> element")
        return
    if len(body_elements) > 1:
        report.errors.append(
            f"{document_label(document, report.repo_root)} contains "
            f"{len(body_elements)} <body> elements; expected exactly one"
        )

    body = body_elements[0]
    body_id = body.attrib.get("id")
    body_id = body_id.strip() if isinstance(body_id, str) else None
    if not body_id:
        body_id = None
        record_missing_body_id(report, document)

    body_route_id = body.attrib.get("data-route-id")
    body_route_id = body_route_id.strip() if isinstance(body_route_id, str) else None
    if body_route_id and body_route_id != document.route_id:
        record_route_id_mismatch(report, document, body_route_id)

    metadata = report.metadata_by_route.get(document.route_id)
    metadata_document_id = metadata.document_id if metadata is not None else None
    if metadata is not None:
        if not metadata_document_id:
            record_metadata_mismatch(
                report,
                document,
                body_id,
                metadata,
                "feature metadata has no document_id",
            )
        elif body_id is not None and metadata_document_id != body_id:
            record_metadata_mismatch(
                report,
                document,
                body_id,
                metadata,
                "feature metadata document_id differs from the document body id",
            )

    report.body_identities.append(
        BodyIdentity(
            route_id=document.route_id,
            document=document.path,
            body_id=body_id,
            body_route_id=body_route_id,
            metadata_document_id=metadata_document_id,
        )
    )


def record_duplicate_body_ids(report: DocumentIdInventoryReport) -> None:
    identities_by_body_id: dict[str, list[BodyIdentity]] = defaultdict(list)
    for identity in report.body_ids:
        if identity.body_id:
            identities_by_body_id[identity.body_id].append(identity)

    for body_id, identities in sorted(identities_by_body_id.items()):
        if len(identities) <= 1:
            continue
        duplicate = DuplicateBodyId(
            body_id=body_id,
            routes=tuple(identity.route_id for identity in identities),
            documents=tuple(identity.document for identity in identities),
        )
        report.duplicate_body_ids.append(duplicate)
        routes = ", ".join(repr(route_id) for route_id in duplicate.routes)
        report.errors.append(f"body id {body_id!r} is duplicated across routes: {routes}")


def build_document_id_inventory(
    manifest_data: dict[str, Any],
    route_metadata_sets: list[tuple[Path, dict[str, Any]]],
    repo_root: Path,
) -> DocumentIdInventoryReport:
    report = collect_manifest_documents(manifest_data, repo_root.resolve())
    index_route_metadata(route_metadata_sets, repo_root.resolve(), report)
    for document in report.documents:
        scan_document(document, report)
    record_duplicate_body_ids(report)
    return report


def body_identity_payload(identity: BodyIdentity, repo_root: Path) -> dict[str, str | None]:
    return {
        "route": identity.route_id,
        "document": display_path(identity.document, repo_root),
        "body_id": identity.body_id,
        "body_route_id": identity.body_route_id,
        "metadata_document_id": identity.metadata_document_id,
    }


def duplicate_body_id_payload(duplicate: DuplicateBodyId, repo_root: Path) -> dict[str, Any]:
    return {
        "body_id": duplicate.body_id,
        "routes": list(duplicate.routes),
        "documents": [display_path(document, repo_root) for document in duplicate.documents],
    }


def metadata_mismatch_payload(
    mismatch: DocumentIdMismatch,
    repo_root: Path,
) -> dict[str, str | None]:
    return {
        "route": mismatch.route_id,
        "document": display_path(mismatch.document, repo_root),
        "body_id": mismatch.body_id,
        "metadata_document_id": mismatch.metadata_document_id,
        "metadata_location": mismatch.metadata_location,
        "reason": mismatch.reason,
    }


def route_id_mismatch_payload(
    mismatch: RouteIdMismatch,
    repo_root: Path,
) -> dict[str, str]:
    return {
        "route": mismatch.route_id,
        "document": display_path(mismatch.document, repo_root),
        "body_route_id": mismatch.body_route_id,
    }


def list_route_payload(identities: list[BodyIdentity]) -> dict[str, Any]:
    return {
        "count": len(identities),
        "routes": [identity.route_id for identity in identities],
    }


def json_report_payload(report: DocumentIdInventoryReport) -> dict[str, Any]:
    sorted_body_identities = sorted(report.body_ids, key=lambda identity: identity.route_id)
    sorted_metadata_identities = sorted(
        report.metadata_document_ids, key=lambda identity: identity.route_id
    )
    sorted_matched_identities = sorted(
        report.matched_document_ids, key=lambda identity: identity.route_id
    )

    return {
        "ok": report.ok(),
        "route_count": report.stats.route_count,
        "route_metadata_file_count": report.stats.route_metadata_file_count,
        "route_metadata_count": report.stats.route_metadata_count,
        "documents_checked": report.stats.documents_checked,
        "documents_missing": report.stats.documents_missing,
        "body_ids": {
            "count": len(sorted_body_identities),
            "documents": [
                body_identity_payload(identity, report.repo_root)
                for identity in sorted_body_identities
            ],
        },
        "unique_body_ids": {
            "count": len(report.unique_body_ids),
            "ids": report.unique_body_ids,
        },
        "metadata_document_ids": {
            "count": len(sorted_metadata_identities),
            "documents": [
                body_identity_payload(identity, report.repo_root)
                for identity in sorted_metadata_identities
            ],
        },
        "matched_document_ids": list_route_payload(sorted_matched_identities),
        "mismatched_document_ids": {
            "count": len(report.metadata_mismatches),
            "mismatches": [
                metadata_mismatch_payload(mismatch, report.repo_root)
                for mismatch in report.metadata_mismatches
            ],
        },
        "duplicate_body_ids": {
            "count": len(report.duplicate_body_ids),
            "ids": [duplicate.body_id for duplicate in report.duplicate_body_ids],
            "duplicates": [
                duplicate_body_id_payload(duplicate, report.repo_root)
                for duplicate in report.duplicate_body_ids
            ],
        },
        "missing_body_ids": {
            "count": len(report.missing_body_ids),
            "documents": [
                problem_payload(problem, report.repo_root) for problem in report.missing_body_ids
            ],
        },
        "route_id_mismatches": {
            "count": len(report.route_id_mismatches),
            "mismatches": [
                route_id_mismatch_payload(mismatch, report.repo_root)
                for mismatch in report.route_id_mismatches
            ],
        },
        "malformed_documents": {
            "count": len(report.malformed_documents),
            "documents": [
                problem_payload(problem, report.repo_root)
                for problem in report.malformed_documents
            ],
        },
        "errors": report.errors,
    }


def print_json_report(report: DocumentIdInventoryReport) -> None:
    print(json.dumps(json_report_payload(report), indent=2, sort_keys=True))


def compact_list(values: list[str], *, limit: int = 20) -> str:
    if not values:
        return "-"
    if len(values) <= limit:
        return ", ".join(values)
    return f"{', '.join(values[:limit])}, ... (+{len(values) - limit} more)"


def format_duplicate_ids(report: DocumentIdInventoryReport) -> str:
    return compact_list([duplicate.body_id for duplicate in report.duplicate_body_ids])


def print_text_report(report: DocumentIdInventoryReport) -> None:
    print("RmlUi document ID/body route identity inventory:")
    print(f"  Routes known: {report.stats.route_count}")
    print(f"  Route metadata files: {report.stats.route_metadata_file_count}")
    print(f"  Route metadata entries: {report.stats.route_metadata_count}")
    print(
        "  Documents checked: "
        f"present={report.stats.documents_checked}, missing={report.stats.documents_missing}"
    )
    print(f"  Body IDs: {len(report.body_ids)}")
    print(f"  Unique body IDs: {len(report.unique_body_ids)}")
    print(f"  Metadata document IDs: {len(report.metadata_document_ids)}")
    print(f"  Matched metadata/body document IDs: {len(report.matched_document_ids)}")
    print(f"  Mismatched metadata/body document IDs: {len(report.metadata_mismatches)}")
    print(f"  Missing body IDs: {len(report.missing_body_ids)}")
    print(f"  Route-id mismatches: {len(report.route_id_mismatches)}")
    print(f"  Duplicate body IDs: {len(report.duplicate_body_ids)}")
    print(f"  Malformed documents: {len(report.malformed_documents)}")
    print(f"  Duplicate body ID values: {format_duplicate_ids(report)}")

    if report.errors:
        print("\nErrors:")
        for error in report.errors:
            print(f"  - {error}")
        print("\nResult: RmlUi document ID/body route identity check failed.")
    else:
        print("\nResult: RmlUi document ID/body route identity check passed.")


def failure_report(repo_root: Path, message: str) -> DocumentIdInventoryReport:
    report = DocumentIdInventoryReport(repo_root=repo_root)
    report.errors.append(message)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST_PATH,
        help="Path to the central RmlUi smoke manifest JSON.",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root_from_script(),
        help="Repository root used to resolve default paths.",
    )
    parser.add_argument(
        "--route-metadata-root",
        type=Path,
        default=DEFAULT_ROUTE_METADATA_ROOT,
        help="Root containing feature route metadata files named */routes.json.",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="Output format. Defaults to the text report.",
    )
    args = parser.parse_args(argv)

    repo_root = args.repo_root.resolve()
    manifest_path = resolve_input_path(repo_root, args.manifest)
    route_metadata_paths = discover_route_metadata_paths(repo_root, args.route_metadata_root)

    try:
        manifest_data = read_json_object(manifest_path, "RmlUi smoke manifest")
        route_metadata_sets = [
            (path, read_json_object(path, f"{display_path(path, repo_root)} route metadata"))
            for path in route_metadata_paths
        ]
        report = build_document_id_inventory(manifest_data, route_metadata_sets, repo_root)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        report = failure_report(repo_root, f"Failed to validate RmlUi document IDs: {exc}")
        if args.format == "json":
            print_json_report(report)
        else:
            print(report.errors[0], file=sys.stderr)
        return 1

    if args.format == "json":
        print_json_report(report)
    else:
        print_text_report(report)
    return 0 if report.ok() else 1


if __name__ == "__main__":
    raise SystemExit(main())
