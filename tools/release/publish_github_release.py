#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
from dataclasses import dataclass
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from tools.release.release_signing import (
    ReleaseSigningError,
    VerifiedPublishAuthorization,
    verify_publish_authorization,
)


MAX_ASSETS = 1000
MAX_ASSET_LIST_BYTES = 16 * 1024 * 1024
GH_API_TIMEOUT_SECONDS = 120
GH_UPLOAD_TIMEOUT_SECONDS = 30 * 60
MAX_TAG_PEEL_DEPTH = 8
REPO_PATTERN = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
TAG_PATTERN = re.compile(r"[A-Za-z0-9._-]+")
COMMIT_SHA_PATTERN = re.compile(r"[0-9a-f]{40}")
CHANNEL_PUBLICATION_SEMANTICS = {
    "stable": (False, True),
    "nightly": (True, False),
}


@dataclass(frozen=True)
class LocalAsset:
    path: pathlib.Path
    name: str
    size: int
    digest: str


def validate_channel_publication_semantics(
    channel: str,
    *,
    prerelease: bool,
    make_latest: bool,
) -> None:
    expected = CHANNEL_PUBLICATION_SEMANTICS.get(channel)
    if expected is None:
        raise ValueError(f"unsupported signed release channel: {channel!r}")
    if (prerelease, make_latest) != expected:
        expected_prerelease, expected_latest = expected
        raise ValueError(
            f"signed {channel} channel requires prerelease={expected_prerelease} "
            f"and make_latest={expected_latest}"
        )


