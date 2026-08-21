#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import stat
import sys
import tempfile
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from tools.release.release_signing import (
    RELEASE_INDEX_MEDIA_TYPE,
    MAX_PUBLISHED_ASSETS,
    MAX_SIGNING_REQUEST_ASSETS,
    MAX_SIGNING_REQUEST_SIZE,
    SIGNING_REQUEST_SCHEMA_VERSION,
    SIGNING_REQUEST_STATUS,
    ReleaseSigningError,
    create_signature_envelope,
    load_private_signing_key,
    load_trust_policy,
    portable_relative_request_path,
    serialize_signature_envelope,
    verify_signature_envelope,
)
from tools.package_release import MAX_RELEASE_ASSET_BYTES, MAX_UPDATE_PACKAGE_BYTES
from tools.release.verify_artifacts import (
    ArtifactLayoutError,
    discover_bundle_metadata_files,
    require_bundle_asset,
)


SIGNING_KEY_LIST_SCHEMA_VERSION = 1
SIGNING_KEY_LIST_FIELDS = {"schema_version", "signing_key_paths"}
REVOCATION_LIST_SCHEMA_VERSION = 1
REVOCATION_LIST_FIELDS = {"schema_version", "channels"}
MAX_RELEASE_METADATA_BYTES = 32 * 1024 * 1024
MAX_REVOCATION_LIST_BYTES = 1024 * 1024
MAX_SIGNING_KEY_LIST_BYTES = 1024 * 1024
MAX_REVOKED_RELEASE_INDICES = 1024
MAX_RELEASE_VALIDITY_DAYS = 366


def _absolute_without_following_links(value: str) -> pathlib.Path:
    return pathlib.Path(os.path.abspath(value))


def _is_link_or_reparse(path_status: os.stat_result) -> bool:
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    return stat.S_ISLNK(path_status.st_mode) or bool(
        getattr(path_status, "st_file_attributes", 0) & reparse_flag
    )


def _require_regular_single_link_file(
    path: pathlib.Path,
    description: str,
    *,
    allow_missing: bool = False,
) -> None:
    try:
        path_status = path.lstat()
    except FileNotFoundError:
        if allow_missing:
            return
        raise ReleaseSigningError(f"{description} does not exist: {path}") from None
    except OSError as exc:
        raise ReleaseSigningError(f"cannot inspect {description} {path} ({exc})") from exc
    if _is_link_or_reparse(path_status):
        raise ReleaseSigningError(
            f"{description} must be a regular non-link file: {path}"
        )
    if not stat.S_ISREG(path_status.st_mode):
        raise ReleaseSigningError(f"{description} must be a regular file: {path}")
    if path_status.st_nlink != 1:
        raise ReleaseSigningError(f"{description} must not have hard links: {path}")


def _path_identity(path: pathlib.Path) -> tuple[str, str]:
    lexical = os.path.normcase(os.path.normpath(str(path))).casefold()
    try:
        canonical = os.path.normcase(
            os.path.normpath(str(path.resolve(strict=False)))
        ).casefold()
    except OSError as exc:
        raise ReleaseSigningError(f"cannot normalize release path {path} ({exc})") from exc
    return lexical, canonical


def _same_existing_file(first: pathlib.Path, second: pathlib.Path) -> bool:
    try:
        return os.path.samefile(first, second)
    except FileNotFoundError:
        return False
    except OSError as exc:
        raise ReleaseSigningError(
            f"cannot compare release paths {first} and {second} ({exc})"
        ) from exc


def validate_compose_path_contract(
    source_paths: list[tuple[pathlib.Path, str]],
    output_paths: list[tuple[pathlib.Path, str]],
) -> None:
    """Reject aliases before any composer output is created or replaced."""

    normalized_sources: list[tuple[pathlib.Path, str, tuple[str, str]]] = []
    for source_path, description in source_paths:
        _require_regular_single_link_file(source_path, description)
        normalized_sources.append(
            (source_path, description, _path_identity(source_path))
        )

    normalized_outputs: list[tuple[pathlib.Path, str, tuple[str, str]]] = []
    for output_path, description in output_paths:
        _require_regular_single_link_file(
            output_path,
            description,
            allow_missing=True,
        )
        identity = _path_identity(output_path)
        for existing_path, existing_description, existing_identity in normalized_outputs:
            if (
                identity[0] == existing_identity[0]
                or identity[1] == existing_identity[1]
                or _same_existing_file(output_path, existing_path)
            ):
                raise ReleaseSigningError(
                    f"{description} collides with {existing_description}: {output_path}"
                )
        normalized_outputs.append((output_path, description, identity))

    for output_path, output_description, output_identity in normalized_outputs:
        for source_path, source_description, source_identity in normalized_sources:
            if (
                output_identity[0] == source_identity[0]
                or output_identity[1] == source_identity[1]
                or _same_existing_file(output_path, source_path)
            ):
                raise ReleaseSigningError(
                    f"{output_description} collides with {source_description}: "
                    f"{output_path}"
                )


