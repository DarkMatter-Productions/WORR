// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

#include <cstddef>
#include <string>

struct BotLibAdapterStatus {
	bool initialized = false;
	bool q3aRuntimeImported = false;
	bool q3aUtilitySmokePassed = false;
	bool q3aAasLoadAttempted = false;
	bool q3aAasLoaded = false;
	bool q3aAasSampleAttempted = false;
	bool q3aAasSamplePassed = false;
	bool q3aAngleVectorsSmokePassed = false;
	bool q3aBspEntityLoadAttempted = false;
	bool q3aBspEntityLoaded = false;
	bool q3aBspEntityValueSmokePassed = false;
	bool q3aBspModelLoadAttempted = false;
	bool q3aBspModelLoaded = false;
	bool q3aBspModelBoundsSmokePassed = false;
	bool q3aBspCollisionLoadAttempted = false;
	bool q3aBspCollisionLoaded = false;
	bool q3aBspCollisionPointContentsSmokePassed = false;
	bool q3aBspCollisionTraceSmokePassed = false;
	bool q3aBspVisibilityLoadAttempted = false;
	bool q3aBspVisibilityLoaded = false;
	bool q3aBspVisibilityPvsSmokePassed = false;
	bool q3aBspVisibilityPhsSmokePassed = false;
	bool levelActive = false;
	int q3aAasLoadResult = 0;
	int q3aAasBspChecksum = 0;
	int q3aAasAreas = 0;
	int q3aAasReachability = 0;
	int q3aAasClusters = 0;
	int q3aAasSampleArea = 0;
	int q3aAasSamplePointArea = 0;
	int q3aAasSamplePresenceType = 0;
	int q3aAasSampleCluster = 0;
	int q3aAasSampleReachability = 0;
	int q3aRuntimeMilliseconds = 0;
	int q3aBspEntityCount = 0;
	int q3aBspEntityPairs = 0;
	int q3aBspModelCount = 0;
	int q3aBspCollisionPlanes = 0;
	int q3aBspCollisionNodes = 0;
	int q3aBspCollisionLeafs = 0;
	int q3aBspCollisionBrushes = 0;
	int q3aBspVisibilityClusters = 0;
	std::string mapName;
	std::string aasPath;
	std::string bspPath;
	std::string message;
	std::string utilityMessage;
	std::string aasMessage;
	std::string aasSampleMessage;
	std::string angleVectorsMessage;
	std::string bspEntityMessage;
	std::string bspModelMessage;
	std::string bspCollisionMessage;
	std::string bspVisibilityMessage;
	const char *sourceCommit = nullptr;
	const char *importRoot = nullptr;
	const char *buildStrategy = nullptr;
	size_t plannedImportFileCount = 0;
};

void BotLibAdapter_Init();
void BotLibAdapter_BeginLevel(const char *mapName, const char *aasPath);
bool BotLibAdapter_LoadBspEntityData(const char *mapName, const char *bspPath, const void *data, int length);
bool BotLibAdapter_LoadBspModelData(const char *mapName, const char *bspPath, const void *data, int length);
bool BotLibAdapter_LoadBspCollisionData(const char *mapName, const char *bspPath, const void *data, int length);
bool BotLibAdapter_LoadBspVisibilityData(const char *mapName, const char *bspPath, const void *data, int length);
bool BotLibAdapter_LoadAasBuffer(const char *mapName, const char *aasPath, const void *data, int length, int bspChecksum);
void BotLibAdapter_EndLevel();
void BotLibAdapter_RunFrame(int runtimeMilliseconds);

const BotLibAdapterStatus &BotLibAdapter_GetStatus();
