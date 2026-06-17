#!/usr/bin/env python3
"""Audit q2aas staged AAS files against a validation report."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path
from typing import Any

STAGE_AUDIT_SCHEMA = "worr-q2aas-stage-audit-v1"


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_path(root: Path, path: Path) -> Path:
    if path.is_absolute():
        return path
    return root / path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def is_under(path: Path, root: Path) -> bool:
    try:
        path_text = os.path.normcase(str(path.resolve()))
        root_text = os.path.normcase(str(root.resolve()))
        return os.path.commonpath([path_text, root_text]) == root_text
    except ValueError:
        return False


def load_report(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as stream:
            data = json.load(stream)
    except OSError as exc:
        raise SystemExit(f"Unable to read q2aas stage report {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise SystemExit(f"Invalid q2aas stage report JSON {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise SystemExit(f"Invalid q2aas stage report root: expected object in {path}")
    maps = data.get("maps")
    if not isinstance(maps, list):
        raise SystemExit(f"Invalid q2aas stage report: expected maps array in {path}")
    return data


def audit_map(map_entry: dict[str, Any], stage_dir: Path, require_staged: bool) -> dict[str, Any]:
    map_id = str(map_entry.get("id", "<unknown>"))
    issues: list[str] = []
    staged_output = map_entry.get("staged_output")
    if not isinstance(staged_output, dict):
        staged_output = {"enabled": False}

    enabled = bool(staged_output.get("enabled"))
    staged_status = staged_output.get("status")
    staged_aas_value = staged_output.get("aas")
    expected_hash = staged_output.get("aas_sha256") or map_entry.get("aas_sha256")

    audit: dict[str, Any] = {
        "id": map_id,
        "source_bsp": map_entry.get("path"),
        "expected_stage_dir": str(stage_dir),
        "expected_sha256": expected_hash,
        "staged_output_status": staged_status,
        "staged_aas": staged_aas_value,
        "size": None,
        "actual_sha256": None,
        "issues": issues,
    }

    if not enabled:
        if require_staged:
            issues.append("staged_output is not enabled")
        audit["status"] = "failed" if issues else "skipped"
        return audit

    if staged_status != "staged":
        issues.append(f"staged_output.status is {staged_status!r}, expected 'staged'")

    if not isinstance(staged_aas_value, str) or not staged_aas_value:
        issues.append("staged_output.aas is missing")
        audit["status"] = "failed"
        return audit

    staged_aas = Path(staged_aas_value)
    if not staged_aas.is_absolute():
        staged_aas = stage_dir / staged_aas
    staged_aas = staged_aas.resolve()
    audit["staged_aas"] = str(staged_aas)

    if not is_under(staged_aas, stage_dir):
        issues.append(f"staged AAS is outside expected stage directory: {staged_aas}")

    if staged_aas.suffix.lower() != ".aas":
        issues.append(f"staged file does not use .aas extension: {staged_aas.name}")

    if not staged_aas.is_file():
        issues.append(f"missing staged AAS file: {staged_aas}")
        audit["status"] = "failed"
        return audit

    size = staged_aas.stat().st_size
    actual_hash = sha256_file(staged_aas)
    audit["size"] = size
    audit["actual_sha256"] = actual_hash

    if size <= 0:
        issues.append(f"staged AAS file is empty: {staged_aas}")

    if not isinstance(expected_hash, str) or not expected_hash:
        issues.append("expected staged AAS hash is missing from report")
    elif actual_hash.lower() != expected_hash.lower():
        issues.append(
            f"staged AAS hash mismatch: expected {expected_hash.lower()}, got {actual_hash.lower()}"
        )

    scratch_hash = map_entry.get("aas_sha256")
    if isinstance(scratch_hash, str) and scratch_hash and actual_hash.lower() != scratch_hash.lower():
        issues.append(
            f"staged AAS hash does not match generated scratch AAS hash {scratch_hash.lower()}"
        )

    audit["status"] = "failed" if issues else "passed"
    return audit


def main() -> int:
    root = repo_root()
    parser = argparse.ArgumentParser(
        description="Audit q2aas staged AAS files against .tmp/q2aas/stage-report.json."
    )
    parser.add_argument(
        "--report-json",
        type=Path,
        default=root / ".tmp" / "q2aas" / "stage-report.json",
        help="q2aas stage report produced by q2aas-stage-aas.",
    )
    parser.add_argument(
        "--stage-dir",
        type=Path,
        default=root / ".install" / "basew" / "maps",
        help="Expected directory containing staged .aas files.",
    )
    parser.add_argument(
        "--audit-report-json",
        type=Path,
        default=None,
        help="Optional JSON audit report path.",
    )
    parser.add_argument(
        "--require-staged-output",
        action="store_true",
        help="Fail if a map entry does not include staged_output.enabled=true.",
    )
    args = parser.parse_args()

    report_path = resolve_path(root, args.report_json).resolve()
    stage_dir = resolve_path(root, args.stage_dir).resolve()
    audit_report_path = (
        resolve_path(root, args.audit_report_json).resolve() if args.audit_report_json else None
    )

    report = load_report(report_path)
    audits = [
        audit_map(entry, stage_dir, args.require_staged_output)
        for entry in report["maps"]
        if isinstance(entry, dict)
    ]
    failed = [entry for entry in audits if entry["status"] == "failed"]

    audit_report = {
        "schema": STAGE_AUDIT_SCHEMA,
        "source_report": str(report_path),
        "source_report_schema": report.get("schema"),
        "stage_dir": str(stage_dir),
        "map_count": len(audits),
        "failed_count": len(failed),
        "status": "failed" if failed else "passed",
        "maps": audits,
    }

    if audit_report_path:
        audit_report_path.parent.mkdir(parents=True, exist_ok=True)
        with audit_report_path.open("w", encoding="utf-8") as stream:
            json.dump(audit_report, stream, indent=2)
            stream.write("\n")
        print(f"[q2aas-stage-audit] report: {audit_report_path}")

    for entry in audits:
        if entry["status"] == "passed":
            print(
                f"[q2aas-stage-audit] {entry['id']}: {entry['staged_aas']} "
                f"({entry['size']} bytes, sha256 {entry['actual_sha256']})"
            )
        elif entry["status"] == "skipped":
            print(f"[q2aas-stage-audit] {entry['id']}: skipped")
        else:
            print(f"[q2aas-stage-audit] {entry['id']}: failed", file=sys.stderr)
            for issue in entry["issues"]:
                print(f"[q2aas-stage-audit]   {issue}", file=sys.stderr)

    if failed:
        return 1

    print(f"[q2aas-stage-audit] passed: {len(audits)} map(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
