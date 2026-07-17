/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/capability.h"
#include "common/net/native_readiness.h"
#include "common/net/native_readiness_sideband.h"
#include "shared/shared.h"
#include "common/protocol.h"
#include "q2proto/q2proto.h"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

constexpr size_t kSvcSettingWireBytes = 9u;
constexpr size_t kSvcSettingValueOffset = 5u;
constexpr size_t kClcSettingWireBytes = 5u;
constexpr size_t kClcSettingValueOffset = 3u;
constexpr size_t kSvcReadinessRecordWireBytes =
    WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT * kSvcSettingWireBytes;
constexpr size_t kClcReadinessRecordWireBytes =
    WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT * kClcSettingWireBytes;

constexpr size_t readiness_pair_index(int setting)
{
    return static_cast<size_t>(
        setting - WORR_NATIVE_READINESS_SETTING_BEGIN);
}

struct memory_io_t {
    std::array<uint8_t, 4096> data{};
    std::array<uint8_t, 4096> overflow{};
    size_t read_pos{};
    size_t write_pos{};
    bool failed{};
    q2proto_error_t codec_error{Q2P_ERR_SUCCESS};

    void clear()
    {
        data.fill(0);
        read_pos = 0;
        write_pos = 0;
        failed = false;
        codec_error = Q2P_ERR_SUCCESS;
    }
};

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(stderr, "native_readiness_q2proto_wire_test:%d: %s\n",
                 line, expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            fail(#expression, __LINE__);                                      \
    } while (0)

memory_io_t &io_from(uintptr_t argument)
{
    return *reinterpret_cast<memory_io_t *>(argument);
}

const uint8_t *read_exact(memory_io_t &io, size_t size)
{
    if (size > io.write_pos - io.read_pos) {
        io.failed = true;
        return nullptr;
    }
    const uint8_t *result = io.data.data() + io.read_pos;
    io.read_pos += size;
    return result;
}

void *reserve_exact(memory_io_t &io, size_t size)
{
    if (size > io.data.size() - io.write_pos) {
        io.failed = true;
        return io.overflow.data();
    }
    void *result = io.data.data() + io.write_pos;
    io.write_pos += size;
    return result;
}

template <typename T>
T read_little(uintptr_t argument)
{
    const uint8_t *bytes = read_exact(io_from(argument), sizeof(T));
    if (!bytes)
        return std::numeric_limits<T>::max();
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(T); ++index)
        value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
    return static_cast<T>(value);
}

template <typename T>
void write_little(uintptr_t argument, T value)
{
    auto *bytes = static_cast<uint8_t *>(
        reserve_exact(io_from(argument), sizeof(T)));
    for (size_t index = 0; index < sizeof(T); ++index)
        bytes[index] = static_cast<uint8_t>(value >> (index * 8));
}

struct protocol_fixture_t {
    q2proto_server_info_t info{};
    q2proto_connect_t connect{};
    q2proto_servercontext_t server{};
    q2proto_clientcontext_t client{};
    memory_io_t io{};
};

