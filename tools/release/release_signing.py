#!/usr/bin/env python3

from __future__ import annotations

import argparse
import base64
import datetime as dt
import hashlib
import json
import os
import pathlib
import re
import stat
import struct
import sys
import tempfile
from dataclasses import dataclass
from typing import Any, Iterable

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))


SIGNATURE_SCHEMA_VERSION = 1
KEY_SCHEMA_VERSION = 1
TRUST_POLICY_SCHEMA_VERSION = 1
SIGNATURE_ALGORITHM = "ed25519"
RELEASE_INDEX_MEDIA_TYPE = "application/vnd.worr.release-index.v3+json"
SIGNING_DOMAIN = b"WORR release index signature v1\0"
REQUEST_AUTHORIZATION_DOMAIN = b"WORR release publication authorization v1\0"
REQUEST_AUTHORIZATION_MEDIA_TYPE = (
    "application/vnd.worr.release-signing-request.v2+json"
)
REQUEST_AUTHORIZATION_SCHEMA_VERSION = 1
SIGNED_REQUEST_BUNDLE_SCHEMA_VERSION = 1
MAX_RELEASE_INDEX_SIZE = 32 * 1024 * 1024
MAX_SIGNATURE_ENVELOPE_SIZE = 1024 * 1024
MAX_POLICY_KEYS = 32
MAX_SIGNATURES = 64
MAX_SCOPES_PER_KEY = 64
MAX_SCOPE_BYTES = 255
MAX_PAYLOAD_NAME_BYTES = 255
SIGNING_REQUEST_SCHEMA_VERSION = 2
SIGNING_REQUEST_STATUS = "awaiting-signatures"
MAX_SIGNING_REQUEST_SIZE = 1024 * 1024
MAX_PUBLISHED_ASSETS = 1000
PUBLISHED_CONTROL_ASSETS = 3  # request + index signature + request authorization
MAX_SIGNING_REQUEST_ASSETS = MAX_PUBLISHED_ASSETS - PUBLISHED_CONTROL_ASSETS
MAX_SIGNING_KEY_LIST_SIZE = 1024 * 1024
MAX_PORTABLE_PATH_BYTES = 4096
MAX_PORTABLE_COMPONENT_BYTES = 255
MAX_CANONICAL_ASSET_LIST_SIZE = MAX_PUBLISHED_ASSETS * (
    MAX_PORTABLE_PATH_BYTES + 1
)
MAX_RELEASE_VALIDITY_DAYS = 366
MAX_RELEASE_CLOCK_SKEW = dt.timedelta(hours=24)
MAX_CANDIDATE_AUTHORIZATION_AGE = dt.timedelta(hours=24)
MAX_REVOKED_RELEASE_INDICES = 1024
MAX_JSON_DOCUMENT_SIZE = 1024 * 1024
KEY_ID_PATTERN = re.compile(r"[0-9a-f]{64}")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
BASE64URL_PATTERN = re.compile(r"[A-Za-z0-9_-]+")
PORTABLE_COMPONENT_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")
WINDOWS_RESERVED_COMPONENT_PATTERN = re.compile(
    r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?",
    re.IGNORECASE,
)


class ReleaseSigningError(ValueError):
    pass


def _utc_now() -> dt.datetime:
    return dt.datetime.now(dt.timezone.utc)


def _require_release_authorization_window(
    index: dict[str, Any],
    *,
    now: dt.datetime | None = None,
) -> None:
    value = index.get("generated_at_utc")
    if not isinstance(value, str):
        raise ReleaseSigningError("release candidate has invalid generated_at_utc")
    try:
        generated = dt.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ").replace(
            tzinfo=dt.timezone.utc
        )
    except ValueError as exc:
        raise ReleaseSigningError(
            "release candidate has invalid generated_at_utc"
        ) from exc
    if generated.strftime("%Y-%m-%dT%H:%M:%SZ") != value:
        raise ReleaseSigningError("release candidate has non-canonical generated_at_utc")

    current = now if now is not None else _utc_now()
    if current.tzinfo is None:
        current = current.replace(tzinfo=dt.timezone.utc)
    current = current.astimezone(dt.timezone.utc)
    if (
        generated - current > MAX_RELEASE_CLOCK_SKEW
        or current - generated > MAX_CANDIDATE_AUTHORIZATION_AGE
    ):
        raise ReleaseSigningError(
            "release candidate generation time is outside the authorization window"
        )


@dataclass(frozen=True)
class PrivateSigningKey:
    key_id: str
    public_key: bytes
    private_seed: bytes


@dataclass(frozen=True)
class TrustedReleaseKey:
    key_id: str
    public_key: bytes
    repos: frozenset[str]
    channels: frozenset[str]


@dataclass(frozen=True)
class TrustPolicy:
    threshold: int
    keys: dict[str, TrustedReleaseKey]


@dataclass(frozen=True)
class VerifiedReleaseIndex:
    payload: bytes
    index: dict[str, Any]
    verified_key_ids: tuple[str, ...]


@dataclass(frozen=True)
class OfflineSigningRequest:
    request_path: pathlib.Path
    request_payload: bytes
    trust_policy_path: pathlib.Path
    trust_policy_payload: bytes
    payload_path: pathlib.Path
    payload: bytes
    signature_path: pathlib.Path
    authorization_path: pathlib.Path
    asset_list_path: pathlib.Path
    release_asset_paths: tuple[pathlib.Path, ...]
    release_asset_snapshots: tuple["AuthorizedPublishAsset", ...]
    trust_policy: TrustPolicy
    repo: str
    channel: str


@dataclass(frozen=True)
class VerifiedPublishAuthorization:
    request: OfflineSigningRequest
    verified_index: VerifiedReleaseIndex
    authorized_assets: tuple["AuthorizedPublishAsset", ...]

    @property
    def authorized_asset_paths(self) -> tuple[pathlib.Path, ...]:
        return tuple(asset.path for asset in self.authorized_assets)


@dataclass(frozen=True)
class AuthorizedPublishAsset:
    path: pathlib.Path
    name: str
    size: int
    digest: str


def _absolute_without_following_links(value: str | os.PathLike[str]) -> pathlib.Path:
    """Return a lexical absolute path without resolving a link at the leaf."""

    return pathlib.Path(os.path.abspath(os.fspath(value)))


def _require_crypto() -> tuple[Any, Any, Any]:
    try:
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives.asymmetric.ed25519 import (
            Ed25519PrivateKey,
            Ed25519PublicKey,
        )
    except ImportError as exc:
        raise ReleaseSigningError(
            "Ed25519 release signing requires the Python 'cryptography' package"
        ) from exc
    return Ed25519PrivateKey, Ed25519PublicKey, InvalidSignature


def _reject_duplicate_json_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise ReleaseSigningError(f"duplicate JSON key {key!r}")
        value[key] = item
    return value


