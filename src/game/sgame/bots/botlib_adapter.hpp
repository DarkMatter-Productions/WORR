// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

#include <cstddef>
#include <string>

enum BotLibAdapterEntityType {
	BotLibAdapterEntityGeneral = 0,
	BotLibAdapterEntityPlayer = 1,
	BotLibAdapterEntityItem = 2,
	BotLibAdapterEntityMissile = 3,
	BotLibAdapterEntityMover = 4,
};

constexpr int BOTLIB_ADAPTER_MAX_ROUTE_POINTS = 8;

struct BotLibAdapterEntitySnapshot {
	int type = 0;
	int flags = 0;
	float origin[3] = {};
	float angles[3] = {};
	float oldOrigin[3] = {};
	float mins[3] = {};
	float maxs[3] = {};
	int groundent = 0;
	int solid = 0;
	int modelindex = 0;
	int modelindex2 = 0;
	int frame = 0;
	int event = 0;
	int eventParm = 0;
	int powerups = 0;
	int weapon = 0;
	int legsAnim = 0;
	int torsoAnim = 0;
};

struct BotLibAdapterRouteSteer {
	bool success = false;
	int startArea = 0;
	int goalArea = 0;
	int routeEndArea = 0;
	int travelTime = 0;
	int reachability = 0;
	int reachabilityTravelType = 0;
	int reachabilityTravelFlags = 0;
	int reachabilityEndArea = 0;
	int stopEvent = 0;
	int routePointCount = 0;
	float moveTarget[3] = {};
	float goalOrigin[3] = {};
	float routePoints[BOTLIB_ADAPTER_MAX_ROUTE_POINTS][3] = {};
};

struct BotLibAdapterTraceResult {
	bool hit = false;
	bool allSolid = false;
	bool startSolid = false;
	float fraction = 1.0f;
	float endPos[3] = {};
	float planeNormal[3] = {};
	float planeDist = 0.0f;
	int contents = 0;
	int entnum = 0;
};

using BotLibAdapterEntityTraceCallback = bool (*)(
	int entnum,
	const float start[3],
	const float mins[3],
	const float maxs[3],
	const float end[3],
	int contentmask,
	BotLibAdapterTraceResult *trace);

using BotLibAdapterPrintCallback = void (*)(int type, const char *message);
using BotLibAdapterBotClientCommandCallback = bool (*)(int client, const char *command);
using BotLibAdapterFilesystemLoadCallback = int (*)(const char *path, const unsigned char **data);
using BotLibAdapterFilesystemFreeCallback = void (*)(const unsigned char *data);

enum BotLibAdapterDebugDrawPrimitive {
	BotLibAdapterDebugDrawClear = 0,
	BotLibAdapterDebugDrawLine = 1,
	BotLibAdapterDebugDrawPermanentLine = 2,
	BotLibAdapterDebugDrawCross = 3,
	BotLibAdapterDebugDrawArrow = 4,
};

using BotLibAdapterDebugDrawCallback = bool (*)(
	int primitive,
	const float start[3],
	const float end[3],
	float size,
	int color,
	int secondaryColor);

using BotLibAdapterDebugPolygonCallback = bool (*)(int color, int pointCount, const float *points);

