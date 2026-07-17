#include "common/net/native_snapshot_admission.h"

#include <stddef.h>

_Static_assert(sizeof(worr_native_snapshot_admission_state_v1) == 144,
               "C admission state size changed");
_Static_assert(offsetof(worr_native_snapshot_admission_state_v1,
                        last_accepted_snapshot) == 24,
               "C admission identity offset changed");
_Static_assert(sizeof(worr_native_snapshot_expectation_v1) == 72,
               "C expectation size changed");
_Static_assert(offsetof(worr_native_snapshot_decode_storage_v1,
                        snapshot) == 8,
               "C decode pointer offset changed");
_Static_assert(offsetof(worr_native_snapshot_decode_storage_v1,
                        entity_capacity) == 48,
               "C decode capacity offset changed");
_Static_assert(offsetof(worr_native_snapshot_consumer_v1, opaque) == 8,
               "C consumer opaque offset changed");
_Static_assert(offsetof(worr_native_snapshot_consumer_v1, GetStatus) == 32,
               "C consumer status callback offset changed");

int main(void)
{
    return 0;
}

