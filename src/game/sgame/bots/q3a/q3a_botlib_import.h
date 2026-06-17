// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Q3ABotLibImportSmokeStatus {
	int initialized;
	int lifecycleInitCount;
	int lifecycleShutdownCount;
	int lifecycleLoadAttempts;
	int lifecycleLoadSuccesses;
	int lifecycleActiveUnloads;
	int lifecycleCleanUnloads;
	int lifecycleUnloadFailures;
	int lifecycleLastUnloadZoneActiveBytes;
	int lifecycleLastUnloadHunkActiveBytes;
	int lifecycleLastUnloadOpenFiles;
	int lifecyclePersistentZoneBytes;
	int libvarSmokePassed;
	int printCallbackSet;
	int printMessages;
	int printWarnings;
	int printErrors;
	int printFatals;
	int printLastType;
	int botClientCommandCallbackSet;
	int botClientCommandAttempted;
	int botClientCommandSmokePassed;
	int botClientCommandClient;
	int botClientCommandAccepted;
	int botClientCommandRejected;
	int botClientCommandFailures;
	int aasLoadAttempted;
	int aasLoaded;
	int aasLoadResult;
	int aasBspChecksum;
	int aasAreas;
	int aasReachability;
	int aasClusters;
	int aasSampleAttempted;
	int aasSamplePassed;
	int aasSampleArea;
	int aasSamplePointArea;
	int aasSamplePresenceType;
	int aasSampleCluster;
	int aasSampleReachability;
	int aasClusterAttempted;
	int aasClusterPassed;
	int aasClusterArea;
	int aasClusterCluster;
	int aasClusterNumClusters;
	int aasClusterAreas;
	int aasClusterReachabilityAreas;
	int aasClusterFailures;
	int aasRouteAttempted;
	int aasRoutePassed;
	int aasRouteStartArea;
	int aasRouteGoalArea;
	int aasRouteTravelTime;
	int aasRouteReachability;
	int aasRouteEndArea;
	int aasRouteStopEvent;
	int aasAltRouteAttempted;
	int aasAltRoutePassed;
	int aasAltRouteStartArea;
	int aasAltRouteGoalArea;
	int aasAltRouteGoals;
	int aasAltRouteFirstArea;
	int aasAltRouteFirstStartTravelTime;
	int aasAltRouteFirstGoalTravelTime;
	int aasAltRouteFirstExtraTravelTime;
	int aasAltRouteFailures;
	int aasMovementAttempted;
	int aasMovementPassed;
	int aasMovementEndArea;
	int aasMovementStopEvent;
	int aasMovementFrames;
	int aasMovementDropToFloorPassed;
	int aasMovementJumpVelocityPassed;
	int aasStartFrameAttempted;
	int aasStartFramePassed;
	int aasStartFrameResult;
	int aasStartFrameCount;
	int aasStartFrameTimeMilliseconds;
	int entitySyncAttempted;
	int entitySyncPassed;
	int entitySyncUpdated;
	int entitySyncUnlinked;
	int entitySyncSkipped;
	int entitySyncFailures;
	int entitySyncMaxEntities;
	int entityTraceCallbackSet;
	int entityTraceAttempted;
	int entityTraceHits;
	int entityTraceMisses;
	int entityTraceFailures;
	int debugDrawCallbackSet;
	int debugDrawAttempted;
	int debugDrawPassed;
	int debugDrawLines;
	int debugDrawCrosses;
	int debugDrawArrows;
	int debugDrawClears;
	int debugDrawFailures;
	int debugPolygonCallbackSet;
	int debugPolygonAttempted;
	int debugPolygonPassed;
	int debugPolygonCreates;
	int debugPolygonDeletes;
	int debugPolygonPoints;
	int debugPolygonLastId;
	int debugPolygonFailures;
	int debugAreaAttempted;
	int debugAreaPassed;
	int debugAreaArea;
	int debugAreaLines;
	int debugAreaPolygonCreates;
	int debugAreaPolygonDeletes;
	int debugAreaFailures;
	int routeOverlayAttempted;
	int routeOverlayPassed;
	int routeOverlayStartArea;
	int routeOverlayGoalArea;
	int routeOverlayEndArea;
	int routeOverlayTravelTime;
	int routeOverlayReachability;
	int routeOverlayLines;
	int routeOverlayCrosses;
	int routeOverlayArrows;
	int routeOverlayClears;
	int routeOverlayFailures;
	int angleVectorsSmokePassed;
	int runtimeMilliseconds;
	int bspEntityLoadAttempted;
	int bspEntityLoaded;
	int bspEntityCount;
	int bspEntityPairs;
	int bspEntityValueSmokePassed;
	int bspModelLoadAttempted;
	int bspModelLoaded;
	int bspModelCount;
	int bspModelBoundsSmokePassed;
	int bspCollisionLoadAttempted;
	int bspCollisionLoaded;
	int bspCollisionPlanes;
	int bspCollisionNodes;
	int bspCollisionLeafs;
	int bspCollisionBrushes;
	int bspCollisionPointContentsSmokePassed;
	int bspCollisionTraceSmokePassed;
	int bspLeafLinkAttempted;
	int bspLeafLinks;
	int bspLeafLinkFailures;
	int bspBoxEntitiesSmokePassed;
	int bspBoxEntitiesCount;
	int bspVisibilityLoadAttempted;
	int bspVisibilityLoaded;
	int bspVisibilityClusters;
	int bspVisibilityPvsSmokePassed;
	int bspVisibilityPhsSmokePassed;
	int memoryZoneActiveBytes;
	int memoryZonePeakBytes;
	int memoryZoneAllocations;
	int memoryZoneFrees;
	int memoryHunkActiveBytes;
	int memoryHunkPeakBytes;
	int memoryHunkAllocations;
	int memoryHunkGroupFrees;
	int memoryFailures;
	int availableMemory;
	int filesystemCallbackSet;
	int filesystemAttempted;
	int filesystemPassed;
	int filesystemOpenAttempts;
	int filesystemOpenFiles;
	int filesystemOpenMemoryFiles;
	int filesystemOpenFailures;
	int filesystemRouteCacheMisses;
	int filesystemReadBytes;
	int filesystemSeekCount;
	int filesystemCloseCount;
	int filesystemWriteRejected;
	const char *lifecycleMessage;
	const char *message;
	const char *memoryMessage;
	const char *filesystemMessage;
	const char *aasMessage;
	const char *aasSampleMessage;
	const char *aasClusterMessage;
	const char *aasRouteMessage;
	const char *aasAltRouteMessage;
	const char *aasMovementMessage;
	const char *aasStartFrameMessage;
	const char *entitySyncMessage;
	const char *entityTraceMessage;
	const char *botClientCommandMessage;
	const char *debugDrawMessage;
	const char *debugPolygonMessage;
	const char *debugAreaMessage;
	const char *routeOverlayMessage;
	const char *angleVectorsMessage;
	const char *bspEntityMessage;
	const char *bspModelMessage;
	const char *bspCollisionMessage;
	const char *bspLeafLinkMessage;
	const char *bspVisibilityMessage;
} Q3ABotLibImportSmokeStatus;

