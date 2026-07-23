#!/usr/bin/env python3
"""Run the headless dedicated-server plus two-client weapon-damage acceptance gate.

The dedicated server only arms a fixture. A hidden, input-disabled shooter
receives the ordinary console ``+attack`` action after connection while a
second hidden, input-disabled client provides independent target history. The
shooter command is still built, sideband-identified, decoded, and consumed by
production code.
The fixture never constructs command authority or calls a weapon function.
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
import re
import socket
import subprocess
import sys
import time
from collections.abc import Sequence
from datetime import datetime, timezone
from pathlib import Path

try:
    from tools.networking.headless_process import (
        creation_flags as _headless_creation_flags,
        start_headless_process,
        terminate_process_tree,
    )
except ModuleNotFoundError:
    from headless_process import (
        creation_flags as _headless_creation_flags,
        start_headless_process,
        terminate_process_tree,
    )


SCHEMA = "worr.networking.canonical-weapon-damage-runtime.v42"
MAP_NAME = "worr_fr10_rewind_mover"
PROTOCOL_VERSION_RERELEASE = 1038
NATIVE_EVENT_PROBE_CLIENT_COMMAND = "cl_worr_native_event_probe_status"
NATIVE_EVENT_PROBE_STATUS_MARKER = "WORR_NATIVE_EVENT_PROBE_STATUS_V1"
NATIVE_EVENT_SENDER_STATUS_MARKER = "WORR_NATIVE_SERVER_EVENT_STATUS_V1"
NATIVE_EVENT_PROBE_PRIVATE_MASK = 0x73
NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED = 424242
NATIVE_EVENT_PROBE_SERVER_IMPAIR_SEED = 817263
NATIVE_EVENT_PROBE_EVENT_RESEND_MS = 100
NATIVE_EVENT_PROBE_FRAME_QUANTUM_MS = 16
NATIVE_EVENT_PROBE_EXPECTED_EVENTS = 5
NATIVE_EVENT_PROBE_EXPECTED_KINDS = (0, 0, 2, 1, 1, 1, 0, 0)
NATIVE_EVENT_PROBE_UNSUPPORTED_KINDS = (0, 1, 6)
NATIVE_EVENT_PROBE_EXPECTED_SCHEMA2_EVENTS = 4
NATIVE_EVENT_PROBE_IMPAIR_COMMON_PROFILE = {
    "jitter": 0,
    "loss": 0.0,
    "burst": 0.0,
    "burst_length": 3,
    "reorder": 0.0,
    "duplicate": 0.0,
    "corrupt": 0.0,
    "upstream_stall": 0,
    "rate_kbps": 0,
    "queue_limit": 1024,
}
# Net impairment schedules each process's outbound path.  Exact visual parity
# requires the first mixed legacy/EVENT datagram to remain ordered, lossless,
# and unthrottled: a retry cannot recreate the transient legacy half after its
# source+1 snapshot deadline.  The client profile delays only the sequenced ACK
# path from the non-input target probe. Its 105 ms latency plus 20 ms
# upstream-only stall gives a deterministic
# 125 ms reverse-path floor, safely beyond the 100 ms EVENT resend by more than
# one 16 ms fixture frame.  This still proves retry and receiver deduplication
# without making presentation parity impossible by construction.
NATIVE_EVENT_PROBE_SERVER_IMPAIR_PROFILE = {
    "latency": 0,
    **NATIVE_EVENT_PROBE_IMPAIR_COMMON_PROFILE,
}
NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE = {
    "latency": 105,
    **NATIVE_EVENT_PROBE_IMPAIR_COMMON_PROFILE,
    "upstream_stall": 20,
}
NATIVE_EVENT_PROBE_IMPAIR_PROFILE_FIELDS = tuple(
    NATIVE_EVENT_PROBE_SERVER_IMPAIR_PROFILE
)
NATIVE_EVENT_PROBE_IMPAIR_COUNTER_FIELDS = (
    "seen",
    "dropped",
    "burst_dropped",
    "reordered",
    "duplicated",
    "corrupted",
    "upstream_stalled",
    "throttled",
    "overflow",
    "resets",
)
NATIVE_EVENT_PROBE_OPENAL_BACKEND_LINE = (
    '[ALSOFT] (II) Initialized backend "null"'
)
NATIVE_EVENT_PROBE_OPENAL_DEVICE = "OpenAL Soft on No Output"
NATIVE_EVENT_PROBE_SCALAR_FIELDS = (
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
)
NATIVE_EVENT_PROBE_STATUS_FIELDS = (
    *NATIVE_EVENT_PROBE_SCALAR_FIELDS,
    *(f"raw_k{index}" for index in range(8)),
    *(f"probe_k{index}" for index in range(8)),
)
NATIVE_EVENT_PROBE_U32_FIELDS = frozenset(
    NATIVE_EVENT_PROBE_SCALAR_FIELDS[:17]
)
NATIVE_EVENT_PROBE_HASH_FIELDS = frozenset(
    name for name in NATIVE_EVENT_PROBE_STATUS_FIELDS
    if name.endswith("_chain_hash")
)
NATIVE_EVENT_SENDER_STATUS_FIELDS = (
    "schema",
    "slot",
    "mode",
    "sender",
    "retired_sender",
    "tx_open",
    "stream_epoch",
    "descriptor_acked",
    "backlog",
    "retained",
    "retired_retained",
    "output_due",
    "confirms",
    "snapshots_queued",
    "queue_failures",
    "candidates_queued",
    "candidates_promoted",
    "descriptor_acks",
    "event_acks",
    "prepared",
    "confirmed",
    "rejected",
    "first_sends",
    "retries",
    "schema2_batches_promoted",
    "schema2_events_promoted",
)
NATIVE_EVENT_SENDER_COUNTER_FIELDS = (
    "confirms",
    "snapshots_queued",
    "queue_failures",
    "candidates_queued",
    "candidates_promoted",
    "descriptor_acks",
    "event_acks",
    "prepared",
    "confirmed",
    "rejected",
    "first_sends",
    "retries",
    "schema2_batches_promoted",
    "schema2_events_promoted",
)
NATIVE_EVENT_PROBE_CHECKPOINT_CLIENT_COMMAND = (
    "cl_worr_native_event_probe_checkpoint"
)
NATIVE_EVENT_PROBE_CHECKPOINT_MARKER = (
    "WORR_NATIVE_EVENT_PROBE_CHECKPOINT_V1"
)
NATIVE_EVENT_PROBE_CHECKPOINT_FIELDS = (
    "valid",
    "schema",
    "size",
    "result",
    "map_generation",
    "authority_epoch",
    "checkpoint_id",
)
NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED = 1
NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED = 2
NATIVE_EVENT_PROBE_CHECKPOINT_BUSY = 7
NATIVE_EVENT_SENDER_U32_FIELDS = frozenset(
    NATIVE_EVENT_SENDER_STATUS_FIELDS[:12]
)
GATE_MODES = {
    "railgun": {
        "arm_command": "worr_rewind_canonical_rail_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_rail_damage_status",
        "weapon_policy": 5,
        "expected_damage": 80,
    },
    # Exercise the ordinary Railgun fixture with a third real client that the
    # runner explicitly keeps in spectator mode.  The production fixture must
    # continue to see exactly the two playing/eligible clients, proving that
    # the connected spectator never enters history or the sealed trace scene.
    "railgun-spectator-exclusion": {
        "arm_command": "worr_rewind_canonical_rail_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_rail_damage_status",
        "weapon_policy": 5,
        "expected_damage": 80,
        "required_client_count": 3,
        "require_spectator_exclusion": True,
        "expected_playing_candidates": 2,
        "expected_eligible_candidates": 2,
    },
    # This required fixture contract is preflighted by the FR-10-T11 parent.
    # Production must expose the arm/status endpoints and prove that a
    # historical Railgun hit is suppressed by current-authority spawn
    # protection without mutating query authority.
    "railgun-spawn-protection": {
        "arm_command": "worr_rewind_canonical_rail_spawn_protection_arm",
        "status_cvar": "sg_worr_rewind_canonical_rail_spawn_protection_status",
        "weapon_policy": 5,
        "expected_damage": 0,
        "require_damage": False,
        "require_no_damage": True,
        "require_historical_hit": True,
        "expected_playing_candidates": 2,
        "expected_eligible_candidates": 2,
        "expected_observation_path": 1,
        "expected_observation_outcome": 1,
        "expected_observation_fallback": 0,
    },
    "railgun-mover-occlusion": {
        "arm_command": "worr_rewind_canonical_rail_mover_occlusion_arm",
        "status_cvar": "sg_worr_rewind_canonical_rail_mover_occlusion_status",
        "weapon_policy": 5,
        "expected_damage": 80,
        # The real Railgun query must terminate on the sealed historical
        # rotating BSP after its live collider has moved out of the lane.
        # Damage behind that occluder is therefore required to remain zero.
        "require_damage": False,
        "require_historical_mover_occlusion": True,
    },
    "machinegun": {
        "arm_command": "worr_rewind_canonical_machinegun_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_machinegun_damage_status",
        "weapon_policy": 1,
        "expected_damage": 8,
    },
    "chaingun": {
        "arm_command": "worr_rewind_canonical_chaingun_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_chaingun_damage_status",
        "weapon_policy": 2,
        "expected_damage": 18,
    },
    "super-shotgun": {
        "arm_command": "worr_rewind_canonical_super_shotgun_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_super_shotgun_damage_status",
        "weapon_policy": 4,
        "expected_damage": 120,
    },
    "disruptor": {
        "arm_command": "worr_rewind_canonical_disruptor_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_disruptor_damage_status",
        "weapon_policy": 6,
        "expected_damage": 45,
        "require_projectile_forward": True,
    },
    "rocket": {
        "arm_command": "worr_rewind_canonical_rocket_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_rocket_damage_status",
        "weapon_policy": 9,
        "expected_damage": 100,
        "require_projectile_forward": True,
        # Rocket impact and splash remain current authority. Unlike Disruptor
        # convergence, this mode deliberately has no historical hit proof.
        "current_authority_projectile": True,
    },
    "rocket-mover-relative": {
        "arm_command": "worr_rewind_canonical_rocket_mover_relative_arm",
        "status_cvar": "sg_worr_rewind_canonical_rocket_mover_relative_status",
        "weapon_policy": 9,
        "expected_damage": 100,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        # The target is a real rider of the packaged rotating BSP mover. Paired
        # normal-frame history must show both moving before they translate
        # together; the real rocket still uses only current-world spawn/contact
        # authority and an unchanged live collision fingerprint.
        "require_mover_relative_projectile": True,
        "expected_mover_relative_policy": 1,
    },
    "rocket-lifecycle-touch": {
        "arm_command": "worr_rewind_canonical_rocket_lifecycle_touch_arm",
        "status_cvar": "sg_worr_rewind_canonical_rocket_lifecycle_touch_status",
        "weapon_policy": 9,
        "expected_damage": 100,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_rocket_lifecycle": True,
        "expected_rocket_lifecycle_policy": 1,
        "expected_rocket_touch_count": 1,
        "expected_rocket_touch_current_world": 1,
        "expected_rocket_retired_by_touch": 1,
        "expected_rocket_retired_by_expiry": 0,
        "expected_rocket_post_touch_hold_verified": 1,
    },
    "rocket-lifetime-expiry": {
        "arm_command": "worr_rewind_canonical_rocket_lifetime_expiry_arm",
        "status_cvar": "sg_worr_rewind_canonical_rocket_lifetime_expiry_status",
        "weapon_policy": 9,
        "expected_damage": 0,
        "require_damage": False,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_rocket_lifecycle": True,
        "expected_rocket_lifecycle_policy": 2,
        "expected_rocket_touch_count": 0,
        "expected_rocket_touch_current_world": 0,
        "expected_rocket_retired_by_touch": 0,
        "expected_rocket_retired_by_expiry": 1,
        "expected_rocket_post_touch_hold_verified": 0,
    },
    "bfg": {
        "arm_command": "worr_rewind_canonical_bfg_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_bfg_damage_status",
        "weapon_policy": 18,
        "expected_damage": 200,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_damage": False,
    },
    "ion-ripper": {
        "arm_command": "worr_rewind_canonical_ion_ripper_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_ion_ripper_damage_status",
        "weapon_policy": 19,
        "expected_damage": 10,
        # The ordinary callback emits fifteen randomized bolts. Every bolt
        # must complete its own bounded current-world spawn sweep; collision,
        # ricochet, damage, and lifetime remain production-owned.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_damage": False,
        "expected_projectile_forward_launches": 15,
    },
    "tesla-mine": {
        "arm_command": "worr_rewind_canonical_tesla_mine_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_tesla_mine_damage_status",
        "weapon_policy": 20,
        "expected_damage": 3,
        # Tesla's ordinary held-release callback creates the bouncing mine.
        # The gate accepts only its clear release-command gravity advance;
        # landing, activation, targeting, effects, damage, and lifetime stay
        # current-world production authority.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_damage": False,
        "release_held_attack_after_seconds": 1.3,
        "release_held_attack_after_attack_received": True,
        "release_held_attack_flush": False,
    },
    "trap": {
        "arm_command": "worr_rewind_canonical_trap_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_trap_damage_status",
        "weapon_policy": 21,
        "expected_damage": 20,
        # Trap's normal held-release callback creates the bouncing deployable.
        # The gate accepts only its clear release-command gravity advance;
        # landing, capture, destruction, and lifetime stay current-world
        # production authority.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_damage": False,
        "release_held_attack_after_seconds": 1.3,
        "release_held_attack_after_attack_received": True,
        "release_held_attack_flush": False,
    },
    "grapple": {
        "arm_command": "worr_rewind_canonical_grapple_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_grapple_damage_status",
        "weapon_policy": 22,
        "expected_damage": 1,
        # The normal Grapple callback creates the hook and performs ordinary
        # muzzle clearance. The gate proves only a later clear current-world
        # hook advance; touch, attachment, pull, damage, and reset stay
        # production authority.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_damage": False,
    },
    "offhand-hook": {
        "arm_command": "worr_rewind_canonical_offhand_hook_arm",
        "status_cvar": "sg_worr_rewind_canonical_offhand_hook_status",
        "weapon_policy": 24,
        "expected_damage": 1,
        # The headless client turns +hook into BUTTON_HOOK. Its active,
        # authenticated command mapping may advance only the just-created hook
        # through a clear current world; touch, attachment, pull, damage,
        # reset, and the legacy hook string remain production authority.
        "input_command": "+hook",
        "enable_offhand_hook": True,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_damage": False,
    },
    "proball-throw": {
        "arm_command": "worr_rewind_canonical_proball_throw_arm",
        "status_cvar": "sg_worr_rewind_canonical_proball_throw_status",
        "weapon_policy": 23,
        "expected_damage": 1,
        # The fixture grants possession before the real Chainfist-held attack.
        # Its later ordinary release command may advance only the new ball
        # through the clear current world; possession, touch, pickup, goals,
        # scoring, teams, and resets remain production authority.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_damage": False,
        "release_held_attack_after_seconds": 1.3,
        "release_held_attack_after_attack_received": True,
        "release_held_attack_flush": False,
        "gametype": 17,
        "team_game": True,
    },
    "grenade-launcher": {
        "arm_command": "worr_rewind_canonical_grenade_launcher_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_grenade_launcher_damage_status",
        "weapon_policy": 15,
        "expected_damage": 60,
        "minimum_damage": 57,
        # The first bounded gravity path is accepted only when every
        # current-world segment is clear. A present-world damageable impact
        # blocker then triggers the normal explosion and off-axis RadiusDamage.
        # Bounce, fuse, and all future deployable behavior stay production-owned.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_current_authority_splash": True,
        "require_reduced_splash": True,
    },
    "hand-grenade": {
        "arm_command": "worr_rewind_canonical_hand_grenade_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_hand_grenade_damage_status",
        "weapon_policy": 16,
        "expected_damage": 60,
        "minimum_damage": 57,
        # The real throw must be caused by a later no-attack release command,
        # never by the earlier prime/hold. The bounded gravity path remains
        # current-world-only; normal touch, bounce, fuse, splash, and damage
        # stay production-owned and are not scripted by this gate.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_damage": False,
        # Ten normal 100 ms prime frames precede the hold frame. Start this
        # margin only after the server has admitted the real prime command;
        # a fixed wall-clock delay can otherwise release before an async
        # headless client has delivered that initial command.
        "release_held_attack_after_seconds": 1.3,
        "release_held_attack_after_attack_received": True,
        # The client emits an immediate normal packet for an attack key-up.
        # No movement edge, physical input path, or mouse capture is used.
        "release_held_attack_flush": False,
    },
    "hand-grenade-splash": {
        "arm_command": "worr_rewind_canonical_hand_grenade_splash_arm",
        "status_cvar": "sg_worr_rewind_canonical_hand_grenade_damage_status",
        "weapon_policy": 16,
        "expected_damage": 60,
        "minimum_damage": 45,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_current_authority_splash": True,
        "require_reduced_splash": True,
        "release_held_attack_after_seconds": 1.3,
        "release_held_attack_after_attack_received": True,
        "release_held_attack_flush": False,
    },
    "prox-launcher": {
        "arm_command": "worr_rewind_canonical_prox_launcher_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_prox_launcher_damage_status",
        "weapon_policy": 17,
        "expected_damage": 90,
        # A proximity mine uses bounded current-world gravity advance only
        # before normal Bounce landing. Its arm/trigger/explosion lifecycle
        # is production authority and deliberately not fabricated here.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_damage": False,
    },
    "prox-launcher-lifecycle": {
        "arm_command": "worr_rewind_canonical_prox_launcher_lifecycle_arm",
        "status_cvar": "sg_worr_rewind_canonical_prox_launcher_damage_status",
        "weapon_policy": 17,
        "expected_damage": 61,
        # The mine may advance only through its initial clear current-world
        # gravity path. Normal land/arm/trigger/explosion/RadiusDamage then
        # must complete against the fixture's staged live target.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_prox_lifecycle": True,
    },
    "rocket-splash": {
        "arm_command": "worr_rewind_canonical_rocket_splash_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_rocket_splash_damage_status",
        "weapon_policy": 9,
        "expected_damage": 58,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_current_authority_splash": True,
        "require_reduced_splash": True,
        "require_splash_occlusion": True,
        "expected_splash_occlusion_policy": 1,
        "expected_splash_can_damage": 1,
        "expected_splash_bsp_blocker": 0,
        "expected_splash_water_boundary": 0,
        "expected_splash_target_undamaged": 0,
    },
    "rocket-splash-bsp-occlusion": {
        "arm_command":
            "worr_rewind_canonical_rocket_splash_bsp_occlusion_arm",
        "status_cvar": "sg_worr_rewind_canonical_rocket_splash_damage_status",
        "weapon_policy": 9,
        "expected_damage": 0,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_current_authority_splash": True,
        "require_damage": False,
        "require_splash_occlusion": True,
        "expected_splash_occlusion_policy": 2,
        "expected_splash_can_damage": 0,
        "expected_splash_bsp_blocker": 1,
        "expected_splash_water_boundary": 0,
        "expected_splash_target_undamaged": 1,
    },
    "rocket-splash-water-boundary": {
        "arm_command":
            "worr_rewind_canonical_rocket_splash_water_boundary_arm",
        "status_cvar": "sg_worr_rewind_canonical_rocket_splash_damage_status",
        "weapon_policy": 9,
        "expected_damage": 58,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_current_authority_splash": True,
        "require_reduced_splash": True,
        "require_splash_occlusion": True,
        "expected_splash_occlusion_policy": 3,
        "expected_splash_can_damage": 1,
        "expected_splash_bsp_blocker": 0,
        "expected_splash_water_boundary": 1,
        "expected_splash_target_undamaged": 0,
    },
    "plasma-gun": {
        "arm_command": "worr_rewind_canonical_plasma_gun_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_plasma_gun_damage_status",
        "weapon_policy": 10,
        "expected_damage": 20,
        # Plasma Gun's direct and small-radius paths remain current authority.
        # This mode proves the normal direct hit only; radius coverage stays
        # independently scoped.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
    },
    "plasma-gun-splash": {
        "arm_command": "worr_rewind_canonical_plasma_gun_splash_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_plasma_gun_splash_damage_status",
        "weapon_policy": 10,
        "expected_damage": 7,
        # The real Plasma Gun must complete normal current-world flight to the
        # small fixture blocker, then let RadiusDamage reach the off-axis
        # target; no historical impact can satisfy this mode.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_current_authority_splash": True,
        "require_reduced_splash": True,
    },
    "blaster": {
        "arm_command": "worr_rewind_canonical_blaster_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_blaster_damage_status",
        "weapon_policy": 11,
        "expected_damage": 15,
        # The shared Blaster/HyperBlaster bolt path keeps direct/radius
        # authority in the current world; this direct-hit seam does not claim
        # the optional Q3 HyperBlaster radius branch.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
    },
    "blaster-legacy-capability-status": {
        "arm_command": "worr_rewind_canonical_blaster_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_blaster_damage_status",
        "weapon_policy": 11,
        "expected_damage": 15,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        # Keep every optional native endpoint disabled and capture the public
        # capability tuple directly from both real clients and both server
        # peers.  This is the live exact-0x03 row used by FR-10-T04.
        "require_legacy_capability_status": True,
    },
    "blaster-local-action-lease": {
        "arm_command": "worr_rewind_canonical_blaster_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_blaster_damage_status",
        "weapon_policy": 11,
        "expected_damage": 15,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        # The same ordinary Blaster input must also cross the observation-only
        # post-command lease and produce an exact scoped->leased join.
        "require_local_action_lease": True,
        "local_action_catalog_id": 1,
        "local_action_v2_blockers": 4367,
        # Opt both endpoints into the private native-event carrier, publish
        # the descriptor-complete authority receipt, and prove a fresh
        # connection epoch by reconnecting the same hidden shooter process
        # before the real attack command.
        "require_local_action_authority_receipt": True,
        "require_in_session_reconnect": True,
        # Capture stable scalar status from both live clients and both server
        # peers after reconnect and exact receipt parity.  This is direct
        # numeric evidence for the event-only 0x73 capability bundle; fixed
        # launch configuration alone is not accepted as mask proof.
        "require_native_event_status": True,
        # Reconnect/asset hitches can leave the first console key-down in a
        # command that arrives before the restored Blaster is ready.  Repeat
        # an ordinary client-side release/press edge for a bounded interval;
        # the fixture still requires the production weapon callback and exact
        # command-scoped receipt.
        "refresh_held_attack": True,
        "refresh_held_attack_until_seconds": 5.0,
    },
    "blaster-local-action-lease-combined": {
        "arm_command": "worr_rewind_canonical_blaster_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_blaster_damage_status",
        "weapon_policy": 11,
        "expected_damage": 15,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_local_action_lease": True,
        "local_action_catalog_id": 1,
        "local_action_v2_blockers": 4367,
        "require_local_action_authority_receipt": True,
        "require_in_session_reconnect": True,
        "refresh_held_attack": True,
        "refresh_held_attack_until_seconds": 5.0,
        # Negotiate private 0x77 after reconnect, prove the event lane with the
        # exact cgame authority receipt above, and independently require the
        # canonical snapshot lane to receive semantic ACK/release traffic.
        "require_combined_native_shadow": True,
    },
    "native-event-probe-map-reuse": {
        "arm_command": "worr_rewind_canonical_blaster_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_blaster_damage_status",
        "weapon_policy": 11,
        "expected_damage": 15,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        # Exercise the event carrier without enabling input-batch/local-action
        # receipts.  One hidden renderer cadence keeps cgame presentation and
        # OpenAL Soft's proven null sink alive while device input stays disabled.
        "require_native_event_probe_map_reuse": True,
        "require_native_event_status": True,
        # Submit one ordinary client-side press/release pulse on the clean,
        # unimpaired shooter path with the matching key token that a real
        # binding supplies. A tokenless console release
        # is the input subsystem's emergency unstick form and intentionally
        # clears the pending key-down impulse; the matched form retains exactly
        # one BUTTON_ATTACK command while leaving no held state. The separate
        # target probe carries the deliberately delayed outbound ACK path.
        "input_command": "+attack 255; -attack 255",
        # The exact impact emits two visual records, one spatial-audio record,
        # and one damage record on one immutable snapshot fence.  Those four
        # records must be admitted as one schema-2 ACK/retry unit.  The launch
        # muzzle record keeps its earlier, distinct snapshot identity and
        # therefore remains an independent schema-1 unit.
        "require_schema2_event_batch": True,
        "minimum_schema2_event_batch_events": (
            NATIVE_EVENT_PROBE_EXPECTED_SCHEMA2_EVENTS
        ),
        "require_schema2_mixed_singletons": True,
        "native_event_private_mask": NATIVE_EVENT_PROBE_PRIVATE_MASK,
    },
    "blaster-native-snapshot-presentation": {
        "arm_command": "worr_rewind_canonical_blaster_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_blaster_damage_status",
        "weapon_policy": 11,
        "expected_damage": 15,
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        # Exercise the exact private 0x57 snapshot adapter without the already
        # proven event/local-action workload. Only the independent target runs
        # the hidden presentation cadence, and its cgame must prove actual
        # source-gated native transform authority.
        "require_native_snapshot_shadow": True,
        "require_native_snapshot_presentation": True,
    },
    "hyperblaster": {
        "arm_command": "worr_rewind_canonical_hyperblaster_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_hyperblaster_damage_status",
        "weapon_policy": 11,
        "expected_damage": 15,
        # The production repeating 6–11 gun-frame cadence must receive a
        # later ordinary held command before its first shared bolt callback.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "refresh_held_attack": True,
    },
    "chainfist": {
        "arm_command": "worr_rewind_canonical_chainfist_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_chainfist_damage_status",
        "weapon_policy": 12,
        "expected_damage": 15,
        # The only historical fact is player reach/FOV eligibility. Live
        # displacement, CanDamage, and Damage retain final authority.
        "require_hybrid_melee": True,
    },
    "etf-rifle": {
        "arm_command": "worr_rewind_canonical_etf_rifle_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_etf_rifle_damage_status",
        "weapon_policy": 13,
        "expected_damage": 10,
        # Flechette contact and damage remain entirely current-world.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        # ETF's production callback requires a subsequent real held-command
        # edge after Weapon_Repeating has entered its firing state.
        "refresh_held_attack": True,
    },
    "phalanx": {
        "arm_command": "worr_rewind_canonical_phalanx_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_phalanx_damage_status",
        "weapon_policy": 14,
        "expected_damage": 80,
        # Direct contact and RadiusDamage remain current-world authority after
        # a bounded authenticated spawn advance.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        # Weapon_Generic enters Phalanx's first fire frame from the received
        # attack and reaches its normal 7/8 barrel frames on later ordinary
        # held-command edges.
        "refresh_held_attack": True,
    },
    "phalanx-splash": {
        "arm_command": "worr_rewind_canonical_phalanx_splash_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_phalanx_splash_damage_status",
        "weapon_policy": 14,
        "expected_damage": 93,
        # The shell must strike the present-world fixture blocker. Normal
        # phalanx_touch/RadiusDamage owns the off-axis target splash result.
        "require_projectile_forward": True,
        "current_authority_projectile": True,
        "require_current_authority_splash": True,
        "require_reduced_splash": True,
        # Weapon_Generic reaches the barrel callback on a later real held
        # command edge; the runner remains headless and input-free.
        "refresh_held_attack": True,
    },
    "plasma-beam": {
        "arm_command": "worr_rewind_canonical_plasma_beam_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_plasma_beam_damage_status",
        "weapon_policy": 7,
        "expected_damage": 8,
    },
    "plasma-beam-held": {
        "arm_command": "worr_rewind_canonical_plasma_beam_held_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_plasma_beam_held_damage_status",
        "weapon_policy": 7,
        "expected_damage": 24,
        "refresh_held_attack": True,
    },
    "plasma-beam-sustained": {
        "arm_command": "worr_rewind_canonical_plasma_beam_sustained_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_plasma_beam_sustained_damage_status",
        "weapon_policy": 7,
        "expected_damage": 256,
        "require_sustained_hold": True,
    },
    "plasma-beam-release": {
        "arm_command": "worr_rewind_canonical_plasma_beam_release_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_plasma_beam_release_damage_status",
        "weapon_policy": 7,
        "expected_damage": 24,
        "refresh_held_attack": True,
        "release_after_expected_damage": True,
    },
    "plasma-beam-water-retrace": {
        "arm_command": "worr_rewind_canonical_plasma_beam_water_retrace_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_plasma_beam_water_retrace_damage_status",
        "weapon_policy": 7,
        "expected_damage": 4,
        "require_water_retrace": True,
    },
    "thunderbolt": {
        "arm_command": "worr_rewind_canonical_thunderbolt_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_thunderbolt_damage_status",
        "weapon_policy": 8,
        "expected_damage": 8,
    },
    "thunderbolt-held": {
        "arm_command": "worr_rewind_canonical_thunderbolt_held_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_thunderbolt_held_damage_status",
        "weapon_policy": 8,
        "expected_damage": 24,
        "refresh_held_attack": True,
    },
    "thunderbolt-sustained": {
        "arm_command": "worr_rewind_canonical_thunderbolt_sustained_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_thunderbolt_sustained_damage_status",
        "weapon_policy": 8,
        "expected_damage": 256,
        "require_sustained_hold": True,
    },
    "thunderbolt-release": {
        "arm_command": "worr_rewind_canonical_thunderbolt_release_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_thunderbolt_release_damage_status",
        "weapon_policy": 8,
        "expected_damage": 24,
        "refresh_held_attack": True,
        "release_after_expected_damage": True,
    },
    "thunderbolt-water-retrace": {
        "arm_command": "worr_rewind_canonical_thunderbolt_water_retrace_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_thunderbolt_water_retrace_damage_status",
        "weapon_policy": 8,
        "expected_damage": 4,
        "require_water_retrace": True,
    },
    "thunderbolt-discharge": {
        "arm_command": "worr_rewind_canonical_thunderbolt_discharge_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_thunderbolt_discharge_damage_status",
        "weapon_policy": 8,
        "expected_damage": 70,
        "require_thunderbolt_discharge": True,
        "current_authority_discharge": True,
    },
    "shotgun": {
        "arm_command": "worr_rewind_canonical_shotgun_damage_arm",
        "status_cvar": "sg_worr_rewind_canonical_shotgun_damage_status",
        "weapon_policy": 3,
        "expected_damage": 48,
    },
}
STATUS_CVAR = GATE_MODES["railgun"]["status_cvar"]
STATUS_FIELDS = (
    "status",
    "armed",
    "players_ready",
    "history_ready",
    "canonical_scope",
    "attack_received",
    "weapon_callback",
    "canonical_historical_hit",
    "damage_applied",
    "current_geometry_unchanged",
    "target_history_captures",
    "applied_age_us",
    "failure_code",
    "eligible_candidates",
    "playing_candidates",
    "observation_path",
    "observation_outcome",
    "observation_fallback",
    "observation_flags",
    "observation_query",
    "observation_snapshot_epoch",
    "history_epoch",
    "target_history_count",
    "observation_applied_time_us",
    "latest_capture_time_us",
    "trace_current_time_us",
    "context_snapshot_time_us",
    "context_mapped_time_us",
    "target_capture_prepares",
    "capture_append_rejections",
    "target_capture_callbacks",
    "observation_weapon_policy",
    "expected_damage",
    "observed_damage",
    "water_retrace_required",
    "water_retrace_observed",
    "thunderbolt_discharge_required",
    "thunderbolt_discharge_ammo_drained",
    "thunderbolt_discharge_observed",
    "sustained_hold_required",
    "sustained_hold_interrupted",
    "projectile_forward_required",
    "projectile_forward_authenticated",
    "projectile_forward_advanced",
    "projectile_forward_clamped",
    "projectile_forward_blocked",
    "projectile_forward_age_us",
    "projectile_forward_advanced_age_us",
    "projectile_forward_launches",
    "projectile_forward_expected_launches",
    "melee_selection_required",
    "melee_selection_authenticated",
    "melee_historical_eligible",
    "melee_current_displacement_accepted",
    "melee_current_displacement_units",
    "prox_lifecycle_required",
    "prox_mine_landed",
    "prox_mine_triggered",
    "prox_mine_exploded",
    "historical_mover_occlusion_required",
    "historical_mover_relocated",
    "historical_mover_baseline_clear",
    "historical_mover_occlusion_observed",
    "historical_mover_target_undamaged",
    "historical_mover_history_count",
    "mover_relative_projectile_required",
    "mover_relative_policy",
    "mover_relative_target_history_moved",
    "mover_relative_mover_history_moved",
    "mover_relative_pair_preserved",
    "mover_relative_current_world_impact",
    "mover_relative_authority_unchanged",
    "mover_relative_history_pairs",
    "local_action_catalog_ready",
    "local_action_lease_ready",
    "local_action_lease_offers",
    "local_action_lease_supersedes",
    "local_action_lease_duplicates",
    "local_action_lease_rebases",
    "local_action_lease_claims",
    "local_action_lease_expired",
    "local_action_lease_rejected",
    "local_action_command_epoch",
    "local_action_command_sequence",
    "local_action_scoped_record",
    "local_action_leased_record",
    "local_action_continuity_exact",
    "local_action_joined_record",
    "local_action_shadow_ready",
    "local_action_shadow_catalog_id",
    "local_action_shadow_flags",
    "local_action_shadow_v2_blockers",
    "local_action_shadow_record_hash",
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
    "rocket_lifetime_elapsed_ms",
)
STATUS_RE = re.compile(
    rf'{re.escape(STATUS_CVAR)}\s+"(?P<value>(?:pending|pass|fail):[0-9:]+)"'
)
CLIENT_STATUS_RE = re.compile(
    r"^\s*(?P<user_id>\d+)\s+[-\d]+\s+[-\d]+\s+(?P<name>\S+)",
    re.MULTILINE,
)
LOCAL_ACTION_PARITY_RE = re.compile(
    r"WORR local-action authority parity "
    r"matches=(?P<matches>\d+) receipts=(?P<receipts>\d+) "
    r"unmatched=(?P<unmatched>\d+) outstanding=(?P<outstanding>\d+) "
    r"mismatches=(?P<mismatches>\d+) conflicts=(?P<conflicts>\d+) "
    r"passes=(?P<passes>\d+) commands=(?P<commands>\d+) "
    r"lookups=(?P<lookups>\d+) hits=(?P<hits>\d+) "
    r"misses=(?P<misses>\d+) "
    r"resync=(?P<resync>\d+)"
)
NATIVE_CLIENT_STATUS_MARKER = "WORR_NATIVE_CLIENT_STATUS_V1"
NATIVE_SERVER_STATUS_MARKER = "WORR_NATIVE_SERVER_STATUS_V1"
NATIVE_SERVER_LIFECYCLE_ACTIVE = 2
NATIVE_READINESS_PHASE_FAILED = 6
CAPABILITY_CLIENT_STATUS_MARKER = "WORR_CAPABILITY_CLIENT_STATUS_V1"
CAPABILITY_SERVER_STATUS_MARKER = "WORR_CAPABILITY_SERVER_STATUS_V1"
NATIVE_SERVER_SNAPSHOT_STATUS_MARKER = (
    "WORR_NATIVE_SERVER_SNAPSHOT_STATUS_V1"
)
SNAPSHOT_EMISSION_STATUS_MARKER = "WORR_SNAPSHOT_EMISSION_STATUS_V1"
CANONICAL_RENDER_STATUS_RE = re.compile(
    r"cg_snapshot_timeline_render: epoch=(?P<epoch>\d+) mode=(?P<mode>\d+) "
    r"clock=(?P<clock_frames>\d+)/(?P<clock_failures>\d+) "
    r"pair=(?P<pair_frames>\d+)/(?P<pair_failures>\d+) "
    r"align_fail=(?P<alignment_failures>\d+) "
    r"pair_mode=(?P<pair_mode>\d+) pair_blocks=0x(?P<pair_blocks>[0-9a-fA-F]+) "
    r"samples=(?P<sample_attempts>\d+) fail=(?P<sample_failures>\d+) "
    r"invisible=(?P<sample_invisible>\d+) "
    r"discontinuity=(?P<sample_discontinuities>\d+) "
    r"parity=(?P<parity_matches>\d+)/(?P<parity_mismatches>\d+) "
    r"native=(?P<native_authority_samples>\d+)/"
    r"(?P<native_authority_blocks>\d+) "
    r"promoted=(?P<promoted_transforms>\d+) "
    r"events=(?P<event_ready_records>\d+)/(?P<event_future_frames>\d+)/"
    r"(?P<event_audit_failures>\d+) "
    r"max_error=(?P<max_origin_error>[0-9]+(?:\.[0-9]+)?)/"
    r"(?P<max_old_origin_error>[0-9]+(?:\.[0-9]+)?)/"
    r"(?P<max_angle_error>[0-9]+(?:\.[0-9]+)?) "
    r"timeline_modes=(?P<interpolation_frames>\d+)/"
    r"(?P<extrapolation_frames>\d+)/(?P<clamped_frames>\d+) "
    r"extrap_us=(?P<extrapolation_time_us>\d+) "
    r"enumeration=(?P<enumeration_frames>\d+)/"
    r"(?P<enumeration_failures>\d+)/"
    r"(?P<enumerated_entities>\d+)/"
    r"(?P<enumerated_removed_entities>\d+) "
    r"resets=(?P<enumeration_generation_resets>\d+) "
    r"previous_only=(?P<previous_only_observed>\d+)/"
    r"(?P<previous_only_selected>\d+)/"
    r"(?P<previous_only_submitted>\d+) "
    r"view=(?P<native_view_result>\d+)/"
    r"(?P<native_view_render_count>\d+)/"
    r"(?P<native_view_record_count>\d+) "
    r"submission=(?P<renderer_submission_calls>\d+)/"
    r"(?P<renderer_submitted_sources>\d+)/"
    r"(?P<renderer_submission_hash>[0-9a-fA-F]{16})"
)
CANONICAL_ADAPTIVE_STATUS_RE = re.compile(
    r"cg_snapshot_timeline_adaptive: enabled=(?P<enabled>\d+) "
    r"adjustment=(?P<adjustment>\d+) "
    r"delay_us=(?P<delay_us>\d+)/(?P<baseline_delay_us>\d+)/"
    r"(?P<maximum_delay_us>\d+) "
    r"arrival_us=(?P<cadence_us>\d+)/(?P<jitter_us>\d+)/"
    r"(?P<last_jitter_us>\d+) "
    r"counts=(?P<rise_adjustments>\d+)/"
    r"(?P<recovery_adjustments>\d+)/"
    r"(?P<pressure_observations>\d+)/(?P<reset_count>\d+) "
    r"failures=(?P<failures>\d+)"
)
RCON_PASSWORD = "canonical_rail_runtime"
SHOOTER_NAME = "rail_shooter"
TARGET_NAME = "rail_target"
SPECTATOR_NAME = "rail_spectator"
CLIENT_QPORTS = {
    # q2repro protocol 1038 carries the low byte on the wire. Keep these
    # explicit, non-zero, and distinct without relying on truncation.
    SHOOTER_NAME: "101",
    TARGET_NAME: "102",
    SPECTATOR_NAME: "103",
}
SPECTATOR_JOIN_MARKER = "You are now spectating."
# LocClient_Print records the stable localization key in headless console logs;
# localized display text is not a reliable automation latch.
SPECTATOR_TEAM_QUERY_MARKER = "g_you_are_on_team"
CLIENT_ERR_DROP_RE = re.compile(
    r"^\s*ERROR:\s*(?P<message>[^\r\n]+?)\s*$", re.MULTILINE,
)
NATIVE_APPLICATION_REJECTED_MARKER = "native application rejected:"
STRICT_DECIMAL_RE = re.compile(r"(?:0|[1-9][0-9]*)\Z")
STRICT_CHAIN_HASH_RE = re.compile(r"[0-9a-f]{16}\Z")
IMPAIR_CONFIG_RE = re.compile(
    r"net_impair: enabled=(?P<enabled>\d+) seed=(?P<seed>-?\d+) "
    r"latency=(?P<latency>\d+) jitter=(?P<jitter>\d+) "
    r"loss=(?P<loss>\d+(?:\.\d+)?) "
    r"burst=(?P<burst>\d+(?:\.\d+)?)/(?P<burst_length>\d+) "
    r"reorder=(?P<reorder>\d+(?:\.\d+)?) "
    r"duplicate=(?P<duplicate>\d+(?:\.\d+)?) "
    r"corrupt=(?P<corrupt>\d+(?:\.\d+)?) "
    r"upstream_stall=(?P<upstream_stall>\d+) "
    r"rate_kbps=(?P<rate_kbps>\d+) "
    r"queue=(?P<queue_current>\d+)/(?P<queue_limit>\d+) "
    r"high_water=(?P<high_water>\d+)"
)
IMPAIR_COUNTERS_RE = re.compile(
    r"net_impair counters: seen=(?P<seen>\d+) "
    r"dropped=(?P<dropped>\d+) "
    r"burst_dropped=(?P<burst_dropped>\d+) "
    r"reordered=(?P<reordered>\d+) "
    r"duplicated=(?P<duplicated>\d+) "
    r"corrupted=(?P<corrupted>\d+) "
    r"upstream_stalled=(?P<upstream_stalled>\d+) "
    r"throttled=(?P<throttled>\d+) "
    r"overflow=(?P<overflow>\d+) resets=(?P<resets>\d+)"
)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json_atomic(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def creation_flags() -> int:
    return _headless_creation_flags()


def append_native_event_probe_impairment(
    command: list[str], seed: int, profile: dict[str, int | float], *,
    enabled: bool = True,
) -> None:
    """Append one endpoint of the frozen T07 delayed-ACK retry profile."""
    command.extend([
        "+set", "net_impair_enable", "1" if enabled else "0",
        "+set", "net_impair_seed", str(seed),
        "+set", "net_impair_latency_ms", str(profile["latency"]),
        "+set", "net_impair_jitter_ms", str(profile["jitter"]),
        "+set", "net_impair_loss_pct", f'{profile["loss"]:g}',
        "+set", "net_impair_burst_loss_pct", f'{profile["burst"]:g}',
        "+set", "net_impair_burst_length", str(profile["burst_length"]),
        "+set", "net_impair_reorder_pct", f'{profile["reorder"]:g}',
        "+set", "net_impair_duplicate_pct", f'{profile["duplicate"]:g}',
        "+set", "net_impair_corrupt_pct", f'{profile["corrupt"]:g}',
        "+set", "net_impair_upstream_stall_ms", str(profile["upstream_stall"]),
        "+set", "net_impair_rate_kbps", str(profile["rate_kbps"]),
    ])


def build_server_command(
    dedicated_exe: Path, port: int, runtime_home: Path | None = None, lag_debug: int = 2,
    game_type: int = 1, enable_offhand_hook: bool = False,
    enable_local_action_authority_receipt: bool = False,
    enable_reconnect_minplayer_bypass: bool = False,
    enable_combined_snapshot_shadow: bool = False,
    enable_native_snapshot_shadow: bool = False,
    enable_native_snapshot_presentation: bool = False,
    enable_native_event_shadow: bool = False,
    enable_native_event_probe_impairment: bool = False,
    defer_native_event_probe_impairment: bool = False,
    disable_player_inactivity: bool = False,
    max_clients: int = 2,
) -> list[str]:
    """Build a dedicated-only fixture host; it never launches a renderer."""
    command = [
        str(dedicated_exe),
        "+set", "game", "basew",
        "+set", "developer", "1",
        "+set", "net_enable_ipv6", "0",
        "+set", "net_ip", "127.0.0.1",
        "+set", "net_port", str(port),
        # The harness issues commands over a temporary UDP rcon socket only
        # after the client is admitted. This keeps both launched processes
        # stdin-free and avoids frame-count races during asset negotiation.
        "+set", "rcon_password", RCON_PASSWORD,
        "+set", "deathmatch", "1",
        "+set", "g_gametype", str(game_type),
        "+set", "maxclients", str(max_clients),
        "+set", "g_owner_auto_join", "1",
        "+set", "match_auto_join", "1",
        "+set", "match_force_join", "1",
        # The weapon callback must run in an active FFA match.  This changes
        # only the test server's match lifecycle; it does not bypass combat
        # policy or damage checks inside the fixture.
        "+set", "warmup_enabled", "0",
        "+set", "g_warmup_countdown", "0",
        "+set", "match_start_no_humans", "1",
        "+set", "g_lag_compensation", "1",
        # A current-world projectile policy may advance only the bounded age
        # of a server-authenticated command mapping; it never rewinds contact.
        "+set", "sg_lag_compensation_projectile_forward_ms", "100",
        "+set", "sg_lag_compensation_melee_max_displacement", "64",
        # Match the deterministic upstream impairment below. This is a real
        # server policy cvar, so the canonical command resolves against a
        # retained earlier authoritative target pose rather than "now".
        "+set", "sg_lag_compensation_interp_ms", "50",
        "+set", "sg_lag_compensation_debug", str(lag_debug),
        "+set", "sv_fps", "62",
        "+map", MAP_NAME,
    ]
    if enable_offhand_hook:
        map_index = command.index("+map")
        command[map_index:map_index] = [
            "+set", "g_allow_grapple", "1",
            "+set", "g_grapple_offhand", "1",
        ]
    if (enable_local_action_authority_receipt or enable_native_event_shadow or
            enable_native_snapshot_shadow):
        map_index = command.index("+map")
        native_cvars = [
            "+set", "sv_worr_native_shadow", "1",
        ]
        if enable_local_action_authority_receipt or enable_native_event_shadow:
            native_cvars.extend([
                "+set", "sv_worr_native_event_shadow", "1",
            ])
        if enable_local_action_authority_receipt:
            native_cvars.extend([
                "+set", "sg_local_action_shadow_receipts", "1",
            ])
        if enable_combined_snapshot_shadow or enable_native_snapshot_shadow:
            native_cvars.extend([
                "+set", "sv_worr_native_snapshot_shadow", "1",
            ])
        if enable_native_snapshot_presentation:
            # The minimal rewind BSP deliberately has no useful cross-player
            # PVS. Force both real player entities into this fixture's legacy
            # and native snapshots so presentation authority has a remote,
            # non-predicted transform to promote.
            native_cvars.extend(["+set", "sv_novis", "1"])
        command[map_index:map_index] = native_cvars
    if enable_native_event_probe_impairment:
        map_index = command.index("+map")
        impairment: list[str] = []
        append_native_event_probe_impairment(
            impairment, NATIVE_EVENT_PROBE_SERVER_IMPAIR_SEED,
            NATIVE_EVENT_PROBE_SERVER_IMPAIR_PROFILE,
            enabled=not defer_native_event_probe_impairment,
        )
        command[map_index:map_index] = impairment
    if disable_player_inactivity:
        # This input-free probe may deliberately poll a failing presentation
        # path for the full gate timeout. Prevent the unrelated default
        # 110-second inactivity warning and 120-second spectator transition
        # from adding world/fish audio to its checkpointed EVENT window.
        map_index = command.index("+map")
        command[map_index:map_index] = ["+set", "g_inactivity", "0"]
    if enable_reconnect_minplayer_bypass:
        map_index = command.index("+map")
        command[map_index:map_index] = ["+set", "cheats", "1"]
    if runtime_home is not None:
        command[1:1] = ["+set", "fs_homepath", str(runtime_home)]
    return command


def build_client_command(
    client_exe: Path, port: int, name: str, runtime_home: Path | None = None,
    enable_local_action_authority_receipt: bool = False,
    enable_combined_snapshot_shadow: bool = False,
    enable_native_snapshot_shadow: bool = False,
    enable_native_snapshot_presentation: bool = False,
    enable_network_impairment: bool = True,
    enable_native_event_shadow: bool = False,
    enable_native_event_probe: bool = False,
    enable_native_event_probe_impairment: bool = False,
    defer_native_event_probe_impairment: bool = False,
) -> list[str]:
    """Build the hidden client. Device input is disabled before initialization."""
    try:
        qport = CLIENT_QPORTS[name]
    except KeyError as exc:
        raise ValueError(f"unsupported canonical fixture client name: {name}") from exc

    command = [
        str(client_exe),
        "+set", "game", "basew",
        "+set", "developer", "1",
        "+set", "loc_language", "english",
        "+set", "win_headless", "1",
        "+set", "cl_headless", "1",
        "+set", "in_enable", "0",
        "+set", "in_grab", "0",
        "+set", "s_enable", "0",
        "+set", "r_renderer", "opengl",
        "+set", "r_fullscreen", "0",
        "+set", "r_geometry", "640x480+0+0",
        # Keep user-command production on the independent physics cadence.
        # Headless automation has no visible render loop to act as an input
        # clock, while ordinary held keyboard state remains device-independent.
        "+set", "cl_async", "1",
        "+set", "cl_maxfps", "62",
        "+set", "r_maxfps", "62",
        "+set", "net_enable_ipv6", "0",
        "+set", "net_clientport", "-1",
        # Quake's protocol qport is independent of the UDP source port. Give
        # each concurrently launched fixture client a stable identity so two
        # processes started in the same millisecond cannot collide.
        "+set", "qport", qport,
        "+set", "cl_protocol", "1038",
        "+set", "name", name,
    ]
    if runtime_home is not None:
        command[1:1] = ["+set", "fs_homepath", str(runtime_home)]
    if (enable_local_action_authority_receipt or enable_native_event_shadow or
            enable_native_snapshot_shadow):
        native_cvars = [
            "+set", "cl_worr_native_shadow", "1",
        ]
        if enable_local_action_authority_receipt or enable_native_event_shadow:
            native_cvars.extend([
                "+set", "cl_worr_native_event_shadow", "1",
            ])
        if enable_combined_snapshot_shadow or enable_native_snapshot_shadow:
            native_cvars.extend([
                "+set", "cl_worr_native_snapshot_shadow", "1",
            ])
        command.extend(native_cvars)
    if enable_native_event_probe:
        command.extend([
            # win_headless retains the hidden renderer while cl_headless=0
            # supplies the ordinary cgame presentation cadence. Input remains
            # disabled. OpenAL Soft's explicit null backend is selected and
            # proven from its own per-run logfile by run_once.
            "+set", "cl_headless", "0",
            "+set", "cg_native_event_preflight_probe", "1",
            "+set", "s_enable", "2",
            "+set", "s_volume", "0",
        ])
    if enable_native_snapshot_presentation:
        command.extend([
            # win_headless retains a valid hidden renderer surface and also
            # hard-disables input initialization/cursor capture. cl_headless=0
            # deliberately restores only the presentation cadence.
            "+set", "cl_headless", "0",
            "+set", "cg_snapshot_timeline_render", "3",
            "+set", "cg_snapshot_timeline_render_epsilon", "0.125",
            "+set", "cg_snapshot_timeline_interpolation_delay_ms", "50",
            "+set", "cg_snapshot_timeline_adaptive_interpolation", "1",
            "+set", "cg_snapshot_timeline_max_interpolation_delay_ms", "150",
            "+set", "cg_snapshot_timeline_max_extrapolation_ms", "50",
        ])
    if (name == SHOOTER_NAME and enable_network_impairment and
            not enable_native_event_probe_impairment):
        # Produce a real, deterministic earlier server selection without
        # loss, reordering, or synthetic command timestamps. Only the
        # shooter's upstream packets are delayed; the target remains an
        # independent ordinary client.
        command.extend([
            "+set", "net_impair_enable", "1",
            "+set", "net_impair_seed", "481516",
            "+set", "net_impair_latency_ms", "50",
            "+set", "net_impair_jitter_ms", "0",
            "+set", "net_impair_loss_pct", "0",
            "+set", "net_impair_burst_loss_pct", "0",
            "+set", "net_impair_reorder_pct", "0",
            "+set", "net_impair_duplicate_pct", "0",
            "+set", "net_impair_upstream_stall_ms", "0",
        ])
    if enable_native_event_probe_impairment:
        append_native_event_probe_impairment(
            command, NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED,
            NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE,
            enabled=not defer_native_event_probe_impairment,
        )
    # Config files can legally replace a userinfo value during startup. Repeat
    # the test-only identity after queuing the connection so the live UDP
    # session reports the named, independently selected fixture clients.
    command.extend((
        "+connect", f"127.0.0.1:{port}",
        "+set", "name", name,
    ))
    return command


def parse_status(text: str, status_cvar: str = STATUS_CVAR) -> dict[str, int | str]:
    status_re = re.compile(
        rf'{re.escape(status_cvar)}\s+"(?P<value>(?:pending|pass|fail):[0-9:]+)"'
    )
    matches = list(status_re.finditer(text))
    if len(matches) != 1:
        raise RuntimeError(f"expected one canonical rail status row; observed={len(matches)}")
    values = matches[0].group("value").split(":")
    if len(values) != len(STATUS_FIELDS):
        raise RuntimeError("canonical rail status field count changed")
    status: dict[str, int | str] = {"status": values[0]}
    for name, value in zip(STATUS_FIELDS[1:], values[1:], strict=True):
        if not value.isdecimal():
            raise RuntimeError(f"canonical rail status {name} is not decimal")
        status[name] = int(value)
    return status


def validate_status(
    status: dict[str, int | str], mode: dict[str, int | str] = GATE_MODES["railgun"],
) -> dict[str, int | str]:
    if status["status"] != "pass":
        splash = "/".join(str(status[name]) for name in (
            "splash_occlusion_required",
            "splash_occlusion_policy",
            "splash_radius_evaluated",
            "splash_can_damage_observed",
            "splash_can_damage_result",
            "splash_bsp_blocker_verified",
            "splash_water_boundary_verified",
            "splash_target_undamaged",
        ))
        lifecycle = "/".join(str(status[name]) for name in (
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
        core = "/".join(str(status[name]) for name in (
            "attack_received",
            "weapon_callback",
            "damage_applied",
            "expected_damage",
            "observed_damage",
            "current_geometry_unchanged",
            "canonical_historical_hit",
            "projectile_forward_required",
            "projectile_forward_authenticated",
            "projectile_forward_advanced",
            "projectile_forward_blocked",
        ))
        timing = "/".join(str(status[name]) for name in (
            "context_snapshot_time_us",
            "context_mapped_time_us",
            "projectile_forward_age_us",
            "projectile_forward_advanced_age_us",
        ))
        raise RuntimeError(
            "canonical rail probe reported "
            f"{status['status']!r} (failure_code={status['failure_code']}, "
            f"core={core}, timing={timing}, splash={splash}, "
            f"lifecycle={lifecycle})"
        )
    required = (
        "armed",
        "players_ready",
        "history_ready",
        "canonical_scope",
        "attack_received",
    )
    if mode.get("require_damage", True):
        required += ("damage_applied",)
    historical_query = (
        not mode.get("current_authority_discharge", False) and
        not mode.get("current_authority_projectile", False)
    )
    if historical_query:
        required += (
            "weapon_callback",
            "current_geometry_unchanged",
        )
        if mode.get("require_historical_hit", True):
            required += ("canonical_historical_hit",)
    elif mode.get("current_authority_projectile", False):
        required += (
            "weapon_callback",
            "current_geometry_unchanged",
        )
    for name in required:
        if status[name] != 1:
            raise RuntimeError(f"canonical rail probe did not prove {name}")
    if not isinstance(status["target_history_captures"], int) or status["target_history_captures"] < 6:
        raise RuntimeError("canonical rail probe did not retain the pre-fire target history")
    if (historical_query and
            (not isinstance(status["applied_age_us"], int) or
             status["applied_age_us"] <= 0)):
        raise RuntimeError("canonical rail probe did not select an earlier authoritative instant")
    if status["failure_code"] != 0:
        raise RuntimeError("passing canonical rail probe retained a failure code")
    expected_eligible = mode.get("expected_eligible_candidates")
    expected_playing = int(mode.get("expected_playing_candidates", 2))
    if (expected_eligible is None and status["eligible_candidates"] < 1) or (
        expected_eligible is not None and
        status["eligible_candidates"] != int(expected_eligible)
    ) or status["playing_candidates"] != expected_playing:
        raise RuntimeError(
            "canonical rail probe retained the wrong playing/eligible client counts"
        )
    if historical_query and not mode.get("require_historical_hit", True) and \
            status["canonical_historical_hit"] != 0:
        raise RuntimeError(
            "canonical exclusion probe incorrectly reported a historical hit"
        )
    if (not mode.get("current_authority_discharge", False) and
            status["observation_weapon_policy"] != mode["weapon_policy"]):
        raise RuntimeError("canonical hitscan probe observed the wrong weapon policy")
    if (mode.get("current_authority_projectile", False) and
            status["canonical_historical_hit"] != 0):
        raise RuntimeError(
            "canonical current-world projectile probe incorrectly claimed a historical impact"
        )
    if (mode.get("require_reduced_splash", False) and
            status["observed_damage"] >= 100):
        raise RuntimeError("canonical rocket splash probe did not retain reduced splash damage")
    maximum_damage = int(mode["expected_damage"])
    if status["expected_damage"] != maximum_damage:
        raise RuntimeError("canonical hitscan probe expected-damage contract changed")
    if mode.get("require_damage", True):
        minimum_damage = int(mode.get("minimum_damage", maximum_damage))
        if not minimum_damage <= status["observed_damage"] <= maximum_damage:
            raise RuntimeError("canonical hitscan probe did not apply exact expected damage")
    if mode.get("require_no_damage", False) and (
        status["damage_applied"] != 1 or status["observed_damage"] != 0
    ):
        raise RuntimeError(
            "canonical protected target did not satisfy its exact zero-damage range"
        )
    if mode.get("require_splash_occlusion", False):
        for name in (
            "splash_occlusion_required",
            "splash_radius_evaluated",
            "splash_can_damage_observed",
        ):
            if status[name] != 1:
                raise RuntimeError(
                    f"canonical splash-occlusion probe did not prove {name}"
                )
        expected_splash = {
            "splash_occlusion_policy": "expected_splash_occlusion_policy",
            "splash_can_damage_result": "expected_splash_can_damage",
            "splash_bsp_blocker_verified": "expected_splash_bsp_blocker",
            "splash_water_boundary_verified":
                "expected_splash_water_boundary",
            "splash_target_undamaged":
                "expected_splash_target_undamaged",
        }
        for field, expectation in expected_splash.items():
            if status[field] != int(mode[expectation]):
                raise RuntimeError(
                    f"canonical splash-occlusion probe reported the wrong {field}"
                )
        if (status["damage_applied"] != 1 or
                status["observed_damage"] != maximum_damage):
            raise RuntimeError(
                "canonical splash-occlusion probe did not retain exact damage"
            )
    for key, field in (
        ("expected_observation_path", "observation_path"),
        ("expected_observation_outcome", "observation_outcome"),
        ("expected_observation_fallback", "observation_fallback"),
    ):
        if key in mode and status[field] != int(mode[key]):
            raise RuntimeError(f"canonical hitscan probe reported the wrong {field}")
    if mode.get("require_water_retrace", False):
        if status["water_retrace_required"] != 1 or status["water_retrace_observed"] != 1:
            raise RuntimeError("canonical hitscan probe did not prove water retrace")
    if mode.get("require_thunderbolt_discharge", False):
        if (status["thunderbolt_discharge_required"] != 1 or
                status["thunderbolt_discharge_ammo_drained"] != 1 or
                status["thunderbolt_discharge_observed"] != 1):
            raise RuntimeError("canonical hitscan probe did not prove Thunderbolt discharge")
    if mode.get("require_sustained_hold", False):
        if (status["sustained_hold_required"] != 1 or
                status["sustained_hold_interrupted"] != 0):
            raise RuntimeError("canonical hitscan probe did not retain the sustained held attack")
    if mode.get("require_projectile_forward", False):
        if (status["projectile_forward_required"] != 1 or
                status["projectile_forward_authenticated"] != 1 or
                status["projectile_forward_advanced"] != 1 or
                status["projectile_forward_blocked"] != 0 or
                status["projectile_forward_advanced_age_us"] <= 0 or
                status["projectile_forward_advanced_age_us"] >
                status["projectile_forward_age_us"]):
            raise RuntimeError(
                "canonical projectile probe did not prove bounded current-world forward")
        expected_launches = int(mode.get("expected_projectile_forward_launches", 0))
        if expected_launches and (
            status["projectile_forward_launches"] != expected_launches or
            status["projectile_forward_expected_launches"] != expected_launches
        ):
            raise RuntimeError(
                "canonical projectile burst did not complete every normal launch"
            )
    if mode.get("require_hybrid_melee", False):
        if (status["melee_selection_required"] != 1 or
                status["melee_selection_authenticated"] != 1 or
                status["melee_historical_eligible"] != 1 or
                status["melee_current_displacement_accepted"] != 1 or
                status["melee_current_displacement_units"] <= 0 or
                status["melee_current_displacement_units"] > 64):
            raise RuntimeError(
                "canonical Chainfist probe did not prove bounded hybrid melee")
    if mode.get("require_prox_lifecycle", False):
        if (status["prox_lifecycle_required"] != 1 or
                status["prox_mine_landed"] != 1 or
                status["prox_mine_triggered"] != 1 or
                status["prox_mine_exploded"] != 1):
            raise RuntimeError(
                "canonical Proximity Launcher probe did not prove its normal lifecycle")
    if mode.get("require_rocket_lifecycle", False):
        exact_lifecycle = {
            "rocket_lifecycle_required": 1,
            "rocket_lifecycle_policy":
                int(mode["expected_rocket_lifecycle_policy"]),
            "rocket_owner_identity_retained": 1,
            "rocket_touch_count": int(mode["expected_rocket_touch_count"]),
            "rocket_touch_current_world":
                int(mode["expected_rocket_touch_current_world"]),
            "rocket_retired": 1,
            "rocket_retired_by_touch":
                int(mode["expected_rocket_retired_by_touch"]),
            "rocket_retired_by_expiry":
                int(mode["expected_rocket_retired_by_expiry"]),
            "rocket_post_touch_hold_verified":
                int(mode["expected_rocket_post_touch_hold_verified"]),
            "rocket_no_double_damage": 1,
            "rocket_lifetime_scheduled_ms": 10000,
        }
        for field, expected in exact_lifecycle.items():
            if status[field] != expected:
                raise RuntimeError(
                    f"canonical Rocket lifecycle reported the wrong {field}"
                )
        elapsed_ms = int(status["rocket_lifetime_elapsed_ms"])
        if mode["expected_rocket_lifecycle_policy"] == 1:
            if not 0 < elapsed_ms < int(status["rocket_lifetime_scheduled_ms"]):
                raise RuntimeError(
                    "canonical Rocket touch retirement elapsed time is invalid"
                )
        else:
            adjusted_ms = (
                elapsed_ms + int(status["projectile_forward_advanced_age_us"]) // 1000
            )
            if not 10000 <= adjusted_ms <= 10032:
                raise RuntimeError(
                    "canonical Rocket expiry did not retain its scheduled lifetime"
                )
    if mode.get("require_historical_mover_occlusion", False):
        for name in (
            "historical_mover_occlusion_required",
            "historical_mover_relocated",
            "historical_mover_baseline_clear",
            "historical_mover_occlusion_observed",
            "historical_mover_target_undamaged",
        ):
            if status[name] != 1:
                raise RuntimeError(
                    f"canonical Railgun mover probe did not prove {name}"
                )
        if status["historical_mover_history_count"] < 6:
            raise RuntimeError(
                "canonical Railgun mover probe did not retain mover history"
            )
        if status["damage_applied"] != 0 or status["observed_damage"] != 0:
            raise RuntimeError(
                "canonical Railgun mover probe damaged through historical occlusion"
            )
    if mode.get("require_mover_relative_projectile", False):
        for name in (
            "mover_relative_projectile_required",
            "mover_relative_target_history_moved",
            "mover_relative_mover_history_moved",
            "mover_relative_pair_preserved",
            "mover_relative_current_world_impact",
            "mover_relative_authority_unchanged",
        ):
            if status[name] != 1:
                raise RuntimeError(
                    f"canonical mover-relative projectile did not prove {name}"
                )
        if status["mover_relative_policy"] != int(
            mode["expected_mover_relative_policy"]
        ):
            raise RuntimeError(
                "canonical mover-relative projectile used the wrong policy"
            )
        if status["mover_relative_history_pairs"] < 2:
            raise RuntimeError(
                "canonical mover-relative projectile lacks paired moving history"
            )
    if mode.get("require_local_action_lease", False):
        for name in (
            "local_action_catalog_ready",
            "local_action_lease_ready",
            "local_action_scoped_record",
            "local_action_leased_record",
            "local_action_continuity_exact",
            "local_action_joined_record",
            "local_action_shadow_ready",
        ):
            if status[name] != 1:
                raise RuntimeError(
                    f"canonical local-action lease did not prove {name}"
                )
        for name in (
            "local_action_lease_offers",
            "local_action_lease_supersedes",
            "local_action_lease_claims",
            "local_action_lease_expired",
            "local_action_command_epoch",
            "local_action_command_sequence",
        ):
            if not isinstance(status[name], int) or status[name] < 1:
                raise RuntimeError(
                    f"canonical local-action lease did not retain {name}"
                )
        if status["local_action_lease_rejected"] != 0:
            raise RuntimeError("canonical local-action lease observed a rejection")
        if status["local_action_shadow_catalog_id"] != mode.get(
            "local_action_catalog_id"
        ):
            raise RuntimeError(
                "canonical local-action shadow catalog identity is not exact"
            )
        if (status["local_action_shadow_flags"] & 0x7) != 0x7:
            raise RuntimeError(
                "canonical local-action shadow is missing its fail-closed base flags"
            )
        if status["local_action_shadow_v2_blockers"] != mode.get(
            "local_action_v2_blockers"
        ):
            raise RuntimeError(
                "canonical local-action shadow blocker mask is not exact"
            )
        if status["local_action_shadow_record_hash"] < 1:
            raise RuntimeError(
                "canonical local-action shadow did not retain a record hash"
            )
    return status


def determinism_signature(status: dict[str, int | str]) -> tuple[int | str, ...]:
    """Compare stable proof semantics, not unavoidable wall-clock samples."""
    return tuple(status[name] for name in (
        "status",
        "armed",
        "players_ready",
        "history_ready",
        "canonical_scope",
        "attack_received",
        "weapon_callback",
        "canonical_historical_hit",
        "damage_applied",
        "current_geometry_unchanged",
        "target_history_captures",
        "failure_code",
        "eligible_candidates",
        "playing_candidates",
        "observation_path",
        "observation_outcome",
        "observation_fallback",
        "observation_flags",
        "observation_query",
        "observation_snapshot_epoch",
        "history_epoch",
        "capture_append_rejections",
        "observation_weapon_policy",
        "expected_damage",
        "observed_damage",
        "water_retrace_required",
        "water_retrace_observed",
        "thunderbolt_discharge_required",
        "thunderbolt_discharge_ammo_drained",
        "thunderbolt_discharge_observed",
        "sustained_hold_required",
        "sustained_hold_interrupted",
        "projectile_forward_required",
        "projectile_forward_authenticated",
        "projectile_forward_advanced",
        "projectile_forward_clamped",
        "projectile_forward_blocked",
        "projectile_forward_launches",
        "projectile_forward_expected_launches",
        "melee_selection_required",
        "melee_selection_authenticated",
        "melee_historical_eligible",
        "melee_current_displacement_accepted",
        "melee_current_displacement_units",
        "prox_lifecycle_required",
        "prox_mine_landed",
        "prox_mine_triggered",
        "prox_mine_exploded",
        "historical_mover_occlusion_required",
        "historical_mover_relocated",
        "historical_mover_baseline_clear",
        "historical_mover_occlusion_observed",
        "historical_mover_target_undamaged",
        "historical_mover_history_count",
        "mover_relative_projectile_required",
        "mover_relative_policy",
        "mover_relative_target_history_moved",
        "mover_relative_mover_history_moved",
        "mover_relative_pair_preserved",
        "mover_relative_current_world_impact",
        "mover_relative_authority_unchanged",
        "local_action_catalog_ready",
        "local_action_lease_ready",
        "local_action_lease_rejected",
        "local_action_scoped_record",
        "local_action_leased_record",
        "local_action_continuity_exact",
        "local_action_joined_record",
        "local_action_shadow_ready",
        "local_action_shadow_catalog_id",
        "local_action_shadow_v2_blockers",
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
    ))


def native_event_probe_determinism_signature(
    evidence: object,
) -> tuple[int, ...]:
    """Compare stable lifecycle/parity semantics, not transport counters."""
    if not isinstance(evidence, dict):
        raise RuntimeError("native event probe run evidence is missing")
    signature: list[int] = []
    probes: list[dict[str, int]] = []
    senders: list[dict[str, int]] = []
    for phase_name in ("first_phase", "second_phase"):
        phase = evidence.get(phase_name)
        if not isinstance(phase, dict):
            raise RuntimeError(
                f"native event probe {phase_name} evidence is missing"
            )
        probe = phase.get("probe_status")
        sender = phase.get("event_sender_status")
        sender_delta = phase.get("event_sender_delta")
        if (not isinstance(probe, dict) or not isinstance(sender, dict) or
                not isinstance(sender_delta, dict) or
                not isinstance(sender_delta.get("counters"), dict)):
            raise RuntimeError(
                f"native event probe {phase_name} semantic rows are missing"
            )
        probes.append(probe)
        senders.append(sender)
        for name in (
            "map_generation",
            "map_end_count",
            "raw_action_records",
            "raw_effect_dispatches",
            "probe_action_commits",
            "probe_effects_suppressed",
            "probe_nonvisual_commits",
            "authoritative_presentations",
            "authority_ref_body_joins",
            *(f"raw_k{index}" for index in range(8)),
            *(f"probe_k{index}" for index in range(8)),
        ):
            value = probe.get(name)
            if not isinstance(value, int):
                raise RuntimeError(
                    f"native event probe signature field {name!r} is missing"
                )
            signature.append(value)
        for name in (
            "candidates_queued", "candidates_promoted", "event_acks",
        ):
            value = sender_delta["counters"].get(name)
            if not isinstance(value, int):
                raise RuntimeError(
                    "native event sender delta signature field "
                    f"{name!r} is missing"
                )
            signature.append(value)
    first_probe, second_probe = probes
    first_sender, second_sender = senders
    for name, value in (
        (
            "map_generation_increment",
            second_probe.get("map_generation", -1) -
            first_probe.get("map_generation", -1),
        ),
        (
            "map_end_increment",
            second_probe.get("map_end_count", -1) -
            first_probe.get("map_end_count", -1),
        ),
        (
            "authority_epoch_rotated",
            int(second_probe.get("authority_epoch") !=
                first_probe.get("authority_epoch")),
        ),
        (
            "sender_stream_epoch_rotated",
            int(second_sender.get("stream_epoch") !=
                first_sender.get("stream_epoch")),
        ),
    ):
        if not isinstance(value, int):
            raise RuntimeError(
                f"native event probe normalized field {name!r} is invalid"
            )
        signature.append(value)
    return tuple(signature)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""


class NativeApplicationRejectionError(RuntimeError):
    """A live client rejected an application payload and cannot make progress."""


def raise_for_native_application_rejection(text: str, expectation: str) -> None:
    """Surface a terminal native application rejection before a wait times out."""
    rejection = text.find(NATIVE_APPLICATION_REJECTED_MARKER)
    if rejection < 0:
        return
    detail = text[rejection:].splitlines()[0].strip()
    raise NativeApplicationRejectionError(
        "child process reported a terminal native application rejection "
        f"while waiting for {expectation}: {detail}"
    )


def server_client_disconnect_count(text: str, client_name: str) -> int:
    """Count exact dedicated-server disconnect rows for one fixture client."""
    return len(re.findall(
        rf"^{re.escape(client_name)}\[[^\r\n]*\]\s+disconnected\s*$",
        text,
        re.MULTILINE,
    ))


def capture_fixture_liveness_baseline(
    server_path: Path,
    clients: dict[str, tuple[str, subprocess.Popen[str], Path]],
) -> tuple[dict[str, int], dict[str, int]]:
    """Snapshot intentional pre-proof disconnects and append-only log sizes."""
    server_text = read_text(server_path)
    return (
        {
            role: server_client_disconnect_count(server_text, client_name)
            for role, (client_name, _process, _path) in clients.items()
        },
        {
            role: len(read_text(path))
            for role, (_client_name, _process, path) in clients.items()
        },
    )


def validate_post_proof_liveness_snapshot(
    server_text: str,
    client_texts: dict[str, str],
    clients: dict[str, tuple[str, subprocess.Popen[str], Path]],
    disconnect_baseline: dict[str, int],
    client_log_size_baseline: dict[str, int],
) -> None:
    """Reject an ERR_DROP or server-observed disconnect after proof begins."""
    for role, (client_name, _process, _path) in clients.items():
        text = client_texts.get(role, "")
        baseline_size = client_log_size_baseline.get(role)
        if baseline_size is None or len(text) < baseline_size:
            raise RuntimeError(
                f"{role} client log truncated during canonical proof"
            )
        post_baseline = text[baseline_size:]
        rejection = post_baseline.find(NATIVE_APPLICATION_REJECTED_MARKER)
        err_drop = CLIENT_ERR_DROP_RE.search(post_baseline)
        if rejection >= 0 or err_drop:
            detail = (
                err_drop.group("message") if err_drop
                else post_baseline[rejection:].splitlines()[0]
            )
            raise RuntimeError(
                f"{role} client reported ERR_DROP after canonical proof: "
                f"{detail}"
            )

        baseline_disconnects = disconnect_baseline.get(role)
        if baseline_disconnects is None:
            raise RuntimeError(
                f"{role} client lacks a canonical disconnect baseline"
            )
        observed_disconnects = server_client_disconnect_count(
            server_text, client_name,
        )
        if observed_disconnects != baseline_disconnects:
            raise RuntimeError(
                f"{role} client disconnected after canonical proof "
                f"(baseline={baseline_disconnects} "
                f"observed={observed_disconnects})"
            )


def verify_post_proof_client_liveness(
    port: int,
    timeout: float,
    server_path: Path,
    clients: dict[str, tuple[str, subprocess.Popen[str], Path]],
    disconnect_baseline: dict[str, int],
    client_log_size_baseline: dict[str, int],
    expected_user_ids: tuple[int, ...],
    require_spectator: bool = False,
) -> str:
    """Require every fixture process and live server peer after all proofs."""

    def validate_snapshot() -> None:
        validate_post_proof_liveness_snapshot(
            read_text(server_path),
            {
                role: read_text(path)
                for role, (_name, _process, path) in clients.items()
            },
            clients,
            disconnect_baseline,
            client_log_size_baseline,
        )
        for role, (_name, process, _path) in clients.items():
            returncode = process.poll()
            if returncode is not None:
                raise RuntimeError(
                    f"{role} client exited after canonical proof: "
                    f"code={returncode}"
                )

    validate_snapshot()
    response = rcon_command(port, "status", min(1.0, timeout))
    try:
        observed_user_ids = admitted_fixture_user_ids(
            response, require_spectator=require_spectator,
        )
    except RuntimeError as error:
        raise RuntimeError(
            "post-proof live client roster is incomplete"
        ) from error
    if observed_user_ids != expected_user_ids:
        raise RuntimeError(
            "post-proof live client identities changed; "
            f"expected={expected_user_ids} observed={observed_user_ids}"
        )
    # Re-read append-only logs after the network round trip so a drop racing
    # the final status query cannot be hidden by the preceding snapshot.
    validate_snapshot()
    return response


def wait_for_marker(process: subprocess.Popen[str], path: Path, marker: str, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = read_text(path)
        raise_for_native_application_rejection(text, f"marker {marker!r}")
        if marker in text:
            return
        if process.poll() is not None:
            raise RuntimeError(f"process exited before marker {marker!r}: {process.returncode}")
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for marker {marker!r}")


def wait_for_marker_count(
    process: subprocess.Popen[str], path: Path, marker: str, count: int, timeout: float,
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = read_text(path)
        raise_for_native_application_rejection(
            text, f"{count} markers {marker!r}",
        )
        if text.count(marker) >= count:
            return
        if process.poll() is not None:
            raise RuntimeError(f"process exited before {count} markers {marker!r}: {process.returncode}")
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {count} markers {marker!r}")


def _rcon_command_attempts(
    port: int, command: str, timeout: float, attempts: int,
) -> str:
    """Execute a localhost-only rcon payload with an explicit send bound."""
    if attempts < 1:
        raise ValueError("rcon attempts must be positive")
    packet = b"\xff\xff\xff\xffrcon " + RCON_PASSWORD.encode("ascii")
    packet += b" " + command.encode("ascii") + b"\n"
    # Rcon is localhost-only and stateless. A bounded resend absorbs transient
    # Windows UDP reset/no-reply noise without changing any gameplay input
    # semantics; the command payload itself remains the same normal fixture
    # command on either delivery.
    for attempt in range(attempts):
        responses: list[bytes] = []
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
            connection.settimeout(min(timeout, 0.25))
            connection.sendto(packet, ("127.0.0.1", port))
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                try:
                    response, _ = connection.recvfrom(65535)
                except socket.timeout:
                    break
                except ConnectionResetError:
                    # Windows can surface an unrelated local UDP ICMP reset on
                    # an ephemeral rcon socket while the dedicated process is
                    # still alive. Keep receiving through this attempt.
                    continue
                responses.append(response)
        if responses:
            return b"".join(responses).decode("utf-8", errors="replace")
        if attempt + 1 < attempts:
            continue
    raise RuntimeError(f"localhost rcon command did not reply: {command!r}")


def rcon_command(port: int, command: str, timeout: float) -> str:
    """Execute one idempotent localhost rcon command with one bounded resend."""
    return _rcon_command_attempts(port, command, timeout, 2)


def rcon_command_once(port: int, command: str, timeout: float) -> str:
    """Execute a non-idempotent rcon command without replaying its payload."""
    return _rcon_command_attempts(port, command, timeout, 1)


def wait_for_status(
    port: int, timeout: float, mode: dict[str, int | str | bool],
    shooter_user_id: int | None = None,
    response_sink: list[str] | None = None,
) -> tuple[dict[str, int | str], list[str]]:
    """Poll the fixture cvar until it completes or reports failure."""
    deadline = time.monotonic() + timeout
    responses: list[str] = []

    def retain(response: str) -> None:
        responses.append(response)
        if response_sink is not None:
            response_sink.append(response)

    release_sent = False
    terminal_attack_release_sent = False
    throw_release_sent = False
    throw_release_flush_sent = False
    # Avoid creating a burst of short-lived Windows UDP sockets while still
    # supplying the later ordinary commands needed by Generic/repeating weapon
    # frames. The first refresh is deliberately later than the initial
    # impaired attack's server arrival.
    next_held_refresh = time.monotonic() + 0.15
    held_refresh_cutoff = time.monotonic() + float(
        mode.get("refresh_held_attack_until_seconds", float("inf"))
    )
    has_scheduled_throw_release = "release_held_attack_after_seconds" in mode
    throw_release_delay = float(
        mode.get("release_held_attack_after_seconds", 0.0)
    )
    throw_release_at = None
    if (has_scheduled_throw_release and
            not mode.get("release_held_attack_after_attack_received", False)):
        throw_release_at = time.monotonic() + throw_release_delay
    throw_release_flush_at = 0.0
    while time.monotonic() < deadline:
        # Held throws are primed by the first ordinary +attack and released by
        # a later ordinary no-attack command. This is a client-side key-up
        # only: it does not construct server input, invoke a weapon callback,
        # initialize physical input, or capture the mouse.
        if (throw_release_at is not None and
                not throw_release_sent and
                time.monotonic() >= throw_release_at):
            if shooter_user_id is None:
                raise RuntimeError("held-throw mode requires a shooter user id")
            retain(rcon_command(
                port,
                (f'stuff {shooter_user_id} "-attack; +moveup"'
                 if mode.get("release_held_attack_flush", False)
                 else f'stuff {shooter_user_id} "-attack"'),
                min(1.0, timeout),
            ))
            throw_release_sent = True
            throw_release_flush_at = time.monotonic() + 0.05
        if (mode.get("release_held_attack_flush", False) and
                throw_release_sent and not throw_release_flush_sent and
                time.monotonic() >= throw_release_flush_at):
            if shooter_user_id is None:
                raise RuntimeError("held-throw mode requires a shooter user id")
            retain(rcon_command(
                port, f'stuff {shooter_user_id} "-moveup"', min(1.0, timeout),
            ))
            throw_release_flush_sent = True
        # A repeated console +attack is a duplicate key-down and need not emit
        # a fresh client command. Cadence modes therefore submit a client-side
        # release/press edge while pending. The zero-net movement edge asks the
        # client to flush that ordinary BUTTON_ATTACK user command promptly;
        # neither it nor the final command moves the player. This never calls a
        # server weapon path or constructs server-side input.
        if (mode.get("refresh_held_attack", False) and not release_sent and
                not terminal_attack_release_sent and
                time.monotonic() >= next_held_refresh and
                time.monotonic() < held_refresh_cutoff):
            if shooter_user_id is None:
                raise RuntimeError("held canonical weapon mode requires a shooter user id")
            retain(rcon_command(
                port,
                f'stuff {shooter_user_id} "-attack; +attack; +moveup; -moveup"',
                min(1.0, timeout),
            ))
            next_held_refresh = time.monotonic() + 0.25
        response = rcon_command(port, f"cvarlist {mode['status_cvar']}", min(1.0, timeout))
        retain(response)
        try:
            status = parse_status(response, str(mode["status_cvar"]))
        except RuntimeError:
            time.sleep(0.20)
            continue
        if status["status"] != "pending":
            # Stop a normal held attack as soon as the authoritative fixture
            # reaches a terminal state.  Post-proof event/snapshot status
            # collection can take seconds; leaving +attack latched during that
            # interval manufactures an unrelated unbounded receipt stream.
            # The zero-net movement edge asks the hidden client to flush the
            # ordinary key-up without initializing device input or moving it.
            uses_held_attack = str(
                mode.get("input_command", "+attack")
            ) == "+attack"
            if uses_held_attack and not terminal_attack_release_sent:
                if shooter_user_id is None:
                    raise RuntimeError(
                        "terminal held-attack release requires a shooter user id"
                    )
                retain(rcon_command(
                    port,
                    f'stuff {shooter_user_id} "-attack; +moveup; -moveup"',
                    min(1.0, timeout),
                ))
                terminal_attack_release_sent = True
            if status["status"] != "pass":
                # A known fixture failure is the primary result. Surface its
                # exact code before parity/native-status waits can replace it
                # with a secondary timeout from the now-disconnected client.
                validate_status(status, mode)
            if status["status"] == "pass" and mode.get(
                    "require_local_action_lease", False):
                # Damage can complete in the weapon callback before the same
                # server frame's post-command observation lease has joined
                # and republished its immutable record.  Keep polling the
                # real fixture state; do not accept a transient pass image
                # that predates the required local-action proof.
                local_action_ready = all(
                    status[name] == 1 for name in (
                        "local_action_catalog_ready",
                        "local_action_lease_ready",
                        "local_action_scoped_record",
                        "local_action_leased_record",
                        "local_action_continuity_exact",
                        "local_action_joined_record",
                        "local_action_shadow_ready",
                    )
                ) and status["local_action_command_epoch"] > 0 and \
                    status["local_action_command_sequence"] > 0 and \
                    status["local_action_shadow_record_hash"] > 0
                if not local_action_ready:
                    time.sleep(0.20)
                    continue
            return status, responses
        if (has_scheduled_throw_release and
                mode.get("release_held_attack_after_attack_received", False) and
                throw_release_at is None and status["attack_received"] == 1):
            # This consumes the fixture's observed normal command admission,
            # not a synthetic server input. The later key-up remains an
            # ordinary command executed by the headless client.
            throw_release_at = time.monotonic() + throw_release_delay
        if (mode.get("release_after_expected_damage", False) and
                not release_sent and
                status["observed_damage"] == mode["expected_damage"]):
            if shooter_user_id is None:
                raise RuntimeError("release canonical weapon mode requires a shooter user id")
            # A normal client-side key-up becomes the no-attack user command
            # that the production repeating weapon sees. No server input or
            # weapon callback is constructed here.
            retain(rcon_command(
                # +moveup requests immediate packet delivery; its matching
                # release leaves this client command with zero movement.
                port, f'stuff {shooter_user_id} "-attack; +moveup; -moveup"',
                min(1.0, timeout),
            ))
            release_sent = True
        time.sleep(0.20)
    last_response = responses[-1] if responses else "<no response>"
    raise RuntimeError(
        "timed out waiting for canonical rail fixture completion; "
        f"last rcon response={last_response!r}"
    )


def wait_for_fixture_ready(
    port: int, timeout: float, mode: dict[str, int | str | bool],
    shooter_user_id: int, target_user_id: int,
) -> tuple[dict[str, int | str], list[str]]:
    """Wait for both real clients and retained history before sending input.

    A reconnect can finish its engine admission before it has a safe spawn and
    accepts the reconnected client's ordinary menu choice. Retry only that
    real client string command while polling the fixture's read-only candidate
    telemetry. Once selected, leave both clients quiet while normal end-frame
    history settles. This never creates a user command, action context, weapon
    state, or gameplay authority on the server.
    """
    deadline = time.monotonic() + timeout
    responses: list[str] = []
    next_join_retry = time.monotonic()
    last_response = ""
    players_observed = False
    expected_playing = int(mode.get("expected_playing_candidates", 2))
    expected_eligible = mode.get("expected_eligible_candidates")
    while time.monotonic() < deadline:
        response = rcon_command(
            port, f"cvarlist {mode['status_cvar']}", min(1.0, timeout),
        )
        responses.append(response)
        last_response = response
        try:
            status = parse_status(response, str(mode["status_cvar"]))
        except RuntimeError:
            time.sleep(0.10)
            continue
        if status["status"] == "fail":
            raise RuntimeError(
                "canonical rail fixture failed before canonical input; "
                f"status={status!r}"
            )
        candidates_ready = (
            status["playing_candidates"] == expected_playing and
            (expected_eligible is None or
             status["eligible_candidates"] == int(expected_eligible))
        )
        if (status["players_ready"] == 1 and candidates_ready and
                status["history_ready"] == 1):
            return status, responses

        if status["players_ready"] == 1 and candidates_ready:
            if not players_observed:
                # Match the proven capture cadence: no RCON or client-command
                # traffic while the ordinary server end frames retain poses.
                players_observed = True
                time.sleep(1.20)
            else:
                time.sleep(0.20)
            continue

        now = time.monotonic()
        if now >= next_join_retry:
            if mode.get("team_game", False):
                join_commands = (
                    f'stuff {shooter_user_id} "cmd team red"',
                    f'stuff {target_user_id} "cmd team blue"',
                )
            else:
                join_commands = (
                    f'stuff {shooter_user_id} "cmd team free"',
                    f'stuff {target_user_id} "cmd team free"',
                )
            for command in join_commands:
                responses.append(rcon_command(port, command, min(1.0, timeout)))
            next_join_retry = now + 1.00
        time.sleep(0.20)

    scheduler = []
    for cvar_name in (
        "sv_paused", "timescale", "fixedtime", "g_frames_per_frame", "sv_fps",
    ):
        try:
            scheduler.append(rcon_command(
                port, f"cvarlist {cvar_name}", min(1.0, timeout),
            ))
        except RuntimeError as error:
            scheduler.append(f"{cvar_name}: {error}")
    raise RuntimeError(
        "timed out waiting for both admitted clients and retained history; "
        f"last rcon response={last_response!r}; scheduler={scheduler!r}"
    )


def admitted_fixture_user_ids(
    status_response: str, *, require_spectator: bool = False,
) -> tuple[int, ...]:
    """Return slots for the exact named real fixture clients."""
    clients = {
        match.group("name"): int(match.group("user_id"))
        for match in CLIENT_STATUS_RE.finditer(status_response)
    }
    expected = {SHOOTER_NAME, TARGET_NAME}
    if require_spectator:
        expected.add(SPECTATOR_NAME)
    if set(clients) != expected:
        raise RuntimeError(
            "canonical-rail client roster differs from the required names; "
            f"observed={clients}"
        )
    result = (clients[SHOOTER_NAME], clients[TARGET_NAME])
    if require_spectator:
        result += (clients[SPECTATOR_NAME],)
    return result


def parse_local_action_parity(text: str) -> dict[str, int]:
    matches = list(LOCAL_ACTION_PARITY_RE.finditer(text))
    if not matches:
        raise RuntimeError("headless shooter did not report local-action authority parity")
    return {
        name: int(value)
        for name, value in matches[-1].groupdict().items()
    }


def wait_for_local_action_parity(
    process: subprocess.Popen[str], path: Path, timeout: float,
) -> dict[str, int]:
    """Wait until every received action receipt has an exact command pair."""
    deadline = time.monotonic() + timeout
    last: dict[str, int] | None = None
    while time.monotonic() < deadline:
        text = read_text(path)
        if LOCAL_ACTION_PARITY_RE.search(text):
            last = parse_local_action_parity(text)
            if (last["mismatches"] != 0 or last["conflicts"] != 0 or
                    last["resync"] != 0):
                raise RuntimeError(
                    "headless shooter rejected local-action authority parity: "
                    f"{last}"
                )
            if (last["receipts"] >= 1 and
                    last["matches"] == last["receipts"] and
                    last["outstanding"] == 0 and
                    last["passes"] >= 1 and
                    last["commands"] >= last["matches"]):
                return last
        if process.poll() is not None:
            raise RuntimeError(
                "headless shooter exited before local-action parity completed: "
                f"code={process.returncode} last={last}"
            )
        time.sleep(0.05)
    raise RuntimeError(
        "timed out waiting for exact local-action authority parity; "
        f"last={last}"
    )


def parse_native_status_rows(
    text: str, marker: str,
) -> list[dict[str, int | str]]:
    """Parse one native diagnostic family without depending on field order."""
    pattern = re.compile(rf"{re.escape(marker)}\s+(?P<fields>[^\r\n]+)")
    rows: list[dict[str, int | str]] = []
    for match in pattern.finditer(text):
        row: dict[str, int | str] = {}
        for token in match.group("fields").split():
            if "=" not in token:
                raise RuntimeError(f"malformed {marker} token {token!r}")
            name, value = token.split("=", 1)
            if not name or name in row:
                raise RuntimeError(f"duplicate or empty {marker} field {name!r}")
            try:
                row[name] = int(value, 0)
            except ValueError:
                row[name] = value
        rows.append(row)
    return rows


def native_uint(row: dict[str, int | str], name: str) -> int:
    value = row.get(name)
    if not isinstance(value, int) or value < 0:
        raise RuntimeError(f"native status field {name!r} is missing or invalid")
    return value


def parse_strict_native_rows(
    text: str,
    marker: str,
    fields: tuple[str, ...],
    *,
    chain_hash_fields: frozenset[str] = frozenset(),
) -> list[dict[str, int]]:
    """Parse an ordered, exact-field diagnostic grammar fail-closed."""
    rows: list[dict[str, int]] = []
    for line in text.splitlines():
        if marker not in line:
            continue
        if not line.startswith(marker + " "):
            raise RuntimeError(
                f"{marker} marker is not anchored at the start of its row"
            )
        if line.count(marker) != 1:
            raise RuntimeError(f"duplicate {marker} marker in one row")
        tail = line[len(marker) + 1:]
        tokens = tail.split()
        names: list[str] = []
        row: dict[str, int] = {}
        for token in tokens:
            if token.count("=") != 1:
                raise RuntimeError(f"malformed {marker} token {token!r}")
            name, raw = token.split("=", 1)
            if not name or name in row:
                raise RuntimeError(
                    f"duplicate or empty {marker} field {name!r}"
                )
            names.append(name)
            if name in chain_hash_fields:
                if not STRICT_CHAIN_HASH_RE.fullmatch(raw):
                    raise RuntimeError(
                        f"{marker} field {name!r} is not fixed lowercase hex"
                    )
                row[name] = int(raw, 16)
            else:
                if not STRICT_DECIMAL_RE.fullmatch(raw):
                    raise RuntimeError(
                        f"{marker} field {name!r} is not unsigned decimal"
                    )
                row[name] = int(raw, 10)
        if tuple(names) != fields:
            missing = [name for name in fields if name not in row]
            extra = [name for name in names if name not in fields]
            raise RuntimeError(
                f"{marker} exact field order changed; "
                f"missing={missing} extra={extra} observed={names}"
            )
        rows.append(row)
    return rows


def parse_native_event_probe_status_rows(text: str) -> list[dict[str, int]]:
    return parse_strict_native_rows(
        text,
        NATIVE_EVENT_PROBE_STATUS_MARKER,
        NATIVE_EVENT_PROBE_STATUS_FIELDS,
        chain_hash_fields=NATIVE_EVENT_PROBE_HASH_FIELDS,
    )


def parse_native_event_probe_status(text: str) -> dict[str, int]:
    rows = parse_native_event_probe_status_rows(text)
    if len(rows) != 1:
        raise RuntimeError(
            "expected one native event probe status row; "
            f"observed={len(rows)}"
        )
    return rows[0]


def parse_native_event_probe_checkpoint_rows(
    text: str,
) -> list[dict[str, int]]:
    return parse_strict_native_rows(
        text,
        NATIVE_EVENT_PROBE_CHECKPOINT_MARKER,
        NATIVE_EVENT_PROBE_CHECKPOINT_FIELDS,
    )


def validate_native_event_probe_checkpoint(
    row: dict[str, int], *, expected_result: int,
    expected_map_generation: int, expected_authority_epoch: int,
    expected_checkpoint_id: int,
) -> dict[str, int]:
    """Validate one exact checkpoint receipt and its request identity."""
    required_exact = {
        "valid": 1,
        "schema": 1,
        "size": 32,
        "result": expected_result,
        "map_generation": expected_map_generation,
        "authority_epoch": expected_authority_epoch,
        "checkpoint_id": expected_checkpoint_id,
    }
    for name in NATIVE_EVENT_PROBE_CHECKPOINT_FIELDS:
        value = row.get(name)
        maximum = 0xffffffffffffffff if name == "checkpoint_id" else 0xffffffff
        if not isinstance(value, int) or not 0 <= value <= maximum:
            raise RuntimeError(
                f"native event probe checkpoint field {name!r} is invalid"
            )
    for name, expected in required_exact.items():
        if row[name] != expected:
            raise RuntimeError(
                "native event probe checkpoint receipt mismatch; "
                f"field={name} observed={row[name]} expected={expected} "
                f"row={row!r}"
            )
    return row


def parse_native_event_sender_status_rows(text: str) -> list[dict[str, int]]:
    return parse_strict_native_rows(
        text,
        NATIVE_EVENT_SENDER_STATUS_MARKER,
        NATIVE_EVENT_SENDER_STATUS_FIELDS,
    )


def validate_native_event_probe_status(
    row: dict[str, int], *, require_activity: bool | None,
) -> dict[str, int]:
    required_exact = {
        "valid": 1,
        "schema": 3,
        "size": 336,
        "kind_count": 8,
        "map_active": 1,
        "probe_requested": 1,
        "probe_latched": 1,
        "probe_active": 1,
        "effect_authority_enabled": 0,
        "resources_required": 1,
        "legacy_owner_active": 1,
        "raw_pending_count": 0,
        "authority_requires_resync": 0,
        "authority_degraded": 0,
        "raw_effect_suppressions": 0,
        "raw_pair_failures": 0,
        "native_effect_dispatches": 0,
        "native_effect_chain_hash": 0,
        "presenter_commit_mismatches": 0,
        "authoritative_duplicates": 0,
        "authoritative_conflicts": 0,
        "legacy_ref_body_mismatches": 0,
    }
    for name, expected in required_exact.items():
        if row.get(name) != expected:
            raise RuntimeError(
                f"native event probe did not prove {name}={expected}; "
                f"row={row!r}"
            )
    for name in NATIVE_EVENT_PROBE_STATUS_FIELDS:
        value = row.get(name)
        if not isinstance(value, int) or value < 0:
            raise RuntimeError(
                f"native event probe field {name!r} is missing or invalid"
            )
        maximum = 0xffffffff if name in NATIVE_EVENT_PROBE_U32_FIELDS else 0xffffffffffffffff
        if value > maximum:
            raise RuntimeError(
                f"native event probe field {name!r} exceeds its ABI width"
            )
    if row["map_generation"] == 0 or row["authority_epoch"] == 0:
        raise RuntimeError(
            "native event probe did not publish live map/authority epochs"
        )
    if row["map_generation"] != row["map_end_count"] + 1:
        raise RuntimeError(
            "native event probe active-map lifecycle is impossible; "
            f"generation={row['map_generation']} "
            f"end_count={row['map_end_count']}"
        )

    # The pre-checkpoint sample is lifecycle evidence, not the comparable
    # action window. Login/spawn traffic can legitimately contain native
    # legacy-entity records whose already-dispatched raw frame carrier is not
    # part of the ACTION_PRE probe FIFO. Health and ABI checks above still
    # apply; the explicit checkpoint discards this cumulative activity before
    # strict supported-kind and semantic parity begin.
    if require_activity is None:
        return row

    unsupported = {
        name: row[name]
        for index in NATIVE_EVENT_PROBE_UNSUPPORTED_KINDS
        for name in (f"raw_k{index}", f"probe_k{index}")
        if row[name]
    }
    if unsupported:
        raise RuntimeError(
            "native event probe reported production-unsupported raw kinds; "
            f"nonzero={unsupported}"
        )

    activity_fields = (
        "raw_action_records",
        "raw_action_chain_hash",
        "raw_effect_dispatches",
        "raw_effect_chain_hash",
        "probe_action_commits",
        "probe_action_chain_hash",
        "probe_effects_suppressed",
        "probe_nonvisual_commits",
        "authoritative_presentations",
        "authority_ref_body_joins",
        *(f"raw_k{index}" for index in range(8)),
        *(f"probe_k{index}" for index in range(8)),
    )
    if not require_activity:
        nonzero = {name: row[name] for name in activity_fields if row[name]}
        if nonzero:
            raise RuntimeError(
                "native event probe map baseline was not zero-reset; "
                f"nonzero={nonzero}"
            )
        return row

    raw_count = row["raw_action_records"]
    probe_count = row["probe_action_commits"]
    if raw_count != NATIVE_EVENT_PROBE_EXPECTED_EVENTS:
        raise RuntimeError(
            "native event probe did not prove the exact production action "
            "count; "
            f"observed={raw_count} "
            f"expected={NATIVE_EVENT_PROBE_EXPECTED_EVENTS}"
        )
    if raw_count != probe_count:
        raise RuntimeError(
            "native event probe raw/probe action counts differ; "
            f"raw={raw_count} probe={probe_count}"
        )
    if (row["raw_action_chain_hash"] == 0 or
            row["raw_action_chain_hash"] != row["probe_action_chain_hash"]):
        raise RuntimeError(
            "native event probe raw/probe action hashes differ or are zero"
        )
    raw_kinds = tuple(row[f"raw_k{index}"] for index in range(8))
    probe_kinds = tuple(row[f"probe_k{index}"] for index in range(8))
    if raw_kinds != probe_kinds or sum(raw_kinds) != raw_count:
        raise RuntimeError(
            "native event probe raw/probe kind parity failed; "
            f"raw={raw_kinds} probe={probe_kinds} count={raw_count}"
        )
    if raw_kinds != NATIVE_EVENT_PROBE_EXPECTED_KINDS:
        raise RuntimeError(
            "native event probe did not prove the exact production kind "
            "profile; "
            f"observed={raw_kinds} "
            f"expected={NATIVE_EVENT_PROBE_EXPECTED_KINDS}"
        )
    visual_count = sum(raw_kinds[1:])
    if visual_count == 0:
        raise RuntimeError(
            "native event probe observed no raw visual effects"
        )
    if (row["raw_effect_dispatches"] != visual_count or
            row["probe_effects_suppressed"] != visual_count or
            row["probe_nonvisual_commits"] != raw_kinds[0]):
        raise RuntimeError(
            "native event probe did not prove exactly-once raw effects and "
            "zero-effect probe commits"
        )
    if visual_count and row["raw_effect_chain_hash"] == 0:
        raise RuntimeError(
            "native event probe raw effect chain hash remained zero"
        )
    if (row["authoritative_presentations"] != probe_count or
            row["authority_ref_body_joins"] != probe_count):
        raise RuntimeError(
            "native event probe did not prove exact runtime/presenter/ref-body "
            "join parity; "
            f"probe={probe_count} "
            f"presentations={row['authoritative_presentations']} "
            f"joins={row['authority_ref_body_joins']}"
        )
    return row


def validate_native_event_probe_phase_scope(
    pre_checkpoint: dict[str, int], checkpoint: dict[str, object],
    zero_baseline: dict[str, int], completed: dict[str, int],
) -> dict[str, int]:
    """Bind one checkpoint and its complete telemetry window to one lifecycle."""
    validate_native_event_probe_status(
        pre_checkpoint, require_activity=None,
    )
    validate_native_event_probe_status(
        zero_baseline, require_activity=False,
    )
    validate_native_event_probe_status(completed, require_activity=True)
    lifecycle_fields = (
        "map_generation", "map_end_count", "authority_epoch",
    )
    expected_lifecycle = tuple(
        pre_checkpoint[name] for name in lifecycle_fields
    )
    for label, row in (
        ("zero baseline", zero_baseline),
        ("completed status", completed),
    ):
        observed_lifecycle = tuple(row[name] for name in lifecycle_fields)
        if observed_lifecycle != expected_lifecycle:
            raise RuntimeError(
                "native event probe lifecycle rotated inside one checkpoint "
                f"window; artifact={label!r} expected={expected_lifecycle} "
                f"observed={observed_lifecycle}"
            )

    map_generation = pre_checkpoint["map_generation"]
    map_end_count = pre_checkpoint["map_end_count"]
    authority_epoch = pre_checkpoint["authority_epoch"]
    expected_checkpoint_id = (map_generation << 32) | authority_epoch
    checkpoint_id = checkpoint.get("checkpoint_id")
    if checkpoint_id != expected_checkpoint_id:
        raise RuntimeError(
            "native event probe checkpoint identity escaped its lifecycle "
            f"scope; observed={checkpoint_id!r} "
            f"expected={expected_checkpoint_id}"
        )
    expected_command = (
        f"{NATIVE_EVENT_PROBE_CHECKPOINT_CLIENT_COMMAND} "
        f"{map_generation} {authority_epoch} {expected_checkpoint_id}"
    )
    if checkpoint.get("command") != expected_command:
        raise RuntimeError(
            "native event probe checkpoint command escaped its lifecycle scope"
        )

    applied = checkpoint.get("applied_receipt")
    duplicate = checkpoint.get("duplicate_receipt")
    if not isinstance(applied, dict) or not isinstance(duplicate, dict):
        raise RuntimeError(
            "native event probe checkpoint lifecycle receipts are missing"
        )
    validate_native_event_probe_checkpoint(
        applied,
        expected_result=NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED,
        expected_map_generation=map_generation,
        expected_authority_epoch=authority_epoch,
        expected_checkpoint_id=expected_checkpoint_id,
    )
    validate_native_event_probe_checkpoint(
        duplicate,
        expected_result=NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED,
        expected_map_generation=map_generation,
        expected_authority_epoch=authority_epoch,
        expected_checkpoint_id=expected_checkpoint_id,
    )
    return {
        "map_generation": map_generation,
        "map_end_count": map_end_count,
        "authority_epoch": authority_epoch,
        "checkpoint_id": expected_checkpoint_id,
    }


def validate_native_event_probe_map_reuse(
    first: dict[str, int], second_baseline: dict[str, int],
    second: dict[str, int],
) -> dict[str, object]:
    validate_native_event_probe_status(first, require_activity=True)
    validate_native_event_probe_status(second_baseline, require_activity=False)
    validate_native_event_probe_status(second, require_activity=True)
    if second_baseline["map_generation"] != first["map_generation"] + 1:
        raise RuntimeError(
            "native event probe map generation did not increment exactly once"
        )
    if second_baseline["map_end_count"] != first["map_end_count"] + 1:
        raise RuntimeError(
            "native event probe map end count did not increment exactly once"
        )
    if second_baseline["authority_epoch"] == first["authority_epoch"]:
        raise RuntimeError(
            "native event probe authority epoch did not rotate on map reload"
        )
    if (second["map_generation"] != second_baseline["map_generation"] or
            second["map_end_count"] != second_baseline["map_end_count"] or
            second["authority_epoch"] != second_baseline["authority_epoch"]):
        raise RuntimeError(
            "native event probe phase-two lifecycle changed during one map"
        )
    return {
        "generation_increment": 1,
        "map_end_increment": 1,
        "authority_epoch_rotated": True,
    }


def validate_native_event_sender_baseline(
    row: dict[str, int], *, expected_slot: int,
) -> dict[str, int]:
    """Validate one closed event-sender image without assuming zero traffic."""
    for name in NATIVE_EVENT_SENDER_STATUS_FIELDS:
        value = row.get(name)
        if not isinstance(value, int) or value < 0:
            raise RuntimeError(
                f"native event sender field {name!r} is missing or invalid"
            )
        maximum = 0xffffffff if name in NATIVE_EVENT_SENDER_U32_FIELDS else 0xffffffffffffffff
        if value > maximum:
            raise RuntimeError(
                f"native event sender field {name!r} exceeds its ABI width"
            )
    required_exact = {
        "schema": 1,
        "slot": expected_slot,
        "mode": 2,
        "sender": 1,
        "retired_sender": 0,
        "tx_open": 1,
        "descriptor_acked": 1,
        "backlog": 0,
        "retained": 0,
        "retired_retained": 0,
        "queue_failures": 0,
        "rejected": 0,
    }
    for name, expected in required_exact.items():
        if row.get(name) != expected:
            raise RuntimeError(
                f"native event sender did not prove {name}={expected}; "
                f"row={row!r}"
            )
    # output_due is intentionally retained as a peer-wide diagnostic.  Event
    # DATA and current/retired native-command ACK receipts share the async
    # output service, so a live command stream may keep it asserted after the
    # event sender's backlog and retention windows are completely closed.
    if row.get("output_due") not in (0, 1):
        raise RuntimeError(
            "native event sender peer-wide output_due was not boolean; "
            f"row={row!r}"
        )
    if row.get("stream_epoch", 0) == 0 or row.get("confirms", 0) == 0:
        raise RuntimeError("native event sender did not publish live epochs")
    queued = row.get("candidates_queued", 0)
    promoted = row.get("candidates_promoted", 0)
    acknowledged = row.get("event_acks", 0)
    if promoted != queued or acknowledged != promoted:
        raise RuntimeError(
            "native event sender baseline was not fully acknowledged and released; "
            f"queued={queued} promoted={promoted} acks={acknowledged}"
        )
    if row.get("descriptor_acks", 0) < 1:
        raise RuntimeError("native event sender descriptor was not acknowledged")
    if (row.get("snapshots_queued", 0) < 1 or
            row.get("prepared", 0) < 1 or
            row.get("confirmed", 0) != row.get("prepared", 0) or
            row.get("first_sends", 0) < 1):
        raise RuntimeError(
            "native event sender did not prove confirmed first-send traffic"
        )
    return row


def native_event_sender_checkpoint_signature(
    row: dict[str, int],
) -> dict[str, int]:
    """Select sender state that must not cross the client checkpoint."""
    fields = (
        "schema", "slot", "mode", "sender", "retired_sender", "tx_open",
        "stream_epoch", "descriptor_acked", "backlog", "retained",
        "retired_retained", "confirms", "queue_failures",
        "candidates_queued", "candidates_promoted", "descriptor_acks",
        "event_acks", "prepared", "confirmed", "rejected", "first_sends",
        "retries", "schema2_batches_promoted", "schema2_events_promoted",
    )
    missing = tuple(name for name in fields if name not in row)
    if missing:
        raise RuntimeError(
            "native event sender checkpoint signature is incomplete; "
            f"missing={missing!r}"
        )
    return {name: row[name] for name in fields}


def validate_native_event_sender_status(
    row: dict[str, int], *, baseline: dict[str, int], expected_slot: int,
    expected_events: int | None, require_schema2_event_batch: bool = False,
    minimum_schema2_event_batch_events: int = 2,
    require_schema2_mixed_singletons: bool = False,
) -> tuple[dict[str, int], dict[str, object]]:
    """Close one post-baseline event window on the unchanged sender stream."""
    validate_native_event_sender_baseline(
        baseline, expected_slot=expected_slot,
    )
    validate_native_event_sender_baseline(row, expected_slot=expected_slot)
    if row["stream_epoch"] != baseline["stream_epoch"]:
        raise RuntimeError(
            "native event sender stream rotated during the checkpointed workload; "
            f"baseline={baseline['stream_epoch']} post={row['stream_epoch']}"
        )

    counters: dict[str, int] = {}
    for name in NATIVE_EVENT_SENDER_COUNTER_FIELDS:
        before = baseline[name]
        after = row[name]
        if after < before:
            raise RuntimeError(
                "native event sender counter was not monotonic within one stream; "
                f"field={name} baseline={before} post={after}"
            )
        counters[name] = after - before

    queued = counters["candidates_queued"]
    promoted = counters["candidates_promoted"]
    acknowledged = counters["event_acks"]
    expected_count_valid = (
        queued > 0 if expected_events is None else queued == expected_events
    )
    if (not expected_count_valid or promoted != queued or
            acknowledged != promoted):
        expected_description = ">0" if expected_events is None else str(
            expected_events
        )
        raise RuntimeError(
            "native event sender did not prove exact checkpoint-window "
            "queue/promote/ACK/release; "
            f"expected={expected_description} queued={queued} promoted={promoted} "
            f"acks={acknowledged}"
        )
    schema2_batches = counters["schema2_batches_promoted"]
    schema2_events = counters["schema2_events_promoted"]
    if schema2_events > promoted:
        raise RuntimeError(
            "native event sender schema-2 logical count exceeded all promoted "
            f"events; batches={schema2_batches} schema2_events={schema2_events} "
            f"promoted={promoted}"
        )
    if require_schema2_event_batch:
        if (not isinstance(minimum_schema2_event_batch_events, int) or
                not 2 <= minimum_schema2_event_batch_events <= 8):
            raise RuntimeError(
                "native event sender schema-2 batch expectation is invalid; "
                "minimum_batch_events="
                f"{minimum_schema2_event_batch_events!r}"
            )
        # Telemetry is aggregate, while the sender's production invariant is
        # 2..8 logical events per batch.  The strict lower average proves at
        # least one batch reaches the requested size (pigeonhole principle)
        # without pretending distinct source fences belong to one unit.
        minimum_total = 2 * schema2_batches
        maximum_total = 8 * schema2_batches
        proves_requested_size = (
            schema2_events >
            (minimum_schema2_event_batch_events - 1) * schema2_batches
        )
        if (schema2_batches < 1 or schema2_events < minimum_total or
                schema2_events > maximum_total or
                not proves_requested_size):
            raise RuntimeError(
                "native event sender did not prove the required same-fence "
                "schema-2 batch population for the checkpoint window; "
                "minimum_batch_events="
                f"{minimum_schema2_event_batch_events} "
                f"batches={schema2_batches} schema2_events={schema2_events} "
                f"promoted={promoted}"
            )
        if require_schema2_mixed_singletons and schema2_events >= promoted:
            raise RuntimeError(
                "native event sender did not prove mixed same-fence schema-2 "
                "batches and distinct-fence schema-1 events; "
                f"batches={schema2_batches} schema2_events={schema2_events} "
                f"promoted={promoted}"
            )
    if (counters["snapshots_queued"] < 1 or counters["prepared"] < 1 or
            counters["confirmed"] != counters["prepared"] or
            counters["first_sends"] < 1):
        raise RuntimeError(
            "native event sender did not prove checkpoint-window confirmed "
            "first-send traffic"
        )
    if counters["queue_failures"] != 0 or counters["rejected"] != 0:
        raise RuntimeError(
            "native event sender checkpoint window observed a queue/rejection "
            f"failure; counters={counters!r}"
        )
    if counters["retries"] < 1:
        raise RuntimeError(
            "native event sender did not prove a checkpoint-window retry under "
            "impairment"
        )
    return row, {
        "stream_epoch": row["stream_epoch"],
        "counters": counters,
    }


def parse_native_event_probe_impairment(
    text: str,
) -> tuple[dict[str, object], dict[str, int]]:
    configs = list(IMPAIR_CONFIG_RE.finditer(text))
    counters = list(IMPAIR_COUNTERS_RE.finditer(text))
    if not configs or len(configs) != len(counters):
        raise RuntimeError(
            "expected one or more complete net_impair profile/counter pairs; "
            f"profiles={len(configs)} counters={len(counters)}"
        )
    for index, (config_match, counter_match) in enumerate(
            zip(configs, counters, strict=True)):
        if config_match.start() > counter_match.start():
            raise RuntimeError(
                "net_impair counter preceded its profile in one diagnostic pair"
            )
        if (index + 1 < len(configs) and
                counter_match.start() > configs[index + 1].start()):
            raise RuntimeError(
                "net_impair diagnostic pairs were not emitted in order"
            )
    float_fields = {"loss", "burst", "reorder", "duplicate", "corrupt"}
    config: dict[str, object] = {
        name: float(value) if name in float_fields else int(value)
        for name, value in configs[-1].groupdict().items()
    }
    observed = {
        name: int(value) for name, value in counters[-1].groupdict().items()
    }
    return config, observed


def validate_native_event_probe_impairment(
    config: dict[str, object], counters: dict[str, int], *, expected_seed: int,
    expected_profile: dict[str, int | float],
) -> dict[str, object]:
    expected = {
        "enabled": 1,
        "seed": expected_seed,
        **expected_profile,
    }
    for name, value in expected.items():
        if config.get(name) != value:
            raise RuntimeError(
                f"native event probe impairment {name}={config.get(name)!r}; "
                f"expected={value!r}"
            )
    if (not isinstance(config.get("queue_current"), int) or
            config["queue_current"] < 0):
        raise RuntimeError("native event probe impairment queue is invalid")
    high_water = config.get("high_water")
    if not isinstance(high_water, int) or high_water < 0:
        raise RuntimeError("native event probe impairment high-water is invalid")
    schedules_packets = any(
        expected_profile[name] > 0 for name in (
            "latency", "jitter", "reorder", "duplicate",
            "upstream_stall", "rate_kbps",
        )
    )
    if schedules_packets and high_water <= 0:
        raise RuntimeError(
            "native event probe impairment queue was never exercised"
        )
    if counters.get("seen", 0) <= 0:
        raise RuntimeError("native event probe impairment observed no packets")
    for name in NATIVE_EVENT_PROBE_IMPAIR_COUNTER_FIELDS:
        value = counters.get(name)
        if not isinstance(value, int) or value < 0:
            raise RuntimeError(
                f"native event probe impairment counter {name!r} is invalid"
            )
    if counters.get("overflow") != 0:
        raise RuntimeError("native event probe impairment queue overflowed")
    return {"config": config, "counters": counters}


def native_event_probe_impairment_delta(
    baseline: dict[str, object], current: dict[str, object],
) -> dict[str, object]:
    """Build a monotonic post-minus-pre workload impairment window."""
    baseline_config = baseline.get("config")
    current_config = current.get("config")
    baseline_counters = baseline.get("counters")
    current_counters = current.get("counters")
    if (not isinstance(baseline_config, dict) or
            not isinstance(current_config, dict) or
            not isinstance(baseline_counters, dict) or
            not isinstance(current_counters, dict)):
        raise RuntimeError("native event probe impairment window is malformed")
    static_config_fields = (
        "enabled", "seed", *NATIVE_EVENT_PROBE_IMPAIR_PROFILE_FIELDS,
    )
    changed = {
        name: (baseline_config.get(name), current_config.get(name))
        for name in static_config_fields
        if baseline_config.get(name) != current_config.get(name)
    }
    if changed:
        raise RuntimeError(
            "native event probe impairment profile changed during workload; "
            f"changed={changed}"
        )
    delta: dict[str, int] = {}
    for name in NATIVE_EVENT_PROBE_IMPAIR_COUNTER_FIELDS:
        before = baseline_counters.get(name)
        after = current_counters.get(name)
        if (not isinstance(before, int) or before < 0 or
                not isinstance(after, int) or after < before):
            raise RuntimeError(
                "native event probe impairment counter was not monotonic; "
                f"field={name} baseline={before!r} post={after!r}"
            )
        delta[name] = after - before
    return {
        "config": current_config,
        "baseline_counters": dict(baseline_counters),
        "post_counters": dict(current_counters),
        "counters": delta,
    }


def validate_native_event_probe_impairment_pair(
    client: dict[str, object], server: dict[str, object],
) -> dict[str, object]:
    aggregate: dict[str, int] = {}
    for name in NATIVE_EVENT_PROBE_IMPAIR_COUNTER_FIELDS:
        aggregate[name] = sum(
            int(endpoint["counters"][name])
            for endpoint in (client, server)
        )
    required_positive = ["seen"]
    profiles = (
        NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE,
        NATIVE_EVENT_PROBE_SERVER_IMPAIR_PROFILE,
    )
    if any(profile["loss"] > 0 for profile in profiles):
        required_positive.append("dropped")
    if any(profile["burst"] > 0 for profile in profiles):
        required_positive.append("burst_dropped")
    if any(profile["reorder"] > 0 for profile in profiles):
        required_positive.append("reordered")
    if any(profile["duplicate"] > 0 for profile in profiles):
        required_positive.append("duplicated")
    if any(profile["corrupt"] > 0 for profile in profiles):
        required_positive.append("corrupted")
    if any(profile["upstream_stall"] > 0 for profile in profiles):
        required_positive.append("upstream_stalled")
    if any(profile["rate_kbps"] > 0 for profile in profiles):
        required_positive.append("throttled")
    for name in required_positive:
        if aggregate[name] <= 0:
            raise RuntimeError(
                f"native event retry impairment did not observe {name}>0"
            )
    required_zero = ["overflow", "resets"]
    if all(profile["loss"] == 0 for profile in profiles):
        required_zero.append("dropped")
    if all(profile["burst"] == 0 for profile in profiles):
        required_zero.append("burst_dropped")
    if all(profile["reorder"] == 0 for profile in profiles):
        required_zero.append("reordered")
    if all(profile["duplicate"] == 0 for profile in profiles):
        required_zero.append("duplicated")
    if all(profile["corrupt"] == 0 for profile in profiles):
        required_zero.append("corrupted")
    if all(profile["rate_kbps"] == 0 for profile in profiles):
        required_zero.append("throttled")
    nonzero = {name: aggregate[name] for name in required_zero if aggregate[name]}
    if nonzero:
        raise RuntimeError(
            "native event retry impairment observed forbidden counters; "
            f"nonzero={nonzero}"
        )
    return {
        "counters": aggregate,
        "required_positive": required_positive,
        "required_zero": required_zero,
        # Latency and jitter have no dedicated counters; their exact configured
        # values remain profile evidence, while scheduler use is proven by the
        # workload seen count and each endpoint's queue high-water mark.
        "configured_scheduler_dimensions": ["latency", "jitter"],
    }


def validate_native_event_probe_openal(
    target_stdout: str, target_stderr: str, openal_log: str,
) -> dict[str, object]:
    """Fail closed unless the hidden target uses OpenAL Soft's null sink."""
    if target_stderr.strip():
        raise RuntimeError("native event probe target wrote stderr")
    stdout_lines = target_stdout.splitlines()
    if "OpenAL initialized." not in stdout_lines:
        raise RuntimeError(
            "native event probe target did not prove OpenAL initialization"
        )
    forbidden_stdout = (
        "Failed to initialize OpenAL:",
        "Initializing DirectSound",
        "DirectSound initialized",
        "Using SDL audio driver:",
        "Couldn't initialize SDL audio:",
        "Couldn't open SDL audio:",
        "Couldn't get SDL audio device:",
        "Couldn't start SDL audio:",
    )
    observed_forbidden = [
        marker for marker in forbidden_stdout if marker in target_stdout
    ]
    if observed_forbidden:
        raise RuntimeError(
            "native event probe target used or attempted an audio fallback; "
            f"markers={observed_forbidden}"
        )
    if NATIVE_EVENT_PROBE_OPENAL_BACKEND_LINE not in openal_log:
        raise RuntimeError(
            "OpenAL Soft did not prove the exact null backend initialization"
        )
    if NATIVE_EVENT_PROBE_OPENAL_DEVICE not in openal_log:
        raise RuntimeError(
            "OpenAL Soft did not prove its No Output playback device"
        )
    return {
        "engine_initialized": True,
        "implementation": "OpenAL Soft",
        "backend": "null",
        "device": NATIVE_EVENT_PROBE_OPENAL_DEVICE,
        "stderr_empty": True,
        "fallback_markers_absent": list(forbidden_stdout),
        "logfile": {"role": "target", "name": "target.openal.log"},
    }