struct BotLibAdapterStatus {
	bool initialized = false;
	bool q3aRuntimeImported = false;
	bool q3aUtilitySmokePassed = false;
	bool q3aPrintCallbackSet = false;
	bool q3aBotClientCommandCallbackSet = false;
	bool q3aBotClientCommandAttempted = false;
	bool q3aBotClientCommandSmokePassed = false;
	bool q3aAasLoadAttempted = false;
	bool q3aAasLoaded = false;
	bool q3aAasSampleAttempted = false;
	bool q3aAasSamplePassed = false;
	bool q3aAasClusterAttempted = false;
	bool q3aAasClusterPassed = false;
	bool q3aAasRouteAttempted = false;
	bool q3aAasRoutePassed = false;
	bool q3aAasAltRouteAttempted = false;
	bool q3aAasAltRoutePassed = false;
	bool q3aAasMovementAttempted = false;
	bool q3aAasMovementPassed = false;
	bool q3aAasMovementDropToFloorPassed = false;
	bool q3aAasMovementJumpVelocityPassed = false;
	bool q3aAasStartFrameAttempted = false;
	bool q3aAasStartFramePassed = false;
	bool q3aEntitySyncAttempted = false;
	bool q3aEntitySyncPassed = false;
	bool q3aEntityTraceCallbackSet = false;
	bool q3aDebugDrawCallbackSet = false;
	bool q3aDebugDrawAttempted = false;
	bool q3aDebugDrawPassed = false;
	bool q3aDebugPolygonCallbackSet = false;
	bool q3aDebugPolygonAttempted = false;
	bool q3aDebugPolygonPassed = false;
	bool q3aDebugAreaAttempted = false;
	bool q3aDebugAreaPassed = false;
	bool q3aRouteOverlayAttempted = false;
	bool q3aRouteOverlayPassed = false;
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
	bool q3aBspLeafLinkAttempted = false;
	bool q3aBspBoxEntitiesSmokePassed = false;
	bool q3aBspVisibilityLoadAttempted = false;
	bool q3aBspVisibilityLoaded = false;
	bool q3aBspVisibilityPvsSmokePassed = false;
	bool q3aBspVisibilityPhsSmokePassed = false;
	bool q3aFilesystemCallbackSet = false;
	bool q3aFilesystemAttempted = false;
	bool q3aFilesystemPassed = false;
	bool levelActive = false;
	int q3aPrintMessages = 0;
	int q3aPrintWarnings = 0;
	int q3aPrintErrors = 0;
	int q3aPrintFatals = 0;
	int q3aPrintLastType = 0;
	int q3aBotClientCommandClient = 0;
	int q3aBotClientCommandAccepted = 0;
	int q3aBotClientCommandRejected = 0;
	int q3aBotClientCommandFailures = 0;
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
	int q3aAasClusterArea = 0;
	int q3aAasClusterCluster = 0;
	int q3aAasClusterNumClusters = 0;
	int q3aAasClusterAreas = 0;
	int q3aAasClusterReachabilityAreas = 0;
	int q3aAasClusterFailures = 0;
	int q3aAasRouteStartArea = 0;
	int q3aAasRouteGoalArea = 0;
	int q3aAasRouteTravelTime = 0;
	int q3aAasRouteReachability = 0;
	int q3aAasRouteEndArea = 0;
	int q3aAasRouteStopEvent = 0;
	int q3aAasAltRouteStartArea = 0;
	int q3aAasAltRouteGoalArea = 0;
	int q3aAasAltRouteGoals = 0;
	int q3aAasAltRouteFirstArea = 0;
	int q3aAasAltRouteFirstStartTravelTime = 0;
	int q3aAasAltRouteFirstGoalTravelTime = 0;
	int q3aAasAltRouteFirstExtraTravelTime = 0;
	int q3aAasAltRouteFailures = 0;
	int q3aAasMovementEndArea = 0;
	int q3aAasMovementStopEvent = 0;
	int q3aAasMovementFrames = 0;
	int q3aAasStartFrameResult = 0;
	int q3aAasStartFrameCount = 0;
	int q3aAasStartFrameTimeMilliseconds = 0;
	int q3aEntitySyncUpdated = 0;
	int q3aEntitySyncUnlinked = 0;
	int q3aEntitySyncSkipped = 0;
	int q3aEntitySyncFailures = 0;
	int q3aEntitySyncMaxEntities = 0;
	int q3aEntityTraceAttempted = 0;
	int q3aEntityTraceHits = 0;
	int q3aEntityTraceMisses = 0;
	int q3aEntityTraceFailures = 0;
	int q3aDebugDrawLines = 0;
	int q3aDebugDrawCrosses = 0;
	int q3aDebugDrawArrows = 0;
	int q3aDebugDrawClears = 0;
	int q3aDebugDrawFailures = 0;
	int q3aDebugPolygonCreates = 0;
	int q3aDebugPolygonDeletes = 0;
	int q3aDebugPolygonPoints = 0;
	int q3aDebugPolygonLastId = 0;
	int q3aDebugPolygonFailures = 0;
	int q3aDebugAreaArea = 0;
	int q3aDebugAreaLines = 0;
	int q3aDebugAreaPolygonCreates = 0;
	int q3aDebugAreaPolygonDeletes = 0;
	int q3aDebugAreaFailures = 0;
	int q3aRouteOverlayStartArea = 0;
	int q3aRouteOverlayGoalArea = 0;
	int q3aRouteOverlayEndArea = 0;
	int q3aRouteOverlayTravelTime = 0;
	int q3aRouteOverlayReachability = 0;
	int q3aRouteOverlayLines = 0;
	int q3aRouteOverlayCrosses = 0;
	int q3aRouteOverlayArrows = 0;
	int q3aRouteOverlayClears = 0;
	int q3aRouteOverlayFailures = 0;
	int q3aRuntimeMilliseconds = 0;
	int q3aLifecycleInitCount = 0;
	int q3aLifecycleShutdownCount = 0;
	int q3aLifecycleLoadAttempts = 0;
	int q3aLifecycleLoadSuccesses = 0;
	int q3aLifecycleActiveUnloads = 0;
	int q3aLifecycleCleanUnloads = 0;
	int q3aLifecycleUnloadFailures = 0;
	int q3aLifecycleLastUnloadZoneActiveBytes = 0;
	int q3aLifecycleLastUnloadHunkActiveBytes = 0;
	int q3aLifecycleLastUnloadOpenFiles = 0;
	int q3aLifecyclePersistentZoneBytes = 0;
	int q3aBspEntityCount = 0;
	int q3aBspEntityPairs = 0;
	int q3aBspModelCount = 0;
	int q3aBspCollisionPlanes = 0;
	int q3aBspCollisionNodes = 0;
	int q3aBspCollisionLeafs = 0;
	int q3aBspCollisionBrushes = 0;
	int q3aBspLeafLinks = 0;
	int q3aBspLeafLinkFailures = 0;
	int q3aBspBoxEntitiesCount = 0;
	int q3aBspVisibilityClusters = 0;
	int q3aMemoryZoneActiveBytes = 0;
	int q3aMemoryZonePeakBytes = 0;
	int q3aMemoryZoneAllocations = 0;
	int q3aMemoryZoneFrees = 0;
	int q3aMemoryHunkActiveBytes = 0;
	int q3aMemoryHunkPeakBytes = 0;
	int q3aMemoryHunkAllocations = 0;
	int q3aMemoryHunkGroupFrees = 0;
	int q3aMemoryFailures = 0;
	int q3aAvailableMemory = 0;
	int q3aFilesystemOpenAttempts = 0;
	int q3aFilesystemOpenFiles = 0;
	int q3aFilesystemOpenMemoryFiles = 0;
	int q3aFilesystemOpenFailures = 0;
	int q3aFilesystemRouteCacheMisses = 0;
	int q3aFilesystemReadBytes = 0;
	int q3aFilesystemSeekCount = 0;
	int q3aFilesystemCloseCount = 0;
	int q3aFilesystemWriteRejected = 0;
	std::string mapName;
	std::string aasPath;
	std::string bspPath;
	std::string message;
	std::string utilityMessage;
	std::string lifecycleMessage;
	std::string memoryMessage;
	std::string filesystemMessage;
	std::string aasMessage;
	std::string aasSampleMessage;
	std::string aasClusterMessage;
	std::string aasRouteMessage;
	std::string aasAltRouteMessage;
	std::string aasMovementMessage;
	std::string aasStartFrameMessage;
	std::string entitySyncMessage;
	std::string entityTraceMessage;
	std::string botClientCommandMessage;
	std::string debugDrawMessage;
	std::string debugPolygonMessage;
	std::string debugAreaMessage;
	std::string routeOverlayMessage;
	std::string angleVectorsMessage;
	std::string bspEntityMessage;
	std::string bspModelMessage;
	std::string bspCollisionMessage;
	std::string bspLeafLinkMessage;
	std::string bspVisibilityMessage;
	const char *sourceCommit = nullptr;
	const char *importRoot = nullptr;
	const char *buildStrategy = nullptr;
	size_t plannedImportFileCount = 0;
};

