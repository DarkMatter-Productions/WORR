#!/usr/bin/env python3
"""Contracts for the two-process canonical rail-damage runtime gate."""

from __future__ import annotations

import contextlib
import importlib.util
import tempfile
import unittest
from unittest import mock
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/networking/run_canonical_rail_damage_runtime_gate.py"
SPEC = importlib.util.spec_from_file_location("canonical_rail_damage_runtime_gate", SCRIPT)
assert SPEC and SPEC.loader
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)

# Frozen independently from the runner so a production grammar edit cannot
# silently update both the parser contract and its fixture generator.
NATIVE_EVENT_PROBE_STATUS_FIELDS_V1 = (
    "valid",
    "schema",
    "size",
    "kind_count",
    "map_generation",
    "map_end_count",
    "map_active",
    "probe_requested",
    "probe_latched",
    "probe_active",
    "effect_authority_enabled",
    "resources_required",
    "legacy_owner_active",
    "raw_pending_count",
    "authority_epoch",
    "authority_requires_resync",
    "authority_degraded",
    "raw_action_records",
    "raw_action_chain_hash",
    "raw_effect_dispatches",
    "raw_effect_chain_hash",
    "raw_effect_suppressions",
    "raw_pair_failures",
    "probe_action_commits",
    "probe_action_chain_hash",
    "probe_effects_suppressed",
    "probe_nonvisual_commits",
    "native_effect_dispatches",
    "native_effect_chain_hash",
    "presenter_commit_mismatches",
    "authoritative_presentations",
    "authoritative_duplicates",
    "authoritative_conflicts",
    "authority_ref_body_joins",
    "legacy_ref_body_mismatches",
    "raw_k0",
    "raw_k1",
    "raw_k2",
    "raw_k3",
    "raw_k4",
    "raw_k5",
    "raw_k6",
    "raw_k7",
    "probe_k0",
    "probe_k1",
    "probe_k2",
    "probe_k3",
    "probe_k4",
    "probe_k5",
    "probe_k6",
    "probe_k7",
)
NATIVE_EVENT_PROBE_CHECKPOINT_FIELDS_V1 = (
    "valid",
    "schema",
    "size",
    "result",
    "map_generation",
    "authority_epoch",
    "checkpoint_id",
)

PASS_STATUS: dict[str, int | str] = {
    name: 0 for name in GATE.STATUS_FIELDS
}
PASS_STATUS.update({
    "status": "pass",
    "armed": 1,
    "players_ready": 1,
    "history_ready": 1,
    "canonical_scope": 1,
    "attack_received": 1,
    "weapon_callback": 1,
    "canonical_historical_hit": 1,
    "damage_applied": 1,
    "current_geometry_unchanged": 1,
    "target_history_captures": 6,
    "applied_age_us": 50_000,
    "eligible_candidates": 2,
    "playing_candidates": 2,
    "observation_path": 1,
    "observation_outcome": 1,
    "observation_flags": 63,
    "observation_snapshot_epoch": 1,
    "history_epoch": 1,
    "target_history_count": 6,
    "observation_applied_time_us": 50_000,
    "latest_capture_time_us": 65_000,
    "trace_current_time_us": 70_000,
    "context_snapshot_time_us": 70_000,
    "context_mapped_time_us": 50_000,
    "target_capture_prepares": 6,
    "target_capture_callbacks": 6,
    "observation_weapon_policy": 5,
    "expected_damage": 80,
    "observed_damage": 80,
    "local_action_catalog_ready": 1,
    "local_action_lease_ready": 1,
    "local_action_lease_offers": 8,
    "local_action_lease_supersedes": 1,
    "local_action_lease_claims": 4,
    "local_action_lease_expired": 2,
    "local_action_command_epoch": 1,
    "local_action_command_sequence": 9,
    "local_action_scoped_record": 1,
    "local_action_leased_record": 1,
    "local_action_continuity_exact": 1,
    "local_action_joined_record": 1,
    "local_action_shadow_ready": 1,
    "local_action_shadow_catalog_id": 1,
    "local_action_shadow_flags": 7,
    "local_action_shadow_v2_blockers": 4367,
    "local_action_shadow_record_hash": 1,
})
PASS_LINE = (
    'sg_worr_rewind_canonical_rail_damage_status "' +
    ":".join(str(PASS_STATUS[name]) for name in GATE.STATUS_FIELDS) + '"'
)


def native_event_probe_row(**overrides: int) -> dict[str, int]:
    row = {name: 0 for name in NATIVE_EVENT_PROBE_STATUS_FIELDS_V1}
    row.update({
        "valid": 1,
        "schema": 3,
        "size": 336,
        "kind_count": 8,
        "map_generation": 1,
        "map_end_count": 0,
        "map_active": 1,
        "probe_requested": 1,
        "probe_latched": 1,
        "probe_active": 1,
        "resources_required": 1,
        "legacy_owner_active": 1,
        "authority_epoch": 11,
    })
    row.update(overrides)
    return row


def completed_native_event_probe_row(**overrides: int) -> dict[str, int]:
    row = native_event_probe_row(
        raw_action_records=5,
        raw_action_chain_hash=0x1020304050607080,
        raw_effect_dispatches=5,
        raw_effect_chain_hash=0x8877665544332211,
        probe_action_commits=5,
        probe_action_chain_hash=0x1020304050607080,
        probe_effects_suppressed=5,
        probe_nonvisual_commits=0,
        authoritative_presentations=5,
        authority_ref_body_joins=5,
        raw_k2=2,
        raw_k3=1,
        raw_k4=1,
        raw_k5=1,
        probe_k2=2,
        probe_k3=1,
        probe_k4=1,
        probe_k5=1,
    )
    row.update(overrides)
    return row


def native_event_probe_line(row: dict[str, int]) -> str:
    values = []
    for name in NATIVE_EVENT_PROBE_STATUS_FIELDS_V1:
        value = row[name]
        raw = f"{value:016x}" if name.endswith("_chain_hash") else str(value)
        values.append(f"{name}={raw}")
    return GATE.NATIVE_EVENT_PROBE_STATUS_MARKER + " " + " ".join(values)


def native_event_probe_checkpoint_row(**overrides: int) -> dict[str, int]:
    row = {
        "valid": 1,
        "schema": 1,
        "size": 32,
        "result": GATE.NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED,
        "map_generation": 1,
        "authority_epoch": 11,
        "checkpoint_id": (1 << 32) | 11,
    }
    row.update(overrides)
    return row


def native_event_probe_checkpoint_line(row: dict[str, int]) -> str:
    return GATE.NATIVE_EVENT_PROBE_CHECKPOINT_MARKER + " " + " ".join(
        f"{name}={row[name]}"
        for name in NATIVE_EVENT_PROBE_CHECKPOINT_FIELDS_V1
    )


def native_event_sender_row(**overrides: int) -> dict[str, int]:
    row = {name: 0 for name in GATE.NATIVE_EVENT_SENDER_STATUS_FIELDS}
    row.update({
        "schema": 1,
        "slot": 7,
        "mode": 2,
        "sender": 1,
        "tx_open": 1,
        "stream_epoch": 9,
        "descriptor_acked": 1,
        "confirms": 2,
        "snapshots_queued": 3,
        "candidates_queued": 3,
        "candidates_promoted": 3,
        "descriptor_acks": 1,
        "event_acks": 3,
        "prepared": 8,
        "confirmed": 8,
        "first_sends": 4,
        "retries": 4,
    })
    row.update(overrides)
    return row


def native_event_sender_line(row: dict[str, int]) -> str:
    return GATE.NATIVE_EVENT_SENDER_STATUS_MARKER + " " + " ".join(
        f"{name}={row[name]}" for name in GATE.NATIVE_EVENT_SENDER_STATUS_FIELDS
    )


def native_event_sender_post_row(
    baseline: dict[str, int], *, event_count: int = 3, retry_count: int = 2,
    **overrides: int,
) -> dict[str, int]:
    row = dict(baseline)
    increments = {
        "confirms": 1,
        "snapshots_queued": 2,
        "candidates_queued": event_count,
        "candidates_promoted": event_count,
        "event_acks": event_count,
        "prepared": 2,
        "confirmed": 2,
        "first_sends": 1,
        "retries": retry_count,
        "schema2_batches_promoted": 1,
        "schema2_events_promoted": event_count,
    }
    for name, increment in increments.items():
        row[name] += increment
    row.update(overrides)
    return row


def native_base_client_line(
    *, private_mask: int = 0x73, **overrides: int,
) -> str:
    row = {
        "schema": 1,
        "enabled": 1,
        "mode": 2,
        "hooks": 1,
        "capability_confirmed": 1,
        "readiness_phase": 5,
        "official_epoch": 11,
        "transport_epoch": 12,
        "protocol": 1038,
        "public_mask": private_mask,
        "private_mask": private_mask,
        "server_active": 1,
        "proof_enqueued": 1,
        "retained_releases": 1,
        "tx_first_sends": 1,
        "acknowledged_reliable": 1,
        "failures": 0,
        "last_failure": 0,
    }
    row.update(overrides)
    return GATE.NATIVE_CLIENT_STATUS_MARKER + " " + " ".join(
        f"{name}={value}" for name, value in row.items()
    )


def native_base_server_line(
    slot: int, *, private_mask: int = 0x73, **overrides: int,
) -> str:
    row = {
        "schema": 1,
        "slot": slot,
        "protocol": 1038,
        "enabled": 1,
        "lifecycle": GATE.NATIVE_SERVER_LIFECYCLE_ACTIVE,
        "hooks": 1,
        "official_epoch": 11 + slot,
        "transport_epoch": 11 + slot,
        "public_mask": private_mask,
        "private_mask": private_mask,
        "wire_committed": 1,
        "wire_committed_transport_epoch": 11 + slot,
        "challenges_queued": 1,
        "client_ready": 1,
        "server_active": 1,
        "legacy_joins": 1,
        "command_matches": 1,
        "failures": 0,
        "rx_rejections": 0,
        "tx_ack_rejections": 0,
        "last_failure": 0,
    }
    row.update(overrides)
    return GATE.NATIVE_SERVER_STATUS_MARKER + " " + " ".join(
        f"{name}={value}" for name, value in row.items()
    )


