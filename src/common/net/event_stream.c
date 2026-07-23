/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/event_stream.h"

#include <string.h>

bool Worr_EventStreamDescriptorInitV1(
    worr_event_stream_descriptor_v1 *descriptor_out,
    uint32_t stream_epoch,
    uint32_t first_sequence)
{
    worr_event_stream_descriptor_v1 descriptor;

    if (descriptor_out == NULL || stream_epoch == 0 ||
        first_sequence == 0) {
        return false;
    }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.struct_size = sizeof(descriptor);
    descriptor.schema_version = WORR_EVENT_STREAM_ABI_VERSION;
    descriptor.stream_epoch = stream_epoch;
    descriptor.first_sequence = first_sequence;
    descriptor.event_schema_version = WORR_EVENT_ABI_VERSION;
    descriptor.model_revision = WORR_EVENT_MODEL_REVISION;
    *descriptor_out = descriptor;
    return true;
}

bool Worr_EventStreamDescriptorValidateV1(
    const worr_event_stream_descriptor_v1 *descriptor)
{
    const uint16_t known_flags =
        WORR_EVENT_STREAM_FLAG_BATCH_SCHEMA2;

    return descriptor != NULL &&
           descriptor->struct_size == sizeof(*descriptor) &&
           descriptor->schema_version == WORR_EVENT_STREAM_ABI_VERSION &&
           (descriptor->flags & ~known_flags) == 0 &&
           descriptor->stream_epoch != 0 &&
           descriptor->first_sequence != 0 &&
           descriptor->event_schema_version == WORR_EVENT_ABI_VERSION &&
           descriptor->model_revision == WORR_EVENT_MODEL_REVISION;
}

bool Worr_EventStreamDescriptorEqualV1(
    const worr_event_stream_descriptor_v1 *left,
    const worr_event_stream_descriptor_v1 *right)
{
    return Worr_EventStreamDescriptorValidateV1(left) &&
           Worr_EventStreamDescriptorValidateV1(right) &&
           left->stream_epoch == right->stream_epoch &&
           left->first_sequence == right->first_sequence &&
           left->flags == right->flags &&
           left->event_schema_version == right->event_schema_version &&
           left->model_revision == right->model_revision;
}
