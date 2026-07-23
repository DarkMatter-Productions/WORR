#!/usr/bin/env python3
"""Source placement contract for native present-once event ownership."""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re


INVENTORY_RELATIVE_PATH = Path(
    "tools/networking/native_event_presenter_ownership_inventory.json"
)
CLASSIFICATIONS = (
    "legacy-authoritative",
    "native-shadow/probe",
    "migrated/native-authoritative",
    "suppressed",
)
PRESENTER_DEFINITION_RE = re.compile(
    r"(?ms)^\s*(?:\[\[.*?\]\]\s*)?"
    r"(?:extern\s+\"C\"\s+)?(?:static\s+)?(?:inline\s+)?"
    r"(?:bool|void|int|float|qboolean|qhandle_t)\s+"
    r"(?P<entrypoint>"
    r"S_(?:ParseStartSound|StartSound)|"
    r"CL_(?:ParseTEnt|MuzzleFlash2?|AddHelpPath|"
    r"Start(?:Muzzle|TEnt)Sound|"
    r"Present[A-Za-z0-9_]*Value|ParseDamage)|"
    r"CG_AddDamageDisplay|SCR_AddToDamageDisplay|"
    r"present_entity_impulse_event|"
    r"parse_entity_event"
    r")\s*\([^;{}]*\)\s*\{"
)
RAW_GUARDED_DISPATCH_RE = re.compile(
    r"if\s*\(\s*(?:![A-Za-z_][A-Za-z0-9_]*\s*\|\|\s*)?"
    r"!CL_NativeReadinessPilotOwnsEventPresentation\(\)"
    r"\s*\)\s*(?:\{\s*)?"
    r"(?P<entrypoint>[A-Za-z_][A-Za-z0-9_]*)\s*\(",
    re.MULTILINE,
)


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


def case_blocks(source: str, label: str) -> list[str]:
    needle = f"case {label}:"
    blocks: list[str] = []
    cursor = 0
    while True:
        start = source.find(needle, cursor)
        if start < 0:
            return blocks
        next_case = source.find("\n        case ", start + len(needle))
        next_default = source.find("\n        default:", start + len(needle))
        ends = [end for end in (next_case, next_default) if end >= 0]
        end = min(ends) if ends else len(source)
        blocks.append(source[start:end])
        cursor = end


def require_guarded_case(
    block: str, capture: str, side_effect: str
) -> None:
    guard = "if (!CL_NativeReadinessPilotOwnsEventPresentation())"
    require_order(block, capture, guard, side_effect)


def require_probe_completion_case(
    block: str, capture: str, side_effect: str, carrier_kind: str
) -> None:
    require_guarded_case(block, capture, side_effect)
    completion = "CL_CGameNativeEventProbeCompleteLegacyDispatch("
    dispatched = (
        f"{carrier_kind},\n"
        "                    "
        "WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED"
    )
    suppressed = (
        f"{carrier_kind},\n"
        "                    "
        "WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_SUPPRESSED"
    )
    if block.count(completion) != 2:
        raise AssertionError("raw action does not have exactly two completions")
    if block.count(carrier_kind) != 2:
        raise AssertionError("raw action completion carrier drift")
    require_order(
        block,
        capture,
        "if (!CL_NativeReadinessPilotOwnsEventPresentation())",
        side_effect,
        dispatched,
        "} else {",
        suppressed,
    )


