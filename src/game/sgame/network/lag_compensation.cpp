// Copyright (c) 2026 The WORR Project
// Licensed under the GNU General Public License 2.0.

#include "lag_compensation.hpp"
#include "rewind_collision_import.hpp"
#include "shared/command_context.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

// Fixed storage keeps authoritative capture and weapon traces allocation-free.
// At the one-millisecond extreme this still covers twice the 250 ms hard cap.
constexpr uint32_t kHistoryCapacity = 512;
// Mover histories need only cover the bounded rewind window plus a generous
// gap allowance. Keeping their storage separate from player histories makes
// the maximum map cost explicit and prevents a map with many doors from
// allocating per-entity history dynamically.
constexpr uint32_t kMoverHistoryCapacity = 64;
constexpr uint32_t kMoverTrackCapacity = 64;
constexpr uint32_t kSceneCandidateCapacity =
    MAX_CLIENTS_KEX + kMoverTrackCapacity;
constexpr uint32_t kIgnoreCapacity = static_cast<uint32_t>(MAX_PIERCE + 2);
constexpr uint32_t kObservationCapacity = 256;
constexpr int kDefaultMaxRewindMs = 200;
constexpr int kPolicyMaxRewindMs = 250;

struct LegacyFrameSample {
  uint64_t timeUs = 0;
  uint32_t serverFrame = 0;
};

struct LegacyFrameTrack {
  std::array<LegacyFrameSample, kHistoryCapacity> samples{};
  uint32_t next = 0;
  uint32_t count = 0;
};

struct PoseTrack {
  std::array<worr_rewind_pose_v1, kHistoryCapacity> storage{};
  worr_rewind_history_v1 history{};
  LegacyFrameTrack legacyFrames{};
  bool initialized = false;
};

struct MoverTrack {
  std::array<worr_rewind_pose_v1, kMoverHistoryCapacity> storage{};
  worr_rewind_history_v1 history{};
  worr_event_entity_ref_v1 identity{};
  bool initialized = false;
};

struct Diagnostics {
  uint64_t requests = 0;
  uint64_t sessions = 0;
  uint64_t targets = 0;
  uint64_t interpolated = 0;
  uint64_t discontinuities = 0;
  uint64_t capped = 0;
  uint64_t rejectedClock = 0;
  uint64_t missingHistory = 0;
  uint64_t appendRejected = 0;
  uint64_t canonicalAccepted = 0;
  uint64_t canonicalRejected = 0;
  uint64_t canonicalReused = 0;
  uint64_t scenesBuilt = 0;
  uint64_t scenesReused = 0;
  uint64_t scenesRejected = 0;
  uint64_t moversCaptured = 0;
  uint64_t moverCaptureRejected = 0;
  uint64_t moverTrackExhausted = 0;
  uint64_t moverSceneCandidates = 0;
  uint64_t historicalBrushes = 0;
  uint64_t historicalBrushRejected = 0;
  uint64_t ignoredIdentities = 0;
  uint64_t projectileForwardRequests = 0;
  uint64_t projectileForwardAuthenticated = 0;
  uint64_t projectileForwardAdvanced = 0;
  uint64_t projectileForwardClamped = 0;
  uint64_t projectileForwardBlocked = 0;
  uint64_t projectileForwardRejected = 0;
  GameTime nextReport{};
};

struct CommandDecisionCache {
  worr_command_id_v1 commandId{};
  worr_authoritative_command_context_v1 authority{};
  worr_rewind_policy_decision_v1 decision{};
  bool valid = false;
  bool accepted = false;
};

// A Generic weapon can reach a normal projectile callback after the
// command-context scope closes. This stores only a short-lived, accepted
// command decision for the same live shooter and weapon; it carries no
// collision result, historical scene, target, or damage authority.
struct DeferredProjectileForwardAuthorization {
  worr_event_entity_ref_v1 shooter{};
  worr_rewind_policy_decision_v1 decision{};
  item_id_t weapon_item = IT_NULL;
  uint64_t expires_at_us = 0;
  uint8_t launches_remaining = 0;
  // Hand grenades may use this only when it was captured from the real
  // attack-to-release edge. Generic projectile policies retain their usual
  // held-attack authorization semantics.
  bool release_only = false;
  bool valid = false;
};

struct FrozenSceneCache {
  worr_command_id_v1 commandId{};
  worr_rewind_policy_decision_v1 decision{};
  std::array<worr_event_entity_ref_v1, kSceneCandidateCapacity> eligibleRoster{};
  std::array<worr_rewind_scene_candidate_v1, kSceneCandidateCapacity> storage{};
  worr_rewind_scene_v1 scene{};
  uint32_t eligibleRosterCount = 0;
  bool valid = false;
};

struct RewindTarget {
  uint64_t timeUs = 0;
  uint32_t mapEpoch = 0;
  bool canonical = false;
  worr_rewind_policy_decision_v1 decision{};
};

struct TraceBounds {
  Vector3 mins{};
  Vector3 maxs{};
};

struct HistoricalBrushEntry {
  worr_event_entity_ref_v1 identity{};
  gentity_t *current = nullptr;
  worr_sgame_rewind_collision::Asset asset{};
};

// This context is built from a sealed scene before the current-world baseline
// runs. It allows that baseline to omit exactly the live movers represented by
// immutable historical brush poses, without unlinking or altering an edict.
struct HistoricalBrushContext {
  worr_sgame_rewind_collision::Map map{};
  std::array<HistoricalBrushEntry, kMoverTrackCapacity> entries{};
  uint32_t count = 0;
};

std::array<PoseTrack, MAX_CLIENTS_KEX> poseTracks{};
std::array<MoverTrack, kMoverTrackCapacity> moverTracks{};
std::array<uint32_t, MAX_CLIENTS_KEX> clientLifeGenerations{};
std::array<bool, MAX_CLIENTS_KEX> clientLifeGenerationExhausted{};
std::array<worr_rewind_policy_state_v1, MAX_CLIENTS_KEX> policyStates{};
std::array<CommandDecisionCache, MAX_CLIENTS_KEX> decisionCaches{};
std::array<DeferredProjectileForwardAuthorization, MAX_CLIENTS_KEX>
    deferredProjectileForwardAuthorizations{};
std::array<FrozenSceneCache, MAX_CLIENTS_KEX> sceneCaches{};
std::array<worr_rewind_pose_v1, kHistoryCapacity> relabelScratch{};
std::array<gentity_t *, MAX_ENTITIES> traceCandidates{};
gentity_t historicalProxy{};
Diagnostics diagnostics{};
std::array<worr_rewind_observation_v1, kObservationCapacity>
    observationStorage{};
worr_rewind_observation_journal_v1 observationJournal{};

const worr_command_context_import_v1 *commandContextImport = nullptr;
const worr_sgame_rewind_collision::Import *rewindCollisionImport = nullptr;
cvar_t *sg_lag_compensation_max_ms = nullptr;
cvar_t *sg_lag_compensation_interp_ms = nullptr;
cvar_t *sg_lag_compensation_debug = nullptr;
cvar_t *sg_lag_compensation_legacy_error_ms = nullptr;
cvar_t *sg_lag_compensation_projectile_forward_ms = nullptr;
cvar_t *sg_lag_compensation_melee_max_displacement = nullptr;
cvar_t *sg_worr_rewind_mover_selftest_status = nullptr;
cvar_t *sg_worr_rewind_rail_damage_selftest_status = nullptr;
cvar_t *sg_worr_rewind_canonical_rail_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_rail_mover_occlusion_status = nullptr;
cvar_t *sg_worr_rewind_canonical_machinegun_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_chaingun_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_super_shotgun_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_disruptor_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_rocket_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_bfg_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_ion_ripper_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_tesla_mine_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_trap_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_grapple_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_offhand_hook_status = nullptr;
cvar_t *sg_worr_rewind_canonical_proball_throw_status = nullptr;
cvar_t *sg_worr_rewind_canonical_grenade_launcher_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_hand_grenade_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_prox_launcher_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_rocket_splash_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_phalanx_splash_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_plasma_gun_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_plasma_gun_splash_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_blaster_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_hyperblaster_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_chainfist_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_etf_rifle_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_phalanx_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_plasma_beam_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_plasma_beam_held_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_plasma_beam_sustained_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_plasma_beam_release_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_plasma_beam_water_retrace_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_thunderbolt_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_thunderbolt_held_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_thunderbolt_sustained_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_thunderbolt_release_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_thunderbolt_water_retrace_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_thunderbolt_discharge_damage_status = nullptr;
cvar_t *sg_worr_rewind_canonical_shotgun_damage_status = nullptr;

// This fixture is intentionally smaller than the general weapon diagnostics:
// its only job is to make a real command->ClientThink->Item::weaponThink path
// observable for one declared hitscan policy. It cannot create a command
// context, choose a rewind instant, or supply a hit result. Those remain
// server and weapon-path authority.
enum class CanonicalRailProbeStage : uint8_t {
  Idle,
  WaitingForPlayers,
  CapturingHistory,
  WaitingForCanonicalAttack,
  AwaitingDamage,
  Passed,
  Failed,
};

struct CanonicalRailProbeState {
  CanonicalRailProbeStage stage = CanonicalRailProbeStage::Idle;
  gentity_t *shooter = nullptr;
  gentity_t *target = nullptr;
  gentity_t *water = nullptr;
  gentity_t *historical_mover = nullptr;
  gentity_t *current_world_splash_impact = nullptr;
  gentity_t *current_world_splash_damage_target = nullptr;
  gentity_t *prox_landing_surface = nullptr;
  worr_event_entity_ref_v1 shooter_identity{};
  worr_event_entity_ref_v1 target_identity{};
  worr_event_entity_ref_v1 water_identity{};
  worr_event_entity_ref_v1 historical_mover_identity{};
  worr_event_entity_ref_v1 current_world_splash_impact_identity{};
  worr_event_entity_ref_v1 current_world_splash_damage_target_identity{};
  worr_event_entity_ref_v1 current_world_splash_projectile_identity{};
  worr_event_entity_ref_v1 prox_mine_identity{};
  worr_event_entity_ref_v1 prox_landing_surface_identity{};
  worr_command_id_v1 command_id{};
  // A Generic weapon may enter its first fire frame from one real attack
  // command and spawn its projectile from a later real held command. Current
  // world projectile proof tracks that authenticated firing command without
  // weakening the initial attack admission proof.
  worr_command_id_v1 projectile_command_id{};
  Vector3 shooter_origin{};
  Vector3 historical_target_origin{64.0f, 192.0f, 64.0f};
  Vector3 current_target_origin{};
  Vector3 current_target_offset{0.0f, 96.0f, 0.0f};
  Vector3 historical_mover_restore_origin{};
  Vector3 historical_mover_current_origin{};
  bool historical_mover_restore_linked = false;
  uint64_t applied_age_us = 0;
  uint32_t target_history_captures = 0;
  uint32_t target_capture_prepares = 0;
  uint32_t target_capture_callbacks = 0;
  uint32_t capture_append_rejections = 0;
  uint32_t eligible_candidates = 0;
  uint32_t playing_candidates = 0;
  uint32_t observation_path = 0;
  uint32_t observation_outcome = 0;
  uint32_t observation_fallback = 0;
  uint32_t observation_flags = 0;
  uint32_t observation_query = 0;
  uint32_t observation_snapshot_epoch = 0;
  uint32_t history_epoch = 0;
  uint32_t target_history_count = 0;
  uint64_t observation_applied_time_us = 0;
  uint64_t latest_capture_time_us = 0;
  uint64_t trace_current_time_us = 0;
  uint64_t context_snapshot_time_us = 0;
  uint64_t context_mapped_time_us = 0;
  uint64_t projectile_forward_age_us = 0;
  uint64_t projectile_forward_advanced_age_us = 0;
  uint32_t projectile_forward_launches = 0;
  uint32_t projectile_forward_expected_launches = 0;
  uint32_t melee_current_displacement_units = 0;
  uint32_t historical_mover_history_count = 0;
  uint32_t weapon_policy = WORR_REWIND_WEAPON_UNSPECIFIED;
  item_id_t weapon_item = IT_NULL;
  uint32_t expected_damage = 0;
  uint32_t minimum_damage = 0;
  int initial_ammo = 8;
  int weapon_idle_frame = 0;
  float target_distance = 112.0f;
  uint64_t damage_settle_delay_us = 0;
  uint64_t damage_settle_deadline_us = 0;
  uint64_t water_trace_observation_sequence = 0;
  bool defer_damage_evaluation_until_deadline = false;
  bool release_required = false;
  bool release_received = false;
  bool release_damage_stable = false;
  bool water_retrace_required = false;
  bool water_retrace_observed = false;
  bool historical_mover_occlusion_required = false;
  bool historical_mover_relocated = false;
  bool historical_mover_baseline_clear = false;
  bool historical_mover_occlusion_observed = false;
  bool historical_mover_target_undamaged = false;
  bool projectile_forward_required = false;
  bool projectile_forward_authenticated = false;
  bool projectile_forward_advanced = false;
  bool projectile_forward_clamped = false;
  bool projectile_forward_blocked = false;
  bool projectile_current_authority_required = false;
  bool offhand_hook_input_required = false;
  bool melee_selection_required = false;
  bool melee_selection_authenticated = false;
  bool melee_historical_eligible = false;
  bool melee_current_displacement_accepted = false;
  bool current_world_splash_required = false;
  bool current_world_splash_impact_observed = false;
  bool current_world_splash_impact_damageable = false;
  bool current_world_splash_after_forward = false;
  bool current_world_splash_clear_impact_after_touch = false;
  bool current_world_splash_damage_target_after_touch = false;
  float current_world_splash_impact_half_extent = 16.0f;
  bool prox_lifecycle_required = false;
  bool prox_mine_landed = false;
  bool prox_mine_triggered = false;
  bool prox_mine_exploded = false;
  bool damage_required = true;
  bool sustained_hold_required = false;
  bool sustained_hold_interrupted = false;
  bool thunderbolt_discharge_required = false;
  bool thunderbolt_discharge_observed = false;
  bool thunderbolt_discharge_ammo_drained = false;
  item_id_t thunderbolt_discharge_ammo_item = IT_NULL;
  int shooter_health_before = 0;
  uint32_t release_damage_before = 0;
  uint64_t release_grace_deadline_us = 0;
  uint32_t weapon_damage = 0;
  int target_health_before = 0;
  uint32_t failure = 0;
  bool players_ready = false;
  bool history_ready = false;
  bool canonical_scope = false;
  bool attack_received = false;
  bool weapon_callback = false;
  bool canonical_historical_hit = false;
  bool current_geometry_unchanged = false;
};

constexpr uint32_t kCanonicalRailProbeRequiredHistoryCaptures = 6;
constexpr int kCanonicalRailProbeTargetHealth = 1000;
constexpr int kCanonicalRailProbeExpectedDamage = 80;
constexpr int kCanonicalMachinegunProbeExpectedDamage = 8;
constexpr int kCanonicalChaingunProbeExpectedDamage = 18;
constexpr int kCanonicalSuperShotgunProbeExpectedDamage = 120;
constexpr int kCanonicalDisruptorProbeExpectedDamage = 45;
constexpr int kCanonicalRocketProbeExpectedDamage = 100;
// The fixture's damageable present-world blocker receives the grenade's
// normal contact, while the off-axis real client receives the ordinary
// RadiusDamage result: 57–60 at the fixture's sideways live target, not the
// nominal 120 direct damage. The fixed geometry turns the production ±10
// right/up launch adjustments into this bounded normal falloff envelope.
constexpr int kCanonicalGrenadeLauncherProbeExpectedDamage = 60;
constexpr int kCanonicalHandGrenadeProbeExpectedDamage = 60;
constexpr int kCanonicalProxLauncherProbeExpectedDamage = 90;
constexpr int kCanonicalTeslaMineProbeExpectedDamage = 3;
constexpr int kCanonicalTrapProbeExpectedDamage = 20;
constexpr int kCanonicalGrappleProbeExpectedDamage = 1;
constexpr int kCanonicalProBallThrowProbeExpectedDamage = 1;
// The fixed post-landing target is 64 units right and 64 units above the
// normally attached mine. Production RadiusDamage resolves that live player
// hull to the deterministic 61-damage falloff below, rather than nominal
// direct mine damage.
constexpr int kCanonicalProxLauncherLifecycleProbeExpectedDamage = 61;
constexpr int kCanonicalRocketSplashProbeExpectedDamage = 58;
// The normal Phalanx 7/8 barrel callbacks both reach the present-world
// blocker. Their production RadiusDamage outcomes are 48 then 45, so this
// bounded fixture requires the exact normal two-shell total.
constexpr int kCanonicalPhalanxSplashProbeExpectedDamage = 93;
constexpr int kCanonicalPlasmaGunProbeExpectedDamage = 20;
constexpr int kCanonicalPlasmaGunSplashProbeExpectedDamage = 7;
constexpr int kCanonicalBlasterProbeExpectedDamage = 15;
constexpr int kCanonicalHyperBlasterProbeExpectedDamage = 15;
constexpr int kCanonicalChainfistProbeExpectedDamage = 15;
constexpr int kCanonicalEtfRifleProbeExpectedDamage = 10;
constexpr int kCanonicalPhalanxProbeExpectedDamage = 80;
constexpr int kCanonicalPlasmaBeamProbeExpectedDamage = 8;
constexpr int kCanonicalPlasmaBeamHeldProbeExpectedDamage = 24;
constexpr int kCanonicalPlasmaBeamSustainedProbeExpectedDamage = 256;
constexpr int kCanonicalPlasmaBeamReleaseProbeExpectedDamage = 24;
constexpr int kCanonicalPlasmaBeamWaterRetraceProbeExpectedDamage = 4;
constexpr int kCanonicalThunderboltProbeExpectedDamage = 8;
constexpr int kCanonicalThunderboltHeldProbeExpectedDamage = 24;
constexpr int kCanonicalThunderboltSustainedProbeExpectedDamage = 256;
constexpr int kCanonicalThunderboltReleaseProbeExpectedDamage = 24;
constexpr int kCanonicalThunderboltWaterRetraceProbeExpectedDamage = 4;
constexpr int kCanonicalThunderboltDischargeProbeExpectedDamage = 70;
constexpr int kCanonicalThunderboltDischargeProbeCells = 8;
// Plasma Beam requires two Cells to start each tick but consumes one. Supply
// 33 Cells so both beam policies can complete exactly 32 normal 8-damage
// ticks; the gate passes on the 32nd tick before Thunderbolt can consume the
// spare Cell.
constexpr int kCanonicalBeamSustainedProbeCells = 33;
constexpr uint64_t kCanonicalBeamSustainedProbeTimeoutUs = UINT64_C(5000000);
// The controlled view angle must remain valid for the complete five-second
// sustained window. Keep one second of tolerance for command and server-frame
// cadence without manufacturing a client angle or a weapon action.
constexpr GameTime kCanonicalBeamSustainedProbeAngleLockDuration = 6_sec;
constexpr int kCanonicalShotgunProbeExpectedDamage = 48;
constexpr uint64_t kCanonicalBeamReleaseGraceUs = UINT64_C(250000);
// A Generic fire animation must reach its normal callback promptly after the
// accepted attack. This independent authorization lifetime never extends the
// projectile forward-distance cap itself.
constexpr uint64_t kDeferredProjectileForwardAuthorizationLifetimeUs =
    UINT64_C(250000);
// BFG starts its ordinary firing sequence at frame 9, performs its wind-up
// there, and creates the projectile at frame 17. At the normal 10 Hz weapon
// rate that is eight animation intervals, so retain only the same accepted
// single-launch authorization for a bounded 1.25 second window. This does not
// authorize BFG laser, touch, explosion, or splash behavior.
constexpr uint64_t kBfgDeferredProjectileForwardAuthorizationLifetimeUs =
    UINT64_C(1250000);
// Grapple's normal Generic state machine reaches its frame-six hook callback
// after the accepted attack edge. Keep one single-launch authorization just
// long enough for those ordinary frames, without extending the 100 ms forward
// distance cap or authorizing any attachment lifecycle.
constexpr uint64_t kGrappleDeferredProjectileForwardAuthorizationLifetimeUs =
    UINT64_C(750000);

CanonicalRailProbeState canonicalRailProbe{};

[[nodiscard]] bool CanonicalRailProbeDamageApplied() {
  const uint32_t observed = canonicalRailProbe.weapon_damage;
  return observed >= canonicalRailProbe.minimum_damage &&
         observed <= canonicalRailProbe.expected_damage;
}

void CanonicalRailProbePrepareFrameCapture(gentity_t *entity);
void CanonicalRailProbeCaptureFrame(gentity_t *entity);
void CanonicalRailProbeObserveTrace(
    gentity_t *shooter, const worr_rewind_observation_v1 &observation);
void CanonicalRailProbeObserveProjectileForward(
    gentity_t *shooter,
    const LagCompensationProjectileForwardResult &result);
void CanonicalRailProbeObserveMeleeSelection(
    gentity_t *shooter, const LagCompensationMeleeSelectionResult &result);
[[nodiscard]] bool CanonicalRailProbeSameEntity(
    const gentity_t *entity, worr_event_entity_ref_v1 expected);
bool CanonicalRailProbeArm();
bool CanonicalRailMoverOcclusionProbeArm();
bool CanonicalMachinegunProbeArm();
bool CanonicalChaingunProbeArm();
bool CanonicalSuperShotgunProbeArm();
bool CanonicalDisruptorProbeArm();
bool CanonicalRocketProbeArm();
bool CanonicalBfgProbeArm();
bool CanonicalIonRipperProbeArm();
bool CanonicalTeslaMineProbeArm();
bool CanonicalTrapProbeArm();
bool CanonicalGrappleProbeArm();
bool CanonicalOffhandHookProbeArm();
bool CanonicalProBallThrowProbeArm();
bool CanonicalGrenadeLauncherProbeArm();
bool CanonicalHandGrenadeProbeArm();
bool CanonicalHandGrenadeSplashProbeArm();
bool CanonicalProxLauncherProbeArm();
bool CanonicalRocketSplashProbeArm();
bool CanonicalPhalanxSplashProbeArm();
bool CanonicalPlasmaGunProbeArm();
bool CanonicalPlasmaGunSplashProbeArm();
bool CanonicalBlasterProbeArm();
bool CanonicalHyperBlasterProbeArm();
bool CanonicalChainfistProbeArm();
bool CanonicalEtfRifleProbeArm();
bool CanonicalPhalanxProbeArm();
bool CanonicalPlasmaBeamProbeArm();
bool CanonicalPlasmaBeamHeldProbeArm();
bool CanonicalPlasmaBeamSustainedProbeArm();
bool CanonicalPlasmaBeamReleaseProbeArm();
bool CanonicalPlasmaBeamWaterRetraceProbeArm();
bool CanonicalThunderboltProbeArm();
bool CanonicalThunderboltHeldProbeArm();
bool CanonicalThunderboltSustainedProbeArm();
bool CanonicalThunderboltReleaseProbeArm();
bool CanonicalThunderboltWaterRetraceProbeArm();
bool CanonicalThunderboltDischargeProbeArm();
bool CanonicalShotgunProbeArm();

uint32_t provisionalMapEpoch = 0;
uint32_t historyMapEpoch = 1;
uint32_t authoritativeMapEpoch = 0;
uint32_t captureTick = 0;
uint64_t lastCaptureTimeUs = std::numeric_limits<uint64_t>::max();
bool awaitingAuthoritativeMapEpoch = true;
bool initialized = false;

// These caches contain the full per-client scene/history working sets.  Their
// contents are meaningful only while valid; clearing the whole arrays with an
// aggregate assignment makes MSVC materialize a multi-megabyte temporary on
// the caller's stack.  Invalidation preserves the same observable contract
// without making map transitions or shutdown depend on stack size.
void InvalidateDecisionCaches() {
  for (CommandDecisionCache &cache : decisionCaches)
    cache.valid = false;
}

void InvalidateDeferredProjectileForwardAuthorizations() {
  for (DeferredProjectileForwardAuthorization &authorization :
       deferredProjectileForwardAuthorizations) {
    authorization = {};
  }
}

void InvalidateSceneCaches() {
  for (FrozenSceneCache &cache : sceneCaches)
    cache.valid = false;
}

[[nodiscard]] bool SameCommand(worr_command_id_v1 lhs,
                               worr_command_id_v1 rhs) {
  return lhs.epoch == rhs.epoch && lhs.sequence == rhs.sequence;
}

template <typename T>
[[nodiscard]] bool SameCanonicalBytes(const T &lhs, const T &rhs) {
  return std::memcmp(&lhs, &rhs, sizeof(T)) == 0;
}

[[nodiscard]] std::size_t ClientIndex(const gentity_t *entity) {
  if (!entity || entity->s.number <= 0)
    return MAX_CLIENTS_KEX;

  const auto index = static_cast<std::size_t>(entity->s.number - 1);
  if (index >= static_cast<std::size_t>(game.maxClients) ||
      index >= static_cast<std::size_t>(MAX_CLIENTS_KEX)) {
    return MAX_CLIENTS_KEX;
  }
  return index;
}

[[nodiscard]] bool TimeToMicroseconds(GameTime time, uint64_t &timeUs) {
  const int64_t milliseconds = time.milliseconds();
  if (milliseconds < 0)
    return false;
  const uint64_t value = static_cast<uint64_t>(milliseconds);
  if (value > std::numeric_limits<uint64_t>::max() / UINT64_C(1000))
    return false;
  timeUs = value * UINT64_C(1000);
  return true;
}

// Server snapshots, canonical command proofs, and retained collision poses
// must share one timeline. GameTime intentionally pauses before a client is
// admitted, while the server's snapshot frame/timeline remains monotonic;
// using GameTime here made canonical decisions point into a different epoch
// of time after an idle dedicated server had accepted its first player.
[[nodiscard]] bool CurrentAuthoritativeTimeUs(uint64_t &timeUs) {
  if (!gi.ServerSimulationTimeUs)
    return false;
  timeUs = gi.ServerSimulationTimeUs();
  return true;
}

// End-frame pose capture observes the state produced by the game frame that
// is currently executing. The engine advances its published simulation clock
// immediately after that frame, so derive that completed-state timestamp
// without waiting for the next frame (where a command could already trace).
[[nodiscard]] bool CompletedAuthoritativeFrameTimeUs(uint64_t &timeUs) {
  uint64_t currentTimeUs = 0;
  if (!CurrentAuthoritativeTimeUs(currentTimeUs) || gi.frameTimeMs == 0)
    return false;
  const uint64_t frameTimeUs =
      static_cast<uint64_t>(gi.frameTimeMs) * UINT64_C(1000);
  if (currentTimeUs > std::numeric_limits<uint64_t>::max() - frameTimeUs)
    return false;
  timeUs = currentTimeUs + frameTimeUs;
  return true;
}

[[nodiscard]] worr_event_entity_ref_v1 AbsentEntityRef() {
  // Rewind poses define their optional mover reference as the all-zero
  // sentinel. The event-stream NO_ENTITY sentinel is deliberately not valid
  // in this ABI and would make every non-rider and mover pose reject.
  return {};
}

[[nodiscard]] bool EntityRef(const gentity_t *entity,
                             worr_event_entity_ref_v1 &ref) {
  if (!entity || entity->s.number <= 0 || entity->s.number >= MAX_ENTITIES)
    return false;

  uint32_t generation = 0;
  if (entity->client) {
    const std::size_t clientIndex = ClientIndex(entity);
    if (clientIndex >= clientLifeGenerations.size())
      return false;
    generation = clientLifeGenerations[clientIndex];
  } else {
    generation = static_cast<uint32_t>(entity->spawn_count) + 1u;
  }
  if (generation == 0)
    return false;
  ref.index = static_cast<uint32_t>(entity->s.number);
  ref.generation = generation;
  return true;
}

[[nodiscard]] gentity_t *EntityFromRef(worr_event_entity_ref_v1 ref) {
  if (ref.index == 0 || ref.index == WORR_EVENT_NO_ENTITY ||
      ref.index >= MAX_ENTITIES ||
      ref.index >= static_cast<uint32_t>(globals.numEntities) ||
      ref.generation == 0) {
    return nullptr;
  }
  gentity_t *entity = &g_entities[ref.index];
  worr_event_entity_ref_v1 currentRef{};
  if (!entity->inUse || !EntityRef(entity, currentRef) ||
      currentRef.index != ref.index ||
      currentRef.generation != ref.generation) {
    return nullptr;
  }
  return entity;
}

void CopyVector(float destination[3], const Vector3 &source) {
  destination[0] = source.x;
  destination[1] = source.y;
  destination[2] = source.z;
}

[[nodiscard]] Vector3 ToVector(const float source[3]) {
  return {source[0], source[1], source[2]};
}

[[nodiscard]] bool InitTrack(PoseTrack &track, uint32_t entityIndex) {
  worr_rewind_history_config_v1 config{};
  Worr_RewindHistoryConfigDefaultsV1(&config);
  track.legacyFrames = {};
  track.initialized = Worr_RewindHistoryInitV1(
      &track.history, track.storage.data(), kHistoryCapacity, entityIndex,
      &config);
  return track.initialized;
}

[[nodiscard]] bool InitMoverTrack(MoverTrack &track, uint32_t entityIndex) {
  worr_rewind_history_config_v1 config{};
  Worr_RewindHistoryConfigDefaultsV1(&config);
  track.initialized = Worr_RewindHistoryInitV1(
      &track.history, track.storage.data(), kMoverHistoryCapacity, entityIndex,
      &config);
  return track.initialized;
}

void ResetMoverTracks() {
  for (MoverTrack &track : moverTracks) {
    track.initialized = false;
    track.identity = AbsentEntityRef();
    std::memset(&track.history, 0, sizeof(track.history));
  }
}

void ResetHistories(uint32_t mapEpoch) {
  historyMapEpoch = mapEpoch ? mapEpoch : 1u;
  captureTick = 0;
  lastCaptureTimeUs = std::numeric_limits<uint64_t>::max();
  for (std::size_t i = 0; i < poseTracks.size(); ++i)
    (void)InitTrack(poseTracks[i], static_cast<uint32_t>(i + 1u));
  ResetMoverTracks();
  InvalidateDecisionCaches();
  InvalidateDeferredProjectileForwardAuthorizations();
  InvalidateSceneCaches();
}

