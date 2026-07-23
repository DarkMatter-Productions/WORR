#!/usr/bin/env python3
"""Production wiring contract for local-action authority mailbox failures."""

from __future__ import annotations

import argparse
from pathlib import Path


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening_brace = source.index("{", start)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function body for {signature}")


def require_order(source: str, *needles: str) -> None:
    positions = [source.index(needle) for needle in needles]
    if positions != sorted(positions) or len(set(positions)) != len(positions):
        raise AssertionError(f"required order not preserved: {needles}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    args = parser.parse_args()
    root = args.repo_root.resolve()

    authority = (
        root / "src/server/local_action_shadow_authority.c"
    ).read_text(encoding="utf-8")
    authority_header = (
        root / "inc/server/local_action_shadow_authority.h"
    ).read_text(encoding="utf-8")
    game = (root / "src/server/game.c").read_text(encoding="utf-8")
    user = (root / "src/server/user.c").read_text(encoding="utf-8")
    native = (root / "src/server/native_shadow.c").read_text(
        encoding="utf-8"
    )
    native_header = (root / "inc/server/native_shadow.h").read_text(
        encoding="utf-8"
    )
    sgame = (
        root / "src/game/sgame/network/local_action_observation.cpp"
    ).read_text(encoding="utf-8")

    # The exact server-owned callback exported to sgame must be the bounded
    # mailbox publisher, and sgame must retain and invoke that callback.
    extension = function_body(
        game, "static void *PF_GetExtension(const char *name)\n{"
    )
    require_order(
        extension,
        "WORR_LOCAL_ACTION_SHADOW_AUTHORITY_IMPORT_V1",
        "SV_LocalActionShadowAuthorityImportV1()",
    )
    assert "publish_receipt," in authority[
        authority.index("static const worr_local_action_shadow_authority_import_v1") :
    ]

    initialize = function_body(
        sgame, "void SG_LocalActionObservationInitialize()"
    )
    require_order(
        initialize,
        "gi.GetExtension(WORR_LOCAL_ACTION_SHADOW_AUTHORITY_IMPORT_V1)",
        "valid_shadow_authority_import(shadow_authority_candidate)",
        "shadow_authority_import = shadow_authority_candidate;",
    )
    publication = function_body(
        sgame,
        "SG_LocalActionObservationLeasedAdvanceScope::\n    ~SG_LocalActionObservationLeasedAdvanceScope()",
    )
    require_order(
        publication,
        "Worr_LocalActionShadowAuthorityReceiptBuildV1(&shadow, &receipt)",
        "shadow_authority_import->PublishReceipt(client_index_, &receipt)",
    )

    # Full mailbox pressure is a latched failure, not an ignored publication
    # result or a test-only retry.  Taking the notification leaves the mailbox
    # poisoned until its normal client/map reset.
    publish = function_body(authority, "static bool publish_receipt(")
    capacity = publish[publish.index("if (free_index < 0) {") :]
    require_order(
        capacity,
        "if (free_index < 0)",
        "SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_CAPACITY",
        "return false;",
    )
    latch = function_body(authority, "static void latch_failure(")
    require_order(
        latch,
        "failures[client_index] = failure;",
        "failed[client_index] = 1;",
    )
    take = function_body(
        authority, "bool SV_LocalActionShadowAuthorityTakeFailure("
    )
    require_order(
        take,
        "*failure_out = failures[client_index];",
        "failures[client_index] =\n"
        "        SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_NONE;",
    )
    assert "failed[client_index] = 0" not in take
    assert "SV_LOCAL_ACTION_SHADOW_AUTHORITY_FAILURE_CAPACITY = 5" in (
        authority_header
    )

    # SV_ClientThink consumes the latch synchronously after the game callback
    # that can publish it.  A native peer is disabled with the dedicated code.
    think = function_body(user, "static inline void SV_ClientThink(")
    require_order(
        think,
        "ge->ClientThink(sv_player, cmd);",
        "SV_WorrConsumeLocalActionShadowAuthorityFailure();",
    )
    consume = function_body(
        user, "static void SV_WorrConsumeLocalActionShadowAuthorityFailure("
    )
    require_order(
        consume,
        "SV_LocalActionShadowAuthorityTakeFailure(",
        "if (sv_client->worr_native_shadow)",
        "SV_NativeShadowPeerDisableV1(",
        "SV_NATIVE_SHADOW_FAILURE_LOCAL_ACTION_AUTHORITY",
    )
    assert "SV_NATIVE_SHADOW_FAILURE_LOCAL_ACTION_AUTHORITY = 18" in (
        native_header
    )

    # Once native wire is committed, the exact disable function enters DRAIN,
    # records failure 18, disables readiness, and exposes both fields through
    # the ordinary production status API.
    disable = function_body(native, "void SV_NativeShadowPeerDisableV1(")
    committed = disable[disable.index("if (peer->native_wire_committed") :]
    require_order(
        committed,
        "peer->native_wire_committed",
        "enter_drain(peer, failure, true);",
        "return;",
    )
    drain = function_body(native, "static void enter_drain(")
    require_order(
        drain,
        "peer->lifecycle = SV_NATIVE_SHADOW_LIFECYCLE_DRAIN;",
        "peer->last_failure = (uint32_t)failure;",
        "peer->enabled = 0;",
    )
    status = function_body(native, "bool SV_NativeShadowGetStatusV1(")
    require_order(
        status,
        "status.lifecycle = peer->lifecycle;",
        "status.last_failure = peer->last_failure;",
        "*status_out = status;",
    )

    print(
        "server_local_action_authority_failure_source_contract "
        "sgame=published mailbox=fail-closed client_think=consumes "
        "native=drain failure=18 status=observable"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
