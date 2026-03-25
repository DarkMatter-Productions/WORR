from __future__ import annotations


class ReleaseIndexError(ValueError):
    pass


def resolve_role_payload(index: dict, platform_stub: str, role: str) -> dict:
    targets = index.get("targets")
    if not isinstance(targets, list):
        raise ReleaseIndexError("Release index is missing targets[]")

    selected = None
    for target in targets:
        if not isinstance(target, dict):
            continue
        if target.get("platform_stub") == platform_stub:
            selected = target
            break

    if selected is None:
        raise ReleaseIndexError(f"Release index does not contain platform {platform_stub}")

    roles = selected.get("roles")
    if not isinstance(roles, dict):
        raise ReleaseIndexError(f"Release index does not contain roles for {platform_stub}")

    payload = roles.get(role)
    if not isinstance(payload, dict):
        raise ReleaseIndexError(f"Release index does not contain role metadata for {role}")

    normalized = dict(payload)
    normalized["launch_exe"] = payload.get("launch_exe") or payload.get("launcher_exe")
    normalized["engine_library"] = payload.get("engine_library") or payload.get("runtime_exe")

    required = (
        "role",
        "launch_exe",
        "engine_library",
        "update_manifest_name",
        "update_package_name",
        "local_manifest_name",
    )
    missing = [key for key in required if not normalized.get(key)]
    if missing:
        raise ReleaseIndexError(
            f"Release index payload for {platform_stub}/{role} is missing: {', '.join(missing)}"
        )

    return normalized
