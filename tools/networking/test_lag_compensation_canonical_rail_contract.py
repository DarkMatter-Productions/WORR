#!/usr/bin/env python3
"""Source contract for the production-owned canonical rail acceptance seam."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LAG = (ROOT / "src/game/sgame/network/lag_compensation.cpp").read_text(encoding="utf-8")
LAG_HEADER = (ROOT / "src/game/sgame/network/lag_compensation.hpp").read_text(encoding="utf-8")
WEAPON = (ROOT / "src/game/sgame/gameplay/g_weapon.cpp").read_text(encoding="utf-8")
PLAYER_WEAPON = (ROOT / "src/game/sgame/player/p_weapon.cpp").read_text(encoding="utf-8")
PROBALL = (ROOT / "src/game/sgame/gameplay/g_proball.cpp").read_text(encoding="utf-8")
CLIENT_THINK = (ROOT / "src/game/sgame/client/client_session_service_impl.cpp").read_text(encoding="utf-8")
SVCMDS = (ROOT / "src/game/sgame/gameplay/g_svcmds.cpp").read_text(encoding="utf-8")
GAME_IMPORT_C = (ROOT / "inc/shared/game.h").read_text(encoding="utf-8")
GAME_IMPORT_CPP = (ROOT / "src/game/bgame/game.hpp").read_text(encoding="utf-8")
SERVER_GAME = (ROOT / "src/server/game.c").read_text(encoding="utf-8")
CLIENT_INPUT = (ROOT / "src/client/input.cpp").read_text(encoding="utf-8")
PROTOCOL = (ROOT / "inc/common/protocol.h").read_text(encoding="utf-8")


class CanonicalRailDamageContractTests(unittest.TestCase):
    def test_pose_history_uses_the_engine_snapshot_time_domain(self) -> None:
        self.assertIn("uint64_t (*ServerSimulationTimeUs)(void);", GAME_IMPORT_C)
        self.assertIn("uint64_t\t(*ServerSimulationTimeUs)();", GAME_IMPORT_CPP)
        provider = SERVER_GAME[
            SERVER_GAME.index("static uint64_t PF_ServerSimulationTimeUs(void)") :
            SERVER_GAME.index("static void PF_SendToClipboard")
        ]
        self.assertIn("return sv.worr_server_time_us;", provider)
        self.assertIn(".ServerSimulationTimeUs = PF_ServerSimulationTimeUs,", SERVER_GAME)

        current_clock = LAG[
            LAG.index("[[nodiscard]] bool CurrentAuthoritativeTimeUs") :
            LAG.index("[[nodiscard]] bool CompletedAuthoritativeFrameTimeUs")
        ]
        self.assertIn("if (!gi.ServerSimulationTimeUs)", current_clock)
        self.assertIn("timeUs = gi.ServerSimulationTimeUs();", current_clock)
        self.assertNotIn("level.time", current_clock)

    def test_end_frame_pose_capture_timestamps_completed_engine_frames(self) -> None:
        completed_clock = LAG[
            LAG.index("[[nodiscard]] bool CompletedAuthoritativeFrameTimeUs") :
            LAG.index("[[nodiscard]] Vector3 ToVector")
        ]
        self.assertIn("CurrentAuthoritativeTimeUs(currentTimeUs)", completed_clock)
        self.assertIn("static_cast<uint64_t>(gi.frameTimeMs) * UINT64_C(1000)", completed_clock)
        self.assertIn("timeUs = currentTimeUs + frameTimeUs;", completed_clock)

        player_capture = LAG[
            LAG.index("void LagCompensation_RecordFrame") :
            LAG.index("void LagCompensation_RecordMovers")
        ]
        self.assertIn("CompletedAuthoritativeFrameTimeUs(timeUs)", player_capture)
        self.assertIn("!track.initialized &&", player_capture)
        self.assertIn("InitTrack(track, static_cast<uint32_t>(index + 1u))", player_capture)

        mover_capture = LAG[
            LAG.index("void LagCompensation_RecordMovers") :
            LAG.index("bool LagCompensation_RunHistoricalBrushRuntimeProbe")
        ]
        self.assertIn("CompletedAuthoritativeFrameTimeUs(timeUs)", mover_capture)

    def test_fixture_requires_real_active_command_scope_and_attack_bit(self) -> None:
        prepare = LAG[LAG.rindex("void CanonicalRailProbePrepareCommand"):LAG.rindex("bool CanonicalRailProbeArm()")]
        for required in (
            "WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID",
            "commandContextImport->GetCurrent",
            "context.client_index != ClientIndex(entity)",
            "(command->buttons & BUTTON_ATTACK) == 0",
            "canonicalRailProbe.command_id = context.command.command_id",
            "entity->client->buttons = BUTTON_NONE",
        ):
            self.assertIn(required, prepare)
        self.assertIn("entity->s.modelIndex = MODELINDEX_PLAYER", LAG)
        self.assertIn("entity->client->pers.healthBonus = 0", LAG)
        self.assertNotIn("fire_rail(", prepare)

    def test_normal_weapon_callback_remains_the_only_fixture_fire_route(self) -> None:
        prepare = LAG[LAG.rindex("void CanonicalRailProbePrepareCommand"):LAG.rindex("bool CanonicalRailProbeArm()")]
        self.assertIn("weapon->weaponThink", prepare)
        self.assertIn("LagCompensation_PrepareCanonicalWeaponDamageCommand(ent, ucmd)", CLIENT_THINK)
        self.assertLess(
            CLIENT_THINK.index("LagCompensation_PrepareCanonicalWeaponDamageCommand(ent, ucmd)"),
            CLIENT_THINK.index("cl->buttons = ucmd->buttons"),
        )
        self.assertLess(
            CLIENT_THINK.index(
                "LagCompensation_RecordDeferredProjectileForwardCommand(ent, ucmd)"
            ),
            CLIENT_THINK.index("cl->buttons = ucmd->buttons"),
        )
        self.assertIn("worr_rewind_canonical_rail_damage_arm", SVCMDS)
        self.assertIn(
            "worr_rewind_canonical_rail_mover_occlusion_arm", SVCMDS
        )
        self.assertIn("worr_rewind_canonical_machinegun_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_chaingun_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_super_shotgun_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_disruptor_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_rocket_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_bfg_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_ion_ripper_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_tesla_mine_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_trap_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_grapple_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_proball_throw_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_rocket_splash_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_plasma_gun_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_plasma_gun_splash_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_blaster_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_hyperblaster_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_chainfist_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_etf_rifle_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_phalanx_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_phalanx_splash_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_plasma_beam_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_plasma_beam_held_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_plasma_beam_sustained_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_plasma_beam_release_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_plasma_beam_water_retrace_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_thunderbolt_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_thunderbolt_held_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_thunderbolt_sustained_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_thunderbolt_release_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_thunderbolt_water_retrace_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_thunderbolt_discharge_damage_arm", SVCMDS)
        self.assertIn("worr_rewind_canonical_shotgun_damage_arm", SVCMDS)

    def test_real_command_railgun_can_be_stopped_by_a_sealed_historical_mover(
        self,
    ) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalRailMoverOcclusionProbeArm") :
            LAG.rindex("bool CanonicalMachinegunProbeArm")
        ]
        for required in (
            "CanonicalRailProbeArm()",
            "historical_mover_occlusion_required = true",
            "damage_required = false",
            "historical_target_origin = {64.0f, 0.0f, -14.0f}",
            "CanonicalRailProbeSelectHistoricalMover",
        ):
            self.assertIn(required, arm)

        relocate = LAG[
            LAG.index(
                "[[nodiscard]] bool CanonicalRailProbeRelocateHistoricalMover()"
            ) :
            LAG.rindex("void CanonicalRailProbePrepareCommand")
        ]
        self.assertIn("FindMoverTrack", relocate)
        self.assertIn("Worr_RewindHistoryValidateV1", relocate)
        self.assertIn("Vector3{0.0f, 96.0f, 0.0f}", relocate)
        self.assertIn("gi.linkEntity(mover)", relocate)

        observe = LAG[
            LAG.rindex("void CanonicalRailProbeObserveTrace") :
            LAG.rindex("void CanonicalRailProbePrepareCommand")
        ]
        self.assertIn("historicalMoverHit", observe)
        self.assertIn("historical_mover_identity", observe)
        self.assertIn("WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED", observe)
        self.assertNotIn(
            "canonicalRailProbe.historical_mover_occlusion_observed =",
            observe,
        )

        rail_pierce = WEAPON[
            WEAPON.index("struct fire_rail_pierce_t") :
            WEAPON.index("// [Paril-KEX] get the current unique unicast key")
        ]
        self.assertIn(
            "LagCompensation_ObserveCanonicalRailPierceHit(self, tr)",
            rail_pierce,
        )
        explicit_observer = LAG[
            LAG.index("void LagCompensation_ObserveCanonicalRailPierceHit") :
            LAG.index("void LagCompensation_ObserveThunderboltUnderwaterDischarge")
        ]
        self.assertIn("historical_mover_baseline_clear", explicit_observer)
        self.assertIn("canonical_historical_hit", explicit_observer)
        self.assertIn("trace.ent", explicit_observer)
        self.assertIn(
            "historical_mover_occlusion_observed = true",
            explicit_observer,
        )

        capture = LAG[
            LAG.rindex("void CanonicalRailProbeCaptureFrame") :
            LAG.rindex("void CanonicalRailProbeObserveTrace")
        ]
        self.assertIn("historicalMoverOcclusionProof", capture)
        self.assertIn("historical_mover_target_undamaged", capture)
        self.assertIn("CanonicalRailProbeRestoreHistoricalMover()", capture)
        self.assertIn(
            "LagCompensation_ArmCanonicalRailMoverOcclusionRuntimeProbe",
            LAG_HEADER,
        )
        self.assertIn(
            "LagCompensation_ObserveCanonicalRailPierceHit",
            LAG_HEADER,
        )

        reset_client = LAG[
            LAG.index("void LagCompensation_ResetClient") :
            LAG.index("void LagCompensation_BeginClientLife")
        ]
        self.assertIn("CanonicalRailProbeActive()", reset_client)
        self.assertIn("CanonicalRailProbeFail(29)", reset_client)
        begin_life = LAG[
            LAG.index("void LagCompensation_BeginClientLife") :
            LAG.index("void LagCompensation_RecordFrame")
        ]
        self.assertIn("CanonicalRailProbeActive()", begin_life)
        self.assertIn("CanonicalRailProbeFail(29)", begin_life)

    def test_terminal_proof_binds_trace_to_the_same_command_and_live_damage(self) -> None:
        observe = LAG[LAG.rindex("void CanonicalRailProbeObserveTrace"):LAG.rindex("void CanonicalRailProbePrepareCommand")]
        for required in (
            "SameCommand(observation.command_id, canonicalRailProbe.command_id)",
            "WORR_REWIND_OBSERVATION_PATH_CANONICAL",
            "WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT",
            "WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED",
            "current_geometry_unchanged",
            "observation.weapon_policy != canonicalRailProbe.weapon_policy",
        ):
            self.assertIn(required, observe)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("healthBefore - healthAfter", capture)
        self.assertIn("CanonicalRailProbeDamageApplied()", capture)
        damage_contract = LAG[
            LAG.index("[[nodiscard]] bool CanonicalRailProbeDamageApplied()") :
            LAG.index("void CanonicalRailProbePrepareFrameCapture")
        ]
        self.assertIn("canonicalRailProbe.expected_damage", damage_contract)
        self.assertIn("canonicalRailProbe.minimum_damage", damage_contract)
        prepare_frame = LAG[
            LAG.rindex("void CanonicalRailProbePrepareFrameCapture"):
            LAG.rindex("void CanonicalRailProbeCaptureFrame")
        ]
        self.assertIn("CanonicalRailProbeStage::WaitingForCanonicalAttack", prepare_frame)

    def test_machinegun_fixture_uses_the_normal_weapon_callback_and_policy(self) -> None:
        prepare = LAG[LAG.rindex("void CanonicalRailProbePrepareCommand"):LAG.rindex("bool CanonicalHitscanProbeArm")]
        for required in (
            "GetItemByIndex(canonicalRailProbe.weapon_item)",
            "entity->client->pers.weapon = weapon",
            "entity->client->ps.gunFrame = canonicalRailProbe.weapon_idle_frame",
            "canonicalRailProbe.weapon_policy == WORR_REWIND_WEAPON_UNSPECIFIED",
        ):
            self.assertIn(required, prepare)
        arm = LAG[LAG.rindex("bool CanonicalMachinegunProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_MACHINEGUN", arm)
        self.assertIn("IT_WEAPON_MACHINEGUN", arm)
        self.assertIn("kCanonicalMachinegunProbeExpectedDamage", arm)

    def test_chaingun_fixture_requires_the_full_normal_three_round_burst(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalChaingunProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_CHAINGUN", arm)
        self.assertIn("IT_WEAPON_CHAINGUN", arm)
        self.assertIn("kCanonicalChaingunProbeExpectedDamage", arm)
        self.assertIn("kCanonicalChaingunProbeExpectedDamage, 14", arm)

    def test_super_shotgun_fixture_requires_both_full_pellet_barrels(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalSuperShotgunProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_SUPER_SHOTGUN", arm)
        self.assertIn("IT_WEAPON_SSHOTGUN", arm)
        self.assertIn("kCanonicalSuperShotgunProbeExpectedDamage", arm)
        self.assertIn("18, 64.0f", arm)

    def test_disruptor_fixture_waits_for_normal_projectile_and_daemon_damage(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalDisruptorProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_DISRUPTOR_CONVERGENCE", arm)
        self.assertIn("IT_WEAPON_DISRUPTOR", arm)
        self.assertIn("kCanonicalDisruptorProbeExpectedDamage", arm)
        self.assertIn("UINT64_C(1500000)", arm)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("damage_settle_deadline_us", capture)
        self.assertIn("if (!damageSettled)", capture)

    def test_disruptor_spawn_forward_is_bounded_authenticated_and_current_world_only(self) -> None:
        self.assertIn("LagCompensationProjectileForwardResult", LAG_HEADER)
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward", LAG_HEADER)
        self.assertIn("sg_lag_compensation_projectile_forward_ms", LAG)
        resolver = LAG[
            LAG.index("LagCompensation_ResolveProjectileSpawnForward") :
            LAG.index("bool LagCompensation_CopyObservations")
        ]
        for required in (
            "ResolveCanonicalDecision",
            "decision.mapped_time_us",
            "CurrentAuthoritativeTimeUs",
            "ProjectileForwardMs",
            "gi.trace(projectile->s.origin",
            "projectile->mins",
            "projectile->maxs",
            "projectile->clipMask",
        ):
            self.assertIn(required, resolver)
        self.assertNotIn("LagCompensation_Trace", resolver)
        self.assertNotIn("TraceHistoricalScene", resolver)

        disruptor = WEAPON[
            WEAPON.index("void fire_disruptor") :
            WEAPON.index("/*\n========================\nfire_flechette")
        ]
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", disruptor)
        self.assertIn("WORR_REWIND_WEAPON_DISRUPTOR_CONVERGENCE", disruptor)
        self.assertIn("forward.trace.endPos", disruptor)
        self.assertIn("bolt->touch(bolt, forward.trace.ent, forward.trace, false)", disruptor)
        self.assertIn("bolt->nextThink = std::max(level.time, bolt->nextThink - elapsed)", disruptor)

    def test_disruptor_fixture_requires_current_world_spawn_forward_proof(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalDisruptorProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_DISRUPTOR_CONVERGENCE", arm)
        state = LAG[LAG.index("struct CanonicalRailProbeState"):LAG.index("constexpr uint32_t kCanonicalRailProbeRequiredHistoryCaptures")]
        self.assertIn("projectile_forward_required", state)
        publish = LAG[LAG.index("void CanonicalRailProbePublish"):LAG.index("void CanonicalRailProbeFail")]
        self.assertIn("projectile_forward_advanced_age_us", publish)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("projectile_forward_required", capture)
        self.assertIn("projectile_forward_blocked", capture)

    def test_rocket_spawn_forward_keeps_impact_and_splash_current_authority(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalRocketProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_ROCKET_SPAWN_FORWARD", arm)
        self.assertIn("IT_WEAPON_RLAUNCHER", arm)
        self.assertIn("kCanonicalRocketProbeExpectedDamage", arm)
        self.assertIn("{32.0f, 0.0f, 0.0f}", arm)

        rocket = WEAPON[
            WEAPON.index("gentity_t *fire_rocket") :
            WEAPON.index("using search_callback_t")
        ]
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", rocket)
        self.assertIn("WORR_REWIND_WEAPON_ROCKET_SPAWN_FORWARD", rocket)
        self.assertIn("rocket->touch(rocket, forward.trace.ent, forward.trace, false)", rocket)
        self.assertIn("return nullptr;", rocket)
        self.assertIn("rocket->nextThink = std::max(level.time, rocket->nextThink - elapsed)", rocket)

        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("currentAuthorityProjectileProof", capture)
        self.assertIn("projectile_current_authority_required", capture)
        observer = LAG[
            LAG.rindex("void CanonicalRailProbeObserveProjectileForward"):
            LAG.rindex("void CanonicalRailProbePrepareCommand")
        ]
        self.assertIn("SameCommand(result.command_id, canonicalRailProbe.command_id)", observer)

    def test_bfg_spawn_forward_is_bound_to_its_normal_windup_and_current_world_launch(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalBfgProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn("WORR_REWIND_WEAPON_BFG_SPAWN_FORWARD", arm)
        self.assertIn("IT_WEAPON_BFG", arm)
        self.assertIn("200, 17, 112.0f", arm)
        self.assertIn("Its later laser,", arm)
        self.assertIn("staged explosion, and radius lifecycle are deliberately unclaimed.", arm)

        bfg_fire = PLAYER_WEAPON[
            PLAYER_WEAPON.index("static void Weapon_BFG_Fire") :
            PLAYER_WEAPON.index("\nvoid Weapon_BFG(gentity_t")
        ]
        self.assertIn("LagCompensation_ObserveCanonicalWeaponCallback(", bfg_fire)
        self.assertIn("WORR_REWIND_WEAPON_BFG_SPAWN_FORWARD", bfg_fire)
        self.assertIn("fire_bfg(ent, start, dir, damage, speed, radius);", bfg_fire)

        bfg = WEAPON[
            WEAPON.index("void fire_bfg") :
            WEAPON.index("static TOUCH(disintegrator_touch)")
        ]
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", bfg)
        self.assertIn("WORR_REWIND_WEAPON_BFG_SPAWN_FORWARD", bfg)
        self.assertIn("bfg->s.origin = forward.trace.endPos", bfg)
        self.assertIn("bfg->touch(bfg, forward.trace.ent, forward.trace, false)", bfg)
        self.assertIn("Every later BFG laser, touch, explosion,", bfg)
        self.assertIn("radius result remains on the normal production lifecycle.", bfg)

        deferred = LAG[
            LAG.index("kBfgDeferredProjectileForwardAuthorizationLifetimeUs") :
            LAG.index("[[nodiscard]] int MeleeMaxDisplacementUnits")
        ]
        self.assertIn("UINT64_C(1250000)", deferred)
        self.assertIn("WORR_REWIND_WEAPON_BFG_SPAWN_FORWARD", deferred)
        self.assertIn("DeferredProjectileForwardAuthorizationLifetimeUs", LAG)
        self.assertIn("sg_worr_rewind_canonical_bfg_damage_status", LAG)

    def test_ion_ripper_requires_all_normal_burst_bolts_to_use_current_world_forward(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalIonRipperProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn("WORR_REWIND_WEAPON_ION_RIPPER_BURST_SPAWN_FORWARD", arm)
        self.assertIn("IT_WEAPON_IONRIPPER", arm)
        self.assertIn("10, 6, 112.0f", arm)
        self.assertIn("{0.0f, 256.0f, 0.0f}", arm)

        ion_ripper_fire = PLAYER_WEAPON[
            PLAYER_WEAPON.index("static void Weapon_IonRipper_Fire") :
            PLAYER_WEAPON.index("\nvoid Weapon_IonRipper(gentity_t")
        ]
        self.assertIn("constexpr int kProjectileCount = 15", ion_ripper_fire)
        self.assertIn("LagCompensation_ObserveCanonicalWeaponCallback(", ion_ripper_fire)
        self.assertIn("WORR_REWIND_WEAPON_ION_RIPPER_BURST_SPAWN_FORWARD", ion_ripper_fire)
        self.assertIn("fire_ionripper(ent, start, dir, kDamage, speed, effectFlags)", ion_ripper_fire)

        ion_ripper = WEAPON[
            WEAPON.index("void fire_ionripper") :
            WEAPON.index("/*\n=================\nfire_heat")
        ]
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", ion_ripper)
        self.assertIn("WORR_REWIND_WEAPON_ION_RIPPER_BURST_SPAWN_FORWARD", ion_ripper)
        self.assertIn("ion->touch(ion, forward.trace.ent, forward.trace, false)", ion_ripper)
        self.assertIn("ion->nextThink = std::max(level.time", ion_ripper)

        state = LAG[
            LAG.index("struct CanonicalRailProbeState") :
            LAG.index("constexpr uint32_t kCanonicalRailProbeRequiredHistoryCaptures")
        ]
        self.assertIn("projectile_forward_launches", state)
        self.assertIn("projectile_forward_expected_launches", state)
        self.assertIn("CanonicalProjectileForwardExpectedLaunches", LAG)
        self.assertIn("? 15u", LAG)
        self.assertIn("return 15u;", LAG)
        self.assertIn("sg_worr_rewind_canonical_ion_ripper_damage_status", LAG)

    def test_tesla_mine_release_advances_only_its_clear_current_world_deploy_path(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalTeslaMineProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn(
            "WORR_REWIND_WEAPON_TESLA_MINE_RELEASE_BALLISTIC_DEPLOY_FORWARD",
            arm,
        )
        self.assertIn("IT_AMMO_TESLA", arm)
        self.assertIn("kCanonicalTeslaMineProbeExpectedDamage", arm)
        self.assertIn("32, 160.0f", arm)
        self.assertIn("clear,\n  // release-bound initial gravity advance", arm)

        tesla_fire = PLAYER_WEAPON[
            PLAYER_WEAPON.index("static void Weapon_Tesla_Fire") :
            PLAYER_WEAPON.index("/*\n======================================================================\n\nCHAINFIST")
        ]
        self.assertIn("if (!held)", tesla_fire)
        self.assertIn("LagCompensation_ObserveCanonicalWeaponCallback(", tesla_fire)
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", tesla_fire)
        self.assertIn("tesla->velocity = forward.final_velocity", tesla_fire)
        self.assertIn("tesla->nextThink = std::max(level.time", tesla_fire)
        self.assertIn("tesla->wait = std::max(level.time.seconds()", tesla_fire)

        tesla_spawn = WEAPON[
            WEAPON.index("gentity_t *fire_tesla") :
            WEAPON.index("/*\n=================\nfire_ionripper")
        ]
        self.assertIn("return tesla;", tesla_spawn)
        self.assertNotIn("LagCompensation_Trace", tesla_spawn)

        resolver = LAG[
            LAG.index("LagCompensation_ResolveProjectileSpawnForward") :
            LAG.index("LagCompensation_ResolveMeleePlayerCandidate")
        ]
        self.assertIn(
            "WORR_REWIND_WEAPON_TESLA_MINE_RELEASE_BALLISTIC_DEPLOY_FORWARD",
            resolver,
        )
        self.assertIn("contact rejects the advance entirely", resolver)
        self.assertIn("ReleaseOnlyProjectileForwardPolicy", LAG)
        self.assertIn(
            "LagCompensation_ArmCanonicalTeslaMineDamageRuntimeProbe",
            LAG_HEADER,
        )
        self.assertIn("sg_worr_rewind_canonical_tesla_mine_damage_status", LAG)

    def test_trap_release_advances_only_its_clear_current_world_deploy_path(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalTrapProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn(
            "WORR_REWIND_WEAPON_TRAP_RELEASE_BALLISTIC_DEPLOY_FORWARD",
            arm,
        )
        self.assertIn("IT_AMMO_TRAP", arm)
        self.assertIn("kCanonicalTrapProbeExpectedDamage", arm)
        self.assertIn("48, 160.0f", arm)
        self.assertIn("fresh clear\n  // release-bound gravity advance", arm)

        trap_fire = PLAYER_WEAPON[
            PLAYER_WEAPON.index("static void Weapon_Trap_Fire") :
            PLAYER_WEAPON.index("\nvoid Weapon_Trap(gentity_t")
        ]
        self.assertIn("if (!held)", trap_fire)
        self.assertIn("LagCompensation_ObserveCanonicalWeaponCallback(", trap_fire)
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", trap_fire)
        self.assertIn("trap->velocity = forward.final_velocity", trap_fire)
        self.assertIn("trap->nextThink = std::max(level.time", trap_fire)
        self.assertIn("trap->timeStamp = std::max(level.time", trap_fire)

        trap_spawn = WEAPON[
            WEAPON.index("gentity_t *fire_trap") :
            WEAPON.index("// =================================================")
        ]
        self.assertIn("return trap;", trap_spawn)
        self.assertNotIn("LagCompensation_Trace", trap_spawn)

        resolver = LAG[
            LAG.index("LagCompensation_ResolveProjectileSpawnForward") :
            LAG.index("LagCompensation_ResolveMeleePlayerCandidate")
        ]
        self.assertIn(
            "WORR_REWIND_WEAPON_TRAP_RELEASE_BALLISTIC_DEPLOY_FORWARD",
            resolver,
        )
        self.assertIn("contact rejects the advance entirely", resolver)
        self.assertIn("ReleaseOnlyProjectileForwardPolicy", LAG)
        self.assertIn(
            "LagCompensation_ArmCanonicalTrapDamageRuntimeProbe",
            LAG_HEADER,
        )
        self.assertIn("sg_worr_rewind_canonical_trap_damage_status", LAG)

    def test_grapple_hook_advances_only_its_clear_current_world_flight(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalGrappleProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn("WORR_REWIND_WEAPON_GRAPPLE_HOOK_SPAWN_FORWARD", arm)
        self.assertIn("IT_WEAPON_GRAPPLE", arm)
        self.assertIn("kCanonicalGrappleProbeExpectedDamage", arm)
        self.assertIn("31, 112.0f", arm)
        self.assertIn("{0.0f, 256.0f, 0.0f}", arm)
        self.assertIn("fresh-hook flight, not contact, attachment, pull, or damage", arm)

        grapple_hook = PLAYER_WEAPON[
            PLAYER_WEAPON.index("static bool Weapon_Grapple_FireHook") :
            PLAYER_WEAPON.index("static void Weapon_Grapple_DoFire")
        ]
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", grapple_hook)
        self.assertIn("weaponPolicy != WORR_REWIND_WEAPON_UNSPECIFIED", grapple_hook)
        self.assertIn("forward.advanced && !forward.blocked", grapple_hook)
        self.assertIn("normal FlyMissile/touch ownership remains intact", grapple_hook)
        self.assertIn("grapple->touch(grapple, tr.ent, tr, false)", grapple_hook)

        grapple_fire = PLAYER_WEAPON[
            PLAYER_WEAPON.index("static void Weapon_Grapple_DoFire") :
            PLAYER_WEAPON.index("\nvoid Weapon_Grapple(gentity_t")
        ]
        self.assertIn("LagCompensation_ObserveCanonicalWeaponCallback(", grapple_fire)
        self.assertIn("WORR_REWIND_WEAPON_GRAPPLE_HOOK_SPAWN_FORWARD", grapple_fire)

        hook_fire = PLAYER_WEAPON[
            PLAYER_WEAPON.index("static void Weapon_Hook_DoFire") :
            PLAYER_WEAPON.index("\nvoid Weapon_Hook(gentity_t")
        ]
        self.assertIn("uint32_t weaponPolicy", hook_fire)
        legacy_hook = PLAYER_WEAPON[
            PLAYER_WEAPON.index("\nvoid Weapon_Hook(gentity_t") :
            PLAYER_WEAPON.index("\nvoid Weapon_Hook_CanonicalInput")
        ]
        self.assertIn("WORR_REWIND_WEAPON_UNSPECIFIED", legacy_hook)

        deferred = LAG[
            LAG.index("kGrappleDeferredProjectileForwardAuthorizationLifetimeUs") :
            LAG.index("[[nodiscard]] int MeleeMaxDisplacementUnits")
        ]
        self.assertIn("UINT64_C(750000)", deferred)
        self.assertIn("WORR_REWIND_WEAPON_GRAPPLE_HOOK_SPAWN_FORWARD", deferred)
        self.assertIn(
            "LagCompensation_ArmCanonicalGrappleDamageRuntimeProbe",
            LAG_HEADER,
        )
        self.assertIn("sg_worr_rewind_canonical_grapple_damage_status", LAG)

    def test_native_offhand_hook_uses_only_the_mapped_button_path(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalOffhandHookProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn(
            "WORR_REWIND_WEAPON_OFFHAND_HOOK_SPAWN_FORWARD",
            arm,
        )
        self.assertIn("BUTTON_HOOK alone invokes", arm)
        self.assertIn("false, false, false, false, true);", arm)
        self.assertIn("not touch,\n  // attachment, pull, damage, or reset", arm)

        native_hook = PLAYER_WEAPON[
            PLAYER_WEAPON.index("void Weapon_Hook_CanonicalInput") :
            PLAYER_WEAPON.index("/*\n======================================================================\n\nBLASTER")
        ]
        self.assertIn("g_grapple_offhand", native_hook)
        self.assertIn(
            "WORR_REWIND_WEAPON_OFFHAND_HOOK_SPAWN_FORWARD",
            native_hook,
        )
        self.assertIn("legacy `hook` client string", native_hook)

        self.assertIn("BUTTON_HOOK", CLIENT_THINK)
        self.assertIn("Weapon_Hook_CanonicalInput(ent)", CLIENT_THINK)
        self.assertIn('{ "+hook", IN_HookDown }', CLIENT_INPUT)
        self.assertIn("cl.cmd.buttons |= BUTTON_HOOK", CLIENT_INPUT)
        self.assertIn(
            "(BUTTON_ATTACK|BUTTON_USE|BUTTON_HOOK|BUTTON_ANY)",
            PROTOCOL,
        )
        self.assertIn("worr_rewind_canonical_offhand_hook_arm", SVCMDS)
        self.assertIn(
            "LagCompensation_ArmCanonicalOffhandHookRuntimeProbe",
            LAG_HEADER,
        )
        self.assertIn("sg_worr_rewind_canonical_offhand_hook_status", LAG)

        resolver = LAG[
            LAG.index("LagCompensation_ResolveProjectileSpawnForward") :
            LAG.index("LagCompensation_ResolveMeleePlayerCandidate")
        ]
        self.assertNotIn("TraceHistoricalScene", resolver)
        self.assertNotIn("LagCompensation_Trace", resolver)

    def test_proball_throw_advances_only_the_release_bound_current_world_ball(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalProBallThrowProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn(
            "WORR_REWIND_WEAPON_PROBALL_HELD_THROW_BALLISTIC_SPAWN_FORWARD",
            arm,
        )
        self.assertIn("IT_WEAPON_CHAINFIST", arm)
        self.assertIn("Game::IsNot(GameType::ProBall)", arm)
        self.assertIn("kCanonicalProBallThrowProbeExpectedDamage", arm)
        self.assertIn("kCanonicalProBallThrowProbeExpectedDamage, 4,", arm)
        self.assertIn(
            "fresh ball's\n  // clear current-world gravity advance",
            arm,
        )

        launch = PROBALL[
            PROBALL.index("bool Ball_Launch") :
            PROBALL.index("bool Ball_Pass")
        ]
        self.assertIn("bool releaseBoundThrow", launch)
        self.assertIn("if (releaseBoundThrow)", launch)
        self.assertIn("LagCompensation_ObserveCanonicalWeaponCallback(", launch)
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", launch)
        self.assertIn(
            "WORR_REWIND_WEAPON_PROBALL_HELD_THROW_BALLISTIC_SPAWN_FORWARD",
            launch,
        )
        self.assertIn("Ball_Touch, pickup, goals, scoring, reset, and team", launch)

        ball_pass = PROBALL[
            PROBALL.index("bool Ball_Pass") :
            PROBALL.index("bool Ball_Drop")
        ]
        self.assertIn("BALL_PASS_SPEED, false", ball_pass)

        deferred = LAG[
            LAG.index("void LagCompensation_RecordDeferredProjectileForwardCommand") :
            LAG.index("void LagCompensation_ObserveCanonicalWeaponCallback")
        ]
        self.assertIn("const bool proBallHeldThrow", deferred)
        self.assertIn("weapon->id == IT_WEAPON_CHAINFIST", deferred)
        self.assertIn("entity->client->pers.inventory[IT_BALL] > 0", deferred)
        self.assertIn("proBallHeldThrow ? IT_BALL", deferred)
        self.assertIn(
            "WORR_REWIND_WEAPON_PROBALL_HELD_THROW_BALLISTIC_SPAWN_FORWARD",
            LAG,
        )
        self.assertIn(
            "LagCompensation_ArmCanonicalProBallThrowRuntimeProbe",
            LAG_HEADER,
        )
        self.assertIn("sg_worr_rewind_canonical_proball_throw_status", LAG)

    def test_current_world_projectile_splash_fixture_requires_impact_before_radius_damage(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalRocketSplashProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_ROCKET_SPAWN_FORWARD", arm)
        self.assertIn("kCanonicalRocketSplashProbeExpectedDamage", arm)
        self.assertIn("{-64.0f, 48.0f, 0.0f}", arm)
        self.assertIn("true);", arm)

        fixture = LAG[
            LAG.index("CanonicalRailProbePlaceCurrentWorldSplashImpact"):
            LAG.index("[[nodiscard]] bool CanonicalRailProbeSelectPlayers")
        ]
        self.assertIn('worr_canonical_current_world_splash_impact', fixture)
        self.assertIn("SOLID_BBOX", fixture)
        self.assertIn("current_world_splash_impact_damageable", fixture)
        self.assertIn("if (impact->takeDamage)", fixture)

        observer = LAG[
            LAG.rindex("void LagCompensation_ObserveCurrentWorldProjectileSplashImpact"):
            LAG.rindex("trace_t LagCompensation_TraceLine")
        ]
        self.assertIn("current_world_splash_impact_identity", observer)
        self.assertIn("current_world_splash_projectile_identity", observer)
        self.assertIn("current_world_splash_impact_observed = true", observer)
        self.assertIn("weaponPolicy != canonicalRailProbe.weapon_policy", observer)

        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("currentWorldSplashProof", capture)
        self.assertIn("!canonicalRailProbe.canonical_historical_hit", capture)

    def test_plasma_gun_spawn_forward_keeps_direct_and_splash_current_authority(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalPlasmaGunProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_PLASMA_GUN_SPAWN_FORWARD", arm)
        self.assertIn("IT_WEAPON_PLASMAGUN", arm)
        self.assertIn("kCanonicalPlasmaGunProbeExpectedDamage", arm)
        self.assertIn("43, 224.0f", arm)
        self.assertIn("{32.0f, 0.0f, 0.0f}", arm)

        plasma_gun = WEAPON[
            WEAPON.index("void fire_plasmagun") :
            WEAPON.index("/*\n=================\nfire_phalanx")
        ]
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", plasma_gun)
        self.assertIn("WORR_REWIND_WEAPON_PLASMA_GUN_SPAWN_FORWARD", plasma_gun)
        self.assertIn("plasma->touch(plasma, forward.trace.ent, forward.trace, false)", plasma_gun)
        self.assertIn("plasma->nextThink = std::max(level.time, plasma->nextThink - elapsed)", plasma_gun)
        self.assertLess(
            plasma_gun.index("gi.traceLine(self->s.origin"),
            plasma_gun.index("LagCompensation_ResolveProjectileSpawnForward"),
        )

        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("currentAuthorityProjectileProof", capture)
        self.assertIn("!canonicalRailProbe.canonical_historical_hit", capture)

    def test_plasma_gun_splash_requires_a_post_forward_current_world_impact(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalPlasmaGunSplashProbeArm"):
            LAG.rindex("bool CanonicalBlasterProbeArm")
        ]
        self.assertIn("WORR_REWIND_WEAPON_PLASMA_GUN_SPAWN_FORWARD", arm)
        self.assertIn("IT_WEAPON_PLASMAGUN", arm)
        self.assertIn("kCanonicalPlasmaGunSplashProbeExpectedDamage", arm)
        self.assertIn("43, 256.0f", arm)
        self.assertIn("{32.0f, 0.0f, 0.0f}", arm)
        self.assertIn("true, 0, false, true", arm)
        self.assertIn("1.0f", arm)
        self.assertIn("1.0f, true, true", arm)
        self.assertIn(
            "LagCompensation_ArmCanonicalPlasmaGunSplashDamageRuntimeProbe",
            LAG_HEADER,
        )
        self.assertIn(
            "sg_worr_rewind_canonical_plasma_gun_splash_damage_status", LAG
        )

        fixture = LAG[
            LAG.index("CanonicalRailProbePlaceCurrentWorldSplashImpact"):
            LAG.index("[[nodiscard]] bool CanonicalRailProbeSelectPlayers")
        ]
        self.assertIn("current_world_splash_impact_half_extent", fixture)
        self.assertIn("-halfExtent", fixture)

        observer = LAG[
            LAG.rindex("void LagCompensation_ObserveCurrentWorldProjectileSplashImpact"):
            LAG.rindex("trace_t LagCompensation_TraceLine")
        ]
        self.assertIn("current_world_splash_clear_impact_after_touch", observer)
        self.assertIn("other->solid = SOLID_NOT", observer)
        self.assertIn("gi.unlinkEntity(other)", observer)
        self.assertIn("current_world_splash_damage_target_after_touch", observer)
        self.assertIn("worr_canonical_current_world_splash_target", observer)
        self.assertIn("trace.endPos - direction * 12.0f", observer)
        self.assertIn("damageTarget->takeDamage = true", observer)

        plasma_gun = WEAPON[
            WEAPON.index("void fire_plasmagun"):
            WEAPON.index("/*\n=================\nfire_phalanx")
        ]
        plasma_touch = WEAPON[
            WEAPON.index("TOUCH(plasmagun_touch)"):
            WEAPON.index("void fire_plasmagun")
        ]
        self.assertIn(
            "LagCompensation_ObserveCurrentWorldProjectileSplashImpact(",
            plasma_touch,
        )
        self.assertIn(
            "WORR_REWIND_WEAPON_PLASMA_GUN_SPAWN_FORWARD", plasma_touch
        )
        self.assertIn("RadiusDamage(", plasma_touch)
        self.assertIn("ModID::PlasmaGun_Splash", plasma_touch)

    def test_blaster_bolt_spawn_forward_keeps_direct_and_radius_current_authority(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalBlasterProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_BLASTER_BOLT_SPAWN_FORWARD", arm)
        self.assertIn("IT_WEAPON_BLASTER", arm)
        self.assertIn("kCanonicalBlasterProbeExpectedDamage", arm)
        self.assertIn("9, 224.0f", arm)
        self.assertIn("{32.0f, 0.0f, 0.0f}", arm)

        blaster = WEAPON[
            WEAPON.index("void fire_blaster") :
            WEAPON.index("/*\n=================\nfire_greenblaster")
        ]
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", blaster)
        self.assertIn("WORR_REWIND_WEAPON_BLASTER_BOLT_SPAWN_FORWARD", blaster)
        self.assertIn("bolt->touch(bolt, forward.trace.ent, forward.trace, false)", blaster)
        self.assertIn("bolt->nextThink = std::max(level.time, bolt->nextThink - elapsed)", blaster)
        self.assertLess(
            blaster.index("gi.traceLine(self->s.origin"),
            blaster.index("LagCompensation_ResolveProjectileSpawnForward"),
        )

        prepare = LAG[
            LAG.rindex("void CanonicalRailProbePrepareCommand"):
            LAG.rindex("bool CanonicalHitscanProbeArm")
        ]
        self.assertIn("if (weapon->ammo != IT_NULL)", prepare)

    def test_chainfist_uses_historical_eligibility_but_live_world_damage(self) -> None:
        self.assertIn("LagCompensationMeleeSelectionResult", LAG_HEADER)
        self.assertIn("LagCompensation_ResolveMeleePlayerCandidate", LAG_HEADER)
        self.assertIn("sg_lag_compensation_melee_max_displacement", LAG)

        resolver = LAG[
            LAG.index("LagCompensation_ResolveMeleePlayerCandidate") :
            LAG.index("bool LagCompensation_CopyObservations")
        ]
        for required in (
            "ResolveCanonicalDecision",
            "Worr_RewindHistoryQueryV1",
            "WORR_REWIND_WEAPON_CHAINFIST_MELEE_HYBRID",
            "MeleeMaxDisplacementUnits",
            "result.selection_ready = true",
            "result.current_displacement_accepted = true",
        ):
            self.assertIn(required, resolver)
        self.assertNotIn("LagCompensation_Trace", resolver)
        self.assertNotIn("Damage(", resolver)

        melee = WEAPON[
            WEAPON.index("bool fire_player_melee") :
            WEAPON.index("// *************************\n// NUKE")
        ]
        self.assertIn("LagCompensation_ResolveMeleePlayerCandidate", melee)
        self.assertIn("WORR_REWIND_WEAPON_CHAINFIST_MELEE_HYBRID", melee)
        self.assertIn("historicalSelection.selection_ready", melee)
        self.assertIn("CanDamage(self, hit)", melee)
        self.assertIn("Damage(hit, self, self", melee)

        arm = LAG[
            LAG.rindex("bool CanonicalChainfistProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn("IT_WEAPON_CHAINFIST", arm)
        self.assertIn("kCanonicalChainfistProbeExpectedDamage", arm)
        self.assertIn("33, 48.0f", arm)
        self.assertIn("{0.0f, 64.0f, 0.0f}", arm)

        state = LAG[
            LAG.index("struct CanonicalRailProbeState") :
            LAG.index("constexpr uint32_t kCanonicalRailProbeRequiredHistoryCaptures")
        ]
        self.assertIn("melee_selection_required", state)
        self.assertIn("melee_current_displacement_accepted", state)
        capture = LAG[
            LAG.rindex("void CanonicalRailProbeCaptureFrame") :
            LAG.rindex("void CanonicalRailProbeObserveTrace")
        ]
        self.assertIn("meleeSelectionProof", capture)
        observer = LAG[
            LAG.rindex("void CanonicalRailProbeObserveMeleeSelection") :
            LAG.rindex("void CanonicalRailProbePrepareCommand")
        ]
        self.assertIn("SameCommand(result.command_id, canonicalRailProbe.command_id)", observer)

    def test_etf_flechette_spawn_forward_keeps_contact_and_damage_current_world(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalEtfRifleProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn("WORR_REWIND_WEAPON_ETF_FLECHETTE_SPAWN_FORWARD", arm)
        self.assertIn("IT_WEAPON_ETF_RIFLE", arm)
        self.assertIn("kCanonicalEtfRifleProbeExpectedDamage", arm)
        self.assertIn("8, 128.0f", arm)

        flechette = WEAPON[
            WEAPON.index("void fire_flechette") :
            WEAPON.index("// **************************\n// PROX")
        ]
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", flechette)
        self.assertIn("WORR_REWIND_WEAPON_ETF_FLECHETTE_SPAWN_FORWARD", flechette)
        self.assertIn("flechette->touch(flechette, forward.trace.ent", flechette)
        self.assertIn("flechette->nextThink = std::max(level.time", flechette)
        self.assertLess(
            flechette.index("gi.traceLine(self->s.origin"),
            flechette.index("LagCompensation_ResolveProjectileSpawnForward"),
        )

        capture = LAG[
            LAG.rindex("void CanonicalRailProbeCaptureFrame") :
            LAG.rindex("void CanonicalRailProbeObserveTrace")
        ]
        self.assertIn("currentAuthorityProjectileProof", capture)

    def test_phalanx_spawn_forward_keeps_direct_and_splash_current_world(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalPhalanxProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn("WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD", arm)
        self.assertIn("IT_WEAPON_PHALANX", arm)
        self.assertIn("kCanonicalPhalanxProbeExpectedDamage", arm)
        self.assertIn("21, 64.0f", arm)

        phalanx = WEAPON[
            WEAPON.index("void fire_phalanx") :
            WEAPON.index("/*\n=================\nfire_trap")
        ]
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", phalanx)
        self.assertIn("WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD", phalanx)
        phalanx_fire = PLAYER_WEAPON[
            PLAYER_WEAPON.index("static void Weapon_Phalanx_Fire") :
            PLAYER_WEAPON.index("\nvoid Weapon_Phalanx(gentity_t")
        ]
        self.assertIn("LagCompensation_ObserveCanonicalWeaponCallback(", phalanx_fire)
        self.assertIn("phalanx->touch(phalanx, forward.trace.ent", phalanx)
        self.assertIn("phalanx->nextThink = std::max(level.time", phalanx)
        self.assertIn("RadiusDamage(ent, ent->owner", WEAPON[
            WEAPON.index("static TOUCH(phalanx_touch)") :
            WEAPON.index("void fire_phalanx")
        ])

        capture = LAG[
            LAG.rindex("void CanonicalRailProbeCaptureFrame") :
            LAG.rindex("void CanonicalRailProbeObserveTrace")
        ]
        self.assertIn("currentAuthorityProjectileProof", capture)
        observer = LAG[
            LAG.rindex("void CanonicalRailProbeObserveProjectileForward") :
            LAG.rindex("void CanonicalRailProbeObserveMeleeSelection")
        ]
        self.assertIn("canonicalRailProbe.projectile_command_id", observer)
        prepare = LAG[
            LAG.rindex("void CanonicalRailProbePrepareCommand") :
            LAG.rindex("bool CanonicalHitscanProbeArm")
        ]
        self.assertIn(
            "Weapon_Generic advances Phalanx from its initial fire frame",
            prepare,
        )
        self.assertIn(
            "LagCompensation_RecordDeferredProjectileForwardCommand",
            LAG_HEADER,
        )
        callback = LAG[
            LAG.index("void LagCompensation_ObserveCanonicalWeaponCallback") :
            LAG.index("void LagCompensation_ObserveThunderboltUnderwaterDischarge")
        ]
        self.assertIn("CanonicalRailProbePinTarget(", callback)
        self.assertIn(
            "DeferredProjectileForwardAuthorization",
            LAG,
        )
        deferred = LAG[
            LAG.index("bool ResolveDeferredProjectileForwardDecision") :
            LAG.index("[[nodiscard]] bool ResolveTarget")
        ]
        self.assertIn("authorization.launches_remaining", deferred)
        self.assertIn("ProjectileForwardPolicyMatchesWeapon", deferred)
        resolver = LAG[
            LAG.index("LagCompensation_ResolveProjectileSpawnForward") :
            LAG.index("LagCompensation_ResolveMeleePlayerCandidate")
        ]
        self.assertIn("ResolveDeferredProjectileForwardDecision(", resolver)

    def test_grenade_launcher_uses_clear_current_world_ballistic_forward(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalGrenadeLauncherProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn(
            "WORR_REWIND_WEAPON_GRENADE_LAUNCHER_BALLISTIC_SPAWN_FORWARD",
            arm,
        )
        self.assertIn("IT_WEAPON_GLAUNCHER", arm)
        self.assertIn("kCanonicalGrenadeLauncherProbeExpectedDamage", arm)
        self.assertIn("UINT64_C(2500000)", arm)
        self.assertNotIn("current-world impact fixture", arm)

        splash = LAG[
            LAG.rindex("bool CanonicalHandGrenadeSplashProbeArm") :
            LAG.rindex("bool CanonicalProxLauncherProbeArm")
        ]
        self.assertIn("actual", splash)
        self.assertIn("false, true", splash)
        forward = LAG[
            LAG.rindex("void CanonicalRailProbeObserveProjectileForward") :
            LAG.rindex("void CanonicalRailProbeObserveMeleeSelection")
        ]
        self.assertIn("current_world_splash_after_forward", forward)
        self.assertIn("impactOrigin", forward)
        self.assertIn("CanonicalRailProbePlaceCurrentWorldSplashImpact(&impactOrigin)", forward)

        self.assertIn("5,\n      160.0f", arm)
        self.assertIn("{-64.0f, 96.0f, 0.0f}", arm)

        grenade_weapon = PLAYER_WEAPON[
            PLAYER_WEAPON.index("static void Weapon_GrenadeLauncher_Fire") :
            PLAYER_WEAPON.index("\nvoid Weapon_GrenadeLauncher(gentity_t")
        ]
        self.assertIn("LagCompensation_ObserveCanonicalWeaponCallback(", grenade_weapon)
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", grenade_weapon)
        self.assertIn("grenade->velocity = forward.final_velocity", grenade_weapon)
        self.assertIn("grenade->timeStamp = std::max(level.time", grenade_weapon)

        grenade_touch = WEAPON[
            WEAPON.index("static TOUCH(Grenade_Touch)") :
            WEAPON.index("static THINK(Grenade4_Think)")
        ]
        self.assertIn(
            "LagCompensation_ObserveCurrentWorldProjectileSplashImpact(",
            grenade_touch,
        )
        self.assertIn(
            "WORR_REWIND_WEAPON_GRENADE_LAUNCHER_BALLISTIC_SPAWN_FORWARD",
            grenade_touch,
        )

        resolver = LAG[
            LAG.index("LagCompensation_ResolveProjectileSpawnForward") :
            LAG.index("LagCompensation_ResolveMeleePlayerCandidate")
        ]
        self.assertIn("kMinimumBallisticStepUs", resolver)
        self.assertIn("projectile->gravityVector", resolver)
        self.assertIn("result.advanced_age_us = 0", resolver)
        self.assertIn("contact rejects the advance entirely", resolver)

    def test_hand_grenade_uses_the_fresh_release_command_for_ballistic_forward(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalHandGrenadeProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn(
            "WORR_REWIND_WEAPON_HAND_GRENADE_RELEASE_BALLISTIC_SPAWN_FORWARD",
            arm,
        )
        self.assertIn("IT_AMMO_GRENADES", arm)
        self.assertIn("kCanonicalHandGrenadeProbeExpectedDamage", arm)
        self.assertIn("16, 160.0f", arm)
        self.assertIn("UINT64_C(2500000)", arm)
        self.assertNotIn("current-world impact fixture", arm)

        hand_grenade = PLAYER_WEAPON[
            PLAYER_WEAPON.index("static void Weapon_HandGrenade_Fire") :
            PLAYER_WEAPON.index("Throw_Generic")
        ]
        self.assertIn("if (!held)", hand_grenade)
        self.assertIn("LagCompensation_ObserveCanonicalWeaponCallback(", hand_grenade)
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", hand_grenade)
        self.assertIn("grenade->nextThink = std::max(level.time", hand_grenade)

        hand_grenade_spawn = WEAPON[
            WEAPON.index("gentity_t *fire_handgrenade") :
            WEAPON.index("static TOUCH(rocket_touch)")
        ]
        self.assertIn("return grenade;", hand_grenade_spawn)

        prepare = LAG[
            LAG.rindex("void CanonicalRailProbePrepareCommand") :
            LAG.rindex("bool CanonicalHitscanProbeArm")
        ]
        self.assertIn("releaseOnlyPolicy", prepare)
        self.assertIn("(command->buttons & BUTTON_ATTACK) == 0", prepare)
        self.assertIn("releaseOnlyCommand", prepare)
        self.assertIn("(entity->client->buttons & BUTTON_ATTACK) != 0", prepare)

        deferred_record = LAG[
            LAG.index("void LagCompensation_RecordDeferredProjectileForwardCommand") :
            LAG.index("void LagCompensation_ObserveCanonicalWeaponCallback")
        ]
        self.assertIn("const bool releaseEdge", deferred_record)
        self.assertIn("authorization.release_only = releaseOnlyPolicy", deferred_record)

        resolver = LAG[
            LAG.index("LagCompensation_ResolveProjectileSpawnForward") :
            LAG.index("LagCompensation_ResolveMeleePlayerCandidate")
        ]
        self.assertIn(
            "WORR_REWIND_WEAPON_HAND_GRENADE_RELEASE_BALLISTIC_SPAWN_FORWARD",
            resolver,
        )
        self.assertIn("actual attack-to-release edge", resolver)
        self.assertIn("authorization.release_only != releaseOnlyPolicy", LAG)
        self.assertIn("bool damageRequired = true", LAG)
        self.assertIn("false, false, 8, false, false, 0, false,", arm)

    def test_attack_key_up_flushes_a_normal_command_without_enabling_input(self) -> None:
        attack_up = CLIENT_INPUT[
            CLIENT_INPUT.index("static void IN_AttackUp(void)") :
            CLIENT_INPUT.index("static void IN_UseDown(void)")
        ]
        self.assertIn("KeyUp(&in_attack);", attack_up)
        self.assertIn("CL_CheckInstantPacket();", attack_up)
        self.assertIn("neither initializes physical", attack_up)
        self.assertIn("nor changes server weapon authority", attack_up)

    def test_prox_launcher_keeps_deployable_lifecycle_current_world_after_clear_ballistic_forward(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalProxLauncherProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn("WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD", arm)
        self.assertIn("IT_WEAPON_PROXLAUNCHER", arm)
        self.assertIn("kCanonicalProxLauncherProbeExpectedDamage", arm)
        self.assertIn("false, false, 8, false, false, 0, false,", arm)
        self.assertIn("false);", arm)

        prox_fire = PLAYER_WEAPON[
            PLAYER_WEAPON.index("static void Weapon_ProxLauncher_Fire") :
            PLAYER_WEAPON.index("void Weapon_ProxLauncher(gentity_t")
        ]
        self.assertIn("LagCompensation_ObserveCanonicalWeaponCallback(", prox_fire)
        self.assertIn("WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD", prox_fire)
        self.assertIn("fire_prox(ent, start, dir, damageMultiplier, 600);", prox_fire)

        prox = WEAPON[
            WEAPON.index("void fire_prox") :
            WEAPON.index("// MELEE WEAPONS")
        ]
        self.assertIn("LagCompensation_ResolveProjectileSpawnForward(", prox)
        self.assertIn("WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD", prox)
        self.assertIn("prox->velocity = launchForward.final_velocity;", prox)
        self.assertIn("prox->timeStamp = std::max(level.time", prox)
        self.assertIn("normal Bounce landing, arming", prox)

        resolver = LAG[
            LAG.index("LagCompensation_ResolveProjectileSpawnForward") :
            LAG.index("LagCompensation_ResolveMeleePlayerCandidate")
        ]
        self.assertIn("WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD", resolver)
        self.assertIn("only owner of collision response, placement, triggers", resolver)
        self.assertIn("LagCompensation_ArmCanonicalProxLauncherDamageRuntimeProbe", LAG_HEADER)
        self.assertIn("worr_rewind_canonical_prox_launcher_damage_arm", SVCMDS)

    def test_prox_launcher_lifecycle_fixture_observes_only_normal_deployable_boundaries(self) -> None:
        lifecycle_arm = LAG[
            LAG.rindex("bool CanonicalProxLauncherLifecycleProbeArm") :
            LAG.rindex("bool CanonicalRocketSplashProbeArm")
        ]
        self.assertIn("WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD", lifecycle_arm)
        self.assertIn("IT_WEAPON_PROXLAUNCHER", lifecycle_arm)
        self.assertIn("UINT64_C(5000000)", lifecycle_arm)
        self.assertIn("real mine has landed", lifecycle_arm)
        self.assertIn("LagCompensation_ArmCanonicalProxLauncherLifecycleRuntimeProbe", LAG)
        self.assertIn(
            "LagCompensation_ArmCanonicalProxLauncherLifecycleRuntimeProbe",
            LAG_HEADER,
        )
        self.assertIn("worr_rewind_canonical_prox_launcher_lifecycle_arm", SVCMDS)

        prox_explode = WEAPON[
            WEAPON.index("static THINK(Prox_Explode)") :
            WEAPON.index("static DIE(prox_die)")
        ]
        self.assertIn("RadiusDamage(ent, owner", prox_explode)
        self.assertIn("LagCompensation_ObserveProxMineExploded(ent);", prox_explode)

        trigger = WEAPON[
            WEAPON.index("static THINK(Prox_TriggerThink)") :
            WEAPON.index("static THINK(prox_seek)")
        ]
        self.assertIn("level.time < trigger->timeStamp", trigger)
        self.assertIn("visible(search, prox)", trigger)
        self.assertIn("LagCompensation_ObserveProxMineTriggered(prox, search);", trigger)
        self.assertIn("prox->nextThink = level.time + PROX_TIME_DELAY;", trigger)

        landing = WEAPON[
            WEAPON.index("static TOUCH(prox_land)") :
            WEAPON.index("static THINK(Prox_Think)")
        ]
        self.assertIn("trigger->timeStamp = level.time + PROX_ARMING_DELAY", landing)
        self.assertIn("LagCompensation_ObserveProxMineLanded(ent, trigger);", landing)

        observers = LAG[
            LAG.index("void LagCompensation_ObserveProxMineLanded") :
            LAG.index("trace_t LagCompensation_TraceLine")
        ]
        self.assertIn("prox_mine_identity", observers)
        self.assertIn("CanonicalRailProbePinTarget", observers)
        self.assertIn("normal production radial candidate/visibility", observers)
        self.assertIn("after its ordinary RadiusDamage invocation", observers)
        self.assertIn("CanonicalRailProbePlaceProxLandingSurface", LAG)
        self.assertIn("worr_canonical_prox_landing_surface", LAG)
        self.assertIn("surface->moveType = MoveType::Push;", LAG)
        self.assertIn("normal physics", LAG)
        self.assertIn("supplies the actual contact", LAG)

    def test_phalanx_splash_requires_the_generic_current_world_impact_fixture(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalPhalanxSplashProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn("WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD", arm)
        self.assertIn("IT_WEAPON_PHALANX", arm)
        self.assertIn("kCanonicalPhalanxSplashProbeExpectedDamage", arm)
        self.assertIn("{-64.0f, 48.0f, 0.0f}", arm)

        phalanx_touch = WEAPON[
            WEAPON.index("static TOUCH(phalanx_touch)") :
            WEAPON.index("void fire_phalanx")
        ]
        self.assertIn(
            "LagCompensation_ObserveCurrentWorldProjectileSplashImpact(",
            phalanx_touch,
        )
        self.assertIn("WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD", phalanx_touch)
        self.assertIn("RadiusDamage(ent, ent->owner", phalanx_touch)

    def test_hyperblaster_fixture_uses_the_shared_bolt_policy_on_a_real_held_cadence(self) -> None:
        arm = LAG[
            LAG.rindex("bool CanonicalHyperBlasterProbeArm") :
            LAG.rindex("} // namespace")
        ]
        self.assertIn("WORR_REWIND_WEAPON_BLASTER_BOLT_SPAWN_FORWARD", arm)
        self.assertIn("IT_WEAPON_HYPERBLASTER", arm)
        self.assertIn("kCanonicalHyperBlasterProbeExpectedDamage", arm)
        self.assertIn("5, 224.0f", arm)

        hyperblaster = PLAYER_WEAPON[
            PLAYER_WEAPON.index("static void Weapon_HyperBlaster_Fire") :
            PLAYER_WEAPON.index("\nvoid Weapon_HyperBlaster(gentity_t")
        ]
        self.assertIn("LagCompensation_ObserveCanonicalWeaponCallback(", hyperblaster)
        self.assertIn("Weapon_Blaster_Fire(ent, offset, damage, true, effect)", hyperblaster)
        self.assertIn("client->buttons & BUTTON_ATTACK", hyperblaster)

    def test_plasma_beam_fixture_proves_one_normal_held_command_tick(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalPlasmaBeamProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_PLASMA_BEAM", arm)
        self.assertIn("IT_WEAPON_PLASMABEAM", arm)
        self.assertIn("kCanonicalPlasmaBeamProbeExpectedDamage", arm)
        self.assertIn("13, 112.0f, 0", arm)
        self.assertIn("first ordinary deathmatch beam tick only", arm)

    def test_plasma_beam_held_fixture_waits_for_three_normal_ticks(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalPlasmaBeamHeldProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_PLASMA_BEAM", arm)
        self.assertIn("IT_WEAPON_PLASMABEAM", arm)
        self.assertIn("kCanonicalPlasmaBeamHeldProbeExpectedDamage", arm)
        self.assertIn("UINT64_C(1500000)", arm)
        self.assertIn("{32.0f, 0.0f, 0.0f}", arm)
        self.assertIn("later commands query their newer history", arm)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("damageApplied || deadlineReached", capture)
        self.assertIn("defer_damage_evaluation_until_deadline", capture)

    def test_plasma_beam_sustained_fixture_requires_the_full_cell_budget(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalPlasmaBeamSustainedProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("kCanonicalPlasmaBeamSustainedProbeExpectedDamage", arm)
        self.assertIn("kCanonicalBeamSustainedProbeCells", arm)
        self.assertIn("kCanonicalBeamSustainedProbeTimeoutUs", arm)
        self.assertIn("32 normal 8-damage ticks", arm)
        self.assertIn("requires two Cells to begin a tick", arm)
        self.assertIn("kCanonicalBeamSustainedProbeCells = 33", LAG)
        self.assertIn("kCanonicalBeamSustainedProbeAngleLockDuration = 6_sec", LAG)
        prepare = LAG[LAG.rindex("void CanonicalRailProbePrepareCommand"):LAG.rindex("bool CanonicalHitscanProbeArm")]
        self.assertIn("sustained_hold_interrupted", prepare)
        self.assertIn("CanonicalRailProbeFail(20)", prepare)
        self.assertIn("kCanonicalBeamSustainedProbeAngleLockDuration", prepare)

    def test_sustained_beam_fixture_pins_only_knockback_displacement(self) -> None:
        pin = LAG[LAG.index("void CanonicalRailProbePinTarget"):LAG.index("[[nodiscard]] bool CanonicalRailProbeSelectPlayers")]
        self.assertIn("entity->velocity = {};", pin)
        self.assertNotIn("entity->health", pin)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("canonicalRailProbe.sustained_hold_required", capture)
        self.assertIn("CanonicalRailProbePinTarget", capture)

    def test_plasma_beam_release_fixture_requires_real_no_attack_and_stable_damage(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalPlasmaBeamReleaseProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_PLASMA_BEAM", arm)
        self.assertIn("kCanonicalPlasmaBeamReleaseProbeExpectedDamage", arm)
        self.assertIn("true);", arm)
        prepare = LAG[LAG.rindex("void CanonicalRailProbePrepareCommand"):LAG.rindex("bool CanonicalHitscanProbeArm")]
        self.assertIn("release_required", prepare)
        self.assertIn("(command->buttons & BUTTON_ATTACK) == 0", prepare)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("release_damage_stable", capture)
        self.assertIn("release_grace_deadline_us", capture)

    def test_plasma_beam_water_retrace_requires_the_production_halved_damage_proof(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalPlasmaBeamWaterRetraceProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("kCanonicalPlasmaBeamWaterRetraceProbeExpectedDamage", arm)
        self.assertIn("224.0f", arm)
        self.assertIn("{112.0f, 0.0f, 0.0f}", arm)
        observe = LAG[LAG.rindex("void CanonicalRailProbeObserveTrace"):LAG.rindex("void CanonicalRailProbePrepareCommand")]
        self.assertIn("historicalWaterHit", observe)
        self.assertIn("water_trace_observation_sequence", observe)
        self.assertIn("water_retrace_observed", observe)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("fire_beams/fire_thunderbolt halve damage only after", capture)

    def test_thunderbolt_fixture_proves_one_normal_deduplicated_footprint(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalThunderboltProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_THUNDERBOLT", arm)
        self.assertIn("IT_WEAPON_THUNDERBOLT", arm)
        self.assertIn("kCanonicalThunderboltProbeExpectedDamage", arm)
        self.assertIn("3, 112.0f, 0", arm)
        self.assertIn("main/side-ray footprint and target de-duplication", arm)

    def test_thunderbolt_held_fixture_waits_for_three_deduplicated_ticks(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalThunderboltHeldProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_THUNDERBOLT", arm)
        self.assertIn("IT_WEAPON_THUNDERBOLT", arm)
        self.assertIn("kCanonicalThunderboltHeldProbeExpectedDamage", arm)
        self.assertIn("UINT64_C(1500000)", arm)
        self.assertIn("{32.0f, 0.0f, 0.0f}", arm)

    def test_thunderbolt_sustained_fixture_requires_the_full_cell_budget(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalThunderboltSustainedProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("kCanonicalThunderboltSustainedProbeExpectedDamage", arm)
        self.assertIn("kCanonicalBeamSustainedProbeCells", arm)
        self.assertIn("kCanonicalBeamSustainedProbeTimeoutUs", arm)
        self.assertIn("target de-duplication", arm)

    def test_thunderbolt_release_fixture_requires_real_no_attack_and_stable_damage(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalThunderboltReleaseProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_THUNDERBOLT", arm)
        self.assertIn("kCanonicalThunderboltReleaseProbeExpectedDamage", arm)
        self.assertIn("true);", arm)

    def test_thunderbolt_water_retrace_retains_normal_target_deduplication(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalThunderboltWaterRetraceProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("kCanonicalThunderboltWaterRetraceProbeExpectedDamage", arm)
        self.assertIn("4 damage", arm)
        self.assertIn("de-duplication", arm)

    def test_thunderbolt_discharge_is_explicit_current_authority_and_drains_cells(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalThunderboltDischargeProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("kCanonicalThunderboltDischargeProbeExpectedDamage", arm)
        self.assertIn("current-authority radius policy", arm)
        self.assertIn("{0.0f, 400.0f, 0.0f}", arm)
        capture = LAG[LAG.rindex("void CanonicalRailProbeCaptureFrame"):LAG.rindex("void CanonicalRailProbeObserveTrace")]
        self.assertIn("thunderbolt_discharge_ammo_drained", capture)
        self.assertIn("thunderboltDischargeProof", capture)
        self.assertIn("shooter_health_before", capture)
        self.assertIn("LagCompensation_ObserveThunderboltUnderwaterDischarge", LAG)
        self.assertIn("LagCompensation_ObserveThunderboltUnderwaterDischarge", WEAPON)

    def test_shotgun_fixture_requires_the_full_normal_pellet_damage(self) -> None:
        arm = LAG[LAG.rindex("bool CanonicalShotgunProbeArm"):LAG.rindex("} // namespace")]
        self.assertIn("WORR_REWIND_WEAPON_SHOTGUN", arm)
        self.assertIn("IT_WEAPON_SHOTGUN", arm)
        self.assertIn("kCanonicalShotgunProbeExpectedDamage", arm)


if __name__ == "__main__":
    unittest.main()
