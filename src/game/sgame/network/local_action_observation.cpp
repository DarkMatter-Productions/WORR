/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "local_action_observation.hpp"

#include "../g_local.hpp"
#include "shared/command_context.h"
#include "shared/local_interaction_abi.h"
#include "shared/local_action_observation.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace {

constexpr uint32_t kHistoryCapacity = 32;

const worr_command_context_import_v1 *command_context_import;
const worr_local_interaction_authority_import_v1 *authority_import;
std::array<std::array<worr_local_action_observation_record_v1,
                      kHistoryCapacity>,
           WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS>
    records;
std::array<uint32_t, WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS> write_heads;
std::array<worr_local_interaction_state_v1,
           WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS>
    interaction_states;
std::array<std::array<worr_local_interaction_transaction_v1,
                      kHistoryCapacity>,
           WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS>
    interaction_records;
std::array<uint32_t, WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS>
    interaction_write_heads;

struct telemetry_t {
  uint64_t scoped_attempts = 0;
  uint64_t scoped_records = 0;
  uint64_t rejected_scope = 0;
  uint64_t rejected_state = 0;
  uint64_t scoped_weapon_thinks = 0;
  uint64_t unscoped_weapon_thinks = 0;
  uint64_t interaction_records = 0;
  uint64_t interaction_rebases = 0;
  uint64_t interaction_discontinuities = 0;
  uint64_t interaction_rejected = 0;
};

telemetry_t telemetry;

void saturating_increment(uint64_t &value)
{
  if (value != std::numeric_limits<uint64_t>::max())
    ++value;
}

uint64_t inventory_hash(const gclient_t *client)
{
  uint64_t hash = UINT64_C(1469598103934665603);

  for (const int32_t value : client->pers.inventory) {
    uint32_t bits = static_cast<uint32_t>(value);
    for (unsigned int index = 0; index != 4; ++index) {
      hash ^= static_cast<uint8_t>(bits & UINT32_C(0xff));
      hash *= UINT64_C(1099511628211);
      bits >>= 8;
    }
  }
  return hash;
}

int32_t remaining_ms(GameTime deadline)
{
  const int64_t value = (deadline - level.time).milliseconds();
  constexpr int64_t maximum = WORR_LOCAL_ACTION_OBSERVATION_MAX_TIMER_MS;
  if (value < -maximum)
    return static_cast<int32_t>(-maximum);
  if (value > maximum)
    return static_cast<int32_t>(maximum);
  return static_cast<int32_t>(value);
}

uint32_t observation_phase(const gclient_t *client)
{
  if (!client->pers.weapon)
    return WORR_LOCAL_ACTION_OBSERVATION_HOLSTERED;

  switch (client->weaponState) {
  case WeaponState::Activating:
    return WORR_LOCAL_ACTION_OBSERVATION_RAISING;
  case WeaponState::Ready:
    return WORR_LOCAL_ACTION_OBSERVATION_READY;
  case WeaponState::Firing:
    return WORR_LOCAL_ACTION_OBSERVATION_FIRING;
  case WeaponState::Dropping:
    return WORR_LOCAL_ACTION_OBSERVATION_LOWERING;
  }

  return WORR_LOCAL_ACTION_OBSERVATION_HOLSTERED;
}

bool capture_state(gentity_t *entity,
                   worr_local_action_observation_state_v1 *state_out)
{
  gclient_t *client;
  const Item *active;
  const Item *pending;
  worr_local_action_observation_state_v1 state{};

  if (!entity || !entity->client || !state_out)
    return false;
  client = entity->client;
  active = client->pers.weapon;
  pending = client->weapon.pending;
  if (client->ps.gunFrame < 0 || client->ps.gunRate < 0 ||
      client->ps.gunRate > 1000) {
    return false;
  }

  state.struct_size = sizeof(state);
  state.schema_version = WORR_LOCAL_ACTION_OBSERVATION_ABI_VERSION;
  state.phase = observation_phase(client);
  state.inventory_hash = inventory_hash(client);
  state.active_weapon_id = active ? static_cast<uint32_t>(active->id) : 0;
  state.pending_weapon_id = pending ? static_cast<uint32_t>(pending->id) : 0;
  if (active && active->ammo != IT_NULL && active->ammo < IT_TOTAL) {
    state.active_ammo_item_id = static_cast<uint32_t>(active->ammo);
    state.active_ammo_units = client->pers.inventory[active->ammo];
  }
  state.presentation_frame = static_cast<uint32_t>(client->ps.gunFrame);
  state.presentation_rate = static_cast<uint32_t>(client->ps.gunRate);
  state.think_remaining_ms = remaining_ms(client->weapon.thinkTime);
  state.fire_remaining_ms = remaining_ms(client->weapon.fireFinished);
  if (client->buttons & BUTTON_ATTACK)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_ATTACK_HELD;
  if (client->latchedButtons & BUTTON_ATTACK)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_ATTACK_LATCHED;
  if (client->weapon.fireBuffered)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_FIRE_BUFFERED;
  if (client->weapon.thunk)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_WEAPON_THUNK;
  if (client->buttons & BUTTON_HOOK)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_HELD;
  if (client->grapple.state != GrappleState::None)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_ACTIVE;
  if (entity->health > 0)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ALIVE;
  if (ClientIsPlaying(client) && !client->eliminated)
    state.flags |= WORR_LOCAL_ACTION_OBSERVATION_PLAYER_ELIGIBLE;

  if (!Worr_LocalActionObservationStateValidateV1(&state))
    return false;
  *state_out = state;
  return true;
}