void BotLibAdapter_Init();
void BotLibAdapter_SetPrintCallback(BotLibAdapterPrintCallback callback);
void BotLibAdapter_SetBotClientCommandCallback(BotLibAdapterBotClientCommandCallback callback);
void BotLibAdapter_SetFilesystemCallbacks(
	BotLibAdapterFilesystemLoadCallback loadCallback,
	BotLibAdapterFilesystemFreeCallback freeCallback);
void BotLibAdapter_SetEntityTraceCallback(BotLibAdapterEntityTraceCallback callback);
void BotLibAdapter_SetDebugDrawCallback(BotLibAdapterDebugDrawCallback callback);
void BotLibAdapter_SetDebugPolygonCallback(BotLibAdapterDebugPolygonCallback callback);
void BotLibAdapter_BeginLevel(const char *mapName, const char *aasPath);
bool BotLibAdapter_LoadBspEntityData(const char *mapName, const char *bspPath, const void *data, int length);
bool BotLibAdapter_LoadBspModelData(const char *mapName, const char *bspPath, const void *data, int length);
bool BotLibAdapter_LoadBspCollisionData(const char *mapName, const char *bspPath, const void *data, int length);
bool BotLibAdapter_LoadBspVisibilityData(const char *mapName, const char *bspPath, const void *data, int length);
bool BotLibAdapter_LoadAasBuffer(const char *mapName, const char *aasPath, const void *data, int length, int bspChecksum);
void BotLibAdapter_EndLevel();
void BotLibAdapter_RunFrame(int runtimeMilliseconds);
bool BotLibAdapter_RunDebugDrawSmoke();
bool BotLibAdapter_RunDebugPolygonSmoke();
bool BotLibAdapter_RunDebugAreaSmoke();
bool BotLibAdapter_RunRouteOverlaySmoke();
bool BotLibAdapter_RunBotClientCommandSmoke();
bool BotLibAdapter_BeginEntitySync(int entityCount);
bool BotLibAdapter_UpdateEntity(int entnum, const BotLibAdapterEntitySnapshot *state);
void BotLibAdapter_FinishEntitySync();
bool BotLibAdapter_BuildRouteSteer(
	const float origin[3],
	int preferredGoalArea,
	BotLibAdapterRouteSteer *result);

const BotLibAdapterStatus &BotLibAdapter_GetStatus();