void initialize_protocol(protocol_fixture_t &fixture)
{
    fixture = {};
    fixture.info.game_api = Q2PROTO_GAME_RERELEASE;
    fixture.info.default_packet_length = 1400;
    fixture.connect.protocol = Q2P_PROTOCOL_Q2REPRO;
    fixture.connect.qport = 27901;
    fixture.connect.challenge = 31415;
    fixture.connect.userinfo = q2proto_make_string("\\name\\readiness-wire");
    fixture.connect.packet_length = 1400;
    CHECK(q2proto_complete_connect(&fixture.connect) == Q2P_ERR_SUCCESS);
    CHECK(q2proto_init_servercontext(&fixture.server, &fixture.info,
                                     &fixture.connect) == Q2P_ERR_SUCCESS);
    CHECK(q2proto_init_clientcontext(&fixture.client) == Q2P_ERR_SUCCESS);

    q2proto_svc_message_t serverdata{};
    serverdata.type = Q2P_SVC_SERVERDATA;
    CHECK(q2proto_server_fill_serverdata(&fixture.server,
                                         &serverdata.serverdata) ==
          Q2P_ERR_SUCCESS);
    serverdata.serverdata.servercount = 1;
    serverdata.serverdata.gamedir = q2proto_make_string("basew");
    serverdata.serverdata.clientnum = 0;
    serverdata.serverdata.levelname = q2proto_make_string("readiness wire");
    serverdata.serverdata.q2pro.server_state = 2;
    serverdata.serverdata.q2repro.server_fps = 40;
    CHECK(q2proto_server_write(
              &fixture.server, reinterpret_cast<uintptr_t>(&fixture.io),
              &serverdata) == Q2P_ERR_SUCCESS);
    q2proto_svc_message_t decoded{};
    CHECK(q2proto_client_read(
              &fixture.client, reinterpret_cast<uintptr_t>(&fixture.io),
              &decoded) == Q2P_ERR_SUCCESS);
    CHECK(decoded.type == Q2P_SVC_SERVERDATA && !fixture.io.failed);
    fixture.io.clear();
}

void write_svc_record(
    memory_io_t &io, const worr_native_readiness_record_v1 &record)
{
    std::array<worr_native_readiness_setting_pair_v1,
               WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT> pairs{};
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs.data(), static_cast<uint32_t>(pairs.size())));
    for (const auto &pair : pairs) {
        write_little<uint8_t>(reinterpret_cast<uintptr_t>(&io),
                              static_cast<uint8_t>(svc_rr_setting));
        write_little<uint32_t>(reinterpret_cast<uintptr_t>(&io),
                               static_cast<uint32_t>(
                                   static_cast<int32_t>(pair.index)));
        write_little<uint32_t>(reinterpret_cast<uintptr_t>(&io),
                               static_cast<uint32_t>(
                                   static_cast<int32_t>(pair.value)));
    }
    CHECK(io.write_pos == kSvcReadinessRecordWireBytes);
    constexpr std::array<uint8_t, kSvcSettingWireBytes> begin_wire{
        37u, 0x14u, 0x83u, 0xffu, 0xffu,
        0x01u, 0x00u, 0x00u, 0x00u};
    CHECK(std::memcmp(io.data.data(), begin_wire.data(),
                      begin_wire.size()) == 0);
}

worr_native_readiness_record_v1 read_svc_record(protocol_fixture_t &fixture)
{
    worr_native_readiness_sideband_parser_v1 parser{};
    CHECK(Worr_NativeReadinessSidebandParserInitV1(&parser));
    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED);
    for (uint32_t index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT; ++index) {
        q2proto_svc_message_t message{};
        CHECK(q2proto_client_read(
                  &fixture.client,
                  reinterpret_cast<uintptr_t>(&fixture.io), &message) ==
              Q2P_ERR_SUCCESS);
        CHECK(message.type == Q2P_SVC_SETTING);
        const auto result = Worr_NativeReadinessSidebandObserveSvcSettingV1(
            &parser, message.setting.index, message.setting.value);
        CHECK(result ==
                  WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED ||
              result ==
                  WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED);
    }
    worr_native_readiness_record_v1 record{};
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(&parser, &record) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED);
    CHECK(fixture.io.read_pos == fixture.io.write_pos && !fixture.io.failed);
    return record;
}