bool valid_import(const worr_command_context_import_v1 *candidate)
{
  return candidate && candidate->struct_size == sizeof(*candidate) &&
         candidate->api_version == WORR_COMMAND_CONTEXT_API_VERSION &&
         candidate->GetCurrent && candidate->GetScopeState;
}

bool valid_authority_import(
    const worr_local_interaction_authority_import_v1 *candidate)
{
  return candidate && candidate->struct_size == sizeof(*candidate) &&
         candidate->api_version == WORR_LOCAL_INTERACTION_AUTHORITY_API_VERSION &&
         candidate->PublishReceipt;
}

bool current_context_for_entity(gentity_t *entity,
                                worr_authoritative_command_context_v1 *out)
{
  ptrdiff_t entity_index;

  if (!entity || !out || !valid_import(command_context_import) ||
      command_context_import->GetScopeState() !=
          WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID ||
      !command_context_import->GetCurrent(out) || !g_entities) {
    return false;
  }
  entity_index = entity - g_entities;
  return entity_index > 0 &&
         static_cast<uint32_t>(entity_index - 1) == out->client_index &&
         out->client_index < WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS;
}

void append_record(const worr_local_action_observation_record_v1 &record)
{
  uint32_t &head = write_heads[record.client_index];
  records[record.client_index][head % kHistoryCapacity] = record;
  if (head != std::numeric_limits<uint32_t>::max())
    ++head;
}

bool hook_held(const worr_local_action_observation_state_v1 &state)
{
  return (state.flags &
          WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_HELD) != 0;
}

bool hook_active(const worr_local_action_observation_state_v1 &state)
{
  return (state.flags &
          WORR_LOCAL_ACTION_OBSERVATION_OFFHAND_HOOK_ACTIVE) != 0;
}

bool interaction_state_matches_observation(
    const worr_local_interaction_state_v1 &interaction,
    const worr_local_action_observation_state_v1 &observation)
{
  const bool interaction_held =
      (interaction.flags & WORR_LOCAL_INTERACTION_STATE_HOOK_HELD) != 0;
  const bool interaction_active =
      (interaction.flags & WORR_LOCAL_INTERACTION_STATE_HOOK_ACTIVE) != 0;
  return interaction_held == hook_held(observation) &&
         interaction_active == hook_active(observation);
}

bool interaction_cursor_accepts(
    const worr_local_interaction_state_v1 &interaction,
    const worr_command_record_v1 &command)
{
  worr_command_id_v1 next{};

  return Worr_LocalInteractionStateValidateV1(&interaction) &&
         Worr_CommandCursorNextIdV1(interaction.applied_cursor, &next) &&
         next.epoch == command.command_id.epoch &&
         next.sequence == command.command_id.sequence;
}

bool rebase_interaction_before_command(
    uint32_t client_index, const worr_command_record_v1 &command,
    const worr_local_action_observation_state_v1 &state_before)
{
  worr_local_interaction_state_v1 rebased{};

  if (!Worr_LocalInteractionRebaseBeforeCommandV1(
          &command, hook_held(state_before), hook_active(state_before),
          &rebased)) {
    return false;
  }
  interaction_states[client_index] = rebased;
  saturating_increment(telemetry.interaction_rebases);
  return true;
}

void append_interaction_record(
    uint32_t client_index,
    const worr_local_interaction_transaction_v1 &transaction)
{
  uint32_t &head = interaction_write_heads[client_index];

  interaction_records[client_index][head % kHistoryCapacity] = transaction;
  if (head != std::numeric_limits<uint32_t>::max())
    ++head;
}

bool observe_interaction(
    const worr_authoritative_command_context_v1 &context,
    const worr_local_action_observation_state_v1 &state_before,
    const worr_local_action_observation_state_v1 &state_after)
{
  const uint32_t client_index = context.client_index;
  worr_local_interaction_state_v1 &interaction =
      interaction_states[client_index];
  worr_local_interaction_intent_v1 intent{};
  worr_local_interaction_transaction_v1 transaction{};
  const bool discontinuity =
      !interaction_cursor_accepts(interaction, context.command) ||
      !interaction_state_matches_observation(interaction, state_before);

  if (discontinuity) {
    if (!rebase_interaction_before_command(client_index, context.command,
                                           state_before)) {
      return false;
    }
    saturating_increment(telemetry.interaction_discontinuities);
  }

  intent.struct_size = sizeof(intent);
  intent.schema_version = WORR_LOCAL_INTERACTION_ABI_VERSION;
  if ((context.command.command.buttons &
       WORR_LOCAL_INTERACTION_HOOK_BUTTON) != 0) {
    intent.flags |= WORR_LOCAL_INTERACTION_INTENT_HOOK_HELD;
  }
  if (!Worr_LocalInteractionBuildAuthoritativeHookV1(
          &interaction, &context.command, &intent, hook_active(state_after),
          &transaction)) {
    return false;
  }

  interaction = transaction.state_after;
  append_interaction_record(client_index, transaction);
  if (authority_import) {
    worr_local_interaction_authority_receipt_v1 receipt{};
    if (Worr_LocalInteractionAuthorityReceiptBuildV1(&transaction, &receipt))
      (void)authority_import->PublishReceipt(client_index, &receipt);
  }
  saturating_increment(telemetry.interaction_records);
  return true;
}

} // namespace

