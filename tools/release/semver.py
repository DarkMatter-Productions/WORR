from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class SemverIdentifier:
    numeric: bool
    value: int | str


@dataclass(frozen=True)
class SemverVersion:
    major: int
    minor: int
    patch: int
    prerelease: tuple[SemverIdentifier, ...] = field(default_factory=tuple)


def _parse_identifier(text: str) -> SemverIdentifier:
    if not text:
        raise ValueError("Empty prerelease identifier")
    if text.isdigit():
        return SemverIdentifier(True, int(text))
    return SemverIdentifier(False, text)


def parse_semver(raw: str) -> SemverVersion:
    value = raw.strip()
    if not value:
        raise ValueError("Empty version")
    if value[:1] in {"v", "V"}:
        value = value[1:]

    value = value.split("+", 1)[0]
    if "-" in value:
        core, prerelease_text = value.split("-", 1)
    else:
        core, prerelease_text = value, ""

    parts = core.split(".")
    if len(parts) != 3 or not all(part.isdigit() for part in parts):
        raise ValueError(f"Invalid semver core: {raw}")

    prerelease: list[SemverIdentifier] = []
    if prerelease_text:
        prerelease = [_parse_identifier(part) for part in prerelease_text.split(".")]

    return SemverVersion(
        major=int(parts[0]),
        minor=int(parts[1]),
        patch=int(parts[2]),
        prerelease=tuple(prerelease),
    )


def compare_versions(lhs: str, rhs: str) -> int:
    left = parse_semver(lhs)
    right = parse_semver(rhs)

    left_core = (left.major, left.minor, left.patch)
    right_core = (right.major, right.minor, right.patch)
    if left_core != right_core:
        return -1 if left_core < right_core else 1

    if not left.prerelease and not right.prerelease:
        return 0
    if not left.prerelease:
        return 1
    if not right.prerelease:
        return -1

    for left_id, right_id in zip(left.prerelease, right.prerelease):
        if left_id.numeric and right_id.numeric:
            if left_id.value != right_id.value:
                return -1 if left_id.value < right_id.value else 1
            continue
        if left_id.numeric != right_id.numeric:
            return -1 if left_id.numeric else 1
        if left_id.value != right_id.value:
            return -1 if left_id.value < right_id.value else 1

    if len(left.prerelease) == len(right.prerelease):
        return 0
    return -1 if len(left.prerelease) < len(right.prerelease) else 1
