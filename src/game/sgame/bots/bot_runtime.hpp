// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

#include <cstdint>
#include <string>

struct gentity_t;

enum class BotAasRuntimeState {
	Disabled,
	NotLoaded,
	Loaded,
	Failed,
};

struct BotAasRuntimeStatus {
	BotAasRuntimeState state = BotAasRuntimeState::Disabled;
	bool enabled = false;
	bool attemptedLoad = false;
	bool attemptedBspEntityLoad = false;
	bool attemptedBspModelLoad = false;
	bool attemptedBspCollisionLoad = false;
	bool attemptedBspVisibilityLoad = false;
	std::string mapName;
	std::string aasPath;
	std::string bspPath;
	std::string message;
	std::string bspEntityMessage;
	std::string bspModelMessage;
	std::string bspCollisionMessage;
	std::string bspVisibilityMessage;
	int32_t version = 0;
	int32_t bspChecksum = 0;
	int64_t fileSize = 0;
	int64_t bspFileSize = 0;
	int32_t bspEntityBytes = 0;
	int32_t bspModelBytes = 0;
	int32_t bspModelCount = 0;
	int32_t bspCollisionPlaneCount = 0;
	int32_t bspCollisionNodeCount = 0;
	int32_t bspCollisionLeafCount = 0;
	int32_t bspCollisionBrushCount = 0;
	int32_t bspVisibilityClusterCount = 0;
	int32_t areaCount = 0;
	int32_t areaSettingsCount = 0;
	int32_t reachabilityCount = 0;
	int32_t clusterCount = 0;
};

void Bot_RuntimeRegisterCvars();
void Bot_RuntimeBeginLevel();
void Bot_RuntimeEndLevel();
void Bot_RuntimeRunFrame();
void Bot_RuntimePrintLifecycleStatus();

bool Bot_RuntimeEnabled();
bool Bot_RuntimeAasLoaded();
const BotAasRuntimeStatus &Bot_RuntimeStatus();