void SG_LocalActionObservationInitialize()
{
  command_context_import = nullptr;
  authority_import = nullptr;
  if (gi.GetExtension) {
    const auto *candidate =
        static_cast<const worr_command_context_import_v1 *>(
            gi.GetExtension(WORR_COMMAND_CONTEXT_IMPORT_V1));
    if (valid_import(candidate))
      command_context_import = candidate;
    const auto *authority_candidate =
        static_cast<const worr_local_interaction_authority_import_v1 *>(
            gi.GetExtension(WORR_LOCAL_INTERACTION_AUTHORITY_IMPORT_V1));
    if (valid_authority_import(authority_candidate))
      authority_import = authority_candidate;
  }
  SG_LocalActionObservationResetMap();
}

void SG_LocalActionObservationResetMap()
{
  std::memset(records.data(), 0, sizeof(records));
  std::memset(write_heads.data(), 0, sizeof(write_heads));
  std::memset(interaction_states.data(), 0, sizeof(interaction_states));
  std::memset(interaction_records.data(), 0, sizeof(interaction_records));
  std::memset(interaction_write_heads.data(), 0,
              sizeof(interaction_write_heads));
  telemetry = {};
}

SG_LocalActionObservationScope::SG_LocalActionObservationScope(
    gentity_t *entity)
{
  saturating_increment(telemetry.scoped_attempts);
  if (!current_context_for_entity(entity, &context_)) {
    saturating_increment(telemetry.rejected_scope);
    return;
  }
  if (!capture_state(entity, &state_before_)) {
    saturating_increment(telemetry.rejected_state);
    return;
  }
  entity_ = entity;
  active_ = true;
}

SG_LocalActionObservationScope::~SG_LocalActionObservationScope()
{
  worr_local_action_observation_state_v1 state_after{};
  worr_local_action_observation_record_v1 record{};

  if (!active_)
    return;
  if (!capture_state(entity_, &state_after) ||
      !Worr_LocalActionObservationBuildV1(
          context_.client_index, &context_.command, &state_before_,
          &state_after, &record)) {
    saturating_increment(telemetry.rejected_state);
  } else {
    append_record(record);
    saturating_increment(telemetry.scoped_records);
    if (!observe_interaction(context_, state_before_, state_after))
      saturating_increment(telemetry.interaction_rejected);
  }
}

void SG_LocalActionObservationNoteWeaponThink(gentity_t *entity)
{
  worr_authoritative_command_context_v1 context{};

  if (current_context_for_entity(entity, &context))
    saturating_increment(telemetry.scoped_weapon_thinks);
  else
    saturating_increment(telemetry.unscoped_weapon_thinks);
}

bool SG_LocalInteractionObservationCopyForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_interaction_transaction_v1 *transaction_out)
{
  const uint32_t head =
      client_index < WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS
          ? interaction_write_heads[client_index]
          : 0;
  const uint32_t count = std::min(head, kHistoryCapacity);

  if (!transaction_out || client_index >=
                             WORR_LOCAL_ACTION_OBSERVATION_MAX_CLIENTS ||
      !Worr_CommandIdValidV1(command_id, false)) {
    return false;
  }

  for (uint32_t offset = 1; offset <= count; ++offset) {
    const auto &candidate =
        interaction_records[client_index][(head - offset) % kHistoryCapacity];
    if (candidate.producer != WORR_LOCAL_INTERACTION_PRODUCER_AUTHORITATIVE ||
        candidate.command.command_id.epoch != command_id.epoch ||
        candidate.command.command_id.sequence != command_id.sequence ||
        !Worr_LocalInteractionTransactionValidateV1(&candidate)) {
      continue;
    }
    *transaction_out = candidate;
    return true;
  }
  return false;
}

bool SG_LocalInteractionObservationCopyAuthorityReceiptForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_interaction_authority_receipt_v1 *receipt_out)
{
  worr_local_interaction_transaction_v1 transaction{};
  worr_local_interaction_authority_receipt_v1 receipt{};

  if (!receipt_out ||
      !SG_LocalInteractionObservationCopyForCommand(
          client_index, command_id, &transaction) ||
      !Worr_LocalInteractionAuthorityReceiptBuildV1(&transaction,
                                                     &receipt)) {
    return false;
  }
  *receipt_out = receipt;
  return true;
}