[[nodiscard]] bool RelabelTrack(PoseTrack &track, uint32_t mapEpoch) {
  if (!track.initialized ||
      !Worr_RewindHistoryValidateV1(&track.history)) {
    return false;
  }

  const uint32_t count = track.history.count;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t index = static_cast<uint32_t>(
        (static_cast<uint64_t>(track.history.head) + i) %
        track.history.capacity);
    relabelScratch[i] = track.history.slots[index];
  }

  const uint32_t entityIndex = track.history.entity_index;
  const LegacyFrameTrack legacyFrames = track.legacyFrames;
  if (!InitTrack(track, entityIndex))
    return false;
  // Epoch adoption relabels canonical pose provenance only.  Preserve the
  // independently validated ack-to-time ring used by mixed legacy clients;
  // map/client resets still clear it through InitTrack.
  track.legacyFrames = legacyFrames;
  for (uint32_t i = 0; i < count; ++i) {
    worr_rewind_pose_v1 pose = relabelScratch[i];
    pose.map_epoch = mapEpoch;
    pose.flags &= ~static_cast<uint32_t>(WORR_REWIND_POSE_DISCONTINUITY_MAP);
    uint32_t reason = WORR_REWIND_APPEND_REJECT_INVALID;
    if (!Worr_RewindHistoryAppendV1(&track.history, &pose, &reason) ||
        reason != WORR_REWIND_APPEND_ACCEPTED) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool RelabelMoverTrack(MoverTrack &track, uint32_t mapEpoch) {
  if (!track.initialized || !Worr_RewindHistoryValidateV1(&track.history))
    return false;

  const uint32_t count = track.history.count;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t index = static_cast<uint32_t>(
        (static_cast<uint64_t>(track.history.head) + i) %
        track.history.capacity);
    relabelScratch[i] = track.history.slots[index];
  }

  const uint32_t entityIndex = track.history.entity_index;
  const worr_event_entity_ref_v1 identity = track.identity;
  if (!InitMoverTrack(track, entityIndex))
    return false;
  track.identity = identity;
  for (uint32_t i = 0; i < count; ++i) {
    worr_rewind_pose_v1 pose = relabelScratch[i];
    pose.map_epoch = mapEpoch;
    pose.flags &= ~static_cast<uint32_t>(WORR_REWIND_POSE_DISCONTINUITY_MAP);
    uint32_t reason = WORR_REWIND_APPEND_REJECT_INVALID;
    if (!Worr_RewindHistoryAppendV1(&track.history, &pose, &reason) ||
        reason != WORR_REWIND_APPEND_ACCEPTED) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool AdoptAuthoritativeMapEpoch(uint32_t mapEpoch) {
  if (mapEpoch == 0)
    return false;
  if (authoritativeMapEpoch == mapEpoch && historyMapEpoch == mapEpoch)
    return true;

  if (awaitingAuthoritativeMapEpoch) {
    for (PoseTrack &track : poseTracks) {
      if (!RelabelTrack(track, mapEpoch)) {
        ResetHistories(mapEpoch);
        break;
      }
    }
    for (MoverTrack &track : moverTracks) {
      if (track.initialized && !RelabelMoverTrack(track, mapEpoch)) {
        ResetHistories(mapEpoch);
        break;
      }
    }
  } else {
    // An epoch transition without the map reset hook is not safe to relabel:
    // discard history rather than alias poses from two maps.
    ResetHistories(mapEpoch);
  }

  historyMapEpoch = mapEpoch;
  authoritativeMapEpoch = mapEpoch;
  awaitingAuthoritativeMapEpoch = false;
  InvalidateSceneCaches();
  return true;
}

[[nodiscard]] int MaxRewindMs() {
  const int configured = sg_lag_compensation_max_ms
                             ? sg_lag_compensation_max_ms->integer
                             : kDefaultMaxRewindMs;
  return std::clamp(configured, 0, kPolicyMaxRewindMs);
}

[[nodiscard]] int InterpolationBiasMs(int estimatedSnapshotIntervalMs) {
  const int configured = sg_lag_compensation_interp_ms
                             ? sg_lag_compensation_interp_ms->integer
                             : -1;
  if (configured >= 0)
    return std::clamp(configured, 0, MaxRewindMs());
  if (estimatedSnapshotIntervalMs <= 0)
    return 0;
  return std::clamp(estimatedSnapshotIntervalMs / 2, 0, MaxRewindMs());
}

[[nodiscard]] int ProjectileForwardMs() {
  const int configured =
      sg_lag_compensation_projectile_forward_ms
          ? sg_lag_compensation_projectile_forward_ms->integer
          : 100;
  // The projectile policy may be narrower than the historical trace window,
  // but it never advances farther than the server's accepted rewind ceiling.
  return std::clamp(configured, 0, MaxRewindMs());
}

[[nodiscard]] uint32_t
ProjectileForwardPolicyForWeapon(item_id_t weaponItem) {
  switch (weaponItem) {
  case IT_WEAPON_DISRUPTOR:
    return WORR_REWIND_WEAPON_DISRUPTOR_CONVERGENCE;
  case IT_WEAPON_RLAUNCHER:
    return WORR_REWIND_WEAPON_ROCKET_SPAWN_FORWARD;
  case IT_WEAPON_PLASMAGUN:
    return WORR_REWIND_WEAPON_PLASMA_GUN_SPAWN_FORWARD;
  case IT_WEAPON_BLASTER:
  case IT_WEAPON_HYPERBLASTER:
    return WORR_REWIND_WEAPON_BLASTER_BOLT_SPAWN_FORWARD;
  case IT_WEAPON_ETF_RIFLE:
    return WORR_REWIND_WEAPON_ETF_FLECHETTE_SPAWN_FORWARD;
  case IT_WEAPON_PHALANX:
    return WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD;
  case IT_WEAPON_GLAUNCHER:
    return WORR_REWIND_WEAPON_GRENADE_LAUNCHER_BALLISTIC_SPAWN_FORWARD;
  case IT_AMMO_GRENADES:
    return WORR_REWIND_WEAPON_HAND_GRENADE_RELEASE_BALLISTIC_SPAWN_FORWARD;
  case IT_WEAPON_PROXLAUNCHER:
    return WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD;
  case IT_WEAPON_BFG:
    return WORR_REWIND_WEAPON_BFG_SPAWN_FORWARD;
  case IT_WEAPON_IONRIPPER:
    return WORR_REWIND_WEAPON_ION_RIPPER_BURST_SPAWN_FORWARD;
  case IT_AMMO_TESLA:
    return WORR_REWIND_WEAPON_TESLA_MINE_RELEASE_BALLISTIC_DEPLOY_FORWARD;
  case IT_AMMO_TRAP:
    return WORR_REWIND_WEAPON_TRAP_RELEASE_BALLISTIC_DEPLOY_FORWARD;
  case IT_WEAPON_GRAPPLE:
    return WORR_REWIND_WEAPON_GRAPPLE_HOOK_SPAWN_FORWARD;
  case IT_BALL:
    return WORR_REWIND_WEAPON_PROBALL_HELD_THROW_BALLISTIC_SPAWN_FORWARD;
  default:
    return WORR_REWIND_WEAPON_UNSPECIFIED;
  }
}

[[nodiscard]] bool ProjectileForwardPolicyMatchesWeapon(
    uint32_t weaponPolicy, item_id_t weaponItem) {
  return ProjectileForwardPolicyForWeapon(weaponItem) == weaponPolicy;
}

[[nodiscard]] bool
ReleaseOnlyProjectileForwardPolicy(uint32_t weaponPolicy) {
  return weaponPolicy ==
             WORR_REWIND_WEAPON_HAND_GRENADE_RELEASE_BALLISTIC_SPAWN_FORWARD ||
         weaponPolicy ==
             WORR_REWIND_WEAPON_TESLA_MINE_RELEASE_BALLISTIC_DEPLOY_FORWARD ||
         weaponPolicy ==
             WORR_REWIND_WEAPON_TRAP_RELEASE_BALLISTIC_DEPLOY_FORWARD ||
         weaponPolicy ==
             WORR_REWIND_WEAPON_PROBALL_HELD_THROW_BALLISTIC_SPAWN_FORWARD;
}

[[nodiscard]] uint8_t DeferredProjectileForwardLaunches(uint32_t weaponPolicy) {
  // One Phalanx attack normally advances through two barrel frames. Each
  // ordinary spawn remains current-world; this only lets both callbacks
  // consume their one accepted attack authorization before it expires.
  if (weaponPolicy == WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD)
    return 2u;
  // The normal Ion Ripper callback emits exactly fifteen independently
  // randomized bolts. A deferred callback may consume only this complete
  // production burst, not an open-ended projectile budget.
  if (weaponPolicy == WORR_REWIND_WEAPON_ION_RIPPER_BURST_SPAWN_FORWARD)
    return 15u;
  return 1u;
}

[[nodiscard]] uint32_t
CanonicalProjectileForwardExpectedLaunches(uint32_t weaponPolicy) {
  return weaponPolicy == WORR_REWIND_WEAPON_ION_RIPPER_BURST_SPAWN_FORWARD
             ? 15u
             : 0u;
}

[[nodiscard]] uint64_t
DeferredProjectileForwardAuthorizationLifetimeUs(uint32_t weaponPolicy) {
  if (weaponPolicy == WORR_REWIND_WEAPON_BFG_SPAWN_FORWARD)
    return kBfgDeferredProjectileForwardAuthorizationLifetimeUs;
  if (weaponPolicy == WORR_REWIND_WEAPON_GRAPPLE_HOOK_SPAWN_FORWARD)
    return kGrappleDeferredProjectileForwardAuthorizationLifetimeUs;
  return kDeferredProjectileForwardAuthorizationLifetimeUs;
}

[[nodiscard]] int MeleeMaxDisplacementUnits() {
  const int configured =
      sg_lag_compensation_melee_max_displacement
          ? sg_lag_compensation_melee_max_displacement->integer
          : 64;
  // This is deliberately a small world-space guard, independent of the
  // command rewind window. A player who has moved farther than this cannot be
  // selected by historical melee eligibility.
  return std::clamp(configured, 0, 128);
}

void MaybeReportDiagnostics() {
  if (!sg_lag_compensation_debug || !sg_lag_compensation_debug->integer)
    return;
  if (diagnostics.nextReport && level.time < diagnostics.nextReport)
    return;

  diagnostics.nextReport = level.time + 1_sec;
  gi.Com_PrintFmt(
      "lagcomp: requests={} sessions={} targets={} interpolated={} "
      "discontinuities={} capped={} rejected_clock={} missing_history={} "
      "append_rejected={} canonical_accepted={} canonical_rejected={} "
      "canonical_reused={} scenes_built={} scenes_reused={} "
      "scenes_rejected={} movers_captured={} mover_capture_rejected={} "
      "mover_track_exhausted={} mover_scene_candidates={} "
      "historical_brushes={} historical_brush_rejected={} "
      "ignored_identities={} projectile_forward_requests={} "
      "projectile_forward_authenticated={} projectile_forward_advanced={} "
      "projectile_forward_clamped={} projectile_forward_blocked={} "
      "projectile_forward_rejected={}\n",
      diagnostics.requests, diagnostics.sessions, diagnostics.targets,
      diagnostics.interpolated, diagnostics.discontinuities,
      diagnostics.capped, diagnostics.rejectedClock,
      diagnostics.missingHistory, diagnostics.appendRejected,
      diagnostics.canonicalAccepted, diagnostics.canonicalRejected,
      diagnostics.canonicalReused, diagnostics.scenesBuilt,
      diagnostics.scenesReused, diagnostics.scenesRejected,
      diagnostics.moversCaptured, diagnostics.moverCaptureRejected,
      diagnostics.moverTrackExhausted, diagnostics.moverSceneCandidates,
      diagnostics.historicalBrushes, diagnostics.historicalBrushRejected,
      diagnostics.ignoredIdentities, diagnostics.projectileForwardRequests,
      diagnostics.projectileForwardAuthenticated,
      diagnostics.projectileForwardAdvanced,
      diagnostics.projectileForwardClamped, diagnostics.projectileForwardBlocked,
      diagnostics.projectileForwardRejected);
}

[[nodiscard]] const LegacyFrameSample &NewestLegacyFrame(
    const LegacyFrameTrack &track, uint32_t age) {
  const uint32_t index =
      (track.next + kHistoryCapacity - 1u - age) % kHistoryCapacity;
  return track.samples[index];
}

void RecordLegacyFrame(PoseTrack &track, uint32_t serverFrame,
                       uint64_t timeUs) {
  LegacyFrameTrack &legacy = track.legacyFrames;
  if (legacy.count) {
    const LegacyFrameSample &newest = NewestLegacyFrame(legacy, 0);
    if (newest.serverFrame == serverFrame && newest.timeUs == timeUs)
      return;
  }
  legacy.samples[legacy.next] = {timeUs, serverFrame};
  legacy.next = (legacy.next + 1u) % kHistoryCapacity;
  legacy.count = std::min(legacy.count + 1u, kHistoryCapacity);
}

[[nodiscard]] bool FindLegacySnapshotTime(const PoseTrack &track,
                                          uint32_t serverFrame,
                                          uint32_t serverFrameDelta,
                                          uint64_t &snapshotTimeUs,
                                          int &estimatedIntervalMs) {
  const LegacyFrameTrack &legacy = track.legacyFrames;
  const LegacyFrameSample *matched = nullptr;
  uint32_t matchedAge = 0;
  for (uint32_t age = 0; age < legacy.count; ++age) {
    const LegacyFrameSample &sample = NewestLegacyFrame(legacy, age);
    if (sample.serverFrame == serverFrame) {
      matched = &sample;
      matchedAge = age;
      break;
    }
    if (sample.serverFrame < serverFrame)
      break;
  }
  if (!matched)
    return false;

  snapshotTimeUs = matched->timeUs;
  estimatedIntervalMs = 0;
  if (!serverFrameDelta || serverFrameDelta >= serverFrame)
    return true;

  const uint32_t previousSentFrame = serverFrame - serverFrameDelta;
  for (uint32_t age = matchedAge + 1u; age < legacy.count; ++age) {
    const LegacyFrameSample &older = NewestLegacyFrame(legacy, age);
    if (older.serverFrame > previousSentFrame)
      continue;
    if (older.serverFrame < previousSentFrame)
      break;
    if (matched->timeUs > older.timeUs) {
      estimatedIntervalMs = static_cast<int>(std::min<uint64_t>(
          (matched->timeUs - older.timeUs) / UINT64_C(1000),
          static_cast<uint64_t>(kPolicyMaxRewindMs)));
    }
    break;
  }
  if (estimatedIntervalMs > 0)
    return true;

  for (uint32_t age = matchedAge + 1u; age < legacy.count; ++age) {
    const LegacyFrameSample &older = NewestLegacyFrame(legacy, age);
    if (older.serverFrame >= serverFrame)
      continue;
    if (matched->timeUs > older.timeUs) {
      estimatedIntervalMs = static_cast<int>(std::min<uint64_t>(
          (matched->timeUs - older.timeUs) / UINT64_C(1000),
          static_cast<uint64_t>(kPolicyMaxRewindMs)));
    }
    break;
  }
  return true;
}

[[nodiscard]] bool ResolveCanonicalDecision(
    std::size_t shooterIndex, worr_rewind_policy_decision_v1 &decision,
    bool &contextAvailable) {
  contextAvailable = false;
  if (!commandContextImport || !commandContextImport->GetCurrent ||
      !commandContextImport->GetScopeState)
    return false;

  const uint32_t scopeState = commandContextImport->GetScopeState();
  if (scopeState == WORR_COMMAND_CONTEXT_SCOPE_INACTIVE_LEGACY)
    return false;
  contextAvailable = true;
  if (scopeState != WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID) {
    ++diagnostics.canonicalRejected;
    return false;
  }

  worr_authoritative_command_context_v1 context{};
  if (!commandContextImport->GetCurrent(&context)) {
    ++diagnostics.canonicalRejected;
    return false;
  }
  if (shooterIndex >= policyStates.size() ||
      context.struct_size != sizeof(context) ||
      context.schema_version != WORR_COMMAND_CONTEXT_API_VERSION ||
      context.client_index != shooterIndex ||
      !AdoptAuthoritativeMapEpoch(context.current_snapshot.snapshot_id.epoch)) {
    ++diagnostics.canonicalRejected;
    return false;
  }

  // A synthesized gap command has no client-observed render instant.  Until
  // the server supplies bounded per-gap timing evidence, it is never eligible
  // for historical hit validation (and must not inherit a zero-error proof).
  if (context.command.render_watermark.provenance ==
      WORR_COMMAND_RENDER_PROVENANCE_SERVER_SYNTHESIZED) {
    ++diagnostics.canonicalRejected;
    return false;
  }

  CommandDecisionCache &cache = decisionCaches[shooterIndex];
  if (cache.valid && SameCommand(cache.commandId, context.command.command_id)) {
    if (SameCanonicalBytes(cache.authority, context)) {
      ++diagnostics.canonicalReused;
      decision = cache.decision;
      return cache.accepted;
    }
    // The server must never mutate authority behind a command identity.  Do
    // not return stale authority or allow a previously rejected same-ID record
    // to become eligible through mutation.
    cache = {};
    cache.valid = true;
    cache.commandId = context.command.command_id;
    cache.authority = context;
    ++diagnostics.canonicalRejected;
    return false;
  }

  cache = {};
  cache.valid = true;
  cache.commandId = context.command.command_id;
  cache.authority = context;
  if (MaxRewindMs() <= 0) {
    ++diagnostics.canonicalRejected;
    return false;
  }

  worr_rewind_policy_config_v1 config{};
  Worr_RewindPolicyConfigDefaultsV1(&config);
  config.target_window_us =
      static_cast<uint64_t>(MaxRewindMs()) * UINT64_C(1000);
  const int legacyErrorMs = std::clamp(
      sg_lag_compensation_legacy_error_ms
          ? sg_lag_compensation_legacy_error_ms->integer
          : 50,
      0, kPolicyMaxRewindMs);
  config.max_legacy_error_us =
      static_cast<uint64_t>(legacyErrorMs) * UINT64_C(1000);

  const bool evaluated = Worr_RewindPolicyResolveV1(
      &policyStates[shooterIndex], &config, &context.command,
      &context.current_snapshot, &context.mapping_proof, &cache.decision);
  if (!evaluated) {
    ++diagnostics.canonicalRejected;
    return false;
  }
  cache.accepted =
      (cache.decision.flags & WORR_REWIND_DECISION_ACCEPTED) != 0;
  decision = cache.decision;
  if (!cache.accepted) {
    ++diagnostics.canonicalRejected;
    return false;
  }

  ++diagnostics.canonicalAccepted;
  ++diagnostics.sessions;
  if ((cache.decision.flags & WORR_REWIND_DECISION_CLAMPED) != 0)
    ++diagnostics.capped;
  return true;
}

[[nodiscard]] bool ResolveDeferredProjectileForwardDecision(
    gentity_t *shooter, std::size_t shooterIndex, uint32_t weaponPolicy,
    uint64_t currentTimeUs, worr_rewind_policy_decision_v1 &decision) {
  if (!shooter || shooterIndex >= deferredProjectileForwardAuthorizations.size())
    return false;

  DeferredProjectileForwardAuthorization &authorization =
      deferredProjectileForwardAuthorizations[shooterIndex];
  if (!authorization.valid || authorization.launches_remaining == 0 ||
      currentTimeUs > authorization.expires_at_us) {
    authorization = {};
    return false;
  }

  worr_event_entity_ref_v1 shooterIdentity{};
  const bool releaseOnlyPolicy =
      ReleaseOnlyProjectileForwardPolicy(weaponPolicy);
  if (!EntityRef(shooter, shooterIdentity) ||
      shooterIdentity.index != authorization.shooter.index ||
      shooterIdentity.generation != authorization.shooter.generation ||
      !ProjectileForwardPolicyMatchesWeapon(weaponPolicy,
                                            authorization.weapon_item) ||
      authorization.release_only != releaseOnlyPolicy ||
      (authorization.decision.flags & WORR_REWIND_DECISION_ACCEPTED) == 0 ||
      authorization.decision.mapped_time_us > currentTimeUs ||
      authorization.decision.snapshot_id.epoch == 0 ||
      authorization.decision.snapshot_id.epoch != authoritativeMapEpoch) {
    authorization = {};
    return false;
  }

  decision = authorization.decision;
  --authorization.launches_remaining;
  if (authorization.launches_remaining == 0)
    authorization.valid = false;
  return true;
}

[[nodiscard]] bool ResolveTarget(gentity_t *fromPlayer, RewindTarget &target,
                                 bool &canonicalContextAvailable,
                                 uint32_t &fallbackReason) {
  ++diagnostics.requests;
  canonicalContextAvailable = false;
  fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_SHOOTER_INELIGIBLE;
  if (!deathmatch || !deathmatch->integer || !g_lagCompensation ||
      !g_lagCompensation->integer) {
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_MASTER_DISABLED;
    return false;
  }
  if (!fromPlayer || !fromPlayer->client || (fromPlayer->svFlags & SVF_BOT)) {
    return false;
  }

  const std::size_t shooterIndex = ClientIndex(fromPlayer);
  if (shooterIndex >= poseTracks.size()) {
    ++diagnostics.rejectedClock;
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED;
    return false;
  }

  if (ResolveCanonicalDecision(shooterIndex, target.decision,
                               canonicalContextAvailable)) {
    target.timeUs = target.decision.applied_time_us;
    target.mapEpoch = target.decision.snapshot_id.epoch;
    target.canonical = true;
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_NONE;
    return true;
  }
  // A callback-scoped canonical context is authoritative.  Policy rejection
  // may use an uncompensated trace, but never the packet-ack rewind estimate.
  if (canonicalContextAvailable) {
    fallbackReason =
        (MaxRewindMs() <= 0 ||
         target.decision.struct_size == sizeof(target.decision))
            ? WORR_REWIND_OBSERVATION_FALLBACK_POLICY_REJECTED
            : WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED;
    MaybeReportDiagnostics();
    return false;
  }

  const uint32_t currentFrame = gi.ServerFrame();
  const uint32_t acknowledgedFrame = fromPlayer->client->cmd.serverFrame;
  if (!acknowledgedFrame || acknowledgedFrame >= currentFrame) {
    ++diagnostics.rejectedClock;
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED;
    MaybeReportDiagnostics();
    return false;
  }

  uint64_t snapshotTimeUs = 0;
  int estimatedIntervalMs = 0;
  if (!FindLegacySnapshotTime(poseTracks[shooterIndex], acknowledgedFrame,
                              fromPlayer->client->cmd.serverFrameDelta,
                              snapshotTimeUs, estimatedIntervalMs)) {
    ++diagnostics.missingHistory;
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_HISTORY_MISS;
    MaybeReportDiagnostics();
    return false;
  }

  uint64_t currentTimeUs = 0;
  if (!CurrentAuthoritativeTimeUs(currentTimeUs) ||
      snapshotTimeUs > currentTimeUs) {
    ++diagnostics.rejectedClock;
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED;
    MaybeReportDiagnostics();
    return false;
  }
  const uint64_t interpolationBiasUs =
      static_cast<uint64_t>(InterpolationBiasMs(estimatedIntervalMs)) *
      UINT64_C(1000);
  uint64_t rawAgeUs = currentTimeUs - snapshotTimeUs;
  rawAgeUs = rawAgeUs > std::numeric_limits<uint64_t>::max() -
                            interpolationBiasUs
                 ? std::numeric_limits<uint64_t>::max()
                 : rawAgeUs + interpolationBiasUs;
  const uint64_t maximumUs =
      static_cast<uint64_t>(MaxRewindMs()) * UINT64_C(1000);
  if (!maximumUs) {
    fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_POLICY_REJECTED;
    return false;
  }
  const uint64_t rewindUs = std::min(rawAgeUs, maximumUs);
  if (rewindUs != rawAgeUs)
    ++diagnostics.capped;

  target.timeUs = currentTimeUs - rewindUs;
  target.mapEpoch = historyMapEpoch;
  target.canonical = false;
  fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_NONE;
  ++diagnostics.sessions;
  return true;
}

[[nodiscard]] bool EligibleLiveMover(const gentity_t *entity) {
  return entity && entity->inUse && !entity->client && entity->linked &&
         entity->solid == SOLID_BSP &&
         (entity->moveType == MoveType::Push || entity->moveType == MoveType::Stop) &&
         entity->s.modelIndex > MODELINDEX_WORLD &&
         entity->s.modelIndex != MODELINDEX_PLAYER;
}

[[nodiscard]] bool CurrentCollisionMap(
    worr_sgame_rewind_collision::Map &map) {
  map = {};
  if (!rewindCollisionImport ||
      rewindCollisionImport->struct_size != sizeof(*rewindCollisionImport) ||
      rewindCollisionImport->api_version !=
          worr_sgame_rewind_collision::kApiVersion ||
      !rewindCollisionImport->GetMapIdentity ||
      !rewindCollisionImport->ResolveInlineBrush ||
      !rewindCollisionImport->TraceTransformed ||
      !rewindCollisionImport->GetMapIdentity(&map)) {
    return false;
  }
  return map.struct_size == sizeof(map) &&
         map.schema_version == worr_sgame_rewind_collision::kSchemaVersion &&
         map.map_epoch != 0 && map.inline_model_count != 0 &&
         map.reserved0 == 0;
}

[[nodiscard]] bool ResolveMoverAsset(
    const gentity_t &entity, const worr_sgame_rewind_collision::Map &map,
    worr_sgame_rewind_collision::Asset &asset) {
  asset = {};
  if (!EligibleLiveMover(&entity) || !rewindCollisionImport ||
      !rewindCollisionImport->ResolveInlineBrush ||
      !rewindCollisionImport->ResolveInlineBrush(
          map.map_epoch, static_cast<uint32_t>(entity.s.modelIndex), &asset)) {
    return false;
  }
  return asset.struct_size == sizeof(asset) &&
         asset.schema_version == worr_sgame_rewind_collision::kSchemaVersion &&
         asset.handle.map_epoch == map.map_epoch && asset.handle.asset_id != 0 &&
         asset.handle.asset_hash != 0 &&
         asset.kind == worr_sgame_rewind_collision::kAssetInlineBrush &&
         asset.source_model_index == static_cast<uint32_t>(entity.s.modelIndex) &&
         asset.map_checksum == map.map_checksum && asset.reserved0 == 0 &&
         std::isfinite(asset.local_mins[0]) && std::isfinite(asset.local_mins[1]) &&
         std::isfinite(asset.local_mins[2]) && std::isfinite(asset.local_maxs[0]) &&
         std::isfinite(asset.local_maxs[1]) && std::isfinite(asset.local_maxs[2]) &&
         asset.local_mins[0] <= asset.local_maxs[0] &&
         asset.local_mins[1] <= asset.local_maxs[1] &&
         asset.local_mins[2] <= asset.local_maxs[2];
}

[[nodiscard]] MoverTrack *FindMoverTrack(worr_event_entity_ref_v1 identity) {
  for (MoverTrack &track : moverTracks) {
    if (track.initialized && track.identity.index == identity.index &&
        track.identity.generation == identity.generation) {
      return &track;
    }
  }
  return nullptr;
}

[[nodiscard]] MoverTrack *AcquireMoverTrack(
    worr_event_entity_ref_v1 identity) {
  if (MoverTrack *existing = FindMoverTrack(identity))
    return existing;

  MoverTrack *reusable = nullptr;
  for (MoverTrack &track : moverTracks) {
    if (!track.initialized) {
      reusable = &track;
      break;
    }
    gentity_t *current = EntityFromRef(track.identity);
    if (!current || !EligibleLiveMover(current)) {
      reusable = &track;
      break;
    }
  }
  if (!reusable || !InitMoverTrack(*reusable, identity.index))
    return nullptr;
  reusable->identity = identity;
  return reusable;
}

[[nodiscard]] bool BuildPose(const gentity_t &entity, uint64_t timeUs,
                             worr_rewind_pose_v1 &pose) {
  worr_event_entity_ref_v1 identity{};
  if (!EntityRef(&entity, identity))
    return false;

  pose = {};
  pose.struct_size = sizeof(pose);
  pose.schema_version = WORR_REWIND_ABI_VERSION;
  pose.model_revision = WORR_REWIND_POSE_MODEL_REVISION;
  pose.entity = identity;
  pose.mover = AbsentEntityRef();
  pose.map_epoch = historyMapEpoch;
  pose.server_tick = captureTick;
  pose.server_time_us = timeUs;
  pose.solid = static_cast<uint32_t>(entity.solid);
  pose.clip_flags = static_cast<uint32_t>(entity.clipMask);
  pose.lifecycle = entity.deadFlag ? WORR_REWIND_LIFECYCLE_DEAD
                                   : WORR_REWIND_LIFECYCLE_ALIVE;

  const bool linked = entity.linked && entity.solid != SOLID_NOT;
  if (linked) {
    pose.flags |= WORR_REWIND_POSE_LINKED;
    pose.collision_shape = WORR_REWIND_COLLISION_BOUNDS;
  }
  if (linked && entity.takeDamage && !entity.deadFlag)
    pose.flags |= WORR_REWIND_POSE_DAMAGEABLE;
  if (entity.s.event == EV_PLAYER_TELEPORT ||
      entity.s.event == EV_OTHER_TELEPORT) {
    pose.flags |= WORR_REWIND_POSE_DISCONTINUITY_TELEPORT;
  }

  CopyVector(pose.origin, entity.s.origin);
  CopyVector(pose.angles, entity.s.angles);
  CopyVector(pose.velocity, entity.velocity);
  CopyVector(pose.mins, entity.mins);
  CopyVector(pose.maxs, entity.maxs);
  if (EligibleLiveMover(entity.groundEntity)) {
    worr_event_entity_ref_v1 mover{};
    if (!EntityRef(entity.groundEntity, mover))
      return false;
    pose.flags |= WORR_REWIND_POSE_HAS_MOVER;
    pose.mover = mover;
    CopyVector(pose.mover_relative_origin,
               entity.s.origin - entity.groundEntity->s.origin);
    CopyVector(pose.mover_relative_angles,
               entity.s.angles - entity.groundEntity->s.angles);
  }
  return Worr_RewindPoseValidateV1(&pose);
}

[[nodiscard]] bool BuildMoverPose(
    const gentity_t &entity, uint64_t timeUs,
    const worr_sgame_rewind_collision::Map &map,
    worr_rewind_pose_v1 &pose) {
  worr_event_entity_ref_v1 identity{};
  worr_sgame_rewind_collision::Asset asset{};
  if (!EntityRef(&entity, identity) || map.map_epoch != historyMapEpoch ||
      !ResolveMoverAsset(entity, map, asset)) {
    return false;
  }

  pose = {};
  pose.struct_size = sizeof(pose);
  pose.schema_version = WORR_REWIND_ABI_VERSION;
  pose.model_revision = WORR_REWIND_POSE_MODEL_REVISION;
  pose.flags = WORR_REWIND_POSE_LINKED;
  pose.entity = identity;
  pose.mover = AbsentEntityRef();
  pose.map_epoch = map.map_epoch;
  pose.server_tick = captureTick;
  pose.lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
  pose.solid = static_cast<uint32_t>(entity.solid);
  pose.clip_flags = static_cast<uint32_t>(entity.clipMask);
  pose.collision_shape = WORR_REWIND_COLLISION_BRUSH_MODEL;
  pose.collision_asset_id = asset.handle.asset_id;
  pose.server_time_us = timeUs;
  CopyVector(pose.origin, entity.s.origin);
  CopyVector(pose.angles, entity.s.angles);
  CopyVector(pose.velocity, entity.velocity);
  std::copy(std::begin(asset.local_mins), std::end(asset.local_mins),
            std::begin(pose.mins));
  std::copy(std::begin(asset.local_maxs), std::end(asset.local_maxs),
            std::begin(pose.maxs));
  return Worr_RewindPoseValidateV1(&pose);
}

[[nodiscard]] bool HistoricalBrushAssetMatches(
    const worr_rewind_pose_v1 &pose,
    const worr_sgame_rewind_collision::Asset &asset) {
  return pose.map_epoch == asset.handle.map_epoch &&
         pose.collision_asset_id == asset.handle.asset_id &&
         pose.collision_shape == WORR_REWIND_COLLISION_BRUSH_MODEL &&
         std::memcmp(pose.mins, asset.local_mins, sizeof(pose.mins)) == 0 &&
         std::memcmp(pose.maxs, asset.local_maxs, sizeof(pose.maxs)) == 0;
}

[[nodiscard]] bool BuildHistoricalBrushContext(
    const worr_rewind_scene_v1 *scene, HistoricalBrushContext &context) {
  context = {};
  if (!scene || !Worr_RewindSceneValidateV1(scene))
    return false;

  bool hasBrush = false;
  for (uint32_t index = 0; index < scene->count; ++index) {
    if (scene->slots[index].pose.collision_shape ==
        WORR_REWIND_COLLISION_BRUSH_MODEL) {
      hasBrush = true;
      break;
    }
  }
  if (!hasBrush)
    return true;

  if (!CurrentCollisionMap(context.map) ||
      context.map.map_epoch != scene->map_epoch) {
    return false;
  }

  for (uint32_t index = 0; index < scene->count; ++index) {
    const worr_rewind_pose_v1 &pose = scene->slots[index].pose;
    if (pose.collision_shape != WORR_REWIND_COLLISION_BRUSH_MODEL)
      continue;
    if (context.count == context.entries.size())
      return false;

    gentity_t *current = EntityFromRef(pose.entity);
    worr_sgame_rewind_collision::Asset asset{};
    if (!current || !EligibleLiveMover(current) ||
        !ResolveMoverAsset(*current, context.map, asset) ||
        !HistoricalBrushAssetMatches(pose, asset)) {
      return false;
    }

    HistoricalBrushEntry &entry = context.entries[context.count++];
    entry.identity = pose.entity;
    entry.current = current;
    entry.asset = asset;
  }
  return true;
}

[[nodiscard]] const HistoricalBrushEntry *FindHistoricalBrushEntry(
    const HistoricalBrushContext &context,
    worr_event_entity_ref_v1 identity) {
  for (uint32_t index = 0; index < context.count; ++index) {
    const HistoricalBrushEntry &entry = context.entries[index];
    if (entry.identity.index == identity.index &&
        entry.identity.generation == identity.generation) {
      return &entry;
    }
  }
  return nullptr;
}

[[nodiscard]] bool HistoricalBrushExcludesCurrent(
    const HistoricalBrushContext *context, const gentity_t *entity) {
  if (!context || !entity)
    return false;
  for (uint32_t index = 0; index < context->count; ++index) {
    if (context->entries[index].current == entity)
      return true;
  }
  return false;
}

[[nodiscard]] bool PrepareCaptureTime(uint64_t timeUs) {
  if (lastCaptureTimeUs == std::numeric_limits<uint64_t>::max() ||
      timeUs > lastCaptureTimeUs) {
    if (captureTick == std::numeric_limits<uint32_t>::max()) {
      ++diagnostics.appendRejected;
      return false;
    }
    ++captureTick;
    lastCaptureTimeUs = timeUs;
    return true;
  }
  if (timeUs < lastCaptureTimeUs) {
    // The explicit map hook should precede a clock reset.  If it did not,
    // discard all old poses instead of accepting cross-map interpolation.
    ++provisionalMapEpoch;
    if (provisionalMapEpoch == 0)
      provisionalMapEpoch = 1;
    authoritativeMapEpoch = 0;
    awaitingAuthoritativeMapEpoch = true;
    ResetHistories(provisionalMapEpoch);
    captureTick = 1;
    lastCaptureTimeUs = timeUs;
  }
  return true;
}

[[nodiscard]] bool EligibleLivePlayer(const gentity_t *entity) {
  return entity && entity->inUse && entity->client && entity->linked &&
         entity->solid != SOLID_NOT && entity->takeDamage && !entity->deadFlag;
}

[[nodiscard]] bool CaptureSceneRoster(
    std::array<worr_event_entity_ref_v1, kSceneCandidateCapacity> &roster,
    uint32_t &rosterCount) {
  roster = {};
  rosterCount = 0;
  const std::size_t clientCount = std::min<std::size_t>(
      static_cast<std::size_t>(game.maxClients), poseTracks.size());
  for (std::size_t index = 0; index < clientCount; ++index) {
    gentity_t *current = &g_entities[index + 1u];
    if (!EligibleLivePlayer(current))
      continue;
    worr_event_entity_ref_v1 identity{};
    if (!EntityRef(current, identity) || rosterCount == roster.size())
      return false;
    roster[rosterCount++] = identity;
  }

  const std::size_t total = std::min<std::size_t>(
      static_cast<std::size_t>(MAX_ENTITIES),
      static_cast<std::size_t>(globals.numEntities));
  const std::size_t firstMover = std::min<std::size_t>(
      total, static_cast<std::size_t>(game.maxClients) + 1u);
  for (std::size_t index = firstMover; index < total; ++index) {
    gentity_t *current = &g_entities[index];
    if (!EligibleLiveMover(current))
      continue;
    worr_event_entity_ref_v1 identity{};
    if (!EntityRef(current, identity) || !FindMoverTrack(identity) ||
        rosterCount == roster.size()) {
      return false;
    }
    roster[rosterCount++] = identity;
    ++diagnostics.moverSceneCandidates;
  }
  return true;
}

[[nodiscard]] bool SameEligibleRoster(
    const FrozenSceneCache &cache,
    const std::array<worr_event_entity_ref_v1, kSceneCandidateCapacity>
        &roster,
    uint32_t rosterCount) {
  if (cache.eligibleRosterCount != rosterCount)
    return false;
  for (uint32_t i = 0; i < rosterCount; ++i) {
    if (cache.eligibleRoster[i].index != roster[i].index ||
        cache.eligibleRoster[i].generation != roster[i].generation) {
      return false;
    }
  }
  return true;
}

enum CanonicalSceneRejection : uint32_t {
  CANONICAL_SCENE_REJECTION_NONE = 0,
  CANONICAL_SCENE_REJECTION_ROSTER = 1,
  CANONICAL_SCENE_REJECTION_INIT = 2,
  CANONICAL_SCENE_REJECTION_CLIENT_INDEX = 3,
  CANONICAL_SCENE_REJECTION_MOVER_TRACK = 4,
  CANONICAL_SCENE_REJECTION_HISTORY_QUERY = 5,
  CANONICAL_SCENE_REJECTION_HISTORY_MISS = 6,
  CANONICAL_SCENE_REJECTION_ADD_RESULT = 7,
  CANONICAL_SCENE_REJECTION_SEAL = 8,
};

[[nodiscard]] const worr_rewind_scene_v1 *CanonicalScene(
    std::size_t shooterIndex, const worr_rewind_policy_decision_v1 &decision,
    uint32_t *rejection = nullptr) {
  if (rejection)
    *rejection = CANONICAL_SCENE_REJECTION_NONE;
  const auto reject = [rejection](uint32_t reason) {
    if (rejection)
      *rejection = reason;
    return static_cast<const worr_rewind_scene_v1 *>(nullptr);
  };
  FrozenSceneCache &cache = sceneCaches[shooterIndex];
  std::array<worr_event_entity_ref_v1, kSceneCandidateCapacity>
      eligibleRoster{};
  uint32_t eligibleRosterCount = 0;
  if (!CaptureSceneRoster(eligibleRoster, eligibleRosterCount)) {
    ++diagnostics.missingHistory;
    ++diagnostics.scenesRejected;
    return reject(CANONICAL_SCENE_REJECTION_ROSTER);
  }
  if (cache.valid && SameCommand(cache.commandId, decision.command_id) &&
      SameCanonicalBytes(cache.decision, decision) &&
      SameEligibleRoster(cache, eligibleRoster, eligibleRosterCount)) {
    ++diagnostics.scenesReused;
    return &cache.scene;
  }

  cache = {};
  cache.commandId = decision.command_id;
  cache.decision = decision;
  cache.eligibleRoster = eligibleRoster;
  cache.eligibleRosterCount = eligibleRosterCount;
  if (!Worr_RewindSceneInitV1(&cache.scene, cache.storage.data(),
                              static_cast<uint32_t>(cache.storage.size()),
                              &decision)) {
    ++diagnostics.scenesRejected;
    return reject(CANONICAL_SCENE_REJECTION_INIT);
  }

  for (uint32_t rosterIndex = 0; rosterIndex < eligibleRosterCount;
       ++rosterIndex) {
    const worr_event_entity_ref_v1 identity = eligibleRoster[rosterIndex];
    worr_rewind_history_v1 *history = nullptr;
    if (identity.index >= 1u &&
        identity.index <= static_cast<uint32_t>(game.maxClients)) {
      const std::size_t index =
          static_cast<std::size_t>(identity.index - 1u);
      if (index >= poseTracks.size()) {
        ++diagnostics.scenesRejected;
        return reject(CANONICAL_SCENE_REJECTION_CLIENT_INDEX);
      }
      history = &poseTracks[index].history;
    } else {
      MoverTrack *track = FindMoverTrack(identity);
      if (!track) {
        ++diagnostics.missingHistory;
        ++diagnostics.scenesRejected;
        return reject(CANONICAL_SCENE_REJECTION_MOVER_TRACK);
      }
      history = &track->history;
    }

    worr_rewind_pose_query_v1 query{};
    query.struct_size = sizeof(query);
    query.schema_version = WORR_REWIND_ABI_VERSION;
    query.entity = identity;
    query.map_epoch = decision.snapshot_id.epoch;
    query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
    query.target_time_us = decision.applied_time_us;
    worr_rewind_pose_result_v1 result{};
    if (!Worr_RewindHistoryQueryV1(history, &query, &result)) {
      ++diagnostics.scenesRejected;
      return reject(CANONICAL_SCENE_REJECTION_HISTORY_QUERY);
    }
    if (!result.found) {
      ++diagnostics.missingHistory;
      ++diagnostics.scenesRejected;
      return reject(CANONICAL_SCENE_REJECTION_HISTORY_MISS);
    }
    const uint32_t requiredFlags =
        result.pose.collision_shape == WORR_REWIND_COLLISION_BRUSH_MODEL
            ? WORR_REWIND_POSE_LINKED
            : (WORR_REWIND_POSE_LINKED | WORR_REWIND_POSE_DAMAGEABLE);
    if ((result.pose.flags & requiredFlags) != requiredFlags) {
      continue;
    }
    if (!Worr_RewindSceneAddResultV1(&cache.scene, &result)) {
      ++diagnostics.scenesRejected;
      return reject(CANONICAL_SCENE_REJECTION_ADD_RESULT);
    }
  }
  if (!Worr_RewindSceneSealV1(&cache.scene)) {
    ++diagnostics.scenesRejected;
    return reject(CANONICAL_SCENE_REJECTION_SEAL);
  }
  cache.valid = true;
  ++diagnostics.scenesBuilt;
  return &cache.scene;
}

void MergeTrace(trace_t &destination, const trace_t &candidate,
                gentity_t *entity) {
  destination.allSolid |= candidate.allSolid;
  destination.startSolid |= candidate.startSolid;
  if (candidate.fraction < destination.fraction) {
    destination.fraction = candidate.fraction;
    destination.endPos = candidate.endPos;
    destination.plane = candidate.plane;
    destination.surface = candidate.surface;
    destination.contents = candidate.contents;
    destination.ent = entity;
  }
  if (candidate.allSolid || candidate.startSolid)
    destination.ent = entity;
}

[[nodiscard]] bool PointerIgnored(const gentity_t *entity,
                                  const gentity_t *passEntity,
                                  gentity_t *const *ignoredEntities,
                                  std::size_t ignoredEntityCount) {
  if (entity == passEntity)
    return true;
  for (std::size_t i = 0; i < ignoredEntityCount; ++i) {
    if (entity == ignoredEntities[i])
      return true;
  }
  return false;
}

[[nodiscard]] bool ShouldSkipCurrentEntity(
    const gentity_t *entity, const gentity_t *passEntity,
    gentity_t *const *ignoredEntities, std::size_t ignoredEntityCount,
    contents_t contentMask) {
  if (!entity || !entity->inUse || !entity->linked ||
      entity->solid == SOLID_NOT ||
      PointerIgnored(entity, passEntity, ignoredEntities,
                     ignoredEntityCount)) {
    return true;
  }
  if (passEntity &&
      (entity->owner == passEntity || passEntity->owner == entity)) {
    return true;
  }
  if (!(contentMask & CONTENTS_DEADMONSTER) &&
      (entity->svFlags & SVF_DEADMONSTER)) {
    return true;
  }
  if (!(contentMask & CONTENTS_PROJECTILE) &&
      (entity->svFlags & SVF_PROJECTILE)) {
    return true;
  }
  return false;
}

trace_t ClipEntity(gentity_t *clipEntity, const Vector3 &start,
                   const Vector3 *mins, const Vector3 *maxs,
                   const Vector3 &end, contents_t contentMask) {
  if (mins && maxs)
    return gi.clip(clipEntity, start, *mins, *maxs, end, contentMask);
  return gi.clip(clipEntity, start, end, contentMask);
}

[[nodiscard]] TraceBounds CalculateTraceBounds(const Vector3 &start,
                                                const Vector3 *mins,
                                                const Vector3 *maxs,
                                                const Vector3 &end) {
  const Vector3 traceMins = mins ? *mins : vec3_origin;
  const Vector3 traceMaxs = maxs ? *maxs : vec3_origin;
  TraceBounds bounds{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (end[axis] > start[axis]) {
      bounds.mins[axis] = start[axis] + traceMins[axis] - 1.0f;
      bounds.maxs[axis] = end[axis] + traceMaxs[axis] + 1.0f;
    } else {
      bounds.mins[axis] = end[axis] + traceMins[axis] - 1.0f;
      bounds.maxs[axis] = start[axis] + traceMaxs[axis] + 1.0f;
    }
  }
  return bounds;
}

[[nodiscard]] bool PoseTouchesBounds(const worr_rewind_pose_v1 &pose,
                                     const TraceBounds &bounds) {
  const Vector3 origin = ToVector(pose.origin);
  const Vector3 absoluteMins = origin + ToVector(pose.mins);
  const Vector3 absoluteMaxs = origin + ToVector(pose.maxs);
  return absoluteMins.x <= bounds.maxs.x &&
         absoluteMins.y <= bounds.maxs.y &&
         absoluteMins.z <= bounds.maxs.z &&
         absoluteMaxs.x >= bounds.mins.x &&
         absoluteMaxs.y >= bounds.mins.y &&
         absoluteMaxs.z >= bounds.mins.z;
}

trace_t TraceCurrentScene(const Vector3 &start, const Vector3 *mins,
                          const Vector3 *maxs, const Vector3 &end,
                          gentity_t *passEntity, contents_t contentMask,
                          gentity_t *const *ignoredEntities,
                          std::size_t ignoredEntityCount,
                          bool replaceLivingPlayers,
                          const HistoricalBrushContext *historicalBrushes,
                          const TraceBounds &bounds) {
  trace_t result = ClipEntity(world, start, mins, maxs, end, contentMask);
  if (result.fraction == 0.0f)
    return result;

  const std::size_t candidateCount = gi.BoxEntities(
      bounds.mins, bounds.maxs, traceCandidates.data(), traceCandidates.size(),
      AREA_SOLID, nullptr, nullptr);
  for (std::size_t i = 0; i < candidateCount && !result.allSolid; ++i) {
    gentity_t *entity = traceCandidates[i];
    if (HistoricalBrushExcludesCurrent(historicalBrushes, entity))
      continue;
    if (ShouldSkipCurrentEntity(entity, passEntity, ignoredEntities,
                                ignoredEntityCount, contentMask)) {
      continue;
    }
    if (replaceLivingPlayers &&
        (entity->client || (entity->svFlags & SVF_PLAYER)) &&
        entity->takeDamage && !entity->deadFlag) {
      continue;
    }
    const trace_t clipped =
        ClipEntity(entity, start, mins, maxs, end, contentMask);
    MergeTrace(result, clipped, entity);
  }
  return result;
}

[[nodiscard]] bool InitIgnoreSet(
    worr_rewind_ignore_set_v1 &ignoreSet,
    std::array<worr_event_entity_ref_v1, kIgnoreCapacity> &storage,
    gentity_t *passEntity, gentity_t *const *ignoredEntities,
    std::size_t ignoredEntityCount) {
  if (!Worr_RewindIgnoreSetInitV1(
          &ignoreSet, storage.data(), static_cast<uint32_t>(storage.size()))) {
    return false;
  }
  auto add = [&ignoreSet](gentity_t *entity) {
    worr_event_entity_ref_v1 ref{};
    return !EntityRef(entity, ref) || Worr_RewindIgnoreSetAddV1(&ignoreSet, ref);
  };
  if (!add(passEntity))
    return false;
  for (std::size_t i = 0; i < ignoredEntityCount; ++i) {
    if (!add(ignoredEntities[i]))
      return false;
  }
  diagnostics.ignoredIdentities += ignoreSet.count;
  return true;
}

void ConfigureHistoricalProxy(const worr_rewind_pose_v1 &pose,
                              const gentity_t &current) {
  historicalProxy.inUse = true;
  historicalProxy.client = nullptr;
  historicalProxy.owner = nullptr;
  historicalProxy.linked = false;
  historicalProxy.s.number = 0;
  historicalProxy.s.modelIndex = 0;
  historicalProxy.s.origin = ToVector(pose.origin);
  historicalProxy.s.angles = ToVector(pose.angles);
  historicalProxy.mins = ToVector(pose.mins);
  historicalProxy.maxs = ToVector(pose.maxs);
  historicalProxy.solid = static_cast<solid_t>(pose.solid);
  historicalProxy.clipMask = static_cast<contents_t>(pose.clip_flags);
  historicalProxy.svFlags = current.svFlags;
}

void ClipHistoricalPose(trace_t &result, const worr_rewind_pose_v1 &pose,
                        uint32_t queryReason, const TraceBounds &bounds,
                        const Vector3 &start, const Vector3 *mins,
                        const Vector3 *maxs, const Vector3 &end,
                        gentity_t *passEntity, contents_t contentMask,
                        gentity_t *const *ignoredEntities,
                        std::size_t ignoredEntityCount) {
  if (result.allSolid ||
      (pose.flags & (WORR_REWIND_POSE_LINKED |
                     WORR_REWIND_POSE_DAMAGEABLE)) !=
          (WORR_REWIND_POSE_LINKED | WORR_REWIND_POSE_DAMAGEABLE) ||
      pose.collision_shape != WORR_REWIND_COLLISION_BOUNDS ||
      !PoseTouchesBounds(pose, bounds)) {
    return;
  }

  gentity_t *current = EntityFromRef(pose.entity);
  if (!current || !current->client || !current->takeDamage ||
      current->deadFlag ||
      ShouldSkipCurrentEntity(current, passEntity, ignoredEntities,
                              ignoredEntityCount, contentMask)) {
    return;
  }

  ConfigureHistoricalProxy(pose, *current);
  const trace_t clipped = ClipEntity(&historicalProxy, start, mins, maxs, end,
                                     contentMask);
  MergeTrace(result, clipped, current);
  ++diagnostics.targets;
  if (queryReason == WORR_REWIND_QUERY_INTERPOLATED)
    ++diagnostics.interpolated;
  if (queryReason == WORR_REWIND_QUERY_DISCONTINUITY_FLOOR)
    ++diagnostics.discontinuities;
}

// The engine receives only immutable asset identity and the transform copied
// into the sealed scene. The current mover is used solely to attach the
// resulting trace to a generation-validated entity; it is never moved,
// unlinked, or otherwise used as collision input.
[[nodiscard]] bool ClipHistoricalBrushPose(
    trace_t &result, const worr_rewind_pose_v1 &pose,
    const HistoricalBrushContext &context, const Vector3 &start,
    const Vector3 *mins, const Vector3 *maxs, const Vector3 &end,
    contents_t contentMask) {
  if (result.allSolid ||
      (pose.flags & WORR_REWIND_POSE_LINKED) == 0 ||
      pose.collision_shape != WORR_REWIND_COLLISION_BRUSH_MODEL) {
    return false;
  }

  const HistoricalBrushEntry *entry =
      FindHistoricalBrushEntry(context, pose.entity);
  if (!entry || !entry->current || EntityFromRef(pose.entity) != entry->current ||
      !HistoricalBrushAssetMatches(pose, entry->asset) ||
      !rewindCollisionImport || !rewindCollisionImport->TraceTransformed) {
    return false;
  }

  worr_sgame_rewind_collision::TraceRequest request{};
  request.struct_size = sizeof(request);
  request.schema_version = worr_sgame_rewind_collision::kSchemaVersion;
  request.asset = entry->asset.handle;
  request.contents_mask = static_cast<uint32_t>(contentMask);
  CopyVector(request.start, start);
  CopyVector(request.end, end);
  if (mins)
    CopyVector(request.mins, *mins);
  if (maxs)
    CopyVector(request.maxs, *maxs);
  CopyVector(request.origin, ToVector(pose.origin));
  CopyVector(request.angles, ToVector(pose.angles));

  trace_t clipped{};
  if (!rewindCollisionImport->TraceTransformed(&request, &clipped))
    return false;
  clipped.ent = entry->current;
  MergeTrace(result, clipped, entry->current);
  ++diagnostics.historicalBrushes;
  return true;
}

trace_t NativeTrace(const Vector3 &start, const Vector3 *mins,
                    const Vector3 *maxs, const Vector3 &end,
                    gentity_t *passEntity, contents_t contentMask,
                    gentity_t *const *ignoredEntities,
                    std::size_t ignoredEntityCount) {
  if (!ignoredEntityCount) {
    if (mins && maxs)
      return gi.trace(start, *mins, *maxs, end, passEntity, contentMask);
    return gi.traceLine(start, end, passEntity, contentMask);
  }
  const TraceBounds bounds = CalculateTraceBounds(start, mins, maxs, end);
  return TraceCurrentScene(start, mins, maxs, end, passEntity, contentMask,
                           ignoredEntities, ignoredEntityCount, false, nullptr,
                           bounds);
}

[[nodiscard]] bool ObservationEnabled() {
  return sg_lag_compensation_debug &&
         sg_lag_compensation_debug->integer >= 2;
}

void HashObservationBytes(uint64_t &hash, const void *data, std::size_t size) {
  constexpr uint64_t kFnvPrime = UINT64_C(1099511628211);
  const auto *bytes = static_cast<const uint8_t *>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= kFnvPrime;
  }
}

template <typename T>
void HashObservationValue(uint64_t &hash, const T &value) {
  HashObservationBytes(hash, &value, sizeof(value));
}

// A query guard covers every live collision participant that this module may
// replace with a sealed historical pose. The before/after fingerprint is
// diagnostic evidence that a lag-compensated trace did not mutate an edict.
[[nodiscard]] uint64_t AuthoritativeCollisionHash() {
  uint64_t hash = UINT64_C(14695981039346656037);
  const std::size_t count = std::min<std::size_t>(
      static_cast<std::size_t>(game.maxClients), poseTracks.size());
  for (std::size_t index = 0; index < count; ++index) {
    const gentity_t &entity = g_entities[index + 1u];
    HashObservationValue(hash, entity.inUse);
    HashObservationValue(hash, entity.linked);
    HashObservationValue(hash, entity.s.number);
    HashObservationValue(hash, clientLifeGenerations[index]);
    HashObservationValue(hash, entity.solid);
    HashObservationValue(hash, entity.clipMask);
    HashObservationValue(hash, entity.svFlags);
    HashObservationValue(hash, entity.takeDamage);
    HashObservationValue(hash, entity.deadFlag);
    HashObservationValue(hash, entity.health);
    HashObservationValue(hash, entity.s.origin);
    HashObservationValue(hash, entity.s.angles);
    HashObservationValue(hash, entity.velocity);
    HashObservationValue(hash, entity.mins);
    HashObservationValue(hash, entity.maxs);
    worr_event_entity_ref_v1 groundMover{};
    if (EligibleLiveMover(entity.groundEntity))
      (void)EntityRef(entity.groundEntity, groundMover);
    HashObservationValue(hash, groundMover);
  }

  const std::size_t total = std::min<std::size_t>(
      static_cast<std::size_t>(MAX_ENTITIES),
      static_cast<std::size_t>(globals.numEntities));
  const std::size_t firstMover = std::min<std::size_t>(
      total, static_cast<std::size_t>(game.maxClients) + 1u);
  for (std::size_t index = firstMover; index < total; ++index) {
    const gentity_t &entity = g_entities[index];
    if (!EligibleLiveMover(&entity))
      continue;
    HashObservationValue(hash, entity.inUse);
    HashObservationValue(hash, entity.linked);
    HashObservationValue(hash, entity.s.number);
    HashObservationValue(hash, entity.spawn_count);
    HashObservationValue(hash, entity.solid);
    HashObservationValue(hash, entity.clipMask);
    HashObservationValue(hash, entity.svFlags);
    HashObservationValue(hash, entity.moveType);
    HashObservationValue(hash, entity.s.modelIndex);
    HashObservationValue(hash, entity.s.origin);
    HashObservationValue(hash, entity.s.angles);
    HashObservationValue(hash, entity.velocity);
    HashObservationValue(hash, entity.mins);
    HashObservationValue(hash, entity.maxs);
  }
  return hash ? hash : UINT64_C(1);
}

void CopyDecisionToObservation(
    const worr_rewind_policy_decision_v1 &decision,
    worr_rewind_observation_v1 &observation) {
  if (decision.struct_size != sizeof(decision) ||
      decision.schema_version != WORR_REWIND_ABI_VERSION) {
    return;
  }
  observation.policy_reason = decision.reason;
  observation.command_id = decision.command_id;
  observation.snapshot_id = decision.snapshot_id;
  observation.source_snapshot_id = decision.source_snapshot_id;
  observation.requested_time_us = decision.requested_time_us;
  observation.mapped_time_us = decision.mapped_time_us;
  observation.applied_time_us = decision.applied_time_us;
  observation.mapping_error_bound_us = decision.mapping_error_bound_us;
}

trace_t TraceHistoricalScene(gentity_t *fromPlayer, const Vector3 &start,
                             const Vector3 *mins, const Vector3 *maxs,
                             const Vector3 &end, gentity_t *passEntity,
                             contents_t contentMask,
                             gentity_t *const *ignoredEntities,
                             std::size_t ignoredEntityCount,
                             uint32_t weaponPolicy) {
  const bool observe = ObservationEnabled();
  // A canonical scene can legitimately terminate on a historical brush mover.
  // Keep that distinct from an unblocked historical query in telemetry without
  // labeling unrelated live baseline entities as historical hits.
  bool historicalBrushHit = false;
  worr_rewind_observation_v1 observation{};
  std::chrono::steady_clock::time_point observationStart{};
  if (observe) {
    observationStart = std::chrono::steady_clock::now();
    (void)Worr_RewindObservationInitV1(&observation, weaponPolicy);
    if (g_lagCompensation && g_lagCompensation->integer)
      observation.flags |= WORR_REWIND_OBSERVATION_MASTER_ENABLED;
    observation.authoritative_hash_before = AuthoritativeCollisionHash();
  }

  auto finishObservation = [&](trace_t result) {
    if (!observe)
      return result;

    if (observation.path == WORR_REWIND_OBSERVATION_PATH_NONE)
      observation.path = WORR_REWIND_OBSERVATION_PATH_CURRENT_WORLD;
    observation.trace_fraction = std::clamp(result.fraction, 0.0f, 1.0f);
    if (result.ent)
      (void)EntityRef(result.ent, observation.hit_entity);

    if (observation.outcome == WORR_REWIND_OBSERVATION_OUTCOME_NONE) {
      if ((observation.path == WORR_REWIND_OBSERVATION_PATH_CANONICAL ||
           observation.path == WORR_REWIND_OBSERVATION_PATH_LEGACY) &&
          (observation.flags & WORR_REWIND_OBSERVATION_CURRENT_FALLBACK) == 0) {
        observation.outcome =
            result.ent && result.fraction < 1.0f &&
                    (result.ent->client || historicalBrushHit)
                ? WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT
                : WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_MISS;
      } else if (result.fraction == 0.0f) {
        observation.outcome = WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_BLOCKED;
        if (observation.fallback_reason ==
            WORR_REWIND_OBSERVATION_FALLBACK_NONE) {
          observation.fallback_reason =
              WORR_REWIND_OBSERVATION_FALLBACK_CURRENT_WORLD_BLOCKED;
        }
      } else {
        observation.outcome = result.fraction < 1.0f
                                  ? WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_HIT
                                  : WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_MISS;
      }
    }

    observation.authoritative_hash_after = AuthoritativeCollisionHash();
    observation.flags |= WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED;
    if (observation.authoritative_hash_before ==
        observation.authoritative_hash_after) {
      observation.flags |= WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED;
    }
    const auto elapsed = std::chrono::steady_clock::now() - observationStart;
    observation.duration_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());

    uint64_t sequence = 0;
    const bool observationAppended = Worr_RewindObservationJournalAppendV1(
        &observationJournal, &observation, &sequence);
    if (observationAppended)
      CanonicalRailProbeObserveTrace(fromPlayer, observation);
    if (observationAppended && sg_lag_compensation_debug->integer >= 3) {
      gi.Com_PrintFmt(
          "lagcomp trace seq={} weapon={} path={} outcome={} fallback={} "
          "policy={} query={} candidates={} requested_us={} applied_us={} "
          "fraction={:.6f} scene={} authority_unchanged={} duration_ns={}\n",
          static_cast<unsigned long long>(sequence), observation.weapon_policy,
          observation.path, observation.outcome, observation.fallback_reason,
          observation.policy_reason, observation.query_reason,
          observation.candidate_count,
          static_cast<unsigned long long>(observation.requested_time_us),
          static_cast<unsigned long long>(observation.applied_time_us),
          observation.trace_fraction,
          static_cast<unsigned long long>(observation.scene_hash),
          observation.authoritative_hash_before ==
                  observation.authoritative_hash_after
              ? 1u
              : 0u,
          static_cast<unsigned long long>(observation.duration_ns));
    }
    return result;
  };

  RewindTarget target{};
  bool canonicalContextAvailable = false;
  uint32_t fallbackReason = WORR_REWIND_OBSERVATION_FALLBACK_NONE;
  const bool targetsPlayers = (contentMask & CONTENTS_PLAYER) != 0;
  if (!targetsPlayers || !ResolveTarget(fromPlayer, target,
                                        canonicalContextAvailable,
                                        fallbackReason)) {
    if (observe) {
      observation.path = canonicalContextAvailable
                             ? WORR_REWIND_OBSERVATION_PATH_CANONICAL
                             : WORR_REWIND_OBSERVATION_PATH_CURRENT_WORLD;
      observation.fallback_reason =
          targetsPlayers
              ? fallbackReason
              : WORR_REWIND_OBSERVATION_FALLBACK_NO_PLAYER_CONTENT;
      observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
      if (canonicalContextAvailable) {
        observation.flags |= WORR_REWIND_OBSERVATION_CANONICAL_CONTEXT;
        CopyDecisionToObservation(target.decision, observation);
        if (fallbackReason ==
            WORR_REWIND_OBSERVATION_FALLBACK_POLICY_REJECTED) {
          observation.outcome =
              WORR_REWIND_OBSERVATION_OUTCOME_POLICY_REJECTED;
        }
      }
    }
    return finishObservation(NativeTrace(start, mins, maxs, end, passEntity,
                                         contentMask, ignoredEntities,
                                         ignoredEntityCount));
  }

  if (observe) {
    observation.path = target.canonical
                           ? WORR_REWIND_OBSERVATION_PATH_CANONICAL
                           : WORR_REWIND_OBSERVATION_PATH_LEGACY;
    observation.flags |= WORR_REWIND_OBSERVATION_POLICY_ACCEPTED;
    observation.fallback_reason = WORR_REWIND_OBSERVATION_FALLBACK_NONE;
    if (target.canonical) {
      observation.flags |= WORR_REWIND_OBSERVATION_CANONICAL_CONTEXT;
      CopyDecisionToObservation(target.decision, observation);
    } else {
      observation.requested_time_us = target.timeUs;
      observation.mapped_time_us = target.timeUs;
      observation.applied_time_us = target.timeUs;
    }
  }

  const TraceBounds bounds = CalculateTraceBounds(start, mins, maxs, end);
  const std::size_t shooterIndex = ClientIndex(fromPlayer);
  const worr_rewind_scene_v1 *scene = nullptr;
  HistoricalBrushContext historicalBrushes{};
  if (target.canonical) {
    uint32_t sceneRejection = CANONICAL_SCENE_REJECTION_NONE;
    scene = CanonicalScene(shooterIndex, target.decision, &sceneRejection);
    if (!scene) {
      // Policy accepted, but a sealed immutable scene could not be built.
      // Use an uncompensated collision query; do not use the legacy estimate.
      if (observe) {
        observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
        observation.fallback_reason =
            WORR_REWIND_OBSERVATION_FALLBACK_SCENE_REJECTED;
        observation.outcome = WORR_REWIND_OBSERVATION_OUTCOME_SCENE_REJECTED;
        observation.query_reason = sceneRejection;
      }
      return finishObservation(NativeTrace(
          start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
          ignoredEntityCount));
    }
    if (!BuildHistoricalBrushContext(scene, historicalBrushes)) {
      ++diagnostics.scenesRejected;
      ++diagnostics.historicalBrushRejected;
      if (observe) {
        observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
        observation.fallback_reason =
            WORR_REWIND_OBSERVATION_FALLBACK_SCENE_REJECTED;
        observation.outcome = WORR_REWIND_OBSERVATION_OUTCOME_SCENE_REJECTED;
      }
      return finishObservation(NativeTrace(
          start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
          ignoredEntityCount));
    }
    if (observe) {
      observation.flags |= WORR_REWIND_OBSERVATION_HISTORICAL_QUERY |
                           WORR_REWIND_OBSERVATION_HISTORICAL_SCENE;
      observation.candidate_count = scene->count;
      observation.scene_hash = scene->scene_hash;
      for (uint32_t index = 0; index < scene->count; ++index) {
        observation.query_reason = std::max<uint32_t>(
            observation.query_reason, scene->slots[index].query_reason);
      }
    }
  }

  trace_t result = TraceCurrentScene(
      start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
      ignoredEntityCount, true,
      target.canonical ? &historicalBrushes : nullptr, bounds);
  if (target.canonical &&
      canonicalRailProbe.stage == CanonicalRailProbeStage::AwaitingDamage &&
      canonicalRailProbe.historical_mover_occlusion_required &&
      fromPlayer == canonicalRailProbe.shooter &&
      weaponPolicy == WORR_REWIND_WEAPON_RAILGUN &&
      canonicalRailProbe.historical_mover_relocated &&
      CanonicalRailProbeSameEntity(
          canonicalRailProbe.historical_mover,
          canonicalRailProbe.historical_mover_identity) &&
      canonicalRailProbe.historical_mover->linked &&
      canonicalRailProbe.historical_mover->s.origin ==
          canonicalRailProbe.historical_mover_current_origin &&
      result.fraction == 1.0f) {
    canonicalRailProbe.historical_mover_baseline_clear = true;
  }
  if (result.fraction == 0.0f) {
    if (observe) {
      observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
      observation.fallback_reason =
          WORR_REWIND_OBSERVATION_FALLBACK_CURRENT_WORLD_BLOCKED;
      observation.outcome =
          WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_BLOCKED;
    }
    MaybeReportDiagnostics();
    return finishObservation(result);
  }

  if (target.canonical) {
    worr_rewind_ignore_set_v1 ignoreSet{};
    std::array<worr_event_entity_ref_v1, kIgnoreCapacity> ignoreStorage{};
    if (!InitIgnoreSet(ignoreSet, ignoreStorage, passEntity, ignoredEntities,
                       ignoredEntityCount)) {
      ++diagnostics.scenesRejected;
      if (observe) {
        observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
        observation.fallback_reason =
            WORR_REWIND_OBSERVATION_FALLBACK_IGNORE_REJECTED;
        observation.outcome = WORR_REWIND_OBSERVATION_OUTCOME_SCENE_REJECTED;
      }
      return finishObservation(NativeTrace(
          start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
          ignoredEntityCount));
    }
    worr_rewind_trace_view_v1 view{};
    if (!Worr_RewindTraceViewV1(scene, &ignoreSet, &view)) {
      ++diagnostics.scenesRejected;
      if (observe) {
        observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
        observation.fallback_reason =
            WORR_REWIND_OBSERVATION_FALLBACK_TRACE_VIEW_REJECTED;
        observation.outcome = WORR_REWIND_OBSERVATION_OUTCOME_SCENE_REJECTED;
      }
      return finishObservation(NativeTrace(
          start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
          ignoredEntityCount));
    }

    for (uint32_t i = 0; i < view.candidate_count && !result.allSolid; ++i) {
      const worr_rewind_scene_candidate_v1 &candidate = view.candidates[i];
      bool ignored = false;
      if (!Worr_RewindIgnoreSetContainsV1(&ignoreSet, candidate.pose.entity,
                                          &ignored) ||
          ignored) {
        continue;
      }
      if (candidate.pose.collision_shape ==
          WORR_REWIND_COLLISION_BRUSH_MODEL) {
        if (!ClipHistoricalBrushPose(result, candidate.pose,
                                     historicalBrushes, start, mins, maxs,
                                     end, contentMask)) {
          ++diagnostics.scenesRejected;
          ++diagnostics.historicalBrushRejected;
          if (observe) {
            observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
            observation.fallback_reason =
                WORR_REWIND_OBSERVATION_FALLBACK_SCENE_REJECTED;
            observation.outcome =
                WORR_REWIND_OBSERVATION_OUTCOME_SCENE_REJECTED;
          }
          return finishObservation(NativeTrace(
              start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
              ignoredEntityCount));
        }
        continue;
      }
      ClipHistoricalPose(result, candidate.pose, candidate.query_reason,
                         bounds, start, mins, maxs, end, passEntity,
                         contentMask, ignoredEntities, ignoredEntityCount);
    }
  } else {
    const std::size_t clientCount = std::min<std::size_t>(
        static_cast<std::size_t>(game.maxClients),
        poseTracks.size());
    for (std::size_t index = 0; index < clientCount && !result.allSolid;
         ++index) {
      gentity_t *current = &g_entities[index + 1u];
      worr_event_entity_ref_v1 identity{};
      if (!EligibleLivePlayer(current))
        continue;
      if (!EntityRef(current, identity)) {
        ++diagnostics.missingHistory;
        if (observe) {
          observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK;
          observation.fallback_reason =
              WORR_REWIND_OBSERVATION_FALLBACK_HISTORY_MISS;
          observation.outcome =
              WORR_REWIND_OBSERVATION_OUTCOME_HISTORY_MISS;
        }
        return finishObservation(NativeTrace(
            start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
            ignoredEntityCount));
      }

      worr_rewind_pose_query_v1 query{};
      query.struct_size = sizeof(query);
      query.schema_version = WORR_REWIND_ABI_VERSION;
      query.entity = identity;
      query.map_epoch = target.mapEpoch;
      query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
      query.target_time_us = target.timeUs;
      worr_rewind_pose_result_v1 historical{};
      if (!Worr_RewindHistoryQueryV1(&poseTracks[index].history, &query,
                                     &historical) ||
          !historical.found) {
        ++diagnostics.missingHistory;
        if (observe) {
          observation.flags |= WORR_REWIND_OBSERVATION_CURRENT_FALLBACK |
                               WORR_REWIND_OBSERVATION_HISTORICAL_QUERY;
          observation.fallback_reason =
              WORR_REWIND_OBSERVATION_FALLBACK_HISTORY_MISS;
          observation.outcome =
              WORR_REWIND_OBSERVATION_OUTCOME_HISTORY_MISS;
          observation.query_reason = historical.reason;
        }
        return finishObservation(NativeTrace(
            start, mins, maxs, end, passEntity, contentMask, ignoredEntities,
            ignoredEntityCount));
      }
      if (observe) {
        observation.flags |= WORR_REWIND_OBSERVATION_HISTORICAL_QUERY;
        observation.query_reason = historical.reason;
        ++observation.candidate_count;
      }
      ClipHistoricalPose(result, historical.pose, historical.reason, bounds,
                         start, mins, maxs, end, passEntity, contentMask,
                         ignoredEntities, ignoredEntityCount);
    }
  }

  if (target.canonical && result.ent && result.fraction < 1.0f &&
      HistoricalBrushExcludesCurrent(&historicalBrushes, result.ent)) {
    historicalBrushHit = true;
  }

  MaybeReportDiagnostics();
  return finishObservation(result);
}

