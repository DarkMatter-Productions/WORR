// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "botlib_adapter.hpp"
#include "q3a/q3a_botlib_boundary.hpp"
#include "q3a/q3a_botlib_import.h"

#include <string>

static_assert(BOTLIB_ADAPTER_MAX_ROUTE_POINTS == Q3A_BOTLIB_IMPORT_MAX_ROUTE_POINTS);

namespace {
BotLibAdapterStatus botLibAdapterStatus;
BotLibAdapterPrintCallback botLibAdapterPrintCallback = nullptr;
BotLibAdapterBotClientCommandCallback botLibAdapterBotClientCommandCallback = nullptr;
BotLibAdapterFilesystemLoadCallback botLibAdapterFilesystemLoadCallback = nullptr;
BotLibAdapterFilesystemFreeCallback botLibAdapterFilesystemFreeCallback = nullptr;
BotLibAdapterEntityTraceCallback botLibAdapterEntityTraceCallback = nullptr;
BotLibAdapterDebugDrawCallback botLibAdapterDebugDrawCallback = nullptr;
BotLibAdapterDebugPolygonCallback botLibAdapterDebugPolygonCallback = nullptr;

std::string PendingRuntimeMessage() {
	const Q3ABotLibBoundaryInfo &boundary = Q3A_BotLibBoundaryInfo();
	return std::string("Q3A BotLib runtime import pending; boundary pinned to ") +
		boundary.sourceCommit;
}

void BotLibAdapter_Q3APrint(int type, const char *message) {
	if (botLibAdapterPrintCallback == nullptr) {
		return;
	}

	botLibAdapterPrintCallback(type, message);
}

int BotLibAdapter_Q3ABotClientCommand(int client, const char *command) {
	if (botLibAdapterBotClientCommandCallback == nullptr) {
		return 0;
	}

	return botLibAdapterBotClientCommandCallback(client, command) ? 1 : 0;
}

int BotLibAdapter_Q3AFilesystemLoad(const char *path, const unsigned char **data) {
	if (botLibAdapterFilesystemLoadCallback == nullptr) {
		if (data != nullptr) {
			*data = nullptr;
		}
		return -1;
	}

	return botLibAdapterFilesystemLoadCallback(path, data);
}

void BotLibAdapter_Q3AFilesystemFree(const unsigned char *data) {
	if (botLibAdapterFilesystemFreeCallback == nullptr) {
		return;
	}

	botLibAdapterFilesystemFreeCallback(data);
}

int BotLibAdapter_Q3AEntityTrace(
	int entnum,
	const float start[3],
	const float mins[3],
	const float maxs[3],
	const float end[3],
	int contentmask,
	Q3ABotLibImportTraceResult *trace) {
	BotLibAdapterTraceResult adapterTrace{};

	if (botLibAdapterEntityTraceCallback == nullptr || trace == nullptr) {
		return 0;
	}

	if (!botLibAdapterEntityTraceCallback(entnum, start, mins, maxs, end, contentmask, &adapterTrace)) {
		return 0;
	}

	trace->hit = adapterTrace.hit ? 1 : 0;
	trace->allSolid = adapterTrace.allSolid ? 1 : 0;
	trace->startSolid = adapterTrace.startSolid ? 1 : 0;
	trace->fraction = adapterTrace.fraction;
	for (int i = 0; i < 3; ++i) {
		trace->endPos[i] = adapterTrace.endPos[i];
		trace->planeNormal[i] = adapterTrace.planeNormal[i];
	}
	trace->planeDist = adapterTrace.planeDist;
	trace->contents = adapterTrace.contents;
	trace->entnum = adapterTrace.entnum;
	return 1;
}

int BotLibAdapter_Q3ADebugDraw(
	int primitive,
	const float start[3],
	const float end[3],
	float size,
	int color,
	int secondaryColor) {
	if (botLibAdapterDebugDrawCallback == nullptr) {
		return 0;
	}

	return botLibAdapterDebugDrawCallback(primitive, start, end, size, color, secondaryColor) ? 1 : 0;
}

int BotLibAdapter_Q3ADebugPolygon(int color, int pointCount, const float *points) {
	if (botLibAdapterDebugPolygonCallback == nullptr) {
		return 0;
	}

	return botLibAdapterDebugPolygonCallback(color, pointCount, points) ? 1 : 0;
}

void CopyImportStatus() {
	const Q3ABotLibImportSmokeStatus *status = Q3A_BotLibImport_SmokeStatus();
	botLibAdapterStatus.q3aUtilitySmokePassed = status->libvarSmokePassed != 0;
	botLibAdapterStatus.q3aPrintCallbackSet = status->printCallbackSet != 0;
	botLibAdapterStatus.q3aBotClientCommandCallbackSet = status->botClientCommandCallbackSet != 0;
	botLibAdapterStatus.q3aBotClientCommandAttempted = status->botClientCommandAttempted != 0;
	botLibAdapterStatus.q3aBotClientCommandSmokePassed = status->botClientCommandSmokePassed != 0;
	botLibAdapterStatus.q3aAasLoadAttempted = status->aasLoadAttempted != 0;
	botLibAdapterStatus.q3aAasLoaded = status->aasLoaded != 0;
	botLibAdapterStatus.q3aAasSampleAttempted = status->aasSampleAttempted != 0;
	botLibAdapterStatus.q3aAasSamplePassed = status->aasSamplePassed != 0;
	botLibAdapterStatus.q3aAasClusterAttempted = status->aasClusterAttempted != 0;
	botLibAdapterStatus.q3aAasClusterPassed = status->aasClusterPassed != 0;
	botLibAdapterStatus.q3aAasRouteAttempted = status->aasRouteAttempted != 0;
	botLibAdapterStatus.q3aAasRoutePassed = status->aasRoutePassed != 0;
	botLibAdapterStatus.q3aAasAltRouteAttempted = status->aasAltRouteAttempted != 0;
	botLibAdapterStatus.q3aAasAltRoutePassed = status->aasAltRoutePassed != 0;
	botLibAdapterStatus.q3aAasMovementAttempted = status->aasMovementAttempted != 0;
	botLibAdapterStatus.q3aAasMovementPassed = status->aasMovementPassed != 0;
	botLibAdapterStatus.q3aAasMovementDropToFloorPassed = status->aasMovementDropToFloorPassed != 0;
	botLibAdapterStatus.q3aAasMovementJumpVelocityPassed = status->aasMovementJumpVelocityPassed != 0;
	botLibAdapterStatus.q3aAasStartFrameAttempted = status->aasStartFrameAttempted != 0;
	botLibAdapterStatus.q3aAasStartFramePassed = status->aasStartFramePassed != 0;
	botLibAdapterStatus.q3aEntitySyncAttempted = status->entitySyncAttempted != 0;
	botLibAdapterStatus.q3aEntitySyncPassed = status->entitySyncPassed != 0;
	botLibAdapterStatus.q3aEntityTraceCallbackSet = status->entityTraceCallbackSet != 0;
	botLibAdapterStatus.q3aDebugDrawCallbackSet = status->debugDrawCallbackSet != 0;
	botLibAdapterStatus.q3aDebugDrawAttempted = status->debugDrawAttempted != 0;
	botLibAdapterStatus.q3aDebugDrawPassed = status->debugDrawPassed != 0;
	botLibAdapterStatus.q3aDebugPolygonCallbackSet = status->debugPolygonCallbackSet != 0;
	botLibAdapterStatus.q3aDebugPolygonAttempted = status->debugPolygonAttempted != 0;
	botLibAdapterStatus.q3aDebugPolygonPassed = status->debugPolygonPassed != 0;
	botLibAdapterStatus.q3aDebugAreaAttempted = status->debugAreaAttempted != 0;
	botLibAdapterStatus.q3aDebugAreaPassed = status->debugAreaPassed != 0;
	botLibAdapterStatus.q3aRouteOverlayAttempted = status->routeOverlayAttempted != 0;
	botLibAdapterStatus.q3aRouteOverlayPassed = status->routeOverlayPassed != 0;
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
	botLibAdapterStatus.q3aBspLeafLinkAttempted = status->bspLeafLinkAttempted != 0;
	botLibAdapterStatus.q3aBspBoxEntitiesSmokePassed = status->bspBoxEntitiesSmokePassed != 0;
	botLibAdapterStatus.q3aBspVisibilityLoadAttempted = status->bspVisibilityLoadAttempted != 0;
	botLibAdapterStatus.q3aBspVisibilityLoaded = status->bspVisibilityLoaded != 0;
	botLibAdapterStatus.q3aBspVisibilityPvsSmokePassed = status->bspVisibilityPvsSmokePassed != 0;
	botLibAdapterStatus.q3aBspVisibilityPhsSmokePassed = status->bspVisibilityPhsSmokePassed != 0;
	botLibAdapterStatus.q3aFilesystemCallbackSet = status->filesystemCallbackSet != 0;
	botLibAdapterStatus.q3aFilesystemAttempted = status->filesystemAttempted != 0;
	botLibAdapterStatus.q3aFilesystemPassed = status->filesystemPassed != 0;
	botLibAdapterStatus.q3aLifecycleInitCount = status->lifecycleInitCount;
	botLibAdapterStatus.q3aLifecycleShutdownCount = status->lifecycleShutdownCount;
	botLibAdapterStatus.q3aLifecycleLoadAttempts = status->lifecycleLoadAttempts;
	botLibAdapterStatus.q3aLifecycleLoadSuccesses = status->lifecycleLoadSuccesses;
	botLibAdapterStatus.q3aLifecycleActiveUnloads = status->lifecycleActiveUnloads;
	botLibAdapterStatus.q3aLifecycleCleanUnloads = status->lifecycleCleanUnloads;
	botLibAdapterStatus.q3aLifecycleUnloadFailures = status->lifecycleUnloadFailures;
	botLibAdapterStatus.q3aLifecycleLastUnloadZoneActiveBytes = status->lifecycleLastUnloadZoneActiveBytes;
	botLibAdapterStatus.q3aLifecycleLastUnloadHunkActiveBytes = status->lifecycleLastUnloadHunkActiveBytes;
	botLibAdapterStatus.q3aLifecycleLastUnloadOpenFiles = status->lifecycleLastUnloadOpenFiles;
	botLibAdapterStatus.q3aLifecyclePersistentZoneBytes = status->lifecyclePersistentZoneBytes;
	botLibAdapterStatus.q3aPrintMessages = status->printMessages;
	botLibAdapterStatus.q3aPrintWarnings = status->printWarnings;
	botLibAdapterStatus.q3aPrintErrors = status->printErrors;
	botLibAdapterStatus.q3aPrintFatals = status->printFatals;
	botLibAdapterStatus.q3aPrintLastType = status->printLastType;
	botLibAdapterStatus.q3aBotClientCommandClient = status->botClientCommandClient;
	botLibAdapterStatus.q3aBotClientCommandAccepted = status->botClientCommandAccepted;
	botLibAdapterStatus.q3aBotClientCommandRejected = status->botClientCommandRejected;
	botLibAdapterStatus.q3aBotClientCommandFailures = status->botClientCommandFailures;
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
	botLibAdapterStatus.q3aAasClusterArea = status->aasClusterArea;
	botLibAdapterStatus.q3aAasClusterCluster = status->aasClusterCluster;
	botLibAdapterStatus.q3aAasClusterNumClusters = status->aasClusterNumClusters;
	botLibAdapterStatus.q3aAasClusterAreas = status->aasClusterAreas;
	botLibAdapterStatus.q3aAasClusterReachabilityAreas = status->aasClusterReachabilityAreas;
	botLibAdapterStatus.q3aAasClusterFailures = status->aasClusterFailures;
	botLibAdapterStatus.q3aAasRouteStartArea = status->aasRouteStartArea;
	botLibAdapterStatus.q3aAasRouteGoalArea = status->aasRouteGoalArea;
	botLibAdapterStatus.q3aAasRouteTravelTime = status->aasRouteTravelTime;
	botLibAdapterStatus.q3aAasRouteReachability = status->aasRouteReachability;
	botLibAdapterStatus.q3aAasRouteEndArea = status->aasRouteEndArea;
	botLibAdapterStatus.q3aAasRouteStopEvent = status->aasRouteStopEvent;
	botLibAdapterStatus.q3aAasAltRouteStartArea = status->aasAltRouteStartArea;
	botLibAdapterStatus.q3aAasAltRouteGoalArea = status->aasAltRouteGoalArea;
	botLibAdapterStatus.q3aAasAltRouteGoals = status->aasAltRouteGoals;
	botLibAdapterStatus.q3aAasAltRouteFirstArea = status->aasAltRouteFirstArea;
	botLibAdapterStatus.q3aAasAltRouteFirstStartTravelTime = status->aasAltRouteFirstStartTravelTime;
	botLibAdapterStatus.q3aAasAltRouteFirstGoalTravelTime = status->aasAltRouteFirstGoalTravelTime;
	botLibAdapterStatus.q3aAasAltRouteFirstExtraTravelTime = status->aasAltRouteFirstExtraTravelTime;
	botLibAdapterStatus.q3aAasAltRouteFailures = status->aasAltRouteFailures;
	botLibAdapterStatus.q3aAasMovementEndArea = status->aasMovementEndArea;
	botLibAdapterStatus.q3aAasMovementStopEvent = status->aasMovementStopEvent;
	botLibAdapterStatus.q3aAasMovementFrames = status->aasMovementFrames;
	botLibAdapterStatus.q3aAasStartFrameResult = status->aasStartFrameResult;
	botLibAdapterStatus.q3aAasStartFrameCount = status->aasStartFrameCount;
	botLibAdapterStatus.q3aAasStartFrameTimeMilliseconds = status->aasStartFrameTimeMilliseconds;
	botLibAdapterStatus.q3aEntitySyncUpdated = status->entitySyncUpdated;
	botLibAdapterStatus.q3aEntitySyncUnlinked = status->entitySyncUnlinked;
	botLibAdapterStatus.q3aEntitySyncSkipped = status->entitySyncSkipped;
	botLibAdapterStatus.q3aEntitySyncFailures = status->entitySyncFailures;
	botLibAdapterStatus.q3aEntitySyncMaxEntities = status->entitySyncMaxEntities;
	botLibAdapterStatus.q3aEntityTraceAttempted = status->entityTraceAttempted;
	botLibAdapterStatus.q3aEntityTraceHits = status->entityTraceHits;
	botLibAdapterStatus.q3aEntityTraceMisses = status->entityTraceMisses;
	botLibAdapterStatus.q3aEntityTraceFailures = status->entityTraceFailures;
	botLibAdapterStatus.q3aDebugDrawLines = status->debugDrawLines;
	botLibAdapterStatus.q3aDebugDrawCrosses = status->debugDrawCrosses;
	botLibAdapterStatus.q3aDebugDrawArrows = status->debugDrawArrows;
	botLibAdapterStatus.q3aDebugDrawClears = status->debugDrawClears;
	botLibAdapterStatus.q3aDebugDrawFailures = status->debugDrawFailures;
	botLibAdapterStatus.q3aDebugPolygonCreates = status->debugPolygonCreates;
	botLibAdapterStatus.q3aDebugPolygonDeletes = status->debugPolygonDeletes;
	botLibAdapterStatus.q3aDebugPolygonPoints = status->debugPolygonPoints;
	botLibAdapterStatus.q3aDebugPolygonLastId = status->debugPolygonLastId;
	botLibAdapterStatus.q3aDebugPolygonFailures = status->debugPolygonFailures;
	botLibAdapterStatus.q3aDebugAreaArea = status->debugAreaArea;
	botLibAdapterStatus.q3aDebugAreaLines = status->debugAreaLines;
	botLibAdapterStatus.q3aDebugAreaPolygonCreates = status->debugAreaPolygonCreates;
	botLibAdapterStatus.q3aDebugAreaPolygonDeletes = status->debugAreaPolygonDeletes;
	botLibAdapterStatus.q3aDebugAreaFailures = status->debugAreaFailures;
	botLibAdapterStatus.q3aRouteOverlayStartArea = status->routeOverlayStartArea;
	botLibAdapterStatus.q3aRouteOverlayGoalArea = status->routeOverlayGoalArea;
	botLibAdapterStatus.q3aRouteOverlayEndArea = status->routeOverlayEndArea;
	botLibAdapterStatus.q3aRouteOverlayTravelTime = status->routeOverlayTravelTime;
	botLibAdapterStatus.q3aRouteOverlayReachability = status->routeOverlayReachability;
	botLibAdapterStatus.q3aRouteOverlayLines = status->routeOverlayLines;
	botLibAdapterStatus.q3aRouteOverlayCrosses = status->routeOverlayCrosses;
	botLibAdapterStatus.q3aRouteOverlayArrows = status->routeOverlayArrows;
	botLibAdapterStatus.q3aRouteOverlayClears = status->routeOverlayClears;
	botLibAdapterStatus.q3aRouteOverlayFailures = status->routeOverlayFailures;
	botLibAdapterStatus.q3aRuntimeMilliseconds = status->runtimeMilliseconds;
	botLibAdapterStatus.q3aBspEntityCount = status->bspEntityCount;
	botLibAdapterStatus.q3aBspEntityPairs = status->bspEntityPairs;
	botLibAdapterStatus.q3aBspModelCount = status->bspModelCount;
	botLibAdapterStatus.q3aBspCollisionPlanes = status->bspCollisionPlanes;
	botLibAdapterStatus.q3aBspCollisionNodes = status->bspCollisionNodes;
	botLibAdapterStatus.q3aBspCollisionLeafs = status->bspCollisionLeafs;
	botLibAdapterStatus.q3aBspCollisionBrushes = status->bspCollisionBrushes;
	botLibAdapterStatus.q3aBspLeafLinks = status->bspLeafLinks;
	botLibAdapterStatus.q3aBspLeafLinkFailures = status->bspLeafLinkFailures;
	botLibAdapterStatus.q3aBspBoxEntitiesCount = status->bspBoxEntitiesCount;
	botLibAdapterStatus.q3aBspVisibilityClusters = status->bspVisibilityClusters;
	botLibAdapterStatus.q3aMemoryZoneActiveBytes = status->memoryZoneActiveBytes;
	botLibAdapterStatus.q3aMemoryZonePeakBytes = status->memoryZonePeakBytes;
	botLibAdapterStatus.q3aMemoryZoneAllocations = status->memoryZoneAllocations;
	botLibAdapterStatus.q3aMemoryZoneFrees = status->memoryZoneFrees;
	botLibAdapterStatus.q3aMemoryHunkActiveBytes = status->memoryHunkActiveBytes;
	botLibAdapterStatus.q3aMemoryHunkPeakBytes = status->memoryHunkPeakBytes;
	botLibAdapterStatus.q3aMemoryHunkAllocations = status->memoryHunkAllocations;
	botLibAdapterStatus.q3aMemoryHunkGroupFrees = status->memoryHunkGroupFrees;
	botLibAdapterStatus.q3aMemoryFailures = status->memoryFailures;
	botLibAdapterStatus.q3aAvailableMemory = status->availableMemory;
	botLibAdapterStatus.q3aFilesystemOpenAttempts = status->filesystemOpenAttempts;
	botLibAdapterStatus.q3aFilesystemOpenFiles = status->filesystemOpenFiles;
	botLibAdapterStatus.q3aFilesystemOpenMemoryFiles = status->filesystemOpenMemoryFiles;
	botLibAdapterStatus.q3aFilesystemOpenFailures = status->filesystemOpenFailures;
	botLibAdapterStatus.q3aFilesystemRouteCacheMisses = status->filesystemRouteCacheMisses;
	botLibAdapterStatus.q3aFilesystemReadBytes = status->filesystemReadBytes;
	botLibAdapterStatus.q3aFilesystemSeekCount = status->filesystemSeekCount;
	botLibAdapterStatus.q3aFilesystemCloseCount = status->filesystemCloseCount;
	botLibAdapterStatus.q3aFilesystemWriteRejected = status->filesystemWriteRejected;
	botLibAdapterStatus.utilityMessage = status->message != nullptr ? status->message : "";
	botLibAdapterStatus.lifecycleMessage = status->lifecycleMessage != nullptr ? status->lifecycleMessage : "";
	botLibAdapterStatus.memoryMessage = status->memoryMessage != nullptr ? status->memoryMessage : "";
	botLibAdapterStatus.filesystemMessage = status->filesystemMessage != nullptr ? status->filesystemMessage : "";
	botLibAdapterStatus.aasMessage = status->aasMessage != nullptr ? status->aasMessage : "";
	botLibAdapterStatus.aasSampleMessage = status->aasSampleMessage != nullptr ? status->aasSampleMessage : "";
	botLibAdapterStatus.aasClusterMessage = status->aasClusterMessage != nullptr ? status->aasClusterMessage : "";
	botLibAdapterStatus.aasRouteMessage = status->aasRouteMessage != nullptr ? status->aasRouteMessage : "";
	botLibAdapterStatus.aasAltRouteMessage = status->aasAltRouteMessage != nullptr ? status->aasAltRouteMessage : "";
	botLibAdapterStatus.aasMovementMessage = status->aasMovementMessage != nullptr ? status->aasMovementMessage : "";
	botLibAdapterStatus.aasStartFrameMessage = status->aasStartFrameMessage != nullptr ? status->aasStartFrameMessage : "";
	botLibAdapterStatus.entitySyncMessage = status->entitySyncMessage != nullptr ? status->entitySyncMessage : "";
	botLibAdapterStatus.entityTraceMessage = status->entityTraceMessage != nullptr ? status->entityTraceMessage : "";
	botLibAdapterStatus.botClientCommandMessage =
		status->botClientCommandMessage != nullptr ? status->botClientCommandMessage : "";
	botLibAdapterStatus.debugDrawMessage = status->debugDrawMessage != nullptr ? status->debugDrawMessage : "";
	botLibAdapterStatus.debugPolygonMessage = status->debugPolygonMessage != nullptr ? status->debugPolygonMessage : "";
	botLibAdapterStatus.debugAreaMessage = status->debugAreaMessage != nullptr ? status->debugAreaMessage : "";
	botLibAdapterStatus.routeOverlayMessage = status->routeOverlayMessage != nullptr ? status->routeOverlayMessage : "";
	botLibAdapterStatus.angleVectorsMessage = status->angleVectorsMessage != nullptr ? status->angleVectorsMessage : "";
	botLibAdapterStatus.bspEntityMessage = status->bspEntityMessage != nullptr ? status->bspEntityMessage : "";
	botLibAdapterStatus.bspModelMessage = status->bspModelMessage != nullptr ? status->bspModelMessage : "";
	botLibAdapterStatus.bspCollisionMessage = status->bspCollisionMessage != nullptr ? status->bspCollisionMessage : "";
	botLibAdapterStatus.bspLeafLinkMessage = status->bspLeafLinkMessage != nullptr ? status->bspLeafLinkMessage : "";
	botLibAdapterStatus.bspVisibilityMessage = status->bspVisibilityMessage != nullptr ? status->bspVisibilityMessage : "";
}
} // namespace

