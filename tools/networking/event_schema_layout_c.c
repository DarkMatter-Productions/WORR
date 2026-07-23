#include "common/net/event_journal.h"
#include "shared/cgame_event_shadow.h"
#include "shared/event_shadow.h"

#include <stddef.h>

int main(void)
{
    return sizeof(worr_event_record_v1) == 168 &&
                   offsetof(worr_event_record_v1, payload) == 88 &&
                   sizeof(worr_event_payload_legacy_entity_v1) == 8 &&
                   sizeof(worr_event_payload_legacy_temp_v1) == 72 &&
                   offsetof(worr_event_payload_legacy_temp_v1, position1) ==
                       20 &&
                   sizeof(worr_event_payload_muzzle_v1) == 8 &&
                   sizeof(worr_event_payload_spatial_audio_v1) == 40 &&
                   sizeof(worr_event_payload_keyed_poi_v1) == 20 &&
                   _Alignof(worr_event_payload_keyed_poi_v1) == 4 &&
                   offsetof(worr_event_payload_keyed_poi_v1, key) == 0 &&
                   offsetof(worr_event_payload_keyed_poi_v1, lifetime_ms) ==
                       2 &&
                   offsetof(worr_event_payload_keyed_poi_v1, position) == 4 &&
                   offsetof(worr_event_payload_keyed_poi_v1, image_index) ==
                       16 &&
                   offsetof(worr_event_payload_keyed_poi_v1, color_index) ==
                       18 &&
                   offsetof(worr_event_payload_keyed_poi_v1, flags) == 19 &&
                   WORR_EVENT_PAYLOAD_LEGACY_ENTITY_V1 == 7 &&
                   WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1 == 8 &&
                   WORR_EVENT_PAYLOAD_MUZZLE_V1 == 9 &&
                   WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1 == 10 &&
                   WORR_EVENT_PAYLOAD_LOCAL_INTERACTION_AUTHORITY_V1 == 11 &&
                   WORR_EVENT_PAYLOAD_LOCAL_ACTION_SHADOW_AUTHORITY_V1 == 12 &&
                   WORR_EVENT_PAYLOAD_KEYED_POI_V1 == 13 &&
                   WORR_EVENT_MODEL_REVISION == 2 &&
                   sizeof(worr_local_action_shadow_authority_receipt_v1) ==
                       64 &&
                   WORR_EVENT_LEGACY_TEMP_BOSSTPORT == 22 &&
                   WORR_EVENT_LEGACY_TEMP_BLUEHYPERBLASTER_BROKEN == 27 &&
                   WORR_EVENT_LEGACY_TEMP_FLAME_RESERVED == 32 &&
                   WORR_EVENT_LEGACY_TEMP_BLUEHYPERBLASTER_FIXED == 56 &&
                   WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT == 128 &&
                   WORR_EVENT_MONSTER_MUZZLE_LAST == 293 &&
                   sizeof(worr_event_receipt_ack_v1) == 24 &&
                   sizeof(worr_event_journal_slot_v1) == 184 &&
                   sizeof(worr_event_shadow_source_state_v1) == 24 &&
                   sizeof(worr_event_shadow_legacy_input_v1) == 40 &&
                   sizeof(worr_event_shadow_status_v1) == 120 &&
                   sizeof(worr_cgame_event_shadow_carrier_v1) == 12 &&
                   sizeof(worr_cgame_event_shadow_observed_v1) == 12 &&
                   sizeof(worr_cgame_event_shadow_audit_status_v1) == 72 &&
                   sizeof(worr_cgame_event_action_candidate_v2) == 184 &&
                   sizeof(worr_cgame_event_carrier_v2) == 12 &&
                   sizeof(worr_cgame_event_observed_v2) == 12 &&
                   sizeof(worr_cgame_event_range_audit_status_v2) == 224
               ? 0
               : 1;
}