[[nodiscard]] bool CanonicalRailProbeTerminal() {
  return canonicalRailProbe.stage == CanonicalRailProbeStage::Passed ||
         canonicalRailProbe.stage == CanonicalRailProbeStage::Failed;
}

[[nodiscard]] bool CanonicalRailProbeActive() {
  return canonicalRailProbe.stage != CanonicalRailProbeStage::Idle &&
         !CanonicalRailProbeTerminal();
}

void CanonicalRailProbeReleaseCurrentWorldSplashImpact() {
  gentity_t *impact = canonicalRailProbe.current_world_splash_impact;
  canonicalRailProbe.current_world_splash_impact = nullptr;
  canonicalRailProbe.current_world_splash_impact_identity = {};
  if (impact && impact->inUse)
    FreeEntity(impact);
}

void CanonicalRailProbeReleaseCurrentWorldSplashDamageTarget() {
  gentity_t *target = canonicalRailProbe.current_world_splash_damage_target;
  canonicalRailProbe.current_world_splash_damage_target = nullptr;
  canonicalRailProbe.current_world_splash_damage_target_identity = {};
  if (target && target->inUse)
    FreeEntity(target);
}

void CanonicalRailProbeReleaseProxLandingSurface() {
  gentity_t *surface = canonicalRailProbe.prox_landing_surface;
  canonicalRailProbe.prox_landing_surface = nullptr;
  canonicalRailProbe.prox_landing_surface_identity = {};
  if (surface && surface->inUse)
    FreeEntity(surface);
}

void CanonicalRailProbeRestoreHistoricalMover() {
  gentity_t *mover = canonicalRailProbe.historical_mover;
  if (canonicalRailProbe.historical_mover_relocated && mover &&
      CanonicalRailProbeSameEntity(
          mover, canonicalRailProbe.historical_mover_identity)) {
    mover->s.origin = canonicalRailProbe.historical_mover_restore_origin;
    if (canonicalRailProbe.historical_mover_restore_linked)
      gi.linkEntity(mover);
    else
      gi.unlinkEntity(mover);
  }
  canonicalRailProbe.historical_mover = nullptr;
  canonicalRailProbe.historical_mover_identity = {};
}

