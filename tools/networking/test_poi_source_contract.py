#!/usr/bin/env python3
"""Production-placement contract for reliable objective and unreliable ping POIs."""

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


def require_poi_writer(
    body: str, key: str, lifetime: str, position: str, image: str,
    color: str, recipient: str, reliable: bool,
) -> None:
    require_order(
        body,
        "gi.WriteByte(svc_poi);",
        f"gi.WriteShort({key});",
        f"gi.WriteShort({lifetime});",
        f"gi.WritePosition({position});",
        f"gi.WriteShort({image});",
        f"gi.WriteByte({color});",
        "gi.WriteByte(POI_FLAG_NONE);",
        f"gi.unicast({recipient}, {'true' if reliable else 'false'});",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    args = parser.parse_args()
    root = args.repo_root.resolve()

    player = (root / "src/game/sgame/player/p_client.cpp").read_text(
        encoding="utf-8"
    )
    items = (root / "src/game/sgame/gameplay/g_items.cpp").read_text(
        encoding="utf-8"
    )
    commands = (root / "src/game/sgame/commands/command_client.cpp").read_text(
        encoding="utf-8"
    )
    send = (root / "src/server/send.c").read_text(encoding="utf-8")
    entities = (root / "src/server/entities.c").read_text(encoding="utf-8")
    native = (root / "src/server/native_shadow.c").read_text(encoding="utf-8")
    adapter = (
        root / "src/common/net/legacy_poi_event_candidate.c"
    ).read_text(encoding="utf-8")
    binders = (root / "src/server/snapshot_event_candidates.c").read_text(
        encoding="utf-8"
    )

    objective = function_body(player, "void P_SendLevelPOI(")
    require_poi_writer(
        objective,
        "POI_OBJECTIVE",
        "10000",
        "ent->client->compass.poiLocation",
        "ent->client->compass.poiImage",
        "208",
        "ent",
        True,
    )

    pickup = function_body(items, "static void BroadcastTeamPickupPing(")
    require_poi_writer(
        pickup,
        "POI_PING + (picker->s.number - 1)",
        "5000",
        "picker->s.origin",
        "gi.imageIndex(it->icon)",
        "215",
        "ec",
        False,
    )

    dropped = function_body(
        commands, "void Drop(gentity_t* ent, const CommandArgs& args) {"
    )
    require_poi_writer(
        dropped,
        "POI_PING + (ent->s.number - 1)",
        "5000",
        "ent->s.origin",
        "gi.imageIndex(it->icon)",
        "215",
        "ec",
        False,
    )

    point = function_body(
        commands, "void Wave(gentity_t* ent, const CommandArgs& args) {"
    )
    require_poi_writer(
        point,
        "POI_PING + (ent->s.number - 1)",
        "5000",
        "pointTrace.endPos",
        "level.picPing",
        "208",
        "player",
        False,
    )

    add_message = function_body(send, "void SV_ClientAddMessage(")
    require_order(
        add_message,
        "client->AddMessage(",
        "SV_NativeShadowCaptureReliableGameEventsV1(",
        "SV_NativeShadowCaptureReliableKeyedPOIV1(",
        "if (flags & MSG_CLEAR)",
    )

    write_message = function_body(send, "static inline void write_msg(")
    require_order(
        write_message,
        "MSG_WriteData(msg->data, msg->cursize);",
        "queue_native_keyed_poi(client, msg);",
        "queue_visible_native_game_events(client, msg);",
    )

    queue = function_body(send, "static void queue_native_keyed_poi(")
    require_order(
        queue,
        "Worr_LegacyKeyedPOIEventDecodeRawV1(",
        "SV_SnapshotShadowFindWireV1(",
        "SV_SnapshotShadowBuildKeyedPOICandidateV1(",
        "SV_NativeShadowQueueEventCandidatesV1(",
    )
    for contract in (
        "client->framenum",
        "client->netchan.type != NETCHAN_NEW",
        "SV_NativeShadowModeHasEventV1",
    ):
        assert contract in queue

    write_frame = function_body(entities, "bool SV_WriteFrameToClient_Enhanced(")
    require_order(
        write_frame,
        "SV_SnapshotShadowCommitFrameV1(",
        "SV_NativeShadowFlushReliableEventsV1(",
        "SV_NativeShadowQueueSnapshotEventsV1(",
    )

    capture = function_body(
        native, "SV_NativeShadowCaptureReliableKeyedPOIV1("
    )
    require_order(
        capture,
        "Worr_LegacyKeyedPOIEventDecodeRawV1(",
        "entry.spawncount = spawncount;",
        "entry.kind = SV_NATIVE_RELIABLE_EVENT_KEYED_POI;",
        "append_reliable_event_entry(peer, &entry);",
    )
    append = function_body(native, "append_reliable_event_entry(")
    require_order(
        append,
        "state->reliable_events[tail] = *entry;",
        "++state->reliable_event_count;",
        "state->reliable_event_record_count += entry->record_count;",
    )

    flush = function_body(native, "SV_NativeShadowFlushReliableEventsV1(")
    for contract in (
        "entry->spawncount != spawncount",
        "SV_SnapshotShadowBuildGameEventCandidatesWithDeliveryV1(",
        "SV_SnapshotShadowBuildKeyedPOICandidateWithDeliveryV1(",
        "SV_SNAPSHOT_KEYED_POI_RELIABLE",
        "clear_reliable_events(state);",
    ):
        assert contract in flush

    binder = function_body(
        binders, "SV_SnapshotShadowBuildKeyedPOICandidateWithDeliveryV1("
    )
    require_order(
        binder,
        "load_final_emission_view(",
        "poi->time == 0",
        "Worr_LegacyKeyedPOIEventCandidateBuildV1(",
        "controlled_entity = view.snapshot->controlled_entity.identity;",
        "candidate.subject_entity = controlled_entity;",
        "*candidate_out = candidate;",
    )
    for contract in (
        "SV_SNAPSHOT_KEYED_POI_KNOWN_FLAGS",
        "sent.wire_snapshot_number == UINT32_MAX",
        "WORR_EVENT_DELIVERY_TRANSIENT",
        "candidate.expiry_tick = candidate.source_tick + 1u;",
        "WORR_EVENT_FLAG_SNAPSHOT_FENCED",
        "Worr_EventRecordCandidateValidateV1",
    ):
        assert contract in binder
    adapter_build = function_body(
        adapter, "Worr_LegacyKeyedPOIEventCandidateBuildV1("
    )
    assert "WORR_EVENT_DELIVERY_RELIABLE_ORDERED" in adapter_build
    assert "candidate.expiry_tick = 0;" in adapter_build

    print(
        "poi_source_contract producers=objective,pickup,drop,point "
        "legacy_append=first reliable=fifo unreliable=post-fit "
        "snapshot=exact subject=controlled-entity source=world"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