void write_clc_record(protocol_fixture_t &fixture,
                      const worr_native_readiness_record_v1 &record)
{
    std::array<worr_native_readiness_setting_pair_v1,
               WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT> pairs{};
    CHECK(Worr_NativeReadinessSidebandEncodeV1(
        &record, pairs.data(), static_cast<uint32_t>(pairs.size())));
    q2proto_clc_message_t message{.type = Q2P_CLC_SETTING};
    for (const auto &pair : pairs) {
        message.setting.index = pair.index;
        message.setting.value = pair.value;
        CHECK(q2proto_client_write(
                  &fixture.client,
                  reinterpret_cast<uintptr_t>(&fixture.io), &message) ==
              Q2P_ERR_SUCCESS);
    }
    CHECK(fixture.io.write_pos == kClcReadinessRecordWireBytes);
    constexpr std::array<uint8_t, kClcSettingWireBytes> begin_wire{
        5u, 0x14u, 0x83u, 0x01u, 0x00u};
    CHECK(std::memcmp(fixture.io.data.data(), begin_wire.data(),
                      begin_wire.size()) == 0);
    constexpr std::array<uint8_t, 2> nonce_word2_wire{0x98u, 0xbau};
    CHECK(std::memcmp(
              fixture.io.data.data() +
                  readiness_pair_index(
                      WORR_NATIVE_READINESS_SETTING_NONCE_WORD2) *
                      kClcSettingWireBytes +
                  kClcSettingValueOffset,
                      nonce_word2_wire.data(),
                      nonce_word2_wire.size()) == 0);
}

worr_native_readiness_record_v1 read_clc_record(protocol_fixture_t &fixture)
{
    worr_native_readiness_sideband_parser_v1 parser{};
    CHECK(Worr_NativeReadinessSidebandParserInitV1(&parser));
    CHECK(Worr_NativeReadinessSidebandPacketBeginV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_STARTED);
    for (uint32_t index = 0;
         index < WORR_NATIVE_READINESS_SIDEBAND_PAIR_COUNT; ++index) {
        q2proto_clc_message_t message{};
        CHECK(q2proto_server_read(
                  &fixture.server,
                  reinterpret_cast<uintptr_t>(&fixture.io), &message) ==
              Q2P_ERR_SUCCESS);
        CHECK(message.type == Q2P_CLC_SETTING);
        const auto result = Worr_NativeReadinessSidebandObservePairV1(
            &parser, message.setting.index, message.setting.value);
        CHECK(result ==
                  WORR_NATIVE_READINESS_SIDEBAND_FIELD_ACCEPTED ||
              result ==
                  WORR_NATIVE_READINESS_SIDEBAND_RECORD_COMMITTED);
    }
    worr_native_readiness_record_v1 record{};
    CHECK(Worr_NativeReadinessSidebandTakeRecordV1(&parser, &record) ==
          WORR_NATIVE_READINESS_SIDEBAND_RECORD_TAKEN);
    CHECK(Worr_NativeReadinessSidebandPacketEndV1(&parser) ==
          WORR_NATIVE_READINESS_SIDEBAND_PACKET_ENDED);
    CHECK(fixture.io.read_pos == fixture.io.write_pos && !fixture.io.failed);
    return record;
}

bool records_equal(const worr_native_readiness_record_v1 &left,
                   const worr_native_readiness_record_v1 &right)
{
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}