void CanonicalRailProbePublish() {
  if (!sg_worr_rewind_canonical_rail_damage_status ||
      !sg_worr_rewind_canonical_rail_mover_occlusion_status ||
      !sg_worr_rewind_canonical_machinegun_damage_status ||
      !sg_worr_rewind_canonical_chaingun_damage_status ||
      !sg_worr_rewind_canonical_super_shotgun_damage_status ||
      !sg_worr_rewind_canonical_disruptor_damage_status ||
      !sg_worr_rewind_canonical_rocket_damage_status ||
      !sg_worr_rewind_canonical_bfg_damage_status ||
      !sg_worr_rewind_canonical_ion_ripper_damage_status ||
      !sg_worr_rewind_canonical_tesla_mine_damage_status ||
      !sg_worr_rewind_canonical_trap_damage_status ||
      !sg_worr_rewind_canonical_grapple_damage_status ||
      !sg_worr_rewind_canonical_offhand_hook_status ||
      !sg_worr_rewind_canonical_proball_throw_status ||
      !sg_worr_rewind_canonical_grenade_launcher_damage_status ||
      !sg_worr_rewind_canonical_hand_grenade_damage_status ||
      !sg_worr_rewind_canonical_prox_launcher_damage_status ||
      !sg_worr_rewind_canonical_rocket_splash_damage_status ||
      !sg_worr_rewind_canonical_phalanx_splash_damage_status ||
      !sg_worr_rewind_canonical_plasma_gun_damage_status ||
      !sg_worr_rewind_canonical_plasma_gun_splash_damage_status ||
      !sg_worr_rewind_canonical_blaster_damage_status ||
      !sg_worr_rewind_canonical_hyperblaster_damage_status ||
      !sg_worr_rewind_canonical_chainfist_damage_status ||
      !sg_worr_rewind_canonical_etf_rifle_damage_status ||
      !sg_worr_rewind_canonical_phalanx_damage_status ||
      !sg_worr_rewind_canonical_plasma_beam_damage_status ||
      !sg_worr_rewind_canonical_plasma_beam_held_damage_status ||
      !sg_worr_rewind_canonical_plasma_beam_sustained_damage_status ||
      !sg_worr_rewind_canonical_plasma_beam_release_damage_status ||
      !sg_worr_rewind_canonical_plasma_beam_water_retrace_damage_status ||
      !sg_worr_rewind_canonical_thunderbolt_damage_status ||
      !sg_worr_rewind_canonical_thunderbolt_held_damage_status ||
      !sg_worr_rewind_canonical_thunderbolt_sustained_damage_status ||
      !sg_worr_rewind_canonical_thunderbolt_release_damage_status ||
      !sg_worr_rewind_canonical_thunderbolt_water_retrace_damage_status ||
      !sg_worr_rewind_canonical_thunderbolt_discharge_damage_status ||
      !sg_worr_rewind_canonical_shotgun_damage_status)
    return;
  const char *status = "idle";
  if (canonicalRailProbe.stage == CanonicalRailProbeStage::Passed)
    status = "pass";
  else if (canonicalRailProbe.stage == CanonicalRailProbeStage::Failed)
    status = "fail";
  else if (CanonicalRailProbeActive())
    status = "pending";
  const uint32_t armed = canonicalRailProbe.stage != CanonicalRailProbeStage::Idle;
  const uint32_t damageApplied = CanonicalRailProbeDamageApplied();
  char value[640]{};
  std::snprintf(
      value, sizeof(value),
      "%s:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%llu:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%llu:%llu:%llu:%llu:%llu:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%llu:%llu:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u", status, armed,
      canonicalRailProbe.players_ready ? 1u : 0u,
      canonicalRailProbe.history_ready ? 1u : 0u,
      canonicalRailProbe.canonical_scope ? 1u : 0u,
      canonicalRailProbe.attack_received ? 1u : 0u,
      canonicalRailProbe.weapon_callback ? 1u : 0u,
      canonicalRailProbe.canonical_historical_hit ? 1u : 0u, damageApplied,
      canonicalRailProbe.current_geometry_unchanged ? 1u : 0u,
      canonicalRailProbe.target_history_captures,
      static_cast<unsigned long long>(canonicalRailProbe.applied_age_us),
      canonicalRailProbe.failure, canonicalRailProbe.eligible_candidates,
      canonicalRailProbe.playing_candidates, canonicalRailProbe.observation_path,
      canonicalRailProbe.observation_outcome,
      canonicalRailProbe.observation_fallback,
      canonicalRailProbe.observation_flags,
      canonicalRailProbe.observation_query,
      canonicalRailProbe.observation_snapshot_epoch,
      canonicalRailProbe.history_epoch,
      canonicalRailProbe.target_history_count,
      static_cast<unsigned long long>(canonicalRailProbe.observation_applied_time_us),
      static_cast<unsigned long long>(canonicalRailProbe.latest_capture_time_us),
      static_cast<unsigned long long>(canonicalRailProbe.trace_current_time_us),
      static_cast<unsigned long long>(canonicalRailProbe.context_snapshot_time_us),
      static_cast<unsigned long long>(canonicalRailProbe.context_mapped_time_us),
      canonicalRailProbe.target_capture_prepares,
      canonicalRailProbe.capture_append_rejections,
      canonicalRailProbe.target_capture_callbacks,
      canonicalRailProbe.weapon_policy,
      canonicalRailProbe.expected_damage,
      canonicalRailProbe.weapon_damage,
      canonicalRailProbe.water_retrace_required ? 1u : 0u,
      canonicalRailProbe.water_retrace_observed ? 1u : 0u,
      canonicalRailProbe.thunderbolt_discharge_required ? 1u : 0u,
      canonicalRailProbe.thunderbolt_discharge_ammo_drained ? 1u : 0u,
      canonicalRailProbe.thunderbolt_discharge_observed ? 1u : 0u,
      canonicalRailProbe.sustained_hold_required ? 1u : 0u,
      canonicalRailProbe.sustained_hold_interrupted ? 1u : 0u,
      canonicalRailProbe.projectile_forward_required ? 1u : 0u,
      canonicalRailProbe.projectile_forward_authenticated ? 1u : 0u,
      canonicalRailProbe.projectile_forward_advanced ? 1u : 0u,
      canonicalRailProbe.projectile_forward_clamped ? 1u : 0u,
      canonicalRailProbe.projectile_forward_blocked ? 1u : 0u,
      static_cast<unsigned long long>(
          canonicalRailProbe.projectile_forward_age_us),
      static_cast<unsigned long long>(
          canonicalRailProbe.projectile_forward_advanced_age_us),
      canonicalRailProbe.projectile_forward_launches,
      canonicalRailProbe.projectile_forward_expected_launches,
      canonicalRailProbe.melee_selection_required ? 1u : 0u,
      canonicalRailProbe.melee_selection_authenticated ? 1u : 0u,
      canonicalRailProbe.melee_historical_eligible ? 1u : 0u,
      canonicalRailProbe.melee_current_displacement_accepted ? 1u : 0u,
      canonicalRailProbe.melee_current_displacement_units,
      canonicalRailProbe.prox_lifecycle_required ? 1u : 0u,
      canonicalRailProbe.prox_mine_landed ? 1u : 0u,
      canonicalRailProbe.prox_mine_triggered ? 1u : 0u,
      canonicalRailProbe.prox_mine_exploded ? 1u : 0u,
      canonicalRailProbe.historical_mover_occlusion_required ? 1u : 0u,
      canonicalRailProbe.historical_mover_relocated ? 1u : 0u,
      canonicalRailProbe.historical_mover_baseline_clear ? 1u : 0u,
      canonicalRailProbe.historical_mover_occlusion_observed ? 1u : 0u,
      canonicalRailProbe.historical_mover_target_undamaged ? 1u : 0u,
      canonicalRailProbe.historical_mover_history_count);
  gi.cvarForceSet("sg_worr_rewind_canonical_rail_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_rail_mover_occlusion_status",
                  value);
  gi.cvarForceSet("sg_worr_rewind_canonical_machinegun_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_chaingun_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_super_shotgun_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_disruptor_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_rocket_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_bfg_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_ion_ripper_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_tesla_mine_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_trap_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_grapple_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_offhand_hook_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_proball_throw_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_grenade_launcher_damage_status",
                  value);
  gi.cvarForceSet("sg_worr_rewind_canonical_hand_grenade_damage_status",
                  value);
  gi.cvarForceSet("sg_worr_rewind_canonical_prox_launcher_damage_status",
                  value);
  gi.cvarForceSet("sg_worr_rewind_canonical_rocket_splash_damage_status",
                  value);
  gi.cvarForceSet("sg_worr_rewind_canonical_phalanx_splash_damage_status",
                  value);
  gi.cvarForceSet("sg_worr_rewind_canonical_plasma_gun_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_plasma_gun_splash_damage_status",
                  value);
  gi.cvarForceSet("sg_worr_rewind_canonical_blaster_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_hyperblaster_damage_status",
                  value);
  gi.cvarForceSet("sg_worr_rewind_canonical_chainfist_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_etf_rifle_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_phalanx_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_plasma_beam_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_plasma_beam_held_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_plasma_beam_sustained_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_plasma_beam_release_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_plasma_beam_water_retrace_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_thunderbolt_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_thunderbolt_held_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_thunderbolt_sustained_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_thunderbolt_release_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_thunderbolt_water_retrace_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_thunderbolt_discharge_damage_status", value);
  gi.cvarForceSet("sg_worr_rewind_canonical_shotgun_damage_status", value);
}

void CanonicalRailProbeFail(uint32_t failure) {
  if (canonicalRailProbe.stage == CanonicalRailProbeStage::Passed ||
      canonicalRailProbe.stage == CanonicalRailProbeStage::Failed) {
    return;
  }
  canonicalRailProbe.failure = failure;
  canonicalRailProbe.stage = CanonicalRailProbeStage::Failed;
  CanonicalRailProbeRestoreHistoricalMover();
  CanonicalRailProbeReleaseCurrentWorldSplashImpact();
  CanonicalRailProbeReleaseCurrentWorldSplashDamageTarget();
  CanonicalRailProbeReleaseProxLandingSurface();
  CanonicalRailProbePublish();
}

[[nodiscard]] bool CanonicalRailProbeSameEntity(
    const gentity_t *entity, worr_event_entity_ref_v1 expected) {
  worr_event_entity_ref_v1 actual{};
  return entity && expected.index != 0 && EntityRef(entity, actual) &&
         actual.index == expected.index && actual.generation == expected.generation;
}

[[nodiscard]] bool CanonicalRailProbeSameReference(
    worr_event_entity_ref_v1 actual, worr_event_entity_ref_v1 expected) {
  return expected.index != 0 && actual.index == expected.index &&
         actual.generation == expected.generation;
}

void CanonicalRailProbePlacePlayer(gentity_t *entity, const Vector3 &origin) {
  if (!entity || !entity->client)
    return;
  entity->s.origin = origin;
  entity->s.modelIndex = MODELINDEX_PLAYER;
  entity->solid = SOLID_BBOX;
  entity->takeDamage = true;
  entity->deadFlag = false;
  entity->health = kCanonicalRailProbeTargetHealth;
  entity->maxHealth = kCanonicalRailProbeTargetHealth;
  // The headless clients may be admitted to the match one frame before their
  // normal spawn bookkeeping is observed. The fixture owns this temporary
  // player state and needs ClientEndServerFrame to perform the ordinary pose
  // capture; it still waits for a real received attack command before firing.
  entity->client->pers.spawned = true;
  // A just-admitted player can retain a spawn health-bonus decay timer. The
  // fixture owns its synthetic 1000-health target state, so retain neither the
  // bonus nor its later per-frame health decrement; otherwise a multi-round
  // weapon can appear to deal one extra point after a correct callback.
  entity->client->pers.healthBonus = 0;
  entity->velocity = {};
  entity->groundEntity = nullptr;
  entity->gravity = 0.0f;
  entity->client->ps.pmove.origin = origin;
  entity->client->ps.pmove.velocity = {};
  entity->client->old_pmove.origin = origin;
  entity->client->old_pmove.velocity = {};
  gi.linkEntity(entity);
}

// Keep a fixture target at its selected live pose after real weapon damage.
// Unlike CanonicalRailProbePlacePlayer this deliberately preserves health and
// every combat outcome: it only clears movement introduced by normal damage
// knockback, so the next ordinary repeating-beam tick queries the same target
// rather than a target displaced out of the bounded acceptance ray.
void CanonicalRailProbePinTarget(gentity_t *entity, const Vector3 &origin) {
  if (!entity || !entity->client)
    return;
  entity->s.origin = origin;
  entity->velocity = {};
  entity->client->ps.pmove.origin = origin;
  entity->client->ps.pmove.velocity = {};
  entity->client->old_pmove.origin = origin;
  entity->client->old_pmove.velocity = {};
  gi.linkEntity(entity);
}

[[nodiscard]] bool CanonicalRailProbePlaceCurrentWorldSplashImpact(
    const Vector3 *originOverride = nullptr) {
  if (!canonicalRailProbe.current_world_splash_required)
    return true;
  if (canonicalRailProbe.current_world_splash_impact) {
    return CanonicalRailProbeSameEntity(
        canonicalRailProbe.current_world_splash_impact,
        canonicalRailProbe.current_world_splash_impact_identity);
  }

  // This is present-world fixture geometry, deliberately created only after
  // history capture and immediately before the real attack. The player target
  // is off-axis on the shooter's left, so the normal projectile must collide
  // with this blocker and let the production RadiusDamage path decide splash.
  gentity_t *impact = Spawn();
  impact->className = "worr_canonical_current_world_splash_impact";
  impact->s.origin = originOverride
                         ? *originOverride
                         : canonicalRailProbe.historical_target_origin +
                               Vector3{-16.0f, 0.0f, 0.0f};
  const float halfExtent =
      canonicalRailProbe.current_world_splash_impact_half_extent;
  impact->mins = {-halfExtent, -halfExtent, -64.0f};
  impact->maxs = {halfExtent, halfExtent, 64.0f};
  impact->solid = SOLID_BBOX;
  impact->clipMask = MASK_PROJECTILE;
  impact->moveType = MoveType::None;
  impact->takeDamage =
      canonicalRailProbe.current_world_splash_impact_damageable;
  if (impact->takeDamage)
    impact->health = 100000;
  impact->svFlags = SVF_NONE;
  gi.linkEntity(impact);

  worr_event_entity_ref_v1 identity{};
  if (!EntityRef(impact, identity)) {
    FreeEntity(impact);
    return false;
  }
  canonicalRailProbe.current_world_splash_impact = impact;
  canonicalRailProbe.current_world_splash_impact_identity = identity;
  return true;
}

[[nodiscard]] bool CanonicalRailProbePlaceProxLandingSurface(
    gentity_t *mine) {
  if (!canonicalRailProbe.prox_lifecycle_required)
    return true;
  if (canonicalRailProbe.prox_landing_surface) {
    return CanonicalRailProbeSameEntity(
        canonicalRailProbe.prox_landing_surface,
        canonicalRailProbe.prox_landing_surface_identity);
  }
  if (!mine || !CanonicalRailProbeSameEntity(
                   mine, canonicalRailProbe.prox_mine_identity)) {
    return false;
  }

  // The packaged collision map deliberately has an open firing lane. Stage a
  // broad, non-damageable Push surface beneath the mine only after its
  // accepted clear ballistic advance has finished. The next normal physics
  // frame supplies the actual contact; prox_land still decides whether it can
  // attach and creates the production trigger.
  gentity_t *surface = Spawn();
  surface->className = "worr_canonical_prox_landing_surface";
  surface->s.origin = mine->s.origin + Vector3{0.0f, 0.0f, -128.0f};
  surface->mins = {-1024.0f, -1024.0f, -16.0f};
  surface->maxs = {1024.0f, 1024.0f, 16.0f};
  surface->solid = SOLID_BBOX;
  surface->clipMask = MASK_PROJECTILE;
  surface->moveType = MoveType::Push;
  surface->takeDamage = false;
  surface->svFlags = SVF_NONE;
  gi.linkEntity(surface);

  worr_event_entity_ref_v1 identity{};
  if (!EntityRef(surface, identity)) {
    FreeEntity(surface);
    return false;
  }
  canonicalRailProbe.prox_landing_surface = surface;
  canonicalRailProbe.prox_landing_surface_identity = identity;
  return true;
}

[[nodiscard]] bool CanonicalRailProbeSelectHistoricalMover() {
  if (!canonicalRailProbe.historical_mover_occlusion_required)
    return true;
  if (canonicalRailProbe.historical_mover) {
    return CanonicalRailProbeSameEntity(
               canonicalRailProbe.historical_mover,
               canonicalRailProbe.historical_mover_identity) &&
           EligibleLiveMover(canonicalRailProbe.historical_mover);
  }

  worr_sgame_rewind_collision::Map map{};
  if (!CurrentCollisionMap(map))
    return false;
  const std::size_t total = std::min<std::size_t>(
      static_cast<std::size_t>(MAX_ENTITIES),
      static_cast<std::size_t>(globals.numEntities));
  const std::size_t firstMover = std::min<std::size_t>(
      total, static_cast<std::size_t>(game.maxClients) + 1u);
  for (std::size_t index = firstMover; index < total; ++index) {
    gentity_t *candidate = &g_entities[index];
    worr_event_entity_ref_v1 identity{};
    worr_sgame_rewind_collision::Asset asset{};
    if (!EligibleLiveMover(candidate) || !EntityRef(candidate, identity) ||
        !ResolveMoverAsset(*candidate, map, asset) ||
        asset.local_maxs[0] - asset.local_mins[0] < 16.0f ||
        asset.local_maxs[1] - asset.local_mins[1] < 16.0f) {
      continue;
    }
    canonicalRailProbe.historical_mover = candidate;
    canonicalRailProbe.historical_mover_identity = identity;
    return true;
  }
  return false;
}

[[nodiscard]] bool CanonicalRailProbeSelectPlayers() {
  if (canonicalRailProbe.shooter || canonicalRailProbe.target) {
    if (!CanonicalRailProbeSameEntity(canonicalRailProbe.shooter,
                                      canonicalRailProbe.shooter_identity) ||
        !CanonicalRailProbeSameEntity(canonicalRailProbe.target,
                                      canonicalRailProbe.target_identity)) {
      CanonicalRailProbeFail(4);
      return false;
    }
    if (canonicalRailProbe.water_retrace_required &&
        !CanonicalRailProbeSameEntity(canonicalRailProbe.water,
                                      canonicalRailProbe.water_identity)) {
      CanonicalRailProbeFail(16);
      return false;
    }
    if (canonicalRailProbe.current_world_splash_required &&
        canonicalRailProbe.current_world_splash_impact &&
        !CanonicalRailProbeSameEntity(
            canonicalRailProbe.current_world_splash_impact,
            canonicalRailProbe.current_world_splash_impact_identity)) {
      CanonicalRailProbeFail(21);
      return false;
    }
    if (canonicalRailProbe.prox_lifecycle_required &&
        canonicalRailProbe.prox_landing_surface &&
        !CanonicalRailProbeSameEntity(
            canonicalRailProbe.prox_landing_surface,
            canonicalRailProbe.prox_landing_surface_identity)) {
      CanonicalRailProbeFail(24);
      return false;
    }
    if (!CanonicalRailProbeSelectHistoricalMover()) {
      CanonicalRailProbeFail(26);
      return false;
    }
    return true;
  }

  gentity_t *shooter = nullptr;
  gentity_t *target = nullptr;
  canonicalRailProbe.eligible_candidates = 0;
  canonicalRailProbe.playing_candidates = 0;
  const std::size_t clientCount = std::min<std::size_t>(
      static_cast<std::size_t>(game.maxClients), poseTracks.size());
  for (std::size_t index = 0; index < clientCount; ++index) {
    gentity_t *candidate = &g_entities[index + 1u];
    if (!candidate || !candidate->client || !candidate->inUse ||
        !ClientIsPlaying(candidate->client)) {
      continue;
    }
    ++canonicalRailProbe.playing_candidates;
    if (EligibleLivePlayer(candidate))
      ++canonicalRailProbe.eligible_candidates;
    // The acceptance gate supplies two independent real UDP clients. The
    // first supplies the canonical attack command; the second retains the
    // target history. Do not use a game bot here: it does not exercise the
    // connection and command lifecycle required by this proof.
    if (!shooter) {
      shooter = candidate;
    } else if (!target) {
      target = candidate;
    }
  }
  if (!shooter || !target || shooter == target) {
    CanonicalRailProbePublish();
    return false;
  }

  worr_event_entity_ref_v1 shooterIdentity{};
  worr_event_entity_ref_v1 targetIdentity{};
  if (!EntityRef(shooter, shooterIdentity) || !EntityRef(target, targetIdentity)) {
    CanonicalRailProbeFail(5);
    return false;
  }

  canonicalRailProbe.shooter = shooter;
  canonicalRailProbe.target = target;
  canonicalRailProbe.shooter_identity = shooterIdentity;
  canonicalRailProbe.target_identity = targetIdentity;
  if (canonicalRailProbe.water_retrace_required) {
    // The packaged collision fixture owns a real func_water brush centered at
    // the world origin. Select it by entity identity so later observations
    // prove actual water contact followed by a normal production retrace.
    const std::size_t total = std::min<std::size_t>(
        static_cast<std::size_t>(MAX_ENTITIES),
        static_cast<std::size_t>(globals.numEntities));
    for (std::size_t index = static_cast<std::size_t>(game.maxClients) + 1u;
         index < total; ++index) {
      gentity_t *candidate = &g_entities[index];
      if (!candidate->inUse || candidate->solid != SOLID_BSP ||
          !candidate->className ||
          Q_strcasecmp(candidate->className, "func_water") != 0) {
        continue;
      }
      worr_event_entity_ref_v1 waterIdentity{};
      if (EntityRef(candidate, waterIdentity)) {
        canonicalRailProbe.water = candidate;
        canonicalRailProbe.water_identity = waterIdentity;
        break;
      }
    }
    if (!canonicalRailProbe.water) {
      CanonicalRailProbeFail(16);
      return false;
    }
  }
  if (!CanonicalRailProbeSelectHistoricalMover()) {
    CanonicalRailProbeFail(26);
    return false;
  }
  // Keep the target inside P_ProjectSource's close-target branch for the
  // bullet family as well as Railgun. The ordinary weapon callback still
  // computes and owns the final hitscan trace; this merely prevents the
  // fixture's off-axis muzzle offset from turning the initial projection probe
  // into a different line than the following bullet trace.
  canonicalRailProbe.shooter_origin =
      canonicalRailProbe.historical_target_origin -
      Vector3{canonicalRailProbe.target_distance, 0.0f, 0.0f};
  canonicalRailProbe.current_target_origin =
      canonicalRailProbe.historical_target_origin +
      canonicalRailProbe.current_target_offset;
  canonicalRailProbe.players_ready = true;
  canonicalRailProbe.stage = CanonicalRailProbeStage::CapturingHistory;
  CanonicalRailProbePublish();
  return true;
}

void CanonicalRailProbePrepareFrameCapture(gentity_t *entity) {
  if (!CanonicalRailProbeActive())
    return;
  if (!CanonicalRailProbeSelectPlayers())
    return;
  if ((canonicalRailProbe.stage == CanonicalRailProbeStage::CapturingHistory ||
       canonicalRailProbe.stage ==
           CanonicalRailProbeStage::WaitingForCanonicalAttack) &&
      entity == canonicalRailProbe.target) {
    ++canonicalRailProbe.target_capture_prepares;
    canonicalRailProbe.capture_append_rejections =
        static_cast<uint32_t>(std::min<uint64_t>(
            diagnostics.appendRejected, std::numeric_limits<uint32_t>::max()));
    // Every pre-fire pose is an ordinary end-frame capture at the same
    // historical location.  The actual rail trace later sees the target only
    // at current_target_origin in the live world.
    CanonicalRailProbePlacePlayer(
        canonicalRailProbe.target,
        canonicalRailProbe.historical_target_origin);
    canonicalRailProbe.target->health = kCanonicalRailProbeTargetHealth;
    canonicalRailProbe.target->client->PowerupTimer(
        PowerupTimer::SpawnProtection) = 0_ms;
    CanonicalRailProbePublish();
  } else if (canonicalRailProbe.stage == CanonicalRailProbeStage::AwaitingDamage &&
             canonicalRailProbe.projectile_current_authority_required &&
             entity == canonicalRailProbe.target) {
    // Headless clients can submit a predicted zero-input origin between the
    // initial attack and a Generic weapon's later fire frame. Keep only the
    // fixture target's server-staged current pose; health, collision, normal
    // projectile flight, contact, splash, and damage remain untouched.
    CanonicalRailProbePinTarget(entity,
                                canonicalRailProbe.current_target_origin);
  }
}

void CanonicalRailProbeCaptureFrame(gentity_t *entity) {
  if (!CanonicalRailProbeActive())
    return;
  if (entity == canonicalRailProbe.target)
    ++canonicalRailProbe.target_capture_callbacks;
  if (canonicalRailProbe.stage == CanonicalRailProbeStage::CapturingHistory &&
      entity == canonicalRailProbe.target) {
    ++canonicalRailProbe.target_history_captures;
    if (canonicalRailProbe.target_history_captures >=
        kCanonicalRailProbeRequiredHistoryCaptures) {
      canonicalRailProbe.history_ready = true;
      canonicalRailProbe.stage =
          CanonicalRailProbeStage::WaitingForCanonicalAttack;
    }
    CanonicalRailProbePublish();
    return;
  }

  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      !canonicalRailProbe.weapon_callback) {
    return;
  }
  if (!CanonicalRailProbeSameEntity(canonicalRailProbe.target,
                                    canonicalRailProbe.target_identity)) {
    CanonicalRailProbeFail(11);
    return;
  }
  if (canonicalRailProbe.thunderbolt_discharge_required &&
      !CanonicalRailProbeSameEntity(canonicalRailProbe.shooter,
                                    canonicalRailProbe.shooter_identity)) {
    CanonicalRailProbeFail(11);
    return;
  }
  if (canonicalRailProbe.sustained_hold_required ||
      canonicalRailProbe.current_world_splash_required ||
      (canonicalRailProbe.prox_lifecycle_required &&
       canonicalRailProbe.prox_mine_landed)) {
    CanonicalRailProbePinTarget(canonicalRailProbe.target,
                                canonicalRailProbe.current_target_origin);
  }
  gentity_t *damageTarget = canonicalRailProbe.target;
  if (canonicalRailProbe.current_world_splash_damage_target_after_touch) {
    if (!CanonicalRailProbeSameEntity(
            canonicalRailProbe.current_world_splash_damage_target,
            canonicalRailProbe.current_world_splash_damage_target_identity)) {
      // The production touch has not yet staged its isolated radius target.
      // Preserve the bounded normal projectile lifetime before failing it.
      uint64_t currentTimeUs = 0;
      if (canonicalRailProbe.damage_settle_delay_us &&
          CurrentAuthoritativeTimeUs(currentTimeUs) &&
          currentTimeUs >= canonicalRailProbe.damage_settle_deadline_us) {
        CanonicalRailProbeFail(25);
        return;
      }
      CanonicalRailProbePublish();
      return;
    }
    damageTarget = canonicalRailProbe.current_world_splash_damage_target;
  }
  const int healthBefore = canonicalRailProbe.thunderbolt_discharge_required
                               ? canonicalRailProbe.shooter_health_before
                               : canonicalRailProbe.target_health_before;
  const int healthAfter = canonicalRailProbe.thunderbolt_discharge_required
                              ? canonicalRailProbe.shooter->health
                              : damageTarget->health;
  canonicalRailProbe.weapon_damage =
      healthBefore > healthAfter
          ? static_cast<uint32_t>(healthBefore - healthAfter)
          : 0u;
  canonicalRailProbe.historical_mover_target_undamaged =
      !canonicalRailProbe.historical_mover_occlusion_required ||
      healthAfter == healthBefore;
  const bool damageApplied = CanonicalRailProbeDamageApplied();
  uint64_t currentTimeUs = 0;
  const bool needsAuthoritativeTime =
      canonicalRailProbe.damage_settle_delay_us ||
      (canonicalRailProbe.release_required &&
       canonicalRailProbe.release_received);
  if (needsAuthoritativeTime && !CurrentAuthoritativeTimeUs(currentTimeUs)) {
    CanonicalRailProbeFail(13);
    return;
  }
  bool damageSettled = true;
  if (canonicalRailProbe.damage_settle_delay_us) {
    const bool deadlineReached =
        currentTimeUs >= canonicalRailProbe.damage_settle_deadline_us;
    // Delayed projectiles need their normal daemon/lifecycle window to settle
    // before measuring damage. Held beams instead publish pending progress
    // until the requested normal cadence reaches exact cumulative damage, then
    // finish immediately; the same deadline remains a fail-closed timeout.
    damageSettled = !canonicalRailProbe.damage_required
                        ? true
                        : (canonicalRailProbe.defer_damage_evaluation_until_deadline
                               ? deadlineReached
                               : (damageApplied || deadlineReached));
  }
  if (!damageSettled) {
    CanonicalRailProbePublish();
    return;
  }
  if (canonicalRailProbe.release_required) {
    // The real held cadence must reach its exact damage before a release may
    // complete the fixture. A missing release remains pending only until the
    // same bounded cadence deadline, then fails closed.
    if (!damageApplied) {
      CanonicalRailProbeFail(12);
      return;
    }
    if (!canonicalRailProbe.release_received) {
      if (currentTimeUs >= canonicalRailProbe.damage_settle_deadline_us) {
        CanonicalRailProbeFail(14);
        return;
      }
      CanonicalRailProbePublish();
      return;
    }
    if (currentTimeUs < canonicalRailProbe.release_grace_deadline_us) {
      CanonicalRailProbePublish();
      return;
    }
    canonicalRailProbe.release_damage_stable =
        canonicalRailProbe.weapon_damage == canonicalRailProbe.release_damage_before;
    if (!canonicalRailProbe.release_damage_stable) {
      CanonicalRailProbeFail(15);
      return;
    }
  }
  // The collision journal does not promise an entity reference for every
  // inline water-brush hit. The production weapon result is stronger and
  // unambiguous here: fire_beams/fire_thunderbolt halve damage only after the
  // real water trace, and the displaced live target can receive that exact
  // damage only through the following water-excluding historical retrace.
  if (canonicalRailProbe.water_retrace_required) {
    canonicalRailProbe.water_retrace_observed =
        damageApplied && canonicalRailProbe.canonical_historical_hit &&
        canonicalRailProbe.current_geometry_unchanged;
  }
  if (canonicalRailProbe.thunderbolt_discharge_required) {
    canonicalRailProbe.thunderbolt_discharge_ammo_drained =
        canonicalRailProbe.thunderbolt_discharge_ammo_item != IT_NULL &&
        canonicalRailProbe.shooter && canonicalRailProbe.shooter->client &&
        canonicalRailProbe.shooter->client->pers.inventory[
            canonicalRailProbe.thunderbolt_discharge_ammo_item] == 0;
  }
  const bool projectileForwardProof =
      canonicalRailProbe.projectile_forward_required &&
      canonicalRailProbe.projectile_forward_authenticated &&
      canonicalRailProbe.projectile_forward_advanced &&
      !canonicalRailProbe.projectile_forward_blocked &&
      (canonicalRailProbe.projectile_forward_expected_launches == 0 ||
       canonicalRailProbe.projectile_forward_launches ==
           canonicalRailProbe.projectile_forward_expected_launches) &&
      canonicalRailProbe.projectile_forward_advanced_age_us > 0 &&
      canonicalRailProbe.projectile_forward_advanced_age_us <=
          canonicalRailProbe.projectile_forward_age_us;
  const bool meleeSelectionProof =
      !canonicalRailProbe.melee_selection_required ||
      (canonicalRailProbe.melee_selection_authenticated &&
       canonicalRailProbe.melee_historical_eligible &&
       canonicalRailProbe.melee_current_displacement_accepted &&
       canonicalRailProbe.melee_current_displacement_units <=
           static_cast<uint32_t>(MeleeMaxDisplacementUnits()));
  const bool historicalMoverOcclusionProof =
      !canonicalRailProbe.historical_mover_occlusion_required ||
      (canonicalRailProbe.historical_mover_relocated &&
       canonicalRailProbe.historical_mover_baseline_clear &&
       canonicalRailProbe.historical_mover_occlusion_observed &&
       canonicalRailProbe.historical_mover_target_undamaged &&
       canonicalRailProbe.historical_mover_history_count >=
           kCanonicalRailProbeRequiredHistoryCaptures);
  const bool normalHitscanProof =
      canonicalRailProbe.canonical_scope &&
      canonicalRailProbe.attack_received && canonicalRailProbe.weapon_callback &&
      canonicalRailProbe.canonical_historical_hit &&
      (!canonicalRailProbe.damage_required || damageApplied) &&
      canonicalRailProbe.current_geometry_unchanged &&
      (!canonicalRailProbe.water_retrace_required ||
       canonicalRailProbe.water_retrace_observed) &&
      (!canonicalRailProbe.projectile_forward_required ||
       projectileForwardProof) &&
      meleeSelectionProof &&
      historicalMoverOcclusionProof &&
      (!canonicalRailProbe.release_required ||
       (canonicalRailProbe.release_received &&
        canonicalRailProbe.release_damage_stable));
  // Current-world spawn-forward projectiles intentionally do not use a
  // historical impact, target, or splash query. Their proof instead joins the
  // same authenticated command scope to the current-world spawn sweep and
  // unmodified current-authority direct impact.
  const bool currentAuthorityProjectileProof =
      canonicalRailProbe.players_ready && canonicalRailProbe.history_ready &&
      canonicalRailProbe.canonical_scope &&
      canonicalRailProbe.attack_received &&
      canonicalRailProbe.weapon_callback &&
      (!canonicalRailProbe.damage_required || damageApplied) &&
      !canonicalRailProbe.canonical_historical_hit &&
      canonicalRailProbe.current_geometry_unchanged &&
      projectileForwardProof;
  const bool proxLifecycleProof =
      !canonicalRailProbe.prox_lifecycle_required ||
      (canonicalRailProbe.prox_mine_landed &&
       canonicalRailProbe.prox_mine_triggered &&
       canonicalRailProbe.prox_mine_exploded);
  const bool currentWorldSplashProof =
      currentAuthorityProjectileProof &&
      canonicalRailProbe.current_world_splash_required &&
      canonicalRailProbe.current_world_splash_impact_observed &&
      (!canonicalRailProbe.damage_required || damageApplied);
  // Production discharge intentionally returns before the beam's historical
  // trace. It is a current-authority radius/self-damage effect, so require the
  // explicit production-branch observer rather than fabricating a hitscan
  // observation that the weapon did not make.
  const bool thunderboltDischargeProof =
      canonicalRailProbe.players_ready && canonicalRailProbe.history_ready &&
      canonicalRailProbe.canonical_scope && canonicalRailProbe.attack_received &&
      canonicalRailProbe.thunderbolt_discharge_observed && damageApplied &&
      canonicalRailProbe.thunderbolt_discharge_ammo_drained;
  if ((canonicalRailProbe.thunderbolt_discharge_required
           ? thunderboltDischargeProof
           : (canonicalRailProbe.projectile_current_authority_required
                  ? (canonicalRailProbe.current_world_splash_required
                         ? currentWorldSplashProof
                         : (currentAuthorityProjectileProof &&
                            proxLifecycleProof))
                  : normalHitscanProof))) {
    canonicalRailProbe.stage = CanonicalRailProbeStage::Passed;
    CanonicalRailProbeRestoreHistoricalMover();
    CanonicalRailProbeReleaseCurrentWorldSplashImpact();
    CanonicalRailProbeReleaseCurrentWorldSplashDamageTarget();
    CanonicalRailProbeReleaseProxLandingSurface();
    CanonicalRailProbePublish();
  } else {
    uint32_t failure = 12;
    if (canonicalRailProbe.water_retrace_required &&
        !canonicalRailProbe.water_retrace_observed) {
      failure = 17;
    } else if (canonicalRailProbe.thunderbolt_discharge_required &&
               (!canonicalRailProbe.thunderbolt_discharge_ammo_drained ||
                !canonicalRailProbe.thunderbolt_discharge_observed)) {
      failure = canonicalRailProbe.thunderbolt_discharge_ammo_drained ? 19 : 18;
    } else if (canonicalRailProbe.current_world_splash_required &&
               !canonicalRailProbe.current_world_splash_impact_observed) {
      failure = 22;
    } else if (canonicalRailProbe.prox_lifecycle_required &&
               !proxLifecycleProof) {
      failure = 24;
    } else if (canonicalRailProbe.historical_mover_occlusion_required &&
               !historicalMoverOcclusionProof) {
      failure = 28;
    }
    CanonicalRailProbeFail(failure);
  }
}

void CanonicalRailProbeObserveTrace(
    gentity_t *shooter, const worr_rewind_observation_v1 &observation) {
  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      shooter != canonicalRailProbe.shooter ||
      observation.weapon_policy != canonicalRailProbe.weapon_policy) {
    return;
  }
  canonicalRailProbe.weapon_callback = true;
  const uint32_t requiredFlags =
      WORR_REWIND_OBSERVATION_CANONICAL_CONTEXT |
      WORR_REWIND_OBSERVATION_POLICY_ACCEPTED |
      WORR_REWIND_OBSERVATION_HISTORICAL_QUERY |
      WORR_REWIND_OBSERVATION_HISTORICAL_SCENE |
      WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED |
      WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED;
  const bool historicalTargetHit =
      observation.path == WORR_REWIND_OBSERVATION_PATH_CANONICAL &&
      observation.outcome == WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT &&
      observation.fallback_reason == WORR_REWIND_OBSERVATION_FALLBACK_NONE &&
      (observation.flags & requiredFlags) == requiredFlags &&
      (observation.flags & WORR_REWIND_OBSERVATION_CURRENT_FALLBACK) == 0 &&
      SameCommand(observation.command_id, canonicalRailProbe.command_id) &&
      observation.hit_entity.index == canonicalRailProbe.target_identity.index &&
      observation.hit_entity.generation == canonicalRailProbe.target_identity.generation &&
      observation.trace_fraction > 0.0f && observation.trace_fraction < 1.0f;
  const bool historicalMoverHit =
      canonicalRailProbe.historical_mover_occlusion_required &&
      observation.path == WORR_REWIND_OBSERVATION_PATH_CANONICAL &&
      observation.outcome == WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT &&
      observation.fallback_reason == WORR_REWIND_OBSERVATION_FALLBACK_NONE &&
      (observation.flags & requiredFlags) == requiredFlags &&
      (observation.flags & WORR_REWIND_OBSERVATION_CURRENT_FALLBACK) == 0 &&
      SameCommand(observation.command_id, canonicalRailProbe.command_id) &&
      CanonicalRailProbeSameReference(
          observation.hit_entity,
          canonicalRailProbe.historical_mover_identity) &&
      observation.trace_fraction > 0.0f && observation.trace_fraction < 1.0f;
  const bool historicalWaterHit =
      canonicalRailProbe.water_retrace_required &&
      observation.path == WORR_REWIND_OBSERVATION_PATH_CANONICAL &&
      observation.fallback_reason == WORR_REWIND_OBSERVATION_FALLBACK_NONE &&
      (observation.flags & WORR_REWIND_OBSERVATION_CURRENT_FALLBACK) == 0 &&
      SameCommand(observation.command_id, canonicalRailProbe.command_id) &&
      CanonicalRailProbeSameReference(observation.hit_entity,
                                      canonicalRailProbe.water_identity) &&
      observation.trace_fraction > 0.0f && observation.trace_fraction < 1.0f;
  if (historicalWaterHit)
    canonicalRailProbe.water_trace_observation_sequence =
        observation.observation_sequence;
  const bool currentGeometryUnchanged =
      canonicalRailProbe.target && canonicalRailProbe.target->linked &&
      canonicalRailProbe.target->s.origin ==
          canonicalRailProbe.current_target_origin &&
      (!canonicalRailProbe.historical_mover_occlusion_required ||
       (canonicalRailProbe.historical_mover_relocated &&
        CanonicalRailProbeSameEntity(
            canonicalRailProbe.historical_mover,
            canonicalRailProbe.historical_mover_identity) &&
        canonicalRailProbe.historical_mover->linked &&
        canonicalRailProbe.historical_mover->s.origin ==
            canonicalRailProbe.historical_mover_current_origin));

  // A hitscan weapon can emit auxiliary collision queries before or after the
  // target-bearing damage trace. Retain only the observation that names the
  // target instead of mistaking an unrelated clear segment for a miss.
  if (historicalTargetHit || historicalMoverHit) {
    canonicalRailProbe.observation_path = observation.path;
    canonicalRailProbe.observation_outcome = observation.outcome;
    canonicalRailProbe.observation_fallback = observation.fallback_reason;
    canonicalRailProbe.observation_flags = observation.flags;
    canonicalRailProbe.observation_query = observation.query_reason;
    canonicalRailProbe.observation_snapshot_epoch = observation.snapshot_id.epoch;
    canonicalRailProbe.history_epoch = historyMapEpoch;
    canonicalRailProbe.observation_applied_time_us = observation.applied_time_us;
    canonicalRailProbe.latest_capture_time_us = lastCaptureTimeUs;
    const std::size_t targetIndex = ClientIndex(canonicalRailProbe.target);
    if (targetIndex < poseTracks.size())
      canonicalRailProbe.target_history_count = poseTracks[targetIndex].history.count;
    uint64_t currentTimeUs = 0;
    if (CurrentAuthoritativeTimeUs(currentTimeUs)) {
      canonicalRailProbe.trace_current_time_us = currentTimeUs;
      if (observation.applied_time_us <= currentTimeUs)
        canonicalRailProbe.applied_age_us =
            currentTimeUs - observation.applied_time_us;
    }
    canonicalRailProbe.canonical_historical_hit = true;
    canonicalRailProbe.current_geometry_unchanged = currentGeometryUnchanged;
    canonicalRailProbe.water_retrace_observed =
        !canonicalRailProbe.water_retrace_required ||
        (canonicalRailProbe.water_trace_observation_sequence != 0 &&
         canonicalRailProbe.water_trace_observation_sequence <
             observation.observation_sequence);
  }
  CanonicalRailProbePublish();
}

