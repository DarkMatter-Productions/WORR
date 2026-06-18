// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

struct gentity_t;
struct usercmd_t;

struct BotBrainBlackboardSnapshot {
	bool valid = false;
	int clientIndex = -1;
	int currentEnemyEntity = -1;
	int currentEnemyClient = -1;
	int currentEnemySpawnCount = 0;
	bool currentEnemyVisible = false;
	bool currentEnemyShootable = false;
	int currentEnemyDistanceSquared = 0;
	int currentEnemyLastSeenFrame = 0;
	int currentEnemyLastSeenTimeMilliseconds = 0;
	int lastSeenEnemyEntity = -1;
	int lastSeenEnemyClient = -1;
	int lastSeenEnemySpawnCount = 0;
	int lastSeenEnemyDistanceSquared = 0;
	int lastSeenFrame = 0;
	int lastSeenTimeMilliseconds = 0;
	int lastSeenOriginX = 0;
	int lastSeenOriginY = 0;
	int lastSeenOriginZ = 0;
	int lastHeardEntity = -1;
	int lastHeardClient = -1;
	int lastHeardFrame = 0;
	int lastHeardTimeMilliseconds = 0;
	int lastHeardOriginX = 0;
	int lastHeardOriginY = 0;
	int lastHeardOriginZ = 0;
	int lastDamagedByEntity = -1;
	int lastDamagedByClient = -1;
	int lastDamagedFrame = 0;
	int lastDamagedTimeMilliseconds = 0;
	int lastDamageOriginX = 0;
	int lastDamageOriginY = 0;
	int lastDamageOriginZ = 0;
};

void BotBrain_BeginFrame( gentity_t * bot );
void BotBrain_EndFrame( gentity_t * bot );
bool BotBrain_BuildFrameCommand( gentity_t * bot, usercmd_t * cmd );
void BotBrain_PrintFrameCommandStatus( int expectedMinFrames, int expectedMinCommands );
bool BotBrain_GetBlackboardSnapshot( int clientIndex, BotBrainBlackboardSnapshot * snapshot );
