#!/usr/bin/env python3
"""Source contract for the production-bound FR-10-T07 render policy."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ENTITIES = (ROOT / "src/game/cgame/cg_entities.cpp").read_text(
    encoding="utf-8"
)
LIVE_GATE = (
    ROOT / "tools/networking/run_canonical_rail_damage_runtime_gate.py"
).read_text(encoding="utf-8")
SOUND_MAIN = (ROOT / "src/client/sound/main.cpp").read_text(encoding="utf-8")
SOUND_DMA = (ROOT / "src/client/sound/dma.cpp").read_text(encoding="utf-8")
SOUND_AL = (ROOT / "src/client/sound/al.cpp").read_text(encoding="utf-8")
SNAPSHOT_SHADOW = (ROOT / "src/client/snapshot_shadow.cpp").read_text(
    encoding="utf-8"
)
CGAME_BRIDGE = (ROOT / "src/client/cgame.cpp").read_text(encoding="utf-8")
COMMON = (ROOT / "src/common/common.c").read_text(encoding="utf-8")
CG_MAIN = (ROOT / "src/game/cgame/cg_main.cpp").read_text(encoding="utf-8")


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


class CanonicalSnapshotRenderPolicyContractTests(unittest.TestCase):
    def test_native_policy_is_explicit_bounded_and_default_off(self) -> None:
        init = function_body(
            ENTITIES, "void CG_CanonicalSnapshotRender_InitCvars(void)"
        )
        for declaration in (
            '"cg_snapshot_timeline_render", "0", CVAR_NOARCHIVE',
            '"cg_snapshot_timeline_interpolation_delay_ms", "50"',
            '"cg_snapshot_timeline_adaptive_interpolation", "1"',
            '"cg_snapshot_timeline_max_interpolation_delay_ms", "150"',
            '"cg_snapshot_timeline_max_extrapolation_ms", "50"',
        ):
            self.assertIn(declaration, init)

        begin = function_body(
            ENTITIES, "void begin_canonical_snapshot_render_frame()"
        )
        normalized = " ".join(begin.split())
        self.assertIn(
            "cl_worr_native_snapshot_timeline_owned && "
            "cl_worr_native_snapshot_timeline_owned->integer != 0",
            normalized,
        )
        self.assertIn(
            "Cvar_ClampValue( cg_snapshot_timeline_interpolation_delay_ms, "
            "0.0f, 1000.0f)",
            normalized,
        )
        self.assertIn(
            "Cvar_ClampValue( cg_snapshot_timeline_max_extrapolation_ms, "
            "0.0f, 250.0f)",
            normalized,
        )
        self.assertIn(
            "clock.render_time_us - interpolation_delay_us", begin
        )
        self.assertIn("update_canonical_interpolation_delay(", begin)
        self.assertIn(
            "policy.max_extrapolation_us = max_extrapolation_us;", begin
        )
        self.assertIn(
            "policy.allow_extrapolation = "
            "max_extrapolation_us != 0 ? 1u : 0u;",
            normalized,
        )
        self.assertNotIn("latest.server_time_us", begin)

    def test_native_authority_waits_for_first_bind_then_fails_closed(self) -> None:
        begin = function_body(
            ENTITIES, "void begin_canonical_snapshot_render_frame()"
        )
        normalized = " ".join(begin.split())
        for contract in (
            "CG_ResolveCanonicalNativeAuthorityV1(",
            "CG_CANONICAL_NATIVE_AUTHORITY_PREBIND_LEGACY",
            "CG_CANONICAL_NATIVE_AUTHORITY_FIRST_BIND",
            "CG_CANONICAL_NATIVE_AUTHORITY_POST_BIND_LOSS",
            "canonical_render_stats.native_authority_blocks",
        ):
            self.assertIn(contract, begin)
        self.assertIn(
            "first_native_bind || canonical_render_stats.epoch != "
            "diagnostics.active_epoch",
            normalized,
        )
        self.assertIn(
            "canonical_render_frame.mode = "
            "static_cast<std::uint32_t>(resolved_mode);",
            normalized,
        )

        prepare_audio = function_body(
            ENTITIES, "void CL_PrepareLoopSoundEntities(void)"
        )
        self.assertIn("canonical_render_frame.requested_mode", prepare_audio)

    def test_whole_stream_init_reopens_the_prebind_wait(self) -> None:
        reset = function_body(
            ENTITIES, "void CG_CanonicalSnapshotRender_ResetStream(void)"
        )
        for contract in (
            "CG_ResetCanonicalNativeAuthorityV1(",
            "CG_ResetCanonicalInterpolationDelayV1(",
            "canonical_interpolation_pair_feedback = {};",
            "canonical_render_frame = {};",
            "reset_canonical_native_render_view();",
            "reset_canonical_render_stats(0u);",
        ):
            self.assertIn(contract, reset)

        init = function_body(CG_MAIN, "static void InitCGame()")
        self.assertLess(
            init.index("CG_CanonicalSnapshotRender_ResetStream();"),
            init.index("CG_CanonicalSnapshotTimelineInitialize();"),
        )

    def test_legacy_demo_is_a_whole_stream_authority_fallback(self) -> None:
        effective = function_body(
            ENTITIES, "int effective_canonical_snapshot_render_mode()"
        )
        self.assertIn(
            "configured_mode == CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY",
            effective,
        )
        self.assertIn("cls.demo.playback || cls.demo.seeking", effective)
        self.assertIn("return CANONICAL_SNAPSHOT_RENDER_LEGACY;", effective)

        begin = function_body(
            ENTITIES, "void begin_canonical_snapshot_render_frame()"
        )
        prepare_audio = function_body(
            ENTITIES, "void CL_PrepareLoopSoundEntities(void)"
        )
        self.assertIn("effective_canonical_snapshot_render_mode()", begin)
        self.assertIn(
            "effective_canonical_snapshot_render_mode()", prepare_audio
        )

    def test_policy_retains_discontinuity_and_motion_guards(self) -> None:
        sample = function_body(
            ENTITIES, "bool sample_canonical_entity_transform("
        )
        for guard in (
            "WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_SNAPSHOT",
            "WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_GENERATION",
            "WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_MISSING",
            "WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_COMPONENT",
            "WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_TELEPORT",
            "WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_LINEAR_SPEED",
            "WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_ANGULAR_SPEED",
            "WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_DISCRETE_TRANSITION",
            "WORR_SNAPSHOT_TIMELINE_ENTITY_BLOCK_POLICY",
        ):
            self.assertIn(guard, sample)

    def test_cumulative_mode_and_duration_telemetry_is_emitted(self) -> None:
        begin = function_body(
            ENTITIES, "void begin_canonical_snapshot_render_frame()"
        )
        end = function_body(
            ENTITIES, "void end_canonical_snapshot_render_frame()"
        )
        for contract in (
            "WORR_SNAPSHOT_TIMELINE_PAIR_INTERPOLATE",
            "canonical_render_stats.interpolation_frames",
            "WORR_SNAPSHOT_TIMELINE_PAIR_EXTRAPOLATE",
            "canonical_render_stats.extrapolation_frames",
            "canonical_render_stats.extrapolation_time_us",
            "WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_EARLIEST",
            "WORR_SNAPSHOT_TIMELINE_PAIR_CLAMP_LATEST",
            "canonical_render_stats.clamped_frames",
        ):
            self.assertIn(contract, begin)
        self.assertIn(
            '"timeline_modes=%llu/%llu/%llu extrap_us=%llu "', end
        )
        self.assertIn(
            '"enumeration=%llu/%llu/%llu/%llu resets=%llu "', end
        )
        self.assertIn(
            '"previous_only=%llu/%llu/%llu "', end
        )
        self.assertIn(
            '"view=%u/%u/%u submission=%llu/%llu/%016llx\\n"', end
        )
        self.assertIn(
            '"cg_snapshot_timeline_adaptive: enabled=%u adjustment=%u "',
            end,
        )
        self.assertIn("canonical_interpolation_delay_status.delay_us", end)
        self.assertIn("canonical_render_stats.adaptive_delay_failures", end)

    def test_native_enumeration_is_value_owned_and_fails_closed(self) -> None:
        prepare = function_body(
            ENTITIES, "bool prepare_canonical_native_render_view()"
        )
        for contract in (
            "CG_CanonicalSnapshotTimelineCopyEntities(",
            "canonical_render_frame.pair.previous",
            "canonical_render_frame.pair.current",
            "CG_BuildCanonicalRenderEntitiesV1(",
            "CG_CanonicalSnapshotTimelineSampleEntity(",
            "CG_SelectCanonicalRenderSampleV1(",
            "view.removed_count = removed_count;",
            "view.render_states[render_count] = selected.state;",
            "view.render_provenances[render_count] = selected.provenance;",
            "entry.provenance == view.render_provenances[index]",
            "view.render_modes[render_count] = sample.mode;",
            "view.render_previous_only[render_count] =",
            "view.selected_previous_only_count = "
            "selected_previous_only_count;",
            "CG_ResolveCanonicalRenderLifecycleV1(",
            "CG_CanonicalRenderEndpointKeyV1(",
            "entry.pending_discontinuity_blocks",
            "entry.pending_discontinuity_key",
            "entry.handled_discontinuity_key",
            "view.render_lifecycle_resets[index]",
            "lifecycle.pending_blocking_reasons",
            "lifecycle.handled_discontinuity_key",
        ):
            self.assertIn(contract, prepare)

        packet = function_body(ENTITIES, "static void CL_AddPacketEntities(")
        normalized = " ".join(packet.split())
        self.assertIn(
            "if (native_authority && !canonical_native_render_view.valid) "
            "return;",
            normalized,
        )
        self.assertIn(
            "canonical_native_render_view.render_identities[pnum]", packet
        )
        self.assertIn("s1 = &cent->current;", packet)
        self.assertIn(
            "canonical_native_render_view.render_previous_only[pnum]",
            packet,
        )
        self.assertIn("} else {", packet)
        self.assertIn("s1 = &cl.entityStates[i];", packet)

    def test_previous_only_evidence_is_value_owned_ordered_and_submitted(self) -> None:
        prepare = function_body(
            ENTITIES, "bool prepare_canonical_native_render_view()"
        )
        reset = function_body(
            ENTITIES, "void reset_canonical_native_render_view()"
        )
        fail = function_body(
            ENTITIES, "bool fail_canonical_native_render_view("
        )
        submit = function_body(
            ENTITIES, "void record_canonical_renderer_submission("
        )
        packet = function_body(ENTITIES, "static void CL_AddPacketEntities(")

        self.assertIn(
            "canonical_render_stats.enumerated_removed_entities", prepare
        )
        self.assertIn(
            "canonical_render_stats.selected_visible_previous_only_sources",
            prepare,
        )
        self.assertIn(
            "view.render_previous_only[render_count] =", prepare
        )
        self.assertLess(
            prepare.index("if (!sample.visible)"),
            prepare.index("view.render_previous_only[render_count] ="),
        )
        for body in (prepare, reset, fail):
            self.assertIn("render_previous_only.fill(0u);", body)

        self.assertIn(
            "renderer_previous_only_submitted_sources", submit
        )
        self.assertIn(
            "CG_CanonicalPreviousOnlyEvidenceOrderedV1(", submit
        )
        self.assertLess(
            packet.index("V_AddEntity(submitted);"),
            packet.index("record_canonical_renderer_submission("),
        )

    def test_controlled_prediction_precedes_canonical_remote_transform(self) -> None:
        packet = function_body(ENTITIES, "static void CL_AddPacketEntities(")
        predicted_origin = packet.index(
            "if (s1->number == controlled_entity_number)"
        )
        canonical_remote_origin = packet.index(
            "else if (canonical_transform)", predicted_origin
        )
        self.assertLess(predicted_origin, canonical_remote_origin)
        predicted_angles = packet.index(
            "} else if (s1->number == controlled_entity_number)",
            canonical_remote_origin,
        )
        canonical_remote_angles = packet.index(
            "} else if (canonical_transform)", predicted_angles
        )
        self.assertLess(predicted_angles, canonical_remote_angles)

        sound = function_body(ENTITIES, "void CL_GetEntitySoundOrigin(")
        self.assertIn("canonical_native_render_view.cache[entnum]", sound)

    def test_native_loop_audio_uses_the_same_value_owned_phase(self) -> None:
        copy = function_body(ENTITIES, "int CL_CopyLoopSoundEntities(")
        self.assertIn("CANONICAL_SNAPSHOT_RENDER_NATIVE_AUTHORITY", copy)
        self.assertIn("canonical_native_render_view.render_states.data()", copy)
        self.assertIn("return -1;", copy)

        build = function_body(SOUND_MAIN, "int S_BuildSoundList(")
        self.assertIn("CL_CopyEntityLoopSoundStates(", build)
        self.assertIn("s_loop_sound_entities[i] = cl.entityStates[num];", build)
        self.assertIn("if (source_count < 0)", build)
        for backend in (SOUND_DMA, SOUND_AL):
            self.assertIn("S_LoopSoundEntityCount()", backend)
            self.assertIn("S_LoopSoundEntity(", backend)

    def test_all_native_audio_uses_lifecycle_bound_canonical_origins(self) -> None:
        update = function_body(SOUND_MAIN, "void S_Update(void)")
        self.assertLess(
            update.index("CL_PrepareEntityLoopSoundStates();"),
            update.index("s_api->update();"),
        )

        issue = function_body(SOUND_MAIN, "void S_IssuePlaysound(")
        start = function_body(SOUND_MAIN, "void S_StartSound(")
        bind = function_body(SOUND_MAIN, "bool S_BindChannelEntity(")
        self.assertIn("CL_GetEntitySoundBinding(entnum, &binding)", start)
        self.assertIn("ps->native_binding = true", start)
        self.assertIn(
            "CL_GetEntitySoundOriginBound(entnum, binding, ps->origin)",
            start,
        )
        self.assertIn(
            "attenuation != ATTN_NONE && entnum != -1", start
        )
        self.assertIn("entnum != listener_entnum", start)
        allocation = start.index("ps = S_AllocPlaysound();")
        clear_binding = start.index("ps->entity_binding = 0;")
        origin_branch = start.index("if (origin)")
        self.assertLess(allocation, clear_binding)
        self.assertLess(clear_binding, origin_branch)
        self.assertLess(
            start.index("ps->native_binding = false;"), origin_branch
        )
        self.assertIn("CL_GetEntitySoundOriginBound(", issue)
        self.assertIn(
            "ps->native_binding && ps->entity_binding == 0", issue
        )
        self.assertLess(
            issue.index("ps->native_binding && ps->entity_binding == 0"),
            issue.index("S_PickChannel("),
        )
        self.assertIn("(void)CL_GetEntitySoundOriginBound(", issue)
        self.assertIn("CL_GetEntitySoundBinding(channel->entnum, &binding)", bind)
        self.assertIn("CL_GetEntitySoundOriginBound(", bind)
        self.assertIn(
            "channel->native_binding && channel->entity_binding != binding",
            bind,
        )

        add_loop_entity = function_body(
            SOUND_AL, "static void AL_AddLoopSoundEntity("
        )
        add_loops = function_body(SOUND_AL, "static void AL_AddLoopSounds(void)")
        for loop_path in (add_loop_entity, add_loops):
            self.assertIn("AL_StopChannel(ch);", loop_path)
            self.assertIn("ch = nullptr;", loop_path)
            self.assertLess(
                loop_path.index("ch = nullptr;"),
                loop_path.index("ch = S_PickChannel(0, 0)"),
            )

        binding = function_body(
            ENTITIES, "int CL_GetEntitySoundBinding("
        )
        bound_origin = function_body(
            ENTITIES, "bool CL_GetEntitySoundOriginBound("
        )
        self.assertIn("entry.entity.id", binding)
        self.assertIn("entry.generation", binding)
        self.assertIn("entry.entity.id", bound_origin)
        self.assertIn("entry.generation", bound_origin)

        reset_view = function_body(
            ENTITIES, "void reset_canonical_native_render_view()"
        )
        self.assertNotIn("next_entity_id = 0", reset_view)

        for backend in (SOUND_DMA, SOUND_AL):
            self.assertIn("ch->native_binding", backend)
            self.assertIn("CL_GetEntitySoundOriginBound(", backend)
        self.assertIn("S_BindChannelEntity(ch)", SOUND_AL)

    def test_anchor_and_render_clock_share_unscaled_64_bit_host_time(self) -> None:
        host = function_body(SNAPSHOT_SHADOW, "uint64_t host_time_us()")
        self.assertIn("return com_unscaledTimeUs;", host)
        self.assertNotIn("cls.realtime", host)
        self.assertIn(".host_realtime_us = &com_unscaledTimeUs", CGAME_BRIDGE)
        self.assertIn("com_unscaledTimeUs += (uint64_t)msec * 1000u", COMMON)
        frame = function_body(COMMON, "void Qcommon_Frame(void)")
        self.assertIn("unscaled_msec = msec;", frame)
        self.assertLess(
            frame.index("unscaled_msec = msec;"),
            frame.index("if (msec > 250)"),
        )
        self.assertIn("(uint64_t)unscaled_msec * 1000u", frame)
        advance = function_body(
            ENTITIES,
            "worr_snapshot_timeline_result_v1 advance_canonical_render_clock(",
        )
        self.assertIn("cgei->host_realtime_us", advance)
        self.assertIn("CG_BuildCanonicalRenderClockPlanV1(", advance)

        begin = function_body(
            ENTITIES, "void begin_canonical_snapshot_render_frame()"
        )
        self.assertIn("CG_ResolveCanonicalLegacyAlignmentV1(", begin)
        self.assertIn("desired_time_us = aligned_time_us;", begin)

    def test_headless_live_gate_pins_the_policy(self) -> None:
        for argument in (
            '"+set", "win_headless", "1"',
            '"+set", "in_enable", "0"',
            '"+set", "cg_snapshot_timeline_render", "3"',
            '"+set", "cg_snapshot_timeline_interpolation_delay_ms", "50"',
            '"+set", "cg_snapshot_timeline_max_extrapolation_ms", "50"',
        ):
            self.assertIn(argument, LIVE_GATE)


if __name__ == "__main__":
    unittest.main()