class CanonicalRailDamageRuntimeGateTests(unittest.TestCase):
    def test_v42_schema_retains_exact_rocket_lifecycle_suffix(self) -> None:
        self.assertEqual(
            GATE.SCHEMA, "worr.networking.canonical-weapon-damage-runtime.v42"
        )
        self.assertEqual(len(GATE.STATUS_FIELDS), 113)
        self.assertEqual(GATE.STATUS_FIELDS[-12:], (
            "rocket_lifecycle_required",
            "rocket_lifecycle_policy",
            "rocket_owner_identity_retained",
            "rocket_touch_count",
            "rocket_touch_current_world",
            "rocket_retired",
            "rocket_retired_by_touch",
            "rocket_retired_by_expiry",
            "rocket_post_touch_hold_verified",
            "rocket_no_double_damage",
            "rocket_lifetime_scheduled_ms",
            "rocket_lifetime_elapsed_ms",
        ))

    def test_server_is_dedicated_and_client_is_headless_input_free(self) -> None:
        server = GATE.build_server_command(
            Path("C:/stage/worr_ded_x86_64.exe"), 27960, Path("C:/runtime/server"),
        )
        client = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960, GATE.SHOOTER_NAME,
            Path("C:/runtime/shooter"),
        )
        target = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960, GATE.TARGET_NAME,
            Path("C:/runtime/target"),
        )
        spectator = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960, GATE.SPECTATOR_NAME,
            Path("C:/runtime/spectator"),
        )
        self.assertEqual(server[0], str(Path("C:/stage/worr_ded_x86_64.exe")))
        self.assertIn("rcon_password", server)
        self.assertEqual(server[server.index("fs_homepath") + 1], str(Path("C:/runtime/server")))
        self.assertEqual(server[server.index("sg_lag_compensation_interp_ms") + 1], "50")
        self.assertEqual(client[client.index("qport") + 1], GATE.CLIENT_QPORTS[GATE.SHOOTER_NAME])
        self.assertEqual(target[target.index("qport") + 1], GATE.CLIENT_QPORTS[GATE.TARGET_NAME])
        self.assertEqual(
            spectator[spectator.index("qport") + 1],
            GATE.CLIENT_QPORTS[GATE.SPECTATOR_NAME],
        )
        qports = [int(value) for value in GATE.CLIENT_QPORTS.values()]
        self.assertEqual(len(set(qports)), 3)
        self.assertTrue(all(0 < qport <= 0xff for qport in qports))
        for command in (client, target, spectator):
            self.assertEqual(command.count("qport"), 1)
            self.assertLess(command.index("qport"), command.index("+connect"))
        with self.assertRaisesRegex(ValueError, "unsupported canonical fixture"):
            GATE.build_client_command(
                Path("C:/stage/worr_x86_64.exe"), 27960, "unknown_fixture",
            )
        self.assertEqual(
            server[server.index("sg_lag_compensation_projectile_forward_ms") + 1],
            "100",
        )
        self.assertEqual(
            server[server.index("sg_lag_compensation_melee_max_displacement") + 1],
            "64",
        )
        self.assertNotIn("addbot", server)
        self.assertNotIn("g_inactivity", server)
        self.assertNotIn("worr_rewind_canonical_rail_damage_arm", server)
        self.assertNotIn("+stuffall", server)
        self.assertIn('stuffall "cmd team free"', SCRIPT.read_text(encoding="utf-8"))
        self.assertIn(
            'input_command = str(mode.get("input_command", "+attack"))',
            SCRIPT.read_text(encoding="utf-8"),
        )
        self.assertIn(
            'stuff {shooter_user_id} "-attack; +attack; +moveup; -moveup"',
            SCRIPT.read_text(encoding="utf-8"),
        )
        self.assertIn('stuff {shooter_user_id} "-attack; +moveup; -moveup"', SCRIPT.read_text(encoding="utf-8"))
        self.assertIn("refresh_held_attack", SCRIPT.read_text(encoding="utf-8"))
        self.assertEqual(GATE.GATE_MODES["railgun"]["weapon_policy"], 5)
        spectator_mode = GATE.GATE_MODES["railgun-spectator-exclusion"]
        self.assertEqual(spectator_mode["arm_command"], GATE.GATE_MODES["railgun"]["arm_command"])
        self.assertEqual(spectator_mode["status_cvar"], GATE.GATE_MODES["railgun"]["status_cvar"])
        self.assertEqual(spectator_mode["required_client_count"], 3)
        self.assertTrue(spectator_mode["require_spectator_exclusion"])
        self.assertEqual(spectator_mode["expected_playing_candidates"], 2)
        self.assertEqual(spectator_mode["expected_eligible_candidates"], 2)
        self.assertEqual(GATE.SPECTATOR_JOIN_MARKER, "You are now spectating.")
        self.assertEqual(GATE.SPECTATOR_TEAM_QUERY_MARKER, "g_you_are_on_team")
        protection_mode = GATE.GATE_MODES["railgun-spawn-protection"]
        self.assertEqual(protection_mode["weapon_policy"], 5)
        self.assertEqual(protection_mode["expected_damage"], 0)
        self.assertTrue(protection_mode["require_no_damage"])
        self.assertEqual(
            protection_mode["status_cvar"],
            "sg_worr_rewind_canonical_rail_spawn_protection_status",
        )
        self.assertEqual(
            GATE.GATE_MODES["railgun-mover-occlusion"]["weapon_policy"], 5
        )
        self.assertEqual(
            GATE.GATE_MODES["railgun-mover-occlusion"]["status_cvar"],
            "sg_worr_rewind_canonical_rail_mover_occlusion_status",
        )
        self.assertFalse(
            GATE.GATE_MODES["railgun-mover-occlusion"]["require_damage"]
        )
        self.assertTrue(
            GATE.GATE_MODES["railgun-mover-occlusion"][
                "require_historical_mover_occlusion"
            ]
        )
        self.assertEqual(GATE.GATE_MODES["machinegun"]["weapon_policy"], 1)
        self.assertEqual(GATE.GATE_MODES["chaingun"]["weapon_policy"], 2)
        self.assertEqual(GATE.GATE_MODES["super-shotgun"]["weapon_policy"], 4)
        self.assertEqual(GATE.GATE_MODES["disruptor"]["weapon_policy"], 6)
        self.assertTrue(GATE.GATE_MODES["disruptor"]["require_projectile_forward"])
        self.assertEqual(GATE.GATE_MODES["rocket"]["weapon_policy"], 9)
        self.assertEqual(GATE.GATE_MODES["rocket"]["expected_damage"], 100)
        self.assertTrue(GATE.GATE_MODES["rocket"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["rocket"]["current_authority_projectile"])
        mover_mode = GATE.GATE_MODES["rocket-mover-relative"]
        self.assertEqual(mover_mode["weapon_policy"], 9)
        self.assertEqual(mover_mode["expected_damage"], 100)
        self.assertTrue(mover_mode["require_mover_relative_projectile"])
        self.assertEqual(mover_mode["expected_mover_relative_policy"], 1)
        touch_mode = GATE.GATE_MODES["rocket-lifecycle-touch"]
        self.assertEqual(touch_mode["weapon_policy"], 9)
        self.assertEqual(touch_mode["expected_damage"], 100)
        self.assertEqual(touch_mode["expected_rocket_lifecycle_policy"], 1)
        self.assertTrue(touch_mode["require_rocket_lifecycle"])
        expiry_mode = GATE.GATE_MODES["rocket-lifetime-expiry"]
        self.assertEqual(expiry_mode["weapon_policy"], 9)
        self.assertEqual(expiry_mode["expected_damage"], 0)
        self.assertFalse(expiry_mode["require_damage"])
        self.assertEqual(expiry_mode["expected_rocket_lifecycle_policy"], 2)
        self.assertEqual(GATE.GATE_MODES["plasma-gun-splash"]["weapon_policy"], 10)
        self.assertEqual(GATE.GATE_MODES["plasma-gun-splash"]["expected_damage"], 7)
        self.assertEqual(
            GATE.GATE_MODES["plasma-gun-splash"]["status_cvar"],
            "sg_worr_rewind_canonical_plasma_gun_splash_damage_status",
        )
        self.assertTrue(
            GATE.GATE_MODES["plasma-gun-splash"]["require_projectile_forward"]
        )
        self.assertTrue(
            GATE.GATE_MODES["plasma-gun-splash"]["current_authority_projectile"]
        )
        self.assertTrue(
            GATE.GATE_MODES["plasma-gun-splash"]["require_current_authority_splash"]
        )
        self.assertTrue(
            GATE.GATE_MODES["plasma-gun-splash"]["require_reduced_splash"]
        )
        self.assertEqual(GATE.GATE_MODES["bfg"]["weapon_policy"], 18)
        self.assertEqual(GATE.GATE_MODES["bfg"]["expected_damage"], 200)
        self.assertEqual(
            GATE.GATE_MODES["bfg"]["status_cvar"],
            "sg_worr_rewind_canonical_bfg_damage_status",
        )
        self.assertTrue(GATE.GATE_MODES["bfg"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["bfg"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["bfg"]["require_damage"])
        self.assertEqual(GATE.GATE_MODES["ion-ripper"]["weapon_policy"], 19)
        self.assertEqual(GATE.GATE_MODES["ion-ripper"]["expected_damage"], 10)
        self.assertEqual(
            GATE.GATE_MODES["ion-ripper"]["status_cvar"],
            "sg_worr_rewind_canonical_ion_ripper_damage_status",
        )
        self.assertTrue(GATE.GATE_MODES["ion-ripper"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["ion-ripper"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["ion-ripper"]["require_damage"])
        self.assertEqual(
            GATE.GATE_MODES["ion-ripper"]["expected_projectile_forward_launches"],
            15,
        )
        self.assertEqual(GATE.GATE_MODES["tesla-mine"]["weapon_policy"], 20)
        self.assertEqual(GATE.GATE_MODES["tesla-mine"]["expected_damage"], 3)
        self.assertEqual(
            GATE.GATE_MODES["tesla-mine"]["status_cvar"],
            "sg_worr_rewind_canonical_tesla_mine_damage_status",
        )
        self.assertTrue(GATE.GATE_MODES["tesla-mine"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["tesla-mine"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["tesla-mine"]["require_damage"])
        self.assertTrue(
            GATE.GATE_MODES["tesla-mine"][
                "release_held_attack_after_attack_received"
            ]
        )
        self.assertEqual(GATE.GATE_MODES["trap"]["weapon_policy"], 21)
        self.assertEqual(GATE.GATE_MODES["trap"]["expected_damage"], 20)
        self.assertEqual(
            GATE.GATE_MODES["trap"]["status_cvar"],
            "sg_worr_rewind_canonical_trap_damage_status",
        )
        self.assertTrue(GATE.GATE_MODES["trap"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["trap"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["trap"]["require_damage"])
        self.assertTrue(
            GATE.GATE_MODES["trap"][
                "release_held_attack_after_attack_received"
            ]
        )
        self.assertEqual(GATE.GATE_MODES["grapple"]["weapon_policy"], 22)
        self.assertEqual(GATE.GATE_MODES["grapple"]["expected_damage"], 1)
        self.assertEqual(
            GATE.GATE_MODES["grapple"]["status_cvar"],
            "sg_worr_rewind_canonical_grapple_damage_status",
        )
        self.assertTrue(GATE.GATE_MODES["grapple"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["grapple"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["grapple"]["require_damage"])
        self.assertEqual(GATE.GATE_MODES["offhand-hook"]["weapon_policy"], 24)
        self.assertEqual(GATE.GATE_MODES["offhand-hook"]["input_command"], "+hook")
        self.assertTrue(GATE.GATE_MODES["offhand-hook"]["enable_offhand_hook"])
        self.assertTrue(
            GATE.GATE_MODES["offhand-hook"]["require_projectile_forward"]
        )
        self.assertTrue(
            GATE.GATE_MODES["offhand-hook"]["current_authority_projectile"]
        )
        self.assertFalse(GATE.GATE_MODES["offhand-hook"]["require_damage"])
        self.assertEqual(GATE.GATE_MODES["proball-throw"]["weapon_policy"], 23)
        self.assertEqual(GATE.GATE_MODES["proball-throw"]["expected_damage"], 1)
        self.assertEqual(
            GATE.GATE_MODES["proball-throw"]["status_cvar"],
            "sg_worr_rewind_canonical_proball_throw_status",
        )
        self.assertTrue(
            GATE.GATE_MODES["proball-throw"]["require_projectile_forward"]
        )
        self.assertTrue(
            GATE.GATE_MODES["proball-throw"]["current_authority_projectile"]
        )
        self.assertFalse(GATE.GATE_MODES["proball-throw"]["require_damage"])
        self.assertEqual(GATE.GATE_MODES["proball-throw"]["gametype"], 17)
        self.assertTrue(GATE.GATE_MODES["proball-throw"]["team_game"])
        self.assertEqual(GATE.GATE_MODES["grenade-launcher"]["weapon_policy"], 15)
        self.assertEqual(GATE.GATE_MODES["grenade-launcher"]["expected_damage"], 60)
        self.assertEqual(GATE.GATE_MODES["grenade-launcher"]["minimum_damage"], 57)
        self.assertTrue(GATE.GATE_MODES["grenade-launcher"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["grenade-launcher"]["current_authority_projectile"])
        self.assertTrue(GATE.GATE_MODES["grenade-launcher"]["require_current_authority_splash"])
        self.assertTrue(GATE.GATE_MODES["grenade-launcher"]["require_reduced_splash"])
        self.assertEqual(GATE.GATE_MODES["hand-grenade"]["weapon_policy"], 16)
        self.assertEqual(GATE.GATE_MODES["hand-grenade"]["expected_damage"], 60)
        self.assertEqual(GATE.GATE_MODES["hand-grenade"]["minimum_damage"], 57)
        self.assertTrue(GATE.GATE_MODES["hand-grenade"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["hand-grenade"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["hand-grenade"]["require_damage"])
        self.assertFalse(GATE.GATE_MODES["hand-grenade"]["release_held_attack_flush"])
        self.assertTrue(
            GATE.GATE_MODES["hand-grenade"][
                "release_held_attack_after_attack_received"
            ]
        )
        self.assertGreater(
            float(GATE.GATE_MODES["hand-grenade"]["release_held_attack_after_seconds"]),
            1.0,
        )
        self.assertEqual(GATE.GATE_MODES["hand-grenade-splash"]["weapon_policy"], 16)
        self.assertTrue(
            GATE.GATE_MODES["hand-grenade-splash"]["require_current_authority_splash"]
        )
        self.assertGreater(
            float(
                GATE.GATE_MODES["hand-grenade-splash"][
                    "release_held_attack_after_seconds"
                ]
            ),
            1.0,
        )
        self.assertEqual(GATE.GATE_MODES["prox-launcher"]["weapon_policy"], 17)
        self.assertEqual(GATE.GATE_MODES["prox-launcher"]["expected_damage"], 90)
        self.assertTrue(GATE.GATE_MODES["prox-launcher"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["prox-launcher"]["current_authority_projectile"])
        self.assertFalse(GATE.GATE_MODES["prox-launcher"]["require_damage"])
        self.assertEqual(GATE.GATE_MODES["prox-launcher-lifecycle"]["weapon_policy"], 17)
        self.assertEqual(
            GATE.GATE_MODES["prox-launcher-lifecycle"]["expected_damage"], 61,
        )
        self.assertTrue(
            GATE.GATE_MODES["prox-launcher-lifecycle"]["require_projectile_forward"],
        )
        self.assertTrue(
            GATE.GATE_MODES["prox-launcher-lifecycle"]["current_authority_projectile"],
        )
        self.assertTrue(
            GATE.GATE_MODES["prox-launcher-lifecycle"]["require_prox_lifecycle"],
        )
        self.assertEqual(GATE.GATE_MODES["rocket-splash"]["weapon_policy"], 9)
        self.assertEqual(GATE.GATE_MODES["rocket-splash"]["expected_damage"], 58)
        self.assertTrue(GATE.GATE_MODES["rocket-splash"]["require_current_authority_splash"])
        self.assertTrue(GATE.GATE_MODES["rocket-splash"]["require_reduced_splash"])
        self.assertEqual(
            GATE.GATE_MODES["rocket-splash"][
                "expected_splash_occlusion_policy"
            ],
            1,
        )
        bsp_splash = GATE.GATE_MODES["rocket-splash-bsp-occlusion"]
        self.assertEqual(bsp_splash["expected_damage"], 0)
        self.assertFalse(bsp_splash["require_damage"])
        self.assertEqual(bsp_splash["expected_splash_occlusion_policy"], 2)
        self.assertEqual(bsp_splash["expected_splash_can_damage"], 0)
        self.assertEqual(bsp_splash["expected_splash_bsp_blocker"], 1)
        water_splash = GATE.GATE_MODES["rocket-splash-water-boundary"]
        self.assertEqual(water_splash["expected_damage"], 58)
        self.assertEqual(water_splash["expected_splash_occlusion_policy"], 3)
        self.assertEqual(water_splash["expected_splash_can_damage"], 1)
        self.assertEqual(water_splash["expected_splash_water_boundary"], 1)
        self.assertEqual(GATE.GATE_MODES["plasma-gun"]["weapon_policy"], 10)
        self.assertEqual(GATE.GATE_MODES["plasma-gun"]["expected_damage"], 20)
        self.assertTrue(GATE.GATE_MODES["plasma-gun"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["plasma-gun"]["current_authority_projectile"])
        self.assertEqual(GATE.GATE_MODES["blaster"]["weapon_policy"], 11)
        self.assertEqual(GATE.GATE_MODES["blaster"]["expected_damage"], 15)
        self.assertTrue(GATE.GATE_MODES["blaster"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["blaster"]["current_authority_projectile"])
        legacy_mode = GATE.GATE_MODES["blaster-legacy-capability-status"]
        self.assertTrue(legacy_mode["require_legacy_capability_status"])
        self.assertEqual(legacy_mode["weapon_policy"], 11)
        combined_mode = GATE.GATE_MODES["blaster-local-action-lease-combined"]
        self.assertTrue(combined_mode["require_local_action_authority_receipt"])
        self.assertTrue(combined_mode["require_in_session_reconnect"])
        self.assertTrue(combined_mode["require_combined_native_shadow"])
        self.assertNotIn("release_held_attack_after_seconds", combined_mode)
        event_mode = GATE.GATE_MODES["blaster-local-action-lease"]
        self.assertTrue(event_mode["require_native_event_status"])
        presentation_mode = GATE.GATE_MODES[
            "blaster-native-snapshot-presentation"
        ]
        self.assertTrue(presentation_mode["require_native_snapshot_shadow"])
        self.assertTrue(presentation_mode["require_native_snapshot_presentation"])
        self.assertEqual(GATE.GATE_MODES["hyperblaster"]["weapon_policy"], 11)
        self.assertEqual(GATE.GATE_MODES["hyperblaster"]["expected_damage"], 15)
        self.assertTrue(GATE.GATE_MODES["hyperblaster"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["hyperblaster"]["current_authority_projectile"])
        self.assertTrue(GATE.GATE_MODES["hyperblaster"]["refresh_held_attack"])
        self.assertEqual(GATE.GATE_MODES["chainfist"]["weapon_policy"], 12)
        self.assertEqual(GATE.GATE_MODES["chainfist"]["expected_damage"], 15)
        self.assertTrue(GATE.GATE_MODES["chainfist"]["require_hybrid_melee"])
        self.assertEqual(GATE.GATE_MODES["etf-rifle"]["weapon_policy"], 13)
        self.assertEqual(GATE.GATE_MODES["etf-rifle"]["expected_damage"], 10)
        self.assertTrue(GATE.GATE_MODES["etf-rifle"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["etf-rifle"]["current_authority_projectile"])
        self.assertTrue(GATE.GATE_MODES["etf-rifle"]["refresh_held_attack"])
        self.assertEqual(GATE.GATE_MODES["phalanx"]["weapon_policy"], 14)
        self.assertEqual(GATE.GATE_MODES["phalanx"]["expected_damage"], 80)
        self.assertTrue(GATE.GATE_MODES["phalanx"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["phalanx"]["current_authority_projectile"])
        self.assertTrue(GATE.GATE_MODES["phalanx"]["refresh_held_attack"])
        self.assertEqual(GATE.GATE_MODES["phalanx-splash"]["weapon_policy"], 14)
        self.assertEqual(GATE.GATE_MODES["phalanx-splash"]["expected_damage"], 93)
        self.assertTrue(GATE.GATE_MODES["phalanx-splash"]["require_projectile_forward"])
        self.assertTrue(GATE.GATE_MODES["phalanx-splash"]["current_authority_projectile"])
        self.assertTrue(GATE.GATE_MODES["phalanx-splash"]["require_current_authority_splash"])
        self.assertTrue(GATE.GATE_MODES["phalanx-splash"]["require_reduced_splash"])
        self.assertTrue(GATE.GATE_MODES["phalanx-splash"]["refresh_held_attack"])
        self.assertEqual(GATE.GATE_MODES["plasma-beam"]["weapon_policy"], 7)
        self.assertEqual(GATE.GATE_MODES["plasma-beam-held"]["expected_damage"], 24)
        self.assertTrue(GATE.GATE_MODES["plasma-beam-held"]["refresh_held_attack"])
        self.assertEqual(GATE.GATE_MODES["plasma-beam-sustained"]["expected_damage"], 256)
        self.assertNotIn("refresh_held_attack", GATE.GATE_MODES["plasma-beam-sustained"])
        self.assertTrue(GATE.GATE_MODES["plasma-beam-sustained"]["require_sustained_hold"])
        self.assertTrue(GATE.GATE_MODES["plasma-beam-release"]["release_after_expected_damage"])
        self.assertEqual(GATE.GATE_MODES["plasma-beam-water-retrace"]["expected_damage"], 4)
        self.assertTrue(GATE.GATE_MODES["plasma-beam-water-retrace"]["require_water_retrace"])
        self.assertEqual(GATE.GATE_MODES["thunderbolt"]["weapon_policy"], 8)
        self.assertEqual(GATE.GATE_MODES["thunderbolt-held"]["expected_damage"], 24)
        self.assertTrue(GATE.GATE_MODES["thunderbolt-held"]["refresh_held_attack"])
        self.assertEqual(GATE.GATE_MODES["thunderbolt-sustained"]["expected_damage"], 256)
        self.assertNotIn("refresh_held_attack", GATE.GATE_MODES["thunderbolt-sustained"])
        self.assertTrue(GATE.GATE_MODES["thunderbolt-sustained"]["require_sustained_hold"])
        self.assertTrue(GATE.GATE_MODES["thunderbolt-release"]["release_after_expected_damage"])
        self.assertEqual(GATE.GATE_MODES["thunderbolt-water-retrace"]["expected_damage"], 4)
        self.assertTrue(GATE.GATE_MODES["thunderbolt-water-retrace"]["require_water_retrace"])
        self.assertEqual(GATE.GATE_MODES["thunderbolt-discharge"]["expected_damage"], 70)
        self.assertTrue(GATE.GATE_MODES["thunderbolt-discharge"]["require_thunderbolt_discharge"])
        self.assertTrue(GATE.GATE_MODES["thunderbolt-discharge"]["current_authority_discharge"])
        self.assertEqual(GATE.GATE_MODES["shotgun"]["weapon_policy"], 3)
        self.assertLess(
            SCRIPT.read_text(encoding="utf-8").index("worr_rewind_canonical_rail_damage_arm"),
            SCRIPT.read_text(encoding="utf-8").index("_fixture_ready_status, fixture_ready_responses"),
        )
        self.assertIn("warmup_enabled", server)
        self.assertNotIn("worr_x86_64.exe", " ".join(server))
        self.assertEqual(client[0], str(Path("C:/stage/worr_x86_64.exe")))
        self.assertEqual(client[client.index("fs_homepath") + 1], str(Path("C:/runtime/shooter")))
        for name, value in (("win_headless", "1"), ("cl_headless", "1"),
                            ("in_enable", "0"), ("in_grab", "0")):
            index = client.index(name)
            self.assertEqual(client[index + 1], value)
        self.assertEqual(client[client.index("cl_async") + 1], "1")
        self.assertNotIn("+attack", client)
        self.assertEqual(client[-3:], ["+set", "name", GATE.SHOOTER_NAME])
        self.assertIn("net_impair_enable", client)
        self.assertEqual(client[client.index("net_impair_latency_ms") + 1], "50")

        reconnect_server = GATE.build_server_command(
            Path("C:/stage/worr_ded_x86_64.exe"), 27960,
            Path("C:/runtime/server"),
            enable_reconnect_minplayer_bypass=True,
        )
        self.assertEqual(
            reconnect_server[reconnect_server.index("cheats") + 1], "1",
        )
        self.assertIn('port, shooter_user_id, "cmd dev_ready", shooter',
                      SCRIPT.read_text(encoding="utf-8"))
        self.assertIn("dev_ready: warmup bypass enabled",
                      SCRIPT.read_text(encoding="utf-8"))

        combined_server = GATE.build_server_command(
            Path("C:/stage/worr_ded_x86_64.exe"), 27960,
            Path("C:/runtime/server"),
            enable_local_action_authority_receipt=True,
            enable_combined_snapshot_shadow=True,
        )
        combined_client = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960, GATE.SHOOTER_NAME,
            Path("C:/runtime/shooter"),
            enable_local_action_authority_receipt=True,
            enable_combined_snapshot_shadow=True,
        )
        self.assertEqual(
            combined_server[combined_server.index(
                "sv_worr_native_snapshot_shadow"
            ) + 1],
            "1",
        )
        self.assertEqual(
            combined_client[combined_client.index(
                "cl_worr_native_snapshot_shadow"
            ) + 1],
            "1",
        )
        presentation_client = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960, GATE.TARGET_NAME,
            Path("C:/runtime/target"),
            enable_local_action_authority_receipt=True,
            enable_combined_snapshot_shadow=True,
            enable_native_snapshot_presentation=True,
        )
        headless_values = [
            presentation_client[index + 1]
            for index, value in enumerate(presentation_client[:-1])
            if value == "cl_headless"
        ]
        self.assertEqual(headless_values, ["1", "0"])
        self.assertEqual(
            presentation_client[presentation_client.index(
                "cg_snapshot_timeline_render"
            ) + 1],
            "3",
        )
        self.assertEqual(
            presentation_client[presentation_client.index(
                "cg_snapshot_timeline_adaptive_interpolation"
            ) + 1],
            "1",
        )
        self.assertEqual(
            presentation_client[presentation_client.index(
                "cg_snapshot_timeline_max_interpolation_delay_ms"
            ) + 1],
            "150",
        )
        presentation_server = GATE.build_server_command(
            Path("C:/stage/worr_ded_x86_64.exe"), 27960,
            Path("C:/runtime/server"),
            enable_native_snapshot_shadow=True,
            enable_native_snapshot_presentation=True,
        )
        self.assertEqual(
            presentation_server[presentation_server.index("sv_novis") + 1],
            "1",
        )
        presentation_shooter = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960, GATE.SHOOTER_NAME,
            Path("C:/runtime/shooter"),
            enable_native_snapshot_shadow=True,
            enable_network_impairment=False,
        )
        self.assertNotIn("net_impair_enable", presentation_shooter)

    def test_native_event_probe_mode_is_event_only_hidden_and_delayed_ack(self) -> None:
        mode = GATE.GATE_MODES["native-event-probe-map-reuse"]
        self.assertTrue(mode["require_native_event_probe_map_reuse"])
        self.assertTrue(mode["require_native_event_status"])
        self.assertTrue(mode["require_schema2_event_batch"])
        self.assertEqual(mode["minimum_schema2_event_batch_events"], 4)
        self.assertTrue(mode["require_schema2_mixed_singletons"])
        self.assertEqual(
            mode["input_command"],
            "+attack 255; -attack 255",
        )
        self.assertNotIn("release_after_expected_damage", mode)
        self.assertEqual(mode["native_event_private_mask"], 0x73)
        self.assertNotIn("require_local_action_authority_receipt", mode)

        server = GATE.build_server_command(
            Path("C:/stage/worr_ded_x86_64.exe"), 27960,
            enable_native_event_shadow=True,
            enable_native_event_probe_impairment=True,
            defer_native_event_probe_impairment=True,
            disable_player_inactivity=True,
        )
        shooter = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960, GATE.SHOOTER_NAME,
            enable_network_impairment=False,
            enable_native_event_shadow=True,
        )
        target = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960, GATE.TARGET_NAME,
            enable_network_impairment=False,
            enable_native_event_shadow=True,
            enable_native_event_probe=True,
            enable_native_event_probe_impairment=True,
            defer_native_event_probe_impairment=True,
        )
        for command in (server, shooter, target):
            self.assertEqual(
                command[command.index(
                    "sv_worr_native_event_shadow"
                    if command is server else "cl_worr_native_event_shadow"
                ) + 1],
                "1",
            )
        self.assertNotIn("sg_local_action_shadow_receipts", server)
        self.assertEqual(server.count("g_inactivity"), 1)
        self.assertEqual(
            server[server.index("g_inactivity") + 1], "0",
        )
        self.assertLess(server.index("g_inactivity"), server.index("+map"))
        self.assertNotIn("cl_worr_native_snapshot_shadow", shooter)
        self.assertNotIn("cl_worr_native_snapshot_shadow", target)
        self.assertEqual(
            server[server.index("net_impair_seed") + 1],
            str(GATE.NATIVE_EVENT_PROBE_SERVER_IMPAIR_SEED),
        )
        self.assertEqual(
            target[target.index("net_impair_seed") + 1],
            str(GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED),
        )
        for command in (server, target):
            self.assertEqual(
                command[command.index("net_impair_enable") + 1], "0",
            )
        for command, profile in (
            (server, GATE.NATIVE_EVENT_PROBE_SERVER_IMPAIR_PROFILE),
            (target, GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE),
        ):
            expected = {
                "net_impair_latency_ms": str(profile["latency"]),
                "net_impair_jitter_ms": str(profile["jitter"]),
                "net_impair_loss_pct": f'{profile["loss"]:g}',
                "net_impair_burst_loss_pct": f'{profile["burst"]:g}',
                "net_impair_burst_length": "3",
                "net_impair_reorder_pct": f'{profile["reorder"]:g}',
                "net_impair_duplicate_pct": f'{profile["duplicate"]:g}',
                "net_impair_upstream_stall_ms": str(profile["upstream_stall"]),
                "net_impair_rate_kbps": str(profile["rate_kbps"]),
            }
            for name, value in expected.items():
                self.assertEqual(command[command.index(name) + 1], value)
        server_profile = GATE.NATIVE_EVENT_PROBE_SERVER_IMPAIR_PROFILE
        client_profile = GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE
        data_floor_ms = max(
            0, server_profile["latency"] - server_profile["jitter"],
        )
        data_ceiling_ms = (
            server_profile["latency"] + server_profile["jitter"]
        )
        ack_floor_ms = (
            max(0, client_profile["latency"] - client_profile["jitter"])
            + client_profile["upstream_stall"]
        )
        self.assertEqual((data_floor_ms, data_ceiling_ms), (0, 0))
        self.assertEqual(
            {
                name: server_profile[name]
                for name in (
                    "loss", "burst", "reorder", "duplicate", "corrupt",
                    "upstream_stall", "rate_kbps",
                )
            },
            {
                "loss": 0.0, "burst": 0.0, "reorder": 0.0,
                "duplicate": 0.0, "corrupt": 0.0,
                "upstream_stall": 0, "rate_kbps": 0,
            },
        )
        self.assertLess(
            data_ceiling_ms,
            GATE.NATIVE_EVENT_PROBE_FRAME_QUANTUM_MS,
        )
        self.assertGreater(
            data_floor_ms + ack_floor_ms,
            GATE.NATIVE_EVENT_PROBE_EVENT_RESEND_MS,
        )
        self.assertGreater(
            ack_floor_ms - GATE.NATIVE_EVENT_PROBE_EVENT_RESEND_MS,
            GATE.NATIVE_EVENT_PROBE_FRAME_QUANTUM_MS,
        )
        self.assertNotIn("net_impair_enable", shooter)
        self.assertEqual(
            [target[index + 1] for index, value in enumerate(target[:-1])
             if value == "cl_headless"],
            ["1", "0"],
        )
        for name, value in (
            ("win_headless", "1"),
            ("in_enable", "0"),
            ("in_grab", "0"),
            ("cg_native_event_preflight_probe", "1"),
            ("s_volume", "0"),
        ):
            self.assertEqual(target[target.index(name) + 1], value)
        self.assertNotIn("s_driver", target)
        self.assertEqual(
            [target[index + 1] for index, value in enumerate(target[:-1])
             if value == "s_enable"],
            ["0", "2"],
        )
        self.assertEqual(
            [shooter[index + 1] for index, value in enumerate(shooter[:-1])
             if value == "cl_headless"],
            ["1"],
        )
        self.assertNotIn("cg_native_event_preflight_probe", shooter)

    def test_native_event_probe_arms_both_deferred_impairment_endpoints(self) -> None:
        with mock.patch.object(
            GATE, "rcon_command", side_effect=("client-toggle", "server-toggle"),
        ) as command:
            responses = GATE.arm_native_event_probe_impairment(27960, 7, 1.0)
        self.assertEqual(responses, ["client-toggle", "server-toggle"])
        self.assertEqual(command.call_args_list, [
            mock.call(27960, 'stuff 7 "net_impair_enable 1"', 1.0),
            mock.call(27960, "net_impair_enable 1", 1.0),
        ])

    def test_native_event_probe_openal_requires_exact_null_backend_proof(self) -> None:
        stdout = "boot\nOpenAL initialized.\n"
        openal_log = (
            GATE.NATIVE_EVENT_PROBE_OPENAL_BACKEND_LINE + "\n" +
            "Opened playback device 'OpenAL Soft on No Output'\n"
        )
        proof = GATE.validate_native_event_probe_openal(stdout, "", openal_log)
        self.assertEqual(proof["backend"], "null")
        self.assertEqual(proof["device"], GATE.NATIVE_EVENT_PROBE_OPENAL_DEVICE)
        self.assertEqual(
            proof["logfile"],
            {"role": "target", "name": "target.openal.log"},
        )

        rejected = (
            ("prefix OpenAL initialized.\n", "", openal_log),
            (stdout, "unexpected stderr\n", openal_log),
            (stdout + "DirectSound initialized\n", "", openal_log),
            (stdout + "Using SDL audio driver: wasapi\n", "", openal_log),
            (stdout, "", "[ALSOFT] (II) Initialized backend \"wave\"\n"),
            (
                stdout,
                "",
                GATE.NATIVE_EVENT_PROBE_OPENAL_BACKEND_LINE + "\n",
            ),
        )
        for invalid_stdout, invalid_stderr, invalid_log in rejected:
            with self.subTest(
                stdout=invalid_stdout,
                stderr=invalid_stderr,
                log=invalid_log,
            ), self.assertRaises(RuntimeError):
                GATE.validate_native_event_probe_openal(
                    invalid_stdout, invalid_stderr, invalid_log,
                )

    def test_native_event_probe_run_reports_normalized_openal_and_impairment_windows(
        self,
    ) -> None:
        mode = GATE.GATE_MODES["native-event-probe-map-reuse"]
        first_probe = completed_native_event_probe_row()
        second_baseline = native_event_probe_row(
            map_generation=2, map_end_count=1, authority_epoch=12,
        )
        second_probe = completed_native_event_probe_row(
            map_generation=2, map_end_count=1, authority_epoch=12,
        )
        first_phase = {
            "canonical_status": PASS_STATUS,
            "zero_baseline": native_event_probe_row(),
            "probe_status": first_probe,
            "event_sender_status": native_event_sender_row(stream_epoch=9),
            "native_status": {
                "server_peers": [
                    GATE.parse_native_status_rows(
                        native_base_server_line(slot),
                        GATE.NATIVE_SERVER_STATUS_MARKER,
                    )[0]
                    for slot in (4, 7)
                ],
            },
        }
        second_phase = {
            "canonical_status": PASS_STATUS,
            "zero_baseline": second_baseline,
            "probe_status": second_probe,
            "event_sender_status": native_event_sender_row(stream_epoch=10),
        }
        client_baseline = {"endpoint": "client-baseline"}
        server_baseline = {"endpoint": "server-baseline"}
        client_post = {"endpoint": "client-post"}
        server_post = {"endpoint": "server-post"}
        client_delta = {"endpoint": "client-delta"}
        server_delta = {"endpoint": "server-delta"}
        aggregate = {"proof": "all-claimed-dimensions"}
        server = mock.Mock(name="server")
        shooter = mock.Mock(name="shooter")
        target = mock.Mock(name="target")
        openal_log = (
            GATE.NATIVE_EVENT_PROBE_OPENAL_BACKEND_LINE + "\n" +
            "Opened playback device 'OpenAL Soft on No Output'\n"
        )

        def read_runtime_log(path: Path) -> str:
            by_name = {
                "server.stdout.log":
                    "Going from cs_primed to cs_spawned\n" * 2,
                "server.stderr.log": "",
                "shooter.stdout.log": "Serverdata packet received\n",
                "shooter.stderr.log": "",
                "target.stdout.log":
                    "Serverdata packet received\nOpenAL initialized.\n",
                "target.stderr.log": "",
                "spectator.stdout.log": "",
                "spectator.stderr.log": "",
                "target.openal.log": openal_log,
            }
            return by_name[path.name]

        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            with contextlib.ExitStack() as stack:
                start = stack.enter_context(mock.patch.object(
                    GATE,
                    "start_headless_process",
                    side_effect=(server, shooter, target),
                ))
                stack.enter_context(mock.patch.object(GATE, "wait_for_marker"))
                stack.enter_context(mock.patch.object(
                    GATE, "wait_for_marker_count",
                ))
                stack.enter_context(mock.patch.object(
                    GATE,
                    "wait_for_client_user_ids",
                    return_value=(4, 7, None, ["status"]),
                ))
                stack.enter_context(mock.patch.object(
                    GATE, "rcon_command", return_value="print\n",
                ))
                stack.enter_context(mock.patch.object(
                    GATE,
                    "capture_fixture_liveness_baseline",
                    return_value=({}, {}),
                ))
                client_impairment_capture = stack.enter_context(mock.patch.object(
                    GATE,
                    "capture_client_impairment_status",
                    side_effect=(
                        (client_baseline, ["client-baseline"]),
                        (client_post, ["client-post"]),
                    ),
                ))
                stack.enter_context(mock.patch.object(
                    GATE,
                    "capture_server_impairment_status",
                    side_effect=(
                        (server_baseline, ["server-baseline"]),
                        (server_post, ["server-post"]),
                    ),
                ))
                phase_runner = stack.enter_context(mock.patch.object(
                    GATE,
                    "run_native_event_probe_phase",
                    side_effect=(
                        (first_phase, ["first-phase"]),
                        (second_phase, ["second-phase"]),
                    ),
                ))
                stack.enter_context(mock.patch.object(
                    GATE,
                    "reload_native_event_probe_map",
                    return_value=["reload"],
                ))
                stack.enter_context(mock.patch.object(
                    GATE,
                    "validate_native_event_probe_map_reuse",
                    return_value={"lifecycle": "valid"},
                ))
                impairment_delta = stack.enter_context(mock.patch.object(
                    GATE,
                    "native_event_probe_impairment_delta",
                    side_effect=(client_delta, server_delta),
                ))
                stack.enter_context(mock.patch.object(
                    GATE,
                    "validate_native_event_probe_impairment_pair",
                    return_value=aggregate,
                ))
                stack.enter_context(mock.patch.object(
                    GATE,
                    "wait_for_native_base_shadow",
                    return_value=(
                        {"clients": {}, "server_peers": []},
                        ["native-status"],
                    ),
                ))
                stack.enter_context(mock.patch.object(
                    GATE,
                    "verify_post_proof_client_liveness",
                    return_value="final-status",
                ))
                stack.enter_context(mock.patch.object(
                    GATE,
                    "validate_status",
                    side_effect=lambda row, _mode: row,
                ))
                stack.enter_context(mock.patch.object(
                    GATE, "read_text", side_effect=read_runtime_log,
                ))
                stack.enter_context(mock.patch.object(
                    GATE, "file_sha256", return_value="sha256",
                ))
                stack.enter_context(mock.patch.object(
                    GATE, "terminate", return_value=True,
                ))
                stack.enter_context(mock.patch.object(
                    GATE.os,
                    "environ",
                    {"KEEP_ME": "yes", "SDL_AUDIODRIVER": "forbidden"},
                ))
                result = GATE.run_once(
                    server_command=["server", "net_port", "27960"],
                    shooter_command=["shooter"],
                    target_command=["target"],
                    working_dir=run_root,
                    run_root=run_root,
                    timeout=1.0,
                    mode=mode,
                )

        self.assertNotIn("env", start.call_args_list[1].kwargs)
        target_environment = start.call_args_list[2].kwargs["env"]
        self.assertEqual(target_environment["KEEP_ME"], "yes")
        self.assertNotIn("SDL_AUDIODRIVER", target_environment)
        self.assertEqual(target_environment["ALSOFT_DRIVERS"], "null")
        self.assertEqual(target_environment["ALSOFT_LOGLEVEL"], "3")
        self.assertEqual(
            Path(target_environment["ALSOFT_LOGFILE"]),
            (run_root / "target.openal.log").resolve(),
        )
        evidence = result["native_event_probe_map_reuse"]
        self.assertEqual(evidence["client_impairment_baseline"], client_baseline)
        self.assertEqual(evidence["server_impairment_baseline"], server_baseline)
        self.assertEqual(evidence["client_impairment_delta"], client_delta)
        self.assertEqual(evidence["server_impairment_delta"], server_delta)
        self.assertEqual(evidence["impairment_aggregate"], aggregate)
        self.assertEqual(
            evidence["target_environment"],
            {
                "ALSOFT_DRIVERS": "null",
                "ALSOFT_LOGLEVEL": "3",
                "ALSOFT_LOGFILE": {
                    "role": "target",
                    "name": "target.openal.log",
                },
            },
        )
        self.assertEqual(evidence["audio_backend"], result["native_event_probe_audio"])
        self.assertEqual(result["native_event_probe_audio"]["backend"], "null")
        self.assertEqual(evidence["probe_client"], "target")
        self.assertEqual(
            client_impairment_capture.call_args_list,
            [
                mock.call(
                    27960, 7, target,
                    run_root / "target.stdout.log", 1.0,
                ),
                mock.call(
                    27960, 7, target,
                    run_root / "target.stdout.log", 1.0,
                ),
            ],
        )
        self.assertEqual(
            [call.kwargs["target_user_id"] for call in phase_runner.call_args_list],
            [7, 7],
        )
        self.assertIsNone(
            phase_runner.call_args_list[0].kwargs.get(
                "prior_native_server_peers"
            )
        )
        self.assertEqual(
            phase_runner.call_args_list[1].kwargs[
                "prior_native_server_peers"
            ],
            first_phase["native_status"]["server_peers"],
        )
        self.assertEqual(
            impairment_delta.call_args_list,
            [
                mock.call(client_baseline, client_post),
                mock.call(server_baseline, server_post),
            ],
        )

    def test_native_event_probe_parser_requires_exact_336_byte_row(self) -> None:
        row = completed_native_event_probe_row()
        line = native_event_probe_line(row)
        self.assertEqual(GATE.parse_native_event_probe_status(line), row)
        self.assertEqual(
            GATE.NATIVE_EVENT_PROBE_STATUS_FIELDS,
            NATIVE_EVENT_PROBE_STATUS_FIELDS_V1,
        )
        self.assertEqual(len(NATIVE_EVENT_PROBE_STATUS_FIELDS_V1), 51)

        with self.assertRaisesRegex(RuntimeError, "exact field order changed"):
            GATE.parse_native_event_probe_status(
                line.replace("valid=1 schema=3", "schema=3 valid=1", 1)
            )
        with self.assertRaisesRegex(RuntimeError, "exact field order changed"):
            GATE.parse_native_event_probe_status(
                line.replace(" probe_k7=0", "", 1)
            )
        with self.assertRaisesRegex(RuntimeError, "exact field order changed"):
            GATE.parse_native_event_probe_status(line + " extension=1")
        with self.assertRaisesRegex(RuntimeError, "fixed lowercase hex"):
            GATE.parse_native_event_probe_status(
                line.replace("1020304050607080", "ABCDEFABCDEFABCD", 1)
            )
        with self.assertRaisesRegex(RuntimeError, "exact field order changed"):
            GATE.parse_native_event_probe_status(
                GATE.NATIVE_EVENT_PROBE_STATUS_MARKER + " valid=0"
            )
        with self.assertRaisesRegex(RuntimeError, "observed=2"):
            GATE.parse_native_event_probe_status(line + "\n" + line)
        with self.assertRaises(RuntimeError):
            GATE.parse_native_event_probe_status("synthetic-prefix " + line)

    def test_native_event_probe_checkpoint_grammar_and_identity_are_exact(self) -> None:
        applied = native_event_probe_checkpoint_row()
        line = native_event_probe_checkpoint_line(applied)
        self.assertEqual(
            GATE.NATIVE_EVENT_PROBE_CHECKPOINT_FIELDS,
            NATIVE_EVENT_PROBE_CHECKPOINT_FIELDS_V1,
        )
        self.assertEqual(
            GATE.parse_native_event_probe_checkpoint_rows(line),
            [applied],
        )
        self.assertEqual(
            GATE.validate_native_event_probe_checkpoint(
                applied,
                expected_result=GATE.NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED,
                expected_map_generation=1,
                expected_authority_epoch=11,
                expected_checkpoint_id=(1 << 32) | 11,
            ),
            applied,
        )

        malformed_lines = (
            line.replace("valid=1 schema=1", "schema=1 valid=1", 1),
            line.replace(" checkpoint_id=4294967307", "", 1),
            line + " extension=1",
            line.replace("checkpoint_id=4294967307", "checkpoint_id=0x10000000b"),
        )
        for malformed in malformed_lines:
            with self.subTest(line=malformed), self.assertRaises(RuntimeError):
                GATE.parse_native_event_probe_checkpoint_rows(malformed)

        for name, value in (
            ("valid", 0),
            ("schema", 2),
            ("size", 24),
            ("result", GATE.NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED),
            ("map_generation", 2),
            ("authority_epoch", 12),
            ("checkpoint_id", (2 << 32) | 12),
        ):
            rejected = {**applied, name: value}
            with self.subTest(field=name), self.assertRaisesRegex(
                RuntimeError, "checkpoint receipt mismatch",
            ):
                GATE.validate_native_event_probe_checkpoint(
                    rejected,
                    expected_result=GATE.NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED,
                    expected_map_generation=1,
                    expected_authority_epoch=11,
                    expected_checkpoint_id=(1 << 32) | 11,
                )

    def test_native_event_probe_checkpoint_is_single_send_and_idempotent(self) -> None:
        pre_checkpoint = completed_native_event_probe_row()
        checkpoint_id = (1 << 32) | 11
        applied = native_event_probe_checkpoint_row(checkpoint_id=checkpoint_id)
        duplicate = native_event_probe_checkpoint_row(
            result=GATE.NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED,
            checkpoint_id=checkpoint_id,
        )
        process = mock.Mock()
        process.poll.return_value = None
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "shooter.log"
            path.write_text("connected\n", encoding="utf-8")
            receipt_batches = iter((
                (applied, duplicate),
                (duplicate, duplicate),
            ))

            def append_receipt(
                _port: int, command: str, _timeout: float,
            ) -> str:
                self.assertEqual(
                    command,
                    'stuff 7 "cl_worr_native_event_probe_checkpoint '
                    '1 11 4294967307"',
                )
                with path.open("a", encoding="utf-8") as output:
                    for receipt in next(receipt_batches):
                        output.write(
                            native_event_probe_checkpoint_line(receipt) + "\n"
                        )
                return "print\n"

            with (
                mock.patch.object(
                    GATE, "rcon_command_once", side_effect=append_receipt,
                ) as send_once,
                mock.patch.object(
                    GATE, "rcon_command",
                    side_effect=AssertionError("checkpoint command retried"),
                ),
            ):
                evidence, responses = GATE.issue_native_event_probe_checkpoint(
                    27960, 7, process, path, 1.0, pre_checkpoint,
                )

        self.assertEqual(send_once.call_count, 2)
        self.assertEqual(len(responses), 2)
        self.assertEqual(evidence, {
            "command": "cl_worr_native_event_probe_checkpoint 1 11 4294967307",
            "checkpoint_id": checkpoint_id,
            "busy_receipt_batches": [],
            "applied_receipt": applied,
            "applied_receipt_batch": [applied, duplicate],
            "duplicate_receipt": duplicate,
            "duplicate_receipt_batch": [duplicate, duplicate],
        })

    def test_native_event_probe_checkpoint_retries_only_busy_until_applied(self) -> None:
        pre_checkpoint = completed_native_event_probe_row()
        checkpoint_id = (1 << 32) | 11
        busy = native_event_probe_checkpoint_row(
            result=GATE.NATIVE_EVENT_PROBE_CHECKPOINT_BUSY,
            checkpoint_id=checkpoint_id,
        )
        applied = native_event_probe_checkpoint_row(checkpoint_id=checkpoint_id)
        duplicate = native_event_probe_checkpoint_row(
            result=GATE.NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED,
            checkpoint_id=checkpoint_id,
        )
        process = mock.Mock()
        process.poll.return_value = None
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "shooter.log"
            path.write_text("connected\n", encoding="utf-8")
            receipt_batches = iter(((busy, busy), (applied,), (duplicate,)))

            def append_receipt(
                _port: int, command: str, _timeout: float,
            ) -> str:
                self.assertEqual(
                    command,
                    'stuff 7 "cl_worr_native_event_probe_checkpoint '
                    '1 11 4294967307"',
                )
                with path.open("a", encoding="utf-8") as output:
                    for receipt in next(receipt_batches):
                        output.write(
                            native_event_probe_checkpoint_line(receipt) + "\n"
                        )
                return "print\n"

            with (
                mock.patch.object(
                    GATE, "rcon_command_once", side_effect=append_receipt,
                ) as send_once,
                mock.patch.object(
                    GATE, "rcon_command",
                    side_effect=AssertionError("checkpoint command was resent"),
                ),
            ):
                evidence, responses = GATE.issue_native_event_probe_checkpoint(
                    27960, 7, process, path, 1.0, pre_checkpoint,
                )

        self.assertEqual(send_once.call_count, 3)
        self.assertEqual(len(responses), 3)
        self.assertEqual(evidence["busy_receipt_batches"], [[busy, busy]])
        self.assertEqual(evidence["applied_receipt"], applied)
        self.assertEqual(evidence["duplicate_receipt"], duplicate)

    def test_native_event_probe_checkpoint_times_out_on_persistent_busy(self) -> None:
        pre_checkpoint = completed_native_event_probe_row()
        checkpoint_id = (1 << 32) | 11
        busy = native_event_probe_checkpoint_row(
            result=GATE.NATIVE_EVENT_PROBE_CHECKPOINT_BUSY,
            checkpoint_id=checkpoint_id,
        )
        process = mock.Mock()
        process.poll.return_value = None
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "shooter.log"
            path.write_text("connected\n", encoding="utf-8")

            def append_busy(
                _port: int, _command: str, _timeout: float,
            ) -> str:
                with path.open("a", encoding="utf-8") as output:
                    output.write(native_event_probe_checkpoint_line(busy) + "\n")
                return "print\n"

            with mock.patch.object(
                GATE, "rcon_command_once", side_effect=append_busy,
            ) as send_once, self.assertRaisesRegex(
                RuntimeError, "timed out.*after BUSY",
            ):
                GATE.issue_native_event_probe_checkpoint(
                    27960, 7, process, path, 0.01, pre_checkpoint,
                )
        self.assertEqual(send_once.call_count, 1)

    def test_native_event_probe_checkpoint_never_retries_nonbusy_or_wrong_identity(
        self,
    ) -> None:
        pre_checkpoint = completed_native_event_probe_row()
        checkpoint_id = (1 << 32) | 11
        cases = (
            native_event_probe_checkpoint_row(
                result=8, checkpoint_id=checkpoint_id,
            ),
            native_event_probe_checkpoint_row(
                result=GATE.NATIVE_EVENT_PROBE_CHECKPOINT_BUSY,
                checkpoint_id=checkpoint_id + 1,
            ),
        )
        for receipt in cases:
            with self.subTest(receipt=receipt), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / "shooter.log"
                path.write_text("connected\n", encoding="utf-8")
                process = mock.Mock()
                process.poll.return_value = None

                def append_receipt(
                    _port: int, _command: str, _timeout: float,
                ) -> str:
                    with path.open("a", encoding="utf-8") as output:
                        output.write(
                            native_event_probe_checkpoint_line(receipt) + "\n"
                        )
                    return "print\n"

                with mock.patch.object(
                    GATE, "rcon_command_once", side_effect=append_receipt,
                ) as send_once, self.assertRaises(RuntimeError):
                    GATE.issue_native_event_probe_checkpoint(
                        27960, 7, process, path, 1.0, pre_checkpoint,
                    )
                self.assertEqual(send_once.call_count, 1)

    def test_native_event_probe_validation_proves_parity_and_zero_dispatch(self) -> None:
        baseline = native_event_probe_row()
        completed = completed_native_event_probe_row()
        self.assertEqual(
            GATE.validate_native_event_probe_status(
                baseline, require_activity=False,
            ),
            baseline,
        )
        self.assertEqual(
            GATE.validate_native_event_probe_status(
                completed, require_activity=True,
            ),
            completed,
        )
        failures = {
            "raw_action_records": 4,
            "probe_action_chain_hash": 7,
            "raw_k3": 2,
            "raw_effect_dispatches": 1,
            "native_effect_dispatches": 1,
            "authority_requires_resync": 1,
            "presenter_commit_mismatches": 1,
            "authoritative_presentations": 2,
            "authority_ref_body_joins": 0,
        }
        for name, value in failures.items():
            with self.subTest(field=name):
                invalid = dict(completed)
                invalid[name] = value
                with self.assertRaises(RuntimeError):
                    GATE.validate_native_event_probe_status(
                        invalid, require_activity=True,
                    )
        dirty_baseline = dict(baseline)
        dirty_baseline["probe_k3"] = 1
        with self.assertRaisesRegex(RuntimeError, "not zero-reset"):
            GATE.validate_native_event_probe_status(
                dirty_baseline, require_activity=False,
            )

    def test_native_event_probe_rejects_unreachable_lifecycle_kind_and_join_rows(
        self,
    ) -> None:
        completed = completed_native_event_probe_row()

        impossible_lifecycle = dict(completed)
        impossible_lifecycle["map_generation"] = 2
        with self.assertRaises(RuntimeError):
            GATE.validate_native_event_probe_status(
                impossible_lifecycle, require_activity=True,
            )

        unsupported_kind = dict(completed)
        for prefix in ("raw", "probe"):
            unsupported_kind[f"{prefix}_k2"] = 0
            unsupported_kind[f"{prefix}_k1"] = 1
        with self.assertRaises(RuntimeError):
            GATE.validate_native_event_probe_status(
                unsupported_kind, require_activity=True,
            )
        self.assertEqual(
            GATE.validate_native_event_probe_status(
                unsupported_kind, require_activity=None,
            ),
            unsupported_kind,
        )

        missing_damage = dict(completed)
        for prefix in ("raw", "probe"):
            missing_damage[f"{prefix}_k2"] = 3
            missing_damage[f"{prefix}_k5"] = 0
        with self.assertRaisesRegex(
            RuntimeError, "exact production kind profile",
        ):
            GATE.validate_native_event_probe_status(
                missing_damage, require_activity=True,
            )

        missing_join = dict(completed)
        missing_join["authority_ref_body_joins"] = 2
        with self.assertRaises(RuntimeError):
            GATE.validate_native_event_probe_status(
                missing_join, require_activity=True,
            )

    def test_native_event_probe_phase_scope_rejects_rotation_and_rollover_races(
        self,
    ) -> None:
        def checkpoint_evidence(
            map_generation: int, authority_epoch: int,
        ) -> dict[str, object]:
            checkpoint_id = (map_generation << 32) | authority_epoch
            applied = native_event_probe_checkpoint_row(
                map_generation=map_generation,
                authority_epoch=authority_epoch,
                checkpoint_id=checkpoint_id,
            )
            duplicate = dict(
                applied,
                result=GATE.NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED,
            )
            return {
                "command": (
                    "cl_worr_native_event_probe_checkpoint "
                    f"{map_generation} {authority_epoch} {checkpoint_id}"
                ),
                "checkpoint_id": checkpoint_id,
                "applied_receipt": applied,
                "duplicate_receipt": duplicate,
            }

        pre_checkpoint = completed_native_event_probe_row()
        zero_baseline = native_event_probe_row()
        completed = completed_native_event_probe_row()
        checkpoint = checkpoint_evidence(1, 11)
        self.assertEqual(
            GATE.validate_native_event_probe_phase_scope(
                pre_checkpoint, checkpoint, zero_baseline, completed,
            ),
            {
                "map_generation": 1,
                "map_end_count": 0,
                "authority_epoch": 11,
                "checkpoint_id": (1 << 32) | 11,
            },
        )

        rotated_rows = (
            (
                "zero-map",
                native_event_probe_row(
                    map_generation=2, map_end_count=1, authority_epoch=11,
                ),
                completed,
            ),
            (
                "zero-authority",
                native_event_probe_row(authority_epoch=12),
                completed,
            ),
            (
                "completed-map",
                zero_baseline,
                completed_native_event_probe_row(
                    map_generation=2, map_end_count=1, authority_epoch=11,
                ),
            ),
            (
                "completed-authority",
                zero_baseline,
                completed_native_event_probe_row(authority_epoch=12),
            ),
        )
        for label, invalid_zero, invalid_completed in rotated_rows:
            with self.subTest(race=label), self.assertRaisesRegex(
                RuntimeError, "rotated inside one checkpoint window",
            ):
                GATE.validate_native_event_probe_phase_scope(
                    pre_checkpoint, checkpoint, invalid_zero,
                    invalid_completed,
                )

        for field, value in (
            ("map_generation", 2),
            ("authority_epoch", 12),
            ("checkpoint_id", (2 << 32) | 12),
        ):
            invalid_checkpoint = {
                **checkpoint,
                "applied_receipt": {
                    **checkpoint["applied_receipt"], field: value,
                },
            }
            with self.subTest(receipt_field=field), self.assertRaises(RuntimeError):
                GATE.validate_native_event_probe_phase_scope(
                    pre_checkpoint, invalid_checkpoint, zero_baseline,
                    completed,
                )

        with self.assertRaisesRegex(RuntimeError, "identity escaped"):
            GATE.validate_native_event_probe_phase_scope(
                pre_checkpoint,
                {**checkpoint, "checkpoint_id": checkpoint["checkpoint_id"] + 1},
                zero_baseline,
                completed,
            )
        with self.assertRaisesRegex(RuntimeError, "command escaped"):
            GATE.validate_native_event_probe_phase_scope(
                pre_checkpoint,
                {**checkpoint, "command": checkpoint["command"] + " 0"},
                zero_baseline,
                completed,
            )
        invalid_duplicate = {
            **checkpoint,
            "duplicate_receipt": {
                **checkpoint["duplicate_receipt"], "authority_epoch": 12,
            },
        }
        with self.assertRaisesRegex(RuntimeError, "receipt mismatch"):
            GATE.validate_native_event_probe_phase_scope(
                pre_checkpoint, invalid_duplicate, zero_baseline, completed,
            )

        # The maximal valid u32 lifecycle maps to the maximal u64 checkpoint
        # identity without wrapping. A raced post-wrap row must still fail.
        maximum_map = 0xffffffff
        maximum_end = 0xfffffffe
        maximum_epoch = 0xffffffff
        maximum_pre = completed_native_event_probe_row(
            map_generation=maximum_map,
            map_end_count=maximum_end,
            authority_epoch=maximum_epoch,
        )
        maximum_zero = native_event_probe_row(
            map_generation=maximum_map,
            map_end_count=maximum_end,
            authority_epoch=maximum_epoch,
        )
        maximum_completed = completed_native_event_probe_row(
            map_generation=maximum_map,
            map_end_count=maximum_end,
            authority_epoch=maximum_epoch,
        )
        maximum_checkpoint = checkpoint_evidence(maximum_map, maximum_epoch)
        self.assertEqual(
            GATE.validate_native_event_probe_phase_scope(
                maximum_pre, maximum_checkpoint, maximum_zero,
                maximum_completed,
            )["checkpoint_id"],
            0xffffffffffffffff,
        )
        with self.assertRaisesRegex(
            RuntimeError, "rotated inside one checkpoint window",
        ):
            GATE.validate_native_event_probe_phase_scope(
                maximum_pre,
                maximum_checkpoint,
                native_event_probe_row(
                    map_generation=1, map_end_count=0, authority_epoch=1,
                ),
                maximum_completed,
            )

    def test_native_event_probe_map_reuse_requires_generation_end_and_epoch(self) -> None:
        first = completed_native_event_probe_row(
            map_generation=1, map_end_count=0, authority_epoch=11,
        )
        second_baseline = native_event_probe_row(
            map_generation=2, map_end_count=1, authority_epoch=12,
        )
        second = completed_native_event_probe_row(
            map_generation=2, map_end_count=1, authority_epoch=12,
        )
        self.assertEqual(
            GATE.validate_native_event_probe_map_reuse(
                first, second_baseline, second,
            ),
            {
                "generation_increment": 1,
                "map_end_increment": 1,
                "authority_epoch_rotated": True,
            },
        )
        for field, value, message in (
            ("map_generation", 3, "generation"),
            ("map_end_count", 2, "end_count|map end"),
            ("authority_epoch", 11, "authority epoch"),
        ):
            invalid = dict(second_baseline)
            invalid[field] = value
            with self.subTest(field=field), self.assertRaisesRegex(
                RuntimeError, message,
            ):
                GATE.validate_native_event_probe_map_reuse(
                    first, invalid, second,
                )

    def test_native_event_sender_parser_requires_ack_retry_release_closure(self) -> None:
        baseline = native_event_sender_row(
            confirms=13,
            snapshots_queued=17,
            candidates_queued=19,
            candidates_promoted=19,
            event_acks=19,
            prepared=23,
            confirmed=23,
            first_sends=9,
            retries=11,
        )
        row = native_event_sender_post_row(baseline)
        line = native_event_sender_line(row)
        parsed = GATE.parse_native_event_sender_status_rows(line)
        self.assertEqual(parsed, [row])
        self.assertEqual(
            GATE.validate_native_event_sender_baseline(
                baseline, expected_slot=7,
            ), baseline,
        )
        command_ack_output_due = dict(baseline, output_due=1)
        self.assertEqual(
            GATE.validate_native_event_sender_baseline(
                command_ack_output_due, expected_slot=7,
            ), command_ack_output_due,
        )
        validated, delta = GATE.validate_native_event_sender_status(
            parsed[0], baseline=baseline, expected_slot=7,
            expected_events=3, require_schema2_event_batch=True,
        )
        self.assertEqual(validated, row)
        self.assertEqual(delta, {
            "stream_epoch": 9,
            "counters": {
                "confirms": 1,
                "snapshots_queued": 2,
                "queue_failures": 0,
                "candidates_queued": 3,
                "candidates_promoted": 3,
                "descriptor_acks": 0,
                "event_acks": 3,
                "prepared": 2,
                "confirmed": 2,
                "rejected": 0,
                "first_sends": 1,
                "retries": 2,
                "schema2_batches_promoted": 1,
                "schema2_events_promoted": 3,
            },
        })
        self.assertEqual(parsed[0]["schema2_batches_promoted"], 1)
        self.assertEqual(parsed[0]["schema2_events_promoted"], 3)
        five_event_row = native_event_sender_post_row(
            baseline, event_count=5,
            schema2_events_promoted=(
                baseline["schema2_events_promoted"] + 4
            ),
        )
        validated_five, five_delta = GATE.validate_native_event_sender_status(
            five_event_row, baseline=baseline, expected_slot=7,
            expected_events=5, require_schema2_event_batch=True,
            minimum_schema2_event_batch_events=4,
            require_schema2_mixed_singletons=True,
        )
        self.assertEqual(validated_five, five_event_row)
        self.assertEqual(five_delta["counters"]["candidates_promoted"], 5)
        self.assertEqual(five_delta["counters"]["schema2_batches_promoted"], 1)
        self.assertEqual(five_delta["counters"]["schema2_events_promoted"], 4)
        for invalid_batch_events in (0, 1, 2, 3, 5):
            invalid_five = dict(
                five_event_row,
                schema2_events_promoted=(
                    baseline["schema2_events_promoted"] +
                    invalid_batch_events
                ),
            )
            with self.subTest(
                explicit_schema2_events=invalid_batch_events,
            ), self.assertRaisesRegex(RuntimeError, "schema-2"):
                GATE.validate_native_event_sender_status(
                    invalid_five, baseline=baseline, expected_slot=7,
                    expected_events=5, require_schema2_event_batch=True,
                    minimum_schema2_event_batch_events=4,
                    require_schema2_mixed_singletons=True,
                )
        two_batch_row = native_event_sender_post_row(
            baseline, event_count=8,
            schema2_batches_promoted=(
                baseline["schema2_batches_promoted"] + 2
            ),
            schema2_events_promoted=(
                baseline["schema2_events_promoted"] + 5
            ),
        )
        self.assertEqual(
            GATE.validate_native_event_sender_status(
                two_batch_row, baseline=baseline, expected_slot=7,
                expected_events=8, require_schema2_event_batch=True,
                minimum_schema2_event_batch_events=3,
                require_schema2_mixed_singletons=True,
            )[1]["counters"]["schema2_events_promoted"],
            5,
        )
        two_pairs_only = dict(
            two_batch_row,
            schema2_events_promoted=(
                baseline["schema2_events_promoted"] + 4
            ),
        )
        with self.assertRaisesRegex(RuntimeError, "schema-2"):
            GATE.validate_native_event_sender_status(
                two_pairs_only, baseline=baseline, expected_slot=7,
                expected_events=8, require_schema2_event_batch=True,
                minimum_schema2_event_batch_events=3,
                require_schema2_mixed_singletons=True,
            )
        for field, value in (
            ("schema2_batches_promoted", baseline["schema2_batches_promoted"]),
            ("schema2_batches_promoted", baseline["schema2_batches_promoted"] + 2),
            ("schema2_events_promoted", baseline["schema2_events_promoted"] + 1),
            ("schema2_events_promoted", baseline["schema2_events_promoted"] + 4),
        ):
            with self.subTest(schema2_field=field, schema2_value=value):
                invalid = dict(row, **{field: value})
                with self.assertRaisesRegex(RuntimeError, "schema-2"):
                    GATE.validate_native_event_sender_status(
                        invalid, baseline=baseline, expected_slot=7,
                        expected_events=3, require_schema2_event_batch=True,
                    )
        # Unknown-at-poll-time counts must still prove a positive, exactly
        # closed window; the client probe binds the resulting count later.
        self.assertEqual(
            GATE.validate_native_event_sender_status(
                row, baseline=baseline, expected_slot=7,
                expected_events=None,
            )[1], delta,
        )

        failures = {
            "retired_sender": 1,
            "retained": 1,
            "event_acks": row["event_acks"] - 1,
            "retries": baseline["retries"],
            "descriptor_acks": 0,
            "queue_failures": 1,
            "confirmed": row["confirmed"] - 1,
        }
        for name, value in failures.items():
            with self.subTest(field=name):
                invalid = dict(row)
                invalid[name] = value
                with self.assertRaises(RuntimeError):
                    GATE.validate_native_event_sender_status(
                        invalid, baseline=baseline, expected_slot=7,
                        expected_events=3,
                    )

        peer_output_due = dict(row, output_due=1)
        validated_output_due, output_due_delta = (
            GATE.validate_native_event_sender_status(
                peer_output_due, baseline=command_ack_output_due,
                expected_slot=7, expected_events=3,
            )
        )
        self.assertEqual(validated_output_due, peer_output_due)
        self.assertEqual(output_due_delta, delta)
        with self.assertRaisesRegex(RuntimeError, "output_due was not boolean"):
            GATE.validate_native_event_sender_baseline(
                dict(baseline, output_due=2), expected_slot=7,
            )

        rotated = dict(row, stream_epoch=10)
        with self.assertRaisesRegex(RuntimeError, "stream rotated"):
            GATE.validate_native_event_sender_status(
                rotated, baseline=baseline, expected_slot=7,
                expected_events=3,
            )
        rolled_over = dict(
            row, candidates_queued=0, candidates_promoted=0, event_acks=0,
        )
        with self.assertRaisesRegex(RuntimeError, "not monotonic"):
            GATE.validate_native_event_sender_status(
                rolled_over, baseline=baseline, expected_slot=7,
                expected_events=3,
            )
        unclosed_baseline = dict(
            baseline, candidates_promoted=baseline["candidates_promoted"] - 1,
        )
        with self.assertRaisesRegex(RuntimeError, "baseline was not fully"):
            GATE.validate_native_event_sender_baseline(
                unclosed_baseline, expected_slot=7,
            )
        with self.assertRaisesRegex(RuntimeError, "exact field order changed"):
            GATE.parse_native_event_sender_status_rows(line + " extra=1")
        without_schema2_events = " ".join(
            token for token in line.split()
            if not token.startswith("schema2_events_promoted=")
        )
        with self.assertRaisesRegex(RuntimeError, "exact field order changed"):
            GATE.parse_native_event_sender_status_rows(
                without_schema2_events,
            )

    def test_native_event_probe_determinism_signature_tracks_semantics(self) -> None:
        sender_delta = {
            "stream_epoch": 9,
            "counters": {
                "candidates_queued": 5,
                "candidates_promoted": 5,
                "event_acks": 5,
            },
        }
        evidence = {
            "first_phase": {
                "probe_status": completed_native_event_probe_row(),
                "event_sender_status": native_event_sender_row(),
                "event_sender_delta": sender_delta,
            },
            "second_phase": {
                "probe_status": completed_native_event_probe_row(
                    map_generation=2, map_end_count=1, authority_epoch=12,
                ),
                "event_sender_status": native_event_sender_row(stream_epoch=10),
                "event_sender_delta": {
                    **sender_delta,
                    "stream_epoch": 10,
                },
            },
        }
        signature = GATE.native_event_probe_determinism_signature(evidence)
        self.assertEqual(
            signature,
            GATE.native_event_probe_determinism_signature(evidence),
        )
        timing_changed = {
            **evidence,
            "second_phase": {
                **evidence["second_phase"],
                "probe_status": {
                    **evidence["second_phase"]["probe_status"],
                    "raw_action_chain_hash": 1,
                    "probe_action_chain_hash": 1,
                    "raw_effect_chain_hash": 2,
                },
            },
        }
        self.assertEqual(
            signature,
            GATE.native_event_probe_determinism_signature(timing_changed),
        )

        cumulative_pretraffic_changed = {
            **evidence,
            "second_phase": {
                **evidence["second_phase"],
                "event_sender_status": native_event_sender_row(
                    stream_epoch=10,
                    candidates_queued=103,
                    candidates_promoted=103,
                    event_acks=103,
                ),
            },
        }
        self.assertEqual(
            signature,
            GATE.native_event_probe_determinism_signature(
                cumulative_pretraffic_changed,
            ),
        )

        semantic_change = {
            **evidence,
            "second_phase": {
                **evidence["second_phase"],
                "probe_status": {
                    **evidence["second_phase"]["probe_status"],
                    "raw_k2": 1,
                    "raw_k3": 2,
                    "probe_k2": 1,
                    "probe_k3": 2,
                },
            },
        }
        self.assertNotEqual(
            signature,
            GATE.native_event_probe_determinism_signature(semantic_change),
        )

        within_run_mismatch = completed_native_event_probe_row(
            probe_action_chain_hash=1,
        )
        with self.assertRaisesRegex(RuntimeError, "hashes differ"):
            GATE.validate_native_event_probe_status(
                within_run_mismatch, require_activity=True,
            )
        with self.assertRaisesRegex(RuntimeError, "evidence is missing"):
            GATE.native_event_probe_determinism_signature(None)

    def test_native_event_probe_impairment_requires_exact_profile_and_activity(self) -> None:
        def diagnostic(
            seed: int, profile: dict[str, int | float], *,
            seen: int, dropped: int,
        ) -> str:
            return (
                f"net_impair: enabled=1 seed={seed} "
                f"latency={profile['latency']} jitter={profile['jitter']} "
                f"loss={profile['loss']:.2f} "
                f"burst={profile['burst']:.2f}/{profile['burst_length']} "
                f"reorder={profile['reorder']:.2f} "
                f"duplicate={profile['duplicate']:.2f} "
                f"corrupt={profile['corrupt']:.2f} "
                f"upstream_stall={profile['upstream_stall']} "
                f"rate_kbps={profile['rate_kbps']} "
                "queue=0/1024 high_water=8\n"
                f"net_impair counters: seen={seen} dropped={dropped} "
                "burst_dropped=0 reordered=0 duplicated=0 corrupted=0 "
                "upstream_stalled=6 throttled=0 overflow=0 resets=0"
            )

        older_diagnostic = diagnostic(
            GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED,
            GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE,
            seen=100,
            dropped=1,
        )
        newest_diagnostic = diagnostic(
            GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED,
            GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE,
            seen=125,
            dropped=2,
        )
        parsed_config, parsed_counters = GATE.parse_native_event_probe_impairment(
            older_diagnostic + "\n" + newest_diagnostic,
        )
        self.assertEqual(
            parsed_config["seed"], GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED,
        )
        self.assertEqual(parsed_counters["seen"], 125)
        self.assertEqual(parsed_counters["dropped"], 2)
        with self.assertRaisesRegex(RuntimeError, "complete"):
            GATE.parse_native_event_probe_impairment(
                older_diagnostic + "\n" + newest_diagnostic.splitlines()[0],
            )

        def config(
            seed: int, profile: dict[str, int | float],
        ) -> dict[str, object]:
            schedules_packets = any(
                profile[name] > 0 for name in (
                    "latency", "jitter", "reorder", "duplicate",
                    "upstream_stall", "rate_kbps",
                )
            )
            return {
                "enabled": 1,
                "seed": seed,
                **profile,
                "queue_current": 0,
                "high_water": 8 if schedules_packets else 0,
            }

        def counters(**overrides: int) -> dict[str, int]:
            result = {
                name: 0 for name in GATE.NATIVE_EVENT_PROBE_IMPAIR_COUNTER_FIELDS
            }
            result.update({"seen": 100, **overrides})
            return result

        client_config = config(
            GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED,
            GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE,
        )
        server_config = config(
            GATE.NATIVE_EVENT_PROBE_SERVER_IMPAIR_SEED,
            GATE.NATIVE_EVENT_PROBE_SERVER_IMPAIR_PROFILE,
        )
        client_baseline_counters = counters(upstream_stalled=6)
        server_baseline_counters = counters(seen=200)
        client_post_counters = {
            **client_baseline_counters,
            "seen": 110,
            "upstream_stalled": 7,
        }
        server_post_counters = {
            **server_baseline_counters,
            "seen": 212,
        }

        def validated(
            endpoint_config: dict[str, object], endpoint_counters: dict[str, int],
            seed: int, profile: dict[str, int | float],
        ) -> dict[str, object]:
            return GATE.validate_native_event_probe_impairment(
                endpoint_config, endpoint_counters, expected_seed=seed,
                expected_profile=profile,
            )

        client_baseline = validated(
            client_config, client_baseline_counters,
            GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED,
            GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE,
        )
        server_baseline = validated(
            server_config, server_baseline_counters,
            GATE.NATIVE_EVENT_PROBE_SERVER_IMPAIR_SEED,
            GATE.NATIVE_EVENT_PROBE_SERVER_IMPAIR_PROFILE,
        )
        client_post = validated(
            client_config, client_post_counters,
            GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED,
            GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE,
        )
        server_post = validated(
            server_config, server_post_counters,
            GATE.NATIVE_EVENT_PROBE_SERVER_IMPAIR_SEED,
            GATE.NATIVE_EVENT_PROBE_SERVER_IMPAIR_PROFILE,
        )
        client_delta = GATE.native_event_probe_impairment_delta(
            client_baseline, client_post,
        )
        server_delta = GATE.native_event_probe_impairment_delta(
            server_baseline, server_post,
        )
        self.assertEqual(client_delta["baseline_counters"], client_baseline_counters)
        self.assertEqual(client_delta["post_counters"], client_post_counters)
        self.assertEqual(
            GATE.validate_native_event_probe_impairment_pair(
                client_delta, server_delta,
            ),
            {
                "counters": {
                    "seen": 22,
                    "dropped": 0,
                    "burst_dropped": 0,
                    "reordered": 0,
                    "duplicated": 0,
                    "corrupted": 0,
                    "upstream_stalled": 1,
                    "throttled": 0,
                    "overflow": 0,
                    "resets": 0,
                },
                "required_positive": ["seen", "upstream_stalled"],
                "required_zero": [
                    "overflow", "resets", "dropped", "burst_dropped",
                    "reordered", "duplicated", "corrupted", "throttled",
                ],
                "configured_scheduler_dimensions": ["latency", "jitter"],
            },
        )

        bad_config = dict(client_config)
        bad_config["rate_kbps"] = 1024
        with self.assertRaisesRegex(RuntimeError, "rate_kbps"):
            GATE.validate_native_event_probe_impairment(
                bad_config, client_post_counters,
                expected_seed=GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED,
                expected_profile=GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE,
            )
        symmetric_latency = dict(client_config)
        symmetric_latency["latency"] = server_config["latency"]
        with self.assertRaisesRegex(RuntimeError, "latency"):
            GATE.validate_native_event_probe_impairment(
                symmetric_latency, client_post_counters,
                expected_seed=GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED,
                expected_profile=GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE,
            )
        overflow = dict(client_post_counters)
        overflow["overflow"] = 1
        with self.assertRaisesRegex(RuntimeError, "overflowed"):
            GATE.validate_native_event_probe_impairment(
                client_config, overflow,
                expected_seed=GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED,
                expected_profile=GATE.NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE,
            )

        regressed_post = {
            **client_post,
            "counters": {**client_post_counters, "seen": 99},
        }
        with self.assertRaisesRegex(RuntimeError, "not monotonic"):
            GATE.native_event_probe_impairment_delta(
                client_baseline, regressed_post,
            )

        client_missing_stall = GATE.native_event_probe_impairment_delta(
            client_baseline,
            {
                **client_post,
                "counters": {
                    **client_post_counters,
                    "upstream_stalled": (
                        client_baseline_counters["upstream_stalled"]
                    ),
                },
            },
        )
        server_no_new_stall = GATE.native_event_probe_impairment_delta(
            server_baseline, server_post,
        )
        with self.assertRaisesRegex(RuntimeError, "upstream_stalled>0"):
            GATE.validate_native_event_probe_impairment_pair(
                client_missing_stall, server_no_new_stall,
            )

        client_unexpected_drop = {
            **client_delta,
            "counters": {**client_delta["counters"], "dropped": 1},
        }
        with self.assertRaisesRegex(RuntimeError, "forbidden counters"):
            GATE.validate_native_event_probe_impairment_pair(
                client_unexpected_drop, server_delta,
            )

    def test_native_event_probe_wait_tolerates_batched_delayed_rows_and_selects_newest(
        self,
    ) -> None:
        stable = completed_native_event_probe_row()
        delayed = completed_native_event_probe_row(
            raw_action_chain_hash=0x1111111111111111,
            probe_action_chain_hash=0x1111111111111111,
            raw_effect_chain_hash=0x2222222222222222,
        )
        process = mock.Mock()
        process.poll.return_value = None
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "target.log"
            path.write_text("connected\n", encoding="utf-8")

            def append_batch(
                _port: int, command: str, _timeout: float,
            ) -> str:
                self.assertIn(GATE.NATIVE_EVENT_PROBE_CLIENT_COMMAND, command)
                with path.open("a", encoding="utf-8") as output:
                    output.write(native_event_probe_line(delayed) + "\n")
                    output.write(native_event_probe_line(stable) + "\n")
                return "print\n"

            with (
                mock.patch.object(
                    GATE, "rcon_command_once", side_effect=append_batch,
                ) as send_once,
                mock.patch.object(
                    GATE, "rcon_command",
                    side_effect=AssertionError("diagnostic command retried"),
                ),
            ):
                observed, responses = GATE.wait_for_native_event_probe_samples(
                    27960, 7, process, path, 1.0, require_activity=True,
                )
            self.assertEqual(observed, stable)
            self.assertEqual(len(responses), 2)
            self.assertEqual(send_once.call_count, 2)

    def test_native_event_probe_wait_timeout_reports_latest_invalid_row(self) -> None:
        invalid = completed_native_event_probe_row(authority_degraded=1)
        process = mock.Mock()
        process.poll.return_value = None
        clock = iter((0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 1.2))
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "shooter.log"
            path.write_text("connected\n", encoding="utf-8")

            def append_invalid(
                _port: int, _command: str, _timeout: float,
            ) -> str:
                with path.open("a", encoding="utf-8") as output:
                    output.write(native_event_probe_line(invalid) + "\n")
                return "print\n"

            with (
                mock.patch.object(
                    GATE, "rcon_command_once", side_effect=append_invalid,
                ),
                mock.patch.object(
                    GATE.time, "monotonic", side_effect=lambda: next(clock),
                ),
                mock.patch.object(GATE.time, "sleep"),
                self.assertRaises(RuntimeError) as raised,
            ):
                GATE.wait_for_native_event_probe_samples(
                    27960,
                    7,
                    process,
                    path,
                    1.0,
                    require_activity=True,
                )

        self.assertEqual(
            str(raised.exception),
            "timed out waiting for two stable native event probe samples; "
            f"newest={invalid!r} stable_candidate=None last="
            "native event probe did not prove authority_degraded=0; "
            f"row={invalid!r}",
        )

    def test_native_event_sender_wait_polls_then_fails_closed_when_missing(self) -> None:
        baseline = native_event_sender_row()
        incomplete = native_event_sender_post_row(
            baseline, retry_count=0,
        )
        complete = native_event_sender_post_row(
            baseline, retry_count=2,
        )
        transient_status_error = RuntimeError("transient status reply loss")
        with (
            mock.patch.object(
                GATE,
                "rcon_command",
                side_effect=(
                    transient_status_error,
                    native_event_sender_line(incomplete),
                    native_event_sender_line(complete),
                ),
            ),
            mock.patch.object(GATE.time, "sleep"),
        ):
            row, delta, transcript = GATE.wait_for_native_event_sender_status(
                27960, 7, 1.0, baseline, 3,
            )
        self.assertEqual(row["retries"], baseline["retries"] + 2)
        self.assertEqual(delta["counters"]["retries"], 2)
        self.assertEqual(len(transcript), 3)
        self.assertEqual(
            transcript[0],
            "native event sender RCON reply missing: transient status reply loss",
        )

        transient_baseline_error = RuntimeError("transient baseline reply loss")
        with (
            mock.patch.object(
                GATE,
                "rcon_command",
                side_effect=(
                    transient_baseline_error,
                    native_event_sender_line(baseline),
                ),
            ),
            mock.patch.object(GATE.time, "sleep"),
        ):
            observed_baseline, baseline_transcript = (
                GATE.wait_for_native_event_sender_baseline(27960, 7, 1.0)
            )
        self.assertEqual(observed_baseline, baseline)
        self.assertEqual(len(baseline_transcript), 2)
        self.assertEqual(
            baseline_transcript[0],
            "native event sender RCON reply missing: transient baseline reply loss",
        )

        clock = [0.0]

        def monotonic() -> float:
            clock[0] += 0.1
            return clock[0]

        with (
            mock.patch.object(
                GATE, "rcon_command",
                return_value=native_event_sender_line(incomplete),
            ),
            mock.patch.object(GATE.time, "monotonic", side_effect=monotonic),
            mock.patch.object(GATE.time, "sleep"),
            self.assertRaisesRegex(
                RuntimeError, "checkpoint-window retry",
            ),
        ):
            GATE.wait_for_native_event_sender_status(
                27960, 7, 0.25, baseline, 3,
            )

        zero_clock = [0.0]

        def zero_monotonic() -> float:
            zero_clock[0] += 0.1
            return zero_clock[0]

        with (
            mock.patch.object(
                GATE, "rcon_command", return_value=native_event_sender_line(baseline),
            ),
            mock.patch.object(
                GATE.time, "monotonic", side_effect=zero_monotonic,
            ),
            mock.patch.object(GATE.time, "sleep"),
            self.assertRaisesRegex(
                RuntimeError,
                r"baseline=.*latest=.*expected=>0 queued=0",
            ),
        ):
            GATE.wait_for_native_event_sender_status(
                27960, 7, 0.25, baseline, None,
            )

        rotated = native_event_sender_post_row(baseline, stream_epoch=10)
        with (
            mock.patch.object(
                GATE, "rcon_command", return_value=native_event_sender_line(rotated),
            ) as rotated_query,
            self.assertRaisesRegex(RuntimeError, "stream rotated while polling"),
        ):
            GATE.wait_for_native_event_sender_status(
                27960, 7, 1.0, baseline, 3,
            )
        self.assertEqual(rotated_query.call_count, 1)

        regressed = native_event_sender_post_row(
            baseline,
            candidates_queued=0,
            candidates_promoted=0,
            event_acks=0,
        )
        with (
            mock.patch.object(
                GATE, "rcon_command", return_value=native_event_sender_line(regressed),
            ) as regressed_query,
            self.assertRaisesRegex(RuntimeError, "counters regressed while polling"),
        ):
            GATE.wait_for_native_event_sender_status(
                27960, 7, 1.0, baseline, 3,
            )
        self.assertEqual(regressed_query.call_count, 1)

    def test_native_event_sender_wait_collapses_only_identical_reply_duplicates(
        self,
    ) -> None:
        baseline = native_event_sender_row()
        complete = native_event_sender_post_row(baseline)
        duplicated_baseline = "\n".join((
            native_event_sender_line(baseline),
            native_event_sender_line(baseline),
        ))
        duplicated_complete = "\n".join((
            native_event_sender_line(complete),
            native_event_sender_line(complete),
        ))

        with mock.patch.object(
            GATE, "rcon_command", return_value=duplicated_baseline,
        ):
            observed, _ = GATE.wait_for_native_event_sender_baseline(
                27960, 7, 1.0,
            )
        self.assertEqual(observed, baseline)

        with mock.patch.object(
            GATE, "rcon_command", return_value=duplicated_complete,
        ):
            observed, delta, _ = GATE.wait_for_native_event_sender_status(
                27960, 7, 1.0, baseline, 3,
            )
        self.assertEqual(observed, complete)
        self.assertEqual(delta["counters"]["candidates_queued"], 3)

        conflicting_baseline = "\n".join((
            native_event_sender_line(baseline),
            native_event_sender_line({
                **baseline,
                "snapshots_queued": baseline["snapshots_queued"] + 1,
                "prepared": baseline["prepared"] + 1,
                "confirmed": baseline["confirmed"] + 1,
            }),
        ))
        with (
            mock.patch.object(
                GATE, "rcon_command", return_value=conflicting_baseline,
            ),
            self.assertRaisesRegex(RuntimeError, "conflicting rows"),
        ):
            GATE.wait_for_native_event_sender_baseline(27960, 7, 1.0)

        conflicting_status = "\n".join((
            native_event_sender_line(
                native_event_sender_post_row(baseline, retry_count=0),
            ),
            native_event_sender_line(complete),
        ))
        with (
            mock.patch.object(
                GATE, "rcon_command", return_value=conflicting_status,
            ),
            self.assertRaisesRegex(RuntimeError, "conflicting rows"),
        ):
            GATE.wait_for_native_event_sender_status(
                27960, 7, 1.0, baseline, 3,
            )

    def test_native_event_probe_phase_uses_baseline_shot_and_sender_sequence(self) -> None:
        pre_checkpoint = completed_native_event_probe_row()
        baseline = native_event_probe_row()
        completed = completed_native_event_probe_row()
        sender_baseline = native_event_sender_row()
        sender = native_event_sender_post_row(
            sender_baseline, event_count=5,
            schema2_events_promoted=(
                sender_baseline["schema2_events_promoted"] + 4
            ),
        )
        sender_delta = {
            "stream_epoch": sender["stream_epoch"],
            "counters": {
                "confirms": 1,
                "snapshots_queued": 2,
                "queue_failures": 0,
                "candidates_queued": 5,
                "candidates_promoted": 5,
                "descriptor_acks": 0,
                "event_acks": 5,
                "prepared": 2,
                "confirmed": 2,
                "rejected": 0,
                "first_sends": 1,
                "retries": 2,
                "schema2_batches_promoted": 1,
                "schema2_events_promoted": 4,
            },
        }
        checkpoint = {
            "command": "cl_worr_native_event_probe_checkpoint 1 11 4294967307",
            "checkpoint_id": (1 << 32) | 11,
            "applied_receipt": native_event_probe_checkpoint_row(),
            "duplicate_receipt": native_event_probe_checkpoint_row(
                result=GATE.NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED,
            ),
        }
        commands: list[str] = []

        def fake_rcon(_port: int, command: str, _timeout: float) -> str:
            commands.append(command)
            return "print\n"

        with (
            mock.patch.object(GATE, "rcon_command", side_effect=fake_rcon),
            mock.patch.object(
                GATE, "wait_for_native_base_shadow",
                return_value=({"clients": {}, "server_peers": []}, ["native"]),
            ),
            mock.patch.object(
                GATE, "wait_for_native_event_probe_samples",
                side_effect=(
                    (pre_checkpoint, ["pre-checkpoint"]),
                    (baseline, ["baseline"]),
                    (completed, ["complete"]),
                ),
            ) as probe_wait,
            mock.patch.object(
                GATE, "issue_native_event_probe_checkpoint",
                return_value=(
                    checkpoint,
                    ["checkpoint-applied", "checkpoint-duplicate"],
                ),
            ) as issue_checkpoint,
            mock.patch.object(
                GATE, "wait_for_fixture_ready",
                return_value=(PASS_STATUS, ["ready"]),
            ),
            mock.patch.object(
                GATE, "wait_for_status",
                return_value=(PASS_STATUS, ["status"]),
            ),
            mock.patch.object(
                GATE, "wait_for_native_event_sender_baseline",
                return_value=(sender_baseline, ["sender-baseline"]),
            ) as sender_baseline_wait,
            mock.patch.object(
                GATE, "wait_for_native_event_sender_status",
                return_value=(sender, sender_delta, ["sender"]),
            ) as sender_wait,
            mock.patch.object(GATE, "validate_status", side_effect=lambda row, _mode: row),
        ):
            phase, transcript = GATE.run_native_event_probe_phase(
                port=27960,
                timeout=1.0,
                mode=GATE.GATE_MODES["native-event-probe-map-reuse"],
                shooter=mock.Mock(),
                target=mock.Mock(),
                shooter_path=Path("shooter.log"),
                target_path=Path("target.log"),
                shooter_user_id=4,
                target_user_id=7,
            )
        self.assertEqual(phase["pre_checkpoint_status"], pre_checkpoint)
        self.assertEqual(phase["checkpoint"], checkpoint)
        self.assertEqual(phase["zero_baseline"], baseline)
        self.assertEqual(phase["probe_status"], completed)
        self.assertEqual(
            phase["event_sender_pre_checkpoint"], sender_baseline,
        )
        self.assertEqual(phase["event_sender_baseline"], sender_baseline)
        self.assertEqual(phase["event_sender_status"], sender)
        self.assertEqual(phase["event_sender_delta"], sender_delta)
        self.assertEqual(
            phase["lifecycle_scope"],
            {
                "map_generation": 1,
                "map_end_count": 0,
                "authority_epoch": 11,
                "checkpoint_id": (1 << 32) | 11,
            },
        )
        self.assertIn("sv worr_rewind_canonical_blaster_damage_arm", commands)
        self.assertIn(
            'stuff 4 "+attack 255; -attack 255"', commands,
        )
        self.assertEqual(
            [call.kwargs["require_activity"] for call in probe_wait.call_args_list],
            [None, False, True],
        )
        self.assertEqual(
            [call.args[1] for call in probe_wait.call_args_list],
            [7, 7, 7],
        )
        self.assertEqual(
            [call.args[3] for call in probe_wait.call_args_list],
            [Path("target.log")] * 3,
        )
        issue_checkpoint.assert_called_once_with(
            27960, 7, mock.ANY, Path("target.log"), 1.0, pre_checkpoint,
        )
        self.assertEqual(
            sender_baseline_wait.call_args_list,
            [mock.call(27960, 7, 1.0), mock.call(27960, 7, 1.0)],
        )
        sender_wait.assert_called_once_with(
            27960, 7, 1.0, sender_baseline, 5,
            require_schema2_event_batch=True,
            minimum_schema2_event_batch_events=4,
            require_schema2_mixed_singletons=True,
        )
        self.assertEqual(
            transcript,
            [
                "native",
                "print\n",
                "ready",
                "sender-baseline",
                "pre-checkpoint",
                "checkpoint-applied",
                "checkpoint-duplicate",
                "baseline",
                "sender-baseline",
                "print\n",
                "sender",
                "complete",
            ],
        )

    def test_native_event_probe_phase_surfaces_missing_sender_before_probe_timeout(
        self,
    ) -> None:
        pre_checkpoint = completed_native_event_probe_row()
        zero_baseline = native_event_probe_row()
        checkpoint = {
            "command": "cl_worr_native_event_probe_checkpoint 1 11 4294967307",
            "checkpoint_id": (1 << 32) | 11,
            "applied_receipt": native_event_probe_checkpoint_row(),
            "duplicate_receipt": native_event_probe_checkpoint_row(
                result=GATE.NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED,
            ),
        }
        sender_baseline = native_event_sender_row()
        with (
            mock.patch.object(GATE, "rcon_command", return_value="print\n"),
            mock.patch.object(
                GATE, "wait_for_native_base_shadow",
                return_value=({"clients": {}, "server_peers": []}, ["native"]),
            ),
            mock.patch.object(
                GATE, "wait_for_fixture_ready",
                return_value=(PASS_STATUS, ["ready"]),
            ),
            mock.patch.object(
                GATE, "wait_for_native_event_probe_samples",
                side_effect=(
                    (pre_checkpoint, ["pre-checkpoint"]),
                    (zero_baseline, ["zero-baseline"]),
                ),
            ) as probe_wait,
            mock.patch.object(
                GATE, "issue_native_event_probe_checkpoint",
                return_value=(checkpoint, ["checkpoint"]),
            ),
            mock.patch.object(
                GATE, "wait_for_native_event_sender_baseline",
                return_value=(sender_baseline, ["sender-baseline"]),
            ),
            mock.patch.object(
                GATE, "wait_for_status",
                return_value=(PASS_STATUS, ["status"]),
            ),
            mock.patch.object(
                GATE, "wait_for_native_event_sender_status",
                side_effect=RuntimeError("sender produced no candidates"),
            ),
            self.assertRaisesRegex(RuntimeError, "sender produced no candidates"),
        ):
            GATE.run_native_event_probe_phase(
                port=27960,
                timeout=1.0,
                mode=GATE.GATE_MODES["native-event-probe-map-reuse"],
                shooter=mock.Mock(),
                target=mock.Mock(),
                shooter_path=Path("shooter.log"),
                target_path=Path("target.log"),
                shooter_user_id=4,
                target_user_id=7,
            )
        self.assertEqual(probe_wait.call_count, 2)

    def test_native_event_probe_reload_keeps_same_map_and_live_slots(self) -> None:
        server_path = Path("server.log")
        shooter_path = Path("shooter.log")
        target_path = Path("target.log")
        texts = {
            server_path: f"SpawnServer: {GATE.MAP_NAME}\n",
            shooter_path: "Serverdata packet received\n",
            target_path: "Serverdata packet received\n",
        }
        commands: list[str] = []

        def fake_rcon(_port: int, command: str, _timeout: float) -> str:
            commands.append(command)
            return "print\n"

        transition_reply = (
            "print\n------- Server Initialization -------\n"
            f"SpawnServer: {GATE.MAP_NAME}\n"
            "-------------------------------------\n"
        )

        def fake_rcon_once(
            _port: int, command: str, _timeout: float,
        ) -> str:
            commands.append(command)
            return transition_reply

        server = mock.Mock()
        server.poll.return_value = None

        with (
            mock.patch.object(GATE, "read_text", side_effect=lambda path: texts[path]),
            mock.patch.object(GATE, "rcon_command", side_effect=fake_rcon),
            mock.patch.object(
                GATE, "rcon_command_once", side_effect=fake_rcon_once,
            ) as rcon_once,
            mock.patch.object(GATE, "wait_for_marker_count") as marker_wait,
            mock.patch.object(
                GATE, "wait_for_client_user_ids",
                return_value=(4, 7, None, ["status"]),
            ),
            mock.patch.object(GATE.time, "sleep"),
        ):
            responses = GATE.reload_native_event_probe_map(
                port=27960,
                timeout=1.0,
                server=server,
                shooter=mock.Mock(),
                target=mock.Mock(),
                server_path=server_path,
                shooter_path=shooter_path,
                target_path=target_path,
                expected_user_ids=(4, 7),
            )
        self.assertEqual(commands, [
            f"gamemap {GATE.MAP_NAME}",
        ])
        rcon_once.assert_called_once_with(
            27960, f"gamemap {GATE.MAP_NAME}", 1.0,
        )
        self.assertEqual(marker_wait.call_count, 2)
        self.assertEqual(
            marker_wait.call_args_list,
            [
                mock.call(
                    mock.ANY, shooter_path, "Serverdata packet received", 2,
                    1.0,
                ),
                mock.call(
                    mock.ANY, target_path, "Serverdata packet received", 2,
                    1.0,
                ),
            ],
        )
        self.assertEqual(responses, [transition_reply, "status"])

        with (
            mock.patch.object(GATE, "read_text", side_effect=lambda path: texts[path]),
            mock.patch.object(GATE, "rcon_command", return_value="print\n"),
            mock.patch.object(
                GATE, "rcon_command_once", return_value=transition_reply,
            ),
            mock.patch.object(GATE, "wait_for_marker_count"),
            mock.patch.object(
                GATE, "wait_for_client_user_ids",
                return_value=(5, 7, None, []),
            ),
            self.assertRaisesRegex(RuntimeError, "changed the live client"),
        ):
            GATE.reload_native_event_probe_map(
                port=27960,
                timeout=1.0,
                server=server,
                shooter=mock.Mock(),
                target=mock.Mock(),
                server_path=server_path,
                shooter_path=shooter_path,
                target_path=target_path,
                expected_user_ids=(4, 7),
            )

        with (
            mock.patch.object(GATE, "read_text", side_effect=lambda path: texts[path]),
            mock.patch.object(GATE, "rcon_command_once", return_value="print\n") as once,
            mock.patch.object(GATE, "wait_for_marker_count") as marker_wait,
            self.assertRaisesRegex(RuntimeError, "exactly one server spawn marker"),
        ):
            GATE.reload_native_event_probe_map(
                port=27960,
                timeout=1.0,
                server=server,
                shooter=mock.Mock(),
                target=mock.Mock(),
                server_path=server_path,
                shooter_path=shooter_path,
                target_path=target_path,
                expected_user_ids=(4, 7),
            )
        once.assert_called_once()
        marker_wait.assert_not_called()

    def test_fixture_readiness_retries_real_team_choice_after_reconnect(self) -> None:
        pending = PASS_LINE.replace(
            '"pass:1:1:1:', '"pending:1:0:0:', 1,
        )
        ready = PASS_LINE.replace('"pass:', '"pending:', 1)
        status_responses = iter((pending, ready))
        commands: list[str] = []

        def fake_rcon(_port: int, command: str, _timeout: float) -> str:
            commands.append(command)
            if command.startswith("cvarlist "):
                return next(status_responses)
            return "print\n"

        with (
            mock.patch.object(GATE, "rcon_command", side_effect=fake_rcon),
            mock.patch.object(GATE.time, "sleep"),
        ):
            status, responses = GATE.wait_for_fixture_ready(
                27960, 1.0, GATE.GATE_MODES["railgun"], 4, 7,
            )

        self.assertEqual(status["players_ready"], 1)
        self.assertEqual(status["history_ready"], 1)
        self.assertIn('stuff 4 "cmd team free"', commands)
        self.assertIn('stuff 7 "cmd team free"', commands)
        self.assertGreaterEqual(len(responses), 4)

    def test_terminal_status_releases_held_attack_before_return(self) -> None:
        commands: list[str] = []

        def fake_rcon(_port: int, command: str, _timeout: float) -> str:
            commands.append(command)
            if command.startswith("cvarlist "):
                return PASS_LINE
            return "print\n"

        with mock.patch.object(GATE, "rcon_command", side_effect=fake_rcon):
            status, responses = GATE.wait_for_status(
                27960, 1.0, GATE.GATE_MODES["railgun"], 4,
            )

        self.assertEqual(status["status"], "pass")
        self.assertEqual(commands, [
            "cvarlist sg_worr_rewind_canonical_rail_damage_status",
            'stuff 4 "-attack; +moveup; -moveup"',
        ])
        self.assertEqual(len(responses), 2)

    def test_terminal_failure_surfaces_before_later_native_waits(self) -> None:
        failed = dict(PASS_STATUS)
        failed["status"] = "fail"
        failed["failure_code"] = 29
        failed_line = (
            'sg_worr_rewind_canonical_rail_damage_status "' +
            ":".join(str(failed[name]) for name in GATE.STATUS_FIELDS) + '"'
        )
        commands: list[str] = []
        retained: list[str] = []

        def fake_rcon(_port: int, command: str, _timeout: float) -> str:
            commands.append(command)
            if command.startswith("cvarlist "):
                return failed_line
            return "print\n"

        with (
            mock.patch.object(GATE, "rcon_command", side_effect=fake_rcon),
            self.assertRaisesRegex(RuntimeError, "failure_code=29"),
        ):
            GATE.wait_for_status(
                27960, 1.0, GATE.GATE_MODES["railgun"], 4, retained,
            )

        self.assertEqual(commands, [
            "cvarlist sg_worr_rewind_canonical_rail_damage_status",
            'stuff 4 "-attack; +moveup; -moveup"',
        ])
        self.assertEqual(retained, [failed_line, "print\n"])

    def test_terminal_release_prevents_refresh_while_lease_proof_settles(
        self,
    ) -> None:
        mode = GATE.GATE_MODES["blaster-local-action-lease"]
        unsettled = dict(PASS_STATUS)
        for name in (
            "local_action_catalog_ready",
            "local_action_lease_ready",
            "local_action_scoped_record",
            "local_action_leased_record",
            "local_action_continuity_exact",
            "local_action_joined_record",
            "local_action_shadow_ready",
            "local_action_command_epoch",
            "local_action_command_sequence",
            "local_action_shadow_record_hash",
        ):
            unsettled[name] = 0

        def status_line(values: dict[str, int | str]) -> str:
            return (
                f'{mode["status_cvar"]} "' +
                ":".join(str(values[name]) for name in GATE.STATUS_FIELDS) +
                '"'
            )

        status_responses = iter((status_line(unsettled), status_line(PASS_STATUS)))
        commands: list[str] = []
        clock = [0.0]

        def monotonic() -> float:
            clock[0] += 0.2
            return clock[0]

        def fake_rcon(_port: int, command: str, _timeout: float) -> str:
            commands.append(command)
            if command.startswith("cvarlist "):
                return next(status_responses)
            return "print\n"

        with (
            mock.patch.object(GATE, "rcon_command", side_effect=fake_rcon),
            mock.patch.object(GATE.time, "monotonic", side_effect=monotonic),
            mock.patch.object(GATE.time, "sleep"),
        ):
            status, _responses = GATE.wait_for_status(
                27960, 10.0, mode, 4,
            )

        self.assertEqual(status["status"], "pass")
        self.assertEqual(
            commands.count('stuff 4 "-attack; +attack; +moveup; -moveup"'),
            1,
        )
        self.assertEqual(
            commands.count('stuff 4 "-attack; +moveup; -moveup"'),
            1,
        )
        self.assertEqual(
            commands.count(f'cvarlist {mode["status_cvar"]}'),
            2,
        )

    def test_post_proof_snapshot_rejects_event_only_err_drop(self) -> None:
        process = mock.Mock()
        clients = {
            "shooter": (GATE.SHOOTER_NAME, process, Path("shooter.log")),
            "target": (GATE.TARGET_NAME, process, Path("target.log")),
        }
        baseline_server = (
            "rail_shooter[127.0.0.1:50001] disconnected\n"
        )
        baseline_clients = {
            "shooter": "post-reconnect baseline\n",
            "target": "target baseline\n",
        }
        disconnect_baseline = {
            "shooter": 1,
            "target": 0,
        }
        log_size_baseline = {
            role: len(text) for role, text in baseline_clients.items()
        }

        GATE.validate_post_proof_liveness_snapshot(
            baseline_server,
            {
                "shooter": baseline_clients["shooter"] + "proof passed\n",
                "target": baseline_clients["target"] + "proof passed\n",
            },
            clients,
            disconnect_baseline,
            log_size_baseline,
        )

        shooter_drop = (
            baseline_clients["shooter"] +
            "native application rejected: failure=200912\n"
            "********************\n"
            "ERROR: server sent a rejected application payload\n"
        )
        with self.assertRaisesRegex(
            RuntimeError, "shooter client reported ERR_DROP",
        ):
            GATE.validate_post_proof_liveness_snapshot(
                baseline_server,
                {
                    "shooter": shooter_drop,
                    "target": baseline_clients["target"],
                },
                clients,
                disconnect_baseline,
                log_size_baseline,
            )

        with self.assertRaisesRegex(
            RuntimeError, "target client disconnected after canonical proof",
        ):
            GATE.validate_post_proof_liveness_snapshot(
                baseline_server +
                "rail_target[127.0.0.1:50002] disconnected\n",
                baseline_clients,
                clients,
                disconnect_baseline,
                log_size_baseline,
            )

    def test_marker_wait_surfaces_live_native_application_rejection(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "shooter.stdout.log"
            path.write_text(
                "g_sgame_auto_aa87f5ca778d"
                "native application rejected: failure=200709 mode=3\n"
                "ERROR: server sent a rejected application payload\n",
                encoding="utf-8",
            )
            process = mock.Mock()
            process.poll.return_value = None

            for wait in (
                lambda: GATE.wait_for_marker(
                    process, path, "net_impair counters:", 1.0,
                ),
                lambda: GATE.wait_for_marker_count(
                    process, path, "net_impair counters:", 1, 1.0,
                ),
            ):
                with (
                    self.subTest(wait=wait),
                    mock.patch.object(
                        GATE.time, "sleep",
                        side_effect=AssertionError("rejection wait slept"),
                    ),
                    self.assertRaisesRegex(
                        GATE.NativeApplicationRejectionError,
                        "native application rejected: failure=200709",
                    ) as raised,
                ):
                    wait()
                self.assertNotIn("timed out", str(raised.exception))

    def test_probe_sample_wait_does_not_swallow_native_rejection(self) -> None:
        process = mock.Mock()
        process.poll.return_value = None
        rejection = GATE.NativeApplicationRejectionError(
            "native application rejected: failure=200709"
        )
        with (
            mock.patch.object(
                GATE, "parse_native_event_probe_status_rows", return_value=[],
            ),
            mock.patch.object(GATE, "rcon_command_once", return_value="print\n"),
            mock.patch.object(
                GATE, "wait_for_marker_count", side_effect=rejection,
            ),
            self.assertRaisesRegex(
                GATE.NativeApplicationRejectionError, "failure=200709",
            ),
        ):
            GATE.wait_for_native_event_probe_samples(
                27960,
                4,
                process,
                Path("shooter.stdout.log"),
                1.0,
                require_activity=None,
            )

    def test_final_live_roster_rejects_disconnected_alive_client(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            server_path = root / "server.log"
            shooter_path = root / "shooter.log"
            target_path = root / "target.log"
            server_path.write_text("server ready\n", encoding="utf-8")
            shooter_path.write_text("shooter ready\n", encoding="utf-8")
            target_path.write_text("target ready\n", encoding="utf-8")
            shooter = mock.Mock()
            target = mock.Mock()
            shooter.poll.return_value = None
            target.poll.return_value = None
            clients = {
                "shooter": (GATE.SHOOTER_NAME, shooter, shooter_path),
                "target": (GATE.TARGET_NAME, target, target_path),
            }
            disconnect_baseline, log_size_baseline = (
                GATE.capture_fixture_liveness_baseline(server_path, clients)
            )
            missing_shooter = (
                "print\nnum score ping name\n"
                "  7 0 4 rail_target 8 127.0.0.1:50002\n"
            )
            with (
                mock.patch.object(
                    GATE, "rcon_command", return_value=missing_shooter,
                ),
                self.assertRaisesRegex(RuntimeError, "roster is incomplete"),
            ):
                GATE.verify_post_proof_client_liveness(
                    27960,
                    1.0,
                    server_path,
                    clients,
                    disconnect_baseline,
                    log_size_baseline,
                    (4, 7),
                )

    def test_status_parser_selects_the_two_admitted_real_clients(self) -> None:
        response = (
            "print\nnum score ping name\n"
            "  4     0    3 rail_shooter  10 127.0.0.1:50001\n"
            "  7     0    4 rail_target   8 127.0.0.1:50002\n"
        )
        self.assertEqual(GATE.admitted_fixture_user_ids(response), (4, 7))

    def test_status_parser_requires_the_exact_three_client_spectator_roster(self) -> None:
        response = (
            "print\nnum score ping name\n"
            "  4     0    3 rail_shooter    10 127.0.0.1:50001\n"
            "  7     0    4 rail_target      8 127.0.0.1:50002\n"
            "  9     0    2 rail_spectator   3 127.0.0.1:50003\n"
        )
        self.assertEqual(
            GATE.admitted_fixture_user_ids(response, require_spectator=True),
            (4, 7, 9),
        )
        with self.assertRaisesRegex(RuntimeError, "roster differs"):
            GATE.admitted_fixture_user_ids(response)

    def test_spectator_mode_uses_three_headless_input_free_clients(self) -> None:
        server = GATE.build_server_command(
            Path("C:/stage/worr_ded_x86_64.exe"), 27960,
            Path("C:/runtime/server"), max_clients=3,
        )
        spectator = GATE.build_client_command(
            Path("C:/stage/worr_x86_64.exe"), 27960,
            GATE.SPECTATOR_NAME, Path("C:/runtime/spectator"),
            enable_network_impairment=False,
        )
        self.assertEqual(server[server.index("maxclients") + 1], "3")
        for cvar, expected in (
            ("loc_language", "english"),
            ("win_headless", "1"),
            ("cl_headless", "1"),
            ("in_enable", "0"),
            ("in_grab", "0"),
            ("s_enable", "0"),
        ):
            self.assertEqual(spectator[spectator.index(cvar) + 1], expected)
        self.assertNotIn("net_impair_enable", spectator)

    def test_run_cleanup_terminates_processes_before_rcon_log_write_failure(
        self,
    ) -> None:
        events: list[tuple[str, object]] = []
        server = mock.Mock(name="server")
        shooter = mock.Mock(name="shooter")
        target = mock.Mock(name="target")

        def terminate(process: object) -> bool:
            events.append(("terminate", process))
            return True

        def fail_write(
            _path: Path, _text: str, *, encoding: str | None = None,
        ) -> int:
            self.assertEqual(encoding, "utf-8")
            events.append(("write", "server.rcon"))
            raise OSError("simulated full disk")

        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            with (
                mock.patch.object(
                    GATE, "start_headless_process",
                    side_effect=(server, shooter, target),
                ),
                mock.patch.object(GATE, "wait_for_marker"),
                mock.patch.object(GATE, "wait_for_marker_count"),
                mock.patch.object(
                    GATE, "wait_for_client_user_ids",
                    return_value=(4, 7, None, ["status"]),
                ),
                mock.patch.object(GATE, "rcon_command", return_value="print\n"),
                mock.patch.object(
                    GATE, "capture_fixture_liveness_baseline",
                    side_effect=RuntimeError("stop after rcon activity"),
                ),
                mock.patch.object(GATE, "terminate", side_effect=terminate),
                mock.patch.object(Path, "write_text", new=fail_write),
                self.assertRaisesRegex(RuntimeError, "stop after rcon activity"),
            ):
                GATE.run_once(
                    server_command=["server", "net_port", "27960"],
                    shooter_command=["shooter"],
                    target_command=["target"],
                    working_dir=run_root,
                    run_root=run_root,
                    timeout=1.0,
                    mode=GATE.GATE_MODES["railgun"],
                )

        write_index = events.index(("write", "server.rcon"))
        for process in (shooter, target, server):
            self.assertIn(("terminate", process), events[:write_index])

    def test_windows_teardown_kills_the_complete_isolated_process_tree(self) -> None:
        process = mock.Mock()
        process.pid = 4242
        process.poll.return_value = None
        with (
            mock.patch.object(GATE.os, "name", "nt"),
            mock.patch.object(GATE.subprocess, "run") as taskkill,
        ):
            self.assertTrue(GATE.terminate(process))
        taskkill.assert_called_once_with(
            ["taskkill", "/PID", "4242", "/T", "/F"],
            stdin=GATE.subprocess.DEVNULL,
            stdout=GATE.subprocess.DEVNULL,
            stderr=GATE.subprocess.DEVNULL,
            check=False,
            timeout=5,
            creationflags=GATE.creation_flags(),
        )
        process.wait.assert_called_once_with(timeout=5)
        process.terminate.assert_not_called()

    def test_parser_requires_every_canonical_weapon_proof(self) -> None:
        status = GATE.parse_status(PASS_LINE)
        self.assertEqual(GATE.validate_status(status), status)
        self.assertEqual(status["target_history_captures"], 6)
        self.assertEqual(status["applied_age_us"], 50_000)
        self.assertEqual(status["trace_current_time_us"], 70_000)
        self.assertEqual(status["context_snapshot_time_us"], 70_000)
        self.assertEqual(status["context_mapped_time_us"], 50_000)
        self.assertEqual(status["target_capture_prepares"], 6)
        self.assertEqual(status["capture_append_rejections"], 0)
        self.assertEqual(status["target_capture_callbacks"], 6)
        self.assertEqual(status["observation_weapon_policy"], 5)
        self.assertEqual(status["expected_damage"], 80)
        self.assertEqual(status["observed_damage"], 80)
        self.assertEqual(status["water_retrace_required"], 0)
        self.assertEqual(status["water_retrace_observed"], 0)
        self.assertEqual(status["thunderbolt_discharge_required"], 0)
        self.assertEqual(status["thunderbolt_discharge_ammo_drained"], 0)
        self.assertEqual(status["thunderbolt_discharge_observed"], 0)
        self.assertEqual(status["sustained_hold_required"], 0)
        self.assertEqual(status["sustained_hold_interrupted"], 0)
        self.assertEqual(status["projectile_forward_required"], 0)
        self.assertEqual(status["projectile_forward_authenticated"], 0)
        self.assertEqual(status["projectile_forward_advanced"], 0)
        self.assertEqual(status["projectile_forward_blocked"], 0)
        self.assertEqual(status["melee_selection_required"], 0)
        self.assertEqual(status["melee_selection_authenticated"], 0)
        self.assertEqual(status["melee_historical_eligible"], 0)
        self.assertEqual(status["melee_current_displacement_accepted"], 0)
        self.assertEqual(status["melee_current_displacement_units"], 0)
        self.assertEqual(status["prox_lifecycle_required"], 0)
        self.assertEqual(status["prox_mine_landed"], 0)
        self.assertEqual(status["prox_mine_triggered"], 0)
        self.assertEqual(status["prox_mine_exploded"], 0)
        self.assertEqual(status["historical_mover_occlusion_required"], 0)
        self.assertEqual(status["historical_mover_relocated"], 0)
        self.assertEqual(status["historical_mover_baseline_clear"], 0)
        self.assertEqual(status["historical_mover_occlusion_observed"], 0)
        self.assertEqual(status["historical_mover_target_undamaged"], 0)
        self.assertEqual(status["historical_mover_history_count"], 0)

    def test_spawn_protection_requires_historical_hit_and_exact_zero_damage(self) -> None:
        mode = GATE.GATE_MODES["railgun-spawn-protection"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "damage_applied": 1,
            "expected_damage": 0,
            "observed_damage": 0,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)

        damaged = dict(status)
        damaged["observed_damage"] = 80
        with self.assertRaisesRegex(RuntimeError, "zero-damage range"):
            GATE.validate_status(damaged, mode)

        no_historical_hit = dict(status)
        no_historical_hit["canonical_historical_hit"] = 0
        with self.assertRaisesRegex(RuntimeError, "canonical_historical_hit"):
            GATE.validate_status(no_historical_hit, mode)

    def test_spectator_mode_rejects_candidate_count_drift(self) -> None:
        mode = GATE.GATE_MODES["railgun-spectator-exclusion"]
        status = dict(GATE.parse_status(PASS_LINE))
        self.assertEqual(GATE.validate_status(status, mode), status)
        included_spectator = dict(status)
        included_spectator["playing_candidates"] = 3
        included_spectator["eligible_candidates"] = 3
        with self.assertRaisesRegex(RuntimeError, "client counts"):
            GATE.validate_status(included_spectator, mode)

    def test_railgun_mover_mode_requires_historical_occlusion_and_zero_damage(
        self,
    ) -> None:
        mode = GATE.GATE_MODES["railgun-mover-occlusion"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "damage_applied": 0,
            "observed_damage": 0,
            "historical_mover_occlusion_required": 1,
            "historical_mover_relocated": 1,
            "historical_mover_baseline_clear": 1,
            "historical_mover_occlusion_observed": 1,
            "historical_mover_target_undamaged": 1,
            "historical_mover_history_count": 12,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        damaged = dict(status)
        damaged["observed_damage"] = 80
        with self.assertRaisesRegex(RuntimeError, "damaged through"):
            GATE.validate_status(damaged, mode)

    def test_chainfist_mode_requires_bounded_hybrid_melee_proof(self) -> None:
        mode = GATE.GATE_MODES["chainfist"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "observation_weapon_policy": 12,
            "expected_damage": 15,
            "observed_damage": 15,
            "melee_selection_required": 1,
            "melee_selection_authenticated": 1,
            "melee_historical_eligible": 1,
            "melee_current_displacement_accepted": 1,
            "melee_current_displacement_units": 64,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        bad_displacement = dict(status)
        bad_displacement["melee_current_displacement_units"] = 65
        with self.assertRaisesRegex(RuntimeError, "bounded hybrid melee"):
            GATE.validate_status(bad_displacement, mode)

    def test_etf_rifle_mode_requires_current_world_spawn_forward(self) -> None:
        mode = GATE.GATE_MODES["etf-rifle"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 13,
            "expected_damage": 10,
            "observed_damage": 10,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        wrong_impact = dict(status)
        wrong_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(wrong_impact, mode)

    def test_grenade_launcher_mode_requires_clear_current_world_ballistic_forward(self) -> None:
        mode = GATE.GATE_MODES["grenade-launcher"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 15,
            "expected_damage": 60,
            "observed_damage": 57,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_hand_grenade_mode_requires_release_time_current_world_forward(self) -> None:
        mode = GATE.GATE_MODES["hand-grenade"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 16,
            "expected_damage": 60,
            "observed_damage": 57,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        wrong_impact = dict(status)
        wrong_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(wrong_impact, mode)

    def test_prox_launcher_mode_requires_clear_current_world_ballistic_forward(self) -> None:
        mode = GATE.GATE_MODES["prox-launcher"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 17,
            "expected_damage": 90,
            "observed_damage": 0,
            "damage_applied": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_prox_launcher_lifecycle_mode_requires_normal_landing_trigger_and_explosion(self) -> None:
        mode = GATE.GATE_MODES["prox-launcher-lifecycle"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 17,
            "expected_damage": 61,
            "observed_damage": 61,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
            "prox_lifecycle_required": 1,
            "prox_mine_landed": 1,
            "prox_mine_triggered": 1,
            "prox_mine_exploded": 1,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_trigger = dict(status)
        missing_trigger["prox_mine_triggered"] = 0
        with self.assertRaisesRegex(RuntimeError, "normal lifecycle"):
            GATE.validate_status(missing_trigger, mode)

    def test_phalanx_mode_requires_current_world_spawn_forward(self) -> None:
        mode = GATE.GATE_MODES["phalanx"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 14,
            "expected_damage": 80,
            "observed_damage": 80,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        wrong_impact = dict(status)
        wrong_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(wrong_impact, mode)

    def test_hyperblaster_mode_requires_current_world_spawn_forward(self) -> None:
        mode = GATE.GATE_MODES["hyperblaster"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 11,
            "expected_damage": 15,
            "observed_damage": 15,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        wrong_impact = dict(status)
        wrong_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(wrong_impact, mode)

    def test_phalanx_splash_mode_requires_current_world_splash_proof(self) -> None:
        mode = GATE.GATE_MODES["phalanx-splash"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 14,
            "expected_damage": 93,
            "observed_damage": 93,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        wrong_splash = dict(status)
        wrong_splash["observed_damage"] = 100
        with self.assertRaisesRegex(RuntimeError, "reduced splash"):
            GATE.validate_status(wrong_splash, mode)

    def test_machinegun_mode_requires_its_own_policy_and_damage(self) -> None:
        mode = GATE.GATE_MODES["machinegun"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":1:8:8:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        wrong_policy = dict(status)
        wrong_policy["observation_weapon_policy"] = 5
        with self.assertRaisesRegex(RuntimeError, "wrong weapon policy"):
            GATE.validate_status(wrong_policy, mode)

    def test_shotgun_mode_requires_all_pellet_damage(self) -> None:
        mode = GATE.GATE_MODES["shotgun"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":3:48:48:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_pellet = dict(status)
        missing_pellet["observed_damage"] = 44
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(missing_pellet, mode)

    def test_chaingun_mode_requires_all_three_burst_rounds(self) -> None:
        mode = GATE.GATE_MODES["chaingun"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":2:18:18:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_round = dict(status)
        missing_round["observed_damage"] = 12
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(missing_round, mode)

    def test_super_shotgun_mode_requires_both_full_pellet_barrels(self) -> None:
        mode = GATE.GATE_MODES["super-shotgun"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":4:120:120:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_barrel = dict(status)
        missing_barrel["observed_damage"] = 60
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(missing_barrel, mode)

    def test_disruptor_mode_requires_its_full_delayed_damage(self) -> None:
        mode = GATE.GATE_MODES["disruptor"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":6:45:45:0:0:0:0:0:0:0:1:1:1:0:0:50000:50000")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        incomplete_daemon = dict(status)
        incomplete_daemon["observed_damage"] = 36
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(incomplete_daemon, mode)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_rocket_mode_requires_current_authority_damage_and_forward_proof(self) -> None:
        mode = GATE.GATE_MODES["rocket"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 9,
            "expected_damage": 100,
            "observed_damage": 100,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_forward = dict(status)
        missing_forward["projectile_forward_advanced"] = 0
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(missing_forward, mode)
        wrong_damage = dict(status)
        wrong_damage["observed_damage"] = 90
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(wrong_damage, mode)

    def test_rocket_mover_relative_mode_requires_paired_motion_and_current_authority(self) -> None:
        mode = GATE.GATE_MODES["rocket-mover-relative"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 9,
            "expected_damage": 100,
            "observed_damage": 100,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
            "mover_relative_projectile_required": 1,
            "mover_relative_policy": 1,
            "mover_relative_target_history_moved": 1,
            "mover_relative_mover_history_moved": 1,
            "mover_relative_pair_preserved": 1,
            "mover_relative_current_world_impact": 1,
            "mover_relative_authority_unchanged": 1,
            "mover_relative_history_pairs": 6,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        for field in (
            "mover_relative_target_history_moved",
            "mover_relative_mover_history_moved",
            "mover_relative_pair_preserved",
            "mover_relative_current_world_impact",
            "mover_relative_authority_unchanged",
        ):
            with self.subTest(field=field):
                incomplete = dict(status)
                incomplete[field] = 0
                with self.assertRaisesRegex(
                    RuntimeError, "mover-relative projectile"
                ):
                    GATE.validate_status(incomplete, mode)
        wrong_policy = dict(status)
        wrong_policy["mover_relative_policy"] = 0
        with self.assertRaisesRegex(RuntimeError, "wrong policy"):
            GATE.validate_status(wrong_policy, mode)
        missing_pairs = dict(status)
        missing_pairs["mover_relative_history_pairs"] = 1
        with self.assertRaisesRegex(RuntimeError, "paired moving history"):
            GATE.validate_status(missing_pairs, mode)

    def test_rocket_lifecycle_touch_requires_exact_single_retirement_and_hold(self) -> None:
        mode = GATE.GATE_MODES["rocket-lifecycle-touch"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 9,
            "expected_damage": 100,
            "observed_damage": 100,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
            "rocket_lifecycle_required": 1,
            "rocket_lifecycle_policy": 1,
            "rocket_owner_identity_retained": 1,
            "rocket_touch_count": 1,
            "rocket_touch_current_world": 1,
            "rocket_retired": 1,
            "rocket_retired_by_touch": 1,
            "rocket_retired_by_expiry": 0,
            "rocket_post_touch_hold_verified": 1,
            "rocket_no_double_damage": 1,
            "rocket_lifetime_scheduled_ms": 10_000,
            "rocket_lifetime_elapsed_ms": 150,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        for field in (
            "rocket_owner_identity_retained",
            "rocket_touch_current_world",
            "rocket_retired",
            "rocket_retired_by_touch",
            "rocket_post_touch_hold_verified",
            "rocket_no_double_damage",
        ):
            with self.subTest(field=field):
                invalid = dict(status)
                invalid[field] = 0
                with self.assertRaisesRegex(RuntimeError, f"wrong {field}"):
                    GATE.validate_status(invalid, mode)
        doubled = dict(status)
        doubled["rocket_touch_count"] = 2
        with self.assertRaisesRegex(RuntimeError, "wrong rocket_touch_count"):
            GATE.validate_status(doubled, mode)

    def test_rocket_lifetime_expiry_requires_no_touch_and_scheduled_timing(self) -> None:
        mode = GATE.GATE_MODES["rocket-lifetime-expiry"]
        status = dict(GATE.parse_status(PASS_LINE))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 9,
            "expected_damage": 0,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
            "rocket_lifecycle_required": 1,
            "rocket_lifecycle_policy": 2,
            "rocket_owner_identity_retained": 1,
            "rocket_touch_count": 0,
            "rocket_touch_current_world": 0,
            "rocket_retired": 1,
            "rocket_retired_by_touch": 0,
            "rocket_retired_by_expiry": 1,
            "rocket_post_touch_hold_verified": 0,
            "rocket_no_double_damage": 1,
            "rocket_lifetime_scheduled_ms": 10_000,
            "rocket_lifetime_elapsed_ms": 9_950,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        touched = dict(status)
        touched["rocket_touch_count"] = 1
        with self.assertRaisesRegex(RuntimeError, "wrong rocket_touch_count"):
            GATE.validate_status(touched, mode)
        early = dict(status)
        early["rocket_lifetime_elapsed_ms"] = 9_900
        with self.assertRaisesRegex(RuntimeError, "scheduled lifetime"):
            GATE.validate_status(early, mode)

    def test_bfg_mode_requires_only_its_current_world_spawn_forward_proof(self) -> None:
        mode = GATE.GATE_MODES["bfg"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 18,
            "expected_damage": 200,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 950_000,
            "projectile_forward_advanced_age_us": 100_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        missing_forward = dict(status)
        missing_forward["projectile_forward_advanced"] = 0
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(missing_forward, mode)

    def test_ion_ripper_mode_requires_all_fifteen_current_world_bolt_launches(self) -> None:
        mode = GATE.GATE_MODES["ion-ripper"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 19,
            "expected_damage": 10,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
            "projectile_forward_launches": 15,
            "projectile_forward_expected_launches": 15,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_bolt = dict(status)
        missing_bolt["projectile_forward_launches"] = 14
        with self.assertRaisesRegex(RuntimeError, "complete every normal launch"):
            GATE.validate_status(missing_bolt, mode)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)

    def test_tesla_mine_mode_requires_release_bound_current_world_forward_only(self) -> None:
        mode = GATE.GATE_MODES["tesla-mine"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 20,
            "expected_damage": 3,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 56_000,
            "projectile_forward_advanced_age_us": 56_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_trap_mode_requires_release_bound_current_world_forward_only(self) -> None:
        mode = GATE.GATE_MODES["trap"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 21,
            "expected_damage": 20,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 56_000,
            "projectile_forward_advanced_age_us": 56_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_grapple_mode_requires_clear_current_world_hook_forward_only(self) -> None:
        mode = GATE.GATE_MODES["grapple"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 22,
            "expected_damage": 1,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 400_000,
            "projectile_forward_advanced_age_us": 100_000,
            "projectile_forward_clamped": 1,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_offhand_hook_mode_requires_its_native_current_world_forward_only(self) -> None:
        mode = GATE.GATE_MODES["offhand-hook"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 24,
            "expected_damage": 1,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 56_000,
            "projectile_forward_advanced_age_us": 56_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_proball_throw_mode_requires_clear_release_bound_forward_only(self) -> None:
        mode = GATE.GATE_MODES["proball-throw"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "damage_applied": 0,
            "observation_weapon_policy": 23,
            "expected_damage": 1,
            "observed_damage": 0,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 56_000,
            "projectile_forward_advanced_age_us": 56_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        blocked = dict(status)
        blocked["projectile_forward_blocked"] = 1
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(blocked, mode)

    def test_rocket_splash_mode_requires_reduced_current_authority_damage(self) -> None:
        mode = GATE.GATE_MODES["rocket-splash"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 9,
            "expected_damage": 58,
            "observed_damage": 58,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
            "splash_occlusion_required": 1,
            "splash_occlusion_policy": 1,
            "splash_radius_evaluated": 1,
            "splash_can_damage_observed": 1,
            "splash_can_damage_result": 1,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        direct_damage = dict(status)
        direct_damage["observed_damage"] = 100
        direct_damage["expected_damage"] = 100
        with self.assertRaisesRegex(RuntimeError, "reduced splash damage"):
            GATE.validate_status(direct_damage, mode)

    def test_rocket_splash_bsp_and_water_boundaries_are_exact(self) -> None:
        common = {
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 9,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
            "splash_occlusion_required": 1,
            "splash_radius_evaluated": 1,
            "splash_can_damage_observed": 1,
        }

        bsp_mode = GATE.GATE_MODES["rocket-splash-bsp-occlusion"]
        bsp = GATE.parse_status(
            PASS_LINE.replace(
                "sg_worr_rewind_canonical_rail_damage_status",
                str(bsp_mode["status_cvar"]),
            ),
            str(bsp_mode["status_cvar"]),
        )
        bsp.update(common)
        bsp.update({
            "expected_damage": 0,
            "observed_damage": 0,
            "splash_occlusion_policy": 2,
            "splash_can_damage_result": 0,
            "splash_bsp_blocker_verified": 1,
            "splash_water_boundary_verified": 0,
            "splash_target_undamaged": 1,
        })
        self.assertEqual(GATE.validate_status(bsp, bsp_mode), bsp)
        bsp_unblocked = dict(bsp)
        bsp_unblocked["splash_can_damage_result"] = 1
        with self.assertRaisesRegex(RuntimeError, "wrong splash_can_damage_result"):
            GATE.validate_status(bsp_unblocked, bsp_mode)
        bsp_damaged = dict(bsp)
        bsp_damaged["observed_damage"] = 1
        with self.assertRaisesRegex(RuntimeError, "exact damage"):
            GATE.validate_status(bsp_damaged, bsp_mode)

        water_mode = GATE.GATE_MODES["rocket-splash-water-boundary"]
        water = GATE.parse_status(
            PASS_LINE.replace(
                "sg_worr_rewind_canonical_rail_damage_status",
                str(water_mode["status_cvar"]),
            ),
            str(water_mode["status_cvar"]),
        )
        water.update(common)
        water.update({
            "expected_damage": 58,
            "observed_damage": 58,
            "splash_occlusion_policy": 3,
            "splash_can_damage_result": 1,
            "splash_bsp_blocker_verified": 0,
            "splash_water_boundary_verified": 1,
            "splash_target_undamaged": 0,
        })
        self.assertEqual(GATE.validate_status(water, water_mode), water)
        missing_boundary = dict(water)
        missing_boundary["splash_water_boundary_verified"] = 0
        with self.assertRaisesRegex(
            RuntimeError, "wrong splash_water_boundary_verified"
        ):
            GATE.validate_status(missing_boundary, water_mode)

    def test_plasma_gun_mode_requires_current_authority_direct_damage_and_forward_proof(self) -> None:
        mode = GATE.GATE_MODES["plasma-gun"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 10,
            "expected_damage": 20,
            "observed_damage": 20,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        wrong_damage = dict(status)
        wrong_damage["observed_damage"] = 15
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(wrong_damage, mode)

    def test_plasma_gun_splash_mode_requires_reduced_current_authority_damage(self) -> None:
        mode = GATE.GATE_MODES["plasma-gun-splash"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 10,
            "expected_damage": 7,
            "observed_damage": 7,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        historical_impact = dict(status)
        historical_impact["canonical_historical_hit"] = 1
        with self.assertRaisesRegex(RuntimeError, "historical impact"):
            GATE.validate_status(historical_impact, mode)
        direct_damage = dict(status)
        direct_damage["observed_damage"] = 100
        direct_damage["expected_damage"] = 100
        with self.assertRaisesRegex(RuntimeError, "reduced splash damage"):
            GATE.validate_status(direct_damage, mode)

    def test_blaster_mode_requires_current_authority_direct_damage_and_forward_proof(self) -> None:
        mode = GATE.GATE_MODES["blaster"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 11,
            "expected_damage": 15,
            "observed_damage": 15,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_forward = dict(status)
        missing_forward["projectile_forward_authenticated"] = 0
        with self.assertRaisesRegex(RuntimeError, "bounded current-world forward"):
            GATE.validate_status(missing_forward, mode)
        wrong_damage = dict(status)
        wrong_damage["observed_damage"] = 20
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(wrong_damage, mode)

    def test_blaster_local_action_lease_mode_requires_exact_join(self) -> None:
        mode = GATE.GATE_MODES["blaster-local-action-lease"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        status.update({
            "canonical_historical_hit": 0,
            "observation_weapon_policy": 11,
            "expected_damage": 15,
            "observed_damage": 15,
            "projectile_forward_required": 1,
            "projectile_forward_authenticated": 1,
            "projectile_forward_advanced": 1,
            "projectile_forward_blocked": 0,
            "projectile_forward_age_us": 50_000,
            "projectile_forward_advanced_age_us": 50_000,
        })
        self.assertEqual(GATE.validate_status(status, mode), status)

        missing_join = dict(status)
        missing_join["local_action_joined_record"] = 0
        with self.assertRaisesRegex(RuntimeError, "joined_record"):
            GATE.validate_status(missing_join, mode)

        rejected = dict(status)
        rejected["local_action_lease_rejected"] = 1
        with self.assertRaisesRegex(RuntimeError, "observed a rejection"):
            GATE.validate_status(rejected, mode)

        wrong_catalog = dict(status)
        wrong_catalog["local_action_shadow_catalog_id"] = 2
        with self.assertRaisesRegex(RuntimeError, "catalog identity"):
            GATE.validate_status(wrong_catalog, mode)

        missing_base_flag = dict(status)
        missing_base_flag["local_action_shadow_flags"] = 6
        with self.assertRaisesRegex(RuntimeError, "base flags"):
            GATE.validate_status(missing_base_flag, mode)

        wrong_blockers = dict(status)
        wrong_blockers["local_action_shadow_v2_blockers"] = 0
        with self.assertRaisesRegex(RuntimeError, "blocker mask"):
            GATE.validate_status(wrong_blockers, mode)

        missing_hash = dict(status)
        missing_hash["local_action_shadow_record_hash"] = 0
        with self.assertRaisesRegex(RuntimeError, "record hash"):
            GATE.validate_status(missing_hash, mode)

    def test_combined_native_status_requires_both_acknowledged_snapshot_peers(self) -> None:
        client_text = (
            "WORR_NATIVE_CLIENT_STATUS_V1 schema=1 enabled=1 mode=2 "
            "capability_confirmed=1 protocol=1038 private_mask=0x77 "
            "server_active=1 failures=0 last_failure=0\n"
        )
        client_rows = GATE.parse_native_status_rows(
            client_text, GATE.NATIVE_CLIENT_STATUS_MARKER,
        )
        self.assertEqual(len(client_rows), 1)
        self.assertEqual(client_rows[0]["private_mask"], 0x77)
        self.assertEqual(
            GATE.validate_combined_native_client_status(client_rows[0]),
            client_rows[0],
        )

        base_text = "\n".join(
            "WORR_NATIVE_SERVER_STATUS_V1 schema=1 slot={} protocol=1038 "
            "enabled=1 private_mask=0x77 wire_committed=1 failures=0 "
            "rx_rejections=0 tx_ack_rejections=0 last_failure=0".format(slot)
            for slot in (0, 1)
        )
        snapshot_text = "\n".join(
            "WORR_NATIVE_SERVER_SNAPSHOT_STATUS_V1 schema=1 slot={} "
            "snapshot_epoch={} sender=1 tx_open=1 queued=8 "
            "queue_failures=0 acks=7 released=7 confirmed=8 rejected=0 "
            "first_sends=8".format(slot, slot + 5)
            for slot in (0, 1)
        )
        base_rows = GATE.parse_native_status_rows(
            base_text, GATE.NATIVE_SERVER_STATUS_MARKER,
        )
        snapshot_rows = GATE.parse_native_status_rows(
            snapshot_text, GATE.NATIVE_SERVER_SNAPSHOT_STATUS_MARKER,
        )
        evidence = GATE.validate_combined_native_server_status(
            base_rows, snapshot_rows,
        )
        self.assertEqual(len(evidence["server_peers"]), 2)
        self.assertEqual(len(evidence["snapshot_peers"]), 2)
        target_evidence = GATE.validate_combined_native_server_status(
            base_rows[1:], snapshot_rows[1:], 0x77, (1,),
        )
        self.assertEqual(len(target_evidence["server_peers"]), 1)
        self.assertEqual(target_evidence["server_peers"][0]["slot"], 1)

        snapshot_rows[1]["acks"] = 0
        with self.assertRaisesRegex(RuntimeError, "acks traffic"):
            GATE.validate_combined_native_server_status(
                base_rows, snapshot_rows,
            )

        terminal = dict(base_rows[1])
        terminal.update({
            "enabled": 0,
            "lifecycle": 3,
            "failures": 1,
            "last_failure": 16,
            "last_failure_detail": 0x01140080,
        })
        with self.assertRaisesRegex(
            RuntimeError, "terminal drain.*last_failure=16.*01140080",
        ):
            GATE.reject_terminal_native_server_rows(
                [base_rows[0], terminal], (0, 1),
            )

    def test_event_native_status_requires_exact_0x73_client_server_peers(self) -> None:
        client_text = (
            "WORR_NATIVE_CLIENT_STATUS_V1 schema=1 enabled=1 mode=2 "
            "capability_confirmed=1 protocol=1038 public_mask=0x73 "
            "private_mask=0x73 server_active=1 failures=0 last_failure=0\n"
        )
        client_row = GATE.parse_native_status_rows(
            client_text, GATE.NATIVE_CLIENT_STATUS_MARKER,
        )[0]
        self.assertEqual(
            GATE.validate_native_base_client_status(client_row, 0x73),
            client_row,
        )

        base_text = "\n".join(
            "WORR_NATIVE_SERVER_STATUS_V1 schema=1 slot={} protocol=1038 "
            "enabled=1 lifecycle=2 hooks=1 public_mask=0x73 private_mask=0x73 "
            "wire_committed=1 "
            "server_active=1 failures=0 rx_rejections=0 "
            "tx_ack_rejections=0 last_failure=0".format(slot)
            for slot in (0, 1)
        )
        base_rows = GATE.parse_native_status_rows(
            base_text, GATE.NATIVE_SERVER_STATUS_MARKER,
        )
        evidence = GATE.validate_native_base_server_status(
            base_rows, 0x73, (0, 1),
        )
        self.assertEqual([row["slot"] for row in evidence], [0, 1])

        wrong_client = dict(client_row)
        wrong_client["public_mask"] = 0x53
        with self.assertRaisesRegex(RuntimeError, "public_mask=115"):
            GATE.validate_native_base_client_status(wrong_client, 0x73)

        wrong_server = [dict(row) for row in base_rows]
        wrong_server[1]["private_mask"] = 0x77
        with self.assertRaisesRegex(RuntimeError, "private_mask=115"):
            GATE.validate_native_base_server_status(
                wrong_server, 0x73, (0, 1),
            )
        for name, value in (("lifecycle", 1), ("hooks", 0)):
            wrong_server = [dict(row) for row in base_rows]
            wrong_server[1][name] = value
            with self.subTest(name=name), self.assertRaisesRegex(
                    RuntimeError, f"{name}="):
                GATE.validate_native_base_server_status(
                    wrong_server, 0x73, (0, 1),
                )

    def test_native_server_reactivation_requires_every_new_epoch_signal(
        self,
    ) -> None:
        baselines = {
            slot: GATE.parse_native_status_rows(
                native_base_server_line(slot),
                GATE.NATIVE_SERVER_STATUS_MARKER,
            )[0]
            for slot in (4, 7)
        }
        rows = []
        for slot, baseline in baselines.items():
            rows.append(GATE.parse_native_status_rows(
                native_base_server_line(
                    slot,
                    official_epoch=baseline["official_epoch"] + 1,
                    transport_epoch=baseline["transport_epoch"] + 1,
                    wire_committed_transport_epoch=(
                        baseline["wire_committed_transport_epoch"] + 1
                    ),
                    challenges_queued=baseline["challenges_queued"] + 1,
                    client_ready=baseline["client_ready"] + 1,
                    server_active=baseline["server_active"] + 1,
                ),
                GATE.NATIVE_SERVER_STATUS_MARKER,
            )[0])
        self.assertEqual(
            GATE.validate_native_base_server_reactivation(rows, baselines),
            rows,
        )

        stale_cases = (
            ("lifecycle", 1, "reactivate"),
            ("hooks", 0, "reactivate"),
            ("official_epoch", baselines[4]["official_epoch"], "advance"),
            ("transport_epoch", baselines[4]["transport_epoch"], "advance"),
            (
                "wire_committed_transport_epoch",
                baselines[4]["wire_committed_transport_epoch"],
                "advance",
            ),
            (
                "challenges_queued", baselines[4]["challenges_queued"],
                "advance",
            ),
            ("client_ready", baselines[4]["client_ready"], "advance"),
            ("server_active", baselines[4]["server_active"], "advance"),
        )
        for name, value, message in stale_cases:
            invalid = [dict(row) for row in rows]
            invalid[0][name] = value
            with self.subTest(name=name), self.assertRaisesRegex(
                    RuntimeError, message):
                GATE.validate_native_base_server_reactivation(
                    invalid, baselines,
                )

        mismatched_commit = [dict(row) for row in rows]
        mismatched_commit[0]["wire_committed_transport_epoch"] += 1
        with self.assertRaisesRegex(RuntimeError, "different transport epoch"):
            GATE.validate_native_base_server_reactivation(
                mismatched_commit, baselines,
            )

    def test_native_client_server_epochs_require_exact_role_alignment(
        self,
    ) -> None:
        server_rows = [
            GATE.parse_native_status_rows(
                native_base_server_line(slot),
                GATE.NATIVE_SERVER_STATUS_MARKER,
            )[0]
            for slot in (4, 7)
        ]
        server_by_slot = {row["slot"]: row for row in server_rows}
        client_slots = {"shooter": 4, "target": 7}
        clients = {
            role: GATE.parse_native_status_rows(
                native_base_client_line(
                    official_epoch=server_by_slot[slot]["official_epoch"],
                    transport_epoch=server_by_slot[slot]["transport_epoch"],
                ),
                GATE.NATIVE_CLIENT_STATUS_MARKER,
            )[0]
            for role, slot in client_slots.items()
        }
        self.assertEqual(
            GATE.validate_native_base_client_server_epochs(
                clients, server_rows, client_slots,
            ),
            clients,
        )

        for name, value in (
            ("official_epoch", clients["target"]["official_epoch"] + 1),
            ("transport_epoch", 0),
        ):
            mismatched = {role: dict(row) for role, row in clients.items()}
            mismatched["target"][name] = value
            with self.subTest(name=name), self.assertRaisesRegex(
                    RuntimeError, f"epoch mismatch.*name={name}"):
                GATE.validate_native_base_client_server_epochs(
                    mismatched, server_rows, client_slots,
                )

    def test_native_base_wait_polls_past_fresh_map_quiesced_rows(self) -> None:
        shooter = mock.Mock()
        target = mock.Mock()
        shooter.poll.return_value = None
        target.poll.return_value = None
        commands: list[str] = []
        status_round = {4: 0, 7: 0}
        server_status_calls = [0]
        prior_server_peers = [
            GATE.parse_native_status_rows(
                native_base_server_line(slot),
                GATE.NATIVE_SERVER_STATUS_MARKER,
            )[0]
            for slot in (4, 7)
        ]
        prior_by_slot = {row["slot"]: row for row in prior_server_peers}

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shooter_path = root / "shooter.log"
            target_path = root / "target.log"
            # A valid row from before this wait must not satisfy the fresh-row
            # contract.
            stale = native_base_client_line() + "\n"
            shooter_path.write_text(stale, encoding="utf-8")
            target_path.write_text(stale, encoding="utf-8")

            def fake_rcon(
                _port: int, command: str, _timeout: float,
            ) -> str:
                commands.append(command)
                if command.startswith('stuff '):
                    slot = int(command.split(" ", 2)[1])
                    self.assertEqual(
                        command,
                        f'stuff {slot} "cl_worr_native_shadow_status"',
                    )
                    status_round[slot] += 1
                    if status_round[slot] == 1:
                        row = native_base_client_line(
                            mode=3, readiness_phase=0, server_active=0,
                            official_epoch=(
                                prior_by_slot[slot]["official_epoch"] + 1
                            ),
                            transport_epoch=(
                                prior_by_slot[slot]["transport_epoch"] + 1
                            ),
                            proof_enqueued=0, retained_releases=0,
                            tx_first_sends=0, acknowledged_reliable=0,
                        )
                    elif status_round[slot] == 2:
                        row = native_base_client_line(
                            official_epoch=(
                                prior_by_slot[slot]["official_epoch"] + 1
                            ),
                            transport_epoch=(
                                prior_by_slot[slot]["transport_epoch"] + 1
                            ),
                            proof_enqueued=0, retained_releases=0,
                            tx_first_sends=0, acknowledged_reliable=0,
                        )
                    else:
                        row = native_base_client_line(
                            official_epoch=(
                                prior_by_slot[slot]["official_epoch"] + 1
                            ),
                            transport_epoch=(
                                prior_by_slot[slot]["transport_epoch"] + 1
                            ),
                        )
                    path = shooter_path if slot == 4 else target_path
                    with path.open("a", encoding="utf-8") as output:
                        output.write(row + "\n")
                    return "client status requested\n"
                slot = int(command.rsplit(" ", 1)[1])
                server_status_calls[0] += 1
                baseline = prior_by_slot[slot]
                if server_status_calls[0] <= 2:
                    return native_base_server_line(
                        slot, lifecycle=1,
                        official_epoch=baseline["official_epoch"],
                        transport_epoch=baseline["transport_epoch"],
                        wire_committed_transport_epoch=baseline[
                            "wire_committed_transport_epoch"
                        ],
                        challenges_queued=baseline["challenges_queued"],
                        client_ready=baseline["client_ready"],
                        server_active=baseline["server_active"],
                        legacy_joins=baseline["legacy_joins"],
                        command_matches=baseline["command_matches"],
                    )
                activity = 3 if server_status_calls[0] > 4 else 2
                return native_base_server_line(
                    slot,
                    official_epoch=baseline["official_epoch"] + 1,
                    transport_epoch=baseline["transport_epoch"] + 1,
                    wire_committed_transport_epoch=(
                        baseline["wire_committed_transport_epoch"] + 1
                    ),
                    challenges_queued=baseline["challenges_queued"] + 1,
                    client_ready=baseline["client_ready"] + 1,
                    server_active=baseline["server_active"] + 1,
                    legacy_joins=activity, command_matches=activity,
                )

            with (
                mock.patch.object(GATE, "rcon_command", side_effect=fake_rcon),
                mock.patch.object(GATE.time, "sleep"),
            ):
                evidence, responses = GATE.wait_for_native_base_shadow(
                    27960, shooter, target, shooter_path, target_path, 1.0,
                    0x73, ("shooter", "target"), (4, 7),
                    prior_server_peers,
                )

        self.assertEqual(status_round, {4: 3, 7: 3})
        self.assertEqual(server_status_calls[0], 6)
        self.assertTrue(all(
            command.startswith("sv_worr_native_shadow_status ")
            for command in commands[:4]
        ))
        self.assertEqual(
            commands.count('stuff 4 "cl_worr_native_shadow_status"'), 3,
        )
        self.assertEqual(
            commands.count('stuff 7 "cl_worr_native_shadow_status"'), 3,
        )
        self.assertEqual(evidence["clients"]["shooter"]["mode"], 2)
        self.assertEqual(evidence["clients"]["target"]["readiness_phase"], 5)
        self.assertEqual(
            [row["slot"] for row in evidence["server_peers"]], [4, 7],
        )
        self.assertEqual(len(responses), 12)

    def test_native_base_wait_times_out_on_only_transient_client_rows(self) -> None:
        shooter = mock.Mock()
        target = mock.Mock()
        shooter.poll.return_value = None
        target.poll.return_value = None
        clock = [0.0]
        status_requests = [0]
        server_status_requests = [0]

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shooter_path = root / "shooter.log"
            target_path = root / "target.log"
            shooter_path.write_text("connected\n", encoding="utf-8")
            target_path.write_text("connected\n", encoding="utf-8")

            def fake_rcon(
                _port: int, command: str, _timeout: float,
            ) -> str:
                if command.startswith("sv_worr_native_shadow_status "):
                    slot = int(command.rsplit(" ", 1)[1])
                    server_status_requests[0] += 1
                    count = 1 + (server_status_requests[0] - 1) // 2
                    return native_base_server_line(
                        slot, legacy_joins=count, command_matches=count,
                    )
                self.assertRegex(
                    command,
                    r'^stuff (?:4|7) "cl_worr_native_shadow_status"$',
                )
                status_requests[0] += 1
                row = native_base_client_line(
                    mode=3, readiness_phase=0, server_active=0,
                )
                slot = int(command.split(" ", 2)[1])
                path = shooter_path if slot == 4 else target_path
                with path.open("a", encoding="utf-8") as output:
                    output.write(row + "\n")
                return "client status requested\n"

            def advance(seconds: float) -> None:
                clock[0] += seconds

            with (
                mock.patch.object(GATE, "rcon_command", side_effect=fake_rcon),
                mock.patch.object(
                    GATE.time, "monotonic", side_effect=lambda: clock[0],
                ),
                mock.patch.object(GATE.time, "sleep", side_effect=advance),
                self.assertRaisesRegex(
                    RuntimeError,
                    "timed out waiting for native client status proof.*mode",
                ),
            ):
                GATE.wait_for_native_base_shadow(
                    27960, shooter, target, shooter_path, target_path, 1.1,
                    0x73, ("shooter", "target"), (4, 7),
                )

        self.assertEqual(status_requests[0], 6)

    def test_native_base_wait_fails_fast_on_client_exit(self) -> None:
        shooter = mock.Mock()
        target = mock.Mock()
        shooter.poll.return_value = 17
        target.poll.return_value = None

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shooter_path = root / "shooter.log"
            target_path = root / "target.log"
            shooter_path.write_text("connected\n", encoding="utf-8")
            target_path.write_text("connected\n", encoding="utf-8")
            with (
                mock.patch.object(GATE, "rcon_command") as rcon,
                self.assertRaisesRegex(
                    RuntimeError,
                    "native shooter client exited before status proof",
                ),
            ):
                GATE.wait_for_native_base_shadow(
                    27960, shooter, target, shooter_path, target_path, 1.0,
                    0x73, ("shooter", "target"), (4, 7),
                )
        rcon.assert_not_called()

    def test_native_base_wait_fails_fast_on_terminal_client_readiness(self) -> None:
        terminal_rows = (
            native_base_client_line(
                mode=3, readiness_phase=6, server_active=0,
            ),
            native_base_client_line(
                mode=3, readiness_phase=0, server_active=0,
                failures=1, last_failure=109,
            ),
        )
        for terminal_row in terminal_rows:
            with self.subTest(terminal_row=terminal_row):
                shooter = mock.Mock()
                target = mock.Mock()
                shooter.poll.return_value = None
                target.poll.return_value = None
                server_status_requests = [0]
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    shooter_path = root / "shooter.log"
                    target_path = root / "target.log"
                    shooter_path.write_text("connected\n", encoding="utf-8")
                    target_path.write_text("connected\n", encoding="utf-8")

                    def fake_rcon(
                        _port: int, command: str, _timeout: float,
                    ) -> str:
                        if command.startswith(
                                "sv_worr_native_shadow_status "):
                            slot = int(command.rsplit(" ", 1)[1])
                            server_status_requests[0] += 1
                            count = 1 + (
                                server_status_requests[0] - 1
                            ) // 2
                            return native_base_server_line(
                                slot, legacy_joins=count,
                                command_matches=count,
                            )
                        slot = int(command.split(" ", 2)[1])
                        self.assertEqual(
                            command,
                            f'stuff {slot} "cl_worr_native_shadow_status"',
                        )
                        if slot == 4:
                            with shooter_path.open(
                                    "a", encoding="utf-8") as output:
                                output.write(terminal_row + "\n")
                        return "client status requested\n"

                    with (
                        mock.patch.object(
                            GATE, "rcon_command", side_effect=fake_rcon,
                        ) as rcon,
                        self.assertRaisesRegex(
                            RuntimeError,
                            "native shooter client entered terminal readiness",
                        ),
                    ):
                        GATE.wait_for_native_base_shadow(
                            27960, shooter, target,
                            shooter_path, target_path, 1.0,
                            0x73, ("shooter", "target"), (4, 7),
                        )
                # Two phase-baselined server rounds precede one targeted
                # diagnostic per client; the first row then exposes failure.
                self.assertEqual(rcon.call_count, 6)

    def test_native_base_wait_rechecks_terminal_server_after_client_proof(
        self,
    ) -> None:
        shooter = mock.Mock()
        target = mock.Mock()
        shooter.poll.return_value = None
        target.poll.return_value = None
        server_status_calls = [0]
        client_status_round = {4: 0, 7: 0}

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shooter_path = root / "shooter.log"
            target_path = root / "target.log"
            shooter_path.write_text("connected\n", encoding="utf-8")
            target_path.write_text("connected\n", encoding="utf-8")

            def fake_rcon(
                _port: int, command: str, _timeout: float,
            ) -> str:
                if command.startswith("sv_worr_native_shadow_status "):
                    slot = int(command.rsplit(" ", 1)[1])
                    server_status_calls[0] += 1
                    round_index = (server_status_calls[0] - 1) // 2
                    if round_index < 2:
                        count = round_index + 1
                        return native_base_server_line(
                            slot, legacy_joins=count,
                            command_matches=count,
                        )
                    if slot == 4:
                        return native_base_server_line(
                            slot, enabled=0, lifecycle=3, failures=1,
                            last_failure=16, last_failure_detail=0x01140080,
                            legacy_joins=3, command_matches=3,
                        )
                    return native_base_server_line(
                        slot, legacy_joins=3, command_matches=3,
                    )

                slot = int(command.split(" ", 2)[1])
                self.assertEqual(
                    command,
                    f'stuff {slot} "cl_worr_native_shadow_status"',
                )
                client_status_round[slot] += 1
                count = client_status_round[slot]
                row = native_base_client_line(
                    official_epoch=11 + slot, transport_epoch=11 + slot,
                    proof_enqueued=count, retained_releases=count,
                    tx_first_sends=count, acknowledged_reliable=count,
                )
                path = shooter_path if slot == 4 else target_path
                with path.open("a", encoding="utf-8") as output:
                    output.write(row + "\n")
                return "client status requested\n"

            with (
                mock.patch.object(GATE, "rcon_command", side_effect=fake_rcon),
                mock.patch.object(GATE.time, "sleep"),
                self.assertRaisesRegex(
                    RuntimeError, "terminal drain.*last_failure=16.*01140080",
                ),
            ):
                GATE.wait_for_native_base_shadow(
                    27960, shooter, target, shooter_path, target_path, 1.0,
                    0x73, ("shooter", "target"), (4, 7),
                )

        self.assertEqual(client_status_round, {4: 2, 7: 2})
        self.assertEqual(server_status_calls[0], 6)

    def test_legacy_capability_status_requires_exact_0x03_and_epochs(self) -> None:
        client_text = (
            "WORR_CAPABILITY_CLIENT_STATUS_V1 schema=1 valid=1 phase=2 "
            "protocol=1038 epoch=7 offered=0x03 supported=0x03 "
            "peer_supported=0x03 negotiated=0x03\n"
        )
        client_row = GATE.parse_native_status_rows(
            client_text, GATE.CAPABILITY_CLIENT_STATUS_MARKER,
        )[0]
        self.assertEqual(
            GATE.validate_legacy_capability_client_status(client_row),
            client_row,
        )

        server_text = "\n".join(
            "WORR_CAPABILITY_SERVER_STATUS_V1 schema=1 slot={} "
            "protocol=1038 epoch={} offered=0x03 supported=0x03 "
            "negotiated=0x03 confirm_sent=1 failed=0 native_shadow=0 "
            "input_batch_requested=0 command_parser=1".format(slot, epoch)
            for slot, epoch in ((0, 7), (1, 8))
        )
        server_rows = GATE.parse_native_status_rows(
            server_text, GATE.CAPABILITY_SERVER_STATUS_MARKER,
        )
        evidence = GATE.validate_legacy_capability_server_status(
            server_rows, (0, 1),
        )
        self.assertEqual([row["epoch"] for row in evidence], [7, 8])

        wrong_client = dict(client_row)
        wrong_client["negotiated"] = 0x53
        with self.assertRaisesRegex(RuntimeError, "negotiated=3"):
            GATE.validate_legacy_capability_client_status(wrong_client)

        wrong_server = [dict(row) for row in server_rows]
        wrong_server[0]["native_shadow"] = 1
        with self.assertRaisesRegex(RuntimeError, "native_shadow=0"):
            GATE.validate_legacy_capability_server_status(
                wrong_server, (0, 1),
            )

    def test_native_snapshot_presentation_requires_exact_promoted_parity(self) -> None:
        text = (
            "cg_snapshot_timeline_render: epoch=9 mode=3 clock=120/0 "
            "pair=119/0 align_fail=0 pair_mode=1 pair_blocks=0x0 "
            "samples=64 fail=0 invisible=1 discontinuity=1 "
            "parity=0/0 native=60/0 promoted=60 events=0/0/0 "
            "max_error=0.0000/0.0000/0.0000 "
            "timeline_modes=40/10/20 extrap_us=250000 "
            "enumeration=70/0/4200/3 resets=64 "
            "previous_only=3/3/2 view=1/60/62 "
            "submission=4800/4100/0123456789abcdef\n"
        )
        rows = GATE.parse_native_snapshot_presentation(text)
        self.assertEqual(len(rows), 1)
        self.assertEqual(
            GATE.validate_native_snapshot_presentation(rows[0]), rows[0],
        )

        mismatch = dict(rows[0])
        mismatch["parity_mismatches"] = 1
        with self.assertRaisesRegex(RuntimeError, "parity_mismatches"):
            GATE.validate_native_snapshot_presentation(mismatch)

        not_promoted = dict(rows[0])
        not_promoted["promoted_transforms"] = 59
        with self.assertRaisesRegex(RuntimeError, "every native sample"):
            GATE.validate_native_snapshot_presentation(not_promoted)

        excessive_extrapolation = dict(rows[0])
        excessive_extrapolation["extrapolation_time_us"] = 500001
        with self.assertRaisesRegex(RuntimeError, "bounded extrapolation"):
            GATE.validate_native_snapshot_presentation(excessive_extrapolation)

        enumeration_failure = dict(rows[0])
        enumeration_failure["enumeration_failures"] = 1
        with self.assertRaisesRegex(RuntimeError, "enumeration_failures"):
            GATE.validate_native_snapshot_presentation(enumeration_failure)

        invalid_view = dict(rows[0])
        invalid_view["native_view_result"] = 7
        with self.assertRaisesRegex(RuntimeError, "view is not valid"):
            GATE.validate_native_snapshot_presentation(invalid_view)

        no_renderer_submission = dict(rows[0])
        no_renderer_submission["renderer_submission_calls"] = 0
        with self.assertRaisesRegex(RuntimeError, "renderer_submission_calls"):
            GATE.validate_native_snapshot_presentation(no_renderer_submission)

        invalid_submission_counts = dict(rows[0])
        invalid_submission_counts["renderer_submitted_sources"] = 4801
        with self.assertRaisesRegex(RuntimeError, "sources exceed calls"):
            GATE.validate_native_snapshot_presentation(
                invalid_submission_counts
            )

        no_previous_submission = dict(rows[0])
        no_previous_submission["previous_only_submitted"] = 0
        with self.assertRaisesRegex(RuntimeError, "previous_only_submitted"):
            GATE.validate_native_snapshot_presentation(
                no_previous_submission
            )

        invalid_previous_order = dict(rows[0])
        invalid_previous_order["previous_only_selected"] = 1
        with self.assertRaisesRegex(RuntimeError, "not ordered"):
            GATE.validate_native_snapshot_presentation(
                invalid_previous_order
            )

        baseline = dict(rows[0])
        baseline["enumerated_removed_entities"] = 0
        baseline["previous_only_observed"] = 0
        baseline["previous_only_selected"] = 0
        baseline["previous_only_submitted"] = 0
        adaptive_baseline = {
            "enabled": 1,
            "adjustment": 1,
            "delay_us": 50_000,
            "baseline_delay_us": 50_000,
            "maximum_delay_us": 150_000,
            "cadence_us": 0,
            "jitter_us": 0,
            "last_jitter_us": 0,
            "rise_adjustments": 0,
            "recovery_adjustments": 0,
            "pressure_observations": 0,
            "reset_count": 1,
            "failures": 0,
        }
        self.assertEqual(
            GATE.validate_native_snapshot_presentation_baseline(
                baseline, adaptive_baseline,
            ),
            {"render": baseline, "adaptive": adaptive_baseline},
        )
        adaptive_after = dict(adaptive_baseline)
        adaptive_after.update({
            "adjustment": 5,
            "delay_us": 51_000,
            "cadence_us": 16_000,
            "rise_adjustments": 2,
            "recovery_adjustments": 1,
        })
        delta = GATE.validate_native_snapshot_presentation_delta(
            baseline, rows[0], adaptive_baseline, adaptive_after,
        )
        self.assertEqual(
            delta["previous_only"],
            {"observed": 3, "selected": 3, "submitted": 2},
        )
        no_phase_submission = dict(rows[0])
        no_phase_submission["previous_only_submitted"] = 0
        with self.assertRaisesRegex(RuntimeError, "no ordered"):
            GATE.validate_native_snapshot_presentation_delta(
                baseline, no_phase_submission,
                adaptive_baseline, adaptive_after,
            )

    def test_native_snapshot_adaptive_requires_a_bounded_live_rise(self) -> None:
        text = (
            "cg_snapshot_timeline_adaptive: enabled=1 adjustment=0 "
            "delay_us=51871/50000/150000 "
            "arrival_us=16129/375/871 "
            "counts=4/2/3/1 failures=0\n"
        )
        rows = GATE.parse_native_snapshot_adaptive(text)
        self.assertEqual(len(rows), 1)
        self.assertEqual(
            GATE.validate_native_snapshot_adaptive(rows[0]), rows[0],
        )

        no_rise = dict(rows[0])
        no_rise["delay_us"] = 50_000
        no_rise["rise_adjustments"] = 0
        with self.assertRaisesRegex(RuntimeError, "no live arrival/rise"):
            GATE.validate_native_snapshot_adaptive(no_rise)

        excessive = dict(rows[0])
        excessive["delay_us"] = 150_001
        with self.assertRaisesRegex(RuntimeError, "escaped its bounds"):
            GATE.validate_native_snapshot_adaptive(excessive)

        failed = dict(rows[0])
        failed["failures"] = 1
        with self.assertRaisesRegex(RuntimeError, "lifecycle"):
            GATE.validate_native_snapshot_adaptive(failed)

    def test_plasma_beam_mode_requires_its_first_held_command_tick(self) -> None:
        mode = GATE.GATE_MODES["plasma-beam"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":7:8:8:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_tick = dict(status)
        missing_tick["observed_damage"] = 0
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(missing_tick, mode)

    def test_thunderbolt_mode_requires_one_deduplicated_footprint_damage(self) -> None:
        mode = GATE.GATE_MODES["thunderbolt"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(
            ":5:80:80:" + ":".join(["0"] * 14),
            ":8:8:8:" + ":".join(["0"] * 14),
        )
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        duplicate_ray_damage = dict(status)
        duplicate_ray_damage["observed_damage"] = 24
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(duplicate_ray_damage, mode)

    def test_plasma_beam_held_mode_requires_three_normal_ticks(self) -> None:
        mode = GATE.GATE_MODES["plasma-beam-held"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":7:24:24:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        missing_tick = dict(status)
        missing_tick["observed_damage"] = 16
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(missing_tick, mode)

    def test_thunderbolt_held_mode_requires_three_deduplicated_ticks(self) -> None:
        mode = GATE.GATE_MODES["thunderbolt-held"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":8:24:24:0:0:0:0:0:0:0:0:0:0:0:0:0:0")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        duplicate_ray_damage = dict(status)
        duplicate_ray_damage["observed_damage"] = 32
        with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
            GATE.validate_status(duplicate_ray_damage, mode)

    def test_sustained_beam_modes_require_all_thirty_two_normal_ticks(self) -> None:
        for mode_name, policy in (("plasma-beam-sustained", 7),
                                  ("thunderbolt-sustained", 8)):
            mode = GATE.GATE_MODES[mode_name]
            line = PASS_LINE.replace(
                "sg_worr_rewind_canonical_rail_damage_status",
                str(mode["status_cvar"]),
            ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", f":{policy}:256:256:0:0:0:0:0:1:0:0:0:0:0:0:0:0")
            status = GATE.parse_status(line, str(mode["status_cvar"]))
            self.assertEqual(GATE.validate_status(status, mode), status)
            missing_tick = dict(status)
            missing_tick["observed_damage"] = 248
            with self.assertRaisesRegex(RuntimeError, "exact expected damage"):
                GATE.validate_status(missing_tick, mode)
            interrupted = dict(status)
            interrupted["sustained_hold_interrupted"] = 1
            with self.assertRaisesRegex(RuntimeError, "sustained held attack"):
                GATE.validate_status(interrupted, mode)

    def test_water_retrace_modes_require_halved_damage_and_ordered_retrace(self) -> None:
        for mode_name, policy in (("plasma-beam-water-retrace", 7),
                                  ("thunderbolt-water-retrace", 8)):
            mode = GATE.GATE_MODES[mode_name]
            line = PASS_LINE.replace(
                "sg_worr_rewind_canonical_rail_damage_status",
                str(mode["status_cvar"]),
            ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", f":{policy}:4:4:1:1:0:0:0:0:0:0:0:0:0:0:0:0")
            status = GATE.parse_status(line, str(mode["status_cvar"]))
            self.assertEqual(GATE.validate_status(status, mode), status)
            missing_retrace = dict(status)
            missing_retrace["water_retrace_observed"] = 0
            with self.assertRaisesRegex(RuntimeError, "water retrace"):
                GATE.validate_status(missing_retrace, mode)

    def test_thunderbolt_discharge_requires_self_damage_and_ammo_drain(self) -> None:
        mode = GATE.GATE_MODES["thunderbolt-discharge"]
        line = PASS_LINE.replace(
            "sg_worr_rewind_canonical_rail_damage_status",
            str(mode["status_cvar"]),
        ).replace(":5:80:80:0:0:0:0:0:0:0:0:0:0:0:0:0:0", ":8:70:70:0:0:1:1:1:0:0:0:0:0:0:0:0:0")
        status = GATE.parse_status(line, str(mode["status_cvar"]))
        self.assertEqual(GATE.validate_status(status, mode), status)
        no_drain = dict(status)
        no_drain["thunderbolt_discharge_ammo_drained"] = 0
        with self.assertRaisesRegex(RuntimeError, "Thunderbolt discharge"):
            GATE.validate_status(no_drain, mode)
        no_observation = dict(status)
        no_observation["thunderbolt_discharge_observed"] = 0
        with self.assertRaisesRegex(RuntimeError, "Thunderbolt discharge"):
            GATE.validate_status(no_observation, mode)

    def test_parser_rejects_missing_history_or_current_time_selection(self) -> None:
        no_history = PASS_LINE.replace(":6:50000:0", ":5:50000:0")
        with self.assertRaisesRegex(RuntimeError, "pre-fire target history"):
            GATE.validate_status(GATE.parse_status(no_history))
        current_time = PASS_LINE.replace(":6:50000:0", ":6:0:0")
        with self.assertRaisesRegex(RuntimeError, "earlier authoritative instant"):
            GATE.validate_status(GATE.parse_status(current_time))

    def test_determinism_signature_ignores_runtime_clock_samples(self) -> None:
        baseline = GATE.parse_status(PASS_LINE)
        later = dict(baseline)
        later["applied_age_us"] = 64_000
        later["target_history_count"] = 128
        later["observation_applied_time_us"] = 1_024_000
        later["latest_capture_time_us"] = 1_088_000
        later["trace_current_time_us"] = 1_088_000
        later["context_snapshot_time_us"] = 1_088_000
        later["context_mapped_time_us"] = 1_024_000
        later["target_capture_prepares"] = 64
        later["target_capture_callbacks"] = 64
        later["projectile_forward_age_us"] = 64_000
        later["projectile_forward_advanced_age_us"] = 64_000
        later["mover_relative_history_pairs"] = 64
        later["rocket_lifetime_elapsed_ms"] = 9_984
        self.assertEqual(
            GATE.determinism_signature(baseline),
            GATE.determinism_signature(later),
        )
        for name in (
            "splash_occlusion_required",
            "splash_occlusion_policy",
            "splash_radius_evaluated",
            "splash_can_damage_observed",
            "splash_can_damage_result",
            "splash_bsp_blocker_verified",
            "splash_water_boundary_verified",
            "splash_target_undamaged",
            "rocket_lifecycle_required",
            "rocket_lifecycle_policy",
            "rocket_owner_identity_retained",
            "rocket_touch_count",
            "rocket_touch_current_world",
            "rocket_retired",
            "rocket_retired_by_touch",
            "rocket_retired_by_expiry",
            "rocket_post_touch_hold_verified",
            "rocket_no_double_damage",
            "rocket_lifetime_scheduled_ms",
        ):
            changed = dict(baseline)
            changed[name] = int(changed[name]) + 1
            self.assertNotEqual(
                GATE.determinism_signature(baseline),
                GATE.determinism_signature(changed),
                name,
            )

    def test_runtime_launches_use_no_window_and_devnull(self) -> None:
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertIn("_headless_creation_flags", source)
        self.assertIn("terminate_process_tree", source)
        self.assertGreaterEqual(source.count("stdin=subprocess.DEVNULL"), 2)


if __name__ == "__main__":
    unittest.main()