def sha256_file(path: pathlib.Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def load_local_assets(asset_list_path: pathlib.Path) -> dict[str, LocalAsset]:
    asset_list_path = pathlib.Path(os.path.abspath(asset_list_path))
    if asset_list_path.is_symlink():
        raise ValueError("release asset list must not be a symbolic link")
    asset_list_status = asset_list_path.stat()
    if not asset_list_path.is_file() or asset_list_status.st_nlink != 1:
        raise ValueError("release asset list must be a single-link regular file")
    if asset_list_status.st_size > MAX_ASSET_LIST_BYTES:
        raise ValueError("release asset list exceeds 16 MiB")
    lines = asset_list_path.read_text(encoding="utf-8", errors="strict").splitlines()
    if not lines or len(lines) > MAX_ASSETS:
        raise ValueError("release asset list has an unsupported number of entries")

    candidates = []
    for line in lines:
        candidate = pathlib.Path(line)
        if not candidate.is_absolute():
            candidate = asset_list_path.parent / candidate
        candidates.append(candidate)
    assets: dict[str, LocalAsset] = {}
    asset_names: dict[str, str] = {}
    for candidate in candidates:
        if not str(candidate) or "\n" in str(candidate) or "\r" in str(candidate):
            raise ValueError("release asset list contains an invalid path")
        if candidate.is_symlink():
            raise ValueError(f"release asset is a symbolic link: {candidate}")
        resolved = candidate.resolve(strict=True)
        status = resolved.stat()
        if not resolved.is_file() or status.st_nlink != 1:
            raise ValueError(f"release asset is not a single-link regular file: {candidate}")
        name = resolved.name
        name_key = name.casefold()
        if name_key in asset_names:
            raise ValueError(
                "duplicate release asset basename under portable comparison: "
                f"{asset_names[name_key]} and {name}"
            )
        asset_names[name_key] = name
        assets[name] = LocalAsset(
            path=resolved,
            name=name,
            size=status.st_size,
            digest=sha256_file(resolved),
        )
    return assets


def validate_remote_assets(
    expected: dict[str, LocalAsset], remote: list[dict[str, Any]]
) -> None:
    actual: dict[str, dict[str, Any]] = {}
    for item in remote:
        if not isinstance(item, dict) or not isinstance(item.get("name"), str):
            raise ValueError("GitHub returned an invalid release asset")
        name = item["name"]
        if name in actual:
            raise ValueError(f"GitHub release has duplicate asset {name}")
        actual[name] = item
    if set(actual) != set(expected):
        missing = sorted(set(expected) - set(actual))
        unexpected = sorted(set(actual) - set(expected))
        details = []
        if missing:
            details.append("missing " + ", ".join(missing))
        if unexpected:
            details.append("unexpected " + ", ".join(unexpected))
        raise ValueError("GitHub release asset set mismatch (" + "; ".join(details) + ")")
    for name, local in expected.items():
        item = actual[name]
        if item.get("state") != "uploaded":
            raise ValueError(f"GitHub release asset {name} is not fully uploaded")
        if type(item.get("size")) is not int or item["size"] != local.size:
            raise ValueError(f"GitHub release asset {name} has the wrong size")
        if item.get("digest") != f"sha256:{local.digest}":
            raise ValueError(f"GitHub release asset {name} has the wrong SHA-256")


def validate_cached_assets_against_authorization(
    cached: dict[str, LocalAsset],
    authorization: VerifiedPublishAuthorization,
) -> None:
    expected = {asset.name: asset for asset in authorization.authorized_assets}
    if set(cached) != set(expected):
        raise ValueError(
            "cached release assets differ from the authorized publish snapshot"
        )
    for name, local in cached.items():
        authorized = expected[name]
        if (
            os.path.normcase(str(local.path)).casefold()
            != os.path.normcase(str(authorized.path)).casefold()
            or local.size != authorized.size
            or local.digest != authorized.digest
        ):
            raise ValueError(
                f"cached release asset {name} changed across authorization"
            )


def run_gh(
    arguments: list[str], *, input_document: dict[str, Any] | None = None,
    allow_not_found: bool = False
) -> Any:
    command = ["gh", *arguments]
    payload = None
    if input_document is not None:
        command.extend(["--input", "-"])
        payload = json.dumps(input_document, separators=(",", ":"))
    process = subprocess.run(
        command,
        input=payload,
        text=True,
        encoding="utf-8",
        capture_output=True,
        check=False,
        timeout=GH_API_TIMEOUT_SECONDS,
    )
    if process.returncode != 0:
        if allow_not_found and "HTTP 404" in process.stderr:
            return None
        raise RuntimeError(
            f"GitHub CLI failed ({' '.join(command)}): {process.stderr.strip()}"
        )
    if not process.stdout.strip():
        return None
    return json.loads(process.stdout)


def list_release_assets(repo: str, release_id: int) -> list[dict[str, Any]]:
    pages = run_gh(
        [
            "api",
            "--paginate",
            "--slurp",
            f"repos/{repo}/releases/{release_id}/assets?per_page=100",
        ]
    )
    if not isinstance(pages, list) or any(not isinstance(page, list) for page in pages):
        raise ValueError("GitHub returned an invalid paginated asset list")
    return [item for page in pages for item in page]


def validate_release_document(
    release: Any,
    *,
    tag: str,
    expected_commit: str,
    require_draft: bool | None = None,
) -> int:
    if (
        not isinstance(release, dict)
        or type(release.get("id")) is not int
        or release.get("tag_name") != tag
        or type(release.get("draft")) is not bool
    ):
        raise ValueError("GitHub returned an invalid release document")
    if release.get("target_commitish") != expected_commit:
        raise ValueError(
            f"GitHub release target differs from expected commit {expected_commit}"
        )
    if require_draft is not None and release["draft"] is not require_draft:
        state = "draft" if require_draft else "published"
        raise ValueError(f"GitHub release is not {state}")
    return release["id"]


def validate_release_metadata(
    release: Any,
    *,
    tag: str,
    expected_commit: str,
    name: str,
    body: str,
    prerelease: bool,
    require_draft: bool,
) -> int:
    release_id = validate_release_document(
        release,
        tag=tag,
        expected_commit=expected_commit,
        require_draft=require_draft,
    )
    if (
        release.get("name") != name
        or release.get("body") != body
        or release.get("prerelease") is not prerelease
    ):
        raise ValueError("GitHub release metadata differs from the exact request")
    return release_id


def validate_latest_semantics(
    repo: str,
    release_id: int,
    *,
    make_latest: bool,
) -> None:
    latest = run_gh(
        ["api", f"repos/{repo}/releases/latest"],
        allow_not_found=True,
    )
    if latest is not None and (
        not isinstance(latest, dict) or type(latest.get("id")) is not int
    ):
        raise ValueError("GitHub returned an invalid latest-release document")
    is_latest = latest is not None and latest["id"] == release_id
    if is_latest is not make_latest:
        expected = "latest" if make_latest else "not latest"
        raise ValueError(f"GitHub release does not have the requested {expected} state")


def _validated_git_object(document: Any, description: str) -> tuple[str, str]:
    if not isinstance(document, dict) or not isinstance(document.get("object"), dict):
        raise ValueError(f"GitHub returned an invalid {description}")
    target = document["object"]
    object_type = target.get("type")
    sha = target.get("sha")
    if object_type not in {"commit", "tag"} or not isinstance(
        sha, str
    ) or not COMMIT_SHA_PATTERN.fullmatch(sha):
        raise ValueError(f"GitHub returned an invalid {description} target")
    return object_type, sha


def resolve_tag_commit(repo: str, tag: str) -> str | None:
    reference = run_gh(
        ["api", f"repos/{repo}/git/ref/tags/{tag}"],
        allow_not_found=True,
    )
    if reference is None:
        return None
    if not isinstance(reference, dict) or reference.get("ref") != f"refs/tags/{tag}":
        raise ValueError("GitHub returned an invalid release tag reference")
    object_type, sha = _validated_git_object(reference, "release tag reference")
    visited: set[str] = set()
    for _ in range(MAX_TAG_PEEL_DEPTH):
        if object_type == "commit":
            return sha
        if sha in visited:
            raise ValueError("GitHub release tag chain contains a cycle")
        visited.add(sha)
        tag_object = run_gh(["api", f"repos/{repo}/git/tags/{sha}"])
        object_type, sha = _validated_git_object(
            tag_object,
            "annotated release tag",
        )
    if object_type == "commit":
        return sha
    raise ValueError(
        f"GitHub release tag exceeds the {MAX_TAG_PEEL_DEPTH}-object peel limit"
    )


def require_tag_commit(
    repo: str,
    tag: str,
    expected_commit: str,
    *,
    allow_missing: bool,
) -> None:
    actual_commit = resolve_tag_commit(repo, tag)
    if actual_commit is None:
        if allow_missing:
            return
        raise ValueError("GitHub release tag is missing")
    if actual_commit != expected_commit:
        raise ValueError(
            f"GitHub release tag points to {actual_commit}, not {expected_commit}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Publish an exact, digest-verified GitHub release through a draft gate"
    )
    parser.add_argument("--repo", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--expected-version", required=True)
    parser.add_argument("--expected-release-sequence", required=True, type=int)
    parser.add_argument("--body-path", required=True)
    parser.add_argument("--asset-list-path", required=True)
    parser.add_argument("--signing-request", required=True)
    parser.add_argument("--signature", required=True)
    parser.add_argument("--authorization", required=True)
    parser.add_argument("--trust-policy", required=True)
    parser.add_argument("--channel", required=True)
    parser.add_argument("--prerelease", action="store_true")
    parser.add_argument("--make-latest", action="store_true")
    args = parser.parse_args()

    if not REPO_PATTERN.fullmatch(args.repo) or not TAG_PATTERN.fullmatch(args.tag):
        raise SystemExit("Invalid GitHub repository or release tag")
    if not re.fullmatch(r"[0-9a-f]{40}", args.expected_commit):
        raise SystemExit("Expected commit must be a lowercase 40-digit Git SHA")
    asset_list_path = pathlib.Path(args.asset_list_path)
    body_path = pathlib.Path(args.body_path).resolve(strict=True)
    body = body_path.read_text(encoding="utf-8", errors="strict")
    try:
        assets = load_local_assets(asset_list_path)
        authorization = verify_publish_authorization(
            signing_request_path=pathlib.Path(args.signing_request),
            signature_path=pathlib.Path(args.signature),
            authorization_path=pathlib.Path(args.authorization),
            asset_list_path=asset_list_path,
            trust_policy_path=pathlib.Path(args.trust_policy),
            expected_repo=args.repo,
            expected_channel=args.channel,
            expected_version=args.expected_version,
            expected_tag=args.tag,
            expected_commit_sha=args.expected_commit,
            expected_release_sequence=args.expected_release_sequence,
        )
        validate_cached_assets_against_authorization(assets, authorization)
        validate_channel_publication_semantics(
            authorization.verified_index.index["channel"],
            prerelease=args.prerelease,
            make_latest=args.make_latest,
        )
    except (OSError, UnicodeDecodeError, ValueError, ReleaseSigningError) as exc:
        raise SystemExit(f"Invalid release publication authorization: {exc}") from exc

    if not os.environ.get("GH_TOKEN"):
        raise SystemExit("GH_TOKEN is required")

    release_endpoint = f"repos/{args.repo}/releases/tags/{args.tag}"
    try:
        require_tag_commit(
            args.repo,
            args.tag,
            args.expected_commit,
            allow_missing=True,
        )
        release = run_gh(["api", release_endpoint], allow_not_found=True)
        if release is not None and not isinstance(release, dict):
            raise ValueError("GitHub returned an invalid release document")
        if release is not None and release.get("draft") is False:
            release_id = validate_release_metadata(
                release,
                tag=args.tag,
                expected_commit=args.expected_commit,
                name=args.name,
                body=body,
                prerelease=args.prerelease,
                require_draft=False,
            )
            require_tag_commit(
                args.repo,
                args.tag,
                args.expected_commit,
                allow_missing=False,
            )
            validate_remote_assets(assets, list_release_assets(args.repo, release_id))
            validate_latest_semantics(
                args.repo,
                release_id,
                make_latest=args.make_latest,
            )
            print(f"Published release {args.tag} already matches exactly")
            return 0

        release_payload = {
            "tag_name": args.tag,
            "target_commitish": args.expected_commit,
            "name": args.name,
            "body": body,
            "draft": True,
            "prerelease": args.prerelease,
            "make_latest": "true" if args.make_latest else "false",
        }
        if release is None:
            release = run_gh(
                ["api", "--method", "POST", f"repos/{args.repo}/releases"],
                input_document=release_payload,
            )
        release_id = validate_release_document(
            release,
            tag=args.tag,
            expected_commit=args.expected_commit,
            require_draft=True,
        )
        require_tag_commit(
            args.repo,
            args.tag,
            args.expected_commit,
            allow_missing=False,
        )

        # A failed prior attempt can leave only a draft. Clear every old asset
        # first so the next upload starts from a known-empty asset set.
        for item in list_release_assets(args.repo, release_id):
            if type(item.get("id")) is not int:
                raise ValueError("GitHub returned an invalid release asset id")
            run_gh(
                [
                    "api",
                    "--method",
                    "DELETE",
                    f"repos/{args.repo}/releases/assets/{item['id']}",
                ]
            )

        upload = subprocess.run(
            [
                "gh",
                "release",
                "upload",
                args.tag,
                *[str(asset.path) for asset in assets.values()],
                "--repo",
                args.repo,
                "--clobber",
            ],
            text=True,
            encoding="utf-8",
            capture_output=True,
            check=False,
            timeout=GH_UPLOAD_TIMEOUT_SECONDS,
        )
        if upload.returncode != 0:
            raise RuntimeError(f"GitHub release upload failed: {upload.stderr.strip()}")

        validate_remote_assets(assets, list_release_assets(args.repo, release_id))
        published = run_gh(
            [
                "api",
                "--method",
                "PATCH",
                f"repos/{args.repo}/releases/{release_id}",
            ],
            input_document={
                "name": args.name,
                "body": body,
                "draft": False,
                "prerelease": args.prerelease,
                "make_latest": "true" if args.make_latest else "false",
            },
        )
        validate_release_metadata(
            published,
            tag=args.tag,
            expected_commit=args.expected_commit,
            name=args.name,
            body=body,
            prerelease=args.prerelease,
            require_draft=False,
        )
        final_release = run_gh(["api", release_endpoint])
        final_release_id = validate_release_metadata(
            final_release,
            tag=args.tag,
            expected_commit=args.expected_commit,
            name=args.name,
            body=body,
            prerelease=args.prerelease,
            require_draft=False,
        )
        if final_release_id != release_id:
            raise ValueError("GitHub final release identity changed after publication")
        require_tag_commit(
            args.repo,
            args.tag,
            args.expected_commit,
            allow_missing=False,
        )
        validate_remote_assets(assets, list_release_assets(args.repo, release_id))
        validate_latest_semantics(
            args.repo,
            release_id,
            make_latest=args.make_latest,
        )
    except (
        OSError,
        json.JSONDecodeError,
        RuntimeError,
        subprocess.TimeoutExpired,
        ValueError,
    ) as exc:
        raise SystemExit(f"GitHub release publication failed closed: {exc}") from exc

    print(f"Published exact release {args.tag} with {len(assets)} verified assets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
