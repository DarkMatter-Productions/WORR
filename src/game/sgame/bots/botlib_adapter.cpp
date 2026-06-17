// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "botlib_adapter.hpp"
#include "q3a/q3a_botlib_boundary.hpp"
#include "q3a/q3a_botlib_import.h"

#include <string>

namespace {
BotLibAdapterStatus botLibAdapterStatus;

std::string PendingRuntimeMessage() {
	const Q3ABotLibBoundaryInfo &boundary = Q3A_BotLibBoundaryInfo();
	return std::string("Q3A BotLib runtime import pending; boundary pinned to ") +
		boundary.sourceCommit;
}

void CopyImportStatus() {
	const Q3ABotLibImportSmokeStatus *status = Q3A_BotLibImport_SmokeStatus();
	botLibAdapterStatus.q3aUtilitySmokePassed = status->libvarSmokePassed != 0;
	botLibAdapterStatus.q3aAasLoadAttempted = status->aasLoadAttempted != 0;
	botLibAdapterStatus.q3aAasLoaded = status->aasLoaded != 0;
	botLibAdapterStatus.q3aAasSampleAttempted = status->aasSampleAttempted != 0;
	botLibAdapterStatus.q3aAasSamplePassed = status->aasSamplePassed != 0;
	botLibAdapterStatus.q3aAngleVectorsSmokePassed = status->angleVectorsSmokePassed != 0;
	botLibAdapterStatus.q3aBspEntityLoadAttempted = status->bspEntityLoadAttempted != 0;
	botLibAdapterStatus.q3aBspEntityLoaded = status->bspEntityLoaded != 0;
	botLibAdapterStatus.q3aBspEntityValueSmokePassed = status->bspEntityValueSmokePassed != 0;
	botLibAdapterStatus.q3aBspModelLoadAttempted = status->bspModelLoadAttempted != 0;
	botLibAdapterStatus.q3aBspModelLoaded = status->bspModelLoaded != 0;
	botLibAdapterStatus.q3aBspModelBoundsSmokePassed = status->bspModelBoundsSmokePassed != 0;
	botLibAdapterStatus.q3aBspCollisionLoadAttempted = status->bspCollisionLoadAttempted != 0;
	botLibAdapterStatus.q3aBspCollisionLoaded = status->bspCollisionLoaded != 0;
	botLibAdapterStatus.q3aBspCollisionPointContentsSmokePassed = status->bspCollisionPointContentsSmokePassed != 0;
	botLibAdapterStatus.q3aBspCollisionTraceSmokePassed = status->bspCollisionTraceSmokePassed != 0;
	botLibAdapterStatus.q3aBspVisibilityLoadAttempted = status->bspVisibilityLoadAttempted != 0;
	botLibAdapterStatus.q3aBspVisibilityLoaded = status->bspVisibilityLoaded != 0;
	botLibAdapterStatus.q3aBspVisibilityPvsSmokePassed = status->bspVisibilityPvsSmokePassed != 0;
	botLibAdapterStatus.q3aBspVisibilityPhsSmokePassed = status->bspVisibilityPhsSmokePassed != 0;
	botLibAdapterStatus.q3aAasLoadResult = status->aasLoadResult;
	botLibAdapterStatus.q3aAasBspChecksum = status->aasBspChecksum;
	botLibAdapterStatus.q3aAasAreas = status->aasAreas;
	botLibAdapterStatus.q3aAasReachability = status->aasReachability;
	botLibAdapterStatus.q3aAasClusters = status->aasClusters;
	botLibAdapterStatus.q3aAasSampleArea = status->aasSampleArea;
	botLibAdapterStatus.q3aAasSamplePointArea = status->aasSamplePointArea;
	botLibAdapterStatus.q3aAasSamplePresenceType = status->aasSamplePresenceType;
	botLibAdapterStatus.q3aAasSampleCluster = status->aasSampleCluster;
	botLibAdapterStatus.q3aAasSampleReachability = status->aasSampleReachability;
	botLibAdapterStatus.q3aRuntimeMilliseconds = status->runtimeMilliseconds;
	botLibAdapterStatus.q3aBspEntityCount = status->bspEntityCount;
	botLibAdapterStatus.q3aBspEntityPairs = status->bspEntityPairs;
	botLibAdapterStatus.q3aBspModelCount = status->bspModelCount;
	botLibAdapterStatus.q3aBspCollisionPlanes = status->bspCollisionPlanes;
	botLibAdapterStatus.q3aBspCollisionNodes = status->bspCollisionNodes;
	botLibAdapterStatus.q3aBspCollisionLeafs = status->bspCollisionLeafs;
	botLibAdapterStatus.q3aBspCollisionBrushes = status->bspCollisionBrushes;
	botLibAdapterStatus.q3aBspVisibilityClusters = status->bspVisibilityClusters;
	botLibAdapterStatus.utilityMessage = status->message != nullptr ? status->message : "";
	botLibAdapterStatus.aasMessage = status->aasMessage != nullptr ? status->aasMessage : "";
	botLibAdapterStatus.aasSampleMessage = status->aasSampleMessage != nullptr ? status->aasSampleMessage : "";
	botLibAdapterStatus.angleVectorsMessage = status->angleVectorsMessage != nullptr ? status->angleVectorsMessage : "";
	botLibAdapterStatus.bspEntityMessage = status->bspEntityMessage != nullptr ? status->bspEntityMessage : "";
	botLibAdapterStatus.bspModelMessage = status->bspModelMessage != nullptr ? status->bspModelMessage : "";
	botLibAdapterStatus.bspCollisionMessage = status->bspCollisionMessage != nullptr ? status->bspCollisionMessage : "";
	botLibAdapterStatus.bspVisibilityMessage = status->bspVisibilityMessage != nullptr ? status->bspVisibilityMessage : "";
}
} // namespace

