/* C++20 consumer/layout proof for the pointer-free native envelope ABI. */
#include "shared/native_envelope.h"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<worr_native_record_ref_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_record_ref_v1>);
static_assert(std::is_standard_layout_v<worr_native_envelope_frame_info_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_envelope_frame_info_v1>);
static_assert(std::is_standard_layout_v<worr_native_envelope_fragmenter_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_envelope_fragmenter_v1>);
static_assert(std::is_standard_layout_v<worr_native_envelope_reassembly_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_envelope_reassembly_v1>);
static_assert(std::is_standard_layout_v<worr_native_envelope_tx_queue_v1>);
static_assert(std::is_trivially_copyable_v<worr_native_envelope_tx_queue_v1>);
static_assert(sizeof(worr_native_envelope_frame_info_v1) == 56);
static_assert(sizeof(worr_native_envelope_fragmenter_v1) == 48);
static_assert(sizeof(worr_native_envelope_reassembly_v1) == 64);
static_assert(offsetof(worr_native_envelope_reassembly_v1, priority) == 46);
static_assert(offsetof(worr_native_envelope_reassembly_v1, received_bitmap) ==
              48);
static_assert(
    sizeof(((worr_native_envelope_reassembly_v1 *)nullptr)
               ->received_bitmap) == 16);
static_assert(sizeof(worr_native_envelope_tx_item_v1) == 48);
static_assert(sizeof(worr_native_envelope_tx_queue_v1) == 3096);
static_assert(offsetof(worr_native_envelope_frame_info_v1, payload_offset) ==
              52);
static_assert(offsetof(worr_native_envelope_tx_queue_v1, items) == 24);
static_assert(offsetof(worr_native_envelope_tx_item_v1, enqueue_serial) == 24);
static_assert(offsetof(worr_native_envelope_tx_item_v1, enqueue_dispatch) ==
              32);
static_assert(offsetof(worr_native_envelope_tx_item_v1, priority) == 40);

int main()
{
    worr_native_envelope_reassembly_v1 state{};

    Worr_NativeEnvelopeReassemblyResetV1(&state);
    return state.struct_size == sizeof(state) &&
                   state.schema_version == WORR_NATIVE_ENVELOPE_ABI_VERSION
               ? 0
               : 1;
}