void run_handshake()
{
    protocol_fixture_t fixture;
    initialize_protocol(fixture);

    constexpr uint32_t epoch = UINT32_C(0x89abcdef);
    constexpr uint64_t nonce = UINT64_C(0xfedcba9876543210);
    constexpr uint64_t timeout = 5000;
    constexpr uint32_t capabilities =
        WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK;
    worr_native_readiness_state_v1 server{};
    worr_native_readiness_state_v1 client{};
    worr_native_readiness_record_v1 challenge{};
    worr_native_readiness_record_v1 ready{};
    worr_native_readiness_record_v1 active{};

    CHECK(Worr_NativeReadinessServerInitV1(
              &server, epoch, capabilities, nonce, 100, timeout,
              &challenge) == WORR_NATIVE_READINESS_OK);
    CHECK(challenge.snapshot_epoch == 0);
    CHECK(challenge.record_checksum == UINT32_C(0x5d6c0c0a));
    CHECK(Worr_NativeReadinessClientInitV1(
              &client, epoch, capabilities, 101, timeout) ==
          WORR_NATIVE_READINESS_OK);

    write_svc_record(fixture.io, challenge);
    constexpr std::array<uint8_t, 4> challenge_checksum_low_wire{
        0x0au, 0x0cu, 0x00u, 0x00u};
    CHECK(std::memcmp(
              fixture.io.data.data() +
                  readiness_pair_index(
                      WORR_NATIVE_READINESS_SETTING_CHECKSUM_LOW) *
                      kSvcSettingWireBytes +
                  kSvcSettingValueOffset,
              challenge_checksum_low_wire.data(),
              challenge_checksum_low_wire.size()) == 0);
    const auto wire_challenge = read_svc_record(fixture);
    CHECK(records_equal(challenge, wire_challenge));
    CHECK(Worr_NativeReadinessClientObserveChallengeV1(
              &client, &wire_challenge, 102, &ready) ==
          WORR_NATIVE_READINESS_OK);

    fixture.io.clear();
    write_clc_record(fixture, ready);
    const auto wire_ready = read_clc_record(fixture);
    CHECK(records_equal(ready, wire_ready));
    CHECK(Worr_NativeReadinessServerObserveClientReadyV1(
              &server, &wire_ready, 103, &active) ==
          WORR_NATIVE_READINESS_OK);

    fixture.io.clear();
    write_svc_record(fixture.io, active);
    const auto wire_active = read_svc_record(fixture);
    CHECK(records_equal(active, wire_active));
    CHECK(Worr_NativeReadinessClientObserveServerActiveV1(
              &client, &wire_active, 104) == WORR_NATIVE_READINESS_OK);
    CHECK(server.phase == WORR_NATIVE_READINESS_PHASE_SERVER_ACTIVE);
    CHECK(client.phase == WORR_NATIVE_READINESS_PHASE_CLIENT_ACTIVE);
    CHECK(Worr_NativeReadinessCanReceiveNativeV1(&server, 105));
    CHECK(Worr_NativeReadinessCanTransmitNativeV1(&server, 105));
    CHECK(Worr_NativeReadinessCanReceiveNativeV1(&client, 105));
    CHECK(Worr_NativeReadinessCanTransmitNativeV1(&client, 105));
}

} // namespace

