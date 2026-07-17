/* C11 consumer/layout proof for the pointer-free native envelope ABI. */
#include "shared/native_envelope.h"

#include <stddef.h>

_Static_assert(sizeof(worr_native_record_ref_v1) == 12,
               "C native record reference layout mismatch");
_Static_assert(sizeof(worr_native_envelope_frame_info_v1) == 56,
               "C native frame info layout mismatch");
_Static_assert(sizeof(worr_native_envelope_fragmenter_v1) == 48,
               "C native fragmenter layout mismatch");
_Static_assert(sizeof(worr_native_envelope_reassembly_v1) == 64,
               "C native reassembly layout mismatch");
_Static_assert(sizeof(worr_native_envelope_tx_item_v1) == 48,
               "C native queue item layout mismatch");
_Static_assert(sizeof(worr_native_envelope_tx_queue_v1) == 3096,
               "C native queue layout mismatch");
_Static_assert(offsetof(worr_native_envelope_frame_info_v1, record) == 8,
               "C native frame record offset mismatch");
_Static_assert(offsetof(worr_native_envelope_frame_info_v1, payload_offset) ==
                   52,
               "C native frame payload offset-field mismatch");
_Static_assert(offsetof(worr_native_envelope_reassembly_v1, priority) == 46,
               "C native reassembly priority offset mismatch");
_Static_assert(offsetof(worr_native_envelope_reassembly_v1, received_bitmap) ==
                   48,
               "C native reassembly bitmap offset mismatch");
_Static_assert(
    sizeof(((worr_native_envelope_reassembly_v1 *)0)->received_bitmap) == 16,
    "C native reassembly bitmap width mismatch");
_Static_assert(offsetof(worr_native_envelope_tx_queue_v1, items) == 24,
               "C native queue item offset mismatch");
_Static_assert(offsetof(worr_native_envelope_tx_item_v1, enqueue_serial) == 24,
               "C native enqueue serial offset mismatch");
_Static_assert(offsetof(worr_native_envelope_tx_item_v1, enqueue_dispatch) == 32,
               "C native enqueue dispatch offset mismatch");
_Static_assert(offsetof(worr_native_envelope_tx_item_v1, priority) == 40,
               "C native priority offset mismatch");

int main(void)
{
    worr_native_envelope_tx_queue_v1 queue;

    Worr_NativeEnvelopeTxQueueResetV1(&queue);
    return queue.struct_size == sizeof(queue) &&
                   queue.schema_version == WORR_NATIVE_ENVELOPE_ABI_VERSION
               ? 0
               : 1;
}