def validate_ownership_inventory(
    root: Path, inventory_path: Path
) -> tuple[dict[str, object], list[dict[str, object]]]:
    data = json.loads(inventory_path.read_text(encoding="utf-8"))
    if set(data) != {
        "schema_version",
        "task_id",
        "classifications",
        "conditions",
        "entries",
    }:
        raise AssertionError("ownership inventory top-level schema drift")
    if data["schema_version"] != 1 or data["task_id"] != "FR-10-T07":
        raise AssertionError("ownership inventory identity/schema drift")
    if tuple(data["classifications"]) != CLASSIFICATIONS:
        raise AssertionError("ownership inventory classification drift")

    conditions = data["conditions"]
    entries = data["entries"]
    if not isinstance(conditions, dict) or not conditions:
        raise AssertionError("ownership inventory has no conditions")
    if not isinstance(entries, list) or not entries:
        raise AssertionError("ownership inventory has no entries")

    source_cache: dict[str, str] = {}
    entry_ids: set[str] = set()
    surface_index: dict[tuple[str, str], list[dict[str, object]]] = {}
    for entry in entries:
        if set(entry) != {
            "id",
            "family",
            "role",
            "classification",
            "owner",
            "cutover_condition",
            "source",
            "test_evidence",
        }:
            raise AssertionError(
                f"ownership inventory entry schema drift: {entry.get('id')}"
            )
        entry_id = entry["id"]
        if not isinstance(entry_id, str) or not entry_id or entry_id in entry_ids:
            raise AssertionError(f"duplicate/invalid inventory id: {entry_id}")
        entry_ids.add(entry_id)
        for field in ("family", "role", "owner"):
            if not isinstance(entry[field], str) or not entry[field]:
                raise AssertionError(f"{entry_id}: missing {field}")
        if entry["classification"] not in CLASSIFICATIONS:
            raise AssertionError(f"{entry_id}: unknown classification")
        if entry["cutover_condition"] not in conditions:
            raise AssertionError(f"{entry_id}: unknown cutover condition")
        evidence = entry["test_evidence"]
        if (
            not isinstance(evidence, list)
            or not evidence
            or not all(isinstance(item, str) and item for item in evidence)
            or "network-native-event-presenter-source-contract"
            not in evidence
        ):
            raise AssertionError(f"{entry_id}: incomplete test evidence")

        source = entry["source"]
        if set(source) != {
            "file",
            "entrypoint",
            "anchor",
            "occurrences",
        }:
            raise AssertionError(f"{entry_id}: source schema drift")
        source_file = source["file"]
        entrypoint = source["entrypoint"]
        anchor = source["anchor"]
        occurrences = source["occurrences"]
        if (
            not isinstance(source_file, str)
            or not source_file.startswith(("src/client/", "src/game/cgame/"))
            or "q2proto/" in source_file
            or not isinstance(entrypoint, str)
            or not entrypoint
            or not isinstance(anchor, str)
            or not anchor
            or not isinstance(occurrences, int)
            or isinstance(occurrences, bool)
            or occurrences <= 0
        ):
            raise AssertionError(f"{entry_id}: invalid source contract")
        source_path = (root / source_file).resolve()
        if not source_path.is_relative_to(root) or not source_path.is_file():
            raise AssertionError(f"{entry_id}: source file missing/outside repo")
        text = source_cache.setdefault(
            source_file, source_path.read_text(encoding="utf-8")
        )
        actual_occurrences = text.count(anchor)
        if actual_occurrences != occurrences:
            raise AssertionError(
                f"{entry_id}: anchor drift for {source_file}::{entrypoint}: "
                f"expected {occurrences}, got {actual_occurrences}"
            )
        surface_index.setdefault((source_file, entrypoint), []).append(entry)

    # Every raw presenter boundary definition matching the maintained naming
    # surface must have a classification. Scanning both production trees also
    # catches a newly introduced CL_Present*Value entry point automatically.
    discovered_definitions: Counter[tuple[str, str]] = Counter()
    for source_root in (root / "src/client", root / "src/game/cgame"):
        for source_path in source_root.rglob("*.cpp"):
            source_file = source_path.relative_to(root).as_posix()
            text = source_cache.setdefault(
                source_file, source_path.read_text(encoding="utf-8")
            )
            for match in PRESENTER_DEFINITION_RE.finditer(text):
                discovered_definitions[(
                    source_file,
                    match.group("entrypoint"),
                )] += 1
    duplicate_definitions = {
        key: count
        for key, count in discovered_definitions.items()
        if count != 1
    }
    if duplicate_definitions:
        raise AssertionError(
            f"duplicate raw presenter definitions: {duplicate_definitions}"
        )
    unclassified_definitions = sorted(
        key for key in discovered_definitions if key not in surface_index
    )
    if unclassified_definitions:
        raise AssertionError(
            "unclassified raw presenter definitions: "
            f"{unclassified_definitions}"
        )

    # Capture every server-message suppression edge generically, rather than
    # trusting only the known service labels checked later in this test.
    parser_text = source_cache["src/client/parse.cpp"]
    actual_guarded = Counter(
        match.group("entrypoint")
        for match in RAW_GUARDED_DISPATCH_RE.finditer(parser_text)
    )
    expected_guarded = Counter()
    for entry in entries:
        if entry["role"] != "raw-suppression":
            continue
        source = entry["source"]
        if (
            source["file"] != "src/client/parse.cpp"
            or entry["classification"] != "suppressed"
        ):
            raise AssertionError(
                f"{entry['id']}: raw suppression owner/classification drift"
            )
        expected_guarded[source["entrypoint"]] += source["occurrences"]
    if actual_guarded != expected_guarded:
        raise AssertionError(
            "raw suppression inventory/source drift: "
            f"expected {expected_guarded}, got {actual_guarded}"
        )

    ownership_queries: Counter[str] = Counter()
    embedded_queries: Counter[str] = Counter()
    authority_switches: Counter[str] = Counter()
    for source_root in (root / "src/client", root / "src/game/cgame"):
        for source_path in source_root.rglob("*.cpp"):
            source_file = source_path.relative_to(root).as_posix()
            text = source_cache.setdefault(
                source_file, source_path.read_text(encoding="utf-8")
            )
            ownership_queries[source_file] += text.count(
                "CL_NativeReadinessPilotOwnsEventPresentation()"
            )
            embedded_queries[source_file] += text.count(
                "cl_worr_native_event_presentation_owned->integer"
            )
            authority_switches[source_file] += text.count(
                "CG_NativeEventPresenterSetEffectAuthority("
            )
    ownership_queries += Counter()
    embedded_queries += Counter()
    authority_switches += Counter()
    expected_queries = Counter(
        {
            entry["source"]["file"]: entry["source"]["occurrences"]
            for entry in entries
            if entry["role"] == "ownership-query"
            and entry["source"]["entrypoint"]
            == "CL_NativeReadinessPilotOwnsEventPresentation"
        }
    )
    expected_embedded = Counter(
        {
            entry["source"]["file"]: entry["source"]["occurrences"]
            for entry in entries
            if entry["role"] == "ownership-query"
            and entry["source"]["anchor"]
            == "cl_worr_native_event_presentation_owned->integer"
        }
    )
    expected_authority_switches = Counter(
        {
            entry["source"]["file"]: entry["source"]["occurrences"]
            for entry in entries
            if entry["role"] == "authority-switch"
        }
    )
    if ownership_queries != expected_queries:
        raise AssertionError(
            "unclassified raw ownership query: "
            f"expected {expected_queries}, got {ownership_queries}"
        )
    if embedded_queries != expected_embedded:
        raise AssertionError(
            "unclassified embedded-event suppression: "
            f"expected {expected_embedded}, got {embedded_queries}"
        )
    if authority_switches != expected_authority_switches:
        raise AssertionError(
            "native effect-authority call-site drift: "
            f"expected {expected_authority_switches}, got {authority_switches}"
        )

    readiness = source_cache["src/client/native_readiness_pilot.cpp"]
    if re.search(
        r"event_effect_cutover_confirmed\s*=\s*(?:true|1)", readiness
    ):
        raise AssertionError("production event-effect cutover was promoted")
    if any(
        entry["classification"] == "migrated/native-authoritative"
        for entry in entries
    ):
        raise AssertionError(
            "inventory claims native authority without a production cutover"
        )

    native = source_cache["src/game/cgame/cg_native_event_presenter.cpp"]
    commit_body = function_body(native, "void present(")
    actual_native_commits = Counter(
        re.findall(r"\b((?:CL|CG|S)_[A-Za-z0-9_]+)\s*\(", commit_body)
    )
    expected_native_commits = Counter(
        entry["source"]["entrypoint"]
        for entry in entries
        if entry["role"] == "native-commit"
    )
    if actual_native_commits != expected_native_commits:
        raise AssertionError(
            "native commit inventory/source drift: "
            f"expected {expected_native_commits}, "
            f"got {actual_native_commits}"
        )
    for entry in entries:
        if (
            entry["role"] == "native-commit"
            and entry["classification"] != "native-shadow/probe"
        ):
            raise AssertionError(
                f"{entry['id']}: dormant native commit misclassified"
            )

    return data, entries


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--inventory", type=Path)
    args = parser.parse_args()
    root = args.repo_root.resolve()
    inventory_path = (
        args.inventory.resolve()
        if args.inventory
        else root / INVENTORY_RELATIVE_PATH
    )
    inventory, inventory_entries = validate_ownership_inventory(
        root, inventory_path
    )

    runtime = (root / "src/game/cgame/cg_event_runtime.cpp").read_text(
        encoding="utf-8"
    )
    event_shadow = (root / "src/game/cgame/cg_event_shadow.cpp").read_text(
        encoding="utf-8"
    )
    native = (
        root / "src/game/cgame/cg_native_event_presenter.cpp"
    ).read_text(encoding="utf-8")
    native_header = (
        root / "src/game/cgame/cg_native_event_presenter.hpp"
    ).read_text(encoding="utf-8")
    entity_api = (root / "src/game/cgame/cg_entity_api.cpp").read_text(
        encoding="utf-8"
    )
    cgame_main = (root / "src/game/cgame/cg_main.cpp").read_text(
        encoding="utf-8"
    )
    readiness = (
        root / "src/client/native_readiness_pilot.cpp"
    ).read_text(encoding="utf-8")
    parse = (root / "src/client/parse.cpp").read_text(encoding="utf-8")
    client_event_shadow = (
        root / "src/client/event_shadow.cpp"
    ).read_text(encoding="utf-8")
    cgame_client = (root / "src/client/cgame.cpp").read_text(
        encoding="utf-8"
    )
    input_client = (root / "src/client/input.cpp").read_text(
        encoding="utf-8"
    )
    server_commands = (root / "src/server/commands.c").read_text(
        encoding="utf-8"
    )
    entities = (root / "src/game/cgame/cg_entities.cpp").read_text(
        encoding="utf-8"
    )
    effects = (root / "src/game/cgame/cg_effects.cpp").read_text(
        encoding="utf-8"
    )
    temp_effects = (root / "src/game/cgame/cg_tent.cpp").read_text(
        encoding="utf-8"
    )
    cgame_draw = (root / "src/game/cgame/cg_draw.cpp").read_text(
        encoding="utf-8"
    )
    presenter_test = (
        root / "tools/networking/cgame_native_event_presenter_test.cpp"
    ).read_text(encoding="utf-8")

    predictions = function_body(
        runtime, "cg_event_runtime_result_v1 present_predictions("
    )
    require_order(
        predictions,
        "presenter_ready(best->record, context)",
        "Worr_EventJournalMarkPresentedV1(&state.journal, ref)",
        "audit_present(state, 1u",
        "increment_saturated(state.status.predicted_presentations);",
        "presenter_commit(best->record, context);",
    )
    authority = function_body(
        runtime, "cg_event_runtime_result_v1 present_authority("
    )
    require_order(
        authority,
        "presenter_ready(entry->record, context)",
        "Worr_EventJournalMarkPresentedV1(",
        "audit_present(state, 2u",
        "increment_saturated(\n                state.status.authoritative_presentations);",
        "presenter_commit(entry->record, context);",
    )
    for body, ready_call in (
        (predictions, "if (!presenter_ready(best->record, context))"),
        (authority, "if (!presenter_ready(entry->record, context))"),
    ):
        rejection = body[
            body.index(ready_call) : body.index(
                "Worr_EventJournalMarkPresentedV1", body.index(ready_call)
            )
        ]
        require_order(
            rejection,
            ready_call,
            "state.status.authority_requires_resync = 1;",
            "mark_authority_degraded(state);",
            "return CG_EVENT_RUNTIME_DEGRADED;",
        )

    presenter_ready = function_body(runtime, "bool presenter_ready(")
    for contract in (
        "if (!event_can_present_callback && !event_present_callback)",
        "presentation_callback_active",
        "presentation_callback_active = true;",
        "event_can_present_callback(&record, &context)",
        "presentation_callback_active = false;",
    ):
        assert contract in presenter_ready
    presenter_commit = function_body(runtime, "void presenter_commit(")
    require_order(
        presenter_commit,
        "presentation_callback_active = true;",
        "event_present_callback(&record, &context);",
        "presentation_callback_active = false;",
    )
    advance_runtime = function_body(
        runtime, "cg_event_runtime_result_v1 CG_EventRuntimeAdvanceAudit("
    )
    require_order(
        advance_runtime,
        "if (runtime_mutation_blocked())",
        "return CG_EVENT_RUNTIME_REENTRANT;",
    )
    set_presenter = function_body(
        runtime, "void CG_EventRuntimeSetPresenter("
    )
    require_order(
        set_presenter,
        "event_can_present_callback = can_present;",
        "event_present_callback = present;",
    )
    checkpoint_ready = function_body(
        runtime, "bool CG_EventRuntimeCheckpointReady("
    )
    require_order(
        checkpoint_ready,
        "if (!status_out || expected_authority_epoch == 0 ||",
        "runtime_mutation_blocked()",
        "latch_local_interaction_resync();",
        "!runtime.authority_initialized",
        "runtime.status.authority_epoch != expected_authority_epoch",
        "runtime.status.authority_requires_resync != 0",
        "runtime.status.authority_degraded != 0",
        "authority_has_unpresented_records(runtime)",
        "auto status = runtime.status;",
        "status.receipt = runtime.journal.receipt;",
        "*status_out = status;",
    )

    load_fence = function_body(native, "bool load_fence(")
    for contract in (
        "CG_EVENT_RUNTIME_PRESENTATION_AUTHORITY",
        "context.fence_snapshot_id",
    ):
        assert contract in load_fence
    require_order(
        load_fence,
        "CG_CanonicalSnapshotTimelineFindSnapshot(",
        "CG_CanonicalSnapshotTimelineCopyPlayer(",
        "CG_CanonicalSnapshotTimelineCopyEntities(",
        "fence_player = player;",
        "loaded_fence_id = context.fence_snapshot_id;",
    )
    resolve_entity = function_body(native, "bool resolve_entity(")
    require_order(
        resolve_entity,
        "entity.generation.identity.index != identity.index",
        "entity.generation.identity.generation !=\n                         identity.generation",
        "CG_CanonicalEntityToRenderStateV1(",
        "*generation_out = identity.generation;",
    )
    for contract in (
        "fence_player.controlled_entity.identity",
        "WORR_SNAPSHOT_PLAYER_MOVEMENT | WORR_SNAPSHOT_PLAYER_VIEW",
        "Worr_SnapshotPlayerValidateV2(",
        "std::memcpy(state.origin, fence_player.movement.origin",
        "state.angles[PITCH] = pitch / 3.0f;",
        "state.angles[YAW] = fence_player.view_angles[YAW];",
        "state.angles[ROLL] = 0.0f;",
    ):
        assert contract in resolve_entity
    assert "cl_entities[" not in resolve_entity
    spatial_audio = function_body(native, "bool prepare_spatial_audio(")
    require_order(
        spatial_audio,
        "const bool has_entity_channel",
        "record.source_entity.index != payload.raw_entity",
        "resolve_entity(record.source_entity, context, &source,",
        "prepared.source_generation = generation;",
        "prepared.sound_entity =",
    )

    can_present = function_body(native, "bool can_present(")
    require_order(
        can_present,
        "prepared = {};",
        "context->schema_version != CG_EVENT_RUNTIME_PRESENTER_VERSION",
        "switch (record->payload_kind)",
        "prepared.record = *record;",
        "prepared.context = *context;",
        "prepared.ready = true;",
    )
    assert "case WORR_EVENT_PAYLOAD_AUDIO:" in can_present
    assert "supported = false;" in can_present
    for signature, source in (
        ("bool CL_CanPresentMuzzleFlashValue(", effects),
        ("bool CL_CanPresentLegacyEntityEventValue(", entities),
        ("bool CL_CanPresentFootstepValue(", temp_effects),
        ("bool CL_CanPresentTEntValue(", temp_effects),
        ("bool CL_CanPresentHelpPathValue(", temp_effects),
    ):
        pure_preflight = function_body(source, signature)
        assert "S_RegisterSound(" not in pure_preflight
        assert "cgei->S_RegisterSound(" not in pure_preflight
    prepare_muzzle = function_body(native, "bool prepare_muzzle(")
    require_order(
        prepare_muzzle,
        "resolve_entity(record.source_entity, context,",
        "if (!full_preflight_enabled())",
        "CL_CanPresentMuzzleFlashValue(",
        "select_presentation_kind(presentation_kind_t::muzzle);",
    )
    full_preflight = function_body(native, "bool full_preflight_enabled()")
    assert (
        "effect_authority_enabled || preflight_probe_active()"
        in full_preflight
    )
    select_kind = function_body(
        native, "void select_presentation_kind("
    )
    require_order(
        select_kind,
        "prepared.preflight_kind = kind;",
        "prepared.kind = effect_authority_enabled",
        ": presentation_kind_t::none;",
    )
    for signature in (
        "bool prepare_legacy_entity(",
        "bool prepare_legacy_temp(",
        "bool prepare_muzzle(",
        "bool prepare_spatial_audio(",
        "bool prepare_damage(",
        "bool prepare_effect(",
    ):
        prepared_family = function_body(native, signature)
        assert "if (!full_preflight_enabled())" in prepared_family
    present = function_body(native, "void present(")
    require_order(
        present,
        "std::memcmp(record, &prepared.record, sizeof(*record))",
        "std::memcmp(context, &prepared.context, sizeof(*context))",
        "switch (prepared.kind)",
    )
    committed_present = present[present.index("switch (prepared.kind)") :]
    require_order(
        committed_present,
        "switch (prepared.kind)",
        "account_probe_commit();",
        "scrub_prepared();",
    )
    account_probe = function_body(native, "void account_probe_commit()")
    require_order(
        account_probe,
        "if (!prepared.probe_commit)",
        "increment_saturated(probe_commits);",
        "increment_saturated(probe_commits_by_kind[kind]);",
        "increment_saturated(probe_effects_suppressed);",
    )
    for forbidden in (
        "CG_EventRuntime",
        "CL_PresentLegacyEntityEventValue",
        "CL_PresentTEntValue",
        "CL_PresentMuzzleFlashValue",
        "S_StartSound",
        "CL_PresentDamageDisplayValue",
        "CL_AddHelpPath",
    ):
        assert forbidden not in account_probe
    init_cvars = function_body(
        native, "void CG_NativeEventPresenterInitCvars()"
    )
    require_order(
        init_cvars,
        'Cvar_Get("cg_native_event_preflight_probe", "0",',
        "CVAR_NOARCHIVE",
    )
    begin_map = function_body(
        native, "void CG_NativeEventPresenterBeginMap()"
    )
    require_order(
        begin_map,
        "scrub_prepared();",
        "preflight_probe_latched =",
        "native_event_preflight_probe->integer != 0;",
        "map_active = true;",
        "increment_saturated(map_generation);",
        "reset_probe_window_counters();",
        "probe_checkpoint = {};",
        "CG_NativeEventProbeRawBeginMap();",
    )
    end_map = function_body(
        native, "void CG_NativeEventPresenterEndMap()"
    )
    require_order(
        end_map,
        "CG_NativeEventProbeRawEndMap();",
        "map_active = false;",
        "preflight_probe_latched = false;",
        "scrub_prepared();",
    )
    resources_required = function_body(
        native, "bool CG_NativeEventPresenterResourcesRequired()"
    )
    assert (
        "effect_authority_enabled || preflight_probe_active()"
        in resources_required
    )
    presenter_status = function_body(
        native, "bool CG_NativeEventPresenterGetStatus("
    )
    for contract in (
        "CG_NATIVE_EVENT_PRESENTER_STATUS_VERSION",
        "preflight_probe_requested",
        "preflight_probe_latched",
        "preflight_probe_active",
        "probe_commits_by_kind[index]",
    ):
        assert contract in presenter_status
    assert (
        "CG_NATIVE_EVENT_PRESENTER_STATUS_VERSION = 2u"
        in native_header
    )
    assert "CG_NATIVE_EVENT_PRESENTER_KIND_COUNT = 8u" in native_header
    assert (
        "static_assert(CG_NATIVE_EVENT_PRESENTER_KIND_COUNT ==\n"
        "              WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT);"
    ) in native_header
    probe_abi = (
        root / "inc/shared/cgame_native_event_probe.h"
    ).read_text(encoding="utf-8")
    assert "#define WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT 8u" in probe_abi
    assert (
        "sizeof(cg_native_event_presenter_status_v1) == 128u"
        in native_header
    )
    for contract in (
        "sizeof(worr_cgame_native_event_probe_status_v1) == 336",
        "WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION 1u",
        "WORR_CGAME_NATIVE_EVENT_PROBE_API_VERSION_V2 2u",
        "WORR_CGAME_NATIVE_EVENT_PROBE_STATUS_VERSION 3u",
        "sizeof(worr_cgame_native_event_probe_checkpoint_receipt_v1) == 32",
        "checkpoint_id) == 24",
        "sizeof(worr_cgame_native_event_probe_export_v1)",
        "8 + 2 * sizeof(void *)",
        "sizeof(worr_cgame_native_event_probe_export_v2)",
        "8 + 3 * sizeof(void *)",
        "Worr_CGameNativeEventProbeChainAppendV1(",
        "WORR-EVENT-PROBE-V1",
        "CompleteLegacyDispatch",
        "GetStatus",
        "Checkpoint",
    ):
        assert contract in probe_abi
    get_probe_status = function_body(native, "bool get_probe_status(")
    for contract in (
        "status.probe_action_chain_hash",
        "status.native_effect_chain_hash",
        "CG_NativeEventProbeFillRawStatus(&status)",
        "CG_EventRuntimeGetStatus(&runtime_status)",
        "status.authoritative_presentations",
        "status.legacy_ref_body_mismatches",
    ):
        assert contract in get_probe_status
    assert (
        "CG_NativeEventProbeCompleteLegacyDispatch,\n"
        "    get_probe_status,"
    ) in native
    assert (
        "const worr_cgame_native_event_probe_export_v1 "
        "native_event_probe_api"
    ) in native
    assert (
        "const worr_cgame_native_event_probe_export_v2 "
        "native_event_probe_api_v2"
    ) in native
    assert "return &native_event_probe_api;" in native
    assert "return &native_event_probe_api_v2;" in native
    checkpoint_probe = function_body(native, "bool checkpoint_probe_window(")
    require_order(
        checkpoint_probe,
        "CG_EventRuntimeGetStatus(&runtime_status)",
        "expected_map_generation == 0",
        "expected_map_generation != map_generation",
        "expected_authority_epoch != runtime_status.authority_epoch",
        "if (probe_checkpoint.applied)",
        "WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED",
        "WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_CONFLICT",
        "if (!map_active || !preflight_probe_active()",
        "if (prepared.ready)",
        "CG_NativeEventProbeFillRawStatus(&raw_status)",
        "raw_status.raw_pending_count != 0",
        "CG_NativeEventProbeRawCheckpointReady()",
        "CG_EventRuntimeCheckpointReady(",
        "CG_NativeEventProbeRawApplyCheckpoint();",
        "reset_probe_window_counters();",
        "probe_checkpoint.applied = true;",
        "WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED",
    )
    assert checkpoint_probe.count("CG_NativeEventProbeRawApplyCheckpoint();") == 1
    assert checkpoint_probe.count("reset_probe_window_counters();") == 1
    runtime_checkpoint_refusal = checkpoint_probe[
        checkpoint_probe.index("if (!CG_EventRuntimeCheckpointReady(") :
        checkpoint_probe.index("/* Everything above is read-only.")
    ]
    assert (
        "receipt.result = WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_BUSY;"
        in runtime_checkpoint_refusal
    )
    for forbidden in (
        "CG_EventRuntimeResetAuthority",
        "CG_EventRuntimeResetSnapshot",
        "CG_NativeEventProbeRawBeginMap",
        "CG_NativeEventProbeRawEndMap",
    ):
        assert forbidden not in checkpoint_probe

    raw_checkpoint_ready = function_body(
        event_shadow, "bool CG_NativeEventProbeRawCheckpointReady()"
    )
    for contract in (
        "raw_probe.map_active",
        "raw_probe.pending_count == 0",
        "raw_probe.pair_failures == 0",
        "raw_probe.effect_suppressions == 0",
    ):
        assert contract in raw_checkpoint_ready
    raw_checkpoint_apply = function_body(
        event_shadow, "void CG_NativeEventProbeRawApplyCheckpoint()"
    )
    require_order(
        raw_checkpoint_apply,
        "if (!CG_NativeEventProbeRawCheckpointReady())",
        "raw_probe.action_by_kind = {};",
        "raw_probe.action_records = 0;",
        "raw_probe.action_chain_hash = 0;",
        "raw_probe.effect_dispatches = 0;",
        "raw_probe.effect_chain_hash = 0;",
        "raw_probe.effect_suppressions = 0;",
        "raw_probe.pair_failures = 0;",
    )
    for forbidden in (
        "raw_probe.pending_count =",
        "raw_probe.map_active =",
        "raw_probe.map_generation =",
        "raw_probe.map_end_count =",
    ):
        assert forbidden not in raw_checkpoint_apply
    install = function_body(native, "void CG_NativeEventPresenterInstall()")
    assert "CG_EventRuntimeSetPresenter(&can_present, &present);" in install
    uninstall = function_body(
        native, "void CG_NativeEventPresenterUninstall()"
    )
    require_order(
        uninstall,
        "effect_authority_enabled = false;",
        "preflight_probe_latched = false;",
        "scrub_prepared();",
        "CG_EventRuntimeSetPresenter(nullptr, nullptr);",
    )
    set_effect_authority = function_body(
        native, "void CG_NativeEventPresenterSetEffectAuthority("
    )
    require_order(
        set_effect_authority,
        "effect_authority_enabled = enabled;",
        "scrub_prepared();",
    )
    init_game = function_body(cgame_main, "static void InitCGame()")
    require_order(
        init_game,
        "CG_NativeEventPresenterBeginMap();",
        "CG_CanonicalSnapshotRender_ResetStream();",
        "CG_CanonicalSnapshotTimelineInitialize();",
    )
    shutdown_game = function_body(
        cgame_main, "static void ShutdownCGame()"
    )
    require_order(
        shutdown_game,
        "CG_NativeEventPresenterEndMap();",
        "CG_LocalInteractionReset();",
    )
    parse_serverdata = function_body(
        parse, "static void CL_ParseServerData("
    )
    require_order(
        parse_serverdata,
        "CL_NativeDemoRecorderMapBoundary();",
        "if (cgame && cl.servercount != 0)",
        "cgame->Shutdown();",
        "CL_NativeReadinessPilotServerDataReset();",
        "CL_ClearState();",
        "CG_Load(cl.gamedir, cl.game_api == Q2PROTO_GAME_RERELEASE);",
        "cgame->Init();",
    )
    assert parse_serverdata.count("cgame->Shutdown();") == 1
    assert parse_serverdata.count("cgame->Init();") == 1
    entity_init_cvars = function_body(
        entity_api, "void CG_Entity_InitCvars("
    )
    assert "CG_NativeEventPresenterInitCvars();" in entity_init_cvars
    set_import = function_body(entity_api, "void CG_Entity_SetImport(")
    require_order(
        set_import,
        "CG_NativeEventPresenterUninstall();",
        "cgei = import;",
        "CG_Entity_InitCvars();",
        "CG_NativeEventPresenterInstall();",
    )
    get_extension = function_body(cgame_main, "static void *CG_GetExtension(")
    require_order(
        get_extension,
        "WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V1",
        "CG_GetNativeEventProbeAPI();",
        "WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V2",
        "CG_GetNativeEventProbeAPIv2();",
    )
    register_tent_sounds = function_body(
        temp_effects, "void CL_RegisterTEntSounds("
    )
    require_order(
        register_tent_sounds,
        "CG_NativeEventPresenterResourcesRequired();",
        'S_RegisterSound("misc/bigtele.wav")',
        "CL_RegisterNativeMuzzleSounds(prepare_native_effects);",
    )

    observe_active = function_body(readiness, "bool observe_server_active(")
    require_order(
        observe_active,
        "activate_native_session(next)",
        "pilot.readiness = next;",
        "set_event_presentation_owned(",
        "pilot.event_effect_cutover_confirmed",
    )
    assert "set_event_presentation_owned(true);" not in readiness
    for raw_owner in (readiness, parse, entities):
        assert "cg_native_event_preflight_probe" not in raw_owner
        assert "CG_NativeEventPresenterPreflightProbeEnabled" not in raw_owner
    assert "bool event_effect_cutover_confirmed{};" in readiness
    detach = function_body(readiness, "void detach_transport_fail_closed()")
    require_order(detach, "enter_drain(true);", "detach_owned_hooks();")
    assert "set_event_presentation_owned(false)" not in detach
    map_boundary = function_body(readiness, "bool enter_map_boundary()")
    assert "set_event_presentation_owned(false);" in map_boundary
    disable = function_body(readiness, "void disable_pilot()")
    assert "set_event_presentation_owned(false);" in disable
    owns_events = function_body(
        readiness,
        'extern "C" bool CL_NativeReadinessPilotOwnsEventPresentation(',
    )
    for contract in (
        "if (cls.demo.playback || cls.demo.seeking)",
        "disable_pilot();",
        "pilot.enabled && pilot.event_enabled &&",
        "pilot.event_presentation_owned",
    ):
        assert contract in owns_events
    assert "(void)live_pilot();" not in owns_events
    assert "return live_pilot();" not in owns_events

    # CL_ParseServerMessage has mutually exclusive preprocessor branches,
    # which a brace-only extractor cannot model. The case labels below occur
    # only in that parser, and their exact counts cover both compiled paths.
    server_message = parse
    sound_blocks = case_blocks(server_message, "Q2P_SVC_SOUND")
    assert len(sound_blocks) == 2
    for block in sound_blocks:
        require_probe_completion_case(
            block,
            "CL_ParseStartSoundPacket(",
            "S_ParseStartSound();",
            "WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2",
        )
    temp_blocks = case_blocks(server_message, "Q2P_SVC_TEMP_ENTITY")
    assert len(temp_blocks) == 2
    for block in temp_blocks:
        require_probe_completion_case(
            block,
            "CL_ParseTEntPacket(",
            "CL_ParseTEnt();",
            "WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2",
        )
    for label, side_effect, carrier_kind in (
        (
            "Q2P_SVC_MUZZLEFLASH",
            "CL_MuzzleFlash();",
            "WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2",
        ),
        (
            "Q2P_SVC_MUZZLEFLASH2",
            "CL_MuzzleFlash2();",
            "WORR_CGAME_EVENT_CARRIER_MONSTER_MUZZLE_V2",
        ),
    ):
        muzzle_blocks = case_blocks(server_message, label)
        assert len(muzzle_blocks) == 2
        for block in muzzle_blocks:
            require_probe_completion_case(
                block,
                "CL_ParseMuzzleFlashPacket(",
                side_effect,
                carrier_kind,
            )
    damage_blocks = [
        block
        for block in case_blocks(server_message, "Q2P_SVC_DAMAGE")
        if "CL_ParseDamage(" in block
    ]
    assert len(damage_blocks) == 1
    damage_block = damage_blocks[0]
    require_order(
        damage_block,
        "svc_msg.damage.count == 0",
        "WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2",
        "CL_EventRangeCaptureDamageV2(&svc_msg.damage);",
        "if (!CL_NativeReadinessPilotOwnsEventPresentation())",
        "CL_ParseDamage(&svc_msg.damage);",
        "} else {",
        "index < svc_msg.damage.count",
        "WORR_CGAME_EVENT_CARRIER_DAMAGE_V2",
        "WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_SUPPRESSED",
    )
    damage_presenter = function_body(
        parse, "static void CL_ParseDamage("
    )
    require_order(
        damage_presenter,
        "damage->count > WORR_CGAME_EVENT_DAMAGE_BATCH_MAX_V2",
        "i < damage->count",
        "SCR_AddToDamageDisplay(",
        "CL_CGameNativeEventProbeCompleteLegacyDispatch(",
        "WORR_CGAME_EVENT_CARRIER_DAMAGE_V2",
        "WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED",
    )
    damage_capture = function_body(
        client_event_shadow,
        'extern "C" void CL_EventRangeCaptureDamageV2(',
    )
    require_order(
        damage_capture,
        "Worr_LegacyDamageEventCandidatesBuildV1(",
        "for (std::uint32_t index = 0; index < candidate_count; ++index)",
        "candidates[index].subject_entity_index",
        "Worr_CGameEventRangeDeliverActionBatchV2(",
    )
    assert (
        "WORR_CGAME_EVENT_CARRIER_DAMAGE_V2, demo_range_flags_v2(),"
        in damage_capture
    )
    poi_blocks = [
        block
        for block in case_blocks(server_message, "Q2P_SVC_POI")
        if "CL_EventRangeCapturePOIV2(" in block
    ]
    assert len(poi_blocks) == 1
    poi_block = poi_blocks[0]
    require_order(
        poi_block,
        "CL_EventRangeCapturePOIV2(&svc_msg.poi);",
        "if (!captured ||",
        "!CL_NativeReadinessPilotOwnsEventPresentation())",
        "CL_ParsePOI(&svc_msg.poi);",
        "if (captured)",
        "WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_DISPATCHED",
        "} else {",
        "WORR_CGAME_NATIVE_EVENT_PROBE_LEGACY_SUPPRESSED",
    )
    assert poi_block.count(
        "CL_CGameNativeEventProbeCompleteLegacyDispatch("
    ) == 2
    assert poi_block.count(
        "WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2"
    ) == 2
    poi_capture = function_body(
        client_event_shadow,
        'extern "C" bool CL_EventRangeCapturePOIV2(',
    )
    require_order(
        poi_capture,
        "Worr_LegacyKeyedPOIEventCandidateBuildV1(",
        "synchronize_controlled_action_lineage(",
        "candidate.source_entity_index = 0;",
        "candidate.subject_entity_index = controlled_entity_index;",
        "candidate.record.source_entity = {WORR_EVENT_NO_ENTITY, 0};",
        "candidate.record.subject_entity = {WORR_EVENT_NO_ENTITY, 0};",
        "Worr_CGameEventRangeDeliverActionV2(",
        "if (result == WORR_CGAME_EVENT_RANGE_BUILD_DELIVERED_V2)",
        "return true;",
    )
    assert (
        "&builder_v2, &candidate,\n"
        "        WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2,"
    ) in poi_capture
    assert poi_capture.count("return true;") == 1
    assert "CL_CGameNativeEventProbeCompleteLegacyDispatch" not in poi_capture

    poi_prepare = function_body(native, "bool prepare_keyed_poi(")
    require_order(
        poi_prepare,
        "payload_copy<worr_event_payload_keyed_poi_v1>(record)",
        "ref_absent(record.subject_entity)",
        "if (!full_preflight_enabled())",
        "select_presentation_kind(presentation_kind_t::none);",
        "CG_CanPresentKeyedPOIValue(&payload, &prepared.keyed_poi)",
        "select_presentation_kind(presentation_kind_t::keyed_poi);",
    )
    assert "case WORR_EVENT_PAYLOAD_KEYED_POI_V1:" in native
    assert "supported = prepare_keyed_poi(*record, *context);" in native
    assert "case presentation_kind_t::keyed_poi:" in native
    assert "CG_PresentKeyedPOIValue(&prepared.keyed_poi);" in native

    poi_preflight = function_body(
        cgame_draw, "bool CG_CanPresentKeyedPOIValue("
    )
    require_order(
        poi_preflight,
        "const bool enabled = cl_pois && cl_pois->integer;",
        "WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS",
        "CG_PrepareKeyedPOIStateV1(",
        "CG_KEYED_POI_PRESENTATION_UPSERT_V1",
        "cgi.CL_GetPrecachedImageInfo(",
        "prepared_out->width = width;",
        "prepared_out->height = height;",
    )
    resource_tail = poi_preflight[
        poi_preflight.index("cgi.CL_GetPrecachedImageInfo(") :
    ]
    assert "return false;" not in resource_tail
    for forbidden in (
        "R_RegisterPic(",
        "CG_SetPOIImage(",
        "CG_PresentKeyedPOIValue(",
    ):
        assert forbidden not in poi_preflight
    poi_commit = function_body(
        cgame_draw, "void CG_PresentKeyedPOIValue("
    )
    require_order(
        poi_commit,
        "CG_KEYED_POI_PRESENTATION_NOOP_CAPACITY_V1",
        'cgi.Com_Print("couldn\'t add a POI\\n");',
        "CG_CommitKeyedPOIStateV1(poi_state[0], prepared);",
    )
    for forbidden in (
        "R_RegisterPic(",
        "CL_GetPrecachedImageInfo(",
        "CL_ClientTime(",
    ):
        assert forbidden not in poi_commit
    poi_state_preflight = function_body(
        native_header, "bool CG_PrepareKeyedPOIStateV1("
    )
    require_order(
        poi_state_preflight,
        "*prepared_out = {};",
        "CG_KEYED_POI_PRESENTATION_NOOP_DISABLED_V1",
        "WORR_EVENT_KEYED_POI_REMOVE_LIFETIME_MS",
        "CG_KEYED_POI_PRESENTATION_DELETE_V1",
        "CG_KEYED_POI_PRESENTATION_NOOP_MISSING_V1",
        "CG_SelectKeyedPOISlotV1(",
        "CG_KEYED_POI_PRESENTATION_NOOP_CAPACITY_V1",
        "CG_KEYED_POI_PRESENTATION_UPSERT_V1",
    )
    assert "const Slots &slots" in poi_state_preflight
    assert "CG_CommitKeyedPOIStateV1" not in poi_state_preflight
    poi_state_commit = function_body(
        native_header, "bool CG_CommitKeyedPOIStateV1("
    )
    require_order(
        poi_state_commit,
        "switch (prepared->disposition)",
        "CG_KEYED_POI_PRESENTATION_NOOP_CAPACITY_V1",
        "CG_KEYED_POI_PRESENTATION_DELETE_V1",
        "CG_KEYED_POI_PRESENTATION_UPSERT_V1",
        "slot = {};",
        "slot.id = prepared->payload.key;",
        "slot.infinite = prepared->payload.lifetime_ms == 0;",
        "std::memcpy(slot.image_name, prepared->image_name,",
    )
    real_sink_test = function_body(
        presenter_test, "void test_real_keyed_poi_sink_state()"
    )
    for contract in (
        "prepared.image_name[0] == '\\0'",
        "CG_KEYED_POI_PRESENTATION_NOOP_DISABLED_V1",
        "CG_KEYED_POI_PRESENTATION_NOOP_CAPACITY_V1",
        "CG_KEYED_POI_PRESENTATION_DELETE_V1",
        "CG_KEYED_POI_PRESENTATION_NOOP_MISSING_V1",
        "std::memcmp(&slots",
        "CG_CommitKeyedPOIStateV1(slots, &prepared)",
    ):
        assert contract in real_sink_test
    image_preflight = function_body(
        cgame_client, "static bool CG_CL_GetPrecachedImageInfo("
    )
    require_order(
        image_preflight,
        "cl.image_precache[image_index]",
        "R_GetPicSize(width_out, height_out, image);",
        "*name_out = name;",
    )
    assert "R_RegisterPic(" not in image_preflight

    projection_hash = function_body(
        event_shadow, "bool presentation_projection_hash("
    )
    require_order(
        projection_hash,
        "snapshot.snapshot_id.sequence",
        "snapshot.snapshot_hash",
        "record.payload_kind == WORR_EVENT_PAYLOAD_KEYED_POI_V1",
        "keyed_poi_presentation_projection_hash(",
        "normalized.source_time_us = 0;",
        "Worr_EventRecordSemanticHashV1(",
    )
    assert projection_hash.count("snapshot.snapshot_id.sequence") == 2
    assert projection_hash.count("snapshot.snapshot_hash") == 2
    poi_projection_hash = function_body(
        event_shadow, "bool keyed_poi_presentation_projection_hash("
    )
    require_order(
        poi_projection_hash,
        "const auto &controlled = snapshot.controlled_entity.identity;",
        "record.source_entity.index != 0",
        "record.source_entity.generation != 1",
        "record.subject_entity.index != controlled.index",
        "record.subject_entity.generation != controlled.generation",
        "worr_event_record_v1 neutral = record;",
        "neutral.flags = WORR_EVENT_FLAG_REPLAY_SAFE |",
        "neutral.event_id = {};",
        "neutral.source_tick = 0;",
        "neutral.source_ordinal = 0;",
        "neutral.source_time_us = 0;",
        "neutral.source_entity = {0, 1};",
        "neutral.subject_entity = {WORR_EVENT_NO_ENTITY, 0};",
        "neutral.prediction_key = {};",
        "neutral.delivery_class = WORR_EVENT_DELIVERY_RELIABLE_ORDERED;",
        "neutral.expiry_tick = 0;",
        "Worr_EventRecordCandidateValidateV1(",
        "Worr_EventRecordSemanticHashV1(",
        "WORR-KEYED-POI-PRESENTATION-PARITY-V1",
        "parity_hash_u32(hash, snapshot.snapshot_id.epoch);",
        "parity_hash_u64(hash, semantic_hash);",
    )
    for forbidden in (
        "snapshot.snapshot_id.sequence",
        "snapshot.snapshot_hash",
        "record.source_tick",
        "record.source_time_us",
    ):
        assert forbidden not in poi_projection_hash
    raw_action_kind = function_body(event_shadow, "bool raw_action_kind(")
    assert "WORR_CGAME_EVENT_CARRIER_KEYED_POI_V2" in raw_action_kind
    assert "WORR_EVENT_PAYLOAD_KEYED_POI_V1" in raw_action_kind

    help_blocks = [
        block
        for block in case_blocks(server_message, "Q2P_SVC_HELP_PATH")
        if "CL_AddHelpPath(" in block
    ]
    assert len(help_blocks) == 1
    require_order(
        help_blocks[0],
        "if (!CL_NativeReadinessPilotOwnsEventPresentation())",
        "CL_AddHelpPath(",
    )

    entity_cvars = function_body(
        entities, "void CG_CanonicalSnapshotRender_InitCvars("
    )
    assert '"cl_worr_native_event_presentation_owned"' in entity_cvars
    embedded_event = function_body(entities, "static void parse_entity_event(")
    require_order(
        embedded_event,
        "CG_SnapshotTimeline_ShouldDispatchEvent(",
        "cl_worr_native_event_presentation_owned &&",
        "cl_worr_native_event_presentation_owned->integer",
        "present_entity_impulse_event(number, cent->current.event, nullptr);",
    )

    load_cgame = function_body(cgame_client, "void CG_Load(")
    require_order(
        load_cgame,
        "CL_CGameEventRuntimeSetConsumer(event_runtime)",
        "cgame_native_event_probe = NULL;",
        "cgame_native_event_probe_v2 = NULL;",
        "WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V1",
        "!cgame_native_event_probe->CompleteLegacyDispatch",
        "!cgame_native_event_probe->GetStatus",
        "WORR_CGAME_NATIVE_EVENT_PROBE_EXPORT_V2",
        "!cgame_native_event_probe_v2->CompleteLegacyDispatch",
        "!cgame_native_event_probe_v2->GetStatus",
        "!cgame_native_event_probe_v2->Checkpoint",
        "CL_SnapshotShadowSetConsumer(snapshot_timeline)",
    )
    unload_cgame = function_body(cgame_client, "void CG_Unload(void)")
    require_order(
        unload_cgame,
        "cgame_native_event_probe = NULL;",
        "cgame_native_event_probe_v2 = NULL;",
        "CL_EventRangeSetConsumerV2(NULL);",
        "Sys_FreeLibrary(cgame_library);",
    )
    complete_bridge = function_body(
        cgame_client,
        "bool CL_CGameNativeEventProbeCompleteLegacyDispatch(",
    )
    assert "cgame_native_event_probe->CompleteLegacyDispatch(" in complete_bridge
    status_bridge = function_body(
        cgame_client, "bool CL_CGameNativeEventProbeGetStatus("
    )
    assert "cgame_native_event_probe->GetStatus(status_out)" in status_bridge
    checkpoint_bridge = function_body(
        cgame_client, "bool CL_CGameNativeEventProbeCheckpoint("
    )
    require_order(
        checkpoint_bridge,
        "receipt_out && cgame_native_event_probe_v2",
        "cgame_native_event_probe_v2->Checkpoint(",
        "expected_map_generation, expected_authority_epoch,",
        "checkpoint_id, receipt_out",
    )

    probe_status_command = function_body(
        input_client, "static void CL_NativeEventProbeStatus_f(void)\n{"
    )
    assert (
        '"WORR_NATIVE_EVENT_PROBE_STATUS_V1 valid=0\\n"'
        in probe_status_command
    )
    assert probe_status_command.count("%016llx") == 4
    assert "0x%016llx" not in probe_status_command
    for key in (
        "valid", "schema", "size", "kind_count", "map_generation",
        "map_end_count", "map_active", "probe_requested",
        "probe_latched", "probe_active", "effect_authority_enabled",
        "resources_required", "legacy_owner_active", "raw_pending_count",
        "authority_epoch", "authority_requires_resync",
        "authority_degraded", "raw_action_records",
        "raw_action_chain_hash", "raw_effect_dispatches",
        "raw_effect_chain_hash", "raw_effect_suppressions",
        "raw_pair_failures", "probe_action_commits",
        "probe_action_chain_hash", "probe_effects_suppressed",
        "probe_nonvisual_commits", "native_effect_dispatches",
        "native_effect_chain_hash", "presenter_commit_mismatches",
        "authoritative_presentations", "authoritative_duplicates",
        "authoritative_conflicts", "authority_ref_body_joins",
        "legacy_ref_body_mismatches", "raw_k0", "raw_k1", "raw_k2",
        "raw_k3", "raw_k4", "raw_k5", "raw_k6", "raw_k7", "probe_k0",
        "probe_k1", "probe_k2", "probe_k3", "probe_k4", "probe_k5",
        "probe_k6", "probe_k7",
    ):
        assert f"{key}=" in probe_status_command
    assert (
        '{ "cl_worr_native_event_probe_status", '
        "CL_NativeEventProbeStatus_f }"
    ) in input_client
    decimal_parser = function_body(
        input_client, "static bool CL_ParseNativeEventProbeDecimal("
    )
    require_order(
        decimal_parser,
        "if (!text || !text[0] || !value_out)",
        "if (*cursor < '0' || *cursor > '9')",
        "if (value > (maximum - digit) / 10u)",
        "*value_out = value;",
    )
    checkpoint_command = function_body(
        input_client, "static void CL_NativeEventProbeCheckpoint_f(void)\n{"
    )
    require_order(
        checkpoint_command,
        "Cmd_Argc() != 4",
        "Cmd_Argv(1), UINT32_MAX, &map_generation",
        "Cmd_Argv(2), UINT32_MAX, &authority_epoch",
        "Cmd_Argv(3), UINT64_MAX, &checkpoint_id",
        "CL_PrintNativeEventProbeCheckpoint(false, NULL);",
        "CL_CGameNativeEventProbeCheckpoint(",
        "receipt.struct_size == sizeof(receipt)",
        "receipt.schema_version ==",
        "receipt.reserved0 == 0",
        "CL_PrintNativeEventProbeCheckpoint(valid, valid ? &receipt : NULL);",
    )
    checkpoint_print = function_body(
        input_client, "static void CL_PrintNativeEventProbeCheckpoint("
    )
    for field in (
        "valid=%u",
        "schema=%u",
        "size=%u",
        "result=%u",
        "map_generation=%u",
        "authority_epoch=%u",
        "checkpoint_id=%llu",
    ):
        assert field in checkpoint_print
    assert checkpoint_print.count("WORR_NATIVE_EVENT_PROBE_CHECKPOINT_V1") == 1
    assert (
        '{ "cl_worr_native_event_probe_checkpoint",\n'
        "      CL_NativeEventProbeCheckpoint_f }"
    ) in input_client

    server_shadow_status = function_body(
        server_commands, "static void SV_WorrNativeShadowStatus_f(void)"
    )
    require_order(
        server_shadow_status,
        "SV_NativeShadowGetEventStatusV1(",
        '"WORR_NATIVE_SERVER_EVENT_STATUS_V1 schema=%u "',
        "event_status.retries",
    )

    print(
        "native_event_presenter_source_contract "
        "two_phase=ordered fence=exact probe=map_latched_full_preflight "
        "ownership=explicit raw=capture_first "
        f"inventory=v{inventory['schema_version']}/"
        f"{len(inventory_entries)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