def _load_json_bytes(payload: bytes, description: str) -> Any:
    def reject_constant(value: str) -> None:
        raise ReleaseSigningError(f"{description} contains non-finite number {value}")

    try:
        text = payload.decode("utf-8", errors="strict")
        return json.loads(
            text,
            object_pairs_hook=_reject_duplicate_json_keys,
            parse_constant=reject_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ReleaseSigningError(f"{description} is not valid UTF-8 JSON ({exc})") from exc


def _load_json_file(
    path: pathlib.Path,
    description: str,
    maximum_size: int = MAX_JSON_DOCUMENT_SIZE,
) -> Any:
    try:
        return _load_json_bytes(
            _read_regular_file_bounded(path, maximum_size, description),
            description,
        )
    except OSError as exc:
        raise ReleaseSigningError(f"cannot read {description} {path} ({exc})") from exc


def _require_exact_keys(value: dict[str, Any], expected: set[str], description: str) -> None:
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        unexpected = sorted(actual - expected)
        details: list[str] = []
        if missing:
            details.append(f"missing {', '.join(missing)}")
        if unexpected:
            details.append(f"unexpected {', '.join(unexpected)}")
        raise ReleaseSigningError(f"{description} has invalid fields ({'; '.join(details)})")


def _b64url_encode(value: bytes) -> str:
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode("ascii")


def _b64url_decode(value: Any, expected_size: int, description: str) -> bytes:
    if not isinstance(value, str) or not BASE64URL_PATTERN.fullmatch(value):
        raise ReleaseSigningError(f"{description} is not canonical unpadded base64url")
    padding = "=" * ((4 - len(value) % 4) % 4)
    try:
        decoded = base64.b64decode(value + padding, altchars=b"-_", validate=True)
    except (ValueError, base64.binascii.Error) as exc:
        raise ReleaseSigningError(f"{description} is not valid base64url") from exc
    if len(decoded) != expected_size or _b64url_encode(decoded) != value:
        raise ReleaseSigningError(f"{description} must encode exactly {expected_size} bytes")
    return decoded


def derive_key_id(public_key: bytes) -> str:
    if len(public_key) != 32:
        raise ReleaseSigningError("Ed25519 public key must be 32 bytes")
    return hashlib.sha256(public_key).hexdigest()


def _validate_key_identity(value: dict[str, Any], description: str) -> tuple[str, bytes]:
    if value.get("algorithm") != SIGNATURE_ALGORITHM:
        raise ReleaseSigningError(f"{description} algorithm must be {SIGNATURE_ALGORITHM}")
    key_id = value.get("key_id")
    if not isinstance(key_id, str) or not KEY_ID_PATTERN.fullmatch(key_id):
        raise ReleaseSigningError(f"{description} has an invalid key_id")
    public_key = _b64url_decode(value.get("public_key"), 32, f"{description} public_key")
    expected_key_id = derive_key_id(public_key)
    if key_id != expected_key_id:
        raise ReleaseSigningError(
            f"{description} key_id does not match the Ed25519 public key"
        )
    return key_id, public_key


def private_key_document(private_seed: bytes) -> dict[str, Any]:
    if len(private_seed) != 32:
        raise ReleaseSigningError("Ed25519 private seed must be 32 bytes")
    Ed25519PrivateKey, _, _ = _require_crypto()
    private_key = Ed25519PrivateKey.from_private_bytes(private_seed)
    public_key = private_key.public_key().public_bytes_raw()
    return {
        "schema_version": KEY_SCHEMA_VERSION,
        "algorithm": SIGNATURE_ALGORITHM,
        "key_id": derive_key_id(public_key),
        "public_key": _b64url_encode(public_key),
        "private_key": _b64url_encode(private_seed),
    }


def generate_private_key_document() -> dict[str, Any]:
    Ed25519PrivateKey, _, _ = _require_crypto()
    private_key = Ed25519PrivateKey.generate()
    return private_key_document(private_key.private_bytes_raw())


def trust_key_document(
    private_document: dict[str, Any],
    *,
    repos: Iterable[str],
    channels: Iterable[str],
) -> dict[str, Any]:
    return {
        "key_id": private_document["key_id"],
        "algorithm": private_document["algorithm"],
        "public_key": private_document["public_key"],
        "repos": sorted(set(repos)),
        "channels": sorted(set(channels)),
    }


def load_private_signing_key(path: pathlib.Path) -> PrivateSigningKey:
    try:
        path_status = path.lstat()
    except OSError as exc:
        raise ReleaseSigningError(f"cannot inspect private signing key {path} ({exc})") from exc
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    file_attributes = getattr(path_status, "st_file_attributes", 0)
    if (
        not stat.S_ISREG(path_status.st_mode)
        or stat.S_ISLNK(path_status.st_mode)
        or file_attributes & reparse_flag
    ):
        raise ReleaseSigningError(f"private signing key {path} must be a regular non-link file")
    if path_status.st_nlink != 1:
        raise ReleaseSigningError(f"private signing key {path} must not have hard links")
    document = _load_json_file(path, "private signing key")
    if not isinstance(document, dict):
        raise ReleaseSigningError("private signing key root must be an object")
    _require_exact_keys(
        document,
        {"schema_version", "algorithm", "key_id", "public_key", "private_key"},
        "private signing key",
    )
    if type(document.get("schema_version")) is not int or document["schema_version"] != KEY_SCHEMA_VERSION:
        raise ReleaseSigningError("private signing key has an unsupported schema_version")
    key_id, public_key = _validate_key_identity(document, "private signing key")
    private_seed = _b64url_decode(
        document.get("private_key"), 32, "private signing key private_key"
    )

    Ed25519PrivateKey, _, _ = _require_crypto()
    actual_public_key = (
        Ed25519PrivateKey.from_private_bytes(private_seed).public_key().public_bytes_raw()
    )
    if actual_public_key != public_key:
        raise ReleaseSigningError("private signing key seed does not match its public key")

    if os.name != "nt":
        permissions = stat.S_IMODE(path_status.st_mode)
        if permissions & 0o077:
            raise ReleaseSigningError(
                f"private signing key {path} must not be readable or writable by group/other"
            )
    return PrivateSigningKey(key_id, public_key, private_seed)


def _validate_scope_list(value: Any, description: str) -> frozenset[str]:
    if (
        not isinstance(value, list)
        or not value
        or len(value) > MAX_SCOPES_PER_KEY
        or any(
            not isinstance(item, str)
            or not item
            or item.strip() != item
            or len(item.encode("utf-8")) > MAX_SCOPE_BYTES
            or any(ord(character) < 0x20 or ord(character) > 0x7E for character in item)
            for item in value
        )
    ):
        raise ReleaseSigningError(
            f"{description} must contain 1..{MAX_SCOPES_PER_KEY} trimmed printable ASCII strings of at most {MAX_SCOPE_BYTES} bytes"
        )
    if len(set(value)) != len(value):
        raise ReleaseSigningError(f"{description} contains duplicates")
    return frozenset(value)


def _parse_trust_policy_document(document: Any) -> TrustPolicy:
    if not isinstance(document, dict):
        raise ReleaseSigningError("release trust policy root must be an object")
    _require_exact_keys(document, {"schema_version", "threshold", "keys"}, "release trust policy")
    if (
        type(document.get("schema_version")) is not int
        or document["schema_version"] != TRUST_POLICY_SCHEMA_VERSION
    ):
        raise ReleaseSigningError("release trust policy has an unsupported schema_version")
    threshold = document.get("threshold")
    keys_payload = document.get("keys")
    if type(threshold) is not int or threshold <= 0:
        raise ReleaseSigningError("release trust policy threshold must be a positive integer")
    if (
        not isinstance(keys_payload, list)
        or not keys_payload
        or len(keys_payload) > MAX_POLICY_KEYS
    ):
        raise ReleaseSigningError(
            f"release trust policy keys must contain 1..{MAX_POLICY_KEYS} entries"
        )

    keys: dict[str, TrustedReleaseKey] = {}
    public_keys: set[bytes] = set()
    for index, value in enumerate(keys_payload):
        description = f"release trust policy key {index}"
        if not isinstance(value, dict):
            raise ReleaseSigningError(f"{description} must be an object")
        _require_exact_keys(
            value,
            {"key_id", "algorithm", "public_key", "repos", "channels"},
            description,
        )
        key_id, public_key = _validate_key_identity(value, description)
        if key_id in keys or public_key in public_keys:
            raise ReleaseSigningError("release trust policy contains a duplicate key")
        keys[key_id] = TrustedReleaseKey(
            key_id,
            public_key,
            _validate_scope_list(value.get("repos"), f"{description} repos"),
            _validate_scope_list(value.get("channels"), f"{description} channels"),
        )
        public_keys.add(public_key)
    if threshold > len(keys):
        raise ReleaseSigningError("release trust policy threshold exceeds its key count")
    return TrustPolicy(threshold, keys)


def load_trust_policy_bytes(
    payload: bytes,
    description: str = "release trust policy",
) -> TrustPolicy:
    return _parse_trust_policy_document(_load_json_bytes(payload, description))


def load_trust_policy(path: pathlib.Path) -> TrustPolicy:
    payload = _read_regular_file_bounded(
        path,
        MAX_JSON_DOCUMENT_SIZE,
        "release trust policy",
    )
    return load_trust_policy_bytes(payload)


def validate_portable_relative_path(value: Any, description: str) -> str:
    """Validate a canonical path that is safe to relocate across runner OSes."""

    if (
        not isinstance(value, str)
        or not value
        or value.strip() != value
        or len(value.encode("utf-8")) > MAX_PORTABLE_PATH_BYTES
        or any(ord(character) < 0x20 or ord(character) > 0x7E for character in value)
    ):
        raise ReleaseSigningError(
            f"{description} must be a non-empty printable-ASCII relative path"
        )
    if value.startswith("/") or "\\" in value or value.endswith("/"):
        raise ReleaseSigningError(
            f"{description} must use canonical relative POSIX separators"
        )
    components = value.split("/")
    if any(component in {"", ".", ".."} for component in components):
        raise ReleaseSigningError(
            f"{description} must not contain empty, dot, or traversal components"
        )
    for component in components:
        if (
            len(component.encode("ascii")) > MAX_PORTABLE_COMPONENT_BYTES
            or not PORTABLE_COMPONENT_PATTERN.fullmatch(component)
            or component.endswith((".", " "))
            or WINDOWS_RESERVED_COMPONENT_PATTERN.fullmatch(component)
        ):
            raise ReleaseSigningError(
                f"{description} contains non-portable component {component!r}"
            )
    canonical = "/".join(components)
    if canonical != value:
        raise ReleaseSigningError(f"{description} is not canonical")
    return canonical


def portable_relative_request_path(
    path: pathlib.Path,
    root: pathlib.Path,
    description: str,
) -> str:
    """Serialize *path* below *root* without following filesystem links."""

    candidate = _absolute_without_following_links(path)
    root = _absolute_without_following_links(root)
    try:
        relative = candidate.relative_to(root)
    except ValueError as exc:
        raise ReleaseSigningError(
            f"{description} must be contained by the relocatable request root {root}"
        ) from exc
    if not relative.parts:
        raise ReleaseSigningError(f"{description} must name a file below the request root")
    return validate_portable_relative_path(relative.as_posix(), description)


def _is_link_or_reparse(path_status: os.stat_result) -> bool:
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    return stat.S_ISLNK(path_status.st_mode) or bool(
        getattr(path_status, "st_file_attributes", 0) & reparse_flag
    )


def _require_non_link_directory(path: pathlib.Path, description: str) -> None:
    try:
        path_status = path.lstat()
    except OSError as exc:
        raise ReleaseSigningError(f"cannot inspect {description} {path} ({exc})") from exc
    if not stat.S_ISDIR(path_status.st_mode) or _is_link_or_reparse(path_status):
        raise ReleaseSigningError(f"{description} must be a non-link directory")


def _request_path(root: pathlib.Path, value: Any, description: str) -> pathlib.Path:
    relative = validate_portable_relative_path(value, description)
    _require_non_link_directory(root, "offline signing request root")
    candidate = root
    components = relative.split("/")
    for component in components[:-1]:
        candidate = candidate / component
        _require_non_link_directory(candidate, f"{description} parent")
    return candidate / components[-1]


def _validate_portable_path_inventory(
    values: Iterable[tuple[str, str]],
    *,
    allow_exact_repeats: bool = False,
) -> None:
    """Reject cross-platform case aliases and file/directory prefix collisions."""

    identities: dict[str, tuple[str, str]] = {}
    unique: dict[str, str] = {}
    for value, description in values:
        canonical = validate_portable_relative_path(value, description)
        identity = canonical.casefold()
        prior = identities.get(identity)
        if prior is not None:
            if allow_exact_repeats and prior[0] == canonical:
                continue
            raise ReleaseSigningError(
                f"portable path collision between {prior[1]} {prior[0]!r} and "
                f"{description} {canonical!r}"
            )
        identities[identity] = (canonical, description)
        unique[identity] = canonical

    ordered = sorted(unique.items())
    for identity, canonical in ordered:
        prefix = identity + "/"
        for other_identity, other in ordered:
            if other_identity.startswith(prefix):
                raise ReleaseSigningError(
                    f"portable path hierarchy collision between {canonical!r} and {other!r}"
                )


def _regular_file_status(path: pathlib.Path, description: str) -> os.stat_result:
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
        raise ReleaseSigningError(f"{description} must be a regular non-link file")
    if path_status.st_nlink != 1:
        raise ReleaseSigningError(f"{description} must not have hard links")
    return path_status


def _sha256_file(path: pathlib.Path, description: str) -> tuple[int, str]:
    path_status = _regular_file_status(path, description)
    hasher = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                hasher.update(chunk)
    except OSError as exc:
        raise ReleaseSigningError(f"cannot read {description} {path} ({exc})") from exc
    return path_status.st_size, hasher.hexdigest()


def _read_regular_file_bounded(
    path: pathlib.Path, maximum_size: int, description: str
) -> bytes:
    path_status = _regular_file_status(path, description)
    if path_status.st_size <= 0 or path_status.st_size > maximum_size:
        raise ReleaseSigningError(
            f"{description} must be between 1 and {maximum_size} bytes"
        )
    try:
        with path.open("rb") as handle:
            payload = handle.read(maximum_size + 1)
    except OSError as exc:
        raise ReleaseSigningError(f"cannot read {description} {path} ({exc})") from exc
    if not payload or len(payload) > maximum_size:
        raise ReleaseSigningError(
            f"{description} must be between 1 and {maximum_size} bytes"
        )
    return payload


def _path_identity(path: pathlib.Path) -> tuple[str, str]:
    lexical = os.path.normcase(os.path.normpath(str(path))).casefold()
    try:
        canonical = os.path.normcase(
            os.path.normpath(str(path.resolve(strict=False)))
        ).casefold()
    except OSError as exc:
        raise ReleaseSigningError(f"cannot normalize signing path {path} ({exc})") from exc
    return lexical, canonical


def _same_existing_file(first: pathlib.Path, second: pathlib.Path) -> bool:
    try:
        return os.path.samefile(first, second)
    except FileNotFoundError:
        return False
    except OSError as exc:
        raise ReleaseSigningError(
            f"cannot compare signing paths {first} and {second} ({exc})"
        ) from exc


def _validate_command_path_contract(
    sources: list[tuple[pathlib.Path, str]],
    outputs: list[tuple[pathlib.Path, str]],
    *,
    require_missing_outputs: bool = False,
) -> None:
    """Reject link, hard-link, and input/output aliases before any write."""

    normalized_sources: list[
        tuple[pathlib.Path, str, tuple[str, str]]
    ] = []
    for source_path, description in sources:
        _regular_file_status(source_path, description)
        normalized_sources.append(
            (source_path, description, _path_identity(source_path))
        )

    normalized_outputs: list[
        tuple[pathlib.Path, str, tuple[str, str]]
    ] = []
    for output_path, description in outputs:
        if os.path.lexists(output_path):
            if require_missing_outputs:
                raise ReleaseSigningError(
                    f"refusing to replace existing {description} {output_path}"
                )
            _regular_file_status(output_path, description)
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


def load_offline_signing_request(path: pathlib.Path) -> OfflineSigningRequest:
    path = _absolute_without_following_links(path)
    root = path.parent
    _require_non_link_directory(root, "offline signing request root")
    request_status = _regular_file_status(path, "offline signing request")
    if request_status.st_size <= 0 or request_status.st_size > MAX_SIGNING_REQUEST_SIZE:
        raise ReleaseSigningError(
            f"offline signing request must be between 1 and {MAX_SIGNING_REQUEST_SIZE} bytes"
        )
    request_payload = _read_regular_file_bounded(
        path,
        MAX_SIGNING_REQUEST_SIZE,
        "offline signing request",
    )
    document = _load_json_bytes(request_payload, "offline signing request")
    if not isinstance(document, dict):
        raise ReleaseSigningError("offline signing request root must be an object")
    canonical_request_payload = (
        json.dumps(document, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    if request_payload != canonical_request_payload:
        raise ReleaseSigningError("offline signing request is not canonical JSON")
    _require_exact_keys(
        document,
        {
            "schema_version",
            "status",
            "repo",
            "channel",
            "payload",
            "trust_policy",
            "outputs",
            "release_assets",
        },
        "offline signing request",
    )
    if (
        type(document.get("schema_version")) is not int
        or document["schema_version"] != SIGNING_REQUEST_SCHEMA_VERSION
    ):
        raise ReleaseSigningError(
            "offline signing request has an unsupported schema_version"
        )
    if document.get("status") != SIGNING_REQUEST_STATUS:
        raise ReleaseSigningError("offline signing request has an invalid status")
    repo = document.get("repo")
    channel = document.get("channel")
    if not isinstance(repo, str) or not repo or not isinstance(channel, str) or not channel:
        raise ReleaseSigningError("offline signing request has invalid repo/channel identity")

    payload_descriptor = document.get("payload")
    if not isinstance(payload_descriptor, dict):
        raise ReleaseSigningError("offline signing request payload must be an object")
    _require_exact_keys(
        payload_descriptor,
        {"path", "name", "media_type", "size", "sha256"},
        "offline signing request payload",
    )
    payload_relative = validate_portable_relative_path(
        payload_descriptor.get("path"),
        "offline signing request payload path",
    )
    payload_path = _request_path(
        root,
        payload_relative,
        "offline signing request payload path",
    )
    payload_status = _regular_file_status(
        payload_path,
        "offline signing request payload",
    )
    if payload_status.st_size <= 0 or payload_status.st_size > MAX_RELEASE_INDEX_SIZE:
        raise ReleaseSigningError(
            "offline signing request payload size is outside the supported range"
        )
    payload = _read_regular_file_bounded(
        payload_path,
        MAX_RELEASE_INDEX_SIZE,
        "offline signing request payload",
    )
    expected_payload_descriptor = _signature_payload_descriptor(
        payload,
        payload_path.name,
    )
    if {
        "name": payload_descriptor.get("name"),
        "media_type": payload_descriptor.get("media_type"),
        "size": payload_descriptor.get("size"),
        "sha256": payload_descriptor.get("sha256"),
    } != expected_payload_descriptor:
        raise ReleaseSigningError(
            "offline signing request payload bytes do not match their descriptor"
        )
    release_index_signing_input(payload, payload_path.name)
    index_document = _load_json_bytes(payload, "release index")
    validate_release_index_security_metadata(index_document)
    if index_document.get("repo") != repo or index_document.get("channel") != channel:
        raise ReleaseSigningError(
            "offline signing request identity does not match its release index"
        )

    policy_descriptor = document.get("trust_policy")
    if not isinstance(policy_descriptor, dict):
        raise ReleaseSigningError("offline signing request trust_policy must be an object")
    _require_exact_keys(
        policy_descriptor,
        {"path", "sha256", "threshold", "key_ids"},
        "offline signing request trust_policy",
    )
    policy_relative = validate_portable_relative_path(
        policy_descriptor.get("path"),
        "offline signing request trust policy path",
    )
    policy_path = _request_path(
        root,
        policy_relative,
        "offline signing request trust policy path",
    )
    policy_payload = _read_regular_file_bounded(
        policy_path,
        MAX_JSON_DOCUMENT_SIZE,
        "offline signing request trust policy",
    )
    actual_policy_sha256 = hashlib.sha256(policy_payload).hexdigest()
    if policy_descriptor.get("sha256") != actual_policy_sha256:
        raise ReleaseSigningError(
            "offline signing request trust policy bytes changed"
        )
    trust_policy = load_trust_policy_bytes(
        policy_payload,
        "offline signing request trust policy",
    )
    if (
        type(policy_descriptor.get("threshold")) is not int
        or policy_descriptor["threshold"] != trust_policy.threshold
        or policy_descriptor.get("key_ids") != sorted(trust_policy.keys)
    ):
        raise ReleaseSigningError(
            "offline signing request trust policy identity changed"
        )

    outputs = document.get("outputs")
    if not isinstance(outputs, dict):
        raise ReleaseSigningError("offline signing request outputs must be an object")
    _require_exact_keys(
        outputs,
        {"signature_path", "authorization_path", "asset_list_path"},
        "offline signing request outputs",
    )
    signature_relative = validate_portable_relative_path(
        outputs.get("signature_path"),
        "offline signing request signature output",
    )
    asset_list_relative = validate_portable_relative_path(
        outputs.get("asset_list_path"),
        "offline signing request asset-list output",
    )
    authorization_relative = validate_portable_relative_path(
        outputs.get("authorization_path"),
        "offline signing request authorization output",
    )
    signature_path = _request_path(
        root,
        signature_relative,
        "offline signing request signature output",
    )
    asset_list_path = _request_path(
        root,
        asset_list_relative,
        "offline signing request asset-list output",
    )
    authorization_path = _request_path(
        root,
        authorization_relative,
        "offline signing request authorization output",
    )

    assets = document.get("release_assets")
    if (
        not isinstance(assets, list)
        or not assets
        or len(assets) > MAX_SIGNING_REQUEST_ASSETS
    ):
        raise ReleaseSigningError(
            f"offline signing request release_assets must contain 1..{MAX_SIGNING_REQUEST_ASSETS} entries"
        )
    release_asset_paths: list[pathlib.Path] = []
    release_asset_relatives: list[str] = []
    release_asset_descriptors: dict[str, tuple[int, str]] = {}
    release_asset_snapshots: list[AuthorizedPublishAsset] = []
    identities: set[str] = set()
    payload_seen = False
    policy_seen = False
    for index, asset in enumerate(assets):
        description = f"offline signing request release asset {index}"
        if not isinstance(asset, dict):
            raise ReleaseSigningError(f"{description} must be an object")
        _require_exact_keys(asset, {"path", "size", "sha256"}, description)
        asset_relative = validate_portable_relative_path(
            asset.get("path"), f"{description} path"
        )
        asset_path = _request_path(root, asset_relative, f"{description} path")
        identity = asset_relative.casefold()
        if identity in identities:
            raise ReleaseSigningError("offline signing request repeats a release asset")
        identities.add(identity)
        if asset_relative == payload_relative:
            actual_size = len(payload)
            actual_sha256 = hashlib.sha256(payload).hexdigest()
        elif asset_relative == policy_relative:
            actual_size = len(policy_payload)
            actual_sha256 = hashlib.sha256(policy_payload).hexdigest()
        else:
            actual_size, actual_sha256 = _sha256_file(asset_path, description)
        if (
            type(asset.get("size")) is not int
            or asset["size"] != actual_size
            or asset.get("sha256") != actual_sha256
        ):
            raise ReleaseSigningError(f"{description} bytes changed")
        payload_seen = payload_seen or asset_relative == payload_relative
        policy_seen = policy_seen or asset_relative == policy_relative
        release_asset_paths.append(asset_path)
        release_asset_relatives.append(asset_relative)
        release_asset_descriptors[asset_relative] = (
            asset["size"],
            asset["sha256"],
        )
        release_asset_snapshots.append(
            AuthorizedPublishAsset(
                path=_absolute_without_following_links(asset_path),
                name=asset_path.name,
                size=actual_size,
                digest=actual_sha256,
            )
        )

    if not payload_seen:
        raise ReleaseSigningError(
            "offline signing request release assets omit the release index"
        )
    if not policy_seen:
        raise ReleaseSigningError(
            "offline signing request release assets omit the bound trust policy"
        )

    expected_index_assets: dict[str, tuple[str, int, str]] = {}
    for target_index, target in enumerate(index_document["targets"]):
        if not isinstance(target, dict) or not isinstance(target.get("files"), list):
            raise ReleaseSigningError(
                f"release index target {target_index} has invalid files"
            )
        for file_index, file_descriptor in enumerate(target["files"]):
            description = f"release index target {target_index} file {file_index}"
            if not isinstance(file_descriptor, dict):
                raise ReleaseSigningError(f"{description} must be an object")
            name = file_descriptor.get("name")
            indexed_path = file_descriptor.get("path")
            size = file_descriptor.get("size")
            digest = file_descriptor.get("sha256")
            if (
                not isinstance(name, str)
                or not name
                or not isinstance(indexed_path, str)
                or pathlib.PurePosixPath(indexed_path).name != name
                or type(size) is not int
                or size < 0
                or not isinstance(digest, str)
                or not SHA256_PATTERN.fullmatch(digest)
            ):
                raise ReleaseSigningError(f"{description} is invalid")
            identity = name.casefold()
            if identity in expected_index_assets:
                raise ReleaseSigningError(
                    "release index repeats a portable release asset basename"
                )
            expected_index_assets[identity] = (name, size, digest)

    actual_index_assets: dict[str, tuple[str, int, str]] = {}
    for relative, (size, digest) in release_asset_descriptors.items():
        if relative in {payload_relative, policy_relative}:
            continue
        name = relative.rsplit("/", 1)[-1]
        actual_index_assets[name.casefold()] = (name, size, digest)
    if actual_index_assets != expected_index_assets:
        raise ReleaseSigningError(
            "offline signing request release assets are not the exact set described by the release index"
        )

    inventory = [
        (payload_relative, "release-index payload"),
        (policy_relative, "release trust policy"),
        (signature_relative, "signature output"),
        (authorization_relative, "request authorization output"),
        (asset_list_relative, "asset-list output"),
        *(
            (relative, f"release asset {index}")
            for index, relative in enumerate(release_asset_relatives)
        ),
    ]
    _validate_portable_path_inventory(inventory, allow_exact_repeats=True)
    reserved_identities = {
        signature_relative.casefold(),
        authorization_relative.casefold(),
        asset_list_relative.casefold(),
    }
    if len(reserved_identities) != 3 or identities & reserved_identities:
        raise ReleaseSigningError(
            "offline signing request outputs collide with release inputs"
        )
    request_identity = path.name.casefold()
    if request_identity in identities | reserved_identities:
        raise ReleaseSigningError(
            "offline signing request path collides with a release input or output"
        )
    for sibling_relative in (
        payload_relative,
        signature_relative,
        authorization_relative,
        asset_list_relative,
    ):
        if "/" in sibling_relative:
            raise ReleaseSigningError(
                "offline signing request payload and outputs must be beside the request"
            )
    if signature_path.name != f"{payload_path.name}.sig.json":
        raise ReleaseSigningError(
            "offline signing request signature output is not the canonical detached-signature name"
        )
    if authorization_path.name != f"{path.name}.authorization.sig.json":
        raise ReleaseSigningError(
            "offline signing request authorization output is not canonical"
        )
    if release_asset_relatives != sorted(
        release_asset_relatives, key=lambda value: (value.casefold(), value)
    ):
        raise ReleaseSigningError(
            "offline signing request release assets are not canonically ordered"
        )

    portable_basenames: dict[str, str] = {}
    for relative in [
        *release_asset_relatives,
        path.name,
        signature_relative,
        authorization_relative,
    ]:
        basename = relative.rsplit("/", 1)[-1]
        prior = portable_basenames.get(basename.casefold())
        if prior is not None:
            raise ReleaseSigningError(
                "offline signing request contains duplicate publish basenames "
                f"{prior!r} and {basename!r}"
            )
        portable_basenames[basename.casefold()] = basename

    return OfflineSigningRequest(
        request_path=path,
        request_payload=request_payload,
        trust_policy_path=policy_path,
        trust_policy_payload=policy_payload,
        payload_path=payload_path,
        payload=payload,
        signature_path=signature_path,
        authorization_path=authorization_path,
        asset_list_path=asset_list_path,
        release_asset_paths=tuple(release_asset_paths),
        release_asset_snapshots=tuple(release_asset_snapshots),
        trust_policy=trust_policy,
        repo=repo,
        channel=channel,
    )


def _load_expected_release_revocations(
    path: pathlib.Path,
    channel: str,
) -> list[str]:
    document = _load_json_bytes(
        _read_regular_file_bounded(
            path,
            MAX_JSON_DOCUMENT_SIZE,
            "trusted release revocation list",
        ),
        "trusted release revocation list",
    )
    if not isinstance(document, dict):
        raise ReleaseSigningError("trusted release revocation list must be an object")
    _require_exact_keys(
        document,
        {"schema_version", "channels"},
        "trusted release revocation list",
    )
    if document.get("schema_version") != 1 or not isinstance(
        document.get("channels"), dict
    ):
        raise ReleaseSigningError("trusted release revocation list has invalid schema")
    values = document["channels"].get(channel)
    if (
        not isinstance(values, list)
        or len(values) > MAX_REVOKED_RELEASE_INDICES
        or any(
            not isinstance(value, str) or not SHA256_PATTERN.fullmatch(value)
            for value in values
        )
        or len(set(values)) != len(values)
    ):
        raise ReleaseSigningError(
            f"trusted release revocation list has invalid {channel!r} entries"
        )
    return sorted(values)


def validate_candidate_signing_request(
    request: OfflineSigningRequest,
    *,
    artifacts_root: pathlib.Path,
    expected_repo: str,
    expected_channel: str,
    expected_version: str,
    expected_tag: str,
    expected_commit_sha: str,
    expected_release_sequence: int,
    expected_valid_for_days: int,
    revocation_file: pathlib.Path,
    now: dt.datetime | None = None,
) -> None:
    """Reconstruct candidate targets from independently downloaded build metadata."""

    if not 1 <= expected_release_sequence <= (1 << 64) - 1:
        raise ReleaseSigningError("expected release sequence is outside uint64 range")
    if not 1 <= expected_valid_for_days <= MAX_RELEASE_VALIDITY_DAYS:
        raise ReleaseSigningError("expected release validity is outside the supported range")
    index = _load_json_bytes(request.payload, "release index")
    expected_identity = {
        "repo": expected_repo,
        "channel": expected_channel,
        "version": expected_version,
        "tag": expected_tag,
        "commit_sha": expected_commit_sha,
        "release_sequence": expected_release_sequence,
    }
    for field, expected_value in expected_identity.items():
        if index.get(field) != expected_value:
            raise ReleaseSigningError(
                f"release candidate {field} mismatch "
                f"({index.get(field)!r} != {expected_value!r})"
            )

    try:
        generated = dt.datetime.strptime(
            index["generated_at_utc"], "%Y-%m-%dT%H:%M:%SZ"
        ).replace(tzinfo=dt.timezone.utc)
        expires = dt.datetime.strptime(
            index["expires_at_utc"], "%Y-%m-%dT%H:%M:%SZ"
        ).replace(tzinfo=dt.timezone.utc)
    except (KeyError, TypeError, ValueError) as exc:
        raise ReleaseSigningError("release candidate has invalid validity timestamps") from exc
    if expires - generated != dt.timedelta(days=expected_valid_for_days):
        raise ReleaseSigningError(
            "release candidate validity interval does not match the workflow input"
        )
    _require_release_authorization_window(index, now=now)

    revocation_path = _absolute_without_following_links(revocation_file)
    expected_revocations = _load_expected_release_revocations(
        revocation_path,
        expected_channel,
    )
    if index.get("revoked_release_index_sha256s") != expected_revocations:
        raise ReleaseSigningError(
            "release candidate revocations differ from the trusted checkout"
        )

    from tools.release.verify_artifacts import (
        ArtifactLayoutError,
        discover_bundle_metadata_files,
        require_bundle_asset,
    )

    artifacts_root = _absolute_without_following_links(artifacts_root)
    _require_non_link_directory(artifacts_root, "candidate artifacts root")
    try:
        metadata_files = discover_bundle_metadata_files(artifacts_root)
    except ArtifactLayoutError as exc:
        raise ReleaseSigningError(f"invalid candidate metadata layout ({exc})") from exc
    if not metadata_files:
        raise ReleaseSigningError("candidate artifacts contain no platform metadata")

    reconstructed_targets: list[dict[str, Any]] = []
    platform_ids: set[str] = set()
    asset_names: set[str] = set()
    for metadata_path in metadata_files:
        _require_non_link_directory(
            metadata_path.parent,
            "candidate metadata bundle",
        )
        metadata = _load_json_bytes(
            _read_regular_file_bounded(
                metadata_path,
                MAX_RELEASE_INDEX_SIZE,
                "candidate platform metadata",
            ),
            "candidate platform metadata",
        )
        if not isinstance(metadata, dict):
            raise ReleaseSigningError("candidate platform metadata must be an object")
        for field, expected_value in (
            ("repo", expected_repo),
            ("channel", expected_channel),
            ("version", expected_version),
            ("commit_sha", expected_commit_sha),
        ):
            if metadata.get(field) != expected_value:
                raise ReleaseSigningError(
                    f"candidate platform metadata {field} mismatch at {metadata_path}"
                )
        platform_id = metadata.get("platform_id")
        if not isinstance(platform_id, str) or not platform_id:
            raise ReleaseSigningError("candidate platform metadata has invalid platform_id")
        if metadata_path.name != f"metadata-{platform_id}.json":
            raise ReleaseSigningError(
                f"candidate metadata filename does not match platform {platform_id!r}"
            )
        platform_identity = platform_id.casefold()
        if platform_identity in platform_ids:
            raise ReleaseSigningError("candidate metadata repeats a platform")
        platform_ids.add(platform_identity)
        artifacts = metadata.get("artifacts")
        if not isinstance(artifacts, list):
            raise ReleaseSigningError("candidate platform artifacts must be an array")
        files = []
        for artifact_index, artifact in enumerate(artifacts):
            if not isinstance(artifact, dict) or not isinstance(
                artifact.get("name"), str
            ):
                raise ReleaseSigningError(
                    f"candidate platform artifact {artifact_index} is invalid"
                )
            name = artifact["name"]
            name_identity = name.casefold()
            if name_identity in asset_names:
                raise ReleaseSigningError("candidate metadata repeats an asset basename")
            asset_names.add(name_identity)
            try:
                asset_path = require_bundle_asset(
                    artifacts_root,
                    metadata_path.parent,
                    name,
                )
            except (ArtifactLayoutError, FileNotFoundError, OSError) as exc:
                raise ReleaseSigningError(
                    f"candidate platform asset {name!r} is invalid ({exc})"
                ) from exc
            size, digest = _sha256_file(asset_path, f"candidate platform asset {name}")
            files.append(
                {
                    "name": name,
                    "kind": artifact.get("kind", "archive"),
                    "role": artifact.get("role", ""),
                    "path": asset_path.relative_to(artifacts_root).as_posix(),
                    "size": size,
                    "sha256": digest,
                }
            )
        try:
            reconstructed_targets.append(
                {
                    "platform_id": platform_id,
                    "platform_stub": metadata.get("platform_stub", ""),
                    "os": metadata["os"],
                    "arch": metadata["arch"],
                    "archive_format": metadata["archive_format"],
                    "autoupdater": metadata.get("autoupdater", {}),
                    "roles": metadata.get("roles", {}),
                    "files": files,
                }
            )
        except KeyError as exc:
            raise ReleaseSigningError(
                f"candidate platform metadata omits {exc.args[0]}"
            ) from exc
    reconstructed_targets.sort(key=lambda value: value["platform_id"])
    if index.get("targets") != reconstructed_targets:
        raise ReleaseSigningError(
            "release candidate targets do not exactly match independent platform metadata"
        )


def canonical_request_asset_list(
    request: OfflineSigningRequest,
) -> tuple[bytes, tuple[pathlib.Path, ...]]:
    """Return the one canonical, relocatable publish list authorized by a request."""

    paths = (
        *request.release_asset_paths,
        request.request_path,
        request.signature_path,
        request.authorization_path,
    )
    if len(paths) > MAX_PUBLISHED_ASSETS:
        raise ReleaseSigningError(
            f"canonical release asset list exceeds {MAX_PUBLISHED_ASSETS} assets"
        )
    entries = [
        (
            portable_relative_request_path(
                candidate,
                request.asset_list_path.parent,
                f"canonical release asset {index}",
            ),
            candidate,
        )
        for index, candidate in enumerate(paths)
    ]
    entries.sort(key=lambda item: (item[0].casefold(), item[0]))
    _validate_portable_path_inventory(
        ((relative, "canonical release asset") for relative, _ in entries)
    )
    payload = "".join(f"{relative}\n" for relative, _ in entries).encode("ascii")
    if not payload or len(payload) > MAX_CANONICAL_ASSET_LIST_SIZE:
        raise ReleaseSigningError("canonical release asset list exceeds its size limit")
    return payload, tuple(path for _, path in entries)


def _require_same_lexical_path(
    actual: pathlib.Path,
    expected: pathlib.Path,
    description: str,
) -> None:
    actual = _absolute_without_following_links(actual)
    expected = _absolute_without_following_links(expected)
    if os.path.normcase(str(actual)).casefold() != os.path.normcase(
        str(expected)
    ).casefold():
        raise ReleaseSigningError(
            f"{description} does not match the path bound by the signing request"
        )


def verify_publish_authorization(
    *,
    signing_request_path: pathlib.Path,
    signature_path: pathlib.Path,
    authorization_path: pathlib.Path,
    asset_list_path: pathlib.Path,
    trust_policy_path: pathlib.Path,
    expected_repo: str,
    expected_channel: str,
    expected_version: str,
    expected_tag: str,
    expected_commit_sha: str,
    expected_release_sequence: int,
    now: dt.datetime | None = None,
) -> VerifiedPublishAuthorization:
    """Revalidate the complete offline authorization before publication."""

    request = load_offline_signing_request(signing_request_path)
    _require_same_lexical_path(signature_path, request.signature_path, "release signature")
    _require_same_lexical_path(
        authorization_path,
        request.authorization_path,
        "request authorization",
    )
    _require_same_lexical_path(asset_list_path, request.asset_list_path, "release asset list")
    if request.repo != expected_repo or request.channel != expected_channel:
        raise ReleaseSigningError(
            "offline signing request repo/channel does not match publication"
        )

    authoritative_policy_path = _absolute_without_following_links(trust_policy_path)
    authoritative_policy_payload = _read_regular_file_bounded(
        authoritative_policy_path,
        MAX_JSON_DOCUMENT_SIZE,
        "independently frozen release trust policy",
    )
    if authoritative_policy_payload != request.trust_policy_payload:
        raise ReleaseSigningError(
            "candidate trust policy does not exactly match the independently frozen policy"
        )
    authoritative_policy = load_trust_policy_bytes(
        authoritative_policy_payload,
        "independently frozen release trust policy",
    )

    authorization_payload = _read_regular_file_bounded(
        request.authorization_path,
        MAX_SIGNATURE_ENVELOPE_SIZE,
        "signing-request authorization",
    )
    verify_request_authorization_envelope(
        request.request_payload,
        request.request_path.name,
        authorization_payload,
        authoritative_policy,
        expected_repo=expected_repo,
        expected_channel=expected_channel,
    )

    signature_payload = _read_regular_file_bounded(
        request.signature_path,
        MAX_SIGNATURE_ENVELOPE_SIZE,
        "authorized release signature",
    )
    verified = verify_signature_envelope(
        request.payload,
        request.payload_path.name,
        signature_payload,
        authoritative_policy,
        expected_repo=expected_repo,
        expected_channel=expected_channel,
    )
    if (
        verified.index.get("version") != expected_version
        or verified.index.get("tag") != expected_tag
        or verified.index.get("commit_sha") != expected_commit_sha
        or verified.index.get("release_sequence") != expected_release_sequence
    ):
        raise ReleaseSigningError(
            "authorized release index version/tag/commit/sequence does not match publication"
        )
    _require_release_authorization_window(verified.index, now=now)

    expected_asset_list, authorized_paths = canonical_request_asset_list(request)
    actual_asset_list = _read_regular_file_bounded(
        request.asset_list_path,
        MAX_CANONICAL_ASSET_LIST_SIZE,
        "authorized release asset list",
    )
    if actual_asset_list != expected_asset_list:
        raise ReleaseSigningError(
            "release asset list is not the canonical set bound by the signing request"
        )
    snapshots = {
        os.path.normcase(str(asset.path)).casefold(): asset
        for asset in request.release_asset_snapshots
    }
    for control_path, control_payload in (
        (request.request_path, request.request_payload),
        (request.signature_path, signature_payload),
        (request.authorization_path, authorization_payload),
    ):
        lexical_path = _absolute_without_following_links(control_path)
        snapshots[os.path.normcase(str(lexical_path)).casefold()] = AuthorizedPublishAsset(
            path=lexical_path,
            name=lexical_path.name,
            size=len(control_payload),
            digest=hashlib.sha256(control_payload).hexdigest(),
        )
    authorized_assets = []
    for authorized_path in authorized_paths:
        identity = os.path.normcase(
            str(_absolute_without_following_links(authorized_path))
        ).casefold()
        snapshot = snapshots.get(identity)
        if snapshot is None:
            raise ReleaseSigningError(
                f"authorized publish asset lacks a validation snapshot: {authorized_path}"
            )
        authorized_assets.append(snapshot)
    return VerifiedPublishAuthorization(
        request,
        verified,
        tuple(authorized_assets),
    )


def load_signing_key_path_list(path: pathlib.Path) -> list[pathlib.Path]:
    """Load the private path-list emitted by materialize_signing_secrets.py."""

    path = _absolute_without_following_links(path)
    path_status = _regular_file_status(path, "signing key path list")
    if os.name != "nt" and stat.S_IMODE(path_status.st_mode) & 0o077:
        raise ReleaseSigningError(
            f"signing key path list {path} must not be accessible by group/other"
        )
    document = _load_json_file(
        path,
        "signing key path list",
        MAX_SIGNING_KEY_LIST_SIZE,
    )
    if not isinstance(document, dict):
        raise ReleaseSigningError("signing key path list root must be an object")
    _require_exact_keys(
        document,
        {"schema_version", "signing_key_paths"},
        "signing key path list",
    )
    if type(document.get("schema_version")) is not int or document["schema_version"] != 1:
        raise ReleaseSigningError("signing key path list has an unsupported schema_version")
    values = document.get("signing_key_paths")
    if not isinstance(values, list) or not values or len(values) > MAX_POLICY_KEYS:
        raise ReleaseSigningError(
            f"signing key path list must contain 1..{MAX_POLICY_KEYS} paths"
        )

    _require_non_link_directory(path.parent, "signing key path list directory")
    parent_identity = os.path.normcase(str(path.parent)).casefold()
    paths: list[pathlib.Path] = []
    identities: set[str] = set()
    for index, value in enumerate(values):
        if (
            not isinstance(value, str)
            or not value
            or value.strip() != value
            or any(ord(character) < 0x20 or ord(character) == 0x7F for character in value)
        ):
            raise ReleaseSigningError(f"signing key path list entry {index} is invalid")
        candidate = pathlib.Path(value)
        if not candidate.is_absolute():
            raise ReleaseSigningError(
                f"signing key path list entry {index} must be absolute"
            )
        normalized = _absolute_without_following_links(candidate)
        if os.path.normcase(str(candidate)).casefold() != os.path.normcase(
            str(normalized)
        ).casefold():
            raise ReleaseSigningError(
                f"signing key path list entry {index} is not lexically normalized"
            )
        if os.path.normcase(str(normalized.parent)).casefold() != parent_identity:
            raise ReleaseSigningError(
                f"signing key path list entry {index} must be beside the list"
            )
        identity = os.path.normcase(str(normalized)).casefold()
        if identity in identities:
            raise ReleaseSigningError("signing key path list repeats a path")
        identities.add(identity)
        paths.append(normalized)
    return paths


def collect_private_signing_key_paths(
    direct_paths: Iterable[pathlib.Path],
    list_paths: Iterable[pathlib.Path],
) -> list[pathlib.Path]:
    paths = [_absolute_without_following_links(path) for path in direct_paths]
    for list_path in list_paths:
        paths.extend(load_signing_key_path_list(list_path))
    if not paths:
        raise ReleaseSigningError("at least one private signing key is required")
    identities: set[str] = set()
    key_ids: set[str] = set()
    for path in paths:
        identity = os.path.normcase(str(path)).casefold()
        if identity in identities:
            raise ReleaseSigningError(f"duplicate private signing key path {path}")
        identities.add(identity)
        key = load_private_signing_key(path)
        if key.key_id in key_ids:
            raise ReleaseSigningError(f"duplicate private signing key {key.key_id}")
        key_ids.add(key.key_id)
    return paths


def _validate_payload_name(name: Any) -> str:
    if (
        not isinstance(name, str)
        or not name
        or name in {".", ".."}
        or "/" in name
        or "\\" in name
        or len(name) > MAX_PAYLOAD_NAME_BYTES
        or any(ord(character) < 0x20 or ord(character) > 0x7E for character in name)
    ):
        raise ReleaseSigningError(
            f"signed payload name must be a printable ASCII filename of at most {MAX_PAYLOAD_NAME_BYTES} bytes"
        )
    return name


def release_index_signing_input(payload: bytes, payload_name: str) -> bytes:
    payload_name = _validate_payload_name(payload_name)
    if not payload or len(payload) > MAX_RELEASE_INDEX_SIZE:
        raise ReleaseSigningError(
            f"release index must be between 1 and {MAX_RELEASE_INDEX_SIZE} bytes"
        )
    name_bytes = payload_name.encode("ascii")
    media_type_bytes = RELEASE_INDEX_MEDIA_TYPE.encode("ascii")
    return b"".join(
        (
            SIGNING_DOMAIN,
            struct.pack(">H", len(name_bytes)),
            name_bytes,
            struct.pack(">H", len(media_type_bytes)),
            media_type_bytes,
            struct.pack(">Q", len(payload)),
            payload,
        )
    )


def create_signature_envelope(
    payload: bytes,
    payload_name: str,
    signing_keys: Iterable[PrivateSigningKey],
) -> dict[str, Any]:
    keys_by_id: dict[str, PrivateSigningKey] = {}
    for key in signing_keys:
        if key.key_id in keys_by_id:
            raise ReleaseSigningError(f"duplicate signing key {key.key_id}")
        keys_by_id[key.key_id] = key
    if not keys_by_id:
        raise ReleaseSigningError("at least one private signing key is required")
    if len(keys_by_id) > MAX_SIGNATURES:
        raise ReleaseSigningError(
            f"at most {MAX_SIGNATURES} private signing keys are supported"
        )

    signing_input = release_index_signing_input(payload, payload_name)
    Ed25519PrivateKey, _, _ = _require_crypto()
    signatures = []
    for key_id in sorted(keys_by_id):
        key = keys_by_id[key_id]
        private_key = Ed25519PrivateKey.from_private_bytes(key.private_seed)
        actual_public_key = private_key.public_key().public_bytes_raw()
        if actual_public_key != key.public_key or derive_key_id(actual_public_key) != key.key_id:
            raise ReleaseSigningError(f"signing key {key_id} has inconsistent key material")
        signature = private_key.sign(signing_input)
        signatures.append(
            {
                "key_id": key.key_id,
                "algorithm": SIGNATURE_ALGORITHM,
                "signature": _b64url_encode(signature),
            }
        )

    return {
        "schema_version": SIGNATURE_SCHEMA_VERSION,
        "payload": _signature_payload_descriptor(payload, payload_name),
        "signatures": signatures,
    }


def _signature_payload_descriptor(payload: bytes, payload_name: str) -> dict[str, Any]:
    return {
        "name": payload_name,
        "media_type": RELEASE_INDEX_MEDIA_TYPE,
        "size": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def request_authorization_signing_input(
    request_payload: bytes,
    request_name: str,
) -> bytes:
    request_name = _validate_payload_name(request_name)
    if not request_payload or len(request_payload) > MAX_SIGNING_REQUEST_SIZE:
        raise ReleaseSigningError(
            f"signing request must be between 1 and {MAX_SIGNING_REQUEST_SIZE} bytes"
        )
    name_bytes = request_name.encode("ascii")
    media_type_bytes = REQUEST_AUTHORIZATION_MEDIA_TYPE.encode("ascii")
    return b"".join(
        (
            REQUEST_AUTHORIZATION_DOMAIN,
            struct.pack(">H", len(name_bytes)),
            name_bytes,
            struct.pack(">H", len(media_type_bytes)),
            media_type_bytes,
            struct.pack(">Q", len(request_payload)),
            request_payload,
        )
    )


def _request_authorization_descriptor(
    request_payload: bytes,
    request_name: str,
) -> dict[str, Any]:
    return {
        "name": request_name,
        "media_type": REQUEST_AUTHORIZATION_MEDIA_TYPE,
        "size": len(request_payload),
        "sha256": hashlib.sha256(request_payload).hexdigest(),
    }


def create_request_authorization_envelope(
    request_payload: bytes,
    request_name: str,
    signing_keys: Iterable[PrivateSigningKey],
) -> dict[str, Any]:
    keys_by_id: dict[str, PrivateSigningKey] = {}
    for key in signing_keys:
        if key.key_id in keys_by_id:
            raise ReleaseSigningError(f"duplicate request authorization key {key.key_id}")
        keys_by_id[key.key_id] = key
    if not keys_by_id or len(keys_by_id) > MAX_SIGNATURES:
        raise ReleaseSigningError(
            f"request authorization requires 1..{MAX_SIGNATURES} private keys"
        )
    signing_input = request_authorization_signing_input(
        request_payload,
        request_name,
    )
    Ed25519PrivateKey, _, _ = _require_crypto()
    authorizations = []
    for key_id in sorted(keys_by_id):
        key = keys_by_id[key_id]
        private_key = Ed25519PrivateKey.from_private_bytes(key.private_seed)
        actual_public_key = private_key.public_key().public_bytes_raw()
        if actual_public_key != key.public_key or derive_key_id(actual_public_key) != key_id:
            raise ReleaseSigningError(
                f"request authorization key {key_id} has inconsistent key material"
            )
        authorizations.append(
            {
                "key_id": key_id,
                "algorithm": SIGNATURE_ALGORITHM,
                "signature": _b64url_encode(private_key.sign(signing_input)),
            }
        )
    return {
        "schema_version": REQUEST_AUTHORIZATION_SCHEMA_VERSION,
        "request": _request_authorization_descriptor(request_payload, request_name),
        "authorizations": authorizations,
    }


def serialize_request_authorization_envelope(envelope: dict[str, Any]) -> bytes:
    return (json.dumps(envelope, indent=2, sort_keys=True) + "\n").encode("utf-8")


def verify_request_authorization_envelope(
    request_payload: bytes,
    request_name: str,
    envelope_bytes: bytes,
    trust_policy: TrustPolicy,
    *,
    expected_repo: str,
    expected_channel: str,
) -> tuple[str, ...]:
    request_name = _validate_payload_name(request_name)
    signing_input = request_authorization_signing_input(
        request_payload,
        request_name,
    )
    envelope = _load_json_bytes(envelope_bytes, "request authorization envelope")
    if not isinstance(envelope, dict):
        raise ReleaseSigningError("request authorization envelope root must be an object")
    _require_exact_keys(
        envelope,
        {"schema_version", "request", "authorizations"},
        "request authorization envelope",
    )
    if (
        type(envelope.get("schema_version")) is not int
        or envelope["schema_version"] != REQUEST_AUTHORIZATION_SCHEMA_VERSION
    ):
        raise ReleaseSigningError(
            "request authorization envelope has an unsupported schema_version"
        )
    if envelope.get("request") != _request_authorization_descriptor(
        request_payload, request_name
    ):
        raise ReleaseSigningError(
            "request authorization envelope does not describe the exact signing request"
        )
    values = envelope.get("authorizations")
    if not isinstance(values, list) or not values or len(values) > MAX_SIGNATURES:
        raise ReleaseSigningError(
            f"request authorization envelope must contain 1..{MAX_SIGNATURES} authorizations"
        )
    _, Ed25519PublicKey, InvalidSignature = _require_crypto()
    seen: set[str] = set()
    verified: list[str] = []
    for index, value in enumerate(values):
        description = f"request authorization {index}"
        if not isinstance(value, dict):
            raise ReleaseSigningError(f"{description} must be an object")
        _require_exact_keys(
            value,
            {"key_id", "algorithm", "signature"},
            description,
        )
        key_id = value.get("key_id")
        if not isinstance(key_id, str) or not KEY_ID_PATTERN.fullmatch(key_id):
            raise ReleaseSigningError(f"{description} has an invalid key_id")
        if key_id in seen:
            raise ReleaseSigningError(f"request authorization repeats key {key_id}")
        seen.add(key_id)
        if value.get("algorithm") != SIGNATURE_ALGORITHM:
            raise ReleaseSigningError(f"{description} has an unsupported algorithm")
        signature = _b64url_decode(value.get("signature"), 64, f"{description} signature")
        trusted = trust_policy.keys.get(key_id)
        if (
            trusted is None
            or expected_repo not in trusted.repos
            or expected_channel not in trusted.channels
        ):
            continue
        try:
            Ed25519PublicKey.from_public_bytes(trusted.public_key).verify(
                signature,
                signing_input,
            )
        except InvalidSignature:
            continue
        verified.append(key_id)
    if len(verified) < trust_policy.threshold:
        raise ReleaseSigningError(
            "signing-request authorization threshold not met "
            f"({len(verified)} valid authorized signatures; "
            f"{trust_policy.threshold} required)"
        )
    return tuple(sorted(verified))


def aggregate_request_authorization_envelopes(
    request_payload: bytes,
    request_name: str,
    envelope_payloads: Iterable[bytes],
    trust_policy: TrustPolicy,
    *,
    expected_repo: str,
    expected_channel: str,
) -> dict[str, Any]:
    descriptor = _request_authorization_descriptor(request_payload, request_name)
    merged: dict[str, dict[str, Any]] = {}
    count = 0
    for envelope_index, payload in enumerate(envelope_payloads):
        count += 1
        envelope = _load_json_bytes(payload, f"request authorization input {envelope_index}")
        if not isinstance(envelope, dict):
            raise ReleaseSigningError("request authorization input must be an object")
        _require_exact_keys(
            envelope,
            {"schema_version", "request", "authorizations"},
            f"request authorization input {envelope_index}",
        )
        if (
            envelope.get("schema_version") != REQUEST_AUTHORIZATION_SCHEMA_VERSION
            or envelope.get("request") != descriptor
            or not isinstance(envelope.get("authorizations"), list)
            or not envelope["authorizations"]
        ):
            raise ReleaseSigningError(
                f"request authorization input {envelope_index} is not bound to the exact request"
            )
        for authorization in envelope["authorizations"]:
            if not isinstance(authorization, dict):
                raise ReleaseSigningError("request authorization entry must be an object")
            key_id = authorization.get("key_id")
            if not isinstance(key_id, str) or key_id in merged:
                raise ReleaseSigningError(
                    "request authorization inputs contain a duplicate or invalid key"
                )
            trusted = trust_policy.keys.get(key_id)
            if trusted is None:
                raise ReleaseSigningError(
                    f"request authorization key {key_id} is absent from the trust policy"
                )
            single = {
                "schema_version": REQUEST_AUTHORIZATION_SCHEMA_VERSION,
                "request": descriptor,
                "authorizations": [authorization],
            }
            verify_request_authorization_envelope(
                request_payload,
                request_name,
                serialize_request_authorization_envelope(single),
                TrustPolicy(1, {key_id: trusted}),
                expected_repo=expected_repo,
                expected_channel=expected_channel,
            )
            merged[key_id] = dict(authorization)
    if count == 0:
        raise ReleaseSigningError("at least one request authorization input is required")
    aggregate = {
        "schema_version": REQUEST_AUTHORIZATION_SCHEMA_VERSION,
        "request": descriptor,
        "authorizations": [merged[key_id] for key_id in sorted(merged)],
    }
    verify_request_authorization_envelope(
        request_payload,
        request_name,
        serialize_request_authorization_envelope(aggregate),
        trust_policy,
        expected_repo=expected_repo,
        expected_channel=expected_channel,
    )
    return aggregate


def serialize_signed_request_bundle(
    release_index_signature: dict[str, Any],
    request_authorization: dict[str, Any],
) -> bytes:
    return (
        json.dumps(
            {
                "schema_version": SIGNED_REQUEST_BUNDLE_SCHEMA_VERSION,
                "release_index_signature": release_index_signature,
                "request_authorization": request_authorization,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n"
    ).encode("utf-8")


def _load_signed_request_bundle(
    payload: bytes,
    description: str,
) -> tuple[bytes, bytes]:
    document = _load_json_bytes(payload, description)
    if not isinstance(document, dict):
        raise ReleaseSigningError(f"{description} root must be an object")
    _require_exact_keys(
        document,
        {"schema_version", "release_index_signature", "request_authorization"},
        description,
    )
    if document.get("schema_version") != SIGNED_REQUEST_BUNDLE_SCHEMA_VERSION:
        raise ReleaseSigningError(f"{description} has an unsupported schema_version")
    canonical = serialize_signed_request_bundle(
        document["release_index_signature"],
        document["request_authorization"],
    )
    if payload != canonical:
        raise ReleaseSigningError(f"{description} is not canonical JSON")
    return (
        serialize_signature_envelope(document["release_index_signature"]),
        serialize_request_authorization_envelope(document["request_authorization"]),
    )


def validate_release_index_security_metadata(
    index: Any,
    *,
    now: dt.datetime | None = None,
    require_current_freshness: bool = True,
) -> None:
    """Validate the signed release-index contract required by native clients."""

    if not isinstance(index, dict):
        raise ReleaseSigningError("release index root must be an object")
    if type(index.get("schema_version")) is not int or index["schema_version"] != 3:
        raise ReleaseSigningError("release index has an unsupported schema_version")
    for field in ("repo", "channel", "version", "tag", "commit_sha"):
        value = index.get(field)
        if not isinstance(value, str) or not value:
            raise ReleaseSigningError(f"release index has invalid {field}")
    if not isinstance(index.get("targets"), list):
        raise ReleaseSigningError("release index targets must be an array")

    sequence = index.get("release_sequence")
    if type(sequence) is not int or not (1 <= sequence <= (1 << 64) - 1):
        raise ReleaseSigningError("release index has invalid release_sequence")

    def parse_timestamp(field: str) -> dt.datetime:
        value = index.get(field)
        if not isinstance(value, str):
            raise ReleaseSigningError(f"release index has invalid {field}")
        try:
            parsed = dt.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ").replace(
                tzinfo=dt.timezone.utc
            )
        except ValueError as exc:
            raise ReleaseSigningError(
                f"release index has invalid {field}"
            ) from exc
        if parsed.strftime("%Y-%m-%dT%H:%M:%SZ") != value:
            raise ReleaseSigningError(f"release index has non-canonical {field}")
        return parsed

    generated = parse_timestamp("generated_at_utc")
    expires = parse_timestamp("expires_at_utc")
    validity = expires - generated
    if validity <= dt.timedelta(0) or validity > dt.timedelta(
        days=MAX_RELEASE_VALIDITY_DAYS
    ):
        raise ReleaseSigningError("release index validity interval is invalid")
    if require_current_freshness:
        current = now or dt.datetime.now(dt.timezone.utc)
        if current.tzinfo is None:
            current = current.replace(tzinfo=dt.timezone.utc)
        current = current.astimezone(dt.timezone.utc)
        if generated - current > MAX_RELEASE_CLOCK_SKEW:
            raise ReleaseSigningError("release index was generated too far in the future")
        if current - expires > MAX_RELEASE_CLOCK_SKEW:
            raise ReleaseSigningError("release index has expired")

    revoked = index.get("revoked_release_index_sha256s")
    if (
        not isinstance(revoked, list)
        or len(revoked) > MAX_REVOKED_RELEASE_INDICES
        or any(
            not isinstance(value, str) or not SHA256_PATTERN.fullmatch(value)
            for value in revoked
        )
        or len(set(revoked)) != len(revoked)
    ):
        raise ReleaseSigningError("release index has invalid revocation metadata")


def serialize_signature_envelope(envelope: dict[str, Any]) -> bytes:
    return (json.dumps(envelope, indent=2, sort_keys=True) + "\n").encode("utf-8")


def aggregate_signature_envelopes(
    payload: bytes,
    payload_name: str,
    envelope_payloads: Iterable[bytes],
    trust_policy: TrustPolicy,
    *,
    expected_repo: str | None = None,
    expected_channel: str | None = None,
) -> dict[str, Any]:
    """Combine independently produced envelopes after validating every signature.

    The aggregator needs only the public trust policy. Every input signature must
    be authorized and valid for the exact payload, and the merged result must
    satisfy the policy threshold.
    """

    payload_name = _validate_payload_name(payload_name)
    release_index_signing_input(payload, payload_name)
    expected_descriptor = _signature_payload_descriptor(payload, payload_name)
    merged_signatures: dict[str, dict[str, Any]] = {}
    envelope_count = 0

    for envelope_index, envelope_bytes in enumerate(envelope_payloads):
        envelope_count += 1
        description = f"signature envelope input {envelope_index}"
        if not envelope_bytes or len(envelope_bytes) > MAX_SIGNATURE_ENVELOPE_SIZE:
            raise ReleaseSigningError(
                f"{description} must be between 1 and {MAX_SIGNATURE_ENVELOPE_SIZE} bytes"
            )
        envelope = _load_json_bytes(envelope_bytes, description)
        if not isinstance(envelope, dict):
            raise ReleaseSigningError(f"{description} root must be an object")
        _require_exact_keys(
            envelope,
            {"schema_version", "payload", "signatures"},
            description,
        )
        if (
            type(envelope.get("schema_version")) is not int
            or envelope["schema_version"] != SIGNATURE_SCHEMA_VERSION
        ):
            raise ReleaseSigningError(f"{description} has an unsupported schema_version")
        if envelope.get("payload") != expected_descriptor:
            raise ReleaseSigningError(
                f"{description} does not describe the exact release index payload"
            )

        signatures = envelope.get("signatures")
        if (
            not isinstance(signatures, list)
            or not signatures
            or len(signatures) > MAX_SIGNATURES
        ):
            raise ReleaseSigningError(
                f"{description} signatures must contain 1..{MAX_SIGNATURES} entries"
            )

        for signature_index, signature in enumerate(signatures):
            signature_description = (
                f"{description} signature {signature_index}"
            )
            if not isinstance(signature, dict):
                raise ReleaseSigningError(
                    f"{signature_description} must be an object"
                )
            _require_exact_keys(
                signature,
                {"key_id", "algorithm", "signature"},
                signature_description,
            )
            key_id = signature.get("key_id")
            if not isinstance(key_id, str) or not KEY_ID_PATTERN.fullmatch(key_id):
                raise ReleaseSigningError(
                    f"{signature_description} has an invalid key_id"
                )
            if key_id in merged_signatures:
                raise ReleaseSigningError(
                    f"signature envelope inputs repeat key {key_id}"
                )
            trusted_key = trust_policy.keys.get(key_id)
            if trusted_key is None:
                raise ReleaseSigningError(
                    f"{signature_description} is not present in the trust policy"
                )

            single_envelope = {
                "schema_version": SIGNATURE_SCHEMA_VERSION,
                "payload": expected_descriptor,
                "signatures": [signature],
            }
            try:
                verify_signature_envelope(
                    payload,
                    payload_name,
                    serialize_signature_envelope(single_envelope),
                    TrustPolicy(1, {key_id: trusted_key}),
                    expected_repo=expected_repo,
                    expected_channel=expected_channel,
                )
            except ReleaseSigningError as exc:
                raise ReleaseSigningError(
                    f"{signature_description} is not a valid authorized signature ({exc})"
                ) from exc
            merged_signatures[key_id] = dict(signature)
            if len(merged_signatures) > MAX_SIGNATURES:
                raise ReleaseSigningError(
                    f"at most {MAX_SIGNATURES} aggregate signatures are supported"
                )

    if envelope_count == 0:
        raise ReleaseSigningError("at least one signature envelope input is required")

    aggregate = {
        "schema_version": SIGNATURE_SCHEMA_VERSION,
        "payload": expected_descriptor,
        "signatures": [merged_signatures[key_id] for key_id in sorted(merged_signatures)],
    }
    serialized = serialize_signature_envelope(aggregate)
    verify_signature_envelope(
        payload,
        payload_name,
        serialized,
        trust_policy,
        expected_repo=expected_repo,
        expected_channel=expected_channel,
    )
    return aggregate


def verify_signature_envelope(
    payload: bytes,
    payload_name: str,
    envelope_bytes: bytes,
    trust_policy: TrustPolicy,
    *,
    expected_repo: str | None = None,
    expected_channel: str | None = None,
) -> VerifiedReleaseIndex:
    payload_name = _validate_payload_name(payload_name)
    if not payload or len(payload) > MAX_RELEASE_INDEX_SIZE:
        raise ReleaseSigningError(
            f"release index must be between 1 and {MAX_RELEASE_INDEX_SIZE} bytes"
        )
    if not envelope_bytes or len(envelope_bytes) > MAX_SIGNATURE_ENVELOPE_SIZE:
        raise ReleaseSigningError(
            f"release signature envelope must be between 1 and {MAX_SIGNATURE_ENVELOPE_SIZE} bytes"
        )
    envelope = _load_json_bytes(envelope_bytes, "release signature envelope")
    if not isinstance(envelope, dict):
        raise ReleaseSigningError("release signature envelope root must be an object")
    _require_exact_keys(envelope, {"schema_version", "payload", "signatures"}, "signature envelope")
    if (
        type(envelope.get("schema_version")) is not int
        or envelope["schema_version"] != SIGNATURE_SCHEMA_VERSION
    ):
        raise ReleaseSigningError("release signature envelope has an unsupported schema_version")

    descriptor = envelope.get("payload")
    if not isinstance(descriptor, dict):
        raise ReleaseSigningError("release signature envelope payload must be an object")
    _require_exact_keys(
        descriptor, {"name", "media_type", "size", "sha256"}, "signature payload descriptor"
    )
    if descriptor.get("name") != payload_name:
        raise ReleaseSigningError("release signature envelope payload name mismatch")
    if descriptor.get("media_type") != RELEASE_INDEX_MEDIA_TYPE:
        raise ReleaseSigningError("release signature envelope media type mismatch")
    if type(descriptor.get("size")) is not int or descriptor["size"] != len(payload):
        raise ReleaseSigningError("release signature envelope payload size mismatch")
    payload_sha256 = descriptor.get("sha256")
    if not isinstance(payload_sha256, str) or not SHA256_PATTERN.fullmatch(payload_sha256):
        raise ReleaseSigningError("release signature envelope has an invalid payload sha256")
    if payload_sha256 != hashlib.sha256(payload).hexdigest():
        raise ReleaseSigningError("release signature envelope payload sha256 mismatch")

    index = _load_json_bytes(payload, "release index")
    validate_release_index_security_metadata(index)
    repo = index.get("repo")
    channel = index.get("channel")
    if not isinstance(repo, str) or not repo or not isinstance(channel, str) or not channel:
        raise ReleaseSigningError("release index has invalid repo/channel identity")
    if expected_repo is not None and repo != expected_repo:
        raise ReleaseSigningError(f"release index repo mismatch ({repo!r} != {expected_repo!r})")
    if expected_channel is not None and channel != expected_channel:
        raise ReleaseSigningError(
            f"release index channel mismatch ({channel!r} != {expected_channel!r})"
        )

    signatures = envelope.get("signatures")
    if (
        not isinstance(signatures, list)
        or not signatures
        or len(signatures) > MAX_SIGNATURES
    ):
        raise ReleaseSigningError(
            f"release signature envelope signatures must contain 1..{MAX_SIGNATURES} entries"
        )
    signing_input = release_index_signing_input(payload, payload_name)
    _, Ed25519PublicKey, InvalidSignature = _require_crypto()
    seen_key_ids: set[str] = set()
    verified: list[str] = []
    for index_value, value in enumerate(signatures):
        description = f"release signature {index_value}"
        if not isinstance(value, dict):
            raise ReleaseSigningError(f"{description} must be an object")
        _require_exact_keys(value, {"key_id", "algorithm", "signature"}, description)
        key_id = value.get("key_id")
        if not isinstance(key_id, str) or not KEY_ID_PATTERN.fullmatch(key_id):
            raise ReleaseSigningError(f"{description} has an invalid key_id")
        if key_id in seen_key_ids:
            raise ReleaseSigningError(f"release signature envelope repeats key {key_id}")
        seen_key_ids.add(key_id)
        if value.get("algorithm") != SIGNATURE_ALGORITHM:
            raise ReleaseSigningError(f"{description} has an unsupported algorithm")
        signature = _b64url_decode(value.get("signature"), 64, f"{description} signature")
        trusted_key = trust_policy.keys.get(key_id)
        if trusted_key is None or repo not in trusted_key.repos or channel not in trusted_key.channels:
            continue
        try:
            Ed25519PublicKey.from_public_bytes(trusted_key.public_key).verify(
                signature, signing_input
            )
        except InvalidSignature:
            continue
        verified.append(key_id)

    if len(verified) < trust_policy.threshold:
        raise ReleaseSigningError(
            "release index signature threshold not met "
            f"({len(verified)} valid authorized signatures; {trust_policy.threshold} required)"
        )
    return VerifiedReleaseIndex(payload, index, tuple(sorted(verified)))


def _atomic_write(path: pathlib.Path, payload: bytes, *, private: bool = False) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if private:
        descriptor = -1
        created = False
        try:
            descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            created = True
            with os.fdopen(descriptor, "wb") as handle:
                descriptor = -1
                handle.write(payload)
                handle.flush()
                os.fsync(handle.fileno())
        except FileExistsError as exc:
            raise ReleaseSigningError(f"refusing to overwrite private key {path}") from exc
        except BaseException:
            if descriptor >= 0:
                os.close(descriptor)
            if created:
                path.unlink(missing_ok=True)
            raise
        return
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary_path = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            descriptor = -1
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary_path, path)
    except BaseException:
        if descriptor >= 0:
            os.close(descriptor)
        temporary_path.unlink(missing_ok=True)
        raise


def _atomic_write_set(outputs: list[tuple[pathlib.Path, bytes]]) -> None:
    """Publish a validated output set together, restoring prior files on error."""

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
                "signing output publication failed and rollback was incomplete; "
                + "; ".join(rollback_failures)
            ) from failure
        raise
    finally:
        for _, temporary_path in staged:
            temporary_path.unlink(missing_ok=True)
        if succeeded:
            for backup_path in backups.values():
                backup_path.unlink(missing_ok=True)


def write_signature_envelope(
    payload_path: pathlib.Path,
    signature_path: pathlib.Path,
    signing_key_paths: Iterable[pathlib.Path],
) -> None:
    payload = _read_regular_file_bounded(
        payload_path, MAX_RELEASE_INDEX_SIZE, "release-index payload"
    )
    validate_release_index_security_metadata(
        _load_json_bytes(payload, "release index")
    )
    keys = [load_private_signing_key(path) for path in signing_key_paths]
    envelope = create_signature_envelope(payload, payload_path.name, keys)
    _atomic_write(signature_path, serialize_signature_envelope(envelope))


def _command_generate_key(args: argparse.Namespace) -> int:
    if not args.repo or not args.channel:
        raise ReleaseSigningError("key generation requires at least one --repo and --channel")
    private_key_path = _absolute_without_following_links(args.private_key_path)
    trust_policy_path = _absolute_without_following_links(args.trust_policy_path)
    _validate_command_path_contract(
        [],
        [
            (private_key_path, "private-key output"),
            (trust_policy_path, "trust-policy output"),
        ],
        require_missing_outputs=True,
    )
    private_document = generate_private_key_document()
    policy = {
        "schema_version": TRUST_POLICY_SCHEMA_VERSION,
        "threshold": 1,
        "keys": [
            trust_key_document(private_document, repos=args.repo, channels=args.channel)
        ],
    }
    _atomic_write(
        private_key_path,
        (json.dumps(private_document, indent=2, sort_keys=True) + "\n").encode("utf-8"),
        private=True,
    )
    _atomic_write(
        trust_policy_path,
        (json.dumps(policy, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    return 0


def _command_sign(args: argparse.Namespace) -> int:
    payload_path = _absolute_without_following_links(args.payload)
    signature_path = _absolute_without_following_links(args.signature)
    signing_key_paths = [
        _absolute_without_following_links(path) for path in args.signing_key_file
    ]
    _validate_command_path_contract(
        [(payload_path, "release-index payload")]
        + [(path, "private signing key") for path in signing_key_paths],
        [(signature_path, "signature output")],
    )
    write_signature_envelope(
        payload_path,
        signature_path,
        signing_key_paths,
    )
    return 0


def _validate_candidate_from_args(
    request: OfflineSigningRequest,
    args: argparse.Namespace,
) -> None:
    validate_candidate_signing_request(
        request,
        artifacts_root=pathlib.Path(args.artifacts_root),
        expected_repo=args.repo,
        expected_channel=args.channel,
        expected_version=args.version,
        expected_tag=args.tag,
        expected_commit_sha=args.commit_sha,
        expected_release_sequence=args.release_sequence,
        expected_valid_for_days=args.valid_for_days,
        revocation_file=pathlib.Path(args.revocation_file),
    )


def _command_verify_candidate(args: argparse.Namespace) -> int:
    request = load_offline_signing_request(
        _absolute_without_following_links(args.signing_request)
    )
    _validate_candidate_from_args(request, args)
    print(
        "Verified keyless release candidate against independent metadata and "
        f"workflow identity for {args.tag}"
    )
    return 0


def _command_sign_request(args: argparse.Namespace) -> int:
    request_path = _absolute_without_following_links(args.signing_request)
    request = load_offline_signing_request(request_path)
    _validate_candidate_from_args(request, args)
    signature_path = _absolute_without_following_links(args.signature)
    key_list_paths = [
        _absolute_without_following_links(path) for path in args.signing_key_list
    ]
    signing_key_paths = collect_private_signing_key_paths(
        (pathlib.Path(path) for path in args.signing_key_file),
        key_list_paths,
    )
    _validate_command_path_contract(
        [
            (request.request_path, "offline signing request"),
            (request.payload_path, "release-index payload"),
            (request.trust_policy_path, "release trust policy"),
            *((path, "release asset") for path in request.release_asset_paths),
            *((path, "signing key path list") for path in key_list_paths),
            *((path, "private signing key") for path in signing_key_paths),
        ],
        [(signature_path, "partial signature output")],
        require_missing_outputs=True,
    )

    keys = [load_private_signing_key(path) for path in signing_key_paths]
    for key in keys:
        trusted = request.trust_policy.keys.get(key.key_id)
        if (
            trusted is None
            or request.repo not in trusted.repos
            or request.channel not in trusted.channels
        ):
            raise ReleaseSigningError(
                f"private signing key {key.key_id} is not authorized for "
                f"{request.repo}/{request.channel}"
            )
    envelope = create_signature_envelope(
        request.payload,
        request.payload_path.name,
        keys,
    )
    request_authorization = create_request_authorization_envelope(
        request.request_payload,
        request.request_path.name,
        keys,
    )
    _atomic_write(
        signature_path,
        serialize_signed_request_bundle(envelope, request_authorization),
    )
    print(
        f"Created {len(keys)} scoped offline signature(s) for "
        f"{request.payload_path.name}"
    )
    return 0


def _command_aggregate(args: argparse.Namespace) -> int:
    request = None
    manual_values = (
        args.payload,
        args.output_signature,
        args.trust_policy,
        args.repo,
        args.channel,
    )
    if args.signing_request:
        if any(value is not None for value in manual_values):
            raise ReleaseSigningError(
                "--signing-request cannot be combined with manual payload, output, policy, repo, or channel options"
            )
        request = load_offline_signing_request(
            _absolute_without_following_links(args.signing_request)
        )
        payload_path = request.payload_path
        payload = request.payload
        output_path = request.signature_path
        trust_policy = request.trust_policy
        repo = request.repo
        channel = request.channel
        for pending_path, description in (
            (request.signature_path, "aggregate signature output"),
            (request.authorization_path, "request authorization output"),
            (request.asset_list_path, "publish-ready asset list"),
        ):
            if os.path.lexists(pending_path):
                raise ReleaseSigningError(
                    f"refusing to replace existing {description} {pending_path}"
                )
    else:
        missing = [
            option
            for option, value in (
                ("--payload", args.payload),
                ("--output-signature", args.output_signature),
                ("--trust-policy", args.trust_policy),
                ("--repo", args.repo),
                ("--channel", args.channel),
            )
            if value is None
        ]
        if missing:
            raise ReleaseSigningError(
                "manual aggregation requires " + ", ".join(missing)
            )
        payload_path = _absolute_without_following_links(args.payload)
        output_path = _absolute_without_following_links(args.output_signature)
        trust_policy_path = _absolute_without_following_links(args.trust_policy)
        _validate_command_path_contract(
            [
                (payload_path, "release-index payload"),
                (trust_policy_path, "release trust policy"),
            ],
            [(output_path, "aggregate signature output")],
        )
        payload = _read_regular_file_bounded(
            payload_path, MAX_RELEASE_INDEX_SIZE, "release-index payload"
        )
        trust_policy = load_trust_policy(trust_policy_path)
        repo = args.repo
        channel = args.channel

    signature_paths = [
        _absolute_without_following_links(path) for path in args.signature
    ]
    aggregate_outputs = [(output_path, "aggregate signature output")]
    if request is not None:
        aggregate_outputs.extend(
            [
                (request.authorization_path, "request authorization output"),
                (request.asset_list_path, "publish-ready asset list"),
            ]
        )
    _validate_command_path_contract(
        [(path, "signature envelope input") for path in signature_paths],
        aggregate_outputs,
    )
    input_payloads = [
        _read_regular_file_bounded(
            path, MAX_SIGNATURE_ENVELOPE_SIZE, "signature envelope input"
        )
        for path in signature_paths
    ]
    if request is not None:
        split_payloads = [
            _load_signed_request_bundle(payload_value, f"signed request input {index}")
            for index, payload_value in enumerate(input_payloads)
        ]
        index_signature_payloads = [value[0] for value in split_payloads]
        authorization_payloads = [value[1] for value in split_payloads]
    else:
        index_signature_payloads = input_payloads
    aggregate = aggregate_signature_envelopes(
        payload,
        payload_path.name,
        index_signature_payloads,
        trust_policy,
        expected_repo=repo,
        expected_channel=channel,
    )
    aggregate_payload = serialize_signature_envelope(aggregate)
    if request is not None:
        authorization = aggregate_request_authorization_envelopes(
            request.request_payload,
            request.request_path.name,
            authorization_payloads,
            trust_policy,
            expected_repo=repo,
            expected_channel=channel,
        )
        authorization_payload = serialize_request_authorization_envelope(
            authorization
        )
        asset_list_payload, _ = canonical_request_asset_list(request)
        _atomic_write_set(
            [
                (output_path, aggregate_payload),
                (request.authorization_path, authorization_payload),
                (request.asset_list_path, asset_list_payload),
            ]
        )
    else:
        _atomic_write(output_path, aggregate_payload)
    print(
        f"Aggregated {len(aggregate['signatures'])} authorized Ed25519 "
        f"signature(s) into {output_path}"
    )
    if request is not None:
        print(
            "Threshold signing is complete; wrote publish-ready asset list "
            f"{request.asset_list_path}"
        )
    return 0


def _command_verify_publish(args: argparse.Namespace) -> int:
    result = verify_publish_authorization(
        signing_request_path=pathlib.Path(args.signing_request),
        signature_path=pathlib.Path(args.signature),
        authorization_path=pathlib.Path(args.authorization),
        asset_list_path=pathlib.Path(args.asset_list),
        trust_policy_path=pathlib.Path(args.trust_policy),
        expected_repo=args.repo,
        expected_channel=args.channel,
        expected_version=args.version,
        expected_tag=args.tag,
        expected_commit_sha=args.commit_sha,
        expected_release_sequence=args.release_sequence,
    )
    print(
        "Verified publish authorization for "
        f"{args.tag}: {len(result.verified_index.verified_key_ids)} signature(s), "
        f"{len(result.authorized_asset_paths)} canonical assets"
    )
    return 0


def _command_verify(args: argparse.Namespace) -> int:
    payload_path = _absolute_without_following_links(args.payload)
    signature_path = _absolute_without_following_links(args.signature)
    trust_policy_path = _absolute_without_following_links(args.trust_policy)
    _validate_command_path_contract(
        [
            (payload_path, "release-index payload"),
            (signature_path, "signature envelope"),
            (trust_policy_path, "release trust policy"),
        ],
        [],
    )
    result = verify_signature_envelope(
        _read_regular_file_bounded(
            payload_path, MAX_RELEASE_INDEX_SIZE, "release-index payload"
        ),
        payload_path.name,
        _read_regular_file_bounded(
            signature_path, MAX_SIGNATURE_ENVELOPE_SIZE, "signature envelope"
        ),
        load_trust_policy(trust_policy_path),
        expected_repo=args.repo,
        expected_channel=args.channel,
    )
    print(
        f"Verified {payload_path} with {len(result.verified_key_ids)} authorized Ed25519 signature(s)"
    )
    return 0


def _add_candidate_validation_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--signing-request", required=True)
    parser.add_argument("--artifacts-root", required=True)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--channel", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit-sha", required=True)
    parser.add_argument("--release-sequence", required=True, type=int)
    parser.add_argument("--valid-for-days", required=True, type=int)
    parser.add_argument("--revocation-file", required=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Sign and verify WORR release indices.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    generate_parser = subparsers.add_parser("generate-key", help="Generate an offline Ed25519 key")
    generate_parser.add_argument("--private-key-path", required=True)
    generate_parser.add_argument("--trust-policy-path", required=True)
    generate_parser.add_argument("--repo", action="append", default=[])
    generate_parser.add_argument("--channel", action="append", default=[])
    generate_parser.set_defaults(handler=_command_generate_key)

    sign_parser = subparsers.add_parser("sign", help="Create a detached signature envelope")
    sign_parser.add_argument("--payload", required=True)
    sign_parser.add_argument("--signature", required=True)
    sign_parser.add_argument("--signing-key-file", action="append", required=True)
    sign_parser.set_defaults(handler=_command_sign)

    sign_request_parser = subparsers.add_parser(
        "sign-request",
        help="Sign the payload and policy identity bound by an offline request",
    )
    _add_candidate_validation_arguments(sign_request_parser)
    sign_request_parser.add_argument("--signature", required=True)
    sign_request_parser.add_argument("--signing-key-file", action="append", default=[])
    sign_request_parser.add_argument("--signing-key-list", action="append", default=[])
    sign_request_parser.set_defaults(handler=_command_sign_request)

    verify_candidate_parser = subparsers.add_parser(
        "verify-candidate",
        help="Reconstruct and validate a keyless candidate before key materialization",
    )
    _add_candidate_validation_arguments(verify_candidate_parser)
    verify_candidate_parser.set_defaults(handler=_command_verify_candidate)

    aggregate_parser = subparsers.add_parser(
        "aggregate",
        aliases=["merge"],
        help="Merge independently produced signature envelopes",
    )
    aggregate_parser.add_argument("--payload")
    aggregate_parser.add_argument(
        "--signature",
        action="append",
        required=True,
        help="Partial signature envelope (repeat for each custodian)",
    )
    aggregate_parser.add_argument("--output-signature")
    aggregate_parser.add_argument("--trust-policy")
    aggregate_parser.add_argument("--repo")
    aggregate_parser.add_argument("--channel")
    aggregate_parser.add_argument(
        "--signing-request",
        help=(
            "Deferred signing request; derives payload, policy, outputs, and "
            "release identity and writes the publish-ready asset list only "
            "after threshold verification"
        ),
    )
    aggregate_parser.set_defaults(handler=_command_aggregate)

    verify_parser = subparsers.add_parser("verify", help="Verify a detached signature envelope")
    verify_parser.add_argument("--payload", required=True)
    verify_parser.add_argument("--signature", required=True)
    verify_parser.add_argument("--trust-policy", required=True)
    verify_parser.add_argument("--repo")
    verify_parser.add_argument("--channel")
    verify_parser.set_defaults(handler=_command_verify)

    verify_publish_parser = subparsers.add_parser(
        "verify-publish",
        help="Verify a complete deferred authorization and canonical publish set",
    )
    verify_publish_parser.add_argument("--signing-request", required=True)
    verify_publish_parser.add_argument("--signature", required=True)
    verify_publish_parser.add_argument("--authorization", required=True)
    verify_publish_parser.add_argument("--asset-list", required=True)
    verify_publish_parser.add_argument("--trust-policy", required=True)
    verify_publish_parser.add_argument("--repo", required=True)
    verify_publish_parser.add_argument("--channel", required=True)
    verify_publish_parser.add_argument("--version", required=True)
    verify_publish_parser.add_argument("--tag", required=True)
    verify_publish_parser.add_argument("--commit-sha", required=True)
    verify_publish_parser.add_argument("--release-sequence", required=True, type=int)
    verify_publish_parser.set_defaults(handler=_command_verify_publish)

    args = parser.parse_args()
    try:
        return args.handler(args)
    except (OSError, ReleaseSigningError) as exc:
        print(f"Release signing failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