def validate_combined_native_client_status(
    row: dict[str, int | str],
    expected_private_mask: int = 0x77,
) -> dict[str, int | str]:
    required_exact = {
        "schema": 1,
        "enabled": 1,
        "mode": 2,
        "capability_confirmed": 1,
        "protocol": 1038,
        "private_mask": expected_private_mask,
        "failures": 0,
        "last_failure": 0,
    }
    for name, expected in required_exact.items():
        if native_uint(row, name) != expected:
            raise RuntimeError(
                f"combined native client did not prove {name}={expected}"
            )
    if native_uint(row, "server_active") < 1:
        raise RuntimeError("combined native client did not enter SERVER_ACTIVE")
    return row


def validate_native_base_client_status(
    row: dict[str, int | str],
    expected_private_mask: int,
) -> dict[str, int | str]:
    """Require one live client to publish an exact active native bundle."""
    required_exact = {
        "schema": 1,
        "enabled": 1,
        "mode": 2,
        "capability_confirmed": 1,
        "protocol": 1038,
        "public_mask": expected_private_mask,
        "private_mask": expected_private_mask,
        "failures": 0,
        "last_failure": 0,
    }
    for name, expected in required_exact.items():
        if native_uint(row, name) != expected:
            raise RuntimeError(
                f"native client did not prove {name}={expected}; row={row!r}"
            )
    if native_uint(row, "server_active") < 1:
        raise RuntimeError(
            f"native client did not enter SERVER_ACTIVE; row={row!r}"
        )
    return row