typedef void (*Q3ABotLibImportPrintCallback)(int type, const char *message);
typedef int (*Q3ABotLibImportBotClientCommandCallback)(int client, const char *command);
typedef int (*Q3ABotLibImportFilesystemLoadCallback)(const char *path, const unsigned char **data);
typedef void (*Q3ABotLibImportFilesystemFreeCallback)(const unsigned char *data);

enum {
	Q3A_BOT_ENTITY_GENERAL = 0,
	Q3A_BOT_ENTITY_PLAYER = 1,
	Q3A_BOT_ENTITY_ITEM = 2,
	Q3A_BOT_ENTITY_MISSILE = 3,
	Q3A_BOT_ENTITY_MOVER = 4,
};

typedef struct Q3ABotLibImportEntityState {
	int type;
	int flags;
	float origin[3];
	float angles[3];
	float oldOrigin[3];
	float mins[3];
	float maxs[3];
	int groundent;
	int solid;
	int modelindex;
	int modelindex2;
	int frame;
	int event;
	int eventParm;
	int powerups;
	int weapon;
	int legsAnim;
	int torsoAnim;
} Q3ABotLibImportEntityState;

typedef struct Q3ABotLibImportTraceResult {
	int hit;
	int allSolid;
	int startSolid;
	float fraction;
	float endPos[3];
	float planeNormal[3];
	float planeDist;
	int contents;
	int entnum;
} Q3ABotLibImportTraceResult;

#define Q3A_BOTLIB_IMPORT_MAX_ROUTE_POINTS 8

