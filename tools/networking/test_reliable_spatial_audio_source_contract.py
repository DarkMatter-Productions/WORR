#!/usr/bin/env python3
"""Production-placement contract for reliable/local native spatial audio."""

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

    game = (root / "src/server/game.c").read_text(encoding="utf-8")
    send = (root / "src/server/send.c").read_text(encoding="utf-8")

    start_sound = function_body(game, "static void SV_StartSound(")
    for contract in (
        "sound_encoded = q2proto_server_multicast_write(",
        "SV_SNAPSHOT_SPATIAL_AUDIO_RELIABLE",
        "SV_SNAPSHOT_SPATIAL_AUDIO_NO_PHS",
        "SV_ClientAddMessage(client, MSG_RELIABLE);",
        "SV_QueueNativeSpatialAudio(",
    ):
        assert contract in start_sound
    require_order(
        start_sound,
        "sound_encoded = q2proto_server_multicast_write(",
        "SV_Multicast(origin, to, channel & CHAN_RELIABLE);",
        "SV_QueueNativeSpatialAudio(",
    )
    reliable_branch = start_sound[
        start_sound.index("// reliable sounds will always have position explicitly set") :
    ]
    require_order(
        reliable_branch,
        "SV_ClientAddMessage(client, MSG_RELIABLE);",
        "if (sound_encoded)",
        "SV_QueueNativeSpatialAudio(",
    )

    local_sound = function_body(game, "static void PF_LocalSound(")
    for contract in (
        "SV_SNAPSHOT_SPATIAL_AUDIO_RELIABLE",
        "SV_SNAPSHOT_SPATIAL_AUDIO_LOCAL_ONLY",
        "SV_SNAPSHOT_SPATIAL_AUDIO_NO_PHS",
    ):
        assert contract in local_sound
    require_order(
        local_sound,
        "const bool sound_encoded =",
        "q2proto_server_write(",
        "PF_Unicast(target, !!(channel & CHAN_RELIABLE), dupe_key);",
        "SV_QueueNativeSpatialAudio(",
    )
    assert "if (sound_encoded)" in local_sound
    assert "if (channel & CHAN_RELIABLE)" in local_sound

    queue_audio = function_body(send, "void SV_QueueNativeSpatialAudio(")
    for contract in (
        "SV_NativeShadowModeHasEventV1",
        "SV_SnapshotShadowFindWireV1",
        "q2proto_sound_decode_message",
        "SV_SnapshotShadowBuildSpatialAudioCandidateWithDeliveryV1",
        "SV_NativeShadowQueueEventCandidatesV1",
    ):
        assert contract in queue_audio
    require_order(
        queue_audio,
        "SV_SnapshotShadowFindWireV1",
        "q2proto_sound_decode_message",
        "SV_SnapshotShadowBuildSpatialAudioCandidateWithDeliveryV1",
        "SV_NativeShadowQueueEventCandidatesV1",
    )

    emit_sound = function_body(send, "static void emit_snd(")
    require_order(
        emit_sound,
        "q2proto_server_write(",
        "SV_QueueNativeSpatialAudio(client, &message.sound, 0);",
    )

    print(
        "reliable_spatial_audio_source_contract "
        "legacy_write=first reliable=ordered local=world_bound"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