void CanonicalRailProbeObserveProjectileForward(
    gentity_t *shooter,
    const LagCompensationProjectileForwardResult &result) {
  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      !canonicalRailProbe.projectile_forward_required ||
      shooter != canonicalRailProbe.shooter ||
      result.weapon_policy != canonicalRailProbe.weapon_policy ||
      !CanonicalRailProbeSameEntity(shooter,
                                    canonicalRailProbe.shooter_identity)) {
    return;
  }
  const bool matchingCommand =
      SameCommand(result.command_id, canonicalRailProbe.projectile_command_id);
  if (sg_lag_compensation_debug && sg_lag_compensation_debug->integer >= 3) {
    gi.Com_PrintFmt(
        "lagcomp projectile_forward fixture policy={} result_command={}:{} "
        "expected_command={}:{} match={} authenticated={} advanced={} "
        "age_us={} advanced_us={}\n",
        result.weapon_policy, result.command_id.epoch, result.command_id.sequence,
        canonicalRailProbe.projectile_command_id.epoch,
        canonicalRailProbe.projectile_command_id.sequence,
        matchingCommand ? 1u : 0u, result.authenticated ? 1u : 0u,
        result.advanced ? 1u : 0u,
        static_cast<unsigned long long>(result.authoritative_age_us),
        static_cast<unsigned long long>(result.advanced_age_us));
  }
  if (!matchingCommand)
    return;
  // Item::weaponThink may revisit an idle weapon frame after the one real
  // projectile spawn. Keep the first authenticated spawn result; a later
  // no-op call has no authority to erase that production evidence. Ion Ripper
  // is the deliberate exception: its one callback creates fifteen real bolts,
  // and its fixture requires all fifteen bounded current-world sweeps.
  if (!result.authenticated && !result.advanced)
    return;
  if (canonicalRailProbe.projectile_forward_expected_launches != 0) {
    ++canonicalRailProbe.projectile_forward_launches;
    canonicalRailProbe.projectile_forward_authenticated =
        canonicalRailProbe.projectile_forward_authenticated || result.authenticated;
    canonicalRailProbe.projectile_forward_advanced =
        canonicalRailProbe.projectile_forward_advanced || result.advanced;
    canonicalRailProbe.projectile_forward_clamped =
        canonicalRailProbe.projectile_forward_clamped || result.clamped;
    canonicalRailProbe.projectile_forward_blocked =
        canonicalRailProbe.projectile_forward_blocked || result.blocked;
    canonicalRailProbe.projectile_forward_age_us = std::max(
        canonicalRailProbe.projectile_forward_age_us,
        result.authoritative_age_us);
    canonicalRailProbe.projectile_forward_advanced_age_us = std::max(
        canonicalRailProbe.projectile_forward_advanced_age_us,
        result.advanced_age_us);
  } else {
    canonicalRailProbe.projectile_forward_authenticated = result.authenticated;
    canonicalRailProbe.projectile_forward_advanced = result.advanced;
    canonicalRailProbe.projectile_forward_clamped = result.clamped;
    canonicalRailProbe.projectile_forward_blocked = result.blocked;
    canonicalRailProbe.projectile_forward_age_us =
        result.authoritative_age_us;
    canonicalRailProbe.projectile_forward_advanced_age_us =
        result.advanced_age_us;
  }
  if (canonicalRailProbe.current_world_splash_required) {
    if (result.projectile_entity.index == 0) {
      CanonicalRailProbeFail(23);
      return;
    }
    canonicalRailProbe.current_world_splash_projectile_identity =
        result.projectile_entity;
    if (canonicalRailProbe.current_world_splash_after_forward) {
      const std::size_t projectileIndex =
          static_cast<std::size_t>(result.projectile_entity.index);
      if (projectileIndex >= static_cast<std::size_t>(globals.numEntities)) {
        CanonicalRailProbeFail(23);
        return;
      }
      gentity_t *projectile = &g_entities[projectileIndex];
      Vector3 direction = projectile->velocity;
      if (!CanonicalRailProbeSameEntity(projectile, result.projectile_entity) ||
          direction.normalize() <= 0.0f) {
        CanonicalRailProbeFail(23);
        return;
      }
      const Vector3 impactOrigin = projectile->s.origin + direction * 96.0f;
      const Vector3 lateral{-direction[1], direction[0], 0.0f};
      canonicalRailProbe.current_target_origin =
          impactOrigin + lateral * 128.0f + Vector3{0.0f, 0.0f, 32.0f};
      if (!CanonicalRailProbePlaceCurrentWorldSplashImpact(&impactOrigin)) {
        CanonicalRailProbeFail(23);
        return;
      }
      CanonicalRailProbePinTarget(canonicalRailProbe.target,
                                  canonicalRailProbe.current_target_origin);
      canonicalRailProbe.target->health = kCanonicalRailProbeTargetHealth;
      canonicalRailProbe.target->client->PowerupTimer(
          PowerupTimer::SpawnProtection) = 0_ms;
    }
  }
  if (canonicalRailProbe.prox_lifecycle_required) {
    if (result.projectile_entity.index == 0) {
      CanonicalRailProbeFail(24);
      return;
    }
    canonicalRailProbe.prox_mine_identity = result.projectile_entity;
    const std::size_t mineIndex =
        static_cast<std::size_t>(result.projectile_entity.index);
    if (mineIndex >= static_cast<std::size_t>(globals.numEntities) ||
        !CanonicalRailProbePlaceProxLandingSurface(&g_entities[mineIndex])) {
      CanonicalRailProbeFail(24);
      return;
    }
  }
  canonicalRailProbe.weapon_callback = true;
  canonicalRailProbe.current_geometry_unchanged =
      canonicalRailProbe.target && canonicalRailProbe.target->linked &&
      canonicalRailProbe.target->s.origin ==
          canonicalRailProbe.current_target_origin;
  CanonicalRailProbePublish();
}

void CanonicalRailProbeObserveMeleeSelection(
    gentity_t *shooter, const LagCompensationMeleeSelectionResult &result) {
  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      !canonicalRailProbe.melee_selection_required ||
      shooter != canonicalRailProbe.shooter ||
      result.weapon_policy != canonicalRailProbe.weapon_policy ||
      !SameCommand(result.command_id, canonicalRailProbe.command_id) ||
      !CanonicalRailProbeSameEntity(shooter,
                                    canonicalRailProbe.shooter_identity)) {
    return;
  }

  canonicalRailProbe.weapon_callback = true;
  canonicalRailProbe.melee_selection_authenticated = result.authenticated;
  canonicalRailProbe.melee_historical_eligible =
      result.historical_eligible &&
      CanonicalRailProbeSameReference(result.target_entity,
                                      canonicalRailProbe.target_identity);
  canonicalRailProbe.melee_current_displacement_accepted =
      result.current_displacement_accepted;
  canonicalRailProbe.melee_current_displacement_units =
      result.current_displacement_units;
  canonicalRailProbe.canonical_historical_hit =
      canonicalRailProbe.melee_historical_eligible;
  canonicalRailProbe.observation_applied_time_us = result.applied_time_us;
  canonicalRailProbe.trace_current_time_us = result.current_time_us;
  if (result.applied_time_us <= result.current_time_us) {
    canonicalRailProbe.applied_age_us =
        result.current_time_us - result.applied_time_us;
  }
  canonicalRailProbe.current_geometry_unchanged =
      canonicalRailProbe.target && canonicalRailProbe.target->linked &&
      canonicalRailProbe.target->s.origin ==
          canonicalRailProbe.current_target_origin;
  CanonicalRailProbePublish();
}

[[nodiscard]] bool CanonicalRailProbeRelocateHistoricalMover() {
  if (!canonicalRailProbe.historical_mover_occlusion_required)
    return true;
  if (canonicalRailProbe.historical_mover_relocated) {
    return CanonicalRailProbeSameEntity(
               canonicalRailProbe.historical_mover,
               canonicalRailProbe.historical_mover_identity) &&
           canonicalRailProbe.historical_mover->linked &&
           canonicalRailProbe.historical_mover->s.origin ==
               canonicalRailProbe.historical_mover_current_origin;
  }
  gentity_t *mover = canonicalRailProbe.historical_mover;
  if (!CanonicalRailProbeSameEntity(
          mover, canonicalRailProbe.historical_mover_identity) ||
      !EligibleLiveMover(mover)) {
    return false;
  }
  MoverTrack *track =
      FindMoverTrack(canonicalRailProbe.historical_mover_identity);
  if (!track || !track->initialized ||
      !Worr_RewindHistoryValidateV1(&track->history) ||
      track->history.count < kCanonicalRailProbeRequiredHistoryCaptures) {
    return false;
  }

  canonicalRailProbe.historical_mover_history_count = track->history.count;
  canonicalRailProbe.historical_mover_restore_origin = mover->s.origin;
  canonicalRailProbe.historical_mover_restore_linked = mover->linked;
  canonicalRailProbe.historical_mover_current_origin =
      mover->s.origin + Vector3{0.0f, 96.0f, 0.0f};
  mover->s.origin = canonicalRailProbe.historical_mover_current_origin;
  gi.linkEntity(mover);
  canonicalRailProbe.historical_mover_relocated =
      mover->linked &&
      mover->s.origin == canonicalRailProbe.historical_mover_current_origin;
  return canonicalRailProbe.historical_mover_relocated;
}

void CanonicalRailProbePrepareCommand(gentity_t *entity, usercmd_t *command) {
  if (!CanonicalRailProbeActive() || !entity || !entity->client || !command)
    return;
  if (!CanonicalRailProbeSelectPlayers())
    return;
  if (canonicalRailProbe.stage == CanonicalRailProbeStage::AwaitingDamage &&
      canonicalRailProbe.projectile_current_authority_required &&
      entity == canonicalRailProbe.target) {
    CanonicalRailProbePinTarget(entity,
                                canonicalRailProbe.current_target_origin);
    return;
  }
  if (entity != canonicalRailProbe.shooter) {
    return;
  }
  if (canonicalRailProbe.stage !=
          CanonicalRailProbeStage::WaitingForCanonicalAttack &&
      canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage) {
    return;
  }
  if (!commandContextImport || !commandContextImport->GetScopeState ||
      !commandContextImport->GetCurrent) {
    CanonicalRailProbeFail(2);
    return;
  }
  const uint32_t scopeState = commandContextImport->GetScopeState();
  if (scopeState == WORR_COMMAND_CONTEXT_SCOPE_INACTIVE_LEGACY)
    return;
  if (scopeState != WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID) {
    CanonicalRailProbeFail(6);
    return;
  }
  worr_authoritative_command_context_v1 context{};
  if (!commandContextImport->GetCurrent(&context) ||
      context.struct_size != sizeof(context) ||
      context.schema_version != WORR_COMMAND_CONTEXT_API_VERSION ||
      context.client_index != ClientIndex(entity) ||
      !Worr_CommandIdValidV1(context.command.command_id, false)) {
    CanonicalRailProbeFail(7);
    return;
  }
  canonicalRailProbe.canonical_scope = true;
  canonicalRailProbe.context_snapshot_time_us =
      context.current_snapshot.server_time_us;
  canonicalRailProbe.context_mapped_time_us =
      context.mapping_proof.mapped_server_time_us;
  if (canonicalRailProbe.stage == CanonicalRailProbeStage::AwaitingDamage) {
    // Weapon_Generic advances Phalanx from its initial fire frame to the
    // actual barrel callback on a later held command. Generic projectile
    // weapons use that same held-command correlation. A release-only held
    // throw is different: its callback is caused by the real no-attack
    // release command, which must replace the initial prime command here.
    // This is fixture correlation only; no user-command bit or production
    // weapon state is changed.
    const bool releaseOnlyPolicy =
        ReleaseOnlyProjectileForwardPolicy(canonicalRailProbe.weapon_policy);
    const bool actionPressed =
        (command->buttons &
         (canonicalRailProbe.offhand_hook_input_required ? BUTTON_HOOK
                                                          : BUTTON_ATTACK)) != 0;
    const bool releaseOnlyCommand =
        releaseOnlyPolicy &&
        (command->buttons & BUTTON_ATTACK) == 0 &&
        (entity->client->buttons & BUTTON_ATTACK) != 0;
    if (canonicalRailProbe.projectile_current_authority_required &&
        ((releaseOnlyPolicy && releaseOnlyCommand) ||
         (!releaseOnlyPolicy && !canonicalRailProbe.offhand_hook_input_required &&
          actionPressed))) {
      canonicalRailProbe.projectile_command_id = context.command.command_id;
    }
    if (releaseOnlyPolicy && releaseOnlyCommand &&
        canonicalRailProbe.current_world_splash_after_forward &&
        canonicalRailProbe.damage_settle_delay_us) {
      uint64_t currentTimeUs = 0;
      if (!CurrentAuthoritativeTimeUs(currentTimeUs)) {
        CanonicalRailProbeFail(13);
        return;
      }
      canonicalRailProbe.damage_settle_deadline_us =
          currentTimeUs + canonicalRailProbe.damage_settle_delay_us;
    }
    if (canonicalRailProbe.sustained_hold_required &&
        (command->buttons & BUTTON_ATTACK) == 0) {
      canonicalRailProbe.sustained_hold_interrupted = true;
      CanonicalRailProbeFail(20);
      return;
    }
    // The release is meaningful only after normal held firing has reached the
    // exact requested cumulative damage. It remains a real no-attack command
    // under the same production command-context admission path.
    if (canonicalRailProbe.release_required && !canonicalRailProbe.release_received &&
        (command->buttons & BUTTON_ATTACK) == 0 &&
        CanonicalRailProbeDamageApplied()) {
      uint64_t currentTimeUs = 0;
      if (!CurrentAuthoritativeTimeUs(currentTimeUs)) {
        CanonicalRailProbeFail(13);
        return;
      }
      canonicalRailProbe.release_received = true;
      canonicalRailProbe.release_damage_before = canonicalRailProbe.weapon_damage;
      canonicalRailProbe.release_grace_deadline_us =
          currentTimeUs + kCanonicalBeamReleaseGraceUs;
    }
    CanonicalRailProbePublish();
    return;
  }
  const bool actionPressed =
      (command->buttons &
       (canonicalRailProbe.offhand_hook_input_required ? BUTTON_HOOK
                                                        : BUTTON_ATTACK)) != 0;
  if (!actionPressed) {
    CanonicalRailProbePublish();
    return;
  }

  Item *weapon = GetItemByIndex(canonicalRailProbe.weapon_item);
  if (!weapon || !weapon->weaponThink || weapon->id == IT_NULL ||
      canonicalRailProbe.weapon_policy == WORR_REWIND_WEAPON_UNSPECIFIED ||
      canonicalRailProbe.expected_damage == 0) {
    CanonicalRailProbeFail(8);
    return;
  }

  // This alters only the isolated fixture's server-owned player state. The
  // received action bit remains intact: normal fixtures reach Item::weaponThink
  // later, while the off-hand fixture reaches its production +hook action
  // later in ClientThink. This function never calls a weapon or trace routine.
  CanonicalRailProbePlacePlayer(entity, canonicalRailProbe.shooter_origin);
  CanonicalRailProbePlacePlayer(canonicalRailProbe.target,
                                canonicalRailProbe.current_target_origin);
  if (!CanonicalRailProbeRelocateHistoricalMover()) {
    CanonicalRailProbeFail(27);
    return;
  }
  if (!canonicalRailProbe.current_world_splash_after_forward &&
      !CanonicalRailProbePlaceCurrentWorldSplashImpact()) {
    CanonicalRailProbeFail(21);
    return;
  }
  canonicalRailProbe.target->health = kCanonicalRailProbeTargetHealth;
  canonicalRailProbe.target->client->PowerupTimer(
      PowerupTimer::SpawnProtection) = 0_ms;
  entity->client->spawnAngleLockAngles = {};
  entity->client->spawnAngleLockUntil =
      level.time + (canonicalRailProbe.sustained_hold_required
                        ? kCanonicalBeamSustainedProbeAngleLockDuration
                        : 1_sec);
  entity->client->pers.weapon = weapon;
  entity->client->pers.inventory[weapon->id] = 1;
  if (canonicalRailProbe.weapon_policy ==
      WORR_REWIND_WEAPON_PROBALL_HELD_THROW_BALLISTIC_SPAWN_FORWARD) {
    // The fixture supplies possession only, after both real clients are
    // admitted and before the real attack. It never fabricates a pickup,
    // touch, pass, launch, goal, score, or command authority.
    entity->client->pers.inventory[IT_BALL] = 1;
  }
  if (weapon->ammo != IT_NULL)
    entity->client->pers.inventory[weapon->ammo] =
        canonicalRailProbe.initial_ammo;
  if (canonicalRailProbe.thunderbolt_discharge_required) {
    entity->client->pers.ammoMax[static_cast<int>(AmmoID::Cells)] =
        static_cast<short>(std::max<int>(
            entity->client->pers.ammoMax[static_cast<int>(AmmoID::Cells)],
            kCanonicalThunderboltDischargeProbeCells));
  }
  entity->client->weapon.pending = nullptr;
  entity->client->weapon.fireFinished = level.time;
  entity->client->weapon.thinkTime = level.time;
  entity->client->weapon.fireBuffered = false;
  entity->client->weapon.thunk = false;
  entity->client->weaponState = WeaponState::Ready;
  entity->client->ps.gunFrame = canonicalRailProbe.weapon_idle_frame;
  // The fixture reacts only after observing its genuine received action bit.
  // Reset an older held-button edge so the ordinary ClientThink assignment
  // immediately below latches this same command; no button is synthesized.
  entity->client->buttons = BUTTON_NONE;
  entity->client->latchedButtons &=
      ~(canonicalRailProbe.offhand_hook_input_required ? BUTTON_HOOK
                                                        : BUTTON_ATTACK);

  canonicalRailProbe.command_id = context.command.command_id;
  canonicalRailProbe.projectile_command_id = context.command.command_id;
  canonicalRailProbe.target_health_before = canonicalRailProbe.target->health;
  if (canonicalRailProbe.thunderbolt_discharge_required) {
    canonicalRailProbe.shooter_health_before = entity->health;
    canonicalRailProbe.thunderbolt_discharge_ammo_item = weapon->ammo;
    canonicalRailProbe.thunderbolt_discharge_observed = false;
    canonicalRailProbe.thunderbolt_discharge_ammo_drained = false;
  }
  if (canonicalRailProbe.damage_settle_delay_us) {
    uint64_t currentTimeUs = 0;
    if (!CurrentAuthoritativeTimeUs(currentTimeUs)) {
      CanonicalRailProbeFail(13);
      return;
    }
    canonicalRailProbe.damage_settle_deadline_us =
        currentTimeUs + canonicalRailProbe.damage_settle_delay_us;
  }
  canonicalRailProbe.attack_received = true;
  canonicalRailProbe.stage = CanonicalRailProbeStage::AwaitingDamage;
  CanonicalRailProbePublish();
}

bool CanonicalHitscanProbeArm(uint32_t weaponPolicy, item_id_t weaponItem,
                              uint32_t expectedDamage, int weaponIdleFrame,
                              float targetDistance, uint64_t damageSettleDelayUs,
                              bool deferDamageEvaluationUntilDeadline = false,
                              Vector3 currentTargetOffset = {0.0f, 96.0f,
                                                             0.0f},
                              bool releaseRequired = false,
                              Vector3 historicalTargetOrigin = {64.0f, 192.0f,
                                                                64.0f},
                              bool waterRetraceRequired = false,
                              bool thunderboltDischargeRequired = false,
                              int initialAmmo = 8,
                              bool sustainedHoldRequired = false,
                              bool currentWorldSplashRequired = false,
                              uint32_t minimumDamage = 0,
                              bool currentWorldSplashImpactDamageable = false,
                              bool damageRequired = true,
                              bool proxLifecycleRequired = false,
                              bool currentWorldSplashAfterForward = false,
                              bool offhandHookInputRequired = false,
                              float currentWorldSplashImpactHalfExtent =
                                  16.0f,
                              bool currentWorldSplashClearImpactAfterTouch =
                                  false,
                              bool currentWorldSplashDamageTargetAfterTouch =
                                  false) {
  CanonicalRailProbeRestoreHistoricalMover();
  CanonicalRailProbeReleaseCurrentWorldSplashImpact();
  CanonicalRailProbeReleaseCurrentWorldSplashDamageTarget();
  CanonicalRailProbeReleaseProxLandingSurface();
  if (!initialized || !deathmatch || !deathmatch->integer ||
      !g_lagCompensation || !g_lagCompensation->integer ||
      !ObservationEnabled() || !commandContextImport ||
      !commandContextImport->GetCurrent || !commandContextImport->GetScopeState) {
    canonicalRailProbe = {};
    canonicalRailProbe.stage = CanonicalRailProbeStage::Failed;
    canonicalRailProbe.failure = 1;
    CanonicalRailProbePublish();
    return false;
  }
  canonicalRailProbe = {};
  canonicalRailProbe.weapon_policy = weaponPolicy;
  canonicalRailProbe.weapon_item = weaponItem;
  canonicalRailProbe.expected_damage = expectedDamage;
  canonicalRailProbe.minimum_damage =
      minimumDamage ? minimumDamage : expectedDamage;
  canonicalRailProbe.initial_ammo = initialAmmo;
  canonicalRailProbe.weapon_idle_frame = weaponIdleFrame;
  canonicalRailProbe.target_distance = targetDistance;
  canonicalRailProbe.damage_settle_delay_us = damageSettleDelayUs;
  canonicalRailProbe.defer_damage_evaluation_until_deadline =
      deferDamageEvaluationUntilDeadline;
  canonicalRailProbe.current_target_offset = currentTargetOffset;
  canonicalRailProbe.release_required = releaseRequired;
  canonicalRailProbe.historical_target_origin = historicalTargetOrigin;
  canonicalRailProbe.water_retrace_required = waterRetraceRequired;
  canonicalRailProbe.thunderbolt_discharge_required =
      thunderboltDischargeRequired;
  canonicalRailProbe.projectile_forward_required =
      weaponPolicy == WORR_REWIND_WEAPON_DISRUPTOR_CONVERGENCE ||
      weaponPolicy == WORR_REWIND_WEAPON_ROCKET_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_PLASMA_GUN_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_BLASTER_BOLT_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_ETF_FLECHETTE_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_GRENADE_LAUNCHER_BALLISTIC_SPAWN_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_HAND_GRENADE_RELEASE_BALLISTIC_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_BFG_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_ION_RIPPER_BURST_SPAWN_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_TESLA_MINE_RELEASE_BALLISTIC_DEPLOY_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_TRAP_RELEASE_BALLISTIC_DEPLOY_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_GRAPPLE_HOOK_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_OFFHAND_HOOK_SPAWN_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_PROBALL_HELD_THROW_BALLISTIC_SPAWN_FORWARD;
  canonicalRailProbe.projectile_forward_expected_launches =
      CanonicalProjectileForwardExpectedLaunches(weaponPolicy);
  canonicalRailProbe.projectile_current_authority_required =
      weaponPolicy == WORR_REWIND_WEAPON_ROCKET_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_PLASMA_GUN_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_BLASTER_BOLT_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_ETF_FLECHETTE_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_GRENADE_LAUNCHER_BALLISTIC_SPAWN_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_HAND_GRENADE_RELEASE_BALLISTIC_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_BFG_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_ION_RIPPER_BURST_SPAWN_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_TESLA_MINE_RELEASE_BALLISTIC_DEPLOY_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_TRAP_RELEASE_BALLISTIC_DEPLOY_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_GRAPPLE_HOOK_SPAWN_FORWARD ||
      weaponPolicy == WORR_REWIND_WEAPON_OFFHAND_HOOK_SPAWN_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_PROBALL_HELD_THROW_BALLISTIC_SPAWN_FORWARD;
  canonicalRailProbe.melee_selection_required =
      weaponPolicy == WORR_REWIND_WEAPON_CHAINFIST_MELEE_HYBRID;
  canonicalRailProbe.current_world_splash_required =
      currentWorldSplashRequired;
  canonicalRailProbe.current_world_splash_impact_damageable =
      currentWorldSplashImpactDamageable;
  canonicalRailProbe.current_world_splash_after_forward =
      currentWorldSplashAfterForward;
  canonicalRailProbe.current_world_splash_impact_half_extent =
      std::max(currentWorldSplashImpactHalfExtent, 1.0f);
  canonicalRailProbe.current_world_splash_clear_impact_after_touch =
      currentWorldSplashClearImpactAfterTouch;
  canonicalRailProbe.current_world_splash_damage_target_after_touch =
      currentWorldSplashDamageTargetAfterTouch;
  canonicalRailProbe.damage_required = damageRequired;
  canonicalRailProbe.sustained_hold_required = sustainedHoldRequired;
  canonicalRailProbe.prox_lifecycle_required = proxLifecycleRequired;
  canonicalRailProbe.offhand_hook_input_required = offhandHookInputRequired;
  canonicalRailProbe.stage = CanonicalRailProbeStage::WaitingForPlayers;
  // The runner has already admitted the two real clients. Select and separate
  // them synchronously with the arming command so an ordinary spawn-frame
  // telefrag cannot replace a captured entity identity before the first
  // retained pose. This remains fixture setup only: no command authority,
  // trace, or weapon action is created here.
  if (CanonicalRailProbeSelectPlayers()) {
    CanonicalRailProbePlacePlayer(canonicalRailProbe.shooter,
                                  canonicalRailProbe.shooter_origin);
    CanonicalRailProbePlacePlayer(canonicalRailProbe.target,
                                  canonicalRailProbe.historical_target_origin);
  }
  CanonicalRailProbePublish();
  return canonicalRailProbe.stage != CanonicalRailProbeStage::Failed;
}

bool CanonicalRailProbeArm() {
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_RAILGUN,
                                  IT_WEAPON_RAILGUN,
                                  kCanonicalRailProbeExpectedDamage, 19, 112.0f,
                                  0);
}

bool CanonicalRailMoverOcclusionProbeArm() {
  if (!CanonicalRailProbeArm())
    return false;

  canonicalRailProbe.historical_mover_occlusion_required = true;
  canonicalRailProbe.damage_required = false;
  canonicalRailProbe.historical_target_origin = {64.0f, 0.0f, -14.0f};
  canonicalRailProbe.current_target_offset = {};
  canonicalRailProbe.shooter_origin =
      canonicalRailProbe.historical_target_origin -
      Vector3{canonicalRailProbe.target_distance, 0.0f, 0.0f};
  canonicalRailProbe.current_target_origin =
      canonicalRailProbe.historical_target_origin;
  if (!CanonicalRailProbeSelectHistoricalMover()) {
    CanonicalRailProbeFail(26);
    return false;
  }
  CanonicalRailProbePlacePlayer(canonicalRailProbe.shooter,
                                canonicalRailProbe.shooter_origin);
  CanonicalRailProbePlacePlayer(canonicalRailProbe.target,
                                canonicalRailProbe.historical_target_origin);
  CanonicalRailProbePublish();
  return canonicalRailProbe.stage != CanonicalRailProbeStage::Failed;
}

bool CanonicalMachinegunProbeArm() {
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_MACHINEGUN,
                                  IT_WEAPON_MACHINEGUN,
                                  kCanonicalMachinegunProbeExpectedDamage, 6,
                                  112.0f, 0);
}

bool CanonicalChaingunProbeArm() {
  // Frame 14 advances through the normal firing callback to frame 15, where
  // the Chaingun's ordinary three-round burst is selected from the received
  // attack button. The status requires all three current-authority bullet
  // hits, rather than accepting a single-round callback observation.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_CHAINGUN,
                                  IT_WEAPON_CHAINGUN,
                                  kCanonicalChaingunProbeExpectedDamage, 14,
                                  112.0f, 0);
}

bool CanonicalSuperShotgunProbeArm() {
  // 64 units keeps both ordinary +/-5 degree barrels and their full pellet
  // patterns inside the historic player hull. This is still a normal close
  // range player shot; only the fixture's server-owned starting geometry is
  // selected here, not a trace or weapon result.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_SUPER_SHOTGUN,
                                  IT_WEAPON_SSHOTGUN,
                                  kCanonicalSuperShotgunProbeExpectedDamage,
                                  18, 64.0f, 0);
}

bool CanonicalDisruptorProbeArm() {
  // The convergence trace is canonical and immediate, but fire_disruptor
  // deliberately resolves physical projectile contact and its 500 ms pain
  // daemon later. Give normal flight and lifecycle processing 1500 ms before
  // measuring the final current-authority damage.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_DISRUPTOR_CONVERGENCE,
                                  IT_WEAPON_DISRUPTOR,
                                  kCanonicalDisruptorProbeExpectedDamage, 10,
                                  112.0f, UINT64_C(1500000), true);
}

bool CanonicalRocketProbeArm() {
  // Retain history before the real command, then move the current target
  // farther down the same aim ray. The rocket has no historical impact query:
  // the normal current-world hull flight must advance, impact, and damage the
  // live target after its bounded authenticated spawn advance.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_ROCKET_SPAWN_FORWARD,
                                  IT_WEAPON_RLAUNCHER,
                                  kCanonicalRocketProbeExpectedDamage, 13,
                                  112.0f, UINT64_C(1500000), false,
                                  {32.0f, 0.0f, 0.0f});
}

bool CanonicalBfgProbeArm() {
  // This proves the initial current-world BFG launch only. Its later laser,
  // touch, staged explosion, and radius lifecycle are deliberately unclaimed.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_BFG_SPAWN_FORWARD, IT_WEAPON_BFG, 200, 17, 112.0f,
      UINT64_C(1500000), false, {32.0f, 0.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false, 50, false, false, 0, false,
      false);
}

bool CanonicalIonRipperProbeArm() {
  // The normal Ion Ripper callback creates fifteen randomized bolts. The
  // live target is moved well outside that narrow spread so this fixture
  // proves every bounded spawn advance, not a direct damage or ricochet
  // outcome.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_ION_RIPPER_BURST_SPAWN_FORWARD,
      IT_WEAPON_IONRIPPER, 10, 6, 112.0f, UINT64_C(1500000), false,
      {0.0f, 256.0f, 0.0f}, false, {64.0f, 192.0f, 64.0f}, false, false,
      10, false, false, 0, false, false);
}

bool CanonicalTeslaMineProbeArm() {
  // Tesla uses the same normal held-release path as a hand grenade, but its
  // deployment contains independent bounce, activation, target scan, and
  // damage authority. This fixture proves only the fresh mine's clear,
  // release-bound initial gravity advance.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_TESLA_MINE_RELEASE_BALLISTIC_DEPLOY_FORWARD,
      IT_AMMO_TESLA, kCanonicalTeslaMineProbeExpectedDamage, 32, 160.0f,
      UINT64_C(2500000), false, {-64.0f, 96.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false, 8, false, false, 0, false,
      false);
}

bool CanonicalTrapProbeArm() {
  // Trap deployment starts from the same real held-release input boundary,
  // but its Bounce/capture/destruction lifecycle remains independently
  // current-world. This fixture proves only the new Trap's fresh clear
  // release-bound gravity advance.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_TRAP_RELEASE_BALLISTIC_DEPLOY_FORWARD, IT_AMMO_TRAP,
      kCanonicalTrapProbeExpectedDamage, 48, 160.0f, UINT64_C(2500000),
      false, {-64.0f, 96.0f, 0.0f}, false, {64.0f, 192.0f, 64.0f}, false,
      false, 8, false, false, 0, false, false);
}

bool CanonicalGrappleProbeArm() {
  // The normal Generic weapon callback creates the hook after its frame-six
  // wind-up. Keep the target off-axis: a passing fixture proves only clear
  // fresh-hook flight, not contact, attachment, pull, or damage.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_GRAPPLE_HOOK_SPAWN_FORWARD, IT_WEAPON_GRAPPLE,
      kCanonicalGrappleProbeExpectedDamage, 31, 112.0f, UINT64_C(1500000),
      false, {0.0f, 256.0f, 0.0f}, false, {64.0f, 192.0f, 64.0f}, false,
      false, 8, false, false, 0, false, false);
}

bool CanonicalOffhandHookProbeArm() {
  // +hook is a one-shot native input action rather than an equipped weapon
  // callback. The fixture selects an otherwise inert normal weapon only to
  // keep generic player-state setup deterministic; BUTTON_HOOK alone invokes
  // the production off-hand action later in ClientThink. The target remains
  // off-axis, proving only fresh clear current-world hook flight—not touch,
  // attachment, pull, damage, or reset.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_OFFHAND_HOOK_SPAWN_FORWARD, IT_WEAPON_BLASTER,
      kCanonicalGrappleProbeExpectedDamage, 9, 112.0f, UINT64_C(1500000),
      false, {0.0f, 256.0f, 0.0f}, false, {64.0f, 192.0f, 64.0f}, false,
      false, 8, false, false, 0, false, false, false, false, true);
}

bool CanonicalProBallThrowProbeArm() {
  // A normal Chainfist-held throw releases only after its real attack-to-
  // release edge. The target remains off-axis: a pass proves a fresh ball's
  // clear current-world gravity advance, not possession, pickup, touch,
  // goals, scores, teams, or resets.
  if (Game::IsNot(GameType::ProBall)) {
    canonicalRailProbe = {};
    CanonicalRailProbeFail(26);
    return false;
  }
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PROBALL_HELD_THROW_BALLISTIC_SPAWN_FORWARD,
      // Chainfist's ordinary first fire callback is frame five. Staging the
      // ready state on its preceding frame lets the real attack enter the
      // unmodified held-throw animation rather than beginning in Chainfist's
      // later idle loop.
      IT_WEAPON_CHAINFIST, kCanonicalProBallThrowProbeExpectedDamage, 4,
      160.0f, UINT64_C(2500000), false, {-64.0f, 96.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false, 8, false, false, 0, false,
      false);
}

bool CanonicalGrenadeLauncherProbeArm() {
  // A Grenade Launcher round has gravity and future bounce ownership, so this
  // This acceptance seam puts a damageable, present-world impact fixture in
  // the normal arc after history capture and moves the real client off-axis.
  // The normal grenade touch must initiate the ordinary explosion; only
  // production RadiusDamage may reach the client. Ballistic advance cannot
  // invent a bounce, placement, trigger, or historical impact.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_GRENADE_LAUNCHER_BALLISTIC_SPAWN_FORWARD,
      IT_WEAPON_GLAUNCHER, kCanonicalGrenadeLauncherProbeExpectedDamage, 5,
      160.0f, UINT64_C(1500000), false, {-64.0f, 96.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false, 8, false, true, 57, true);
}

