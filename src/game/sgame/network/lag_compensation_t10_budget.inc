// FR-10-T10 production-bound maximum-capacity budget/load fixture.
//
// This file is included inside lag_compensation.cpp's private namespace so
// the fixture cannot drift from the production owner types or capacities. It
// intentionally has no heap-backed containers: the same caller-owned rewind
// history/scene APIs used by gameplay operate entirely on fixed std::array
// storage here.

constexpr uint32_t kT10BudgetPlayerCount =
    static_cast<uint32_t>(MAX_CLIENTS_KEX);
constexpr uint32_t kT10BudgetMoverCount = kMoverTrackCapacity;
constexpr uint32_t kT10BudgetCandidateCount = kSceneCandidateCapacity;
constexpr uint32_t kT10BudgetWarmupIterations = 32;
constexpr uint32_t kT10BudgetSampleCount = 256;
constexpr uint32_t kT10BudgetSampleBatchIterations = 1;
constexpr uint32_t kT10BudgetTotalIterations =
    kT10BudgetWarmupIterations +
    kT10BudgetSampleCount * kT10BudgetSampleBatchIterations;
constexpr uint64_t kT10BudgetP95LimitNs = UINT64_C(1666600);
constexpr uint64_t kT10BudgetOwnerCapBytes = UINT64_C(8) * 1024u * 1024u;
constexpr uint32_t kT10BudgetMapEpoch = 10;
constexpr uint64_t kT10BudgetBaseTimeUs = UINT64_C(1000000);
constexpr uint64_t kT10BudgetCaptureStepUs = UINT64_C(4000);
constexpr uint64_t kT10BudgetQueryAgeUs = UINT64_C(200000);

static_assert(MAX_CLIENTS_KEX == 32,
              "FR-10-T10 budget identity count changed");
static_assert(kHistoryCapacity == 512,
              "FR-10-T10 player history capacity changed");
static_assert(kMoverHistoryCapacity == 64,
              "FR-10-T10 mover history capacity changed");
static_assert(kMoverTrackCapacity == 64,
              "FR-10-T10 mover track capacity changed");
static_assert(kSceneCandidateCapacity == 96,
              "FR-10-T10 scene capacity changed");
static_assert(kT10BudgetQueryAgeUs ==
                  static_cast<uint64_t>(kDefaultMaxRewindMs) * 1000u,
              "FR-10-T10 load age drifted from the production default");

struct T10BudgetFixture {
  // Use the production owner types, not benchmark-only approximations. This
  // also charges their legacy-frame and identity metadata to the fixture's
  // explicit resident-storage evidence.
  std::array<PoseTrack, MAX_CLIENTS_KEX> players{};
  std::array<MoverTrack, kMoverTrackCapacity> movers{};
  FrozenSceneCache scene{};
  std::array<uint64_t, kT10BudgetSampleCount> elapsedNs{};
};

T10BudgetFixture t10BudgetFixture{};
cvar_t *sg_worr_rewind_budget_selftest_status = nullptr;

// This is the complete fixed production history/scene working set owned by
// T10. Cvar/import pointers reference external owners and the weapon-specific
// acceptance fixture is not part of the rewind history/scene owner. The
// benchmark fixture is reported separately and the stronger combined static
// bound below ensures even a diagnostic-capable dedicated binary stays under
// the same explicit cap.
constexpr uint64_t kT10ProductionFixedStorageBytes =
    sizeof(poseTracks) + sizeof(moverTracks) +
    sizeof(clientLifeGenerations) + sizeof(clientLifeGenerationExhausted) +
    sizeof(policyStates) + sizeof(decisionCaches) +
    sizeof(deferredProjectileForwardAuthorizations) + sizeof(sceneCaches) +
    sizeof(relabelScratch) + sizeof(traceCandidates) +
    sizeof(historicalProxy) + sizeof(diagnostics) +
    sizeof(observationStorage) + sizeof(observationJournal);
constexpr uint64_t kT10FixtureFixedStorageBytes = sizeof(t10BudgetFixture);
constexpr uint64_t kT10CombinedFixedStorageBytes =
    kT10ProductionFixedStorageBytes + kT10FixtureFixedStorageBytes;

