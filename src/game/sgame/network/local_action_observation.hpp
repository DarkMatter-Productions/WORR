/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/command_context.h"
#include "shared/local_interaction_abi.h"
#include "shared/local_action_observation.h"

struct gentity_t;

/*
 * Captures live legacy weapon state around an authenticated canonical command.
 * This is intentionally observational: neither constructor nor destructor may
 * modify game state, enqueue a network message, or drive presentation.
 */
class SG_LocalActionObservationScope {
public:
  explicit SG_LocalActionObservationScope(gentity_t *entity);
  ~SG_LocalActionObservationScope();

  SG_LocalActionObservationScope(const SG_LocalActionObservationScope &) =
      delete;
  SG_LocalActionObservationScope &
  operator=(const SG_LocalActionObservationScope &) = delete;

private:
  gentity_t *entity_ = nullptr;
  worr_authoritative_command_context_v1 context_{};
  worr_local_action_observation_state_v1 state_before_{};
  bool active_ = false;
};

void SG_LocalActionObservationInitialize();
void SG_LocalActionObservationResetMap();
void SG_LocalActionObservationNoteWeaponThink(gentity_t *entity);

/*
 * Read-only bounded lookup for a future authoritative event bridge. It never
 * creates an event, changes game state, or exposes mutable ledger storage.
 */
bool SG_LocalInteractionObservationCopyForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_interaction_transaction_v1 *transaction_out);

/*
 * Read-only sparse projection for a future per-peer reliable authority
 * carrier. It succeeds only for an authoritative Hook request edge and never
 * allocates an event, chooses a recipient, writes a packet, or changes game
 * state. `receipt_out` is untouched on failure.
 */
bool SG_LocalInteractionObservationCopyAuthorityReceiptForCommand(
    uint32_t client_index, worr_command_id_v1 command_id,
    worr_local_interaction_authority_receipt_v1 *receipt_out);
