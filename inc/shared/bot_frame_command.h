#pragma once

#define BOT_FRAME_COMMAND_API_V1 "BOT_FRAME_COMMAND_API_V1"

typedef struct {
    int api_version;

    /*
     * BuildCommand receives a game-owned player entity and a server-owned
     * usercmd_t buffer. Pointers are void to keep this narrow extension ABI
     * independent of the C/C++ usercmd field names on either side.
     */
    int  (*BuildCommand)(void *bot_entity, void *usercmd);
    void (*PrintStatus)(int expected_min_frames, int expected_min_commands);
} bot_frame_command_api_v1_t;