def validate_native_base_client_activity(
    row: dict[str, int | str],
    baseline: dict[str, int | str] | None = None,
) -> dict[str, int | str]:
    """Require one completed command proof before gameplay is armed."""
    for name in (
        "proof_enqueued",
        "retained_releases",
        "tx_first_sends",
        "acknowledged_reliable",
    ):
        minimum = native_uint(baseline, name) + 1 if baseline else 1
        if native_uint(row, name) < minimum:
            raise RuntimeError(
                "native client has not closed its first command proof; "
                f"required={name}>={minimum} row={row!r}"
            )
    return row


def validate_native_base_server_status(
    base_rows: list[dict[str, int | str]],
    expected_private_mask: int,
    expected_slots: tuple[int, ...],
) -> list[dict[str, int | str]]:
    """Require an exact live server peer set with one exact native bundle."""
    if (not expected_slots or len(set(expected_slots)) != len(expected_slots) or
            len(base_rows) != len(expected_slots)):
        raise RuntimeError(
            "native server did not report the exact live peer set"
        )
    by_slot: dict[int, dict[str, int | str]] = {}
    for row in base_rows:
        slot = native_uint(row, "slot")
        if slot in by_slot:
            raise RuntimeError("native server repeated a peer slot")
        by_slot[slot] = row
        required_exact = {
            "schema": 1,
            "protocol": 1038,
            "enabled": 1,
            "lifecycle": NATIVE_SERVER_LIFECYCLE_ACTIVE,
            "hooks": 1,
            "public_mask": expected_private_mask,
            "private_mask": expected_private_mask,
            "wire_committed": 1,
            "failures": 0,
            "rx_rejections": 0,
            "tx_ack_rejections": 0,
            "last_failure": 0,
        }
        for name, expected in required_exact.items():
            if native_uint(row, name) != expected:
                raise RuntimeError(
                    f"native server slot {slot} did not prove {name}={expected}; "
                    f"row={row!r}"
                )
        if native_uint(row, "server_active") < 1:
            raise RuntimeError(
                f"native server slot {slot} did not publish SERVER_ACTIVE; "
                f"row={row!r}"
            )
    if set(by_slot) != set(expected_slots):
        raise RuntimeError("native server reported unexpected peer slots")
    return [by_slot[slot] for slot in sorted(by_slot)]


