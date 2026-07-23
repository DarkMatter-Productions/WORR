#!/usr/bin/env python3
"""Production-placement contract for reliable mixed native game events."""

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

    send = (root / "src/server/send.c").read_text(encoding="utf-8")
    entities = (root / "src/server/entities.c").read_text(encoding="utf-8")
    native = (root / "src/server/native_shadow.c").read_text(encoding="utf-8")
    binders = (root / "src/server/snapshot_event_candidates.c").read_text(
        encoding="utf-8"
    )

    add_message = function_body(send, "void SV_ClientAddMessage(")
    for contract in (
        "native_reliable_data = msg_write.data;",
        "native_reliable_bytes = msg_write.cursize;",
        "if (flags & MSG_RELIABLE)",
        "SV_NativeShadowCaptureReliableGameEventsV1(",
    ):
        assert contract in add_message
    require_order(
        add_message,
        "client->AddMessage(",
        "SV_NativeShadowCaptureReliableGameEventsV1(",
        "if (flags & MSG_CLEAR)",
    )

    write_frame = function_body(entities, "bool SV_WriteFrameToClient_Enhanced(")
    require_order(
        write_frame,
        "SV_SnapshotShadowCommitFrameV1(",
        "SV_NativeShadowFlushReliableEventsV1(",
        "SV_NativeShadowQueueSnapshotEventsV1(",
    )

    capture = function_body(
        native, "SV_NativeShadowCaptureReliableGameEventsV1("
    )
    require_order(
        capture,
        "Worr_LegacyGameEventDecodeRawSequenceV1(",
        "entry.spawncount = spawncount;",
        "entry.kind = SV_NATIVE_RELIABLE_EVENT_GAME_SEQUENCE;",
        "append_reliable_event_entry(peer, &entry);",
    )
    append = function_body(native, "append_reliable_event_entry(")
    require_order(
        append,
        "state->reliable_events[tail] = *entry;",
        "++state->reliable_event_count;",
        "state->reliable_event_record_count += entry->record_count;",
    )
    assert "SV_NATIVE_SHADOW_RELIABLE_GAME_EVENT_CAPACITY" in append

    flush = function_body(native, "SV_NativeShadowFlushReliableEventsV1(")
    for contract in (
        "entry->spawncount != spawncount",
        "SV_SnapshotShadowBuildGameEventCandidatesWithDeliveryV1(",
        "SV_SNAPSHOT_GAME_EVENT_RELIABLE",
        "clear_reliable_events(state);",
        "SV_NativeShadowQueueEventCandidatesV1(",
    ):
        assert contract in flush
    snapshot_attempt = flush[
        flush.index("SV_SnapshotShadowBuildGameEventCandidatesWithDeliveryV1(") :
    ]
    require_order(
        snapshot_attempt,
        "SV_SnapshotShadowBuildGameEventCandidatesWithDeliveryV1(",
        "clear_reliable_events(state);",
        "SV_NativeShadowQueueEventCandidatesV1(",
    )

    reliable_binder = function_body(
        binders, "SV_SnapshotShadowBuildGameEventCandidatesWithDeliveryV1("
    )
    assert "SV_SNAPSHOT_GAME_EVENT_KNOWN_FLAGS" in reliable_binder
    assert "WORR_EVENT_DELIVERY_RELIABLE_ORDERED" in reliable_binder
    assert "candidates[index].expiry_tick = 0;" in reliable_binder

    print(
        "reliable_game_event_source_contract "
        "legacy_append=first snapshot=fenced order=reliable-before-frame"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