void BotLibAdapter_Init() {
	const Q3ABotLibBoundaryInfo &boundary = Q3A_BotLibBoundaryInfo();

	botLibAdapterStatus.initialized = true;
	botLibAdapterStatus.q3aRuntimeImported = boundary.runtimeImported;
	Q3A_BotLibImport_RunLibVarSmoke();
	CopyImportStatus();
	botLibAdapterStatus.levelActive = false;
	botLibAdapterStatus.mapName.clear();
	botLibAdapterStatus.aasPath.clear();
	botLibAdapterStatus.bspPath.clear();
	botLibAdapterStatus.message = PendingRuntimeMessage();
	botLibAdapterStatus.sourceCommit = boundary.sourceCommit;
	botLibAdapterStatus.importRoot = boundary.localImportRoot;
	botLibAdapterStatus.buildStrategy = boundary.buildStrategy;
	botLibAdapterStatus.plannedImportFileCount = Q3A_BotLibPlannedFileCount();
}

void BotLibAdapter_BeginLevel(const char *mapName, const char *aasPath) {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}

	botLibAdapterStatus.levelActive = true;
	botLibAdapterStatus.mapName = mapName != nullptr ? mapName : "";
	botLibAdapterStatus.aasPath = aasPath != nullptr ? aasPath : "";
	botLibAdapterStatus.message = PendingRuntimeMessage();
}

bool BotLibAdapter_LoadBspEntityData(const char *mapName, const char *bspPath, const void *data, int length) {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}

	botLibAdapterStatus.mapName = mapName != nullptr ? mapName : "";
	botLibAdapterStatus.bspPath = bspPath != nullptr ? bspPath : "";
	const bool loaded = Q3A_BotLibImport_LoadBspEntityData(
		botLibAdapterStatus.bspPath.empty() ? "worr-memory.bsp" : botLibAdapterStatus.bspPath.c_str(),
		data,
		length) != 0;
	CopyImportStatus();
	return loaded;
}

bool BotLibAdapter_LoadBspModelData(const char *mapName, const char *bspPath, const void *data, int length) {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}

	botLibAdapterStatus.mapName = mapName != nullptr ? mapName : "";
	botLibAdapterStatus.bspPath = bspPath != nullptr ? bspPath : "";
	const bool loaded = Q3A_BotLibImport_LoadBspModelData(
		botLibAdapterStatus.bspPath.empty() ? "worr-memory.bsp" : botLibAdapterStatus.bspPath.c_str(),
		data,
		length) != 0;
	CopyImportStatus();
	return loaded;
}

bool BotLibAdapter_LoadBspCollisionData(const char *mapName, const char *bspPath, const void *data, int length) {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}

	botLibAdapterStatus.mapName = mapName != nullptr ? mapName : "";
	botLibAdapterStatus.bspPath = bspPath != nullptr ? bspPath : "";
	const bool loaded = Q3A_BotLibImport_LoadBspCollisionData(
		botLibAdapterStatus.bspPath.empty() ? "worr-memory.bsp" : botLibAdapterStatus.bspPath.c_str(),
		data,
		length) != 0;
	CopyImportStatus();
	return loaded;
}

bool BotLibAdapter_LoadBspVisibilityData(const char *mapName, const char *bspPath, const void *data, int length) {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}

	botLibAdapterStatus.mapName = mapName != nullptr ? mapName : "";
	botLibAdapterStatus.bspPath = bspPath != nullptr ? bspPath : "";
	const bool loaded = Q3A_BotLibImport_LoadBspVisibilityData(
		botLibAdapterStatus.bspPath.empty() ? "worr-memory.bsp" : botLibAdapterStatus.bspPath.c_str(),
		data,
		length) != 0;
	CopyImportStatus();
	return loaded;
}

bool BotLibAdapter_LoadAasBuffer(const char *mapName, const char *aasPath, const void *data, int length, int bspChecksum) {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}

	botLibAdapterStatus.mapName = mapName != nullptr ? mapName : "";
	botLibAdapterStatus.aasPath = aasPath != nullptr ? aasPath : "";
	const bool loaded = Q3A_BotLibImport_LoadAASBuffer(
		botLibAdapterStatus.aasPath.empty() ? "worr-memory.aas" : botLibAdapterStatus.aasPath.c_str(),
		data,
		length,
		bspChecksum) != 0;
	CopyImportStatus();
	return loaded;
}

void BotLibAdapter_EndLevel() {
	Q3A_BotLibImport_UnloadAAS();
	Q3A_BotLibImport_ClearBspEntityData();
	Q3A_BotLibImport_ClearBspModelData();
	Q3A_BotLibImport_ClearBspCollisionData();
	Q3A_BotLibImport_ClearBspVisibilityData();
	CopyImportStatus();
	botLibAdapterStatus.levelActive = false;
	botLibAdapterStatus.mapName.clear();
	botLibAdapterStatus.aasPath.clear();
	botLibAdapterStatus.bspPath.clear();
}

void BotLibAdapter_RunFrame(int runtimeMilliseconds) {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}
	Q3A_BotLibImport_SetMilliseconds(runtimeMilliseconds);
	CopyImportStatus();
}

const BotLibAdapterStatus &BotLibAdapter_GetStatus() {
	return botLibAdapterStatus;
}