typedef struct Q3ABotLibImportRouteSteerResult {
	int success;
	int startArea;
	int goalArea;
	int routeEndArea;
	int travelTime;
	int reachability;
	int reachabilityTravelType;
	int reachabilityTravelFlags;
	int reachabilityEndArea;
	int stopEvent;
	int routePointCount;
	float moveTarget[3];
	float goalOrigin[3];
	float routePoints[Q3A_BOTLIB_IMPORT_MAX_ROUTE_POINTS][3];
} Q3ABotLibImportRouteSteerResult;

typedef int (*Q3ABotLibImportEntityTraceCallback)(
	int entnum,
	const float start[3],
	const float mins[3],
	const float maxs[3],
	const float end[3],
	int contentmask,
	Q3ABotLibImportTraceResult *trace);

enum {
	Q3A_BOTLIB_DEBUG_DRAW_CLEAR = 0,
	Q3A_BOTLIB_DEBUG_DRAW_LINE = 1,
	Q3A_BOTLIB_DEBUG_DRAW_PERMANENT_LINE = 2,
	Q3A_BOTLIB_DEBUG_DRAW_CROSS = 3,
	Q3A_BOTLIB_DEBUG_DRAW_ARROW = 4,
};

typedef int (*Q3ABotLibImportDebugDrawCallback)(
	int primitive,
	const float start[3],
	const float end[3],
	float size,
	int color,
	int secondaryColor);

typedef int (*Q3ABotLibImportDebugPolygonCallback)(int color, int numPoints, const float *points);

void Q3A_BotLibImport_Init(void);
void Q3A_BotLibImport_Shutdown(void);
void Q3A_BotLibImport_SetPrintCallback(Q3ABotLibImportPrintCallback callback);
void Q3A_BotLibImport_SetBotClientCommandCallback(Q3ABotLibImportBotClientCommandCallback callback);
void Q3A_BotLibImport_SetFilesystemCallbacks(
	Q3ABotLibImportFilesystemLoadCallback loadCallback,
	Q3ABotLibImportFilesystemFreeCallback freeCallback);
void Q3A_BotLibImport_SetEntityTraceCallback(Q3ABotLibImportEntityTraceCallback callback);
void Q3A_BotLibImport_SetDebugDrawCallback(Q3ABotLibImportDebugDrawCallback callback);
void Q3A_BotLibImport_SetDebugPolygonCallback(Q3ABotLibImportDebugPolygonCallback callback);
int Q3A_BotLibImport_RunLibVarSmoke(void);
int Q3A_BotLibImport_RunDebugDrawSmoke(void);
int Q3A_BotLibImport_RunDebugPolygonSmoke(void);
int Q3A_BotLibImport_RunDebugAreaSmoke(void);
int Q3A_BotLibImport_RunRouteOverlaySmoke(void);
int Q3A_BotLibImport_RunBotClientCommandSmoke(void);
int Q3A_BotLibImport_LoadAASBuffer(const char *name, const void *data, int length, int bspChecksum);
int Q3A_BotLibImport_LoadBspEntityData(const char *name, const void *data, int length);
int Q3A_BotLibImport_LoadBspModelData(const char *name, const void *data, int length);
int Q3A_BotLibImport_LoadBspCollisionData(const char *name, const void *data, int length);
int Q3A_BotLibImport_LoadBspVisibilityData(const char *name, const void *data, int length);
void Q3A_BotLibImport_SetMilliseconds(int milliseconds);
int Q3A_BotLibImport_StartFrame(int milliseconds);
int Q3A_BotLibImport_BeginEntitySync(int entityCount);
int Q3A_BotLibImport_UpdateEntity(int entnum, const Q3ABotLibImportEntityState *state);
void Q3A_BotLibImport_FinishEntitySync(void);
int Q3A_BotLibImport_BuildRouteSteer(
	const float origin[3],
	int preferredGoalArea,
	Q3ABotLibImportRouteSteerResult *result);
void Q3A_BotLibImport_ClearBspEntityData(void);
void Q3A_BotLibImport_ClearBspModelData(void);
void Q3A_BotLibImport_ClearBspCollisionData(void);
void Q3A_BotLibImport_ClearBspVisibilityData(void);
void Q3A_BotLibImport_UnloadAAS(void);
const Q3ABotLibImportSmokeStatus *Q3A_BotLibImport_SmokeStatus(void);

#ifdef __cplusplus
}
#endif
