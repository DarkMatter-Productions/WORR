// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

void Bot_BeginFrame( gentity_t * bot );
void Bot_EndFrame( gentity_t * bot );
bool Bot_BuildFrameCommand( gentity_t * bot, usercmd_t * cmd );
void Bot_FrameCommandPrintStatus( int expectedMinFrames, int expectedMinCommands );