def validate_native_base_server_activity(
    rows: list[dict[str, int | str]],
    baselines: dict[int, dict[str, int | str]] | None = None,
) -> list[dict[str, int | str]]:
    """Require native/legacy command identity to join before fixture input."""
    for row in rows:
        slot = native_uint(row, "slot")
        baseline = baselines.get(slot) if baselines else None
        joins_minimum = (
            native_uint(baseline, "legacy_joins") + 1 if baseline else 1
        )
        matches_minimum = (
            native_uint(baseline, "command_matches") + 1 if baseline else 1
        )
        if (native_uint(row, "legacy_joins") < joins_minimum or
                native_uint(row, "command_matches") < matches_minimum):
            raise RuntimeError(
                "native server has not joined its first command proof; "
                f"required=legacy_joins>={joins_minimum},"
                f"command_matches>={matches_minimum} row={row!r}"
            )
    return rows


def validate_native_base_server_reactivation(
    rows: list[dict[str, int | str]],
    baselines: dict[int, dict[str, int | str]],
) -> list[dict[str, int | str]]:
    """Require a map-reused peer to activate one strictly newer epoch."""
    if not baselines:
        raise RuntimeError("native server reactivation baseline is empty")
    by_slot = {native_uint(row, "slot"): row for row in rows}
    if len(by_slot) != len(rows) or set(by_slot) != set(baselines):
        raise RuntimeError(
            "native server reactivation did not report the baseline peer set"
        )

    for slot, baseline in baselines.items():
        row = by_slot[slot]
        required_exact = {
            "lifecycle": NATIVE_SERVER_LIFECYCLE_ACTIVE,
            "hooks": 1,
        }
        for name, expected in required_exact.items():
            if native_uint(row, name) != expected:
                raise RuntimeError(
                    "native server peer did not reactivate after map reuse; "
                    f"slot={slot} required={name}={expected} row={row!r}"
                )

        for name in (
            "official_epoch",
            "transport_epoch",
            "wire_committed_transport_epoch",
            "challenges_queued",
            "client_ready",
            "server_active",
        ):
            previous = native_uint(baseline, name)
            current = native_uint(row, name)
            if current <= previous:
                raise RuntimeError(
                    "native server peer did not advance map-reuse readiness; "
                    f"slot={slot} required={name}>{previous} row={row!r}"
                )
        if (native_uint(row, "wire_committed_transport_epoch") !=
                native_uint(row, "transport_epoch")):
            raise RuntimeError(
                "native server peer committed a different transport epoch; "
                f"slot={slot} row={row!r}"
            )
    return rows


