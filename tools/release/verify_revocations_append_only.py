#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import pathlib
import re
from typing import Any


MAX_REVOCATION_LIST_BYTES = 1024 * 1024
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")


def load_document(path: pathlib.Path) -> dict[str, set[str]]:
    payload = path.read_bytes()
    if not payload or len(payload) > MAX_REVOCATION_LIST_BYTES:
        raise ValueError(f"{path} has an unsupported size")

    def reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"{path} contains duplicate field {key!r}")
            result[key] = value
        return result

    def reject_constant(value: str) -> None:
        raise ValueError(f"{path} contains non-finite number {value}")

    try:
        document = json.loads(
            payload.decode("utf-8", errors="strict"),
            object_pairs_hook=reject_duplicates,
            parse_constant=reject_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"{path} is not strict UTF-8 JSON ({exc})") from exc
    if (
        not isinstance(document, dict)
        or set(document) != {"schema_version", "channels"}
        or type(document.get("schema_version")) is not int
        or document["schema_version"] != 1
        or not isinstance(document.get("channels"), dict)
    ):
        raise ValueError(f"{path} has an invalid revocation schema")

    result: dict[str, set[str]] = {}
    for channel, values in document["channels"].items():
        if (
            not isinstance(channel, str)
            or not channel
            or not isinstance(values, list)
            or any(not isinstance(value, str) or not SHA256_PATTERN.fullmatch(value) for value in values)
            or len(set(values)) != len(values)
        ):
            raise ValueError(f"{path} has an invalid {channel!r} channel")
        result[channel] = set(values)
    return result


def require_append_only(
    previous: dict[str, set[str]], current: dict[str, set[str]]
) -> None:
    removed_channels = sorted(previous.keys() - current.keys())
    if removed_channels:
        raise ValueError(
            "revocation channels were removed: " + ", ".join(removed_channels)
        )
    for channel, previous_values in previous.items():
        removed = sorted(previous_values - current[channel])
        if removed:
            raise ValueError(
                f"revocations were removed from {channel}: " + ", ".join(removed)
            )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Require a release-index revocation registry to grow append-only"
    )
    parser.add_argument("--previous", required=True)
    parser.add_argument("--current", required=True)
    args = parser.parse_args()

    previous_path = pathlib.Path(args.previous)
    current_path = pathlib.Path(args.current)
    try:
        require_append_only(load_document(previous_path), load_document(current_path))
    except (OSError, ValueError) as exc:
        raise SystemExit(f"Release revocation append-only check failed: {exc}") from exc
    print("Release revocation registry is append-only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