void BotLibAdapter_Init() {
	const Q3ABotLibBoundaryInfo &boundary = Q3A_BotLibBoundaryInfo();

	botLibAdapterStatus.initialized = true;
	botLibAdapterStatus.q3aRuntimeImported = boundary.runtimeImported;
	Q3A_BotLibImport_SetPrintCallback(botLibAdapterPrintCallback != nullptr ? BotLibAdapter_Q3APrint : nullptr);
	Q3A_BotLibImport_SetBotClientCommandCallback(
		botLibAdapterBotClientCommandCallback != nullptr ? BotLibAdapter_Q3ABotClientCommand : nullptr);
	Q3A_BotLibImport_SetFilesystemCallbacks(
		botLibAdapterFilesystemLoadCallback != nullptr ? BotLibAdapter_Q3AFilesystemLoad : nullptr,
		botLibAdapterFilesystemFreeCallback != nullptr ? BotLibAdapter_Q3AFilesystemFree : nullptr);
	Q3A_BotLibImport_SetEntityTraceCallback(
		botLibAdapterEntityTraceCallback != nullptr ? BotLibAdapter_Q3AEntityTrace : nullptr);
	Q3A_BotLibImport_SetDebugDrawCallback(
		botLibAdapterDebugDrawCallback != nullptr ? BotLibAdapter_Q3ADebugDraw : nullptr);
	Q3A_BotLibImport_SetDebugPolygonCallback(
		botLibAdapterDebugPolygonCallback != nullptr ? BotLibAdapter_Q3ADebugPolygon : nullptr);
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

void BotLibAdapter_SetPrintCallback(BotLibAdapterPrintCallback callback) {
	botLibAdapterPrintCallback = callback;
	Q3A_BotLibImport_SetPrintCallback(botLibAdapterPrintCallback != nullptr ? BotLibAdapter_Q3APrint : nullptr);
	CopyImportStatus();
}

void BotLibAdapter_SetBotClientCommandCallback(BotLibAdapterBotClientCommandCallback callback) {
	botLibAdapterBotClientCommandCallback = callback;
	Q3A_BotLibImport_SetBotClientCommandCallback(
		botLibAdapterBotClientCommandCallback != nullptr ? BotLibAdapter_Q3ABotClientCommand : nullptr);
	CopyImportStatus();
}

void BotLibAdapter_SetFilesystemCallbacks(
	BotLibAdapterFilesystemLoadCallback loadCallback,
	BotLibAdapterFilesystemFreeCallback freeCallback) {
	botLibAdapterFilesystemLoadCallback = loadCallback;
	botLibAdapterFilesystemFreeCallback = freeCallback;
	Q3A_BotLibImport_SetFilesystemCallbacks(
		botLibAdapterFilesystemLoadCallback != nullptr ? BotLibAdapter_Q3AFilesystemLoad : nullptr,
		botLibAdapterFilesystemFreeCallback != nullptr ? BotLibAdapter_Q3AFilesystemFree : nullptr);
	CopyImportStatus();
}

void BotLibAdapter_SetEntityTraceCallback(BotLibAdapterEntityTraceCallback callback) {
	botLibAdapterEntityTraceCallback = callback;
	Q3A_BotLibImport_SetEntityTraceCallback(
		botLibAdapterEntityTraceCallback != nullptr ? BotLibAdapter_Q3AEntityTrace : nullptr);
	CopyImportStatus();
}

void BotLibAdapter_SetDebugDrawCallback(BotLibAdapterDebugDrawCallback callback) {
	botLibAdapterDebugDrawCallback = callback;
	Q3A_BotLibImport_SetDebugDrawCallback(
		botLibAdapterDebugDrawCallback != nullptr ? BotLibAdapter_Q3ADebugDraw : nullptr);
	CopyImportStatus();
}

void BotLibAdapter_SetDebugPolygonCallback(BotLibAdapterDebugPolygonCallback callback) {
	botLibAdapterDebugPolygonCallback = callback;
	Q3A_BotLibImport_SetDebugPolygonCallback(
		botLibAdapterDebugPolygonCallback != nullptr ? BotLibAdapter_Q3ADebugPolygon : nullptr);
	CopyImportStatus();
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
	Q3A_BotLibImport_StartFrame(runtimeMilliseconds);
	CopyImportStatus();
}

bool BotLibAdapter_RunDebugDrawSmoke() {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}
	const bool passed = Q3A_BotLibImport_RunDebugDrawSmoke() != 0;
	CopyImportStatus();
	return passed;
}

