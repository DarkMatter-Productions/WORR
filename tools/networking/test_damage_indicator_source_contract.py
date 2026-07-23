#!/usr/bin/env python3
"""Production-placement contract for per-client native damage indicators."""

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

    sgame = (root / "src/game/sgame/player/p_view.cpp").read_text(
        encoding="utf-8"
    )
    send = (root / "src/server/send.c").read_text(encoding="utf-8")
    binders = (root / "src/server/snapshot_event_candidates.c").read_text(
        encoding="utf-8"
    )
    parse = (root / "src/client/parse.cpp").read_text(encoding="utf-8")
    client_shadow = (root / "src/client/event_shadow.cpp").read_text(
        encoding="utf-8"
    )

    feedback = function_body(sgame, "static void P_DamageFeedback(")
    require_order(
        feedback,
        "gi.WriteByte(svc_damage);",
        "gi.WriteByte(client->numDamageIndicators);",
        "gi.WriteDir(",
        "gi.unicast(player, false);",
    )

    write_message = function_body(send, "static inline void write_msg(")
    require_order(
        write_message,
        "MSG_WriteData(msg->data, msg->cursize);",
        "queue_native_damage_indicators(client, msg);",
        "queue_visible_native_game_events(client, msg);",
    )

    queue = function_body(send, "static void queue_native_damage_indicators(")
    require_order(
        queue,
        "Worr_LegacyDamageEventDecodeRawV1(",
        "SV_SnapshotShadowFindWireV1(",
        "SV_SnapshotShadowBuildDamageCandidatesV1(",
        "SV_NativeShadowQueueEventCandidatesV1(",
    )
    for contract in (
        "client->framenum",
        "client->netchan.type != NETCHAN_NEW",
        "SV_NativeShadowModeHasEventV1",
        "Q2PROTO_MAX_DAMAGE_INDICATORS",
    ):
        assert contract in queue

    binder = function_body(
        binders, "SV_SnapshotShadowBuildDamageCandidatesV1("
    )
    require_order(
        binder,
        "load_final_emission_view(",
        "Worr_LegacyDamageEventCandidatesBuildV1(",
        "controlled_entity = view.snapshot->controlled_entity.identity;",
        "candidates[index].subject_entity = controlled_entity;",
        "memcpy(candidates_out, candidates,",
    )
    assert "Worr_EventRecordCandidateValidateV1" in binder

    client_capture = function_body(
        client_shadow, 'extern "C" void CL_EventRangeCaptureDamageV2('
    )
    lineage_sync = function_body(
        client_shadow, "bool synchronize_controlled_action_lineage("
    )
    require_order(
        lineage_sync,
        "CL_SnapshotShadowLatest(&view, &hashes, &projection_ref)",
        "view.snapshot->server_tick != action_tick()",
        "Worr_SnapshotGenerationValidV2(\n            view.snapshot->controlled_entity",
        "snapshot_controlled.identity.index != controlled_entity_index",
        "observed.generation >\n        snapshot_controlled.identity.generation",
        "observed.generation = snapshot_controlled.identity.generation;",
        "observed.present = 1;",
        "observed.provisional = 0;",
        "observed.last_seen_batch = builder_v2.batch_generation;",
    )
    assert "builder_v2.observed[controlled_entity_index]" in lineage_sync
    require_order(
        client_capture,
        "Worr_LegacyDamageEventCandidatesBuildV1(",
        "synchronize_controlled_action_lineage(",
        "for (std::uint32_t index = 0; index < candidate_count; ++index)",
        "candidates[index].subject_entity_index",
        "Worr_CGameEventRangeDeliverActionBatchV2(",
    )
    client_present = function_body(parse, "static void CL_ParseDamage(")
    require_order(
        client_present,
        "damage->count > WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2",
        "i < damage->count",
        "SCR_AddToDamageDisplay(",
        "CL_CGameNativeEventProbeCompleteLegacyDispatch(",
        "WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED",
    )
    server_message = function_body(parse, "void CL_ParseServerMessage(void)")
    damage_start = server_message.index("case Q2P_SVC_DAMAGE:")
    damage_end = server_message.index("case Q2P_SVC_FOG:", damage_start)
    damage_case = server_message[damage_start:damage_end]
    require_order(
        damage_case,
        "svc_msg.damage.count == 0",
        "CL_EventRangeCaptureDamageV2(&svc_msg.damage);",
        "if (!CL_NativeReadinessPilotOwnsEventPresentation())",
        "CL_ParseDamage(&svc_msg.damage);",
        "index < svc_msg.damage.count",
        "WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_SUPPRESSED",
    )

    print(
        "damage_indicator_source_contract "
        "legacy_append=first snapshot=exact subject=controlled-entity "
        "client_raw_batch=bounded controlled_lineage=canonical "
        "completion=per-indicator"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
