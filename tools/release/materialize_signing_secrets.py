#!/usr/bin/env python3

from __future__ import annotations

import argparse
import base64
import binascii
import json
import os
import pathlib
import shutil
import stat
import sys
import tempfile
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from tools.package_release import load_release_trust_policy_document
from tools.release.release_signing import ReleaseSigningError, load_private_signing_key


SIGNING_KEY_BUNDLE_SCHEMA_VERSION = 1
SIGNING_KEY_LIST_SCHEMA_VERSION = 1
MAX_SIGNING_SECRET_BYTES = 1024 * 1024
PRIVATE_KEY_DOCUMENT_FIELDS = {
    "schema_version",
    "algorithm",
    "key_id",
    "public_key",
    "private_key",
}
SIGNING_KEY_BUNDLE_FIELDS = {"schema_version", "keys"}


def decode_required_secret(environment_name: str) -> bytes:
    encoded = os.environ.get(environment_name, "")
    if not encoded:
        raise SystemExit(
            f"Required GitHub Actions secret {environment_name} is not configured"
        )
    try:
        payload = base64.b64decode(encoded, validate=True)
    except (ValueError, binascii.Error) as exc:
        raise SystemExit(
            f"Required GitHub Actions secret {environment_name} is not canonical base64"
        ) from exc
    if not payload:
        raise SystemExit(f"Required GitHub Actions secret {environment_name} is empty")
    if len(payload) > MAX_SIGNING_SECRET_BYTES:
        raise SystemExit(
            f"Required GitHub Actions secret {environment_name} exceeds {MAX_SIGNING_SECRET_BYTES} bytes"
        )
    if base64.b64encode(payload).decode("ascii") != encoded:
        raise SystemExit(
            f"Required GitHub Actions secret {environment_name} is not canonical base64"
        )
    return payload


def write_private_temp_file(directory: pathlib.Path, name: str, payload: bytes) -> pathlib.Path:
    path = directory / name
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            descriptor = -1
            if os.name != "nt":
                os.fchmod(handle.fileno(), 0o600)
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    path_status = path.lstat()
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    if (
        not stat.S_ISREG(path_status.st_mode)
        or stat.S_ISLNK(path_status.st_mode)
        or getattr(path_status, "st_file_attributes", 0) & reparse_flag
        or path_status.st_nlink != 1
    ):
        path.unlink(missing_ok=True)
        raise SystemExit(f"Refusing unsafe materialized signing-secret file: {path}")
    return path


def read_regular_file(path: pathlib.Path, description: str) -> bytes:
    try:
        path_status = path.lstat()
    except OSError as exc:
        raise SystemExit(f"Cannot inspect {description} {path} ({exc})") from exc
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    if (
        not stat.S_ISREG(path_status.st_mode)
        or stat.S_ISLNK(path_status.st_mode)
        or getattr(path_status, "st_file_attributes", 0) & reparse_flag
        or path_status.st_nlink != 1
    ):
        raise SystemExit(f"{description} {path} must be a regular non-link file")
    if path_status.st_size <= 0 or path_status.st_size > MAX_SIGNING_SECRET_BYTES:
        raise SystemExit(f"{description} {path} has an unsupported size")
    try:
        return path.read_bytes()
    except OSError as exc:
        raise SystemExit(f"Cannot read {description} {path} ({exc})") from exc


def _reject_duplicate_json_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise ReleaseSigningError(f"duplicate JSON key {key!r}")
        value[key] = item
    return value


def _reject_json_constant(value: str) -> None:
    raise ReleaseSigningError(f"non-finite JSON number {value!r} is not allowed")


def load_private_key_documents(payload: bytes) -> list[dict[str, Any]]:
    try:
        document = json.loads(
            payload.decode("utf-8", errors="strict"),
            object_pairs_hook=_reject_duplicate_json_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ReleaseSigningError) as exc:
        raise ReleaseSigningError(
            f"release signing key secret is not strict UTF-8 JSON ({exc})"
        ) from exc

    if not isinstance(document, dict):
        raise ReleaseSigningError("release signing key secret root must be an object")

    fields = set(document)
    if fields == PRIVATE_KEY_DOCUMENT_FIELDS:
        return [document]
    if fields != SIGNING_KEY_BUNDLE_FIELDS:
        missing = sorted(SIGNING_KEY_BUNDLE_FIELDS - fields)
        unexpected = sorted(fields - SIGNING_KEY_BUNDLE_FIELDS)
        details = []
        if missing:
            details.append(f"missing {', '.join(missing)}")
        if unexpected:
            details.append(f"unexpected {', '.join(unexpected)}")
        raise ReleaseSigningError(
            "release signing key secret is neither a private key document nor a "
            f"key bundle ({'; '.join(details)})"
        )
    if (
        type(document.get("schema_version")) is not int
        or document["schema_version"] != SIGNING_KEY_BUNDLE_SCHEMA_VERSION
    ):
        raise ReleaseSigningError(
            "release signing key bundle has an unsupported schema_version"
        )
    keys = document.get("keys")
    if not isinstance(keys, list) or not keys:
        raise ReleaseSigningError(
            "release signing key bundle keys must be a non-empty array"
        )
    for index, key_document in enumerate(keys):
        if not isinstance(key_document, dict):
            raise ReleaseSigningError(
                f"release signing key bundle key {index} must be an object"
            )
        actual_fields = set(key_document)
        if actual_fields != PRIVATE_KEY_DOCUMENT_FIELDS:
            missing = sorted(PRIVATE_KEY_DOCUMENT_FIELDS - actual_fields)
            unexpected = sorted(actual_fields - PRIVATE_KEY_DOCUMENT_FIELDS)
            details = []
            if missing:
                details.append(f"missing {', '.join(missing)}")
            if unexpected:
                details.append(f"unexpected {', '.join(unexpected)}")
            raise ReleaseSigningError(
                f"release signing key bundle key {index} has invalid fields "
                f"({'; '.join(details)})"
            )
    return keys