bool BotLibAdapter_RunDebugPolygonSmoke() {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}
	const bool passed = Q3A_BotLibImport_RunDebugPolygonSmoke() != 0;
	CopyImportStatus();
	return passed;
}

bool BotLibAdapter_RunDebugAreaSmoke() {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}
	const bool passed = Q3A_BotLibImport_RunDebugAreaSmoke() != 0;
	CopyImportStatus();
	return passed;
}

bool BotLibAdapter_RunRouteOverlaySmoke() {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}
	const bool passed = Q3A_BotLibImport_RunRouteOverlaySmoke() != 0;
	CopyImportStatus();
	return passed;
}

bool BotLibAdapter_RunBotClientCommandSmoke() {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}
	const bool passed = Q3A_BotLibImport_RunBotClientCommandSmoke() != 0;
	CopyImportStatus();
	return passed;
}

bool BotLibAdapter_BeginEntitySync(int entityCount) {
	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}
	const bool started = Q3A_BotLibImport_BeginEntitySync(entityCount) != 0;
	CopyImportStatus();
	return started;
}

bool BotLibAdapter_UpdateEntity(int entnum, const BotLibAdapterEntitySnapshot *state) {
	Q3ABotLibImportEntityState q3aState{};
	const Q3ABotLibImportEntityState *q3aStatePtr = nullptr;

	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}

	if (state != nullptr) {
		q3aState.type = state->type;
		q3aState.flags = state->flags;
		for (int i = 0; i < 3; ++i) {
			q3aState.origin[i] = state->origin[i];
			q3aState.angles[i] = state->angles[i];
			q3aState.oldOrigin[i] = state->oldOrigin[i];
			q3aState.mins[i] = state->mins[i];
			q3aState.maxs[i] = state->maxs[i];
		}
		q3aState.groundent = state->groundent;
		q3aState.solid = state->solid;
		q3aState.modelindex = state->modelindex;
		q3aState.modelindex2 = state->modelindex2;
		q3aState.frame = state->frame;
		q3aState.event = state->event;
		q3aState.eventParm = state->eventParm;
		q3aState.powerups = state->powerups;
		q3aState.weapon = state->weapon;
		q3aState.legsAnim = state->legsAnim;
		q3aState.torsoAnim = state->torsoAnim;
		q3aStatePtr = &q3aState;
	}

	const bool updated = Q3A_BotLibImport_UpdateEntity(entnum, q3aStatePtr) != 0;
	CopyImportStatus();
	return updated;
}