def validate_native_base_client_server_epochs(
    clients: dict[str, dict[str, int | str]],
    server_rows: list[dict[str, int | str]],
    client_slots: dict[str, int],
) -> dict[str, dict[str, int | str]]:
    """Require every targeted client to publish its active server epochs."""
    if set(clients) != set(client_slots):
        raise RuntimeError(
            "native client/server epoch proof did not report the exact role set"
        )
    server_by_slot = {
        native_uint(row, "slot"): row for row in server_rows
    }
    if (len(server_by_slot) != len(server_rows) or
            set(server_by_slot) != set(client_slots.values())):
        raise RuntimeError(
            "native client/server epoch proof did not report the exact peer set"
        )
    for role, slot in client_slots.items():
        client = clients[role]
        server = server_by_slot[slot]
        for name in ("official_epoch", "transport_epoch"):
            client_epoch = native_uint(client, name)
            server_epoch = native_uint(server, name)
            if client_epoch == 0 or client_epoch != server_epoch:
                raise RuntimeError(
                    "native client/server epoch mismatch; "
                    f"role={role} slot={slot} name={name} "
                    f"client={client_epoch} server={server_epoch}"
                )
    return clients


def validate_legacy_capability_client_status(
    row: dict[str, int | str], expected_mask: int = 0x03,
) -> dict[str, int | str]:
    """Require one connected client to publish its exact public tuple."""
    required_exact = {
        "schema": 1,
        "valid": 1,
        "phase": 2,
        "protocol": PROTOCOL_VERSION_RERELEASE,
        "offered": expected_mask,
        "supported": expected_mask,
        "peer_supported": expected_mask,
        "negotiated": expected_mask,
    }
    for name, expected in required_exact.items():
        if native_uint(row, name) != expected:
            raise RuntimeError(
                f"legacy capability client did not prove {name}={expected}; "
                f"row={row!r}"
            )
    if native_uint(row, "epoch") == 0:
        raise RuntimeError("legacy capability client reported a zero epoch")
    return row


def validate_legacy_capability_server_status(
    rows: list[dict[str, int | str]], expected_slots: tuple[int, ...],
    expected_mask: int = 0x03,
) -> list[dict[str, int | str]]:
    """Require the exact live server peer set to publish the legacy tuple."""
    if (not expected_slots or len(set(expected_slots)) != len(expected_slots) or
            len(rows) != len(expected_slots)):
        raise RuntimeError(
            "legacy capability server did not report the exact live peer set"
        )
    by_slot: dict[int, dict[str, int | str]] = {}
    for row in rows:
        slot = native_uint(row, "slot")
        if slot in by_slot:
            raise RuntimeError("legacy capability server repeated a peer slot")
        by_slot[slot] = row
        required_exact = {
            "schema": 1,
            "protocol": PROTOCOL_VERSION_RERELEASE,
            "offered": expected_mask,
            "supported": expected_mask,
            "negotiated": expected_mask,
            "confirm_sent": 1,
            "failed": 0,
            "native_shadow": 0,
            "input_batch_requested": 0,
            "command_parser": 1,
        }
        for name, expected in required_exact.items():
            if native_uint(row, name) != expected:
                raise RuntimeError(
                    f"legacy capability server slot {slot} did not prove "
                    f"{name}={expected}; row={row!r}"
                )
        if native_uint(row, "epoch") == 0:
            raise RuntimeError(
                f"legacy capability server slot {slot} reported a zero epoch"
            )
    if set(by_slot) != set(expected_slots):
        raise RuntimeError("legacy capability server reported unexpected slots")
    return [by_slot[slot] for slot in sorted(by_slot)]


def wait_for_legacy_capability_status(
    port: int,
    shooter: subprocess.Popen[str],
    target: subprocess.Popen[str],
    shooter_path: Path,
    target_path: Path,
    timeout: float,
    server_slots: tuple[int, ...],
) -> tuple[dict[str, object], list[str]]:
    """Capture exact client/server status for the default legacy 0x03 row."""
    client_specs = {
        "shooter": (shooter, shooter_path),
        "target": (target, target_path),
    }
    client_counts = {
        role: read_text(path).count(CAPABILITY_CLIENT_STATUS_MARKER)
        for role, (_process, path) in client_specs.items()
    }
    responses: list[str] = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        responses.append(rcon_command(
            port, 'stuffall "cl_worr_capability_status"',
            min(1.0, max(0.05, deadline - time.monotonic())),
        ))
        retry_at = min(deadline, time.monotonic() + 0.5)
        while time.monotonic() < retry_at:
            ready = True
            for role, (process, path) in client_specs.items():
                if process.poll() is not None:
                    raise RuntimeError(
                        f"legacy capability {role} exited before status output"
                    )
                ready = ready and read_text(path).count(
                    CAPABILITY_CLIENT_STATUS_MARKER
                ) >= client_counts[role] + 1
            if ready:
                break
            time.sleep(0.05)
        if ready:
            break
    else:
        raise RuntimeError("timed out waiting for legacy capability clients")

    clients: dict[str, dict[str, int | str]] = {}
    for role, (_process, path) in client_specs.items():
        rows = parse_native_status_rows(
            read_text(path), CAPABILITY_CLIENT_STATUS_MARKER,
        )
        if not rows:
            raise RuntimeError(
                f"legacy capability {role} emitted no status row"
            )
        clients[role] = validate_legacy_capability_client_status(rows[-1])

    deadline = time.monotonic() + timeout
    last_error: RuntimeError | None = None
    while time.monotonic() < deadline:
        slot_responses = [
            rcon_command(
                port, f"sv_worr_capability_status {slot}",
                min(1.0, max(0.05, deadline - time.monotonic())),
            )
            for slot in server_slots
        ]
        responses.extend(slot_responses)
        rows = parse_native_status_rows(
            "\n".join(slot_responses), CAPABILITY_SERVER_STATUS_MARKER,
        )
        try:
            server_peers = validate_legacy_capability_server_status(
                rows, server_slots,
            )
            client_epochs = {
                native_uint(row, "epoch") for row in clients.values()
            }
            server_epochs = {
                native_uint(row, "epoch") for row in server_peers
            }
            if (len(client_epochs) != len(clients) or
                    client_epochs != server_epochs):
                raise RuntimeError(
                    "legacy capability client/server epochs did not align"
                )
            return {
                "clients": clients,
                "server_peers": server_peers,
            }, responses
        except RuntimeError as error:
            last_error = error
        if shooter.poll() is not None or target.poll() is not None:
            raise RuntimeError(
                "a legacy capability client exited before server proof completed"
            )
        time.sleep(0.05)
    raise RuntimeError(
        "timed out waiting for legacy capability server proof; "
        f"last={last_error}"
    )