bool CanonicalHandGrenadeProbeArm() {
  // The hand grenade must be released by a real no-attack command after the
  // ordinary prime/hold animation. Acceptance proves only its first clear
  // bounded current-world ballistic advance; normal Toss collision, bounce,
  // touch, fuse, splash, and damage remain wholly production-owned.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_HAND_GRENADE_RELEASE_BALLISTIC_SPAWN_FORWARD,
      IT_AMMO_GRENADES, kCanonicalHandGrenadeProbeExpectedDamage, 16, 160.0f,
      // The normal prime/hold/release lifecycle precedes flight, unlike a
      // launcher callback. The settling allowance includes that real
      // animation and does not create a throw.
      UINT64_C(2500000), false, {-64.0f, 96.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false, 8, false, false, 0, false,
      false);
}

bool CanonicalHandGrenadeSplashProbeArm() {
  // The live blocker is derived from the accepted released grenade's actual
  // post-forward origin/velocity, then normal Toss physics chooses contact.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_HAND_GRENADE_RELEASE_BALLISTIC_SPAWN_FORWARD,
      IT_AMMO_GRENADES, kCanonicalHandGrenadeProbeExpectedDamage, 16, 160.0f,
      UINT64_C(2500000), false, {-64.0f, 96.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false, 8, false, true, 45, true, true,
      false, true);
}

bool CanonicalProxLauncherProbeArm() {
  // A proximity mine starts as a bouncing projectile but becomes a
  // deployable with independently current-world land/arm/trigger/explosion
  // behavior. This fixture proves only one clear initial gravity path after
  // a real command; it deliberately creates neither a landing nor a trigger.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD,
      IT_WEAPON_PROXLAUNCHER, kCanonicalProxLauncherProbeExpectedDamage, 5,
      160.0f, UINT64_C(1500000), false, {-64.0f, 96.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false, 8, false, false, 0, false,
      false);
}

bool CanonicalProxLauncherLifecycleProbeArm() {
  // The first gravity advance is still the only rewind-owned operation. Once
  // the real mine has landed, this fixture stages its isolated target near
  // that normal landing and proves production arm delay, radial target scan,
  // delayed explosion, and RadiusDamage without creating a touch, target, or
  // damage result itself.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD,
      IT_WEAPON_PROXLAUNCHER,
      kCanonicalProxLauncherLifecycleProbeExpectedDamage, 5, 160.0f,
      UINT64_C(5000000), false, {-64.0f, 96.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false, 8, false, false, 0, false,
      true, true);
}

bool CanonicalRocketSplashProbeArm() {
  // The target moves off the firing ray after history capture. A normal rocket
  // must first strike the current-world blocker, then let RadiusDamage apply
  // its exact reduced splash through the clear side of that blocker.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_ROCKET_SPAWN_FORWARD, IT_WEAPON_RLAUNCHER,
      kCanonicalRocketSplashProbeExpectedDamage, 13, 112.0f,
      UINT64_C(1500000), false, {-64.0f, 48.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false, 8, false, true);
}

bool CanonicalPhalanxSplashProbeArm() {
  // The live target moves off the shell's aim ray after history capture. The
  // normal Phalanx shell must strike the present-world blocker, then let its
  // unmodified RadiusDamage path apply the exact reduced splash amount.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD, IT_WEAPON_PHALANX,
      kCanonicalPhalanxSplashProbeExpectedDamage, 21, 112.0f,
      UINT64_C(1500000), false, {-64.0f, 48.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false, 8, false, true);
}

bool CanonicalPlasmaGunProbeArm() {
  // The target remains on the live aim ray but beyond the 100 ms / 200-unit
  // maximum Plasma Gun advance. This accepts only a real production
  // current-world direct hit after its bounded authenticated spawn advance;
  // radius damage is intentionally outside this direct-hit seam.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PLASMA_GUN_SPAWN_FORWARD, IT_WEAPON_PLASMAGUN,
      kCanonicalPlasmaGunProbeExpectedDamage, 43, 224.0f, UINT64_C(1500000),
      false, {32.0f, 0.0f, 0.0f});
}

bool CanonicalPlasmaGunSplashProbeArm() {
  // The normal Plasma Gun may advance no more than 200 units from the accepted
  // command. Start the real client 256 units behind retained history so that
  // bounded advance cannot reach the fixture blocker. The live player remains
  // on the established direct-hit ray behind that blocker, so its ordinary
  // Plasma Gun callback still fires. Only after normal current-world touch is
  // the small radius target staged from that touch's actual position and
  // flight direction. This gives RadiusDamage a real
  // clear-side target for a deterministic seven-damage production splash
  // without assuming a
  // muzzle, player-center, or trace-coordinate offset. A one-unit impact hull
  // still leaves the player collision hull separate. Once normal touch has
  // accepted the sacrificial fixture blocker, it is unlinked solely so the
  // plasma entity relinked at that impact cannot self-occlude RadiusDamage's
  // production current-world line-of-sight test.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PLASMA_GUN_SPAWN_FORWARD, IT_WEAPON_PLASMAGUN,
      kCanonicalPlasmaGunSplashProbeExpectedDamage, 43, 256.0f,
      UINT64_C(1500000), false, {32.0f, 0.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false, 8, false, true, 0, false, true,
      false, false, false, 1.0f, true, true);
}

bool CanonicalBlasterProbeArm() {
  // The standard Blaster's maximum 100 ms advance is 150 units at speed 1500.
  // Keep the live target beyond that advance and require normal remaining
  // current-world bolt flight for the exact direct hit. The shared path also
  // covers HyperBlaster, whose direct/radius lifecycle remains current-owned.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_BLASTER_BOLT_SPAWN_FORWARD, IT_WEAPON_BLASTER,
      kCanonicalBlasterProbeExpectedDamage, 9, 224.0f, UINT64_C(1500000),
      false, {32.0f, 0.0f, 0.0f});
}

bool CanonicalHyperBlasterProbeArm() {
  // A real held attack enters the repeating 6–11 gun-frame cadence. The
  // shared bolt policy may only consume bounded accepted delay through the
  // current world; this fixture accepts the first ordinary 15-damage bolt,
  // not a historical contact or a Q3 radius branch.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_BLASTER_BOLT_SPAWN_FORWARD, IT_WEAPON_HYPERBLASTER,
      kCanonicalHyperBlasterProbeExpectedDamage, 5, 224.0f,
      UINT64_C(1500000), false, {32.0f, 0.0f, 0.0f});
}

bool CanonicalChainfistProbeArm() {
  // At the authenticated historical time the two player bounds are 48 units
  // apart, leaving a 16-unit box gap inside Chainfist's 24-unit reach. The
  // live target is shifted 64 units off-axis, beyond ordinary present-time
  // melee reach but still exactly at the bounded displacement guard.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_CHAINFIST_MELEE_HYBRID, IT_WEAPON_CHAINFIST,
      kCanonicalChainfistProbeExpectedDamage, 33, 48.0f, 0, false,
      {0.0f, 64.0f, 0.0f});
}

bool CanonicalEtfRifleProbeArm() {
  // ETF's 1150-unit-per-second flechette may consume up to 115 units under
  // the shared 100 ms cap. The muzzle-to-target box gap is 129 units, so a
  // passing result requires normal remaining current-world flight and direct
  // damage after even the full bounded advance.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_ETF_FLECHETTE_SPAWN_FORWARD, IT_WEAPON_ETF_RIFLE,
      kCanonicalEtfRifleProbeExpectedDamage, 8, 128.0f, UINT64_C(1500000),
      false, {32.0f, 0.0f, 0.0f});
}

bool CanonicalPhalanxProbeArm() {
  // The 725-unit-per-second shell can advance at most 72 units under the
  // shared 100 ms cap. Keep the target beyond that advance; normal remaining
  // flight must supply the exact direct hit while RadiusDamage stays available
  // to its unmodified production lifecycle.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PHALANX_SPAWN_FORWARD, IT_WEAPON_PHALANX,
      kCanonicalPhalanxProbeExpectedDamage, 21, 64.0f, UINT64_C(1500000),
      false, {32.0f, 0.0f, 0.0f});
}

bool CanonicalPlasmaBeamProbeArm() {
  // A held real command starts the normal repeating Plasma Beam state. This
  // acceptance seam proves its first ordinary deathmatch beam tick only; it
  // intentionally does not claim a full held/release or water-retrace
  // lifecycle from a one-command fixture.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_PLASMA_BEAM,
                                  IT_WEAPON_PLASMABEAM,
                                  kCanonicalPlasmaBeamProbeExpectedDamage,
                                  13, 112.0f, 0);
}

bool CanonicalPlasmaBeamHeldProbeArm() {
  // The held real command remains active while normal repeating weapon frames
  // accumulate three 8-damage ticks. Keep the live target distinct but on the
  // aim ray so later commands query their newer history rather than correctly
  // missing the one-shot fixture's deliberately off-ray current pose. The
  // bounded window is a timeout, not a mandatory delay: completion occurs as
  // soon as the normal cadence reaches exact 24 damage.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_PLASMA_BEAM,
                                  IT_WEAPON_PLASMABEAM,
                                  kCanonicalPlasmaBeamHeldProbeExpectedDamage,
                                  13, 112.0f, UINT64_C(1500000), false,
                                  {32.0f, 0.0f, 0.0f});
}

bool CanonicalPlasmaBeamSustainedProbeArm() {
  // Hold through 32 normal 8-damage ticks. The bounded five-second deadline
  // rejects a stalled repeater, while no release is synthesized after the
  // initial real attack edge. Plasma Beam receives a one-Cell eligibility
  // reserve because it requires two Cells to begin a tick.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PLASMA_BEAM, IT_WEAPON_PLASMABEAM,
      kCanonicalPlasmaBeamSustainedProbeExpectedDamage, 13, 112.0f,
      kCanonicalBeamSustainedProbeTimeoutUs, false, {32.0f, 0.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false,
      kCanonicalBeamSustainedProbeCells, true);
}

bool CanonicalPlasmaBeamReleaseProbeArm() {
  // The release variant first reaches the normal three-tick cadence, then
  // requires a real no-attack command and a 250 ms no-extra-damage grace.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PLASMA_BEAM, IT_WEAPON_PLASMABEAM,
      kCanonicalPlasmaBeamReleaseProbeExpectedDamage, 13, 112.0f,
      UINT64_C(1500000), false, {32.0f, 0.0f, 0.0f}, true);
}

bool CanonicalPlasmaBeamWaterRetraceProbeArm() {
  // The horizontal beam crosses the packaged fixture's real func_water before reaching
  // the retained historical player. Production fire_beams must observe water,
  // re-trace without it, and apply the normal halved 4 damage.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_PLASMA_BEAM, IT_WEAPON_PLASMABEAM,
      kCanonicalPlasmaBeamWaterRetraceProbeExpectedDamage, 13, 224.0f, 0,
      false, {0.0f, 96.0f, 0.0f}, false, {112.0f, 0.0f, 0.0f}, true);
}

bool CanonicalThunderboltProbeArm() {
  // This is the normal dry-world first tick. The Thunderbolt implementation
  // owns its main/side-ray footprint and target de-duplication; exact 8 damage
  // rejects a fixture result that incorrectly accumulates multiple rays.
  // Underwater discharge and sustained/release lifecycle coverage are separate.
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_THUNDERBOLT,
                                  IT_WEAPON_THUNDERBOLT,
                                  kCanonicalThunderboltProbeExpectedDamage,
                                  3, 112.0f, 0);
}

bool CanonicalThunderboltHeldProbeArm() {
  // fire_thunderbolt owns the repeated main/side-ray footprint and target
  // de-duplication on every ordinary held tick. Keep the distinct live target
  // on the aim ray so later command histories remain hittable. Exact 24 damage
  // proves three normal 8-damage ticks, without inferring underwater discharge
  // behavior.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_THUNDERBOLT, IT_WEAPON_THUNDERBOLT,
      kCanonicalThunderboltHeldProbeExpectedDamage, 3, 112.0f,
      UINT64_C(1500000), false, {32.0f, 0.0f, 0.0f});
}

bool CanonicalThunderboltSustainedProbeArm() {
  // Hold through 32 normal 8-damage ticks. The normal production footprint
  // must retain its per-tick target de-duplication for the complete bounded
  // hold; no release is synthesized after attack start.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_THUNDERBOLT, IT_WEAPON_THUNDERBOLT,
      kCanonicalThunderboltSustainedProbeExpectedDamage, 3, 112.0f,
      kCanonicalBeamSustainedProbeTimeoutUs, false, {32.0f, 0.0f, 0.0f}, false,
      {64.0f, 192.0f, 64.0f}, false, false,
      kCanonicalBeamSustainedProbeCells, true);
}

bool CanonicalThunderboltReleaseProbeArm() {
  // Retain the ordinary dry-world main/side-ray and de-duplication path until
  // the real client releases; any post-release damage fails the grace check.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_THUNDERBOLT, IT_WEAPON_THUNDERBOLT,
      kCanonicalThunderboltReleaseProbeExpectedDamage, 3, 112.0f,
      UINT64_C(1500000), false, {32.0f, 0.0f, 0.0f}, true);
}

bool CanonicalThunderboltWaterRetraceProbeArm() {
  // Keep the shooter out of water while the production main beam crosses the
  // fixture volume. Exact 4 damage verifies normal water halving; a recorded
  // water observation must precede the target retrace and side-ray
  // de-duplication still prevents duplicate target damage.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_THUNDERBOLT, IT_WEAPON_THUNDERBOLT,
      kCanonicalThunderboltWaterRetraceProbeExpectedDamage, 3, 224.0f, 0,
      false, {0.0f, 96.0f, 0.0f}, false, {112.0f, 0.0f, 0.0f}, true);
}

bool CanonicalThunderboltDischargeProbeArm() {
  // This is an explicit current-authority radius policy, not a hitscan rewind
  // claim. The shooter begins underwater with eight cells; the real command
  // must drain them and apply the normal 35 * 8 / 2 discharge hit followed by
  // Damage's self-damage halving (70). The retained target remains outside the
  // radius after command setup, while the command context remains the normal
  // canonical admission path.
  return CanonicalHitscanProbeArm(
      WORR_REWIND_WEAPON_THUNDERBOLT, IT_WEAPON_THUNDERBOLT,
      kCanonicalThunderboltDischargeProbeExpectedDamage, 3, 112.0f, 0, false,
      {0.0f, 400.0f, 0.0f}, false, {112.0f, 0.0f, 0.0f}, false, true);
}

bool CanonicalShotgunProbeArm() {
  return CanonicalHitscanProbeArm(WORR_REWIND_WEAPON_SHOTGUN,
                                  IT_WEAPON_SHOTGUN,
                                  kCanonicalShotgunProbeExpectedDamage, 19,
                                  112.0f, 0);
}

} // namespace

void LagCompensation_Init() {
  sg_lag_compensation_max_ms = gi.cvar(
      "sg_lag_compensation_max_ms", "200", CVAR_NOFLAGS);
  sg_lag_compensation_interp_ms = gi.cvar(
      "sg_lag_compensation_interp_ms", "-1", CVAR_NOFLAGS);
  sg_lag_compensation_debug =
      gi.cvar("sg_lag_compensation_debug", "0", CVAR_NOFLAGS);
  sg_lag_compensation_legacy_error_ms = gi.cvar(
      "sg_lag_compensation_legacy_error_ms", "50", CVAR_NOFLAGS);
  // Projectile simulation remains current-world authority. This cap only
  // compensates the server-side spawn delay of an already accepted command.
  sg_lag_compensation_projectile_forward_ms = gi.cvar(
      "sg_lag_compensation_projectile_forward_ms", "100", CVAR_NOFLAGS);
  sg_lag_compensation_melee_max_displacement = gi.cvar(
      "sg_lag_compensation_melee_max_displacement", "64", CVAR_NOFLAGS);
  sg_worr_rewind_mover_selftest_status = gi.cvar(
      "sg_worr_rewind_mover_selftest_status", "idle", CVAR_NOSET);
  sg_worr_rewind_rail_damage_selftest_status = gi.cvar(
      "sg_worr_rewind_rail_damage_selftest_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_rail_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_rail_damage_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_rail_mover_occlusion_status = gi.cvar(
      "sg_worr_rewind_canonical_rail_mover_occlusion_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_machinegun_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_machinegun_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_chaingun_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_chaingun_damage_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_super_shotgun_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_super_shotgun_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_disruptor_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_disruptor_damage_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_rocket_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_rocket_damage_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_bfg_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_bfg_damage_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_ion_ripper_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_ion_ripper_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_tesla_mine_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_tesla_mine_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_trap_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_trap_damage_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_grapple_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_grapple_damage_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_offhand_hook_status = gi.cvar(
      "sg_worr_rewind_canonical_offhand_hook_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_proball_throw_status = gi.cvar(
      "sg_worr_rewind_canonical_proball_throw_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_grenade_launcher_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_grenade_launcher_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_hand_grenade_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_hand_grenade_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_prox_launcher_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_prox_launcher_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_rocket_splash_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_rocket_splash_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_phalanx_splash_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_phalanx_splash_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_plasma_gun_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_plasma_gun_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_plasma_gun_splash_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_plasma_gun_splash_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_blaster_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_blaster_damage_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_hyperblaster_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_hyperblaster_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_chainfist_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_chainfist_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_etf_rifle_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_etf_rifle_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_phalanx_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_phalanx_damage_status", "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_plasma_beam_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_plasma_beam_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_plasma_beam_held_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_plasma_beam_held_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_plasma_beam_sustained_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_plasma_beam_sustained_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_plasma_beam_release_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_plasma_beam_release_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_plasma_beam_water_retrace_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_plasma_beam_water_retrace_damage_status",
      "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_thunderbolt_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_thunderbolt_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_thunderbolt_held_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_thunderbolt_held_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_thunderbolt_sustained_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_thunderbolt_sustained_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_thunderbolt_release_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_thunderbolt_release_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_thunderbolt_water_retrace_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_thunderbolt_water_retrace_damage_status",
      "idle", CVAR_NOSET);
  sg_worr_rewind_canonical_thunderbolt_discharge_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_thunderbolt_discharge_damage_status", "idle",
      CVAR_NOSET);
  sg_worr_rewind_canonical_shotgun_damage_status = gi.cvar(
      "sg_worr_rewind_canonical_shotgun_damage_status", "idle", CVAR_NOSET);

  commandContextImport = nullptr;
  rewindCollisionImport = nullptr;
  if (gi.GetExtension) {
    const auto *candidate =
        static_cast<const worr_command_context_import_v1 *>(
            gi.GetExtension(WORR_COMMAND_CONTEXT_IMPORT_V1));
    if (candidate && candidate->struct_size == sizeof(*candidate) &&
        candidate->api_version == WORR_COMMAND_CONTEXT_API_VERSION &&
        candidate->GetCurrent && candidate->GetScopeState) {
      commandContextImport = candidate;
    }

    const auto *collisionCandidate =
        static_cast<const worr_sgame_rewind_collision::Import *>(
            gi.GetExtension(worr_sgame_rewind_collision::kImportName));
    if (collisionCandidate &&
        collisionCandidate->struct_size == sizeof(*collisionCandidate) &&
        collisionCandidate->api_version ==
            worr_sgame_rewind_collision::kApiVersion &&
        collisionCandidate->GetMapIdentity &&
        collisionCandidate->ResolveInlineBrush &&
        collisionCandidate->TraceTransformed) {
      rewindCollisionImport = collisionCandidate;
    }
  }

  for (worr_rewind_policy_state_v1 &state : policyStates)
    (void)Worr_RewindPolicyStateInitV1(&state);
  (void)Worr_RewindObservationJournalInitV1(
      &observationJournal, observationStorage.data(),
      static_cast<uint32_t>(observationStorage.size()));
  provisionalMapEpoch = 0;
  diagnostics = {};
  initialized = true;
  LagCompensation_ResetMap();
}

void LagCompensation_Shutdown() {
  commandContextImport = nullptr;
  rewindCollisionImport = nullptr;
  CanonicalRailProbeRestoreHistoricalMover();
  CanonicalRailProbeReleaseCurrentWorldSplashImpact();
  CanonicalRailProbeReleaseCurrentWorldSplashDamageTarget();
  canonicalRailProbe = {};
  // Init()/ResetMap() recreate all bounded state before it can be used again.
  // Do not aggregate-assign those large static arrays here: MSVC lowers that
  // assignment through a stack temporary and can overflow the game thread.
  InvalidateDecisionCaches();
  InvalidateDeferredProjectileForwardAuthorizations();
  InvalidateSceneCaches();
  initialized = false;
}

void LagCompensation_ResetMap() {
  if (!initialized)
    return;
  CanonicalRailProbeRestoreHistoricalMover();
  CanonicalRailProbeReleaseCurrentWorldSplashImpact();
  CanonicalRailProbeReleaseCurrentWorldSplashDamageTarget();
  CanonicalRailProbeReleaseProxLandingSurface();
  canonicalRailProbe = {};
  CanonicalRailProbePublish();
  ++provisionalMapEpoch;
  if (provisionalMapEpoch == 0)
    provisionalMapEpoch = 1;
  authoritativeMapEpoch = 0;
  awaitingAuthoritativeMapEpoch = true;
  clientLifeGenerations = {};
  clientLifeGenerationExhausted = {};
  ResetHistories(provisionalMapEpoch);
  for (worr_rewind_policy_state_v1 &state : policyStates)
    (void)Worr_RewindPolicyStateInitV1(&state);
  (void)Worr_RewindObservationJournalInitV1(
      &observationJournal, observationStorage.data(),
      static_cast<uint32_t>(observationStorage.size()));
}

void LagCompensation_ResetClient(gentity_t *entity) {
  if (!initialized)
    return;
  const std::size_t index = ClientIndex(entity);
  if (index >= poseTracks.size())
    return;
  if (CanonicalRailProbeActive() &&
      (entity == canonicalRailProbe.shooter ||
       entity == canonicalRailProbe.target)) {
    // A participant disconnect/lifecycle reset must not strand fixture-owned
    // mover relocation after its normal end-frame callback disappears.
    CanonicalRailProbeFail(29);
  }
  (void)InitTrack(poseTracks[index], static_cast<uint32_t>(index + 1u));
  (void)Worr_RewindPolicyStateInitV1(&policyStates[index]);
  decisionCaches[index].valid = false;
  deferredProjectileForwardAuthorizations[index] = {};
  sceneCaches[index].valid = false;
}

void LagCompensation_BeginClientLife(gentity_t *entity) {
  if (!initialized)
    return;
  const std::size_t index = ClientIndex(entity);
  if (index >= poseTracks.size())
    return;
  if (CanonicalRailProbeActive() &&
      (entity == canonicalRailProbe.shooter ||
       entity == canonicalRailProbe.target)) {
    CanonicalRailProbeFail(29);
  }

  // Clear the old life first.  Exhaustion is sticky for the rest of the map:
  // generation zero makes pose capture and live-reference resolution fail
  // closed rather than aliasing an earlier life after uint32 wrap.
  (void)InitTrack(poseTracks[index], static_cast<uint32_t>(index + 1u));
  decisionCaches[index].valid = false;
  deferredProjectileForwardAuthorizations[index] = {};
  sceneCaches[index].valid = false;
  if (clientLifeGenerationExhausted[index] ||
      clientLifeGenerations[index] == UINT32_MAX) {
    clientLifeGenerations[index] = 0;
    clientLifeGenerationExhausted[index] = true;
    return;
  }

  // Commit the generation after clearing storage so every subsequent pose,
  // ignore identity, and live-reference check observes the same new life.
  ++clientLifeGenerations[index];
}

void LagCompensation_RecordFrame(gentity_t *entity) {
  const std::size_t index = ClientIndex(entity);
  if (!initialized || index >= poseTracks.size() || !entity ||
      !entity->client || !entity->inUse) {
    return;
  }

  // The headless canonical rail fixture stages its target before the ordinary
  // end-frame pose capture.  Normal operation is an exact no-op here.
  CanonicalRailProbePrepareFrameCapture(entity);

  uint64_t timeUs = 0;
  if (!CompletedAuthoritativeFrameTimeUs(timeUs)) {
    ++diagnostics.appendRejected;
    return;
  }
  if (!PrepareCaptureTime(timeUs))
    return;

  PoseTrack &track = poseTracks[index];
  // Map/client lifecycle callbacks normally initialize every track, but a
  // newly admitted client can reach its first end-frame during a reset handoff.
  // Recover the empty bounded store here instead of silently omitting that
  // authoritative pose (the history is still empty and never aliases a life).
  if (!track.initialized &&
      !InitTrack(track, static_cast<uint32_t>(index + 1u))) {
    ++diagnostics.appendRejected;
    return;
  }
  if (track.history.count) {
    const uint32_t newestIndex = static_cast<uint32_t>(
        (static_cast<uint64_t>(track.history.head) +
         track.history.count - 1u) %
        track.history.capacity);
    const worr_rewind_pose_v1 &newest = track.history.slots[newestIndex];
    if (newest.server_time_us == timeUs && newest.server_tick == captureTick) {
      RecordLegacyFrame(track, gi.ServerFrame(), timeUs);
      return;
    }
  }

  worr_rewind_pose_v1 pose{};
  if (!BuildPose(*entity, timeUs, pose)) {
    ++diagnostics.appendRejected;
    return;
  }
  uint32_t reason = WORR_REWIND_APPEND_REJECT_INVALID;
  if (!Worr_RewindHistoryAppendV1(&track.history, &pose, &reason) ||
      reason != WORR_REWIND_APPEND_ACCEPTED) {
    ++diagnostics.appendRejected;
    if (entity == canonicalRailProbe.target)
      canonicalRailProbe.capture_append_rejections =
          static_cast<uint32_t>(std::min<uint64_t>(
              diagnostics.appendRejected, std::numeric_limits<uint32_t>::max()));
    return;
  }
  RecordLegacyFrame(track, gi.ServerFrame(), timeUs);
  CanonicalRailProbeCaptureFrame(entity);
}

void LagCompensation_RecordMovers() {
  if (!initialized)
    return;

  uint64_t timeUs = 0;
  if (!CompletedAuthoritativeFrameTimeUs(timeUs)) {
    ++diagnostics.moverCaptureRejected;
    return;
  }
  if (!PrepareCaptureTime(timeUs)) {
    ++diagnostics.moverCaptureRejected;
    return;
  }

  worr_sgame_rewind_collision::Map map{};
  if (!CurrentCollisionMap(map) || !AdoptAuthoritativeMapEpoch(map.map_epoch)) {
    ++diagnostics.moverCaptureRejected;
    return;
  }

  const std::size_t total = std::min<std::size_t>(
      static_cast<std::size_t>(MAX_ENTITIES),
      static_cast<std::size_t>(globals.numEntities));
  const std::size_t firstMover = std::min<std::size_t>(
      total, static_cast<std::size_t>(game.maxClients) + 1u);
  for (std::size_t index = firstMover; index < total; ++index) {
    gentity_t *entity = &g_entities[index];
    if (!EligibleLiveMover(entity))
      continue;

    worr_event_entity_ref_v1 identity{};
    if (!EntityRef(entity, identity)) {
      ++diagnostics.moverCaptureRejected;
      continue;
    }
    MoverTrack *track = AcquireMoverTrack(identity);
    if (!track) {
      ++diagnostics.moverTrackExhausted;
      continue;
    }
    if (track->history.count) {
      const uint32_t newestIndex = static_cast<uint32_t>(
          (static_cast<uint64_t>(track->history.head) + track->history.count -
           1u) %
          track->history.capacity);
      const worr_rewind_pose_v1 &newest = track->history.slots[newestIndex];
      if (newest.server_time_us == timeUs &&
          newest.server_tick == captureTick) {
        continue;
      }
    }

    worr_rewind_pose_v1 pose{};
    if (!BuildMoverPose(*entity, timeUs, map, pose)) {
      ++diagnostics.moverCaptureRejected;
      continue;
    }
    uint32_t reason = WORR_REWIND_APPEND_REJECT_INVALID;
    if (!Worr_RewindHistoryAppendV1(&track->history, &pose, &reason) ||
        reason != WORR_REWIND_APPEND_ACCEPTED) {
      ++diagnostics.moverCaptureRejected;
      continue;
    }
    ++diagnostics.moversCaptured;
  }
}

[[nodiscard]] bool RuntimeRiderFrameContinuity(
    const PoseTrack &riderTrack, const MoverTrack &moverTrack,
    worr_event_entity_ref_v1 moverIdentity, uint32_t &sampleCount,
    uint64_t &latestCaptureTimeUs) {
  sampleCount = 0;
  latestCaptureTimeUs = 0;
  if (!riderTrack.initialized || !moverTrack.initialized ||
      !Worr_RewindHistoryValidateV1(&riderTrack.history) ||
      !Worr_RewindHistoryValidateV1(&moverTrack.history)) {
    return false;
  }

  bool haveFirstSample = false;
  bool moverRotated = false;
  bool riderMoved = false;
  float firstMoverAngles[3]{};
  float firstRiderOrigin[3]{};
  for (uint32_t riderOffset = 0; riderOffset < riderTrack.history.count;
       ++riderOffset) {
    const uint32_t riderSlot = static_cast<uint32_t>(
        (static_cast<uint64_t>(riderTrack.history.head) + riderOffset) %
        riderTrack.history.capacity);
    const worr_rewind_pose_v1 &riderPose =
        riderTrack.history.slots[riderSlot];
    if ((riderPose.flags & WORR_REWIND_POSE_HAS_MOVER) == 0 ||
        riderPose.mover.index != moverIdentity.index ||
        riderPose.mover.generation != moverIdentity.generation) {
      continue;
    }

    const worr_rewind_pose_v1 *moverPose = nullptr;
    for (uint32_t moverOffset = 0; moverOffset < moverTrack.history.count;
         ++moverOffset) {
      const uint32_t moverSlot = static_cast<uint32_t>(
          (static_cast<uint64_t>(moverTrack.history.head) + moverOffset) %
          moverTrack.history.capacity);
      const worr_rewind_pose_v1 &candidate =
          moverTrack.history.slots[moverSlot];
      if (candidate.entity.index == moverIdentity.index &&
          candidate.entity.generation == moverIdentity.generation &&
          candidate.collision_shape == WORR_REWIND_COLLISION_BRUSH_MODEL &&
          candidate.server_time_us == riderPose.server_time_us &&
          candidate.server_tick == riderPose.server_tick) {
        moverPose = &candidate;
        break;
      }
    }
    if (!moverPose)
      continue;

    float expectedRelativeOrigin[3]{};
    float expectedRelativeAngles[3]{};
    for (std::size_t component = 0; component < 3; ++component) {
      expectedRelativeOrigin[component] =
          riderPose.origin[component] - moverPose->origin[component];
      expectedRelativeAngles[component] =
          riderPose.angles[component] - moverPose->angles[component];
    }
    if (std::memcmp(riderPose.mover_relative_origin, expectedRelativeOrigin,
                    sizeof(expectedRelativeOrigin)) != 0 ||
        std::memcmp(riderPose.mover_relative_angles, expectedRelativeAngles,
                    sizeof(expectedRelativeAngles)) != 0) {
      return false;
    }

    if (!haveFirstSample) {
      std::memcpy(firstMoverAngles, moverPose->angles,
                  sizeof(firstMoverAngles));
      std::memcpy(firstRiderOrigin, riderPose.origin, sizeof(firstRiderOrigin));
      haveFirstSample = true;
    } else {
      moverRotated |= std::memcmp(firstMoverAngles, moverPose->angles,
                                  sizeof(firstMoverAngles)) != 0;
      riderMoved |= std::memcmp(firstRiderOrigin, riderPose.origin,
                                sizeof(firstRiderOrigin)) != 0;
    }
    ++sampleCount;
    latestCaptureTimeUs = riderPose.server_time_us;
  }
  return sampleCount >= 2 && moverRotated && riderMoved;
}

bool LagCompensation_ArmHistoricalBrushRuntimeProbe() {
  if (!initialized)
    return false;

  worr_sgame_rewind_collision::Map map{};
  if (!CurrentCollisionMap(map))
    return false;
  const std::size_t total = std::min<std::size_t>(
      static_cast<std::size_t>(MAX_ENTITIES),
      static_cast<std::size_t>(globals.numEntities));
  const std::size_t firstMover = std::min<std::size_t>(
      total, static_cast<std::size_t>(game.maxClients) + 1u);
  gentity_t *mover = nullptr;
  worr_sgame_rewind_collision::Asset asset{};
  for (std::size_t index = firstMover; index < total; ++index) {
    gentity_t *candidate = &g_entities[index];
    worr_event_entity_ref_v1 identity{};
    worr_sgame_rewind_collision::Asset candidateAsset{};
    if (!EligibleLiveMover(candidate) || !EntityRef(candidate, identity) ||
        !ResolveMoverAsset(*candidate, map, candidateAsset) ||
        candidateAsset.local_maxs[0] - candidateAsset.local_mins[0] < 16.0f ||
        candidateAsset.local_maxs[1] - candidateAsset.local_mins[1] <=
            candidateAsset.local_maxs[0] - candidateAsset.local_mins[0] +
                4.0f ||
        candidate->aVelocity.length() == 0.0f) {
      continue;
    }
    mover = candidate;
    asset = candidateAsset;
    break;
  }
  if (!mover)
    return false;

  gentity_t *rider = nullptr;
  for (std::size_t index = 1;
       index <= static_cast<std::size_t>(game.maxClients); ++index) {
    gentity_t *candidate = &g_entities[index];
    if (EligibleLivePlayer(candidate)) {
      rider = candidate;
      break;
    }
  }
  if (!rider)
    return false;

  Vector3 forward, right, up;
  AngleVectors(mover->s.angles, forward, right, up);
  const float localXHalf =
      (asset.local_maxs[0] - asset.local_mins[0]) * 0.5f;
  const float riderOffset = std::min(8.0f, localXHalf * 0.5f);
  rider->groundEntity = mover;
  rider->s.origin = mover->s.origin + forward * riderOffset;
  rider->s.origin.z += asset.local_maxs[2] - rider->mins.z;
  rider->s.angles = mover->s.angles;
  rider->s.angles[YAW] += 15.0f;
  rider->velocity = {};
  gi.linkEntity(rider);
  return true;
}

