#!/usr/bin/env python3
"""Production-placement contract for per-client native help-path markers."""

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

    sgame = (root / "src/game/sgame/gameplay/g_items.cpp").read_text(
        encoding="utf-8"
    )
    send = (root / "src/server/send.c").read_text(encoding="utf-8")
    binders = (root / "src/server/snapshot_event_candidates.c").read_text(
        encoding="utf-8"
    )

    compass = function_body(sgame, "void Compass_Update(")
    require_order(
        compass,
        "gi.WriteByte(svc_help_path);",
        "gi.WriteByte(first ? 1 : 0);",
        "gi.WritePosition(currentPoint);",
        "gi.WriteDir(",
        "gi.unicast(ent, false);",
    )

    write_message = function_body(send, "static inline void write_msg(")
    require_order(
        write_message,
        "MSG_WriteData(msg->data, msg->cursize);",
        "queue_native_damage_indicators(client, msg);",
        "queue_native_help_path_marker(client, msg);",
        "queue_visible_native_game_events(client, msg);",
    )

    queue = function_body(send, "static void queue_native_help_path_marker(")
    require_order(
        queue,
        "Worr_LegacyHelpPathEventDecodeRawV1(",
        "SV_SnapshotShadowFindWireV1(",
        "SV_SnapshotShadowBuildHelpPathCandidateV1(",
        "SV_NativeShadowQueueEventCandidatesV1(",
    )
    for contract in (
        "client->framenum",
        "client->netchan.type != NETCHAN_NEW",
        "SV_NativeShadowModeHasEventV1",
    ):
        assert contract in queue

    binder = function_body(
        binders, "SV_SnapshotShadowBuildHelpPathCandidateV1("
    )
    require_order(
        binder,
        "load_final_emission_view(",
        "Worr_LegacyHelpPathEventCandidateBuildV1(",
        "controlled_entity = view.snapshot->controlled_entity.identity;",
        "candidate.subject_entity = controlled_entity;",
        "*candidate_out = candidate;",
    )
    assert "Worr_EventRecordCandidateValidateV1" in binder

    print(
        "help_path_source_contract "
        "legacy_append=first snapshot=exact subject=controlled-entity"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