def wait_for_native_base_shadow(
    port: int,
    shooter: subprocess.Popen[str],
    target: subprocess.Popen[str],
    shooter_path: Path,
    target_path: Path,
    timeout: float,
    expected_private_mask: int,
    required_clients: tuple[str, ...],
    server_slots: tuple[int, ...],
    prior_server_peers: Sequence[dict[str, int | str]] | None = None,
) -> tuple[dict[str, object], list[str]]:
    """Capture exact client/server scalar status for one live native mode."""
    client_specs = {
        "shooter": (shooter, shooter_path),
        "target": (target, target_path),
    }
    if (not required_clients or
            len(set(required_clients)) != len(required_clients) or
            any(role not in client_specs for role in required_clients)):
        raise RuntimeError("invalid required native client role set")
    if (not server_slots or len(set(server_slots)) != len(server_slots) or
            len(server_slots) != len(required_clients)):
        raise RuntimeError("invalid required native server slot set")
    client_slots = dict(zip(required_clients, server_slots))

    server_epoch_baselines: dict[int, dict[str, int | str]] | None = None
    if prior_server_peers is not None:
        prior_rows = list(prior_server_peers)
        validate_native_base_server_status(
            prior_rows, expected_private_mask, server_slots,
        )
        server_epoch_baselines = {
            native_uint(row, "slot"): dict(row) for row in prior_rows
        }

    # Let the reliable readiness exchange drain before injecting any client
    # stufftext diagnostics.  A pending CHALLENGE is deliberately queued only
    # after the post-bootstrap reliable queue becomes idle; polling clients
    # first can keep that queue perpetually non-idle on an impaired peer.
    responses: list[str] = []
    def poll_server_activity(
        baselines: dict[int, dict[str, int | str]] | None,
        phase: str,
    ) -> list[dict[str, int | str]]:
        deadline = time.monotonic() + timeout
        last_error: RuntimeError | None = None
        activity_baselines = baselines
        while time.monotonic() < deadline:
            for role in required_clients:
                process, _path = client_specs[role]
                if process.poll() is not None:
                    raise RuntimeError(
                        f"native {role} client exited before status proof"
                    )
            slot_responses = [
                rcon_command(
                    port, f"sv_worr_native_shadow_status {slot}",
                    min(1.0, max(0.05, deadline - time.monotonic())),
                )
                for slot in server_slots
            ]
            responses.extend(slot_responses)
            base_rows = parse_native_status_rows(
                "\n".join(slot_responses), NATIVE_SERVER_STATUS_MARKER,
            )
            reject_terminal_native_server_rows(base_rows, server_slots)
            if activity_baselines is None:
                try:
                    by_slot = {
                        native_uint(row, "slot"): row for row in base_rows
                    }
                    if (len(by_slot) == len(server_slots) and
                            set(by_slot) == set(server_slots)):
                        for row in by_slot.values():
                            native_uint(row, "legacy_joins")
                            native_uint(row, "command_matches")
                        activity_baselines = {
                            slot: dict(row) for slot, row in by_slot.items()
                        }
                except RuntimeError as error:
                    last_error = error
            try:
                server_rows = validate_native_base_server_status(
                    base_rows, expected_private_mask, server_slots,
                )
                if server_epoch_baselines is not None:
                    validate_native_base_server_reactivation(
                        server_rows, server_epoch_baselines,
                    )
                if activity_baselines is None:
                    raise RuntimeError(
                        "native server emitted no phase activity baseline"
                    )
                return validate_native_base_server_activity(
                    server_rows, activity_baselines,
                )
            except RuntimeError as error:
                last_error = error
            time.sleep(0.05)
        raise RuntimeError(
            f"timed out waiting for native server {phase} proof; "
            f"last={last_error}"
        )

    # Epoch/readiness counters compare with the completed prior map, while
    # command identity keeps its own first-row baseline inside this map.  Do
    # not let command traffic accumulated late in the prior phase satisfy the
    # post-reload activity proof.
    server_peers = poll_server_activity(None, "pre-client")

    client_counts = {
        role: read_text(client_specs[role][1]).count(
            NATIVE_CLIENT_STATUS_MARKER)
        for role in required_clients
    }
    client_deadline = time.monotonic() + timeout
    clients: dict[str, dict[str, int | str]] = {}
    client_activity_baselines: dict[str, dict[str, int | str]] = {}
    client_last_rows: dict[str, dict[str, int | str]] = {}
    client_last_errors: dict[str, RuntimeError] = {}
    while time.monotonic() < client_deadline:
        for role in required_clients:
            process, _path = client_specs[role]
            if process.poll() is not None:
                raise RuntimeError(
                    f"native {role} client exited before status proof"
                )

        request_counts = {
            role: read_text(client_specs[role][1]).count(
                NATIVE_CLIENT_STATUS_MARKER)
            for role in required_clients
        }
        requested_roles = tuple(
            role for role in required_clients if role not in clients
        )
        for role in requested_roles:
            slot = client_slots[role]
            responses.append(rcon_command(
                port, f'stuff {slot} "cl_worr_native_shadow_status"',
                min(1.0, max(0.05, client_deadline - time.monotonic())),
            ))
        retry_at = min(client_deadline, time.monotonic() + 0.5)
        while time.monotonic() < retry_at:
            emitted = {role: False for role in required_clients}
            for role in required_clients:
                process, path = client_specs[role]
                if process.poll() is not None:
                    raise RuntimeError(
                        f"native {role} client exited before status proof"
                    )
                text = read_text(path)
                marker_count = text.count(NATIVE_CLIENT_STATUS_MARKER)
                emitted[role] = marker_count > request_counts[role]
                if marker_count <= client_counts[role]:
                    continue
                try:
                    rows = parse_native_status_rows(
                        text, NATIVE_CLIENT_STATUS_MARKER,
                    )
                except RuntimeError as error:
                    client_last_errors[role] = error
                    clients.pop(role, None)
                    continue
                fresh_rows = rows[client_counts[role]:]
                if not fresh_rows:
                    continue
                row = fresh_rows[-1]
                client_last_rows[role] = row

                failures = row.get("failures")
                last_failure = row.get("last_failure")
                readiness_phase = row.get("readiness_phase")
                if ((isinstance(failures, int) and failures != 0) or
                        (isinstance(last_failure, int) and
                         last_failure != 0) or
                        readiness_phase == NATIVE_READINESS_PHASE_FAILED):
                    raise RuntimeError(
                        f"native {role} client entered terminal readiness; "
                        f"row={row!r}"
                    )
                if role not in client_activity_baselines:
                    client_activity_baselines[role] = dict(row)
                    client_last_errors[role] = RuntimeError(
                        "captured phase-local native client activity baseline"
                    )
                    clients.pop(role, None)
                    continue
                try:
                    clients[role] = validate_native_base_client_activity(
                        validate_native_base_client_status(
                            row, expected_private_mask,
                        ), client_activity_baselines[role],
                    )
                    client_last_errors.pop(role, None)
                except RuntimeError as error:
                    client_last_errors[role] = error
                    clients.pop(role, None)
            if len(clients) == len(required_clients):
                break
            # Every unresolved client answered this request with a complete or
            # partial diagnostic.  A new command is required to sample state
            # after the readiness challenge advances.
            if all(
                    role in clients or emitted[role]
                    for role in required_clients):
                break
            time.sleep(0.05)
        if len(clients) == len(required_clients):
            break
        # A diagnostic row is only a sample of readiness, not the mechanism
        # that advances it.  Preserve the intended half-second cadence after
        # an immediate transient reply; otherwise stuffall can flood both
        # client command buffers at roughly one request per polling sleep and
        # starve the handshake this loop is trying to observe.
        retry_delay = retry_at - time.monotonic()
        if retry_delay > 0:
            time.sleep(retry_delay)
    else:
        raise RuntimeError(
            "timed out waiting for native client status proof; "
            f"newest={client_last_rows!r} last={client_last_errors!r}"
        )
    validate_native_base_client_server_epochs(
        clients, server_peers, client_slots,
    )
    server_activity_baselines = {
        native_uint(row, "slot"): dict(row) for row in server_peers
    }
    server_peers = poll_server_activity(
        server_activity_baselines, "post-client",
    )
    return {
        "clients": clients,
        "server_peers": server_peers,
    }, responses


def reject_terminal_native_server_rows(
    rows: list[dict[str, int | str]], expected_slots: tuple[int, ...],
) -> None:
    """Stop polling once an exact peer has entered irreversible drain."""
    expected = set(expected_slots)
    for row in rows:
        slot = native_uint(row, "slot")
        if slot not in expected:
            continue
        lifecycle = native_uint(row, "lifecycle") if "lifecycle" in row else 0
        failures = native_uint(row, "failures") if "failures" in row else 0
        last_failure = (
            native_uint(row, "last_failure") if "last_failure" in row else 0
        )
        if lifecycle == 3 or failures != 0 or last_failure != 0:
            detail = (
                native_uint(row, "last_failure_detail")
                if "last_failure_detail" in row else 0
            )
            raise RuntimeError(
                "native server peer entered terminal drain; "
                f"slot={slot} lifecycle={lifecycle} failures={failures} "
                f"last_failure={last_failure} last_failure_detail=0x{detail:08x}"
            )


def validate_combined_native_server_status(
    base_rows: list[dict[str, int | str]],
    snapshot_rows: list[dict[str, int | str]],
    expected_private_mask: int = 0x77,
    expected_slots: tuple[int, ...] = (0, 1),
) -> dict[str, object]:
    if (len(base_rows) != len(expected_slots) or
            len(snapshot_rows) != len(expected_slots)):
        raise RuntimeError(
            "combined native server did not report the exact live peer set"
        )
    base_by_slot: dict[int, dict[str, int | str]] = {}
    for row in base_rows:
        slot = native_uint(row, "slot")
        if slot in base_by_slot:
            raise RuntimeError("combined native server repeated a peer slot")
        base_by_slot[slot] = row
        required_exact = {
            "schema": 1,
            "protocol": 1038,
            "enabled": 1,
            "private_mask": expected_private_mask,
            "wire_committed": 1,
            "failures": 0,
            "rx_rejections": 0,
            "tx_ack_rejections": 0,
            "last_failure": 0,
        }
        for name, expected in required_exact.items():
            if native_uint(row, name) != expected:
                raise RuntimeError(
                    f"combined native server slot {slot} did not prove "
                    f"{name}={expected}; row={row!r}"
                )

    snapshot_by_slot: dict[int, dict[str, int | str]] = {}
    for row in snapshot_rows:
        slot = native_uint(row, "slot")
        if slot in snapshot_by_slot:
            raise RuntimeError("combined snapshot server repeated a peer slot")
        snapshot_by_slot[slot] = row
        required_exact = {
            "schema": 1,
            "sender": 1,
            "tx_open": 1,
            "queue_failures": 0,
            "rejected": 0,
        }
        for name, expected in required_exact.items():
            if native_uint(row, name) != expected:
                raise RuntimeError(
                    f"combined snapshot slot {slot} did not prove "
                    f"{name}={expected}; row={row!r}"
                )
        if native_uint(row, "snapshot_epoch") == 0:
            raise RuntimeError(
                f"combined snapshot slot {slot} has no snapshot epoch"
            )
        for name in ("queued", "acks", "released", "confirmed", "first_sends"):
            if native_uint(row, name) < 1:
                raise RuntimeError(
                    f"combined snapshot slot {slot} did not prove "
                    f"{name} traffic; row={row!r}"
                )
    if set(base_by_slot) != set(snapshot_by_slot):
        raise RuntimeError("combined native base/snapshot peer slots differ")
    if set(base_by_slot) != set(expected_slots):
        raise RuntimeError(
            "combined native server reported unexpected peer slots"
        )
    return {
        "server_peers": [base_by_slot[slot] for slot in sorted(base_by_slot)],
        "snapshot_peers": [
            snapshot_by_slot[slot] for slot in sorted(snapshot_by_slot)
        ],
    }


def wait_for_combined_native_shadow(
    port: int,
    shooter: subprocess.Popen[str],
    target: subprocess.Popen[str],
    shooter_path: Path,
    target_path: Path,
    timeout: float,
    expected_private_mask: int = 0x77,
    required_clients: tuple[str, ...] = ("shooter", "target"),
    server_slots: tuple[int, ...] = (0, 1),
) -> tuple[dict[str, object], list[str]]:
    """Prove a private native mode plus acknowledged snapshots on exact peers."""
    client_specs = {
        "shooter": (shooter, shooter_path),
        "target": (target, target_path),
    }
    if (not required_clients or any(
            role not in client_specs for role in required_clients)):
        raise RuntimeError("invalid required native client role set")
    if not server_slots or len(set(server_slots)) != len(server_slots):
        raise RuntimeError("invalid required native server slot set")
    client_counts = {
        role: read_text(client_specs[role][1]).count(
            NATIVE_CLIENT_STATUS_MARKER)
        for role in required_clients
    }
    responses: list[str] = []
    client_status_deadline = time.monotonic() + timeout
    client_ready = {role: False for role in required_clients}
    while time.monotonic() < client_status_deadline:
        responses.append(rcon_command(
            port, 'stuffall "cl_worr_native_shadow_status"',
            min(1.0, max(
                0.05, client_status_deadline - time.monotonic(),
            )),
        ))
        retry_at = min(client_status_deadline, time.monotonic() + 0.5)
        while time.monotonic() < retry_at:
            for role in required_clients:
                process, path = client_specs[role]
                client_ready[role] = read_text(path).count(
                    NATIVE_CLIENT_STATUS_MARKER,
                ) >= client_counts[role] + 1
                if process.poll() is not None:
                    raise RuntimeError(
                        f"native {role} client exited before status output"
                    )
            if all(client_ready.values()):
                break
            time.sleep(0.05)
        if all(client_ready.values()):
            break
    else:
        raise RuntimeError(
            "timed out waiting for combined native client status output"
        )
    clients: dict[str, dict[str, int | str]] = {}
    for role in required_clients:
        rows = parse_native_status_rows(
            read_text(client_specs[role][1]), NATIVE_CLIENT_STATUS_MARKER,
        )
        if not rows:
            raise RuntimeError(f"native {role} client emitted no status row")
        clients[role] = validate_combined_native_client_status(
            rows[-1], expected_private_mask,
        )

    deadline = time.monotonic() + timeout
    last_error: RuntimeError | None = None
    while time.monotonic() < deadline:
        slot_responses = [
            rcon_command(
                port, f"sv_worr_native_shadow_status {slot}",
                min(1.0, max(0.05, deadline - time.monotonic())),
            )
            for slot in server_slots
        ]
        responses.extend(slot_responses)
        response = "\n".join(slot_responses)
        base_rows = parse_native_status_rows(
            response, NATIVE_SERVER_STATUS_MARKER,
        )
        snapshot_rows = parse_native_status_rows(
            response, NATIVE_SERVER_SNAPSHOT_STATUS_MARKER,
        )
        emission_rows = parse_native_status_rows(
            response, SNAPSHOT_EMISSION_STATUS_MARKER,
        )
        reject_terminal_native_server_rows(base_rows, server_slots)
        try:
            server = validate_combined_native_server_status(
                base_rows, snapshot_rows, expected_private_mask,
                server_slots,
            )
            return {"clients": clients, **server}, responses
        except RuntimeError as error:
            last_error = RuntimeError(
                f"{error}; emission={emission_rows!r}"
            )
        if shooter.poll() is not None or target.poll() is not None:
            raise RuntimeError(
                "a combined native client exited before status proof completed"
            )
        time.sleep(0.05)
    raise RuntimeError(
        f"timed out waiting for combined native shadow proof; last={last_error}"
    )


def parse_native_snapshot_presentation(text: str) -> list[dict[str, int | float]]:
    """Parse cgame's aggregate canonical transform-promotion diagnostics."""
    rows: list[dict[str, int | float]] = []
    for match in CANONICAL_RENDER_STATUS_RE.finditer(text):
        row: dict[str, int | float] = {}
        for name, value in match.groupdict().items():
            if name.startswith("max_"):
                row[name] = float(value)
            elif name in ("pair_blocks", "renderer_submission_hash"):
                row[name] = int(value, 16)
            else:
                row[name] = int(value)
        rows.append(row)
    return rows


def validate_native_snapshot_presentation(
    row: dict[str, int | float],
) -> dict[str, int | float]:
    """Require real default-off native timeline use at the render boundary."""
    required_positive = (
        "epoch", "clock_frames", "pair_frames", "sample_attempts",
        "native_authority_samples", "promoted_transforms",
        "enumeration_frames", "enumerated_entities",
        "enumerated_removed_entities",
        "previous_only_observed", "previous_only_selected",
        "previous_only_submitted",
        "enumeration_generation_resets", "native_view_render_count",
        "native_view_record_count", "renderer_submission_calls",
        "renderer_submitted_sources", "renderer_submission_hash",
    )
    for name in required_positive:
        value = row.get(name)
        if not isinstance(value, int) or value < 1:
            raise RuntimeError(
                f"native snapshot presentation did not prove {name}; row={row!r}"
            )
    if row.get("mode") != 3:
        raise RuntimeError(f"native snapshot presentation was not promoted; row={row!r}")
    for name in (
        "clock_failures", "pair_failures", "alignment_failures",
        "sample_failures", "parity_mismatches", "native_authority_blocks",
        "event_audit_failures", "enumeration_failures",
    ):
        if row.get(name) != 0:
            raise RuntimeError(
                f"native snapshot presentation observed {name}; row={row!r}"
            )
    if row.get("native_view_result") != 1:
        raise RuntimeError(
            "native snapshot presentation view is not valid; "
            f"row={row!r}"
        )
    if (int(row["native_view_render_count"]) >
            int(row["native_view_record_count"])):
        raise RuntimeError(
            "native snapshot presentation view exceeds its identity union; "
            f"row={row!r}"
        )
    if int(row["renderer_submission_calls"]) < int(
            row["renderer_submitted_sources"]):
        raise RuntimeError(
            "native snapshot renderer submission sources exceed calls; "
            f"row={row!r}"
        )
    observed = int(row["previous_only_observed"])
    selected = int(row["previous_only_selected"])
    submitted = int(row["previous_only_submitted"])
    if observed != int(row["enumerated_removed_entities"]):
        raise RuntimeError(
            "native snapshot previous-only observation disagrees with the "
            f"identity union; row={row!r}"
        )
    if not (submitted <= selected <= observed):
        raise RuntimeError(
            "native snapshot previous-only renderer evidence is not ordered; "
            f"row={row!r}"
        )
    if row["promoted_transforms"] != row["native_authority_samples"]:
        raise RuntimeError(
            "native snapshot presentation did not promote every native sample; "
            f"row={row!r}"
        )
    classified_frames = sum(
        int(row.get(name, 0))
        for name in (
            "interpolation_frames", "extrapolation_frames", "clamped_frames",
        )
    )
    if classified_frames < 1 or classified_frames > row["pair_frames"]:
        raise RuntimeError(
            "native snapshot presentation has invalid timeline mode totals; "
            f"row={row!r}"
        )
    extrapolation_frames = int(row.get("extrapolation_frames", 0))
    extrapolation_time_us = int(row.get("extrapolation_time_us", 0))
    if ((extrapolation_frames == 0) != (extrapolation_time_us == 0) or
            extrapolation_time_us > extrapolation_frames * 50_000):
        raise RuntimeError(
            "native snapshot presentation exceeded the bounded extrapolation "
            f"telemetry contract; row={row!r}"
        )
    for name in (
        "max_origin_error", "max_old_origin_error", "max_angle_error",
    ):
        value = row.get(name)
        if not isinstance(value, float) or value > 0.125:
            raise RuntimeError(
                f"native snapshot presentation exceeded epsilon for {name}; "
                f"row={row!r}"
            )
    return row


def parse_native_snapshot_adaptive(text: str) -> list[dict[str, int]]:
    """Parse cgame's adaptive interpolation-delay diagnostics."""
    return [
        {name: int(value) for name, value in match.groupdict().items()}
        for match in CANONICAL_ADAPTIVE_STATUS_RE.finditer(text)
    ]


def validate_native_snapshot_adaptive(
    row: dict[str, int],
) -> dict[str, int]:
    """Require live, bounded adaptive-delay activity for mode-3 rendering."""
    if row.get("enabled") != 1:
        raise RuntimeError(
            f"native snapshot adaptive interpolation is disabled; row={row!r}"
        )
    if (row.get("baseline_delay_us") != 50_000 or
            row.get("maximum_delay_us") != 150_000):
        raise RuntimeError(
            "native snapshot adaptive interpolation bounds changed; "
            f"row={row!r}"
        )
    delay = row.get("delay_us")
    if not isinstance(delay, int) or not (50_000 <= delay <= 150_000):
        raise RuntimeError(
            "native snapshot adaptive delay escaped its bounds; "
            f"row={row!r}"
        )
    if row.get("cadence_us", 0) < 1 or row.get("rise_adjustments", 0) < 1:
        raise RuntimeError(
            "native snapshot adaptive delay observed no live arrival/rise; "
            f"row={row!r}"
        )
    adjustment = row.get("adjustment")
    if not isinstance(adjustment, int) or not 0 <= adjustment <= 5:
        raise RuntimeError(
            f"native snapshot adaptive adjustment is invalid; row={row!r}"
        )
    if row.get("reset_count", 0) < 1 or row.get("failures") != 0:
        raise RuntimeError(
            "native snapshot adaptive lifecycle is invalid; "
            f"row={row!r}"
        )
    return row


def validate_native_snapshot_presentation_baseline(
    render: dict[str, int | float], adaptive: dict[str, int],
) -> dict[str, object]:
    """Validate a pre-action mode-3 row without requiring action deltas."""
    if (render.get("mode") != 3 or
            not isinstance(render.get("epoch"), int) or
            int(render["epoch"]) < 1):
        raise RuntimeError(
            f"native snapshot presentation baseline is not active; row={render!r}"
        )
    for name in (
        "clock_failures", "pair_failures", "alignment_failures",
        "sample_failures", "parity_mismatches", "native_authority_blocks",
        "event_audit_failures", "enumeration_failures",
    ):
        if render.get(name) != 0:
            raise RuntimeError(
                "native snapshot presentation baseline observed "
                f"{name}; row={render!r}"
            )
    if render.get("native_view_result") != 1:
        raise RuntimeError(
            f"native snapshot presentation baseline view is invalid; row={render!r}"
        )
    observed = int(render.get("previous_only_observed", -1))
    selected = int(render.get("previous_only_selected", -1))
    submitted = int(render.get("previous_only_submitted", -1))
    if (observed < 0 or submitted > selected or selected > observed or
            observed != int(render.get("enumerated_removed_entities", -2))):
        raise RuntimeError(
            "native snapshot presentation baseline previous-only evidence "
            f"is invalid; row={render!r}"
        )
    if (adaptive.get("enabled") != 1 or
            adaptive.get("baseline_delay_us") != 50_000 or
            adaptive.get("maximum_delay_us") != 150_000 or
            adaptive.get("failures") != 0):
        raise RuntimeError(
            f"native snapshot adaptive baseline is invalid; row={adaptive!r}"
        )
    baseline_delay = adaptive.get("delay_us")
    baseline_adjustment = adaptive.get("adjustment")
    if (not isinstance(baseline_delay, int) or
            not 50_000 <= baseline_delay <= 150_000 or
            not isinstance(baseline_adjustment, int) or
            not 0 <= baseline_adjustment <= 5):
        raise RuntimeError(
            f"native snapshot adaptive baseline is out of bounds; row={adaptive!r}"
        )
    return {"render": render, "adaptive": adaptive}


def validate_native_snapshot_presentation_delta(
    baseline_render: dict[str, int | float],
    render: dict[str, int | float],
    baseline_adaptive: dict[str, int],
    adaptive: dict[str, int],
) -> dict[str, object]:
    """Require renderer evidence produced after the fixture action."""
    if render.get("epoch") != baseline_render.get("epoch"):
        raise RuntimeError(
            "native snapshot presentation epoch changed across the action; "
            f"baseline={baseline_render!r} row={render!r}"
        )
    previous_delta: dict[str, int] = {}
    for short_name, field in (
        ("observed", "previous_only_observed"),
        ("selected", "previous_only_selected"),
        ("submitted", "previous_only_submitted"),
    ):
        before = int(baseline_render.get(field, -1))
        after = int(render.get(field, -1))
        if before < 0 or after < before:
            raise RuntimeError(
                "native snapshot previous-only evidence regressed across "
                f"the action; field={field} baseline={baseline_render!r} "
                f"row={render!r}"
            )
        previous_delta[short_name] = after - before
    if (previous_delta["submitted"] < 1 or
            not (previous_delta["submitted"] <=
                 previous_delta["selected"] <=
                 previous_delta["observed"])):
        raise RuntimeError(
            "native snapshot action produced no ordered previous-only "
            f"renderer delta; delta={previous_delta!r}"
        )

    adaptive_delta: dict[str, int] = {}
    for short_name, field in (
        ("rises", "rise_adjustments"),
        ("recoveries", "recovery_adjustments"),
        ("pressure", "pressure_observations"),
        ("resets", "reset_count"),
    ):
        before = int(baseline_adaptive.get(field, -1))
        after = int(adaptive.get(field, -1))
        if before < 0 or after < before:
            raise RuntimeError(
                "native snapshot adaptive evidence regressed across the "
                f"action; field={field} baseline={baseline_adaptive!r} "
                f"row={adaptive!r}"
            )
        adaptive_delta[short_name] = after - before
    return {
        "previous_only": previous_delta,
        "adaptive_activity": adaptive_delta,
    }


def wait_for_native_snapshot_presentation_baseline(
    process: subprocess.Popen[str], path: Path, timeout: float,
) -> dict[str, object]:
    """Capture the latest healthy native presentation row before firing."""
    deadline = time.monotonic() + timeout
    last_error: RuntimeError | None = None
    while time.monotonic() < deadline:
        text = read_text(path)
        render_rows = parse_native_snapshot_presentation(text)
        adaptive_rows = parse_native_snapshot_adaptive(text)
        if render_rows and adaptive_rows:
            try:
                return validate_native_snapshot_presentation_baseline(
                    render_rows[-1], adaptive_rows[-1]
                )
            except RuntimeError as error:
                last_error = error
        if process.poll() is not None:
            raise RuntimeError(
                "hidden presentation client exited before its pre-action "
                f"baseline: code={process.returncode} error={last_error}"
            )
        time.sleep(0.05)
    raise RuntimeError(
        "timed out waiting for native snapshot presentation baseline; "
        f"error={last_error}"
    )


def wait_for_native_snapshot_presentation(
    process: subprocess.Popen[str], path: Path, timeout: float,
    baseline: dict[str, object],
) -> dict[str, object]:
    """Wait for the hidden target renderer to promote a remote native sample."""
    deadline = time.monotonic() + timeout
    last_render: dict[str, int | float] | None = None
    last_adaptive: dict[str, int] | None = None
    last_error: RuntimeError | None = None
    while time.monotonic() < deadline:
        text = read_text(path)
        render_rows = parse_native_snapshot_presentation(text)
        adaptive_rows = parse_native_snapshot_adaptive(text)
        if render_rows:
            last_render = render_rows[-1]
        if adaptive_rows:
            last_adaptive = adaptive_rows[-1]
        if last_render is not None and last_adaptive is not None:
            try:
                render = validate_native_snapshot_presentation(last_render)
                adaptive = validate_native_snapshot_adaptive(last_adaptive)
                baseline_render = baseline.get("render")
                baseline_adaptive = baseline.get("adaptive")
                if (not isinstance(baseline_render, dict) or
                        not isinstance(baseline_adaptive, dict)):
                    raise RuntimeError(
                        "native snapshot presentation baseline is malformed"
                    )
                return {
                    "baseline": baseline,
                    "render": render,
                    "adaptive": adaptive,
                    "delta": validate_native_snapshot_presentation_delta(
                        baseline_render, render,
                        baseline_adaptive, adaptive,
                    ),
                }
            except RuntimeError as error:
                last_error = error
        if process.poll() is not None:
            raise RuntimeError(
                "hidden presentation client exited before native transform "
                f"promotion: code={process.returncode} "
                f"render={last_render!r} adaptive={last_adaptive!r}"
            )
        time.sleep(0.05)
    raise RuntimeError(
        "timed out waiting for native snapshot presentation promotion; "
        f"render={last_render!r} adaptive={last_adaptive!r} "
        f"error={last_error}"
    )


def wait_for_client_user_ids(
    port: int, timeout: float, *, require_spectator: bool = False,
) -> tuple[int, int, int | None, list[str]]:
    deadline = time.monotonic() + timeout
    responses: list[str] = []
    while time.monotonic() < deadline:
        response = rcon_command(port, "status", min(1.0, timeout))
        responses.append(response)
        try:
            user_ids = admitted_fixture_user_ids(
                response, require_spectator=require_spectator,
            )
            spectator_user_id = user_ids[2] if require_spectator else None
            return user_ids[0], user_ids[1], spectator_user_id, responses
        except RuntimeError:
            time.sleep(0.10)
    expected = "three" if require_spectator else "two"
    raise RuntimeError(
        f"timed out waiting for {expected} admitted real canonical-rail clients"
    )