void BotLibAdapter_FinishEntitySync() {
	Q3A_BotLibImport_FinishEntitySync();
	CopyImportStatus();
}

bool BotLibAdapter_BuildRouteSteer(
	const float origin[3],
	int preferredGoalArea,
	BotLibAdapterRouteSteer *result) {
	Q3ABotLibImportRouteSteerResult q3aResult{};

	if (!botLibAdapterStatus.initialized) {
		BotLibAdapter_Init();
	}

	const bool routed = Q3A_BotLibImport_BuildRouteSteer(origin, preferredGoalArea, &q3aResult) != 0;
	if (result != nullptr) {
		result->success = q3aResult.success != 0;
		result->startArea = q3aResult.startArea;
		result->goalArea = q3aResult.goalArea;
		result->routeEndArea = q3aResult.routeEndArea;
		result->travelTime = q3aResult.travelTime;
		result->reachability = q3aResult.reachability;
		result->reachabilityTravelType = q3aResult.reachabilityTravelType;
		result->reachabilityTravelFlags = q3aResult.reachabilityTravelFlags;
		result->reachabilityEndArea = q3aResult.reachabilityEndArea;
		result->stopEvent = q3aResult.stopEvent;
		result->routePointCount = q3aResult.routePointCount;
		if (result->routePointCount < 0) {
			result->routePointCount = 0;
		} else if (result->routePointCount > BOTLIB_ADAPTER_MAX_ROUTE_POINTS) {
			result->routePointCount = BOTLIB_ADAPTER_MAX_ROUTE_POINTS;
		}
		for (int i = 0; i < 3; ++i) {
			result->moveTarget[i] = q3aResult.moveTarget[i];
			result->goalOrigin[i] = q3aResult.goalOrigin[i];
		}
		for (int pointIndex = 0; pointIndex < result->routePointCount; ++pointIndex) {
			for (int axis = 0; axis < 3; ++axis) {
				result->routePoints[pointIndex][axis] = q3aResult.routePoints[pointIndex][axis];
			}
		}
	}
	return routed;
}

const BotLibAdapterStatus &BotLibAdapter_GetStatus() {
	return botLibAdapterStatus;
}