static_assert(kT10ProductionFixedStorageBytes <= kT10BudgetOwnerCapBytes,
              "FR-10-T10 production owner exceeds 8 MiB");
static_assert(kT10CombinedFixedStorageBytes <= kT10BudgetOwnerCapBytes,
              "FR-10-T10 production plus load fixture exceeds 8 MiB");

struct T10BudgetResult {
  uint64_t queryCount = 0;
  uint64_t expectedQueryCount = 0;
  uint64_t overwriteCount = 0;
  uint64_t expectedOverwriteCount = 0;
  uint64_t capacityOverflows = 0;
  uint64_t p50Ns = 0;
  uint64_t p95Ns = 0;
  uint64_t p99Ns = 0;
  uint64_t workloadHash = 0;
  uint32_t failure = 0;
  bool historiesFull = false;
  bool sceneSealed = false;
  bool authorityUnchanged = false;
  bool allocationFree = true;
};

[[nodiscard]] uint64_t T10BudgetHashBytes(uint64_t hash, const void *data,
                                          std::size_t size) {
  const auto *bytes = static_cast<const unsigned char *>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= static_cast<uint64_t>(bytes[index]);
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

template <typename T>
[[nodiscard]] uint64_t T10BudgetHashObject(uint64_t hash, const T &object) {
  return T10BudgetHashBytes(hash, &object, sizeof(object));
}

// Covers live entity authority plus every production history/scene object the
// fixture's common API calls could otherwise accidentally alias. The fixture
// storage itself is deliberately excluded because it is the only state the
// workload is permitted to change.
[[nodiscard]] uint64_t T10BudgetAuthorityHash() {
  uint64_t hash = UINT64_C(14695981039346656037);
  const std::size_t entityCount =
      globals.numEntities > 0
          ? std::min<std::size_t>(static_cast<std::size_t>(globals.numEntities),
                                  static_cast<std::size_t>(MAX_ENTITIES))
          : 0u;
  if (g_entities && entityCount != 0) {
    hash = T10BudgetHashBytes(hash, g_entities,
                              entityCount * sizeof(g_entities[0]));
  }
  hash = T10BudgetHashObject(hash, poseTracks);
  hash = T10BudgetHashObject(hash, moverTracks);
  hash = T10BudgetHashObject(hash, clientLifeGenerations);
  hash = T10BudgetHashObject(hash, clientLifeGenerationExhausted);
  hash = T10BudgetHashObject(hash, policyStates);
  hash = T10BudgetHashObject(hash, decisionCaches);
  hash = T10BudgetHashObject(hash,
                             deferredProjectileForwardAuthorizations);
  hash = T10BudgetHashObject(hash, sceneCaches);
  hash = T10BudgetHashObject(hash, relabelScratch);
  hash = T10BudgetHashObject(hash, observationStorage);
  hash = T10BudgetHashObject(hash, observationJournal);
  hash = T10BudgetHashObject(hash, historyMapEpoch);
  hash = T10BudgetHashObject(hash, authoritativeMapEpoch);
  hash = T10BudgetHashObject(hash, captureTick);
  hash = T10BudgetHashObject(hash, lastCaptureTimeUs);
  return hash;
}

[[nodiscard]] worr_rewind_pose_v1
T10BudgetPose(uint32_t entityIndex, bool mover, uint32_t tick,
              uint64_t timeUs) {
  worr_rewind_pose_v1 pose{};
  pose.struct_size = sizeof(pose);
  pose.schema_version = WORR_REWIND_ABI_VERSION;
  pose.model_revision = WORR_REWIND_POSE_MODEL_REVISION;
  pose.flags = WORR_REWIND_POSE_LINKED;
  pose.entity = {entityIndex, 1u};
  pose.map_epoch = kT10BudgetMapEpoch;
  pose.server_tick = tick;
  pose.lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
  pose.solid = mover ? static_cast<uint32_t>(SOLID_BSP)
                     : static_cast<uint32_t>(SOLID_BBOX);
  pose.clip_flags = 1u;
  pose.collision_shape = mover ? WORR_REWIND_COLLISION_BRUSH_MODEL
                               : WORR_REWIND_COLLISION_BOUNDS;
  pose.collision_asset_id = mover ? entityIndex : 0u;
  pose.server_time_us = timeUs;

  const float motion = static_cast<float>(tick & 15u) * 0.03125f;
  pose.origin[0] = static_cast<float>(entityIndex * 4u) + motion;
  pose.origin[1] = static_cast<float>(entityIndex & 7u) * 8.0f;
  pose.origin[2] = mover ? 0.0f : 32.0f;
  pose.angles[1] = static_cast<float>(tick % 360u);
  pose.mins[0] = mover ? -24.0f : -16.0f;
  pose.mins[1] = mover ? -24.0f : -16.0f;
  pose.mins[2] = mover ? -8.0f : -24.0f;
  pose.maxs[0] = mover ? 24.0f : 16.0f;
  pose.maxs[1] = mover ? 24.0f : 16.0f;
  pose.maxs[2] = mover ? 8.0f : 32.0f;

  if (!mover) {
    // Every player identity exercises real mover-relative provenance. The
    // referenced mover is present later in the same sorted, sealed scene.
    const uint32_t moverIndex = kT10BudgetPlayerCount + entityIndex;
    pose.flags |= WORR_REWIND_POSE_DAMAGEABLE |
                  WORR_REWIND_POSE_HAS_MOVER;
    pose.mover = {moverIndex, 1u};
    pose.mover_relative_origin[0] = 1.0f;
    pose.mover_relative_origin[2] = 40.0f;
    pose.mover_relative_angles[1] = 5.0f;
  }
  return pose;
}

[[nodiscard]] bool T10BudgetAppend(worr_rewind_history_v1 &history,
                                   uint32_t entityIndex, bool mover,
                                   uint32_t tick, uint64_t timeUs) {
  const worr_rewind_pose_v1 pose =
      T10BudgetPose(entityIndex, mover, tick, timeUs);
  uint32_t reason = WORR_REWIND_APPEND_REJECT_INVALID;
  // The common append boundary canonicalizes and validates the candidate
  // before committing it.  Revalidating the same synthetic pose here was
  // benchmark-only duplicate work that production capture does not perform.
  return Worr_RewindHistoryAppendV1(&history, &pose, &reason) &&
         reason == WORR_REWIND_APPEND_ACCEPTED;
}

[[nodiscard]] bool T10BudgetInitialize(T10BudgetResult &result) {
  for (std::size_t index = 0; index < t10BudgetFixture.players.size();
       ++index) {
    if (!InitTrack(t10BudgetFixture.players[index],
                   static_cast<uint32_t>(index + 1u))) {
      result.failure = 2;
      return false;
    }
  }
  for (std::size_t index = 0; index < t10BudgetFixture.movers.size();
       ++index) {
    const uint32_t entityIndex =
        kT10BudgetPlayerCount + static_cast<uint32_t>(index) + 1u;
    MoverTrack &track = t10BudgetFixture.movers[index];
    if (!InitMoverTrack(track, entityIndex)) {
      result.failure = 3;
      return false;
    }
    track.identity = {entityIndex, 1u};
  }
  t10BudgetFixture.scene.valid = false;

  // Fill every player history to 512. Movers use the newest aligned 64
  // instants, leaving all 96 histories at their production capacity and at a
  // shared newest timestamp before the measured steady state begins.
  for (uint32_t sample = 0; sample < kHistoryCapacity; ++sample) {
    const uint32_t tick = sample + 1u;
    const uint64_t timeUs =
        kT10BudgetBaseTimeUs +
        static_cast<uint64_t>(sample) * kT10BudgetCaptureStepUs;
    for (uint32_t index = 0; index < kT10BudgetPlayerCount; ++index) {
      if (!T10BudgetAppend(t10BudgetFixture.players[index].history, index + 1u,
                           false, tick, timeUs)) {
        result.failure = 4;
        return false;
      }
    }
    if (sample + kMoverHistoryCapacity < kHistoryCapacity)
      continue;
    for (uint32_t index = 0; index < kT10BudgetMoverCount; ++index) {
      const uint32_t entityIndex = kT10BudgetPlayerCount + index + 1u;
      if (!T10BudgetAppend(t10BudgetFixture.movers[index].history,
                           entityIndex, true, tick, timeUs)) {
        result.failure = 5;
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] worr_rewind_policy_decision_v1
T10BudgetDecision(uint32_t sequence, uint64_t timeUs) {
  worr_rewind_policy_decision_v1 decision{};
  decision.struct_size = sizeof(decision);
  decision.schema_version = WORR_REWIND_ABI_VERSION;
  decision.flags = WORR_REWIND_DECISION_ACCEPTED;
  decision.reason = WORR_REWIND_POLICY_EXACT;
  decision.command_id = {kT10BudgetMapEpoch, sequence};
  decision.snapshot_id = {kT10BudgetMapEpoch, sequence};
  decision.source_snapshot_id = decision.snapshot_id;
  decision.watermark_provenance =
      WORR_COMMAND_RENDER_PROVENANCE_EXACT_COMMAND;
  decision.requested_time_us = timeUs;
  decision.mapped_time_us = timeUs;
  decision.applied_time_us = timeUs;
  return decision;
}

[[nodiscard]] bool T10BudgetQueryAndAdd(worr_rewind_history_v1 &history,
                                        worr_event_entity_ref_v1 identity,
                                        uint64_t timeUs,
                                        T10BudgetResult &result) {
  worr_rewind_pose_query_v1 query{};
  query.struct_size = sizeof(query);
  query.schema_version = WORR_REWIND_ABI_VERSION;
  query.entity = identity;
  query.map_epoch = kT10BudgetMapEpoch;
  query.required_lifecycle = WORR_REWIND_LIFECYCLE_ALIVE;
  query.target_time_us = timeUs;
  worr_rewind_pose_result_v1 queryResult{};
  ++result.queryCount;
  return Worr_RewindHistoryQueryV1(&history, &query, &queryResult) &&
         queryResult.found == 1u &&
         queryResult.reason == WORR_REWIND_QUERY_EXACT &&
         Worr_RewindSceneAddOwnedResultV1(&t10BudgetFixture.scene.scene,
                                           &queryResult);
}

[[nodiscard]] bool T10BudgetIteration(uint32_t iteration,
                                      T10BudgetResult &result) {
  const uint32_t tick = kHistoryCapacity + iteration + 1u;
  const uint64_t timeUs =
      kT10BudgetBaseTimeUs +
      static_cast<uint64_t>(tick - 1u) * kT10BudgetCaptureStepUs;
  // Exercise the production default target window, not a zero-age lookup.
  // Fifty 4 ms samples keep the exact timestamp inside both the full 512-pose
  // player rings and the full 64-pose mover rings throughout steady state.
  static_assert(kT10BudgetQueryAgeUs % kT10BudgetCaptureStepUs == 0,
                "FR-10-T10 query age must align to a capture instant");
  static_assert(kT10BudgetQueryAgeUs / kT10BudgetCaptureStepUs <
                    kMoverHistoryCapacity,
                "FR-10-T10 query age escaped the mover history");
  const uint64_t queryTimeUs = timeUs - kT10BudgetQueryAgeUs;

  for (uint32_t index = 0; index < kT10BudgetPlayerCount; ++index) {
    if (!T10BudgetAppend(t10BudgetFixture.players[index].history, index + 1u,
                         false, tick, timeUs)) {
      result.failure = 6;
      return false;
    }
  }
  for (uint32_t index = 0; index < kT10BudgetMoverCount; ++index) {
    const uint32_t entityIndex = kT10BudgetPlayerCount + index + 1u;
    if (!T10BudgetAppend(t10BudgetFixture.movers[index].history, entityIndex,
                         true, tick, timeUs)) {
      result.failure = 7;
      return false;
    }
  }

  const worr_rewind_policy_decision_v1 decision =
      T10BudgetDecision(iteration + 1u, queryTimeUs);
  // Scene initialization owns validation of the accepted decision, matching
  // CanonicalScene's production call path without a benchmark-only duplicate.
  if (!Worr_RewindSceneInitV1(
          &t10BudgetFixture.scene.scene,
          t10BudgetFixture.scene.storage.data(),
          static_cast<uint32_t>(t10BudgetFixture.scene.storage.size()),
          &decision)) {
    result.failure = 8;
    return false;
  }

  // Ascending entity identity is the production scene contract and keeps the
  // common scene builder bounded without a sorting allocation.
  for (uint32_t index = 0; index < kT10BudgetPlayerCount; ++index) {
    const worr_event_entity_ref_v1 identity{index + 1u, 1u};
    if (!T10BudgetQueryAndAdd(t10BudgetFixture.players[index].history,
                              identity, queryTimeUs, result)) {
      result.failure = 9;
      return false;
    }
  }
  for (uint32_t index = 0; index < kT10BudgetMoverCount; ++index) {
    const worr_event_entity_ref_v1 identity{
        kT10BudgetPlayerCount + index + 1u, 1u};
    if (!T10BudgetQueryAndAdd(t10BudgetFixture.movers[index].history,
                              identity, queryTimeUs, result)) {
      result.failure = 10;
      return false;
    }
  }
  if (t10BudgetFixture.scene.scene.count != kT10BudgetCandidateCount ||
      !Worr_RewindSceneSealV1(&t10BudgetFixture.scene.scene)) {
    result.failure = 11;
    return false;
  }
  return true;
}

[[nodiscard]] uint64_t T10BudgetPercentile(
    const std::array<uint64_t, kT10BudgetSampleCount> &sorted,
    uint32_t percentile) {
  const uint64_t rank =
      (static_cast<uint64_t>(percentile) * sorted.size() + 99u) / 100u;
  const std::size_t index =
      static_cast<std::size_t>(std::max<uint64_t>(rank, 1u) - 1u);
  return sorted[index];
}

[[nodiscard]] bool T10BudgetVerifyTelemetry(T10BudgetResult &result) {
  uint64_t queries = 0;
  uint64_t overwrites = 0;
  bool full = true;
  const auto consume = [&queries, &overwrites, &full](
                           const worr_rewind_history_v1 &history,
                           uint32_t capacity) {
    full = full && Worr_RewindHistoryValidateV1(&history) &&
           history.capacity == capacity && history.count == capacity &&
           history.telemetry.rejected_invalid == 0 &&
           history.telemetry.rejected_order == 0;
    queries += history.telemetry.queries;
    overwrites += history.telemetry.overwritten;
  };
  for (const PoseTrack &track : t10BudgetFixture.players)
    consume(track.history, kHistoryCapacity);
  for (const MoverTrack &track : t10BudgetFixture.movers)
    consume(track.history, kMoverHistoryCapacity);

  result.expectedQueryCount =
      static_cast<uint64_t>(kT10BudgetTotalIterations) *
      kT10BudgetCandidateCount;
  result.expectedOverwriteCount = result.expectedQueryCount;
  result.overwriteCount = overwrites;
  result.historiesFull = full;
  result.sceneSealed =
      t10BudgetFixture.scene.scene.count == kT10BudgetCandidateCount &&
      (t10BudgetFixture.scene.scene.flags & WORR_REWIND_SCENE_SEALED) != 0 &&
      Worr_RewindSceneValidateV1(&t10BudgetFixture.scene.scene);
  result.workloadHash = t10BudgetFixture.scene.scene.scene_hash;
  // A ring overwrite is the intended bounded steady state, not a capacity
  // overflow. Any count/capacity, API, or scene failure exits earlier; exact
  // overwrite telemetry below proves all captures remained within the rings.
  result.capacityOverflows = 0;
  return full && result.sceneSealed && queries == result.queryCount &&
         queries == result.expectedQueryCount &&
         overwrites == result.expectedOverwriteCount &&
         result.workloadHash != 0;
}

[[nodiscard]] bool T10BudgetExecute(T10BudgetResult &result) {
  if (!initialized) {
    result.failure = 1;
    return false;
  }
  if (kT10CombinedFixedStorageBytes > kT10BudgetOwnerCapBytes) {
    result.failure = 12;
    return false;
  }
  if (!T10BudgetInitialize(result))
    return false;

  uint32_t iteration = 0;
  for (; iteration < kT10BudgetWarmupIterations; ++iteration) {
    if (!T10BudgetIteration(iteration, result))
      return false;
  }
  // Every sample is one complete server-frame workload: all 96 captures,
  // queries, scene additions, and the seal. Keep the batch dimension explicit
  // in the status contract, but fixed at one so p95 cannot hide a slow frame
  // behind a multi-frame throughput average.
  for (uint32_t sample = 0; sample < kT10BudgetSampleCount; ++sample) {
    const auto started = std::chrono::steady_clock::now();
    for (uint32_t batch = 0; batch < kT10BudgetSampleBatchIterations;
         ++batch, ++iteration) {
      if (!T10BudgetIteration(iteration, result))
        return false;
    }
    const auto completed = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             completed - started)
                             .count();
    if (elapsed <= 0) {
      result.failure = 13;
      return false;
    }
    t10BudgetFixture.elapsedNs[sample] = static_cast<uint64_t>(elapsed);
  }

  if (!T10BudgetVerifyTelemetry(result)) {
    result.failure = 14;
    return false;
  }
  std::sort(t10BudgetFixture.elapsedNs.begin(),
            t10BudgetFixture.elapsedNs.end());
  result.p50Ns = T10BudgetPercentile(t10BudgetFixture.elapsedNs, 50u);
  result.p95Ns = T10BudgetPercentile(t10BudgetFixture.elapsedNs, 95u);
  result.p99Ns = T10BudgetPercentile(t10BudgetFixture.elapsedNs, 99u);
  if (result.p50Ns > result.p95Ns || result.p95Ns > result.p99Ns ||
      result.p95Ns > kT10BudgetP95LimitNs) {
    result.failure = 15;
    return false;
  }
  return true;
}

void T10BudgetPublish(const T10BudgetResult &result, bool passed) {
  if (!sg_worr_rewind_budget_selftest_status)
    return;
  char value[512]{};
  std::snprintf(
      value, sizeof(value),
      "%s:%u:%u:%u:%u:%u:%u:%u:%u:%u:%llu:%llu:%llu:%llu:%llu:%llu:%u:%u:%u:%u:%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu:%u",
      passed ? "pass" : "fail", kT10BudgetPlayerCount,
      kT10BudgetMoverCount, kHistoryCapacity, kMoverHistoryCapacity,
      kT10BudgetCandidateCount, kT10BudgetWarmupIterations,
      kT10BudgetSampleCount, kT10BudgetSampleBatchIterations,
      kT10BudgetCandidateCount,
      static_cast<unsigned long long>(kT10BudgetQueryAgeUs),
      static_cast<unsigned long long>(result.queryCount),
      static_cast<unsigned long long>(result.expectedQueryCount),
      static_cast<unsigned long long>(result.overwriteCount),
      static_cast<unsigned long long>(result.expectedOverwriteCount),
      static_cast<unsigned long long>(result.capacityOverflows),
      result.historiesFull ? 1u : 0u, result.sceneSealed ? 1u : 0u,
      result.authorityUnchanged ? 1u : 0u,
      result.allocationFree ? 1u : 0u,
      static_cast<unsigned long long>(kT10ProductionFixedStorageBytes),
      static_cast<unsigned long long>(kT10FixtureFixedStorageBytes),
      static_cast<unsigned long long>(kT10CombinedFixedStorageBytes),
      static_cast<unsigned long long>(kT10BudgetOwnerCapBytes),
      static_cast<unsigned long long>(result.p50Ns),
      static_cast<unsigned long long>(result.p95Ns),
      static_cast<unsigned long long>(result.p99Ns),
      static_cast<unsigned long long>(kT10BudgetP95LimitNs),
      static_cast<unsigned long long>(result.workloadHash), result.failure);
  gi.cvarForceSet("sg_worr_rewind_budget_selftest_status", value);
}

[[nodiscard]] bool RunT10BudgetRuntimeProbe() {
  T10BudgetResult result{};
  const uint64_t authorityBefore = T10BudgetAuthorityHash();
  bool passed = T10BudgetExecute(result);
  result.authorityUnchanged = authorityBefore == T10BudgetAuthorityHash();
  if (!result.authorityUnchanged) {
    if (result.failure == 0)
      result.failure = 16;
    passed = false;
  }
  passed = passed && result.capacityOverflows == 0 &&
           result.historiesFull && result.sceneSealed &&
           result.allocationFree &&
           kT10CombinedFixedStorageBytes <= kT10BudgetOwnerCapBytes &&
           result.p95Ns <= kT10BudgetP95LimitNs;
  T10BudgetPublish(result, passed);
  return passed;
}