def stuff_client_until_marker(
    port: int, user_id: int, client_command: str,
    process: subprocess.Popen[str], path: Path, marker: str, timeout: float,
) -> list[str]:
    """Retry an idempotent client fixture command until its exact log latch."""
    deadline = time.monotonic() + timeout
    target_count = read_text(path).count(marker) + 1
    responses: list[str] = []
    while time.monotonic() < deadline:
        responses.append(rcon_command(
            port, f'stuff {user_id} "{client_command}"',
            min(1.0, max(0.05, deadline - time.monotonic())),
        ))
        retry_at = min(deadline, time.monotonic() + 0.5)
        while time.monotonic() < retry_at:
            if read_text(path).count(marker) >= target_count:
                return responses
            if process.poll() is not None:
                raise RuntimeError(
                    "headless client exited before stuffed marker "
                    f"{marker!r}: code={process.returncode}"
                )
            time.sleep(0.05)
    raise RuntimeError(
        f"timed out waiting for stuffed marker {marker!r}"
    )


def wait_for_native_event_probe_samples(
    port: int,
    user_id: int,
    process: subprocess.Popen[str],
    path: Path,
    timeout: float,
    *,
    require_activity: bool | None,
) -> tuple[dict[str, int], list[str]]:
    """Require two later, stable newest rows from one live cgame."""
    responses: list[str] = []
    deadline = time.monotonic() + timeout
    last_error: RuntimeError | None = None
    candidate: dict[str, int] | None = None
    latest_observed: dict[str, int] | None = None
    while time.monotonic() < deadline:
        before = len(parse_native_event_probe_status_rows(read_text(path)))
        command_timeout = min(
            1.0, max(0.05, deadline - time.monotonic())
        )
        try:
            response = rcon_command_once(
                port,
                f'stuff {user_id} "{NATIVE_EVENT_PROBE_CLIENT_COMMAND}"',
                command_timeout,
            )
        except RuntimeError as error:
            # The one UDP reply may be impaired even when the server accepted
            # the single-send command. The client logfile remains authoritative.
            response = f"single-send RCON reply missing: {error}"
            last_error = error
        responses.append(response)
        try:
            wait_for_marker_count(
                process, path, NATIVE_EVENT_PROBE_STATUS_MARKER,
                before + 1,
                min(1.0, max(0.05, deadline - time.monotonic())),
            )
        except NativeApplicationRejectionError:
            raise
        except RuntimeError as error:
            last_error = error
            if process.poll() is not None:
                raise RuntimeError(
                    "native event probe client exited before status became valid"
                ) from error
            continue
        rows = parse_native_event_probe_status_rows(read_text(path))
        appended = rows[before:]
        if not appended:
            last_error = RuntimeError(
                "native event probe status command appended no parseable rows"
            )
            continue
        latest_observed = appended[-1]
        try:
            newest = validate_native_event_probe_status(
                latest_observed, require_activity=require_activity,
            )
        except RuntimeError as error:
            last_error = error
            candidate = None
        else:
            if candidate is not None and newest == candidate:
                return newest, responses
            candidate = newest
            last_error = RuntimeError(
                "native event probe has not yet repeated its newest valid row"
            )
        if process.poll() is not None:
            raise RuntimeError(
                "native event probe client exited before status became valid"
            )
        time.sleep(0.05)
    raise RuntimeError(
        "timed out waiting for two stable native event probe samples; "
        f"newest={latest_observed!r} stable_candidate={candidate!r} "
        f"last={last_error}"
    )


def issue_native_event_probe_checkpoint(
    port: int,
    user_id: int,
    process: subprocess.Popen[str],
    path: Path,
    timeout: float,
    pre_checkpoint_status: dict[str, int],
) -> tuple[dict[str, object], list[str]]:
    """Apply and idempotently repeat one map-scoped telemetry checkpoint."""
    map_generation = pre_checkpoint_status["map_generation"]
    authority_epoch = pre_checkpoint_status["authority_epoch"]
    checkpoint_id = (map_generation << 32) | authority_epoch
    if not 0 < checkpoint_id <= 0xffffffffffffffff:
        raise RuntimeError("native event probe checkpoint identity is invalid")
    command = (
        f"{NATIVE_EVENT_PROBE_CHECKPOINT_CLIENT_COMMAND} "
        f"{map_generation} {authority_epoch} {checkpoint_id}"
    )
    responses: list[str] = []

    def issue(
        *, fresh: bool, issue_timeout: float,
    ) -> tuple[dict[str, int] | None, list[dict[str, int]]]:
        before = len(parse_native_event_probe_checkpoint_rows(read_text(path)))
        try:
            response = rcon_command_once(
                port,
                f'stuff {user_id} "{command}"',
                issue_timeout,
            )
        except RuntimeError as error:
            response = f"single-send RCON reply missing: {error}"
        responses.append(response)
        wait_for_marker_count(
            process,
            path,
            NATIVE_EVENT_PROBE_CHECKPOINT_MARKER,
            before + 1,
            issue_timeout,
        )
        rows = parse_native_event_probe_checkpoint_rows(read_text(path))
        appended = rows[before:]
        if not appended:
            raise RuntimeError(
                "native event probe checkpoint appended no parseable receipt"
            )
        selected: dict[str, int] | None = None
        saw_already = False
        saw_busy = False
        for row in appended:
            result = row.get("result")
            if fresh and result == NATIVE_EVENT_PROBE_CHECKPOINT_BUSY:
                if selected is not None or saw_already:
                    raise RuntimeError(
                        "native event probe fresh checkpoint emitted busy after "
                        "checkpoint application"
                    )
                validate_native_event_probe_checkpoint(
                    row,
                    expected_result=NATIVE_EVENT_PROBE_CHECKPOINT_BUSY,
                    expected_map_generation=map_generation,
                    expected_authority_epoch=authority_epoch,
                    expected_checkpoint_id=checkpoint_id,
                )
                saw_busy = True
                continue
            if fresh and result == NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED:
                if selected is not None or saw_already:
                    raise RuntimeError(
                        "native event probe fresh checkpoint emitted a later "
                        "or duplicate applied receipt"
                    )
                selected = validate_native_event_probe_checkpoint(
                    row,
                    expected_result=NATIVE_EVENT_PROBE_CHECKPOINT_APPLIED,
                    expected_map_generation=map_generation,
                    expected_authority_epoch=authority_epoch,
                    expected_checkpoint_id=checkpoint_id,
                )
                continue
            if result == NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED:
                if fresh and selected is None:
                    raise RuntimeError(
                        "native event probe fresh checkpoint emitted already-applied "
                        "before its applied receipt"
                    )
                validate_native_event_probe_checkpoint(
                    row,
                    expected_result=NATIVE_EVENT_PROBE_CHECKPOINT_ALREADY_APPLIED,
                    expected_map_generation=map_generation,
                    expected_authority_epoch=authority_epoch,
                    expected_checkpoint_id=checkpoint_id,
                )
                saw_already = True
                if not fresh:
                    selected = row
                continue
            raise RuntimeError(
                "native event probe checkpoint emitted an unexpected result; "
                f"fresh={fresh} row={row!r}"
            )
        if selected is None:
            if fresh and saw_busy:
                return None, appended
            expected = "applied" if fresh else "already-applied"
            raise RuntimeError(
                f"native event probe checkpoint batch lacked {expected} evidence"
            )
        return selected, appended

    apply_deadline = time.monotonic() + timeout
    busy_receipt_batches: list[list[dict[str, int]]] = []
    while True:
        remaining = apply_deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError(
                "timed out waiting for native event probe checkpoint authority "
                "to drain after BUSY receipts; "
                f"busy_batches={busy_receipt_batches!r}"
            )
        applied, applied_batch = issue(
            fresh=True, issue_timeout=remaining,
        )
        if applied is not None:
            break
        busy_receipt_batches.append(applied_batch)
        remaining = apply_deadline - time.monotonic()
        if remaining <= 0:
            continue
        time.sleep(min(0.05, remaining))

    duplicate, duplicate_batch = issue(
        fresh=False, issue_timeout=timeout,
    )
    if duplicate is None:
        raise RuntimeError(
            "native event probe duplicate checkpoint lacked a receipt"
        )
    identity_fields = tuple(
        name for name in NATIVE_EVENT_PROBE_CHECKPOINT_FIELDS
        if name != "result"
    )
    if any(applied[name] != duplicate[name] for name in identity_fields):
        raise RuntimeError(
            "native event probe duplicate checkpoint changed receipt identity; "
            f"applied={applied!r} duplicate={duplicate!r}"
        )
    return {
        "command": command,
        "checkpoint_id": checkpoint_id,
        "busy_receipt_batches": busy_receipt_batches,
        "applied_receipt": applied,
        "applied_receipt_batch": applied_batch,
        "duplicate_receipt": duplicate,
        "duplicate_receipt_batch": duplicate_batch,
    }, responses


def wait_for_native_event_sender_baseline(
    port: int,
    slot: int,
    timeout: float,
) -> tuple[dict[str, int], list[str]]:
    """Capture one closed cumulative sender image before fixture workload."""
    deadline = time.monotonic() + timeout
    responses: list[str] = []
    last_error: RuntimeError | None = None
    latest_row: dict[str, int] | None = None
    while time.monotonic() < deadline:
        try:
            response = rcon_command(
                port, f"sv_worr_native_shadow_status {slot}",
                min(1.0, max(0.05, deadline - time.monotonic())),
            )
        except RuntimeError as error:
            response = f"native event sender RCON reply missing: {error}"
            responses.append(response)
            last_error = error
            time.sleep(0.05)
            continue
        responses.append(response)
        rows = parse_native_event_sender_status_rows(response)
        if not rows:
            last_error = RuntimeError(
                "missing native event sender baseline evidence from "
                "sv_worr_native_shadow_status"
            )
        elif any(row != rows[-1] for row in rows[:-1]):
            raise RuntimeError(
                "native event sender baseline command emitted conflicting "
                f"rows; rows={rows!r}"
            )
        else:
            # Connectionless status replies cross the same impairment layer
            # as the stream under test, so one datagram can be duplicated.
            # Collapse only byte-semantically identical parsed rows; distinct
            # cumulative images remain an ambiguous fail-closed response.
            latest_row = rows[-1]
            try:
                return validate_native_event_sender_baseline(
                    latest_row, expected_slot=slot,
                ), responses
            except RuntimeError as error:
                last_error = error
        time.sleep(0.05)
    raise RuntimeError(
        "timed out waiting for a closed native event sender baseline; "
        f"latest={latest_row!r} last={last_error}"
    )


def wait_for_native_event_sender_status(
    port: int,
    slot: int,
    timeout: float,
    baseline: dict[str, int],
    expected_events: int | None,
    *,
    require_schema2_event_batch: bool = False,
    minimum_schema2_event_batch_events: int = 2,
    require_schema2_mixed_singletons: bool = False,
) -> tuple[dict[str, int], dict[str, object], list[str]]:
    """Poll one unchanged sender stream for exact post-baseline closure."""
    deadline = time.monotonic() + timeout
    responses: list[str] = []
    last_error: RuntimeError | None = None
    latest_row: dict[str, int] | None = None
    while time.monotonic() < deadline:
        try:
            response = rcon_command(
                port, f"sv_worr_native_shadow_status {slot}",
                min(1.0, max(0.05, deadline - time.monotonic())),
            )
        except RuntimeError as error:
            response = f"native event sender RCON reply missing: {error}"
            responses.append(response)
            last_error = error
            time.sleep(0.05)
            continue
        responses.append(response)
        rows = parse_native_event_sender_status_rows(response)
        if not rows:
            last_error = RuntimeError(
                "missing native event sender evidence from "
                "sv_worr_native_shadow_status"
            )
        elif any(row != rows[-1] for row in rows[:-1]):
            raise RuntimeError(
                "native event sender status command emitted conflicting "
                f"rows; rows={rows!r}"
            )
        else:
            latest_row = rows[-1]
            if latest_row["stream_epoch"] != baseline["stream_epoch"]:
                raise RuntimeError(
                    "native event sender stream rotated while polling the "
                    "checkpointed workload; "
                    f"baseline={baseline['stream_epoch']} "
                    f"post={latest_row['stream_epoch']}"
                )
            regressions = {
                name: (baseline[name], latest_row[name])
                for name in NATIVE_EVENT_SENDER_COUNTER_FIELDS
                if latest_row[name] < baseline[name]
            }
            if regressions:
                raise RuntimeError(
                    "native event sender counters regressed while polling one "
                    f"stream; regressions={regressions!r}"
                )
            try:
                validated, delta = validate_native_event_sender_status(
                    latest_row, baseline=baseline, expected_slot=slot,
                    expected_events=expected_events,
                    require_schema2_event_batch=require_schema2_event_batch,
                    minimum_schema2_event_batch_events=(
                        minimum_schema2_event_batch_events
                    ),
                    require_schema2_mixed_singletons=(
                        require_schema2_mixed_singletons
                    ),
                )
                return validated, delta, responses
            except RuntimeError as error:
                last_error = error
        time.sleep(0.05)
    raise RuntimeError(
        "timed out waiting for native event sender evidence; "
        f"baseline={baseline!r} latest={latest_row!r} last={last_error}"
    )


def capture_client_impairment_status(
    port: int,
    user_id: int,
    process: subprocess.Popen[str],
    path: Path,
    timeout: float,
) -> tuple[dict[str, object], list[str]]:
    baseline = read_text(path)
    try:
        response = rcon_command_once(
            port, f'stuff {user_id} "net_impair_status"', timeout,
        )
    except RuntimeError as error:
        response = f"single-send RCON reply missing: {error}"
    responses = [response]
    wait_for_marker_count(
        process, path, "net_impair counters:",
        baseline.count("net_impair counters:") + 1, timeout,
    )
    appended = read_text(path)[len(baseline):]
    config, counters = parse_native_event_probe_impairment(appended)
    return validate_native_event_probe_impairment(
        config, counters,
        expected_seed=NATIVE_EVENT_PROBE_CLIENT_IMPAIR_SEED,
        expected_profile=NATIVE_EVENT_PROBE_CLIENT_IMPAIR_PROFILE,
    ), responses


def arm_native_event_probe_impairment(
    port: int, target_user_id: int, timeout: float,
) -> list[str]:
    """Enable the preconfigured delayed-ACK retry profile after readiness."""
    # The client toggle is queued while the server path is still unimpaired.
    # Reliable ordering then puts the following local status request after the
    # toggle.  The server endpoint is armed second; both exact configurations
    # are sampled before the shot opens its telemetry window.
    return [
        rcon_command(
            port,
            f'stuff {target_user_id} "net_impair_enable 1"',
            timeout,
        ),
        rcon_command(port, "net_impair_enable 1", timeout),
    ]


def capture_server_impairment_status(
    port: int, timeout: float,
) -> tuple[dict[str, object], list[str]]:
    # This diagnostic is returned directly in the RCON reply rather than
    # appended to a client log, so a bounded resend cannot duplicate evidence.
    response = rcon_command(port, "net_impair_status", timeout)
    config, counters = parse_native_event_probe_impairment(response)
    return validate_native_event_probe_impairment(
        config, counters,
        expected_seed=NATIVE_EVENT_PROBE_SERVER_IMPAIR_SEED,
        expected_profile=NATIVE_EVENT_PROBE_SERVER_IMPAIR_PROFILE,
    ), [response]


def run_native_event_probe_phase(
    *,
    port: int,
    timeout: float,
    mode: dict[str, int | str | bool],
    shooter: subprocess.Popen[str],
    target: subprocess.Popen[str],
    shooter_path: Path,
    target_path: Path,
    shooter_user_id: int,
    target_user_id: int,
    prior_native_server_peers: Sequence[dict[str, int | str]] | None = None,
) -> tuple[dict[str, object], list[str]]:
    """Run one real Blaster phase and close client/server event evidence."""
    responses: list[str] = []
    native_status, native_responses = wait_for_native_base_shadow(
        port, shooter, target, shooter_path, target_path, timeout,
        NATIVE_EVENT_PROBE_PRIVATE_MASK, ("shooter", "target"),
        (shooter_user_id, target_user_id),
        prior_native_server_peers,
    )
    responses.extend(native_responses)

    # Arm and settle the ordinary fixture before opening the telemetry window.
    # A delayed team admission may emit legitimate spawn events; checkpointing
    # only after readiness keeps those lifecycle effects outside the shot.
    responses.append(rcon_command(
        port, f"sv {mode['arm_command']}", timeout,
    ))
    _ready, ready_responses = wait_for_fixture_ready(
        port, timeout, mode, shooter_user_id, target_user_id,
    )
    responses.extend(ready_responses)

    # Close every target-slot EVENT record emitted by team admission/login
    # before asking the target cgame to reset its comparable counters. A
    # client-only readiness check cannot rule out an EVENT record that is
    # still retained or in flight on the producer side.
    sender_pre_checkpoint, sender_pre_checkpoint_responses = (
        wait_for_native_event_sender_baseline(
            port, target_user_id, timeout,
        )
    )
    responses.extend(sender_pre_checkpoint_responses)

    # Every map phase creates an explicit target telemetry window. The
    # pre-checkpoint row is intentionally allowed to contain prior lifecycle
    # activity; the applied/duplicate receipts and later stable zero row prove
    # the reset.
    pre_checkpoint, pre_checkpoint_responses = (
        wait_for_native_event_probe_samples(
            port, target_user_id, target, target_path, timeout,
            require_activity=None,
        )
    )
    responses.extend(pre_checkpoint_responses)
    checkpoint, checkpoint_responses = issue_native_event_probe_checkpoint(
        port, target_user_id, target, target_path, timeout,
        pre_checkpoint,
    )
    responses.extend(checkpoint_responses)
    baseline, baseline_responses = wait_for_native_event_probe_samples(
        port, target_user_id, target, target_path, timeout,
        require_activity=False,
    )
    responses.extend(baseline_responses)
    sender_baseline, sender_baseline_responses = (
        wait_for_native_event_sender_baseline(
            port, target_user_id, timeout,
        )
    )
    responses.extend(sender_baseline_responses)
    sender_pre_signature = native_event_sender_checkpoint_signature(
        sender_pre_checkpoint,
    )
    sender_baseline_signature = native_event_sender_checkpoint_signature(
        sender_baseline,
    )
    if sender_baseline_signature != sender_pre_signature:
        raise RuntimeError(
            "native event sender changed across the client checkpoint; "
            f"before={sender_pre_signature!r} "
            f"after={sender_baseline_signature!r}"
        )

    responses.append(rcon_command(
        port, f'stuff {shooter_user_id} "{mode.get("input_command", "+attack")}"',
        timeout,
    ))
    status, _status_responses = wait_for_status(
        port, timeout, mode, shooter_user_id, responses,
    )

    # Close the target's server sender first. If the real Blaster path produced
    # no native candidate, surface that exact absence instead of masking it
    # behind a later client-probe timeout.
    sender, sender_delta, sender_responses = (
        wait_for_native_event_sender_status(
            port, target_user_id, timeout, sender_baseline,
            NATIVE_EVENT_PROBE_EXPECTED_EVENTS,
            require_schema2_event_batch=bool(
                mode.get("require_schema2_event_batch", False)
            ),
            minimum_schema2_event_batch_events=int(
                mode.get("minimum_schema2_event_batch_events", 2)
            ),
            require_schema2_mixed_singletons=bool(
                mode.get("require_schema2_mixed_singletons", False)
            ),
        )
    )
    responses.extend(sender_responses)
    completed, probe_responses = wait_for_native_event_probe_samples(
        port, target_user_id, target, target_path, timeout,
        require_activity=True,
    )
    responses.extend(probe_responses)
    sender_counters = sender_delta.get("counters")
    if not isinstance(sender_counters, dict):
        raise RuntimeError("native event sender delta evidence is malformed")
    sender_events = sender_counters.get("candidates_queued")
    if sender_events != completed["raw_action_records"]:
        raise RuntimeError(
            "native event client/server checkpoint-window counts differ; "
            f"sender={sender_events!r} "
            f"client={completed['raw_action_records']}"
        )
    lifecycle_scope = validate_native_event_probe_phase_scope(
        pre_checkpoint, checkpoint, baseline, completed,
    )
    return {
        "canonical_status": validate_status(status, mode),
        "pre_checkpoint_status": pre_checkpoint,
        "checkpoint": checkpoint,
        "zero_baseline": baseline,
        "lifecycle_scope": lifecycle_scope,
        "probe_status": completed,
        "native_status": native_status,
        "event_sender_pre_checkpoint": sender_pre_checkpoint,
        "event_sender_checkpoint_signature": sender_baseline_signature,
        "event_sender_baseline": sender_baseline,
        "event_sender_status": sender,
        "event_sender_delta": sender_delta,
    }, responses


def reload_native_event_probe_map(
    *,
    port: int,
    timeout: float,
    server: subprocess.Popen[str],
    shooter: subprocess.Popen[str],
    target: subprocess.Popen[str],
    server_path: Path,
    shooter_path: Path,
    target_path: Path,
    expected_user_ids: tuple[int, int],
) -> list[str]:
    """Reload the same map without replacing any of the three processes."""
    server_maps = read_text(server_path).count(f"SpawnServer: {MAP_NAME}")
    shooter_serverdata = read_text(shooter_path).count(
        "Serverdata packet received"
    )
    target_serverdata = read_text(target_path).count(
        "Serverdata packet received"
    )
    # Same-process map reuse is deliberately non-idempotent: replaying it after
    # a lost RCON reply would manufacture an extra EndMap/BeginMap pair and hide
    # the lifecycle property this row exists to measure. Dedicated servers
    # default sv_allow_map=0 and correctly reject `map` while separately
    # connected loopback-IP clients are present; `gamemap` is the supported
    # in-session transition and preserves all three processes.
    if server_maps != 1:
        raise RuntimeError(
            "same-map native event reload did not begin from exactly one "
            f"server spawn marker; observed={server_maps}"
        )
    transition_response = rcon_command_once(
        port, f"gamemap {MAP_NAME}", timeout,
    )
    responses = [transition_response]
    # The map command executes inside SVC_RemoteCommand's packet-redirection
    # scope, so SV_SpawnServer's print is returned in the RCON datagram rather
    # than appended to server.stdout. Require that direct server-side marker
    # exactly once, then independently require both existing client processes
    # to accept one more serverdata packet.
    spawn_marker = f"SpawnServer: {MAP_NAME}"
    transition_spawns = transition_response.count(spawn_marker)
    if transition_spawns != 1:
        raise RuntimeError(
            "same-map native event gamemap reply did not contain exactly one "
            f"server spawn marker; observed={transition_spawns} "
            f"reply={transition_response!r}"
        )
    if server.poll() is not None:
        raise RuntimeError(
            "dedicated server exited during same-map native event reload; "
            f"code={server.returncode}"
        )
    wait_for_marker_count(
        shooter, shooter_path, "Serverdata packet received",
        shooter_serverdata + 1, timeout,
    )
    wait_for_marker_count(
        target, target_path, "Serverdata packet received",
        target_serverdata + 1, timeout,
    )
    if server.poll() is not None:
        raise RuntimeError(
            "dedicated server exited after same-map native event reload; "
            f"code={server.returncode}"
        )
    shooter_id, target_id, spectator_id, status_responses = (
        wait_for_client_user_ids(port, timeout)
    )
    responses.extend(status_responses)
    if spectator_id is not None or (shooter_id, target_id) != expected_user_ids:
        raise RuntimeError(
            "same-map native event reload changed the live client processes/slots"
        )
    # SV_Begin has now requested a post-bootstrap CHALLENGE.  Do not append a
    # runner team/status command to either reliable queue here: the next phase
    # first proves both newer server epochs through queue-neutral RCON status.
    # The fixture-ready contract later proves that the existing FFA admission
    # survived the same-process reload.
    return responses


def terminate(process: subprocess.Popen[str] | None) -> bool:
    return terminate_process_tree(process)