extern "C" uint8_t q2protoio_read_u8(uintptr_t argument)
{
    return read_little<uint8_t>(argument);
}
extern "C" uint16_t q2protoio_read_u16(uintptr_t argument)
{
    return read_little<uint16_t>(argument);
}
extern "C" uint32_t q2protoio_read_u32(uintptr_t argument)
{
    return read_little<uint32_t>(argument);
}
extern "C" uint64_t q2protoio_read_u64(uintptr_t argument)
{
    return read_little<uint64_t>(argument);
}
extern "C" q2proto_string_t q2protoio_read_string(uintptr_t argument)
{
    auto &io = io_from(argument);
    q2proto_string_t result{};
    result.str = reinterpret_cast<const char *>(io.data.data() + io.read_pos);
    while (io.read_pos < io.write_pos) {
        if (io.data[io.read_pos++] == 0)
            return result;
        ++result.len;
    }
    io.failed = true;
    return {};
}
extern "C" const void *q2protoio_read_raw(uintptr_t argument, size_t size,
                                           size_t *readcount)
{
    auto &io = io_from(argument);
    if (readcount) {
        const size_t available = io.write_pos - io.read_pos;
        const size_t amount = size < available ? size : available;
        const void *result = io.data.data() + io.read_pos;
        io.read_pos += amount;
        *readcount = amount;
        return result;
    }
    return read_exact(io, size);
}
extern "C" size_t q2protoio_read_available(uintptr_t argument)
{
    const auto &io = io_from(argument);
    return io.write_pos - io.read_pos;
}
extern "C" void q2protoio_write_u8(uintptr_t argument, uint8_t value)
{
    write_little(argument, value);
}
extern "C" void q2protoio_write_u16(uintptr_t argument, uint16_t value)
{
    write_little(argument, value);
}
extern "C" void q2protoio_write_u32(uintptr_t argument, uint32_t value)
{
    write_little(argument, value);
}
extern "C" void q2protoio_write_u64(uintptr_t argument, uint64_t value)
{
    write_little(argument, value);
}
extern "C" void *q2protoio_write_reserve_raw(uintptr_t argument, size_t size)
{
    return reserve_exact(io_from(argument), size);
}
extern "C" void q2protoio_write_raw(uintptr_t argument, const void *data,
                                     size_t size, size_t *written)
{
    auto &io = io_from(argument);
    const size_t available = io.data.size() - io.write_pos;
    const size_t amount = written && size > available ? available : size;
    if (!written && size > available) {
        io.failed = true;
        return;
    }
    std::memcpy(reserve_exact(io, amount), data, amount);
    if (written)
        *written = amount;
}
extern "C" size_t q2protoio_write_available(uintptr_t argument)
{
    const auto &io = io_from(argument);
    return io.data.size() - io.write_pos;
}
extern "C" q2proto_error_t q2protoerr_client_read(
    uintptr_t argument, q2proto_error_t error, const char *, ...)
{
    io_from(argument).codec_error = error;
    return error;
}
extern "C" q2proto_error_t q2protoerr_client_write(
    uintptr_t argument, q2proto_error_t error, const char *, ...)
{
    io_from(argument).codec_error = error;
    return error;
}
extern "C" q2proto_error_t q2protoerr_server_write(
    uintptr_t argument, q2proto_error_t error, const char *, ...)
{
    io_from(argument).codec_error = error;
    return error;
}
extern "C" q2proto_error_t q2protoerr_server_read(
    uintptr_t argument, q2proto_error_t error, const char *, ...)
{
    io_from(argument).codec_error = error;
    return error;
}

#if Q2PROTO_COMPRESSION_DEFLATE
extern "C" q2proto_error_t q2protoio_inflate_begin(
    uintptr_t argument, q2proto_inflate_deflate_header_mode_t,
    uintptr_t *inflate_argument)
{
    *inflate_argument = argument;
    return Q2P_ERR_NOT_IMPLEMENTED;
}
extern "C" q2proto_error_t q2protoio_inflate_data(uintptr_t, uintptr_t,
                                                   size_t)
{
    return Q2P_ERR_NOT_IMPLEMENTED;
}
extern "C" q2proto_error_t q2protoio_inflate_stream_ended(uintptr_t,
                                                           bool *ended)
{
    *ended = false;
    return Q2P_ERR_NOT_IMPLEMENTED;
}
extern "C" q2proto_error_t q2protoio_inflate_end(uintptr_t)
{
    return Q2P_ERR_NOT_IMPLEMENTED;
}
extern "C" q2proto_error_t q2protoio_deflate_begin(
    q2protoio_deflate_args_t *, size_t,
    q2proto_inflate_deflate_header_mode_t, uintptr_t *argument)
{
    *argument = 0;
    return Q2P_ERR_NOT_IMPLEMENTED;
}
extern "C" q2proto_error_t q2protoio_deflate_get_data(
    uintptr_t, size_t *in_size, const void **out, size_t *out_size)
{
    if (in_size)
        *in_size = 0;
    *out = nullptr;
    *out_size = 0;
    return Q2P_ERR_NOT_IMPLEMENTED;
}
extern "C" q2proto_error_t q2protoio_deflate_end(uintptr_t)
{
    return Q2P_ERR_NOT_IMPLEMENTED;
}
#endif

int main()
{
    run_handshake();
    std::printf(
        "native_readiness_q2proto_wire_test: ok svc=%zu clc=%zu\n",
        kSvcReadinessRecordWireBytes, kClcReadinessRecordWireBytes);
    return 0;
}