def _atomic_write_text(path: pathlib.Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.",
        dir=path.parent,
    )
    temporary_path = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            descriptor = -1
            handle.write(contents.encode("utf-8"))
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary_path, path)
    except BaseException:
        if descriptor >= 0:
            os.close(descriptor)
        temporary_path.unlink(missing_ok=True)
        raise


def _atomic_write_set(outputs: list[tuple[pathlib.Path, bytes]]) -> None:
    """Stage every output before replacing any, restoring old files on error."""

    staged: list[tuple[pathlib.Path, pathlib.Path]] = []
    backups: dict[pathlib.Path, pathlib.Path] = {}
    committed: list[pathlib.Path] = []
    originally_missing: set[pathlib.Path] = set()
    succeeded = False
    try:
        for path, payload in outputs:
            path.parent.mkdir(parents=True, exist_ok=True)
            descriptor, temporary_name = tempfile.mkstemp(
                prefix=f".{path.name}.", dir=path.parent
            )
            temporary_path = pathlib.Path(temporary_name)
            with os.fdopen(descriptor, "wb") as handle:
                handle.write(payload)
                handle.flush()
                os.fsync(handle.fileno())
            staged.append((path, temporary_path))

        for path, _ in staged:
            if not os.path.lexists(path):
                originally_missing.add(path)
                continue
            descriptor, backup_name = tempfile.mkstemp(
                prefix=f".{path.name}.backup.", dir=path.parent
            )
            os.close(descriptor)
            backup_path = pathlib.Path(backup_name)
            backup_path.unlink()
            os.replace(path, backup_path)
            backups[path] = backup_path

        for path, temporary_path in staged:
            os.replace(temporary_path, path)
            committed.append(path)
        succeeded = True
    except BaseException as failure:
        rollback_failures: list[str] = []
        for path, backup_path in list(backups.items()):
            try:
                os.replace(backup_path, path)
                del backups[path]
            except OSError as exc:
                rollback_failures.append(f"restore {path} from {backup_path}: {exc}")
        for path in originally_missing:
            if path not in committed:
                continue
            try:
                path.unlink(missing_ok=True)
            except OSError as exc:
                rollback_failures.append(f"remove newly-created {path}: {exc}")
        if rollback_failures:
            raise ReleaseSigningError(
                "release output publication failed and rollback was incomplete; "
                + "; ".join(rollback_failures)
            ) from failure
        raise
    finally:
        for _, temporary_path in staged:
            temporary_path.unlink(missing_ok=True)
        if succeeded:
            for backup_path in backups.values():
                backup_path.unlink(missing_ok=True)


def _reject_duplicate_json_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise ReleaseSigningError(f"duplicate JSON key {key!r}")
        value[key] = item
    return value


def _reject_json_constant(value: str) -> None:
    raise ReleaseSigningError(f"non-finite JSON number {value!r} is not allowed")


def _require_private_non_link_file(path: pathlib.Path, description: str) -> None:
    try:
        path_status = path.lstat()
    except OSError as exc:
        raise ReleaseSigningError(f"cannot inspect {description} {path} ({exc})") from exc
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    if (
        not stat.S_ISREG(path_status.st_mode)
        or stat.S_ISLNK(path_status.st_mode)
        or getattr(path_status, "st_file_attributes", 0) & reparse_flag
    ):
        raise ReleaseSigningError(f"{description} {path} must be a regular non-link file")
    if path_status.st_nlink != 1:
        raise ReleaseSigningError(f"{description} {path} must not have hard links")
    if os.name != "nt" and stat.S_IMODE(path_status.st_mode) & 0o077:
        raise ReleaseSigningError(
            f"{description} {path} must not be readable or writable by group/other"
        )


