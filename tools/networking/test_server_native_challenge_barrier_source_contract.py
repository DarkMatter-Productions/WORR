#!/usr/bin/env python3
"""Production ordering contract for the server native CHALLENGE barrier."""

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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    args = parser.parse_args()
    root = args.repo_root.resolve()

    user = (root / "src/server/user.c").read_text(encoding="utf-8")
    send = (root / "src/server/send.c").read_text(encoding="utf-8")
    init = (root / "src/server/init.c").read_text(encoding="utf-8")
    server_h = (root / "src/server/server.h").read_text(encoding="utf-8")
    native_h = (root / "inc/server/native_shadow.h").read_text(
        encoding="utf-8"
    )

    begin = function_body(user, "void SV_Begin_f(void)")
    begin_game = begin.index("ge->ClientBegin(sv_player);")
    announce = begin.index("AC_ClientAnnounce(sv_client);")
    barrier = begin.index(
        "sv_client->worr_native_shadow_challenge_barrier_bytes ="
    )
    requested = begin.index(
        "sv_client->worr_native_shadow_challenge_requested_at ="
    )
    pending = begin.index(
        "sv_client->worr_native_shadow_challenge_pending = true;"
    )
    assert begin_game < announce < barrier < requested < pending

    service = function_body(user, "SV_ServiceNativeShadowChallenge(")
    maintain = service.index("SV_MaintainNativeShadowChallengePending(")
    generation_idle = service.index(
        "SV_NativeShadowReliableGenerationIdleV1("
    )
    barrier_read = service.index(
        "client->worr_native_shadow_challenge_barrier_bytes;"
    )
    prefix_branch = service.index("if (barrier_bytes != 0)")
    prefix_transmit = service.index(
        "Netchan_TransmitQueuedReliablePrefix(", prefix_branch
    )
    barrier_clear = service.index(
        "client->worr_native_shadow_challenge_barrier_bytes = 0;",
        prefix_transmit,
    )
    begin_epoch = service.index("SV_NativeShadowBeginEpochBoundV1(")
    assert (
        maintain
        < generation_idle
        < barrier_read
        < prefix_branch
        < prefix_transmit
        < barrier_clear
        < begin_epoch
    )
    assert "SV_NativeShadowPostBootstrapQueueIdleV1(" not in service

    challenge_transmit = service.index(
        "Netchan_TransmitIsolatedReliable(", begin_epoch
    )
    cleared_request = service.index(
        "SV_ClearNativeShadowChallengeRequest(client);", challenge_transmit
    )
    assert begin_epoch < challenge_transmit < cleared_request

    synchronous = function_body(send, "void SV_SendClientMessages(void)")
    sync_rate = synchronous.index("if (SV_RateDrop(client))")
    sync_fragment = synchronous.index("if (client->netchan.fragment_pending)")
    sync_service = synchronous.index("SV_ServiceNativeShadowChallenge(")
    build_frame = synchronous.index("SV_BuildClientFrame(client);")
    assert sync_rate < sync_fragment < sync_service < build_frame

    asynchronous = function_body(send, "void SV_SendAsyncPackets(void)")
    async_rate = asynchronous.index(
        "if (svs.realtime - client->send_time < client->send_delta)"
    )
    async_fragment = asynchronous.index("if (netchan->fragment_pending)")
    async_active = asynchronous.index("if (CLIENT_ACTIVE(client))")
    async_service = asynchronous.index("SV_ServiceNativeShadowChallenge(")
    native_output = asynchronous.index(
        "native_output_due = client->worr_native_shadow"
    )
    assert async_rate < async_fragment < async_active < async_service < native_output
    assert "CLIENT_ACTIVE(client) && SV_PAUSED" not in asynchronous

    reset = function_body(init, "void SV_ClientReset(client_t *client)")
    assert (
        "client->worr_native_shadow_challenge_barrier_bytes = 0;"
        in reset
    )
    assert "worr_native_shadow_challenge_barrier_bytes" in server_h
    assert "SV_NATIVE_SHADOW_CHALLENGE_QUEUE_TIMEOUT_MS UINT32_C(60000)" in native_h
    assert "SV_NATIVE_SHADOW_TIMEOUT_MS UINT64_C(10000)" in native_h

    print(
        "server_native_challenge_barrier_source_contract "
        "begin=exact-prefix challenge=isolated later-reliables=preserved "
        "queue-timeout-ms=60000 readiness-timeout-ms=10000"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