def serialize_private_document(document: dict[str, Any]) -> bytes:
    return (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")


def serialize_signing_key_list(paths: list[pathlib.Path]) -> bytes:
    document = {
        "schema_version": SIGNING_KEY_LIST_SCHEMA_VERSION,
        "signing_key_paths": [str(path.absolute()) for path in paths],
    }
    return (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")


def append_github_environment(path: pathlib.Path, values: dict[str, pathlib.Path]) -> None:
    with path.open("a", encoding="utf-8", newline="\n") as handle:
        for name, value in values.items():
            rendered = str(value.resolve())
            if "\n" in rendered or "\r" in rendered:
                raise SystemExit("Refusing to emit a multiline signing-secret path")
            handle.write(f"{name}={rendered}\n")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Materialize base64 CI release-signing secrets into private runner files."
    )
    trust_policy_source = parser.add_mutually_exclusive_group(required=True)
    trust_policy_source.add_argument("--trust-policy-env")
    trust_policy_source.add_argument("--trust-policy-file")
    parser.add_argument("--signing-key-env")
    parser.add_argument("--repo", required=True)
    parser.add_argument("--channel", required=True)
    parser.add_argument(
        "--github-env",
        default=os.environ.get("GITHUB_ENV", ""),
        help="GitHub Actions environment output file",
    )
    parser.add_argument(
        "--temp-root",
        default=os.environ.get("RUNNER_TEMP", ""),
        help="GitHub Actions private temporary root",
    )
    args = parser.parse_args()

    if not args.github_env:
        raise SystemExit("GITHUB_ENV is required")
    if not args.temp_root:
        raise SystemExit("RUNNER_TEMP is required")

    temp_root = pathlib.Path(args.temp_root).resolve()
    if not temp_root.is_dir():
        raise SystemExit(f"Runner temporary root not found: {temp_root}")
    secret_dir = pathlib.Path(
        tempfile.mkdtemp(prefix="worr-release-signing-", dir=temp_root)
    )
    try:
        if os.name != "nt":
            secret_dir.chmod(0o700)
        trust_policy_payload = (
            decode_required_secret(args.trust_policy_env)
            if args.trust_policy_env
            else read_regular_file(
                pathlib.Path(args.trust_policy_file).resolve(),
                "release trust policy",
            )
        )
        trust_policy_path = write_private_temp_file(
            secret_dir,
            "release-trust-policy.json",
            trust_policy_payload,
        )
        trust_policy_document = load_release_trust_policy_document(
            trust_policy_path,
            expected_repo=args.repo,
            expected_channel=args.channel,
        )

        environment = {
            "WORR_RELEASE_SIGNING_SECRET_DIR": secret_dir,
            "WORR_RELEASE_TRUST_POLICY_PATH": trust_policy_path,
        }
        if args.signing_key_env:
            try:
                private_documents = load_private_key_documents(
                    decode_required_secret(args.signing_key_env)
                )
                signing_key_paths = []
                signing_keys = []
                for index, private_document in enumerate(private_documents, start=1):
                    signing_key_path = write_private_temp_file(
                        secret_dir,
                        f"release-signing-key-{index:03d}.json",
                        serialize_private_document(private_document),
                    )
                    signing_key_paths.append(signing_key_path)
                    signing_keys.append(load_private_signing_key(signing_key_path))
            except (OSError, ReleaseSigningError) as exc:
                raise SystemExit(f"Invalid release signing key: {exc}") from exc
            authorized_key_ids = {
                key["key_id"]
                for key in trust_policy_document["keys"]
                if args.repo in key["repos"] and args.channel in key["channels"]
            }
            signing_key_ids = [key.key_id for key in signing_keys]
            if len(set(signing_key_ids)) != len(signing_key_ids):
                raise SystemExit(
                    "Invalid release signing key bundle: duplicate signing key identity"
                )
            unauthorized_key_ids = sorted(set(signing_key_ids) - authorized_key_ids)
            if unauthorized_key_ids:
                raise SystemExit(
                    "Invalid release signing key bundle: signing key is not authorized "
                    f"for {args.repo!r} channel {args.channel!r}: "
                    f"{unauthorized_key_ids[0]}"
                )
            threshold = trust_policy_document["threshold"]
            if len(signing_key_ids) < threshold:
                raise SystemExit(
                    "Invalid release signing key bundle: scoped trust-policy threshold "
                    f"requires {threshold} distinct keys, but only {len(signing_key_ids)} "
                    "were provided"
                )
            signing_key_list_path = write_private_temp_file(
                secret_dir,
                "release-signing-key-paths.json",
                serialize_signing_key_list(signing_key_paths),
            )
            environment["WORR_RELEASE_SIGNING_KEY_LIST_PATH"] = signing_key_list_path
            if len(signing_key_paths) == 1:
                environment["WORR_RELEASE_SIGNING_KEY_PATH"] = signing_key_paths[0]

        append_github_environment(pathlib.Path(args.github_env), environment)
    except BaseException:
        shutil.rmtree(secret_dir)
        raise

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