def run_once(
    *, server_command: list[str], shooter_command: list[str], target_command: list[str],
    spectator_command: list[str] | None = None, working_dir: Path,
    run_root: Path, timeout: float, mode: dict[str, int | str | bool],
) -> dict[str, object]:
    paths = {role: run_root / f"{role}.log" for role in (
        "server.stdout", "server.stderr", "shooter.stdout", "shooter.stderr",
        "target.stdout", "target.stderr", "spectator.stdout", "spectator.stderr",
        "target.openal", "server.rcon"
    )}
    server: subprocess.Popen[str] | None = None
    shooter: subprocess.Popen[str] | None = None
    target: subprocess.Popen[str] | None = None
    spectator: subprocess.Popen[str] | None = None
    server_terminated = shooter_terminated = target_terminated = False
    spectator_terminated = False
    required_client_count = int(mode.get("required_client_count", 2))
    if (spectator_command is None) != (required_client_count == 2):
        raise RuntimeError("spectator command does not match the required client count")
    rcon_log: list[str] = []
    reconnect = {
        "required": bool(mode.get("require_in_session_reconnect", False)),
        "server_admissions": required_client_count,
        "shooter_serverdata_packets": 1,
    }
    spectator_exclusion: dict[str, object] = {
        "required": bool(mode.get("require_spectator_exclusion", False)),
        "roster_size": required_client_count,
        "team_verified_before_fire": False,
        "team_verified_after_fire": False,
        "spectator_undamaged": False,
    }
    local_action_parity: dict[str, int] | None = None
    legacy_capability_status: dict[str, object] | None = None
    native_event_shadow: dict[str, object] | None = None
    combined_native_preflight: dict[str, object] | None = None
    combined_native_shadow: dict[str, object] | None = None
    native_snapshot_shadow: dict[str, object] | None = None
    native_snapshot_presentation_baseline: dict[str, object] | None = None
    native_snapshot_presentation: dict[str, object] | None = None
    native_event_probe_map_reuse: dict[str, object] | None = None
    native_event_probe_pre_impairment: dict[str, object] | None = None
    native_event_probe_audio: dict[str, object] | None = None
    target_environment = None
    if mode.get("require_native_event_probe_map_reuse", False):
        target_environment = os.environ.copy()
        target_environment.pop("SDL_AUDIODRIVER", None)
        target_environment["ALSOFT_DRIVERS"] = "null"
        target_environment["ALSOFT_LOGLEVEL"] = "3"
        target_environment["ALSOFT_LOGFILE"] = str(
            paths["target.openal"].resolve()
        )
    try:
        with contextlib.ExitStack() as files:
            server_out = files.enter_context(paths["server.stdout"].open("w", encoding="utf-8"))
            server_err = files.enter_context(paths["server.stderr"].open("w", encoding="utf-8"))
            shooter_out = files.enter_context(paths["shooter.stdout"].open("w", encoding="utf-8"))
            shooter_err = files.enter_context(paths["shooter.stderr"].open("w", encoding="utf-8"))
            target_out = files.enter_context(paths["target.stdout"].open("w", encoding="utf-8"))
            target_err = files.enter_context(paths["target.stderr"].open("w", encoding="utf-8"))
            spectator_out = spectator_err = None
            if spectator_command is not None:
                spectator_out = files.enter_context(
                    paths["spectator.stdout"].open("w", encoding="utf-8")
                )
                spectator_err = files.enter_context(
                    paths["spectator.stderr"].open("w", encoding="utf-8")
                )
            server = start_headless_process(
                server_command, cwd=working_dir, stdin=subprocess.DEVNULL,
                stdout=server_out, stderr=server_err, text=True,
                creationflags=creation_flags(),
            )
            wait_for_marker(server, paths["server.stdout"], f"SpawnServer: {MAP_NAME}", timeout)
            shooter = start_headless_process(
                shooter_command, cwd=working_dir, stdin=subprocess.DEVNULL,
                stdout=shooter_out, stderr=shooter_err, text=True,
                creationflags=creation_flags(),
            )
            wait_for_marker(server, paths["server.stdout"], "Going from cs_primed to cs_spawned", timeout)
            target = start_headless_process(
                target_command, cwd=working_dir, stdin=subprocess.DEVNULL,
                stdout=target_out, stderr=target_err, text=True,
                creationflags=creation_flags(),
                env=target_environment,
            )
            if mode.get("require_native_event_probe_map_reuse", False):
                wait_for_marker(
                    target, paths["target.stdout"],
                    "OpenAL initialized.", timeout,
                )
                wait_for_marker(
                    target, paths["target.openal"],
                    NATIVE_EVENT_PROBE_OPENAL_BACKEND_LINE, timeout,
                )
                wait_for_marker(
                    target, paths["target.openal"],
                    NATIVE_EVENT_PROBE_OPENAL_DEVICE, timeout,
                )
            wait_for_marker_count(
                server, paths["server.stdout"], "Going from cs_primed to cs_spawned", 2, timeout
            )
            if spectator_command is not None:
                spectator = start_headless_process(
                    spectator_command, cwd=working_dir, stdin=subprocess.DEVNULL,
                    stdout=spectator_out, stderr=spectator_err, text=True,
                    creationflags=creation_flags(),
                )
                wait_for_marker_count(
                    server, paths["server.stdout"],
                    "Going from cs_primed to cs_spawned", 3, timeout,
                )
            port = int(server_command[server_command.index("net_port") + 1])
            shooter_user_id, target_user_id, spectator_user_id, client_statuses = (
                wait_for_client_user_ids(
                    port, timeout, require_spectator=spectator_command is not None,
                )
            )
            rcon_log.extend(client_statuses)
            if mode.get("require_in_session_reconnect", False):
                initial_shooter_user_id = shooter_user_id
                initial_target_user_id = target_user_id
                # The production minimum-player rule queues intermission after
                # a 200 ms one-player gap. The deliberate reconnect is longer
                # than that, so use the existing cheat-gated developer fixture
                # command to retain the active match. It changes no command,
                # rewind, weapon, damage, or receipt authority and is cleared
                # by the isolated map lifecycle.
                rcon_log.extend(stuff_client_until_marker(
                    port, shooter_user_id, "cmd dev_ready", shooter,
                    paths["shooter.stdout"],
                    "dev_ready: warmup bypass enabled", timeout,
                ))
                rcon_log.append(rcon_command(
                    port, f'stuff {shooter_user_id} "reconnect"', timeout,
                ))
                wait_for_marker_count(
                    server, paths["server.stdout"],
                    "Going from cs_primed to cs_spawned", 3, timeout,
                )
                wait_for_marker_count(
                    shooter, paths["shooter.stdout"],
                    "Serverdata packet received", 2, timeout,
                )
                shooter_user_id, target_user_id, spectator_user_id, reconnected_statuses = (
                    wait_for_client_user_ids(
                        port, timeout, require_spectator=spectator_command is not None,
                    )
                )
                rcon_log.extend(reconnected_statuses)
                if target_user_id != initial_target_user_id:
                    raise RuntimeError(
                        "independent target slot changed during shooter reconnect"
                    )
                reconnect = {
                    "required": True,
                    "server_admissions": 3,
                    "shooter_serverdata_packets": 2,
                    "initial_shooter_user_id": initial_shooter_user_id,
                    "reconnected_shooter_user_id": shooter_user_id,
                    "target_user_id": target_user_id,
                    "minplayer_bypass": "dev_ready",
                }
                # cs_spawned precedes the reconnected cgame's ordinary command
                # handling by a few frames. Let that lifecycle settle before
                # sending the idempotent FFA team choice below.
                time.sleep(0.5)
            # Resolve the ordinary welcome-menu choice before arming. These
            # are real client string commands and cannot create user-command
            # authority or weapon state. ProBall needs two normal team joins;
            # every other mode remains in FFA.
            if mode.get("team_game", False):
                rcon_log.append(rcon_command(
                    port, f'stuff {shooter_user_id} "cmd team red"', timeout,
                ))
                rcon_log.append(rcon_command(
                    port, f'stuff {target_user_id} "cmd team blue"', timeout,
                ))
                time.sleep(0.25)
            else:
                if mode.get("require_spectator_exclusion", False):
                    if spectator is None or spectator_user_id is None:
                        raise RuntimeError(
                            "spectator exclusion requires its third admitted client"
                        )
                    rcon_log.append(rcon_command(
                        port, f'stuff {shooter_user_id} "cmd team free"', timeout,
                    ))
                    rcon_log.append(rcon_command(
                        port, f'stuff {target_user_id} "cmd team free"', timeout,
                    ))
                    rcon_log.extend(stuff_client_until_marker(
                        port, spectator_user_id,
                        "cmd team spectator; cmd team", spectator,
                        paths["spectator.stdout"],
                        SPECTATOR_JOIN_MARKER, timeout,
                    ))
                    spectator_exclusion["team_verified_before_fire"] = True
                    spectator_exclusion["user_id"] = spectator_user_id
                else:
                    rcon_log.append(rcon_command(
                        port, 'stuffall "cmd team free"', timeout,
                    ))
                if mode.get("require_in_session_reconnect", False):
                    # ClientBegin restores an already-joined FFA player during
                    # reconnect, so the repeated team choice is deliberately
                    # silent. Reconnect readiness is proven above by the third
                    # server admission and second client serverdata packet.
                    time.sleep(0.5)
            if mode.get("require_combined_native_shadow", False):
                # Reject a dead native lane before gameplay can obscure the
                # primary transport failure. Both exact peers must already
                # exchange command proof and acknowledged snapshots after the
                # reconnect/team lifecycle; the post-fire proof below remains
                # independently required.
                combined_native_preflight, preflight_responses = (
                    wait_for_combined_native_shadow(
                        port, shooter, target,
                        paths["shooter.stdout"], paths["target.stdout"],
                        timeout, 0x77, ("shooter", "target"),
                        (shooter_user_id, target_user_id),
                    )
                )
                rcon_log.extend(preflight_responses)
            fixture_clients = {
                "shooter": (
                    SHOOTER_NAME, shooter, paths["shooter.stdout"],
                ),
                "target": (
                    TARGET_NAME, target, paths["target.stdout"],
                ),
            }
            if spectator is not None:
                fixture_clients["spectator"] = (
                    SPECTATOR_NAME, spectator, paths["spectator.stdout"],
                )
            (
                disconnect_baseline,
                client_log_size_baseline,
            ) = capture_fixture_liveness_baseline(
                paths["server.stdout"], fixture_clients,
            )
            if mode.get("require_native_event_probe_map_reuse", False):
                # Close native readiness before enabling the delayed-ACK retry
                # profile.
                # The hidden presentation client deliberately runs a much
                # slower renderer cadence; applying 105 ms upstream latency to
                # its bootstrap ACKs can keep reliable fragments in flight for
                # the separate queue deadline without exercising EVENT at all.
                # Both impairment endpoints are still enabled, sampled, and
                # delta-proven before either map phase opens its shot window.
                (
                    native_event_probe_pre_impairment,
                    pre_impairment_responses,
                ) = wait_for_native_base_shadow(
                    port, shooter, target,
                    paths["shooter.stdout"], paths["target.stdout"],
                    timeout, int(mode.get(
                        "native_event_private_mask", 0x73,
                    )), ("shooter", "target"),
                    (shooter_user_id, target_user_id),
                )
                rcon_log.extend(pre_impairment_responses)
                rcon_log.extend(arm_native_event_probe_impairment(
                    port, target_user_id, timeout,
                ))
                (
                    client_impairment_baseline,
                    client_impairment_baseline_responses,
                ) = capture_client_impairment_status(
                    port, target_user_id, target,
                    paths["target.stdout"], timeout,
                )
                rcon_log.extend(client_impairment_baseline_responses)
                (
                    server_impairment_baseline,
                    server_impairment_baseline_responses,
                ) = capture_server_impairment_status(port, timeout)
                rcon_log.extend(server_impairment_baseline_responses)
                first_phase, first_responses = run_native_event_probe_phase(
                    port=port, timeout=timeout, mode=mode,
                    shooter=shooter, target=target,
                    shooter_path=paths["shooter.stdout"],
                    target_path=paths["target.stdout"],
                    shooter_user_id=shooter_user_id,
                    target_user_id=target_user_id,
                )
                rcon_log.extend(first_responses)
                rcon_log.extend(reload_native_event_probe_map(
                    port=port, timeout=timeout, server=server,
                    shooter=shooter, target=target,
                    server_path=paths["server.stdout"],
                    shooter_path=paths["shooter.stdout"],
                    target_path=paths["target.stdout"],
                    expected_user_ids=(shooter_user_id, target_user_id),
                ))
                second_phase, second_responses = run_native_event_probe_phase(
                    port=port, timeout=timeout, mode=mode,
                    shooter=shooter, target=target,
                    shooter_path=paths["shooter.stdout"],
                    target_path=paths["target.stdout"],
                    shooter_user_id=shooter_user_id,
                    target_user_id=target_user_id,
                    prior_native_server_peers=first_phase[
                        "native_status"
                    ]["server_peers"],
                )
                rcon_log.extend(second_responses)
                lifecycle = validate_native_event_probe_map_reuse(
                    first_phase["probe_status"],
                    second_phase["zero_baseline"],
                    second_phase["probe_status"],
                )
                if (second_phase["event_sender_status"]["stream_epoch"] ==
                        first_phase["event_sender_status"]["stream_epoch"]):
                    raise RuntimeError(
                        "native event sender stream epoch did not rotate on map reload"
                    )
                client_impairment, client_impairment_responses = (
                    capture_client_impairment_status(
                        port, target_user_id, target,
                        paths["target.stdout"], timeout,
                    )
                )
                rcon_log.extend(client_impairment_responses)
                server_impairment, server_impairment_responses = (
                    capture_server_impairment_status(port, timeout)
                )
                rcon_log.extend(server_impairment_responses)
                client_impairment_delta = (
                    native_event_probe_impairment_delta(
                        client_impairment_baseline, client_impairment,
                    )
                )
                server_impairment_delta = (
                    native_event_probe_impairment_delta(
                        server_impairment_baseline, server_impairment,
                    )
                )
                impairment_aggregate = (
                    validate_native_event_probe_impairment_pair(
                        client_impairment_delta, server_impairment_delta,
                    )
                )
                native_event_probe_map_reuse = {
                    "first_phase": first_phase,
                    "second_phase": second_phase,
                    "lifecycle": lifecycle,
                    "client_impairment_baseline": client_impairment_baseline,
                    "server_impairment_baseline": server_impairment_baseline,
                    "client_impairment": client_impairment,
                    "server_impairment": server_impairment,
                    "client_impairment_delta": client_impairment_delta,
                    "server_impairment_delta": server_impairment_delta,
                    "impairment_aggregate": impairment_aggregate,
                    "pre_impairment_native_shadow": (
                        native_event_probe_pre_impairment
                    ),
                    "impairment_armed_after_readiness": True,
                    "same_processes": True,
                    "same_map_reload": MAP_NAME,
                    "probe_client": "target",
                    "target_environment": {
                        "ALSOFT_DRIVERS": "null",
                        "ALSOFT_LOGLEVEL": "3",
                        "ALSOFT_LOGFILE": {
                            "role": "target",
                            "name": "target.openal.log",
                        },
                    },
                }
                status = second_phase["canonical_status"]
            else:
                # This arms only the fixture. It never manufactures a command
                # context or invokes the selected weapon callback.
                rcon_log.append(rcon_command(
                    port, f"sv {mode['arm_command']}", timeout,
                ))
                # Prove that both real clients are playing and retain normal
                # end-frame target poses before the real render watermark can
                # refer to them.
                _fixture_ready_status, fixture_ready_responses = (
                    wait_for_fixture_ready(
                        port, timeout, mode, shooter_user_id, target_user_id,
                    )
                )
                rcon_log.extend(fixture_ready_responses)
                if mode.get("require_native_snapshot_presentation", False):
                    native_snapshot_presentation_baseline = (
                        wait_for_native_snapshot_presentation_baseline(
                            target, paths["target.stdout"], timeout,
                        )
                    )
                # The action goes only to the admitted shooter. Its client
                # turns the payload into a normal authenticated user command.
                input_command = str(mode.get("input_command", "+attack"))
                rcon_log.append(rcon_command(
                    port, f'stuff {shooter_user_id} "{input_command}"', timeout,
                ))
                status, _status_responses = wait_for_status(
                    port, timeout, mode, shooter_user_id, rcon_log,
                )
            if mode.get("require_spectator_exclusion", False):
                if spectator is None or spectator_user_id is None:
                    raise RuntimeError("spectator exclusion lost its third client")
                rcon_log.extend(stuff_client_until_marker(
                    port, spectator_user_id, "cmd team", spectator,
                    paths["spectator.stdout"],
                    SPECTATOR_TEAM_QUERY_MARKER, timeout,
                ))
                spectator_exclusion["team_verified_after_fire"] = True
            if mode.get("require_local_action_authority_receipt", False):
                local_action_parity = wait_for_local_action_parity(
                    shooter, paths["shooter.stdout"], timeout,
                )
            if mode.get("require_legacy_capability_status", False):
                legacy_capability_status, capability_status_responses = (
                    wait_for_legacy_capability_status(
                        port, shooter, target,
                        paths["shooter.stdout"], paths["target.stdout"],
                        timeout, (shooter_user_id, target_user_id),
                    )
                )
                rcon_log.extend(capability_status_responses)
            if mode.get("require_native_event_status", False):
                native_event_shadow, event_status_responses = (
                    wait_for_native_base_shadow(
                        port, shooter, target,
                        paths["shooter.stdout"], paths["target.stdout"],
                        timeout, int(mode.get(
                            "native_event_private_mask", 0x73,
                        )), ("shooter", "target"),
                        (shooter_user_id, target_user_id),
                    )
                )
                rcon_log.extend(event_status_responses)
            if (mode.get("require_combined_native_shadow", False) or
                    mode.get("require_native_snapshot_shadow", False)):
                snapshot_shadow, combined_status_responses = (
                    wait_for_combined_native_shadow(
                        port, shooter, target,
                        paths["shooter.stdout"], paths["target.stdout"],
                        timeout,
                        0x77 if mode.get(
                            "require_combined_native_shadow", False
                        ) else 0x57,
                        ("target",) if mode.get(
                            "require_native_snapshot_presentation", False
                        ) else ("shooter", "target"),
                        (target_user_id,) if mode.get(
                            "require_native_snapshot_presentation", False
                        ) else (shooter_user_id, target_user_id),
                    )
                )
                if mode.get("require_combined_native_shadow", False):
                    combined_native_shadow = snapshot_shadow
                else:
                    native_snapshot_shadow = snapshot_shadow
                rcon_log.extend(combined_status_responses)
            if mode.get("require_native_snapshot_presentation", False):
                if native_snapshot_presentation_baseline is None:
                    raise RuntimeError(
                        "native snapshot presentation baseline is missing"
                    )
                native_snapshot_presentation = (
                    wait_for_native_snapshot_presentation(
                        target, paths["target.stdout"], timeout,
                        native_snapshot_presentation_baseline,
                    )
                )
            expected_live_user_ids = (shooter_user_id, target_user_id)
            if spectator_user_id is not None:
                expected_live_user_ids += (spectator_user_id,)
            rcon_log.append(verify_post_proof_client_liveness(
                port,
                timeout,
                paths["server.stdout"],
                fixture_clients,
                disconnect_baseline,
                client_log_size_baseline,
                expected_live_user_ids,
                require_spectator=spectator is not None,
            ))
        # Close the complete process lifetime before inspecting stderr, audio
        # proof, or hashes. The liveness proof above already captured the
        # required pre-cleanup state.
        shooter_terminated = terminate(shooter)
        target_terminated = terminate(target)
        spectator_terminated = terminate(spectator)
        server_terminated = terminate(server)
        server_text = read_text(paths["server.stdout"])
        shooter_text = read_text(paths["shooter.stdout"])
        shooter_stderr_text = read_text(paths["shooter.stderr"])
        target_text = read_text(paths["target.stdout"])
        target_stderr_text = read_text(paths["target.stderr"])
        spectator_text = read_text(paths["spectator.stdout"])
        required_admissions = required_client_count + (
            1 if mode.get("require_in_session_reconnect", False) else 0
        )
        if server_text.count("Going from cs_primed to cs_spawned") < required_admissions:
            raise RuntimeError("server did not admit every required headless client")
        required_shooter_serverdata = 2 if mode.get(
            "require_in_session_reconnect", False
        ) else 1
        if (shooter_text.count("Serverdata packet received") < required_shooter_serverdata or
                "Serverdata packet received" not in target_text):
            raise RuntimeError("a headless canonical-rail client did not accept serverdata")
        if (spectator_command is not None and
                "Serverdata packet received" not in spectator_text):
            raise RuntimeError("the headless spectator did not accept serverdata")
        if mode.get("require_native_event_probe_map_reuse", False):
            native_event_probe_audio = validate_native_event_probe_openal(
                target_text,
                target_stderr_text,
                read_text(paths["target.openal"]),
            )
            if not isinstance(native_event_probe_map_reuse, dict):
                raise RuntimeError("native event probe report evidence is missing")
            native_event_probe_map_reuse["audio_backend"] = (
                native_event_probe_audio
            )
        status = validate_status(status, mode)
        stderr_names = ["server.stderr", "shooter.stderr", "target.stderr"]
        if spectator_command is not None:
            stderr_names.append("spectator.stderr")
        if any(read_text(paths[name]).strip() for name in stderr_names):
            raise RuntimeError("canonical rail runtime gate wrote stderr")
        if mode.get("require_spectator_exclusion", False):
            if (spectator_exclusion["team_verified_before_fire"] is not True or
                    spectator_exclusion["team_verified_after_fire"] is not True or
                    status["playing_candidates"] != 2 or
                    status["eligible_candidates"] != 2):
                raise RuntimeError("spectator exclusion proof is incomplete")
            # A client that remains a non-playing spectator is outside both
            # damageable player selection and the sealed historical scene.
            spectator_exclusion["spectator_undamaged"] = True
        result: dict[str, object] = {
            "status": status,
            "server_stdout_sha256": file_sha256(paths["server.stdout"]),
            "shooter_stdout_sha256": file_sha256(paths["shooter.stdout"]),
            "target_stdout_sha256": file_sha256(paths["target.stdout"]),
            "shooter_terminated_by_gate": shooter_terminated,
            "target_terminated_by_gate": target_terminated,
            "spectator_terminated_by_gate": spectator_terminated,
            "server_terminated_by_gate": server_terminated,
            "reconnect": reconnect,
            "spectator_exclusion": spectator_exclusion,
            "local_action_authority_parity": local_action_parity,
            "legacy_capability_status": legacy_capability_status,
            "native_event_shadow": native_event_shadow,
            "combined_native_preflight": combined_native_preflight,
            "combined_native_shadow": combined_native_shadow,
            "native_snapshot_shadow": native_snapshot_shadow,
            "native_snapshot_presentation": native_snapshot_presentation,
            "native_event_probe_audio": native_event_probe_audio,
            "native_event_probe_map_reuse": native_event_probe_map_reuse,
            "logs": {name: str(path) for name, path in paths.items()},
        }
        if spectator_command is not None:
            result["spectator_stdout_sha256"] = file_sha256(
                paths["spectator.stdout"]
            )
        return result
    finally:
        body_exception_active = sys.exc_info()[0] is not None
        cleanup_errors: list[Exception] = []
        # Process cleanup must not depend on artifact persistence. Terminate
        # every process independently, then attempt to write the RCON log.
        for process in (shooter, target, spectator, server):
            try:
                terminate(process)
            except Exception as error:
                cleanup_errors.append(error)
        if rcon_log:
            try:
                paths["server.rcon"].write_text(
                    "\n--- rcon response ---\n".join(rcon_log), encoding="utf-8"
                )
            except Exception as error:
                cleanup_errors.append(error)
        if cleanup_errors and not body_exception_active:
            raise cleanup_errors[0]


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--client-exe", required=True, type=Path)
    parser.add_argument("--dedicated-exe", required=True, type=Path)
    parser.add_argument("--working-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--port", type=int, default=27960)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=35.0)
    parser.add_argument("--weapon", choices=tuple(GATE_MODES), default="railgun")
    parser.add_argument("--lag-debug", type=int, default=2)
    args = parser.parse_args(argv)
    if (not 1 <= args.port <= 65535 or args.repeat < 1 or args.timeout <= 0 or
            not 0 <= args.lag_debug <= 3):
        parser.error("port, repeat, timeout, or lag debug is invalid")
    client_exe, dedicated_exe, working_dir, output = (
        args.client_exe.resolve(), args.dedicated_exe.resolve(),
        args.working_dir.resolve(), args.output.resolve(),
    )
    if not client_exe.is_file() or not dedicated_exe.is_file() or not working_dir.is_dir():
        parser.error("client, dedicated executable, or working directory is missing")
    if not (working_dir / "basew" / "maps" / f"{MAP_NAME}.bsp").is_file():
        parser.error("staged canonical hitscan fixture map is missing")
    mode = GATE_MODES[args.weapon]
    native_event_probe_mode = bool(
        mode.get("require_native_event_probe_map_reuse", False)
    )
    started = datetime.now(timezone.utc)
    run_id = started.strftime("%Y%m%dT%H%M%S.%fZ") + f"-{os.getpid()}"
    run_root = output.parent / f"{output.stem}.runs" / run_id
    run_root.mkdir(parents=True, exist_ok=False)
    runtime_root = run_root / "runtime"
    server_home = runtime_root / "server"
    shooter_home = runtime_root / "shooter"
    target_home = runtime_root / "target"
    spectator_home = runtime_root / "spectator"
    runtime_homes = [server_home, shooter_home, target_home]
    if int(mode.get("required_client_count", 2)) == 3:
        runtime_homes.append(spectator_home)
    for runtime_home in runtime_homes:
        runtime_home.mkdir(parents=True, exist_ok=False)
    server_command = build_server_command(
        dedicated_exe, args.port, server_home, args.lag_debug,
        int(mode.get("gametype", 1)),
        bool(mode.get("enable_offhand_hook", False)),
        enable_local_action_authority_receipt=bool(
            mode.get("require_local_action_authority_receipt", False)
        ),
        enable_reconnect_minplayer_bypass=bool(
            mode.get("require_in_session_reconnect", False)
        ),
        enable_combined_snapshot_shadow=bool(
            mode.get("require_combined_native_shadow", False)
        ),
        enable_native_snapshot_shadow=bool(
            mode.get("require_native_snapshot_shadow", False)
        ),
        enable_native_snapshot_presentation=bool(
            mode.get("require_native_snapshot_presentation", False)
        ),
        enable_native_event_shadow=native_event_probe_mode,
        enable_native_event_probe_impairment=native_event_probe_mode,
        defer_native_event_probe_impairment=native_event_probe_mode,
        disable_player_inactivity=native_event_probe_mode,
        max_clients=int(mode.get("required_client_count", 2)),
    )
    shooter_command = build_client_command(
        client_exe, args.port, SHOOTER_NAME, shooter_home,
        enable_local_action_authority_receipt=bool(
            mode.get("require_local_action_authority_receipt", False)
        ),
        enable_combined_snapshot_shadow=bool(
            mode.get("require_combined_native_shadow", False)
        ),
        enable_native_snapshot_shadow=bool(
            mode.get("require_native_snapshot_shadow", False) and
            not mode.get("require_native_snapshot_presentation", False)
        ),
        enable_network_impairment=not bool(
            mode.get("require_native_snapshot_presentation", False) or
            native_event_probe_mode
        ),
        enable_native_event_shadow=native_event_probe_mode,
    )
    target_command = build_client_command(
        client_exe, args.port, TARGET_NAME, target_home,
        enable_local_action_authority_receipt=bool(
            mode.get("require_local_action_authority_receipt", False)
        ),
        enable_combined_snapshot_shadow=bool(
            mode.get("require_combined_native_shadow", False)
        ),
        enable_native_snapshot_shadow=bool(
            mode.get("require_native_snapshot_shadow", False)
        ),
        enable_native_snapshot_presentation=bool(
            mode.get("require_native_snapshot_presentation", False)
        ),
        enable_network_impairment=not native_event_probe_mode,
        enable_native_event_shadow=native_event_probe_mode,
        enable_native_event_probe=native_event_probe_mode,
        enable_native_event_probe_impairment=native_event_probe_mode,
        defer_native_event_probe_impairment=native_event_probe_mode,
    )
    spectator_command = None
    if int(mode.get("required_client_count", 2)) == 3:
        spectator_command = build_client_command(
            client_exe, args.port, SPECTATOR_NAME, spectator_home,
            enable_network_impairment=False,
        )
    try:
        runs: list[dict[str, object]] = []
        for index in range(args.repeat):
            repeat_root = run_root / f"repeat-{index + 1:02d}"
            repeat_root.mkdir()
            runs.append(run_once(
                server_command=server_command, shooter_command=shooter_command,
                target_command=target_command, spectator_command=spectator_command,
                working_dir=working_dir, run_root=repeat_root,
                timeout=args.timeout, mode=mode,
            ))
        statuses = [run["status"] for run in runs]
        signatures = [determinism_signature(validate_status(status, mode)) for status in statuses]
        if any(signature != signatures[0] for signature in signatures[1:]):
            raise RuntimeError("canonical rail runtime evidence was not deterministic")
        if native_event_probe_mode:
            probe_signatures = [
                native_event_probe_determinism_signature(
                    run.get("native_event_probe_map_reuse")
                )
                for run in runs
            ]
            if any(
                    signature != probe_signatures[0]
                    for signature in probe_signatures[1:]):
                raise RuntimeError(
                    "native event probe semantic evidence was not deterministic"
                )
        report: dict[str, object] = {
            "schema": SCHEMA,
            "run_id": run_id,
            "started_at_utc": started.isoformat(),
            "completed_at_utc": datetime.now(timezone.utc).isoformat(),
            "shooter_command": shooter_command,
            "target_command": target_command,
            "spectator_command": spectator_command,
            "dedicated_command": server_command,
            "client_count": int(mode.get("required_client_count", 2)),
            "repeat": args.repeat,
            "weapon": args.weapon,
            "weapon_policy": mode["weapon_policy"],
            "expected_damage": mode["expected_damage"],
            "status": statuses[0],
            "runs": runs,
        }
        write_json_atomic(run_root / "report.json", report)
        write_json_atomic(output, report)
    except Exception as error:
        failure = {
            "schema": SCHEMA + ".failure", "run_id": run_id,
            "error_type": type(error).__name__, "error": str(error),
            "weapon": args.weapon,
            "shooter_command": shooter_command, "target_command": target_command,
            "spectator_command": spectator_command,
            "dedicated_command": server_command,
        }
        write_json_atomic(run_root / "failure.json", failure)
        write_json_atomic(output.with_suffix(".failure.json"), failure)
        print(f"canonical rail runtime gate failed: {type(error).__name__}: {error}", file=sys.stderr)
        return 1
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