bool LagCompensation_RunHistoricalBrushRuntimeProbe(
    LagCompensationHistoricalBrushRuntimeProbe *probe) {
  if (!probe)
    return false;
  *probe = {};
  struct PublishStatus {
    LagCompensationHistoricalBrushRuntimeProbe *probe;
    bool passed = false;
    uint32_t failure = 0;

    ~PublishStatus() {
      if (!probe || !sg_worr_rewind_mover_selftest_status)
        return;
      char value[128]{};
      const auto scaleFraction = [](float fraction) {
        return static_cast<uint32_t>(std::clamp(fraction, 0.0f, 1.0f) *
                                     1000000.0f + 0.5f);
      };
      std::snprintf(
          value, sizeof(value),
          "%s:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u",
          passed ? "pass" : "fail", probe->setup_ready ? 1u : 0u,
          probe->scene_ready ? 1u : 0u, probe->rotation_applied ? 1u : 0u,
          probe->rider_setup ? 1u : 0u,
          probe->rider_frame_continuity ? 1u : 0u,
          probe->rider_frame_scene_sealed ? 1u : 0u,
          probe->rider_provenance_sealed ? 1u : 0u,
          probe->fixture_reference_blocked ? 1u : 0u,
          probe->rotation_control_unblocked ? 1u : 0u,
          probe->baseline_unblocked ? 1u : 0u,
          probe->historical_dispatched ? 1u : 0u,
          probe->historical_blocked ? 1u : 0u,
          probe->authority_unchanged ? 1u : 0u, probe->candidate_count,
          probe->rider_continuity_samples,
          scaleFraction(probe->baseline_fraction),
          scaleFraction(probe->historical_fraction), failure);
      gi.cvarForceSet("sg_worr_rewind_mover_selftest_status", value);
    }
  } status{probe};
  if (!initialized) {
    status.failure = 1;
    return false;
  }

  // The probe runs only through an explicit `sv` command on a dedicated
  // fixture map. It still uses the production game entities, provider import,
  // scene builder, baseline, and transformed trace path.
  worr_sgame_rewind_collision::Map map{};
  if (!rewindCollisionImport) {
    status.failure = 2;
    return false;
  }
  if (rewindCollisionImport->struct_size != sizeof(*rewindCollisionImport) ||
      rewindCollisionImport->api_version !=
          worr_sgame_rewind_collision::kApiVersion ||
      !rewindCollisionImport->GetMapIdentity ||
      !rewindCollisionImport->ResolveInlineBrush ||
      !rewindCollisionImport->TraceTransformed) {
    status.failure = 3;
    return false;
  }
  if (!rewindCollisionImport->GetMapIdentity(&map)) {
    status.failure = 4;
    return false;
  }
  if (map.struct_size != sizeof(map) ||
      map.schema_version != worr_sgame_rewind_collision::kSchemaVersion ||
      map.map_epoch == 0 || map.inline_model_count == 0 ||
      map.reserved0 != 0) {
    status.failure = 5;
    return false;
  }

  const std::size_t total = std::min<std::size_t>(
      static_cast<std::size_t>(MAX_ENTITIES),
      static_cast<std::size_t>(globals.numEntities));
  const std::size_t firstMover = std::min<std::size_t>(
      total, static_cast<std::size_t>(game.maxClients) + 1u);
  gentity_t *mover = nullptr;
  worr_event_entity_ref_v1 identity{};
  worr_sgame_rewind_collision::Asset asset{};
  for (std::size_t index = firstMover; index < total; ++index) {
    gentity_t *candidate = &g_entities[index];
    worr_event_entity_ref_v1 candidateIdentity{};
    worr_sgame_rewind_collision::Asset candidateAsset{};
    if (!EligibleLiveMover(candidate) || !EntityRef(candidate, candidateIdentity) ||
        !ResolveMoverAsset(*candidate, map, candidateAsset) ||
        candidateAsset.local_maxs[0] - candidateAsset.local_mins[0] < 16.0f ||
        candidateAsset.local_maxs[1] - candidateAsset.local_mins[1] <=
            candidateAsset.local_maxs[0] - candidateAsset.local_mins[0] +
                4.0f) {
      continue;
    }
    mover = candidate;
    identity = candidateIdentity;
    asset = candidateAsset;
    break;
  }
  if (!mover) {
    status.failure = 6;
    return false;
  }

  gentity_t *rider = nullptr;
  for (std::size_t index = 1;
       index <= static_cast<std::size_t>(game.maxClients) &&
       index < poseTracks.size() + 1u;
       ++index) {
    gentity_t *candidate = &g_entities[index];
    if (EligibleLivePlayer(candidate)) {
      rider = candidate;
      break;
    }
  }
  const std::size_t riderIndex = ClientIndex(rider);
  if (!rider || riderIndex >= poseTracks.size()) {
    status.failure = 14;
    return false;
  }
  worr_event_entity_ref_v1 riderIdentity{};
  if (!EntityRef(rider, riderIdentity)) {
    status.failure = 15;
    return false;
  }
  uint64_t normalFrameCaptureTimeUs = 0;
  MoverTrack *frameMoverTrack = FindMoverTrack(identity);
  if (!frameMoverTrack ||
      !RuntimeRiderFrameContinuity(poseTracks[riderIndex], *frameMoverTrack,
                                   identity, probe->rider_continuity_samples,
                                   normalFrameCaptureTimeUs)) {
    status.failure = 16;
    return false;
  }
  probe->rider_frame_continuity = true;

  // Freeze a scene at the newest pair captured by ordinary server frames. This
  // proves that the live pusher/player history is usable by the same immutable
  // scene builder that authoritative rewind traces consume, before the scoped
  // diagnostic setup below changes either entity.
  worr_rewind_policy_decision_v1 normalFrameDecision{};
  normalFrameDecision.struct_size = sizeof(normalFrameDecision);
  normalFrameDecision.schema_version = WORR_REWIND_ABI_VERSION;
  normalFrameDecision.flags = WORR_REWIND_DECISION_ACCEPTED;
  normalFrameDecision.reason = WORR_REWIND_POLICY_EXACT;
  normalFrameDecision.command_id = {1u, 2u};
  normalFrameDecision.snapshot_id = {map.map_epoch, 2u};
  normalFrameDecision.source_snapshot_id = normalFrameDecision.snapshot_id;
  normalFrameDecision.watermark_provenance =
      WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
  normalFrameDecision.requested_time_us = normalFrameCaptureTimeUs;
  normalFrameDecision.mapped_time_us = normalFrameCaptureTimeUs;
  normalFrameDecision.applied_time_us = normalFrameCaptureTimeUs;
  if (!Worr_RewindPolicyDecisionValidateV1(&normalFrameDecision, true)) {
    status.failure = 17;
    return false;
  }
  const worr_rewind_scene_v1 *normalFrameScene =
      CanonicalScene(riderIndex, normalFrameDecision);
  const worr_rewind_scene_candidate_v1 *normalFrameRider = nullptr;
  const worr_rewind_scene_candidate_v1 *normalFrameMover = nullptr;
  if (normalFrameScene) {
    for (uint32_t index = 0; index < normalFrameScene->count; ++index) {
      const worr_rewind_scene_candidate_v1 &candidate =
          normalFrameScene->slots[index];
      if (candidate.pose.entity.index == riderIdentity.index &&
          candidate.pose.entity.generation == riderIdentity.generation &&
          candidate.pose.collision_shape == WORR_REWIND_COLLISION_BOUNDS) {
        normalFrameRider = &candidate;
      }
      if (candidate.pose.entity.index == identity.index &&
          candidate.pose.entity.generation == identity.generation &&
          candidate.pose.collision_shape == WORR_REWIND_COLLISION_BRUSH_MODEL) {
        normalFrameMover = &candidate;
      }
    }
  }
  float normalRelativeOrigin[3]{};
  float normalRelativeAngles[3]{};
  if (normalFrameRider && normalFrameMover) {
    for (std::size_t component = 0; component < 3; ++component) {
      normalRelativeOrigin[component] =
          normalFrameRider->pose.origin[component] -
          normalFrameMover->pose.origin[component];
      normalRelativeAngles[component] =
          normalFrameRider->pose.angles[component] -
          normalFrameMover->pose.angles[component];
    }
  }
  probe->rider_frame_scene_sealed =
      normalFrameScene &&
      (normalFrameScene->flags & WORR_REWIND_SCENE_SEALED) != 0 &&
      normalFrameRider && normalFrameMover &&
      normalFrameRider->query_reason == WORR_REWIND_QUERY_EXACT &&
      normalFrameMover->query_reason == WORR_REWIND_QUERY_EXACT &&
      normalFrameRider->pose.server_time_us == normalFrameCaptureTimeUs &&
      normalFrameMover->pose.server_time_us == normalFrameCaptureTimeUs &&
      (normalFrameRider->pose.flags & WORR_REWIND_POSE_HAS_MOVER) != 0 &&
      normalFrameRider->pose.mover.index == identity.index &&
      normalFrameRider->pose.mover.generation == identity.generation &&
      std::memcmp(normalFrameRider->pose.mover_relative_origin,
                  normalRelativeOrigin, sizeof(normalRelativeOrigin)) == 0 &&
      std::memcmp(normalFrameRider->pose.mover_relative_angles,
                  normalRelativeAngles, sizeof(normalRelativeAngles)) == 0;
  if (!probe->rider_frame_scene_sealed) {
    status.failure = 17;
    return false;
  }

  uint64_t captureTimeUs = 0;
  if (!CurrentAuthoritativeTimeUs(captureTimeUs)) {
    status.failure = 7;
    return false;
  }

  // Keep all setup mutations scoped and restore the active map entity even if
  // a later assertion rejects the probe. The before/after authority guard is
  // deliberately taken after setup, so it measures the real trace path only.
  struct RestoreMover {
    gentity_t *entity;
    Vector3 origin;
    Vector3 angles;
    bool linked;

    ~RestoreMover() {
      entity->s.origin = origin;
      entity->s.angles = angles;
      if (linked)
        gi.linkEntity(entity);
      else
        gi.unlinkEntity(entity);
    }
  } restore{mover, mover->s.origin, mover->s.angles, mover->linked};

  struct RestoreRider {
    gentity_t *entity;
    Vector3 origin;
    Vector3 angles;
    gentity_t *ground;
    bool linked;

    ~RestoreRider() {
      entity->s.origin = origin;
      entity->s.angles = angles;
      entity->groundEntity = ground;
      if (linked)
        gi.linkEntity(entity);
      else
        gi.unlinkEntity(entity);
    }
  } riderRestore{rider, rider->s.origin, rider->s.angles, rider->groundEntity,
                  rider->linked};

  // The fixture brush has an intentionally asymmetric XY footprint.  Capture
  // it at a quarter turn so a ray at its rotated-only X extent proves that the
  // sealed pose's angles reach the native transformed-BSP provider; an
  // unrotated asset at the same origin cannot block that ray.
  mover->s.angles[YAW] += 90.0f;
  gi.linkEntity(mover);
  probe->rotation_applied = true;

  // The runner creates a real engine bot before invoking this console command.
  // Place that actual spawned player on the moved brush and retain a non-zero
  // angle offset, so the ordinary player capture must record both rider
  // identity and mover-relative origin/angles.
  rider->groundEntity = mover;
  rider->s.origin = mover->s.origin;
  rider->s.origin.z += asset.local_maxs[2] - rider->mins.z;
  rider->s.angles = mover->s.angles;
  rider->s.angles[YAW] += 15.0f;
  gi.linkEntity(rider);
  probe->rider_setup = true;
  const Vector3 expectedRiderRelativeOrigin = rider->s.origin - mover->s.origin;
  const Vector3 expectedRiderRelativeAngles = rider->s.angles - mover->s.angles;

  // Capture the fixture's authentic inline BSP at its historical transform.
  // The current collider then moves by 96 units: it would intersect the same
  // query if the baseline did not omit represented live movers.
  LagCompensation_RecordFrame(rider);
  LagCompensation_RecordMovers();
  MoverTrack *moverTrack = FindMoverTrack(identity);
  if (!moverTrack || moverTrack->history.count == 0) {
    status.failure = 9;
    return false;
  }

  // Startup commands can run in the same simulation timestamp as the map's
  // normal final-frame mover capture.  Keep that production call above, then
  // append one strictly later, fully validated test sample through the same
  // builder/history ABI so this probe cannot accidentally select the preceding
  // unrotated pose.  The temporary sample and every frozen scene cache are
  // restored before the command returns.
  if (captureTick == std::numeric_limits<uint32_t>::max() ||
      captureTimeUs > std::numeric_limits<uint64_t>::max() - 1000u) {
    status.failure = 13;
    return false;
  }
  struct RestoreProbeMoverHistories {
    std::array<worr_rewind_history_v1, kMoverTrackCapacity> histories{};
    std::array<bool, kMoverTrackCapacity> touched{};

    ~RestoreProbeMoverHistories() {
      for (std::size_t index = 0; index < moverTracks.size(); ++index) {
        if (touched[index])
          moverTracks[index].history = histories[index];
      }
      InvalidateSceneCaches();
    }
  } historyRestore{};
  struct RestoreProbePlayerHistory {
    PoseTrack *track;
    worr_rewind_history_v1 history;

    ~RestoreProbePlayerHistory() {
      track->history = history;
      InvalidateSceneCaches();
    }
  } riderHistoryRestore{&poseTracks[riderIndex], poseTracks[riderIndex].history};
  const uint64_t probeCaptureTimeUs = captureTimeUs + 1000u;
  if (riderHistoryRestore.track->history.count >=
      riderHistoryRestore.track->history.capacity) {
    status.failure = 15;
    return false;
  }
  worr_rewind_pose_v1 riderCapture{};
  uint32_t riderAppendReason = WORR_REWIND_APPEND_REJECT_INVALID;
  if (!BuildPose(*rider, probeCaptureTimeUs, riderCapture)) {
    status.failure = 15;
    return false;
  }
  riderCapture.server_tick = captureTick + 1u;
  if (!Worr_RewindHistoryAppendV1(&riderHistoryRestore.track->history,
                                   &riderCapture, &riderAppendReason) ||
      riderAppendReason != WORR_REWIND_APPEND_ACCEPTED) {
    status.failure = 15;
    return false;
  }
  bool selectedCaptureAppended = false;
  for (std::size_t index = 0; index < moverTracks.size(); ++index) {
    MoverTrack &track = moverTracks[index];
    gentity_t *current = EntityFromRef(track.identity);
    if (!track.initialized || !EligibleLiveMover(current))
      continue;
    if (track.history.count >= track.history.capacity) {
      status.failure = 13;
      return false;
    }
    historyRestore.histories[index] = track.history;
    historyRestore.touched[index] = true;
    worr_rewind_pose_v1 probeCapture{};
    uint32_t appendReason = WORR_REWIND_APPEND_REJECT_INVALID;
    if (!BuildMoverPose(*current, probeCaptureTimeUs, map, probeCapture)) {
      status.failure = 13;
      return false;
    }
    probeCapture.server_tick = captureTick + 1u;
    if (!Worr_RewindHistoryAppendV1(&track.history, &probeCapture,
                                     &appendReason) ||
        appendReason != WORR_REWIND_APPEND_ACCEPTED) {
      status.failure = 13;
      return false;
    }
    selectedCaptureAppended |= &track == moverTrack;
  }
  if (!selectedCaptureAppended) {
    status.failure = 13;
    return false;
  }
  worr_rewind_policy_decision_v1 decision{};
  decision.struct_size = sizeof(decision);
  decision.schema_version = WORR_REWIND_ABI_VERSION;
  decision.flags = WORR_REWIND_DECISION_ACCEPTED;
  decision.reason = WORR_REWIND_POLICY_EXACT;
  decision.command_id = {1u, 1u};
  decision.snapshot_id = {map.map_epoch, 1u};
  decision.source_snapshot_id = decision.snapshot_id;
  decision.watermark_provenance = WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
  decision.requested_time_us = probeCaptureTimeUs;
  decision.mapped_time_us = probeCaptureTimeUs;
  decision.applied_time_us = probeCaptureTimeUs;
  if (!Worr_RewindPolicyDecisionValidateV1(&decision, true)) {
    status.failure = 8;
    return false;
  }

  const worr_rewind_scene_v1 *scene = CanonicalScene(0, decision);
  HistoricalBrushContext historicalBrushes{};
  if (!scene) {
    status.failure = 10;
    return false;
  }
  if (!BuildHistoricalBrushContext(scene, historicalBrushes)) {
    status.failure = 11;
    return false;
  }
  probe->setup_ready = true;
  probe->scene_ready = true;
  probe->candidate_count = scene->count;

  const worr_rewind_pose_v1 *historicalPose = nullptr;
  const worr_rewind_pose_v1 *historicalRiderPose = nullptr;
  for (uint32_t index = 0; index < scene->count; ++index) {
    const worr_rewind_pose_v1 &pose = scene->slots[index].pose;
    if (pose.entity.index == identity.index &&
        pose.entity.generation == identity.generation &&
        pose.collision_shape == WORR_REWIND_COLLISION_BRUSH_MODEL) {
      historicalPose = &pose;
    }
    if (pose.entity.index == riderIdentity.index &&
        pose.entity.generation == riderIdentity.generation &&
        pose.collision_shape == WORR_REWIND_COLLISION_BOUNDS) {
      historicalRiderPose = &pose;
    }
  }
  if (!historicalPose || !HistoricalBrushAssetMatches(*historicalPose, asset)) {
    status.failure = 12;
    return false;
  }
  float expectedRiderRelativeOriginArray[3]{};
  float expectedRiderRelativeAnglesArray[3]{};
  CopyVector(expectedRiderRelativeOriginArray, expectedRiderRelativeOrigin);
  CopyVector(expectedRiderRelativeAnglesArray, expectedRiderRelativeAngles);
  probe->rider_provenance_sealed =
      historicalRiderPose &&
      (historicalRiderPose->flags & WORR_REWIND_POSE_HAS_MOVER) != 0 &&
      historicalRiderPose->mover.index == identity.index &&
      historicalRiderPose->mover.generation == identity.generation &&
      historicalRiderPose->server_time_us == probeCaptureTimeUs &&
      std::memcmp(historicalRiderPose->mover_relative_origin,
                  expectedRiderRelativeOriginArray,
                  sizeof(expectedRiderRelativeOriginArray)) == 0 &&
      std::memcmp(historicalRiderPose->mover_relative_angles,
                  expectedRiderRelativeAnglesArray,
                  sizeof(expectedRiderRelativeAnglesArray)) == 0;
  if (!probe->rider_provenance_sealed) {
    status.failure = 15;
    return false;
  }

  const Vector3 historicalOrigin = ToVector(historicalPose->origin);
  const float centerZ =
      (historicalPose->mins[2] + historicalPose->maxs[2]) * 0.5f;
  const float localXHalf =
      (historicalPose->maxs[0] - historicalPose->mins[0]) * 0.5f;
  const float localYHalf =
      (historicalPose->maxs[1] - historicalPose->mins[1]) * 0.5f;
  const float rotationOnlyX = localXHalf + (localYHalf - localXHalf) * 0.5f;
  Vector3 captureForward, captureRight, captureUp;
  AngleVectors(ToVector(historicalPose->angles), captureForward, captureRight,
               captureUp);
  Vector3 start = historicalOrigin + captureRight * rotationOnlyX;
  Vector3 end = start;
  start -= captureForward * 64.0f;
  end += captureForward * 64.0f;
  start.z += centerZ;
  end.z += centerZ;
  const TraceBounds bounds = CalculateTraceBounds(start, nullptr, nullptr, end);
  const trace_t fixtureReference =
      ClipEntity(mover, start, nullptr, nullptr, end, MASK_SHOT);
  probe->fixture_reference_blocked =
      fixtureReference.ent == mover && fixtureReference.fraction > 0.0f &&
      fixtureReference.fraction < 0.5f;

  // Use the identical immutable asset/context at the pose's quarter-turn
  // predecessor as a negative control. The fixture now rotates during the
  // ordinary frame-continuity phase, so restore.angles is no longer generally
  // an unrotated world orientation. This must dispatch but remain clear:
  // otherwise the positive historical result would not demonstrate
  // rotation-sensitive collision behavior.
  worr_rewind_pose_v1 unrotatedPose = *historicalPose;
  unrotatedPose.angles[YAW] -= 90.0f;
  trace_t rotationControl{};
  rotationControl.fraction = 1.0f;
  const bool rotationControlDispatched = ClipHistoricalBrushPose(
      rotationControl, unrotatedPose, historicalBrushes, start, nullptr,
      nullptr, end, MASK_SHOT);
  probe->rotation_control_unblocked =
      rotationControlDispatched && rotationControl.fraction == 1.0f;

  mover->s.origin.x += 96.0f;
  mover->s.angles = restore.angles;
  gi.linkEntity(mover);

  const uint64_t authorityBefore = AuthoritativeCollisionHash();
  trace_t result = TraceCurrentScene(start, nullptr, nullptr, end, nullptr,
                                     MASK_SHOT, nullptr, 0, true,
                                     &historicalBrushes, bounds);
  probe->baseline_fraction = result.fraction;
  probe->baseline_unblocked = result.fraction == 1.0f;
  const bool dispatched = ClipHistoricalBrushPose(
      result, *historicalPose, historicalBrushes, start, nullptr, nullptr, end,
      MASK_SHOT);
  probe->historical_dispatched = dispatched;
  probe->historical_fraction = result.fraction;
  probe->historical_blocked =
      dispatched && result.ent == mover && result.fraction > 0.0f &&
      result.fraction < 0.5f;
  probe->authority_unchanged =
      authorityBefore == AuthoritativeCollisionHash();

  status.passed = probe->setup_ready && probe->scene_ready &&
                  probe->rotation_applied && probe->rider_setup &&
                  probe->rider_frame_continuity &&
                  probe->rider_frame_scene_sealed &&
                  probe->rider_provenance_sealed &&
                  probe->fixture_reference_blocked &&
                  probe->rotation_control_unblocked &&
                  probe->baseline_unblocked && probe->historical_blocked &&
                  probe->authority_unchanged;
  return status.passed;
}

bool LagCompensation_ArmRailDamageRuntimeProbe() {
  if (!initialized)
    return false;

  std::array<gentity_t *, 2> players{};
  uint32_t playerCount = 0;
  for (std::size_t index = 1;
       index <= static_cast<std::size_t>(game.maxClients) &&
       index < poseTracks.size() + 1u;
       ++index) {
    gentity_t *candidate = &g_entities[index];
    if (!candidate->inUse || !candidate->client || candidate->deadFlag)
      continue;
    players[playerCount++] = candidate;
    if (playerCount == players.size())
      break;
  }
  if (playerCount != players.size())
    return false;

  // The collision-only BSP intentionally has no world floor. Keep the two
  // real engine bots separated and gravity-free only while the dedicated
  // acceptance runner captures their ordinary end-frame history; otherwise
  // one can fall out of the fixture before the live weapon probe begins.
  constexpr Vector3 kOrigins[] = {{-192.0f, 192.0f, 64.0f},
                                  {64.0f, 192.0f, 64.0f}};
  for (std::size_t index = 0; index < players.size(); ++index) {
    gentity_t *player = players[index];
    player->s.origin = kOrigins[index];
    player->velocity = {};
    player->groundEntity = nullptr;
    player->gravity = 0.0f;
    gi.linkEntity(player);
  }
  return true;
}

bool LagCompensation_RunRailDamageRuntimeProbe(
    LagCompensationRailDamageRuntimeProbe *probe) {
  if (!probe)
    return false;
  *probe = {};
  struct PublishStatus {
    LagCompensationRailDamageRuntimeProbe *probe;
    bool passed = false;
    uint32_t failure = 0;

    ~PublishStatus() {
      if (!probe || !sg_worr_rewind_rail_damage_selftest_status)
        return;
      const auto scaleFraction = [](float fraction) {
        return static_cast<uint32_t>(std::clamp(fraction, 0.0f, 1.0f) *
                                     1000000.0f + 0.5f);
      };
      char value[192]{};
      std::snprintf(
          value, sizeof(value),
          "%s:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u",
          passed ? "pass" : "fail", probe->setup_ready ? 1u : 0u,
          probe->history_ready ? 1u : 0u,
          probe->current_world_miss ? 1u : 0u,
          probe->rejected_current_fallback ? 1u : 0u,
          probe->rejected_no_damage ? 1u : 0u,
          probe->legacy_rewind_selected ? 1u : 0u,
          probe->rail_policy_observed ? 1u : 0u,
          probe->near_latency_hit ? 1u : 0u,
          probe->bounded_latency_hit ? 1u : 0u,
          probe->capped_latency_hit ? 1u : 0u,
          probe->damage_applied ? 1u : 0u,
          probe->geometry_unchanged ? 1u : 0u,
          probe->query_authority_unchanged ? 1u : 0u,
          probe->candidate_count, probe->damage_amount,
          scaleFraction(probe->current_fraction),
          scaleFraction(probe->near_latency_fraction),
          scaleFraction(probe->bounded_latency_fraction),
          scaleFraction(probe->capped_latency_fraction), failure);
      gi.cvarForceSet("sg_worr_rewind_rail_damage_selftest_status", value);
    }
  } status{probe};

  // This probe is deliberately explicit about all opt-in state. It is run
  // only by a dedicated fixture command and must never make a disabled server
  // look as though it was evaluating live lag compensation.
  if (!initialized || !deathmatch || !deathmatch->integer ||
      !g_lagCompensation || !g_lagCompensation->integer) {
    status.failure = 1;
    return false;
  }
  if (!ObservationEnabled()) {
    status.failure = 2;
    return false;
  }

  gentity_t *shooter = nullptr;
  gentity_t *target = nullptr;
  for (std::size_t index = 1;
       index <= static_cast<std::size_t>(game.maxClients) &&
       index < poseTracks.size() + 1u;
       ++index) {
    gentity_t *candidate = &g_entities[index];
    if (!EligibleLivePlayer(candidate) || !candidate->client)
      continue;
    if (!shooter)
      shooter = candidate;
    else {
      target = candidate;
      break;
    }
  }
  if (!shooter || !target || shooter == target) {
    status.failure = 3;
    return false;
  }

  const std::size_t shooterIndex = ClientIndex(shooter);
  const std::size_t targetIndex = ClientIndex(target);
  if (shooterIndex >= poseTracks.size() || targetIndex >= poseTracks.size() ||
      !poseTracks[shooterIndex].initialized ||
      !poseTracks[targetIndex].initialized) {
    status.failure = 4;
    return false;
  }
  worr_event_entity_ref_v1 targetIdentity{};
  if (!EntityRef(target, targetIdentity)) {
    status.failure = 5;
    return false;
  }

  uint64_t currentTimeUs = 0;
  if (!CurrentAuthoritativeTimeUs(currentTimeUs)) {
    status.failure = 6;
    return false;
  }
  const uint32_t currentFrame = gi.ServerFrame();
  const PoseTrack &shooterTrack = poseTracks[shooterIndex];
  struct HistoricalAcknowledgement {
    LegacyFrameSample frame{};
    worr_rewind_pose_result_v1 target{};
    uint64_t rawAgeUs = 0;
    bool found = false;
  };
  HistoricalAcknowledgement nearAcknowledgement{};
  HistoricalAcknowledgement boundedAcknowledgement{};
  HistoricalAcknowledgement cappedAcknowledgement{};
  const uint64_t maxRewindUs =
      static_cast<uint64_t>(MaxRewindMs()) * UINT64_C(1000);
  const uint64_t nearWindowUs = maxRewindUs / 2u;
  for (uint32_t age = 0; age < shooterTrack.legacyFrames.count; ++age) {
    const LegacyFrameSample &sample =
        NewestLegacyFrame(shooterTrack.legacyFrames, age);
    if (!sample.serverFrame || sample.serverFrame >= currentFrame ||
        sample.timeUs > currentTimeUs) {
      continue;
    }
    const uint64_t rawAgeUs = currentTimeUs - sample.timeUs;
    worr_rewind_pose_query_v1 query{};
    query.struct_size = sizeof(query);
    query.schema_version = WORR_REWIND_ABI_VERSION;
    query.entity = targetIdentity;
    query.map_epoch = historyMapEpoch;
    query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
    query.target_time_us = sample.timeUs;
    worr_rewind_pose_result_v1 candidate{};
    if (!Worr_RewindHistoryQueryV1(&poseTracks[targetIndex].history, &query,
                                   &candidate) ||
        !candidate.found ||
        candidate.pose.collision_shape != WORR_REWIND_COLLISION_BOUNDS ||
        (candidate.pose.flags & WORR_REWIND_POSE_LINKED) == 0 ||
        (candidate.pose.flags & WORR_REWIND_POSE_DAMAGEABLE) == 0) {
      continue;
    }
    HistoricalAcknowledgement *selected = nullptr;
    if (rawAgeUs <= nearWindowUs && !nearAcknowledgement.found) {
      selected = &nearAcknowledgement;
    } else if (rawAgeUs <= maxRewindUs &&
               !boundedAcknowledgement.found) {
      selected = &boundedAcknowledgement;
    } else if (rawAgeUs > maxRewindUs && !cappedAcknowledgement.found) {
      selected = &cappedAcknowledgement;
    }
    if (!selected)
      continue;
    selected->frame = sample;
    selected->target = candidate;
    selected->rawAgeUs = rawAgeUs;
    selected->found = true;
    if (nearAcknowledgement.found && boundedAcknowledgement.found &&
        cappedAcknowledgement.found) {
      break;
    }
  }
  if (!nearAcknowledgement.found || !boundedAcknowledgement.found ||
      !cappedAcknowledgement.found) {
    status.failure = 7;
    return false;
  }
  probe->history_ready = true;

  // The two fixture bots are real engine players. The bot source marker is
  // cleared only for these two synchronous calls so ResolveTarget exercises
  // its normal non-bot command acknowledgement branch. The marker, command,
  // health, velocity, linkage, and geometry are restored before this console
  // command returns; ordinary gameplay never enters this diagnostic mode.
  struct RestorePlayers {
    gentity_t *shooter;
    gentity_t *target;
    Vector3 shooterOrigin;
    Vector3 shooterAngles;
    Vector3 shooterVelocity;
    gentity_t *shooterGround;
    float shooterGravity;
    bool shooterLinked;
    svflags_t shooterFlags;
    usercmd_t shooterCommand;
    Vector3 targetOrigin;
    Vector3 targetAngles;
    Vector3 targetVelocity;
    gentity_t *targetGround;
    float targetGravity;
    bool targetLinked;
    solid_t targetSolid;
    contents_t targetClipMask;
    int targetHealth;

    ~RestorePlayers() {
      shooter->s.origin = shooterOrigin;
      shooter->s.angles = shooterAngles;
      shooter->velocity = shooterVelocity;
      shooter->groundEntity = shooterGround;
      shooter->gravity = shooterGravity;
      shooter->svFlags = shooterFlags;
      shooter->client->cmd = shooterCommand;
      target->s.origin = targetOrigin;
      target->s.angles = targetAngles;
      target->velocity = targetVelocity;
      target->groundEntity = targetGround;
      target->gravity = targetGravity;
      target->health = targetHealth;
      if (shooterLinked)
        gi.linkEntity(shooter);
      else
        gi.unlinkEntity(shooter);
      if (targetLinked)
        gi.linkEntity(target);
      else
        gi.unlinkEntity(target);
    }
  } restore{shooter,
            target,
            shooter->s.origin,
            shooter->s.angles,
            shooter->velocity,
            shooter->groundEntity,
            shooter->gravity,
            shooter->linked,
            shooter->svFlags,
            shooter->client->cmd,
            target->s.origin,
            target->s.angles,
            target->velocity,
            target->groundEntity,
            target->gravity,
            target->linked,
            target->solid,
            target->clipMask,
            target->health};

  constexpr int kProbeDamage = 10;
  if (target->health <= kProbeDamage * 3 || !target->takeDamage ||
      target->deadFlag) {
    status.failure = 8;
    return false;
  }
  // The arm phase keeps the fixture bots stationary while normal frames fill
  // history. All three authority classes must therefore resolve the same
  // target geometry; otherwise a latency result could be an accidental
  // aim-position change rather than the selected rewind boundary.
  if (std::memcmp(nearAcknowledgement.target.pose.origin,
                  boundedAcknowledgement.target.pose.origin,
                  sizeof(nearAcknowledgement.target.pose.origin)) != 0 ||
      std::memcmp(nearAcknowledgement.target.pose.origin,
                  cappedAcknowledgement.target.pose.origin,
                  sizeof(nearAcknowledgement.target.pose.origin)) != 0) {
    status.failure = 8;
    return false;
  }
  const Vector3 historicalOrigin =
      ToVector(nearAcknowledgement.target.pose.origin);
  const Vector3 start = historicalOrigin - Vector3{160.0f, 0.0f, 0.0f};
  const Vector3 end = historicalOrigin + Vector3{8192.0f, 0.0f, 0.0f};
  const Vector3 currentTargetOrigin =
      historicalOrigin + Vector3{0.0f, 96.0f, 0.0f};
  shooter->s.origin = start;
  shooter->velocity = {};
  shooter->groundEntity = nullptr;
  target->s.origin = currentTargetOrigin;
  target->velocity = {};
  target->groundEntity = nullptr;
  gi.linkEntity(shooter);
  gi.linkEntity(target);
  probe->setup_ready = true;

  const trace_t current = NativeTrace(start, nullptr, nullptr, end, shooter,
                                      MASK_PROJECTILE, nullptr, 0);
  probe->current_fraction = current.fraction;
  probe->current_world_miss = current.fraction == 1.0f;
  if (!probe->current_world_miss) {
    status.failure = 9;
    return false;
  }

  shooter->svFlags &= ~SVF_BOT;
  const int targetHealthBefore = target->health;

  std::array<worr_rewind_observation_v1, kObservationCapacity> observations{};
  auto findRailObservation =
      [&](uint64_t firstSequence, bool historical,
          worr_rewind_observation_v1 &result) {
        uint32_t observationCount = 0;
        if (!LagCompensation_CopyObservations(observations.data(),
                                              observations.size(),
                                              &observationCount)) {
          return false;
        }
        for (uint32_t index = 0; index < observationCount; ++index) {
          const worr_rewind_observation_v1 &candidate = observations[index];
          if (candidate.observation_sequence < firstSequence ||
              candidate.weapon_policy != WORR_REWIND_WEAPON_RAILGUN) {
            continue;
          }
          if (!historical &&
              candidate.path == WORR_REWIND_OBSERVATION_PATH_CURRENT_WORLD &&
              candidate.outcome ==
                  WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_MISS &&
              candidate.fallback_reason ==
                  WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED) {
            result = candidate;
            return true;
          }
          if (historical &&
              candidate.path == WORR_REWIND_OBSERVATION_PATH_LEGACY &&
              candidate.outcome ==
                  WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT &&
              candidate.hit_entity.index == targetIdentity.index &&
              candidate.hit_entity.generation == targetIdentity.generation) {
            result = candidate;
            return true;
          }
        }
        return false;
      };

  // A current-or-future acknowledgement is never a rewind target. The same
  // production rail path must therefore miss the displaced current target and
  // leave health unchanged before the valid acknowledgement classes are tried.
  const uint64_t rejectedSequenceBefore = observationJournal.next_sequence;
  shooter->client->cmd.serverFrame = currentFrame;
  shooter->client->cmd.serverFrameDelta = 0;
  fire_rail(shooter, start, Vector3{1.0f, 0.0f, 0.0f}, kProbeDamage, 0);
  const int targetHealthAfterRejected = target->health;
  worr_rewind_observation_v1 rejected{};
  if (!findRailObservation(rejectedSequenceBefore, false, rejected)) {
    status.failure = 10;
    return false;
  }

  // These acknowledgements come from the shooter slot's ordinary server-frame
  // capture and are selected only after matching eligible target history. No
  // caller-provided timestamp or target identity reaches the production trace.
  auto fireHistorical = [&](const HistoricalAcknowledgement &acknowledgement,
                            worr_rewind_observation_v1 &result) {
    const uint64_t firstSequence = observationJournal.next_sequence;
    shooter->client->cmd.serverFrame = acknowledgement.frame.serverFrame;
    shooter->client->cmd.serverFrameDelta = 0;
    fire_rail(shooter, start, Vector3{1.0f, 0.0f, 0.0f}, kProbeDamage, 0);
    return findRailObservation(firstSequence, true, result);
  };
  worr_rewind_observation_v1 near{};
  worr_rewind_observation_v1 bounded{};
  worr_rewind_observation_v1 capped{};
  if (!fireHistorical(nearAcknowledgement, near) ||
      !fireHistorical(boundedAcknowledgement, bounded) ||
      !fireHistorical(cappedAcknowledgement, capped)) {
    status.failure = 10;
    return false;
  }

  constexpr uint32_t kAuthorityGuard =
      WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_CHECKED |
      WORR_REWIND_OBSERVATION_AUTHORITY_GUARD_UNCHANGED;
  probe->rejected_current_fallback =
      rejected.weapon_policy == WORR_REWIND_WEAPON_RAILGUN &&
      rejected.path == WORR_REWIND_OBSERVATION_PATH_CURRENT_WORLD &&
      rejected.outcome == WORR_REWIND_OBSERVATION_OUTCOME_CURRENT_MISS &&
      rejected.fallback_reason ==
          WORR_REWIND_OBSERVATION_FALLBACK_AUTHORITY_REJECTED &&
      (rejected.flags & WORR_REWIND_OBSERVATION_CURRENT_FALLBACK) != 0;
  probe->rejected_no_damage = targetHealthAfterRejected == targetHealthBefore;
  probe->legacy_rewind_selected =
      near.path == WORR_REWIND_OBSERVATION_PATH_LEGACY &&
      bounded.path == WORR_REWIND_OBSERVATION_PATH_LEGACY &&
      capped.path == WORR_REWIND_OBSERVATION_PATH_LEGACY &&
      near.fallback_reason == WORR_REWIND_OBSERVATION_FALLBACK_NONE &&
      bounded.fallback_reason == WORR_REWIND_OBSERVATION_FALLBACK_NONE &&
      capped.fallback_reason == WORR_REWIND_OBSERVATION_FALLBACK_NONE &&
      (near.flags & WORR_REWIND_OBSERVATION_POLICY_ACCEPTED) != 0 &&
      (bounded.flags & WORR_REWIND_OBSERVATION_POLICY_ACCEPTED) != 0 &&
      (capped.flags & WORR_REWIND_OBSERVATION_POLICY_ACCEPTED) != 0 &&
      (near.flags & WORR_REWIND_OBSERVATION_HISTORICAL_QUERY) != 0 &&
      (bounded.flags & WORR_REWIND_OBSERVATION_HISTORICAL_QUERY) != 0 &&
      (capped.flags & WORR_REWIND_OBSERVATION_HISTORICAL_QUERY) != 0;
  probe->rail_policy_observed =
      near.weapon_policy == WORR_REWIND_WEAPON_RAILGUN &&
      bounded.weapon_policy == WORR_REWIND_WEAPON_RAILGUN &&
      capped.weapon_policy == WORR_REWIND_WEAPON_RAILGUN;
  probe->near_latency_fraction = near.trace_fraction;
  probe->bounded_latency_fraction = bounded.trace_fraction;
  probe->capped_latency_fraction = capped.trace_fraction;
  probe->candidate_count =
      std::min({near.candidate_count, bounded.candidate_count,
                capped.candidate_count});
  auto historicalHit = [&](const worr_rewind_observation_v1 &observation) {
    return observation.outcome ==
               WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT &&
           observation.hit_entity.index == targetIdentity.index &&
           observation.hit_entity.generation == targetIdentity.generation &&
           observation.trace_fraction > 0.0f && observation.trace_fraction < 1.0f;
  };
  probe->near_latency_hit =
      historicalHit(near) && near.applied_time_us == nearAcknowledgement.frame.timeUs;
  probe->bounded_latency_hit =
      historicalHit(bounded) &&
      bounded.applied_time_us == boundedAcknowledgement.frame.timeUs;
  const uint64_t cappedTimeUs = currentTimeUs - maxRewindUs;
  probe->capped_latency_hit =
      historicalHit(capped) && cappedAcknowledgement.rawAgeUs > maxRewindUs &&
      capped.applied_time_us == cappedTimeUs &&
      capped.applied_time_us > cappedAcknowledgement.frame.timeUs;
  probe->damage_amount = targetHealthBefore > target->health
                             ? static_cast<uint32_t>(targetHealthBefore -
                                                     target->health)
                             : 0u;
  probe->damage_applied = probe->damage_amount == kProbeDamage * 3;
  probe->geometry_unchanged =
      target->s.origin == currentTargetOrigin && target->velocity == Vector3{} &&
      target->groundEntity == nullptr && target->linked &&
      target->solid == restore.targetSolid &&
      target->clipMask == restore.targetClipMask;
  probe->query_authority_unchanged =
      (rejected.flags & kAuthorityGuard) == kAuthorityGuard &&
      (near.flags & kAuthorityGuard) == kAuthorityGuard &&
      (bounded.flags & kAuthorityGuard) == kAuthorityGuard &&
      (capped.flags & kAuthorityGuard) == kAuthorityGuard;

  status.passed =
      probe->setup_ready && probe->history_ready && probe->current_world_miss &&
      probe->rejected_current_fallback && probe->rejected_no_damage &&
      probe->legacy_rewind_selected && probe->rail_policy_observed &&
      probe->near_latency_hit && probe->bounded_latency_hit &&
      probe->capped_latency_hit && probe->damage_applied &&
      probe->geometry_unchanged && probe->query_authority_unchanged;
  if (!status.passed)
    status.failure = 11;
  return status.passed;
}