def load_signing_key_list(path: pathlib.Path) -> list[pathlib.Path]:
    path = _absolute_without_following_links(str(path))
    _require_private_non_link_file(path, "signing key path list")
    if path.stat().st_size <= 0 or path.stat().st_size > MAX_SIGNING_KEY_LIST_BYTES:
        raise ReleaseSigningError(
            f"signing key path list must be between 1 and "
            f"{MAX_SIGNING_KEY_LIST_BYTES} bytes"
        )
    try:
        with path.open("rb") as handle:
            raw_document = handle.read(MAX_SIGNING_KEY_LIST_BYTES + 1)
        if not raw_document or len(raw_document) > MAX_SIGNING_KEY_LIST_BYTES:
            raise ReleaseSigningError(
                "signing key path list changed outside its supported size"
            )
        document = json.loads(
            raw_document.decode("utf-8", errors="strict"),
            object_pairs_hook=_reject_duplicate_json_keys,
            parse_constant=_reject_json_constant,
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ReleaseSigningError) as exc:
        raise ReleaseSigningError(
            f"signing key path list {path} is not strict UTF-8 JSON ({exc})"
        ) from exc
    if not isinstance(document, dict):
        raise ReleaseSigningError("signing key path list root must be an object")
    if set(document) != SIGNING_KEY_LIST_FIELDS:
        missing = sorted(SIGNING_KEY_LIST_FIELDS - set(document))
        unexpected = sorted(set(document) - SIGNING_KEY_LIST_FIELDS)
        details = []
        if missing:
            details.append(f"missing {', '.join(missing)}")
        if unexpected:
            details.append(f"unexpected {', '.join(unexpected)}")
        raise ReleaseSigningError(
            f"signing key path list has invalid fields ({'; '.join(details)})"
        )
    if (
        type(document.get("schema_version")) is not int
        or document["schema_version"] != SIGNING_KEY_LIST_SCHEMA_VERSION
    ):
        raise ReleaseSigningError(
            "signing key path list has an unsupported schema_version"
        )
    values = document.get("signing_key_paths")
    if not isinstance(values, list) or not values:
        raise ReleaseSigningError(
            "signing key path list signing_key_paths must be a non-empty array"
        )

    list_parent = _absolute_without_following_links(str(path.parent))
    try:
        parent_status = list_parent.lstat()
    except OSError as exc:
        raise ReleaseSigningError(
            f"cannot inspect signing key path list directory {list_parent} ({exc})"
        ) from exc
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    if (
        not stat.S_ISDIR(parent_status.st_mode)
        or stat.S_ISLNK(parent_status.st_mode)
        or getattr(parent_status, "st_file_attributes", 0) & reparse_flag
    ):
        raise ReleaseSigningError(
            "signing key path list directory must be a non-link directory"
        )

    paths = []
    normalized_paths: set[str] = set()
    normalized_parent = os.path.normcase(str(list_parent))
    for index, value in enumerate(values):
        if (
            not isinstance(value, str)
            or not value
            or value.strip() != value
            or any(ord(character) < 0x20 or ord(character) == 0x7F for character in value)
        ):
            raise ReleaseSigningError(
                f"signing key path list entry {index} must be a non-empty single-line path"
            )
        candidate_value = pathlib.Path(value)
        if not candidate_value.is_absolute():
            raise ReleaseSigningError(
                f"signing key path list entry {index} must be an absolute path"
            )
        candidate = _absolute_without_following_links(value)
        if os.path.normcase(value) != os.path.normcase(str(candidate)):
            raise ReleaseSigningError(
                f"signing key path list entry {index} must be lexically normalized"
            )
        if os.path.normcase(str(candidate.parent)) != normalized_parent:
            raise ReleaseSigningError(
                f"signing key path list entry {index} must name a file beside the list"
            )
        normalized = os.path.normcase(str(candidate))
        if normalized in normalized_paths:
            raise ReleaseSigningError("signing key path list contains a duplicate path")
        normalized_paths.add(normalized)
        paths.append(candidate)
    return paths


def load_release_revocations(path: pathlib.Path, channel: str) -> list[str]:
    path = _absolute_without_following_links(str(path))
    _require_regular_single_link_file(path, "release revocation list")
    if path.stat().st_size > MAX_REVOCATION_LIST_BYTES:
        raise ReleaseSigningError("release revocation list exceeds 1 MiB")
    try:
        document = json.loads(
            path.read_text(encoding="utf-8", errors="strict"),
            object_pairs_hook=_reject_duplicate_json_keys,
            parse_constant=_reject_json_constant,
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ReleaseSigningError) as exc:
        raise ReleaseSigningError(
            f"release revocation list {path} is not strict UTF-8 JSON ({exc})"
        ) from exc
    if not isinstance(document, dict) or set(document) != REVOCATION_LIST_FIELDS:
        raise ReleaseSigningError("release revocation list has invalid fields")
    if (
        type(document.get("schema_version")) is not int
        or document["schema_version"] != REVOCATION_LIST_SCHEMA_VERSION
    ):
        raise ReleaseSigningError(
            "release revocation list has an unsupported schema_version"
        )
    channels = document.get("channels")
    if not isinstance(channels, dict) or channel not in channels:
        raise ReleaseSigningError(
            f"release revocation list has no {channel!r} channel"
        )
    values = channels[channel]
    if not isinstance(values, list) or len(values) > MAX_REVOKED_RELEASE_INDICES:
        raise ReleaseSigningError("release revocation list channel is invalid")
    if any(
        not isinstance(value, str)
        or len(value) != 64
        or value != value.lower()
        or any(character not in "0123456789abcdef" for character in value)
        for value in values
    ):
        raise ReleaseSigningError(
            "release revocation list contains a non-canonical SHA-256 digest"
        )
    if len(set(values)) != len(values):
        raise ReleaseSigningError("release revocation list contains duplicates")
    return sorted(values)