bool LagCompensation_ArmCanonicalRailDamageRuntimeProbe() {
  return CanonicalRailProbeArm();
}

bool LagCompensation_ArmCanonicalRailMoverOcclusionRuntimeProbe() {
  return CanonicalRailMoverOcclusionProbeArm();
}

bool LagCompensation_ArmCanonicalMachinegunDamageRuntimeProbe() {
  return CanonicalMachinegunProbeArm();
}

bool LagCompensation_ArmCanonicalChaingunDamageRuntimeProbe() {
  return CanonicalChaingunProbeArm();
}

bool LagCompensation_ArmCanonicalSuperShotgunDamageRuntimeProbe() {
  return CanonicalSuperShotgunProbeArm();
}

bool LagCompensation_ArmCanonicalDisruptorDamageRuntimeProbe() {
  return CanonicalDisruptorProbeArm();
}

bool LagCompensation_ArmCanonicalRocketDamageRuntimeProbe() {
  return CanonicalRocketProbeArm();
}

bool LagCompensation_ArmCanonicalBfgDamageRuntimeProbe() {
  return CanonicalBfgProbeArm();
}

bool LagCompensation_ArmCanonicalIonRipperDamageRuntimeProbe() {
  return CanonicalIonRipperProbeArm();
}

bool LagCompensation_ArmCanonicalTeslaMineDamageRuntimeProbe() {
  return CanonicalTeslaMineProbeArm();
}

bool LagCompensation_ArmCanonicalTrapDamageRuntimeProbe() {
  return CanonicalTrapProbeArm();
}

bool LagCompensation_ArmCanonicalGrappleDamageRuntimeProbe() {
  return CanonicalGrappleProbeArm();
}

bool LagCompensation_ArmCanonicalOffhandHookRuntimeProbe() {
  return CanonicalOffhandHookProbeArm();
}

bool LagCompensation_ArmCanonicalProBallThrowRuntimeProbe() {
  return CanonicalProBallThrowProbeArm();
}

bool LagCompensation_ArmCanonicalGrenadeLauncherDamageRuntimeProbe() {
  return CanonicalGrenadeLauncherProbeArm();
}

bool LagCompensation_ArmCanonicalHandGrenadeDamageRuntimeProbe() {
  return CanonicalHandGrenadeProbeArm();
}

bool LagCompensation_ArmCanonicalHandGrenadeSplashRuntimeProbe() {
  return CanonicalHandGrenadeSplashProbeArm();
}

bool LagCompensation_ArmCanonicalProxLauncherDamageRuntimeProbe() {
  return CanonicalProxLauncherProbeArm();
}

bool LagCompensation_ArmCanonicalProxLauncherLifecycleRuntimeProbe() {
  return CanonicalProxLauncherLifecycleProbeArm();
}

bool LagCompensation_ArmCanonicalRocketSplashDamageRuntimeProbe() {
  return CanonicalRocketSplashProbeArm();
}

bool LagCompensation_ArmCanonicalPhalanxSplashDamageRuntimeProbe() {
  return CanonicalPhalanxSplashProbeArm();
}

bool LagCompensation_ArmCanonicalPlasmaGunDamageRuntimeProbe() {
  return CanonicalPlasmaGunProbeArm();
}

bool LagCompensation_ArmCanonicalPlasmaGunSplashDamageRuntimeProbe() {
  return CanonicalPlasmaGunSplashProbeArm();
}

bool LagCompensation_ArmCanonicalBlasterDamageRuntimeProbe() {
  return CanonicalBlasterProbeArm();
}

bool LagCompensation_ArmCanonicalHyperBlasterDamageRuntimeProbe() {
  return CanonicalHyperBlasterProbeArm();
}

bool LagCompensation_ArmCanonicalChainfistDamageRuntimeProbe() {
  return CanonicalChainfistProbeArm();
}

bool LagCompensation_ArmCanonicalEtfRifleDamageRuntimeProbe() {
  return CanonicalEtfRifleProbeArm();
}

bool LagCompensation_ArmCanonicalPhalanxDamageRuntimeProbe() {
  return CanonicalPhalanxProbeArm();
}

bool LagCompensation_ArmCanonicalPlasmaBeamDamageRuntimeProbe() {
  return CanonicalPlasmaBeamProbeArm();
}

bool LagCompensation_ArmCanonicalPlasmaBeamHeldDamageRuntimeProbe() {
  return CanonicalPlasmaBeamHeldProbeArm();
}

bool LagCompensation_ArmCanonicalPlasmaBeamSustainedDamageRuntimeProbe() {
  return CanonicalPlasmaBeamSustainedProbeArm();
}

bool LagCompensation_ArmCanonicalPlasmaBeamReleaseDamageRuntimeProbe() {
  return CanonicalPlasmaBeamReleaseProbeArm();
}

bool LagCompensation_ArmCanonicalPlasmaBeamWaterRetraceDamageRuntimeProbe() {
  return CanonicalPlasmaBeamWaterRetraceProbeArm();
}

bool LagCompensation_ArmCanonicalThunderboltDamageRuntimeProbe() {
  return CanonicalThunderboltProbeArm();
}

bool LagCompensation_ArmCanonicalThunderboltHeldDamageRuntimeProbe() {
  return CanonicalThunderboltHeldProbeArm();
}

bool LagCompensation_ArmCanonicalThunderboltSustainedDamageRuntimeProbe() {
  return CanonicalThunderboltSustainedProbeArm();
}

bool LagCompensation_ArmCanonicalThunderboltReleaseDamageRuntimeProbe() {
  return CanonicalThunderboltReleaseProbeArm();
}

bool LagCompensation_ArmCanonicalThunderboltWaterRetraceDamageRuntimeProbe() {
  return CanonicalThunderboltWaterRetraceProbeArm();
}

bool LagCompensation_ArmCanonicalThunderboltDischargeDamageRuntimeProbe() {
  return CanonicalThunderboltDischargeProbeArm();
}

bool LagCompensation_ArmCanonicalShotgunDamageRuntimeProbe() {
  return CanonicalShotgunProbeArm();
}

void LagCompensation_PrepareCanonicalWeaponDamageCommand(gentity_t *entity,
                                                          usercmd_t *command) {
  CanonicalRailProbePrepareCommand(entity, command);
}

void LagCompensation_RecordDeferredProjectileForwardCommand(
    gentity_t *entity, const usercmd_t *command) {
  if (!initialized || !entity || !entity->client || !command) {
    return;
  }

  const std::size_t shooterIndex = ClientIndex(entity);
  if (shooterIndex >= deferredProjectileForwardAuthorizations.size())
    return;
  DeferredProjectileForwardAuthorization &authorization =
      deferredProjectileForwardAuthorizations[shooterIndex];

  const Item *weapon = entity->client->pers.weapon;
  // ProBall's Chainfist-held throw is a release-bound action that ultimately
  // launches IT_BALL. Record it as ball authority only while the carrier still
  // possesses the ball; ordinary Chainfist melee and the direct-use pass path
  // remain outside this policy.
  const bool proBallHeldThrow =
      Game::Is(GameType::ProBall) && weapon &&
      weapon->id == IT_WEAPON_CHAINFIST &&
      entity->client->pers.inventory[IT_BALL] > 0;
  const item_id_t authorizationItem =
      proBallHeldThrow ? IT_BALL : (weapon ? weapon->id : IT_NULL);
  const uint32_t weaponPolicy =
      ProjectileForwardPolicyForWeapon(authorizationItem);
  if (weaponPolicy == WORR_REWIND_WEAPON_UNSPECIFIED ||
      ProjectileForwardMs() <= 0) {
    authorization = {};
    return;
  }
  const bool releaseOnlyPolicy =
      ReleaseOnlyProjectileForwardPolicy(weaponPolicy);
  const uint64_t authorizationLifetimeUs =
      DeferredProjectileForwardAuthorizationLifetimeUs(weaponPolicy);
  const bool releaseEdge =
      releaseOnlyPolicy &&
      (command->buttons & BUTTON_ATTACK) == 0 &&
      (entity->client->buttons & BUTTON_ATTACK) != 0;
  if (releaseOnlyPolicy) {
    // A subsequent prime always invalidates any unconsumed release. Other
    // zero-input commands retain the first edge because the normal held-throw
    // callback may occur on its following weapon-animation frame.
    if ((command->buttons & BUTTON_ATTACK) != 0)
      authorization = {};
    if (!releaseEdge)
      return;
  } else if ((command->buttons & BUTTON_ATTACK) == 0) {
    return;
  }

  bool canonicalContextAvailable = false;
  worr_rewind_policy_decision_v1 decision{};
  uint64_t currentTimeUs = 0;
  worr_event_entity_ref_v1 shooterIdentity{};
  if (!ResolveCanonicalDecision(shooterIndex, decision,
                                canonicalContextAvailable) ||
      (decision.flags & WORR_REWIND_DECISION_ACCEPTED) == 0 ||
      !CurrentAuthoritativeTimeUs(currentTimeUs) ||
      decision.mapped_time_us > currentTimeUs ||
      !EntityRef(entity, shooterIdentity) ||
      currentTimeUs >
          std::numeric_limits<uint64_t>::max() -
              authorizationLifetimeUs) {
    authorization = {};
    return;
  }

  authorization = {};
  authorization.shooter = shooterIdentity;
  authorization.decision = decision;
  authorization.weapon_item = authorizationItem;
  authorization.release_only = releaseOnlyPolicy;
  authorization.expires_at_us =
      currentTimeUs + authorizationLifetimeUs;
  authorization.launches_remaining =
      DeferredProjectileForwardLaunches(weaponPolicy);
  authorization.valid = authorization.launches_remaining != 0;
  if (authorization.valid && sg_lag_compensation_debug &&
      sg_lag_compensation_debug->integer >= 3 &&
      canonicalRailProbe.stage == CanonicalRailProbeStage::AwaitingDamage &&
      entity == canonicalRailProbe.shooter &&
      weaponPolicy == canonicalRailProbe.weapon_policy) {
    gi.Com_PrintFmt(
        "lagcomp projectile_forward deferred_record policy={} command={}:{} "
        "mapped_us={} expires_us={} launches={} release={}\n",
        weaponPolicy, decision.command_id.epoch, decision.command_id.sequence,
        static_cast<unsigned long long>(decision.mapped_time_us),
        static_cast<unsigned long long>(authorization.expires_at_us),
        authorization.launches_remaining,
        authorization.release_only ? 1u : 0u);
  }
}

void LagCompensation_ObserveCanonicalWeaponCallback(
    gentity_t *shooter, uint32_t weaponPolicy) {
  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      !shooter || shooter != canonicalRailProbe.shooter ||
      weaponPolicy != canonicalRailProbe.weapon_policy ||
      !CanonicalRailProbeSameEntity(shooter,
                                    canonicalRailProbe.shooter_identity)) {
    return;
  }
  // Generic firing can be deferred past a target's next zero-input client
  // update. Restore only the already-staged fixture target pose immediately
  // before the normal callback creates its current-world projectile; do not
  // alter health, collision shape, contact, splash, or damage authority.
  if (canonicalRailProbe.projectile_current_authority_required) {
    CanonicalRailProbePinTarget(canonicalRailProbe.target,
                                canonicalRailProbe.current_target_origin);
  }
  canonicalRailProbe.weapon_callback = true;
  CanonicalRailProbePublish();
}

void LagCompensation_ObserveCanonicalRailPierceHit(
    gentity_t *shooter, const trace_t &trace) {
  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      !canonicalRailProbe.historical_mover_occlusion_required ||
      shooter != canonicalRailProbe.shooter ||
      !CanonicalRailProbeSameEntity(shooter,
                                    canonicalRailProbe.shooter_identity) ||
      !canonicalRailProbe.historical_mover_baseline_clear ||
      !canonicalRailProbe.canonical_historical_hit ||
      canonicalRailProbe.observation_path !=
          WORR_REWIND_OBSERVATION_PATH_CANONICAL ||
      canonicalRailProbe.observation_outcome !=
          WORR_REWIND_OBSERVATION_OUTCOME_HISTORICAL_HIT ||
      canonicalRailProbe.observation_fallback !=
          WORR_REWIND_OBSERVATION_FALLBACK_NONE ||
      trace.fraction <= 0.0f || trace.fraction >= 1.0f ||
      !CanonicalRailProbeSameEntity(
          trace.ent, canonicalRailProbe.historical_mover_identity)) {
    return;
  }

  canonicalRailProbe.weapon_callback = true;
  canonicalRailProbe.historical_mover_occlusion_observed = true;
  CanonicalRailProbePublish();
}

void LagCompensation_ObserveThunderboltUnderwaterDischarge(gentity_t *shooter) {
  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      !canonicalRailProbe.thunderbolt_discharge_required ||
      shooter != canonicalRailProbe.shooter ||
      !CanonicalRailProbeSameEntity(shooter,
                                    canonicalRailProbe.shooter_identity)) {
    return;
  }
  canonicalRailProbe.weapon_callback = true;
  canonicalRailProbe.thunderbolt_discharge_observed = true;
  CanonicalRailProbePublish();
}

void LagCompensation_ObserveCurrentWorldProjectileSplashImpact(
    gentity_t *projectile, gentity_t *other, const trace_t &trace,
    uint32_t weaponPolicy) {
  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      !canonicalRailProbe.current_world_splash_required || !projectile ||
      !other || projectile->owner != canonicalRailProbe.shooter ||
      weaponPolicy != canonicalRailProbe.weapon_policy ||
      !CanonicalRailProbeSameEntity(
          other, canonicalRailProbe.current_world_splash_impact_identity) ||
      !CanonicalRailProbeSameEntity(
          projectile,
          canonicalRailProbe.current_world_splash_projectile_identity) ||
      trace.fraction >= 1.0f || trace.startSolid || trace.allSolid) {
    return;
  }
  canonicalRailProbe.current_world_splash_impact_observed = true;
  if (canonicalRailProbe.current_world_splash_damage_target_after_touch) {
    Vector3 direction = projectile->velocity;
    if (direction.normalize() <= 0.0f) {
      CanonicalRailProbeFail(23);
      return;
    }
    // A player hull is larger than the Plasma Gun's 20-unit splash radius,
    // so staging it near this impact can produce a second direct projectile
    // touch. Use a one-unit current-world damageable fixture on the clear side
    // instead. RadiusDamage still owns candidate discovery, line-of-sight,
    // falloff, and Damage; this merely supplies the isolated target geometry
    // after the real projectile contact has already been accepted.
    gentity_t *damageTarget = Spawn();
    damageTarget->className = "worr_canonical_current_world_splash_target";
    damageTarget->s.origin = trace.endPos - direction * 12.0f;
    damageTarget->mins = {-1.0f, -1.0f, -1.0f};
    damageTarget->maxs = {1.0f, 1.0f, 1.0f};
    damageTarget->solid = SOLID_BBOX;
    damageTarget->clipMask = MASK_PROJECTILE;
    damageTarget->moveType = MoveType::None;
    damageTarget->takeDamage = true;
    damageTarget->health = kCanonicalRailProbeTargetHealth;
    damageTarget->maxHealth = kCanonicalRailProbeTargetHealth;
    damageTarget->svFlags = SVF_NONE;
    gi.linkEntity(damageTarget);
    worr_event_entity_ref_v1 identity{};
    if (!EntityRef(damageTarget, identity)) {
      FreeEntity(damageTarget);
      CanonicalRailProbeFail(23);
      return;
    }
    canonicalRailProbe.current_world_splash_damage_target = damageTarget;
    canonicalRailProbe.current_world_splash_damage_target_identity = identity;
    canonicalRailProbe.target_health_before = damageTarget->health;
  }
  if (canonicalRailProbe.current_world_splash_clear_impact_after_touch) {
    // The production touch has already selected this isolated, non-damageable
    // fixture hull. Remove only that hull before the production RadiusDamage
    // call so a projectile relinked exactly at the contact point cannot trace
    // into the blocker it has just hit. This neither creates a victim nor
    // fabricates damage, line-of-sight, or a collision result.
    other->solid = SOLID_NOT;
    gi.unlinkEntity(other);
  }
  CanonicalRailProbePublish();
}

void LagCompensation_ObserveProxMineLanded(gentity_t *mine,
                                           gentity_t *trigger) {
  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      !canonicalRailProbe.prox_lifecycle_required || !mine || !trigger ||
      mine->teamMaster != canonicalRailProbe.shooter ||
      trigger->owner != mine ||
      !CanonicalRailProbeSameEntity(mine,
                                    canonicalRailProbe.prox_mine_identity)) {
    return;
  }
  // The mine/trigger pair is already linked by prox_land. Only now stage the
  // isolated real target at a fixed, clear point within the normal trigger
  // and damage radius. This neither creates contact nor changes the mine's
  // arming, visibility, explosion, or RadiusDamage decisions.
  canonicalRailProbe.current_target_origin =
      mine->s.origin + Vector3{64.0f, 0.0f, 64.0f};
  CanonicalRailProbePinTarget(canonicalRailProbe.target,
                              canonicalRailProbe.current_target_origin);
  canonicalRailProbe.prox_mine_landed = true;
  canonicalRailProbe.current_geometry_unchanged =
      canonicalRailProbe.target && canonicalRailProbe.target->linked &&
      canonicalRailProbe.target->s.origin ==
          canonicalRailProbe.current_target_origin;
  CanonicalRailProbePublish();
}

void LagCompensation_ObserveProxMineTriggered(gentity_t *mine,
                                              gentity_t *target) {
  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      !canonicalRailProbe.prox_lifecycle_required || !mine || !target ||
      target != canonicalRailProbe.target ||
      !canonicalRailProbe.prox_mine_landed ||
      !CanonicalRailProbeSameEntity(mine,
                                    canonicalRailProbe.prox_mine_identity)) {
    return;
  }
  // This runs only after the normal production radial candidate/visibility
  // checks have accepted the staged target.
  canonicalRailProbe.prox_mine_triggered = true;
  CanonicalRailProbePublish();
}

void LagCompensation_ObserveProxMineExploded(gentity_t *mine) {
  if (canonicalRailProbe.stage != CanonicalRailProbeStage::AwaitingDamage ||
      !canonicalRailProbe.prox_lifecycle_required || !mine ||
      !canonicalRailProbe.prox_mine_landed ||
      !canonicalRailProbe.prox_mine_triggered ||
      !CanonicalRailProbeSameEntity(mine,
                                    canonicalRailProbe.prox_mine_identity)) {
    return;
  }
  // Prox_Explode calls this only after its ordinary RadiusDamage invocation.
  canonicalRailProbe.prox_mine_exploded = true;
  CanonicalRailProbePublish();
}

trace_t LagCompensation_TraceLine(gentity_t *fromPlayer,
                                  const Vector3 &start,
                                  const Vector3 &end,
                                  gentity_t *passEntity,
                                  contents_t contentMask,
                                  gentity_t *const *ignoredEntities,
                                  size_t ignoredEntityCount,
                                  uint32_t weaponPolicy) {
  return TraceHistoricalScene(fromPlayer, start, nullptr, nullptr, end,
                              passEntity, contentMask, ignoredEntities,
                              ignoredEntityCount, weaponPolicy);
}

trace_t LagCompensation_Trace(gentity_t *fromPlayer,
                              const Vector3 &start,
                              const Vector3 &mins,
                              const Vector3 &maxs,
                              const Vector3 &end,
                              gentity_t *passEntity,
                              contents_t contentMask,
                              uint32_t weaponPolicy) {
  return TraceHistoricalScene(fromPlayer, start, &mins, &maxs, end,
                              passEntity, contentMask, nullptr, 0,
                              weaponPolicy);
}

LagCompensationProjectileForwardResult
LagCompensation_ResolveProjectileSpawnForward(gentity_t *shooter,
                                              gentity_t *projectile,
                                              uint32_t weaponPolicy) {
  LagCompensationProjectileForwardResult result{};
  result.weapon_policy = weaponPolicy;
  const auto finish = [&]() {
    CanonicalRailProbeObserveProjectileForward(shooter, result);
    MaybeReportDiagnostics();
    return result;
  };

  ++diagnostics.projectileForwardRequests;
  if (!initialized || !deathmatch || !deathmatch->integer ||
      !g_lagCompensation || !g_lagCompensation->integer || !shooter ||
      !shooter->client || (shooter->svFlags & SVF_BOT) || !projectile ||
      !projectile->inUse ||
      weaponPolicy == WORR_REWIND_WEAPON_UNSPECIFIED ||
      weaponPolicy >= WORR_REWIND_WEAPON_POLICY_COUNT ||
      ProjectileForwardMs() <= 0) {
    ++diagnostics.projectileForwardRejected;
    return finish();
  }
  if (!EntityRef(projectile, result.projectile_entity)) {
    ++diagnostics.projectileForwardRejected;
    return finish();
  }

  const std::size_t shooterIndex = ClientIndex(shooter);
  if (shooterIndex >= policyStates.size()) {
    ++diagnostics.projectileForwardRejected;
    return finish();
  }

  bool canonicalContextAvailable = false;
  worr_rewind_policy_decision_v1 decision{};
  uint64_t currentTimeUs = 0;
  if (!CurrentAuthoritativeTimeUs(currentTimeUs)) {
    ++diagnostics.projectileForwardRejected;
    return finish();
  }
  const bool resolvedActive = ResolveCanonicalDecision(
      shooterIndex, decision, canonicalContextAvailable);
  // A release-only held throw can occur one weapon-animation frame after its
  // command scope closes. Its deferred record is accepted only when it was
  // captured from that actual attack-to-release edge; a held prime can never
  // be relabeled as release authority.
  const bool resolvedDeferred =
      !resolvedActive &&
      ResolveDeferredProjectileForwardDecision(shooter, shooterIndex,
                                                weaponPolicy, currentTimeUs,
                                                decision);
  if (!resolvedActive && !resolvedDeferred) {
    ++diagnostics.projectileForwardRejected;
    return finish();
  }
  result.command_id = decision.command_id;
  if (sg_lag_compensation_debug && sg_lag_compensation_debug->integer >= 3 &&
      canonicalRailProbe.stage == CanonicalRailProbeStage::AwaitingDamage &&
      shooter == canonicalRailProbe.shooter &&
      weaponPolicy == canonicalRailProbe.weapon_policy) {
    gi.Com_PrintFmt(
        "lagcomp projectile_forward resolve policy={} active={} deferred={} "
        "command={}:{} mapped_us={} current_us={}\n",
        weaponPolicy, resolvedActive ? 1u : 0u, resolvedDeferred ? 1u : 0u,
        decision.command_id.epoch, decision.command_id.sequence,
        static_cast<unsigned long long>(decision.mapped_time_us),
        static_cast<unsigned long long>(currentTimeUs));
  }

  if ((decision.flags & WORR_REWIND_DECISION_ACCEPTED) == 0 ||
      decision.mapped_time_us > currentTimeUs) {
    ++diagnostics.projectileForwardRejected;
    return finish();
  }

  result.authenticated = true;
  result.authoritative_age_us = currentTimeUs - decision.mapped_time_us;
  ++diagnostics.projectileForwardAuthenticated;
  if (result.authoritative_age_us == 0)
    return finish();

  const uint64_t capUs =
      static_cast<uint64_t>(ProjectileForwardMs()) * UINT64_C(1000);
  result.advanced_age_us = std::min(result.authoritative_age_us, capUs);
  result.clamped = result.advanced_age_us != result.authoritative_age_us;
  if (result.clamped)
    ++diagnostics.projectileForwardClamped;
  if (result.advanced_age_us == 0)
    return finish();

  // A bouncing projectile needs a separate, conservative path. Mirror the
  // regular Toss gravity-before-move ordering over the accepted bounded age,
  // but do not fabricate a partial bounce or touch. Any current-world
  // contact rejects the advance entirely so normal G_Physics_Toss remains the
  // only owner of collision response, placement, triggers, and damage.
  if (weaponPolicy ==
          WORR_REWIND_WEAPON_GRENADE_LAUNCHER_BALLISTIC_SPAWN_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_HAND_GRENADE_RELEASE_BALLISTIC_SPAWN_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_PROX_MINE_BALLISTIC_DEPLOY_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_TESLA_MINE_RELEASE_BALLISTIC_DEPLOY_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_TRAP_RELEASE_BALLISTIC_DEPLOY_FORWARD ||
      weaponPolicy ==
          WORR_REWIND_WEAPON_PROBALL_HELD_THROW_BALLISTIC_SPAWN_FORWARD) {
    constexpr uint64_t kMinimumBallisticStepUs = UINT64_C(1000);
    constexpr uint64_t kMaximumBallisticStepUs = UINT64_C(20000);
    const uint64_t frameStepUs = std::clamp(
        static_cast<uint64_t>(std::llround(gi.frameTimeSec * 1000000.0f)),
        kMinimumBallisticStepUs, kMaximumBallisticStepUs);
    uint64_t remainingUs = result.advanced_age_us;
    Vector3 origin = projectile->s.origin;
    Vector3 velocity = projectile->velocity;
    trace_t trace{};

    while (remainingUs != 0) {
      const uint64_t stepUs = std::min(remainingUs, frameStepUs);
      const float stepSeconds = static_cast<float>(stepUs) / 1000000.0f;
      velocity += projectile->gravityVector *
                  (projectile->gravity * level.gravity * stepSeconds);
      const Vector3 end = origin + velocity * stepSeconds;
      trace = gi.trace(origin, projectile->mins, projectile->maxs, end,
                       projectile, projectile->clipMask);
      if (trace.startSolid || trace.allSolid || trace.fraction < 1.0f) {
        result.trace = trace;
        result.blocked = true;
        result.advanced_age_us = 0;
        ++diagnostics.projectileForwardBlocked;
        return finish();
      }
      origin = trace.endPos;
      remainingUs -= stepUs;
    }

    result.trace = trace;
    result.trace.endPos = origin;
    result.final_velocity = velocity;
    result.advanced = true;
    result.ballistic = true;
    ++diagnostics.projectileForwardAdvanced;
    return finish();
  }

  // This is deliberately the same current-world swept-hull query used by the
  // ordinary FlyMissile physics path. It does not consult historical player or
  // mover poses and cannot pass through a present-world blocker.
  const float elapsedSeconds =
      static_cast<float>(result.advanced_age_us) / 1000000.0f;
  const Vector3 end = projectile->s.origin + projectile->velocity * elapsedSeconds;
  result.trace = gi.trace(projectile->s.origin, projectile->mins,
                          projectile->maxs, end, projectile,
                          projectile->clipMask);
  result.advanced = true;
  result.blocked = result.trace.startSolid || result.trace.allSolid ||
                   result.trace.fraction < 1.0f;
  ++diagnostics.projectileForwardAdvanced;
  if (result.blocked)
    ++diagnostics.projectileForwardBlocked;
  return finish();
}

LagCompensationMeleeSelectionResult
LagCompensation_ResolveMeleePlayerCandidate(gentity_t *shooter,
                                            const Vector3 &start,
                                            const Vector3 &aim, int reach,
                                            uint32_t weaponPolicy) {
  LagCompensationMeleeSelectionResult result{};
  result.weapon_policy = weaponPolicy;
  const auto finish = [&]() {
    CanonicalRailProbeObserveMeleeSelection(shooter, result);
    MaybeReportDiagnostics();
    return result;
  };

  // Melee admits only the active canonical command. Unlike legacy historical
  // hitscan compatibility, there is no packet-ack or timestamp fallback that
  // could extend close-range authority outside the authenticated command.
  if (!initialized || !deathmatch || !deathmatch->integer ||
      !g_lagCompensation || !g_lagCompensation->integer || !shooter ||
      !shooter->client || (shooter->svFlags & SVF_BOT) || reach <= 0 ||
      weaponPolicy != WORR_REWIND_WEAPON_CHAINFIST_MELEE_HYBRID) {
    return finish();
  }

  const std::size_t shooterIndex = ClientIndex(shooter);
  if (shooterIndex >= poseTracks.size())
    return finish();

  bool canonicalContextAvailable = false;
  worr_rewind_policy_decision_v1 decision{};
  if (!ResolveCanonicalDecision(shooterIndex, decision,
                                canonicalContextAvailable) ||
      (decision.flags & WORR_REWIND_DECISION_ACCEPTED) == 0) {
    return finish();
  }
  result.authenticated = true;
  result.command_id = decision.command_id;
  result.applied_time_us = decision.applied_time_us;
  if (!CurrentAuthoritativeTimeUs(result.current_time_us) ||
      result.applied_time_us > result.current_time_us) {
    return finish();
  }

  Vector3 normalizedAim = aim;
  if (normalizedAim.normalize() <= 0.0f)
    return finish();

  gentity_t *nearest = nullptr;
  worr_event_entity_ref_v1 nearestIdentity{};
  Vector3 nearestHistoricalOrigin{};
  float nearestRange = std::numeric_limits<float>::max();
  const std::size_t clientCount = std::min<std::size_t>(
      static_cast<std::size_t>(game.maxClients), poseTracks.size());
  for (std::size_t index = 0; index < clientCount; ++index) {
    gentity_t *candidate = &g_entities[index + 1u];
    if (candidate == shooter || !EligibleLivePlayer(candidate))
      continue;

    worr_event_entity_ref_v1 identity{};
    if (!EntityRef(candidate, identity)) {
      // Do not let a partial historical roster exclude only some live
      // players. Returning selection_ready=false leaves the normal current
      // melee broadphase untouched for this attack.
      return finish();
    }

    worr_rewind_pose_query_v1 query{};
    query.struct_size = sizeof(query);
    query.schema_version = WORR_REWIND_ABI_VERSION;
    query.entity = identity;
    query.map_epoch = decision.snapshot_id.epoch;
    query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
    query.target_time_us = decision.applied_time_us;
    worr_rewind_pose_result_v1 historical{};
    if (!Worr_RewindHistoryQueryV1(&poseTracks[index].history, &query,
                                   &historical) ||
        !historical.found) {
      ++diagnostics.missingHistory;
      return finish();
    }
    if (historical.pose.collision_shape != WORR_REWIND_COLLISION_BOUNDS ||
        (historical.pose.flags &
         (WORR_REWIND_POSE_LINKED | WORR_REWIND_POSE_DAMAGEABLE)) !=
            (WORR_REWIND_POSE_LINKED | WORR_REWIND_POSE_DAMAGEABLE)) {
      continue;
    }

    const Vector3 historicalOrigin = ToVector(historical.pose.origin);
    const Vector3 historicalMins =
        historicalOrigin + ToVector(historical.pose.mins);
    const Vector3 historicalMaxs =
        historicalOrigin + ToVector(historical.pose.maxs);
    const Vector3 closestPointToCandidate =
        closest_point_to_box(start, historicalMins, historicalMaxs);
    const Vector3 closestPointToShooter = closest_point_to_box(
        closestPointToCandidate, shooter->s.origin + shooter->mins,
        shooter->s.origin + shooter->maxs);
    Vector3 direction = closestPointToCandidate - closestPointToShooter;
    const float range = direction.normalize();
    if (range > reach)
      continue;

    const Vector3 shrink{2, 2, 2};
    if (!boxes_intersect(historicalMins + shrink, historicalMaxs - shrink,
                         shooter->absMin + shrink,
                         shooter->absMax - shrink)) {
      direction =
          (((historicalMins + historicalMaxs) / 2) - start).normalized();
      if (direction.dot(normalizedAim) < 0.70f)
        continue;
    }

    if (!nearest || range < nearestRange) {
      nearest = candidate;
      nearestIdentity = identity;
      nearestHistoricalOrigin = historicalOrigin;
      nearestRange = range;
    }
  }

  // Every live player had a coherent historical query. Subsequent caller
  // broadphase filtering can therefore reject present-time player candidates
  // without accidentally replacing only part of the roster.
  result.selection_ready = true;
  if (!nearest)
    return finish();

  result.target_entity = nearestIdentity;
  result.historical_origin = nearestHistoricalOrigin;
  result.historical_eligible = true;
  const float displacement =
      (nearest->s.origin - nearestHistoricalOrigin).length();
  result.current_displacement_units = static_cast<uint32_t>(
      std::min<float>(displacement + 0.5f,
                      static_cast<float>(std::numeric_limits<uint32_t>::max())));
  if (displacement > static_cast<float>(MeleeMaxDisplacementUnits()))
    return finish();

  result.current_displacement_accepted = true;
  result.target = nearest;
  return finish();
}

bool LagCompensation_CopyObservations(
    worr_rewind_observation_v1 *records, uint32_t recordsCapacity,
    uint32_t *recordCount) {
  if (!recordCount || (!records && recordsCapacity != 0) ||
      observationJournal.struct_size != sizeof(observationJournal)) {
    return false;
  }
  if (!records) {
    *recordCount = observationJournal.count;
    return true;
  }
  return Worr_RewindObservationJournalCopyV1(
      &observationJournal, records, recordsCapacity, recordCount);
}

bool LagCompensation_GetObservationTelemetry(
    worr_rewind_observation_telemetry_v1 *telemetry) {
  if (!telemetry ||
      !Worr_RewindObservationJournalValidateV1(&observationJournal)) {
    return false;
  }
  *telemetry = observationJournal.telemetry;
  return true;
}