def collect_signing_key_paths(
    direct_values: list[str], list_values: list[str]
) -> list[pathlib.Path]:
    paths = [_absolute_without_following_links(value) for value in direct_values]
    for list_value in list_values:
        paths.extend(load_signing_key_list(pathlib.Path(list_value)))

    normalized_paths: set[str] = set()
    key_ids: set[str] = set()
    for path in paths:
        normalized = os.path.normcase(str(path))
        if normalized in normalized_paths:
            raise ReleaseSigningError(f"duplicate signing key path {path}")
        normalized_paths.add(normalized)
        signing_key = load_private_signing_key(path)
        if signing_key.key_id in key_ids:
            raise ReleaseSigningError(
                f"duplicate signing key identity {signing_key.key_id}"
            )
        key_ids.add(signing_key.key_id)
    return paths


def sha256_file(path: pathlib.Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def validate_release_artifact_size(
    path: pathlib.Path, artifact_kind: str, file_size: int
) -> None:
    if file_size > MAX_RELEASE_ASSET_BYTES:
        raise ReleaseSigningError(
            f"release asset {path} is {file_size} bytes; GitHub release assets "
            f"must be at most {MAX_RELEASE_ASSET_BYTES} bytes"
        )
    if artifact_kind == "archive" and file_size > MAX_UPDATE_PACKAGE_BYTES:
        raise ReleaseSigningError(f"{path}: update package exceeds 4 GiB")
    if artifact_kind == "manifest" and file_size > MAX_RELEASE_METADATA_BYTES:
        raise ReleaseSigningError(f"{path}: update manifest exceeds 32 MiB")


def load_metadata_files(artifacts_root: pathlib.Path) -> list[pathlib.Path]:
    try:
        files = discover_bundle_metadata_files(artifacts_root)
    except (OSError, ArtifactLayoutError) as exc:
        raise SystemExit(f"Invalid artifact metadata layout: {exc}") from exc
    if not files:
        raise SystemExit(f"No metadata files found under {artifacts_root}")
    return files


def find_asset(
    bundle_dir: pathlib.Path,
    name: str,
    *,
    artifacts_root: pathlib.Path | None = None,
) -> pathlib.Path:
    try:
        return require_bundle_asset(
            artifacts_root or bundle_dir,
            bundle_dir,
            name,
        )
    except FileNotFoundError as exc:
        raise SystemExit(f"Expected asset not found: {name} in {bundle_dir}") from exc
    except (OSError, ArtifactLayoutError) as exc:
        raise SystemExit(f"Invalid release asset binding for {name!r}: {exc}") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description="Compose cross-platform release index from build metadata.")
    parser.add_argument("--artifacts-root", required=True, help="Directory containing downloaded build artifacts")
    parser.add_argument("--repo", required=True, help="GitHub owner/repo")
    parser.add_argument("--channel", required=True, help="Release channel")
    parser.add_argument("--version", required=True, help="Release version")
    parser.add_argument("--tag", required=True, help="Release tag")
    parser.add_argument("--commit-sha", required=True, help="Release commit")
    parser.add_argument(
        "--release-sequence",
        type=int,
        help="Positive monotonic channel sequence (defaults to the current UTC epoch second)",
    )
    parser.add_argument(
        "--valid-for-days",
        type=int,
        help="Signed index validity window (default: 14 nightly, 180 otherwise)",
    )
    parser.add_argument(
        "--revocation-file",
        help="Schema-1 channel release-index SHA-256 revocation list",
    )
    parser.add_argument("--index-path", required=True, help="Output release index JSON path")
    parser.add_argument("--asset-list-path", required=True, help="Output newline-delimited release asset paths")
    parser.add_argument(
        "--signing-key-file",
        action="append",
        default=[],
        help="Offline Ed25519 private key file (repeat for overlap rotation)",
    )
    parser.add_argument(
        "--signing-key-list",
        action="append",
        default=[],
        help=(
            "Private JSON signing-key path list containing sibling key files "
            "(repeat to combine lists)"
        ),
    )
    parser.add_argument(
        "--trust-policy",
        help="Public release-key trust policy used to verify the produced signature",
    )
    parser.add_argument(
        "--signature-path",
        help="Detached signature output (default: <index-path>.sig.json)",
    )
    parser.add_argument(
        "--defer-signing",
        action="store_true",
        help=(
            "Write an offline signing request without creating a publish-ready "
            "asset list"
        ),
    )
    parser.add_argument(
        "--signing-request-path",
        help="Offline signing request output (default: <index-path>.signing-request.json)",
    )
    parser.add_argument(
        "--allow-unsigned-development-index",
        action="store_true",
        help="Allow an unsigned index only for the explicit development channel",
    )
    args = parser.parse_args()

    if args.release_sequence is not None and not (
        1 <= args.release_sequence <= (1 << 64) - 1
    ):
        raise SystemExit("--release-sequence must be in the unsigned 64-bit range")
    valid_for_days = args.valid_for_days
    if valid_for_days is None:
        valid_for_days = 14 if args.channel == "nightly" else 180
    if not 1 <= valid_for_days <= MAX_RELEASE_VALIDITY_DAYS:
        raise SystemExit(
            f"--valid-for-days must be between 1 and {MAX_RELEASE_VALIDITY_DAYS}"
        )

    has_signing_keys = bool(args.signing_key_file or args.signing_key_list)
    if args.defer_signing:
        if has_signing_keys:
            raise SystemExit(
                "--defer-signing cannot be combined with private signing keys"
            )
        if args.allow_unsigned_development_index:
            raise SystemExit(
                "--defer-signing cannot be combined with unsigned development mode"
            )
        if not args.trust_policy:
            raise SystemExit("--trust-policy is required when deferring signing")
    elif has_signing_keys:
        if not args.trust_policy:
            raise SystemExit("--trust-policy is required when signing a release index")
        if args.allow_unsigned_development_index:
            raise SystemExit(
                "--allow-unsigned-development-index cannot be combined with signing keys"
            )
    elif not args.allow_unsigned_development_index:
        raise SystemExit(
            "Release index signing is required; provide --signing-key-file or "
            "--signing-key-list with --trust-policy, or explicitly allow an unsigned "
            "development fixture"
        )
    elif args.channel != "development":
        raise SystemExit(
            "--allow-unsigned-development-index is restricted to --channel development"
        )
    elif args.trust_policy or args.signature_path or args.signing_request_path:
        raise SystemExit("unsigned development indices cannot use signature options")

    index_path = _absolute_without_following_links(args.index_path)
    asset_list_path = _absolute_without_following_links(args.asset_list_path)
    signature_path = None
    signing_request_path = None
    authorization_path = None
    trust_policy_path = None
    revocation_path = None
    revoked_release_index_sha256s: list[str] = []
    trust_policy = None
    if has_signing_keys or args.defer_signing:
        signature_path = (
            _absolute_without_following_links(args.signature_path)
            if args.signature_path
            else pathlib.Path(f"{index_path}.sig.json")
        )
        trust_policy_path = _absolute_without_following_links(args.trust_policy)
        expected_signature_path = pathlib.Path(f"{index_path}.sig.json")
        if _path_identity(signature_path) != _path_identity(expected_signature_path):
            raise SystemExit(
                "--signature-path must be the canonical detached-signature name "
                f"{expected_signature_path}"
            )
    if args.defer_signing:
        signing_request_path = (
            _absolute_without_following_links(args.signing_request_path)
            if args.signing_request_path
            else pathlib.Path(f"{index_path}.signing-request.json")
        )
        authorization_path = pathlib.Path(
            f"{signing_request_path}.authorization.sig.json"
        )
        deferred_parent_identities = {
            _path_identity(path.parent)[1]
            for path in (
                index_path,
                signature_path,
                asset_list_path,
                signing_request_path,
                authorization_path,
            )
        }
        if len(deferred_parent_identities) != 1:
            raise SystemExit(
                "Deferred index, signing request, signature, and asset-list outputs must be siblings"
            )
    elif args.signing_request_path:
        raise SystemExit("--signing-request-path requires --defer-signing")

    if args.revocation_file:
        revocation_path = _absolute_without_following_links(args.revocation_file)
        try:
            revoked_release_index_sha256s = load_release_revocations(
                revocation_path, args.channel
            )
        except (OSError, ReleaseSigningError) as exc:
            raise SystemExit(f"Invalid release revocation list: {exc}") from exc

    signing_key_paths: list[pathlib.Path] = []
    if has_signing_keys:
        try:
            signing_key_paths = collect_signing_key_paths(
                args.signing_key_file, args.signing_key_list
            )
        except (OSError, ReleaseSigningError) as exc:
            raise SystemExit(f"Invalid release signing key input: {exc}") from exc

    artifacts_root = _absolute_without_following_links(args.artifacts_root)
    metadata_files = load_metadata_files(artifacts_root)
    source_paths: list[tuple[pathlib.Path, str]] = []
    if revocation_path is not None:
        source_paths.append((revocation_path, "release revocation list"))
    if has_signing_keys or args.defer_signing:
        source_paths.extend(
            (path, "release signing key") for path in signing_key_paths
        )
        source_paths.extend(
            (
                _absolute_without_following_links(value),
                "release signing key path list",
            )
            for value in args.signing_key_list
        )
        source_paths.append((trust_policy_path, "release trust policy"))
        try:
            _require_regular_single_link_file(
                trust_policy_path,
                "release trust policy",
            )
            trust_policy = load_trust_policy(trust_policy_path)
        except (OSError, ReleaseSigningError) as exc:
            raise SystemExit(f"Invalid release trust policy: {exc}") from exc

    targets: list[dict[str, Any]] = []
    asset_paths: list[pathlib.Path] = []
    release_asset_descriptors: dict[str, dict[str, Any]] = {}
    seen_platform_ids: dict[str, pathlib.Path] = {}
    seen_artifact_names: dict[str, pathlib.Path] = {}

    for metadata_path in metadata_files:
        try:
            _require_regular_single_link_file(
                metadata_path,
                "release metadata input",
            )
        except ReleaseSigningError as exc:
            raise SystemExit(str(exc)) from exc
        source_paths.append((metadata_path, "release metadata input"))
        if metadata_path.stat().st_size > MAX_RELEASE_METADATA_BYTES:
            raise SystemExit(f"{metadata_path}: release metadata exceeds 32 MiB")
        try:
            metadata = json.loads(
                metadata_path.read_text(encoding="utf-8", errors="strict"),
                object_pairs_hook=_reject_duplicate_json_keys,
                parse_constant=_reject_json_constant,
            )
        except (
            OSError,
            UnicodeDecodeError,
            json.JSONDecodeError,
            ReleaseSigningError,
        ) as exc:
            raise SystemExit(f"{metadata_path}: invalid strict JSON ({exc})") from exc
        if not isinstance(metadata, dict):
            raise SystemExit(f"{metadata_path}: release metadata root must be an object")
        expected_release_identity = {
            "repo": args.repo,
            "channel": args.channel,
            "version": args.version,
            "commit_sha": args.commit_sha,
        }
        for key, expected_value in expected_release_identity.items():
            if metadata.get(key) != expected_value:
                raise SystemExit(
                    f"{metadata_path}: {key} mismatch "
                    f"({metadata.get(key)!r} != {expected_value!r})"
                )
        platform_id = metadata.get("platform_id")
        if not isinstance(platform_id, str) or not platform_id:
            raise SystemExit(f"{metadata_path}: invalid platform_id")
        expected_metadata_name = f"metadata-{platform_id}.json"
        if metadata_path.name != expected_metadata_name:
            raise SystemExit(
                f"{metadata_path}: metadata filename must be {expected_metadata_name}"
            )
        platform_key = platform_id.casefold()
        previous_platform = seen_platform_ids.get(platform_key)
        if previous_platform is not None:
            raise SystemExit(
                f"duplicate release platform {platform_id!r} in {previous_platform} "
                f"and {metadata_path}"
            )
        seen_platform_ids[platform_key] = metadata_path

        bundle_dir = metadata_path.parent
        target_files = []
        artifacts = metadata.get("artifacts")
        if not isinstance(artifacts, list):
            raise SystemExit(f"{metadata_path}: artifacts must be an array")
        for artifact in artifacts:
            if not isinstance(artifact, dict) or not isinstance(artifact.get("name"), str):
                raise SystemExit(f"{metadata_path}: invalid release artifact entry")
            name = artifact["name"]
            artifact_key = name.casefold()
            previous_artifact = seen_artifact_names.get(artifact_key)
            if previous_artifact is not None:
                raise SystemExit(
                    f"duplicate release asset name {name!r} in {previous_artifact} "
                    f"and {metadata_path}"
                )
            seen_artifact_names[artifact_key] = metadata_path
            file_path = find_asset(
                bundle_dir,
                name,
                artifacts_root=artifacts_root,
            )
            try:
                _require_regular_single_link_file(
                    file_path,
                    f"release artifact {name}",
                )
            except ReleaseSigningError as exc:
                raise SystemExit(str(exc)) from exc
            source_paths.append((file_path, f"release artifact {name}"))
            rel_path = file_path.relative_to(artifacts_root).as_posix()
            file_size = file_path.stat().st_size
            artifact_kind = artifact.get("kind", "archive")
            try:
                validate_release_artifact_size(
                    file_path, artifact_kind, file_size
                )
            except ReleaseSigningError as exc:
                raise SystemExit(str(exc)) from exc
            file_sha256 = sha256_file(file_path)
            target_files.append(
                {
                    "name": name,
                    "kind": artifact_kind,
                    "role": artifact.get("role", ""),
                    "path": rel_path,
                    "size": file_size,
                    "sha256": file_sha256,
                }
            )
            asset_paths.append(file_path)
            release_asset_descriptors[
                os.path.normcase(str(file_path)).casefold()
            ] = {
                "path": str(file_path),
                "size": file_size,
                "sha256": file_sha256,
            }

        targets.append(
            {
                "platform_id": platform_id,
                "platform_stub": metadata.get("platform_stub", ""),
                "os": metadata["os"],
                "arch": metadata["arch"],
                "archive_format": metadata["archive_format"],
                "autoupdater": metadata.get("autoupdater", {}),
                "roles": metadata.get("roles", {}),
                "files": target_files,
            }
        )

    targets.sort(key=lambda item: item["platform_id"])
    asset_paths = sorted(set(asset_paths))

    generated_at = dt.datetime.now(dt.timezone.utc).replace(microsecond=0)
    expires_at = generated_at + dt.timedelta(days=valid_for_days)
    release_sequence = args.release_sequence
    if release_sequence is None:
        release_sequence = int(generated_at.timestamp())
    index = {
        "schema_version": 3,
        "generated_at_utc": generated_at.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "expires_at_utc": expires_at.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "release_sequence": release_sequence,
        "revoked_release_index_sha256s": revoked_release_index_sha256s,
        "repo": args.repo,
        "channel": args.channel,
        "version": args.version,
        "tag": args.tag,
        "commit_sha": args.commit_sha,
        "targets": targets,
    }

    output_paths = [
        (index_path, "release index output"),
        (asset_list_path, "release asset-list output"),
    ]
    if signature_path is not None:
        output_paths.append((signature_path, "release signature output"))
    if signing_request_path is not None:
        output_paths.append(
            (signing_request_path, "offline signing request output")
        )
        output_paths.append(
            (authorization_path, "request authorization output")
        )
    try:
        validate_compose_path_contract(source_paths, output_paths)
    except ReleaseSigningError as exc:
        raise SystemExit(f"Invalid release path contract: {exc}") from exc

    if args.defer_signing:
        for pending_path, description in (
            (index_path, "release index output"),
            (signature_path, "release signature output"),
            (asset_list_path, "release asset-list output"),
            (signing_request_path, "offline signing request output"),
            (authorization_path, "request authorization output"),
        ):
            if pending_path.exists():
                raise SystemExit(
                    f"Deferred signing requires a fresh {description}: {pending_path}"
                )

    index_contents = json.dumps(index, indent=2) + "\n"
    index_payload = index_contents.encode("utf-8")
    if len(index_payload) > MAX_RELEASE_METADATA_BYTES:
        raise SystemExit("release index exceeds the updater's 32 MiB metadata limit")
    asset_paths.append(index_path)
    release_asset_descriptors[
        os.path.normcase(str(index_path)).casefold()
    ] = {
        "path": str(index_path),
        "size": len(index_payload),
        "sha256": hashlib.sha256(index_payload).hexdigest(),
    }

    if args.defer_signing:
        request_root = signing_request_path.parent
        trust_policy_payload = trust_policy_path.read_bytes()
        release_asset_descriptors[
            os.path.normcase(str(trust_policy_path)).casefold()
        ] = {
            "path": str(trust_policy_path),
            "size": len(trust_policy_payload),
            "sha256": hashlib.sha256(trust_policy_payload).hexdigest(),
        }
        if len(release_asset_descriptors) > MAX_SIGNING_REQUEST_ASSETS:
            raise SystemExit(
                "offline signing request contains too many release assets "
                f"({len(release_asset_descriptors)} > {MAX_SIGNING_REQUEST_ASSETS})"
            )
        try:
            portable_assets = []
            portable_identities: set[str] = set()
            portable_basenames: dict[str, str] = {}
            for index_value, descriptor in enumerate(
                release_asset_descriptors.values()
            ):
                relative = portable_relative_request_path(
                    pathlib.Path(descriptor["path"]),
                    request_root,
                    f"offline signing request release asset {index_value}",
                )
                identity = relative.casefold()
                if identity in portable_identities:
                    raise ReleaseSigningError(
                        f"portable release asset collision at {relative!r}"
                    )
                portable_identities.add(identity)
                basename = relative.rsplit("/", 1)[-1]
                prior_basename = portable_basenames.get(basename.casefold())
                if prior_basename is not None:
                    raise ReleaseSigningError(
                        "duplicate publish basename under portable comparison: "
                        f"{prior_basename!r} and {basename!r}"
                    )
                portable_basenames[basename.casefold()] = basename
                portable_assets.append(
                    {
                        "path": relative,
                        "size": descriptor["size"],
                        "sha256": descriptor["sha256"],
                    }
                )
            portable_assets.sort(
                key=lambda item: (item["path"].casefold(), item["path"])
            )
            payload_relative = portable_relative_request_path(
                index_path,
                request_root,
                "offline signing request payload",
            )
            policy_relative = portable_relative_request_path(
                trust_policy_path,
                request_root,
                "offline signing request trust policy",
            )
            signature_relative = portable_relative_request_path(
                signature_path,
                request_root,
                "offline signing request signature output",
            )
            authorization_relative = portable_relative_request_path(
                authorization_path,
                request_root,
                "offline signing request authorization output",
            )
            asset_list_relative = portable_relative_request_path(
                asset_list_path,
                request_root,
                "offline signing request asset-list output",
            )
        except ReleaseSigningError as exc:
            raise SystemExit(f"Invalid relocatable signing request: {exc}") from exc
        signing_request = {
            "schema_version": SIGNING_REQUEST_SCHEMA_VERSION,
            "status": SIGNING_REQUEST_STATUS,
            "repo": args.repo,
            "channel": args.channel,
            "payload": {
                "path": payload_relative,
                "name": index_path.name,
                "media_type": RELEASE_INDEX_MEDIA_TYPE,
                "size": len(index_payload),
                "sha256": hashlib.sha256(index_payload).hexdigest(),
            },
            "trust_policy": {
                "path": policy_relative,
                "sha256": hashlib.sha256(trust_policy_payload).hexdigest(),
                "threshold": trust_policy.threshold,
                "key_ids": sorted(trust_policy.keys),
            },
            "outputs": {
                "signature_path": signature_relative,
                "authorization_path": authorization_relative,
                "asset_list_path": asset_list_relative,
            },
            "release_assets": portable_assets,
        }
        signing_request_payload = (
            json.dumps(signing_request, indent=2, sort_keys=True) + "\n"
        ).encode("utf-8")
        if len(signing_request_payload) > MAX_SIGNING_REQUEST_SIZE:
            raise SystemExit(
                "offline signing request exceeds the supported "
                f"{MAX_SIGNING_REQUEST_SIZE}-byte limit"
            )
        _atomic_write_set(
            [
                (index_path, index_payload),
                (signing_request_path, signing_request_payload),
            ]
        )
        print(f"Wrote immutable release index {index_path}")
        print(f"Wrote offline signing request {signing_request_path}")
        print(
            "Deferred signing is not publish-ready; the release signature and "
            f"asset list {asset_list_path} were intentionally not created"
        )
        return 0

    if has_signing_keys:
        try:
            signing_keys = [
                load_private_signing_key(path) for path in signing_key_paths
            ]
            envelope = create_signature_envelope(
                index_payload, index_path.name, signing_keys
            )
            signature_payload = serialize_signature_envelope(envelope)
            verification = verify_signature_envelope(
                index_payload,
                index_path.name,
                signature_payload,
                trust_policy,
                expected_repo=args.repo,
                expected_channel=args.channel,
            )
        except (OSError, ReleaseSigningError) as exc:
            raise SystemExit(f"Release index signing failed: {exc}") from exc
        asset_paths.append(signature_path)
        asset_paths.append(trust_policy_path)
        print(
            f"Signed {index_path} with {len(verification.verified_key_ids)} "
            "authorized Ed25519 signature(s)"
        )

    if len(set(asset_paths)) > MAX_PUBLISHED_ASSETS:
        raise SystemExit(
            "publish-ready release contains too many assets "
            f"({len(set(asset_paths))} > {MAX_PUBLISHED_ASSETS})"
        )

    try:
        asset_entries = {
            portable_relative_request_path(
                path,
                asset_list_path.parent,
                "publish-ready release asset",
            )
            for path in set(asset_paths)
        }
    except ReleaseSigningError as exc:
        raise SystemExit(f"Invalid portable release asset list: {exc}") from exc
    if len(asset_entries) != len(set(asset_paths)):
        raise SystemExit("Portable release asset paths collide")
    asset_lines = [
        f"{entry}\n"
        for entry in sorted(asset_entries, key=lambda value: (value.casefold(), value))
    ]
    asset_list_payload = "".join(asset_lines).encode("utf-8")
    coherent_outputs = [
        (index_path, index_payload),
        (asset_list_path, asset_list_payload),
    ]
    if has_signing_keys:
        coherent_outputs.append((signature_path, signature_payload))
    _atomic_write_set(coherent_outputs)

    print(f"Wrote {index_path}")
    print(f"Wrote {asset_list_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
