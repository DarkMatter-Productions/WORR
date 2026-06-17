// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "q3a_botlib_import.h"

#include "game/q_shared.h"
#include "game/botlib.h"
#include "game/be_aas.h"
#include "botlib/aasfile.h"
#include "botlib/be_aas_def.h"
#include "botlib/be_aas_bsp.h"
#include "botlib/be_aas_cluster.h"
#include "botlib/be_aas_debug.h"
#include "botlib/be_aas_move.h"
#include "botlib/be_aas_reach.h"
#include "botlib/be_aas_route.h"
#include "botlib/be_aas_routealt.h"
#include "botlib/be_aas_sample.h"
#include "botlib/be_interface.h"
#include "botlib/l_libvar.h"
#include "botlib/l_log.h"
#include "botlib/l_memory.h"

#ifdef _WIN32
#include <malloc.h>
#else
#include <strings.h>
#endif
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

botlib_import_t botimport;
int bot_developer = 0;
vec3_t vec3_origin = { 0.0f, 0.0f, 0.0f };

typedef struct Q3AMemoryFile {
	const char *name;
	const unsigned char *data;
	int length;
} Q3AMemoryFile;

typedef struct Q3AFileSlot {
	const unsigned char *data;
	int length;
	int offset;
	int open;
	int callbackOwned;
} Q3AFileSlot;

typedef struct Q3ABotLibAllocation {
	unsigned int magic;
	int size;
	int hunk;
	struct Q3ABotLibAllocation *prev;
	struct Q3ABotLibAllocation *next;
} Q3ABotLibAllocation;

typedef struct Q3ABspEpair {
	char *key;
	char *value;
	struct Q3ABspEpair *next;
} Q3ABspEpair;

typedef struct Q3ABspEntity {
	Q3ABspEpair *epairs;
} Q3ABspEntity;

typedef struct Q3ABspModel {
	vec3_t mins;
	vec3_t maxs;
	vec3_t origin;
	int headnode;
	int firstface;
	int numfaces;
} Q3ABspModel;

typedef struct Q3ABspLump {
	int offset;
	int length;
} Q3ABspLump;

typedef struct Q3ABspPlane {
	vec3_t normal;
	float dist;
	int type;
} Q3ABspPlane;

typedef struct Q3ABspNode {
	int planenum;
	int children[2];
	short mins[3];
	short maxs[3];
} Q3ABspNode;

typedef struct Q3ABspLeaf {
	int contents;
	int cluster;
	int area;
	short mins[3];
	short maxs[3];
	int firstleafbrush;
	int numleafbrushes;
} Q3ABspLeaf;

typedef struct Q3ABspBrushSide {
	int planenum;
	int texinfo;
} Q3ABspBrushSide;

typedef struct Q3ABspBrush {
	int firstside;
	int numsides;
	int contents;
} Q3ABspBrush;

enum {
	Q3A_MEMORY_FILE_HANDLE = 1,
	Q3A_MAX_FILE_HANDLES = 16,
	Q3A_BOTLIB_MEMORY_MAGIC = 0x57424c4du,
	Q3A_BOTLIB_MEMORY_BUDGET_BYTES = 16 * 1024 * 1024,
	Q3A_MAX_BSP_ENTITIES = 8192,
	Q3A_MAX_BSP_MODELS = 4096,
	Q3A_Q2_BSP_ID = ('P' << 24) + ('S' << 16) + ('B' << 8) + 'I',
	Q3A_Q2_BSP_VERSION = 38,
	Q3A_Q2_BSP_LUMP_COUNT = 19,
	Q3A_Q2_BSP_HEADER_SIZE = 8 + Q3A_Q2_BSP_LUMP_COUNT * 8,
	Q3A_Q2_BSP_LUMP_PLANES = 1,
	Q3A_Q2_BSP_LUMP_VISIBILITY = 3,
	Q3A_Q2_BSP_LUMP_NODES = 4,
	Q3A_Q2_BSP_LUMP_LEAFS = 8,
	Q3A_Q2_BSP_LUMP_LEAFBRUSHES = 10,
	Q3A_Q2_BSP_LUMP_MODELS = 13,
	Q3A_Q2_BSP_LUMP_BRUSHES = 14,
	Q3A_Q2_BSP_LUMP_BRUSHSIDES = 15,
	Q3A_Q2_BSP_PLANE_SIZE = 20,
	Q3A_Q2_BSP_NODE_SIZE = 28,
	Q3A_Q2_BSP_LEAF_SIZE = 28,
	Q3A_Q2_BSP_LEAFBRUSH_SIZE = 2,
	Q3A_Q2_BSP_MODEL_SIZE = 48,
	Q3A_Q2_BSP_BRUSH_SIZE = 12,
	Q3A_Q2_BSP_BRUSHSIDE_SIZE = 4,
	Q3A_Q2_DVIS_PVS = 0,
	Q3A_Q2_DVIS_PHS = 1,
	Q3A_Q2_CONTENTS_SOLID = 1,
};

static Q3AMemoryFile q3aMemoryFile;
static Q3AFileSlot q3aFileSlots[Q3A_MAX_FILE_HANDLES];
static Q3ABotLibAllocation *q3aBotLibZoneAllocations;
static Q3ABotLibAllocation *q3aBotLibHunkAllocations;
static Q3ABspEntity *q3aBspEntities;
static int q3aBspEntityCount;
static int q3aBspEntityPairs;
static Q3ABspModel *q3aBspModels;
static int q3aBspModelCount;
static Q3ABspPlane *q3aBspPlanes;
static int q3aBspPlaneCount;
static Q3ABspNode *q3aBspNodes;
static int q3aBspNodeCount;
static Q3ABspLeaf *q3aBspLeafs;
static int q3aBspLeafCount;
static unsigned short *q3aBspLeafBrushes;
static int q3aBspLeafBrushCount;
static bsp_link_t **q3aBspLeafLinkedEntities;
static int q3aBspLeafLinkCount;
static Q3ABspBrushSide *q3aBspBrushSides;
static int q3aBspBrushSideCount;
static Q3ABspBrush *q3aBspBrushes;
static int q3aBspBrushCount;
static int *q3aBspBrushCheckCounts;
static int q3aBspBrushCheckCountSize;
static int q3aBspTraceCheckCount = 1;
static bsp_trace_t *q3aBspTrace;
static int q3aBspTraceContents;
static qboolean q3aBspTraceIsPoint;
static vec3_t q3aBspTraceStart;
static vec3_t q3aBspTraceEnd;
static vec3_t q3aBspTraceExtents;
static vec3_t q3aBspTraceOffsets[8];
static unsigned char *q3aBspVisData;
static int q3aBspVisLength;
static int *q3aBspVisOffsets;
static int q3aBspVisClusterCount;
static Q3ABotLibImportPrintCallback q3aPrintCallback;
static Q3ABotLibImportBotClientCommandCallback q3aBotClientCommandCallback;
static Q3ABotLibImportFilesystemLoadCallback q3aFilesystemLoadCallback;
static Q3ABotLibImportFilesystemFreeCallback q3aFilesystemFreeCallback;
static Q3ABotLibImportEntityTraceCallback q3aEntityTraceCallback;
static Q3ABotLibImportDebugDrawCallback q3aDebugDrawCallback;
static Q3ABotLibImportDebugPolygonCallback q3aDebugPolygonCallback;
static int q3aDebugLineNextId = 1;
static int q3aDebugPolygonNextId = 1;
static char q3aPrintMessage[512];
static char q3aAasSampleMessage[512];
static char q3aAasClusterMessage[512];
static char q3aAasRouteMessage[512];
static char q3aAasAltRouteMessage[512];
static char q3aAasMovementMessage[512];
static char q3aAasStartFrameMessage[512];
static char q3aEntitySyncMessage[512];
static char q3aEntityTraceMessage[512];
static char q3aBotClientCommandMessage[512];
static char q3aMemoryMessage[512];
static char q3aLifecycleMessage[512];
static char q3aFilesystemMessage[512];
static char q3aDebugDrawMessage[512];
static char q3aDebugPolygonMessage[512];
static char q3aDebugAreaMessage[512];
static char q3aRouteOverlayMessage[512];
static char q3aBspEntityMessage[512];
static char q3aBspModelMessage[512];
static char q3aBspCollisionMessage[512];
static char q3aBspLeafLinkMessage[512];
static char q3aBspVisibilityMessage[512];

static Q3ABotLibImportSmokeStatus q3aSmokeStatus = {
	.initialized = qfalse,
	.lifecycleInitCount = 0,
	.lifecycleShutdownCount = 0,
	.lifecycleLoadAttempts = 0,
	.lifecycleLoadSuccesses = 0,
	.lifecycleActiveUnloads = 0,
	.lifecycleCleanUnloads = 0,
	.lifecycleUnloadFailures = 0,
	.lifecycleLastUnloadZoneActiveBytes = 0,
	.lifecycleLastUnloadHunkActiveBytes = 0,
	.lifecycleLastUnloadOpenFiles = 0,
	.lifecyclePersistentZoneBytes = 0,
	.libvarSmokePassed = qfalse,
	.printCallbackSet = qfalse,
	.printMessages = 0,
	.printWarnings = 0,
	.printErrors = 0,
	.printFatals = 0,
	.printLastType = 0,
	.botClientCommandCallbackSet = qfalse,
	.botClientCommandAttempted = qfalse,
	.botClientCommandSmokePassed = qfalse,
	.botClientCommandClient = -1,
	.botClientCommandAccepted = 0,
	.botClientCommandRejected = 0,
	.botClientCommandFailures = 0,
	.aasLoadAttempted = qfalse,
	.aasLoaded = qfalse,
	.aasLoadResult = BLERR_NOAASFILE,
	.aasBspChecksum = 0,
	.aasAreas = 0,
	.aasReachability = 0,
	.aasClusters = 0,
	.aasSampleAttempted = qfalse,
	.aasSamplePassed = qfalse,
	.aasSampleArea = 0,
	.aasSamplePointArea = 0,
	.aasSamplePresenceType = 0,
	.aasSampleCluster = 0,
	.aasSampleReachability = 0,
	.aasClusterAttempted = qfalse,
	.aasClusterPassed = qfalse,
	.aasClusterArea = 0,
	.aasClusterCluster = 0,
	.aasClusterNumClusters = 0,
	.aasClusterAreas = 0,
	.aasClusterReachabilityAreas = 0,
	.aasClusterFailures = 0,
	.aasRouteAttempted = qfalse,
	.aasRoutePassed = qfalse,
	.aasRouteStartArea = 0,
	.aasRouteGoalArea = 0,
	.aasRouteTravelTime = 0,
	.aasRouteReachability = 0,
	.aasRouteEndArea = 0,
	.aasRouteStopEvent = 0,
	.aasAltRouteAttempted = qfalse,
	.aasAltRoutePassed = qfalse,
	.aasAltRouteStartArea = 0,
	.aasAltRouteGoalArea = 0,
	.aasAltRouteGoals = 0,
	.aasAltRouteFirstArea = 0,
	.aasAltRouteFirstStartTravelTime = 0,
	.aasAltRouteFirstGoalTravelTime = 0,
	.aasAltRouteFirstExtraTravelTime = 0,
	.aasAltRouteFailures = 0,
	.aasMovementAttempted = qfalse,
	.aasMovementPassed = qfalse,
	.aasMovementEndArea = 0,
	.aasMovementStopEvent = 0,
	.aasMovementFrames = 0,
	.aasMovementDropToFloorPassed = qfalse,
	.aasMovementJumpVelocityPassed = qfalse,
	.aasStartFrameAttempted = qfalse,
	.aasStartFramePassed = qfalse,
	.aasStartFrameResult = 0,
	.aasStartFrameCount = 0,
	.aasStartFrameTimeMilliseconds = 0,
	.entitySyncAttempted = qfalse,
	.entitySyncPassed = qfalse,
	.entitySyncUpdated = 0,
	.entitySyncUnlinked = 0,
	.entitySyncSkipped = 0,
	.entitySyncFailures = 0,
	.entitySyncMaxEntities = 0,
	.entityTraceCallbackSet = qfalse,
	.entityTraceAttempted = 0,
	.entityTraceHits = 0,
	.entityTraceMisses = 0,
	.entityTraceFailures = 0,
	.debugDrawCallbackSet = qfalse,
	.debugDrawAttempted = qfalse,
	.debugDrawPassed = qfalse,
	.debugDrawLines = 0,
	.debugDrawCrosses = 0,
	.debugDrawArrows = 0,
	.debugDrawClears = 0,
	.debugDrawFailures = 0,
	.debugPolygonCallbackSet = qfalse,
	.debugPolygonAttempted = qfalse,
	.debugPolygonPassed = qfalse,
	.debugPolygonCreates = 0,
	.debugPolygonDeletes = 0,
	.debugPolygonPoints = 0,
	.debugPolygonLastId = 0,
	.debugPolygonFailures = 0,
	.debugAreaAttempted = qfalse,
	.debugAreaPassed = qfalse,
	.debugAreaArea = 0,
	.debugAreaLines = 0,
	.debugAreaPolygonCreates = 0,
	.debugAreaPolygonDeletes = 0,
	.debugAreaFailures = 0,
	.routeOverlayAttempted = qfalse,
	.routeOverlayPassed = qfalse,
	.routeOverlayStartArea = 0,
	.routeOverlayGoalArea = 0,
	.routeOverlayEndArea = 0,
	.routeOverlayTravelTime = 0,
	.routeOverlayReachability = 0,
	.routeOverlayLines = 0,
	.routeOverlayCrosses = 0,
	.routeOverlayArrows = 0,
	.routeOverlayClears = 0,
	.routeOverlayFailures = 0,
	.angleVectorsSmokePassed = qfalse,
	.runtimeMilliseconds = 0,
	.bspEntityLoadAttempted = qfalse,
	.bspEntityLoaded = qfalse,
	.bspEntityCount = 0,
	.bspEntityPairs = 0,
	.bspEntityValueSmokePassed = qfalse,
	.bspModelLoadAttempted = qfalse,
	.bspModelLoaded = qfalse,
	.bspModelCount = 0,
	.bspModelBoundsSmokePassed = qfalse,
	.bspCollisionLoadAttempted = qfalse,
	.bspCollisionLoaded = qfalse,
	.bspCollisionPlanes = 0,
	.bspCollisionNodes = 0,
	.bspCollisionLeafs = 0,
	.bspCollisionBrushes = 0,
	.bspCollisionPointContentsSmokePassed = qfalse,
	.bspCollisionTraceSmokePassed = qfalse,
	.bspLeafLinkAttempted = qfalse,
	.bspLeafLinks = 0,
	.bspLeafLinkFailures = 0,
	.bspBoxEntitiesSmokePassed = qfalse,
	.bspBoxEntitiesCount = 0,
	.bspVisibilityLoadAttempted = qfalse,
	.bspVisibilityLoaded = qfalse,
	.bspVisibilityClusters = 0,
	.bspVisibilityPvsSmokePassed = qfalse,
	.bspVisibilityPhsSmokePassed = qfalse,
	.memoryZoneActiveBytes = 0,
	.memoryZonePeakBytes = 0,
	.memoryZoneAllocations = 0,
	.memoryZoneFrees = 0,
	.memoryHunkActiveBytes = 0,
	.memoryHunkPeakBytes = 0,
	.memoryHunkAllocations = 0,
	.memoryHunkGroupFrees = 0,
	.memoryFailures = 0,
	.availableMemory = Q3A_BOTLIB_MEMORY_BUDGET_BYTES,
	.filesystemCallbackSet = qfalse,
	.filesystemAttempted = qfalse,
	.filesystemPassed = qfalse,
	.filesystemOpenAttempts = 0,
	.filesystemOpenFiles = 0,
	.filesystemOpenMemoryFiles = 0,
	.filesystemOpenFailures = 0,
	.filesystemRouteCacheMisses = 0,
	.filesystemReadBytes = 0,
	.filesystemSeekCount = 0,
	.filesystemCloseCount = 0,
	.filesystemWriteRejected = 0,
	.lifecycleMessage = "Q3A BotLib lifecycle has not run",
	.message = "Q3A BotLib import callbacks are not initialized",
	.memoryMessage = "Q3A BotLib memory allocator has not run",
	.filesystemMessage = "Q3A BotLib filesystem bridge has not run",
	.aasMessage = "Q3A AAS file loader has not run",
	.aasSampleMessage = "Q3A AAS area sample has not run",
	.aasClusterMessage = "Q3A AAS clustering has not run",
	.aasRouteMessage = "Q3A AAS route query has not run",
	.aasAltRouteMessage = "Q3A AAS alternative route query has not run",
	.aasMovementMessage = "Q3A AAS movement prediction has not run",
	.aasStartFrameMessage = "Q3A AAS start frame has not run",
	.entitySyncMessage = "Q3A AAS entity sync has not run",
	.entityTraceMessage = "Q3A AAS entity trace has not run",
	.botClientCommandMessage = "Q3A BotClientCommand bridge has not run",
	.debugDrawMessage = "Q3A debug draw bridge has not run",
	.debugPolygonMessage = "Q3A debug polygon bridge has not run",
	.debugAreaMessage = "Q3A AAS debug area helpers have not run",
	.routeOverlayMessage = "Q3A route overlay has not run",
	.angleVectorsMessage = "Q3A AngleVectors smoke has not run",
	.bspEntityMessage = "Q3A BSP entity data has not run",
	.bspModelMessage = "Q3A BSP model data has not run",
	.bspCollisionMessage = "Q3A BSP collision data has not run",
	.bspLeafLinkMessage = "Q3A BSP leaf entity links have not run",
	.bspVisibilityMessage = "Q3A BSP visibility data has not run",
};

static bsp_trace_t Q3A_BotLibImport_TraceQ2Bsp(
	vec3_t start,
	vec3_t mins,
	vec3_t maxs,
	vec3_t end,
	int contentmask);
static int Q3A_BotLibImport_PointContentsQ2Bsp(vec3_t point);
static qboolean Q3A_BotLibImport_PointsVisibleQ2Bsp(vec3_t p1, vec3_t p2, int mode);
static int Q3A_BotLibImport_PlaneSignbits(const vec3_t normal);
static int Q3A_BotLibImport_WorldHeadNode(void);
static int Q3A_BotLibImport_BoxOnPlaneSide(vec3_t mins, vec3_t maxs, Q3ABspPlane *plane);
static void Q3A_BotLibImport_ClearBspLeafLinks(void);
static void Q3A_BotLibImport_SetBspLeafLinkMessage(const char *fmt, ...);
static void Q3A_BotLibImport_ResetBspLeafLinkStatus(const char *message);
static void Q3A_BotLibImport_SetEntityTraceMessage(const char *prefix);
static void Q3A_BotLibImport_ResetDebugDrawStatus(const char *message);
static void Q3A_BotLibImport_SetDebugDrawMessage(const char *prefix);
static void Q3A_BotLibImport_ResetDebugPolygonStatus(const char *message);
static void Q3A_BotLibImport_SetDebugPolygonMessage(const char *prefix);
static void Q3A_BotLibImport_ResetDebugAreaStatus(const char *message);
static void Q3A_BotLibImport_SetDebugAreaMessage(const char *prefix);
static void Q3A_BotLibImport_ResetBotClientCommandStatus(const char *message);
static void Q3A_BotLibImport_SetBotClientCommandMessage(const char *prefix);
static void Q3A_BotLibImport_ResetRouteOverlayStatus(const char *message);
static void Q3A_BotLibImport_SetRouteOverlayMessage(const char *prefix);
static int Q3A_BotLibImport_DebugDraw(
	int primitive,
	const vec3_t start,
	const vec3_t end,
	float size,
	int color,
	int secondaryColor);
int AAS_AreaReachabilityToGoalArea(int areanum, vec3_t origin, int goalareanum, int travelflags);

static void QDECL Q3A_BotLibPrint(int type, char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(q3aPrintMessage, sizeof(q3aPrintMessage), fmt, args);
	va_end(args);

	q3aSmokeStatus.printLastType = type;
	switch (type) {
	case PRT_WARNING:
		q3aSmokeStatus.printWarnings++;
		break;
	case PRT_ERROR:
		q3aSmokeStatus.printErrors++;
		break;
	case PRT_FATAL:
		q3aSmokeStatus.printFatals++;
		break;
	default:
		q3aSmokeStatus.printMessages++;
		break;
	}

	if (type >= PRT_ERROR) {
		q3aSmokeStatus.message = q3aPrintMessage;
	}

	if (q3aPrintCallback != NULL) {
		q3aPrintCallback(type, q3aPrintMessage);
	}
}

static void Q3A_BotLibClientCommand(int client, char *command) {
	q3aSmokeStatus.botClientCommandAttempted = qtrue;
	q3aSmokeStatus.botClientCommandClient = client;
	q3aSmokeStatus.botClientCommandCallbackSet = q3aBotClientCommandCallback != NULL;

	if (command == NULL || command[0] == '\0') {
		q3aSmokeStatus.botClientCommandRejected++;
		q3aSmokeStatus.botClientCommandFailures++;
		Q3A_BotLibImport_SetBotClientCommandMessage("Q3A BotClientCommand bridge failed: empty command");
		return;
	}

	if (q3aBotClientCommandCallback == NULL) {
		q3aSmokeStatus.botClientCommandRejected++;
		q3aSmokeStatus.botClientCommandFailures++;
		Q3A_BotLibImport_SetBotClientCommandMessage("Q3A BotClientCommand bridge failed: callback is not registered");
		return;
	}

	if (q3aBotClientCommandCallback(client, command)) {
		q3aSmokeStatus.botClientCommandAccepted++;
		Q3A_BotLibImport_SetBotClientCommandMessage("Q3A BotClientCommand bridge accepted");
		return;
	}

	q3aSmokeStatus.botClientCommandRejected++;
	Q3A_BotLibImport_SetBotClientCommandMessage("Q3A BotClientCommand bridge safely rejected");
}

static int Q3A_BotLibAvailableMemory(void) {
	int used;

	used = q3aSmokeStatus.memoryZoneActiveBytes + q3aSmokeStatus.memoryHunkActiveBytes;
	if (used < 0 || used >= Q3A_BOTLIB_MEMORY_BUDGET_BYTES) {
		return 0;
	}
	return Q3A_BOTLIB_MEMORY_BUDGET_BYTES - used;
}

static void Q3A_BotLibImport_SetMemoryMessage(const char *prefix) {
	q3aSmokeStatus.availableMemory = Q3A_BotLibAvailableMemory();
	snprintf(
		q3aMemoryMessage,
		sizeof(q3aMemoryMessage),
		"%s: zone_active=%d zone_peak=%d zone_allocs=%d zone_frees=%d hunk_active=%d hunk_peak=%d hunk_allocs=%d hunk_groups=%d failures=%d available=%d",
		prefix,
		q3aSmokeStatus.memoryZoneActiveBytes,
		q3aSmokeStatus.memoryZonePeakBytes,
		q3aSmokeStatus.memoryZoneAllocations,
		q3aSmokeStatus.memoryZoneFrees,
		q3aSmokeStatus.memoryHunkActiveBytes,
		q3aSmokeStatus.memoryHunkPeakBytes,
		q3aSmokeStatus.memoryHunkAllocations,
		q3aSmokeStatus.memoryHunkGroupFrees,
		q3aSmokeStatus.memoryFailures,
		q3aSmokeStatus.availableMemory);
	q3aSmokeStatus.memoryMessage = q3aMemoryMessage;
}

static void Q3A_BotLibImport_SetLifecycleMessage(const char *fmt, ...) {
	va_list args;

	if (fmt == NULL) {
		q3aSmokeStatus.lifecycleMessage = "";
		return;
	}

	va_start(args, fmt);
	vsnprintf(q3aLifecycleMessage, sizeof(q3aLifecycleMessage), fmt, args);
	va_end(args);
	q3aLifecycleMessage[sizeof(q3aLifecycleMessage) - 1] = '\0';
	q3aSmokeStatus.lifecycleMessage = q3aLifecycleMessage;
}

static void Q3A_BotLibImport_LinkAllocation(Q3ABotLibAllocation **head, Q3ABotLibAllocation *block) {
	block->prev = NULL;
	block->next = *head;
	if (*head != NULL) {
		(*head)->prev = block;
	}
	*head = block;
}

static void Q3A_BotLibImport_UnlinkAllocation(Q3ABotLibAllocation **head, Q3ABotLibAllocation *block) {
	if (block->prev != NULL) {
		block->prev->next = block->next;
	} else {
		*head = block->next;
	}
	if (block->next != NULL) {
		block->next->prev = block->prev;
	}
	block->prev = NULL;
	block->next = NULL;
}

static void *Q3A_BotLibAllocateMemory(int size, qboolean hunk) {
	Q3ABotLibAllocation *block;
	size_t totalSize;

	if (size <= 0) {
		size = 1;
	}
	if (size > INT_MAX - (int)sizeof(*block)) {
		q3aSmokeStatus.memoryFailures++;
		Q3A_BotLibImport_SetMemoryMessage("Q3A BotLib memory allocation failed: size overflow");
		return NULL;
	}

	totalSize = sizeof(*block) + (size_t)size;
	block = (Q3ABotLibAllocation *)malloc(totalSize);
	if (block == NULL) {
		q3aSmokeStatus.memoryFailures++;
		Q3A_BotLibImport_SetMemoryMessage("Q3A BotLib memory allocation failed: allocation returned null");
		return NULL;
	}

	block->magic = Q3A_BOTLIB_MEMORY_MAGIC;
	block->size = size;
	block->hunk = hunk;
	if (hunk) {
		Q3A_BotLibImport_LinkAllocation(&q3aBotLibHunkAllocations, block);
		q3aSmokeStatus.memoryHunkActiveBytes += size;
		q3aSmokeStatus.memoryHunkAllocations++;
		if (q3aSmokeStatus.memoryHunkActiveBytes > q3aSmokeStatus.memoryHunkPeakBytes) {
			q3aSmokeStatus.memoryHunkPeakBytes = q3aSmokeStatus.memoryHunkActiveBytes;
		}
		Q3A_BotLibImport_SetMemoryMessage("Q3A BotLib hunk allocation tracked");
	} else {
		Q3A_BotLibImport_LinkAllocation(&q3aBotLibZoneAllocations, block);
		q3aSmokeStatus.memoryZoneActiveBytes += size;
		q3aSmokeStatus.memoryZoneAllocations++;
		if (q3aSmokeStatus.memoryZoneActiveBytes > q3aSmokeStatus.memoryZonePeakBytes) {
			q3aSmokeStatus.memoryZonePeakBytes = q3aSmokeStatus.memoryZoneActiveBytes;
		}
		Q3A_BotLibImport_SetMemoryMessage("Q3A BotLib zone allocation tracked");
	}

	return block + 1;
}

static void Q3A_BotLibFreeMemory(void *ptr) {
	Q3ABotLibAllocation *block;

	if (ptr == NULL) {
		return;
	}

	block = ((Q3ABotLibAllocation *)ptr) - 1;
	if (block->magic != Q3A_BOTLIB_MEMORY_MAGIC || block->size <= 0) {
		q3aSmokeStatus.memoryFailures++;
		Q3A_BotLibImport_SetMemoryMessage("Q3A BotLib memory free failed: invalid block");
		return;
	}

	if (block->hunk) {
		Q3A_BotLibImport_UnlinkAllocation(&q3aBotLibHunkAllocations, block);
		q3aSmokeStatus.memoryHunkActiveBytes -= block->size;
		if (q3aSmokeStatus.memoryHunkActiveBytes < 0) {
			q3aSmokeStatus.memoryHunkActiveBytes = 0;
			q3aSmokeStatus.memoryFailures++;
		}
	} else {
		Q3A_BotLibImport_UnlinkAllocation(&q3aBotLibZoneAllocations, block);
		q3aSmokeStatus.memoryZoneActiveBytes -= block->size;
		if (q3aSmokeStatus.memoryZoneActiveBytes < 0) {
			q3aSmokeStatus.memoryZoneActiveBytes = 0;
			q3aSmokeStatus.memoryFailures++;
		}
		q3aSmokeStatus.memoryZoneFrees++;
	}
	block->magic = 0;
	free(block);
	Q3A_BotLibImport_SetMemoryMessage("Q3A BotLib allocation freed");
}

static void Q3A_BotLibReleaseHunkAllocations(const char *prefix) {
	Q3ABotLibAllocation *block;

	if (q3aBotLibHunkAllocations != NULL) {
		q3aSmokeStatus.memoryHunkGroupFrees++;
	}

	while (q3aBotLibHunkAllocations != NULL) {
		block = q3aBotLibHunkAllocations;
		Q3A_BotLibImport_UnlinkAllocation(&q3aBotLibHunkAllocations, block);
		q3aSmokeStatus.memoryHunkActiveBytes -= block->size;
		if (q3aSmokeStatus.memoryHunkActiveBytes < 0) {
			q3aSmokeStatus.memoryHunkActiveBytes = 0;
			q3aSmokeStatus.memoryFailures++;
		}
		block->magic = 0;
		free(block);
	}
	Q3A_BotLibImport_SetMemoryMessage(prefix);
}

static void Q3A_BotLibReleaseAllAllocations(const char *prefix) {
	Q3ABotLibAllocation *block;

	Q3A_BotLibReleaseHunkAllocations(prefix);
	while (q3aBotLibZoneAllocations != NULL) {
		block = q3aBotLibZoneAllocations;
		Q3A_BotLibImport_UnlinkAllocation(&q3aBotLibZoneAllocations, block);
		q3aSmokeStatus.memoryZoneActiveBytes -= block->size;
		if (q3aSmokeStatus.memoryZoneActiveBytes < 0) {
			q3aSmokeStatus.memoryZoneActiveBytes = 0;
			q3aSmokeStatus.memoryFailures++;
		}
		q3aSmokeStatus.memoryZoneFrees++;
		block->magic = 0;
		free(block);
	}
	Q3A_BotLibImport_SetMemoryMessage(prefix);
}

static void *Q3A_BotLibGetMemory(int size) {
	return Q3A_BotLibAllocateMemory(size, qfalse);
}

static void *Q3A_BotLibHunkAlloc(int size) {
	return Q3A_BotLibAllocateMemory(size, qtrue);
}

static void Q3A_BotLibImport_SetFilesystemMessage(const char *fmt, ...) {
	va_list args;

	if (fmt == NULL) {
		q3aSmokeStatus.filesystemMessage = "";
		return;
	}

	va_start(args, fmt);
	vsnprintf(q3aFilesystemMessage, sizeof(q3aFilesystemMessage), fmt, args);
	va_end(args);
	q3aFilesystemMessage[sizeof(q3aFilesystemMessage) - 1] = '\0';
	q3aSmokeStatus.filesystemMessage = q3aFilesystemMessage;
}

static void Q3A_BotLibImport_ResetFilesystemStatus(const char *message) {
	q3aSmokeStatus.filesystemCallbackSet = q3aFilesystemLoadCallback != NULL;
	q3aSmokeStatus.filesystemAttempted = qfalse;
	q3aSmokeStatus.filesystemPassed = qfalse;
	q3aSmokeStatus.filesystemOpenAttempts = 0;
	q3aSmokeStatus.filesystemOpenFiles = 0;
	q3aSmokeStatus.filesystemOpenMemoryFiles = 0;
	q3aSmokeStatus.filesystemOpenFailures = 0;
	q3aSmokeStatus.filesystemRouteCacheMisses = 0;
	q3aSmokeStatus.filesystemReadBytes = 0;
	q3aSmokeStatus.filesystemSeekCount = 0;
	q3aSmokeStatus.filesystemCloseCount = 0;
	q3aSmokeStatus.filesystemWriteRejected = 0;
	Q3A_BotLibImport_SetFilesystemMessage(message);
}

static int Q3A_BotLibFSIndexForHandle(fileHandle_t f) {
	return f - Q3A_MEMORY_FILE_HANDLE;
}

static Q3AFileSlot *Q3A_BotLibFSFileForHandle(fileHandle_t f) {
	const int index = Q3A_BotLibFSIndexForHandle(f);
	if (index < 0 || index >= Q3A_MAX_FILE_HANDLES || !q3aFileSlots[index].open) {
		return NULL;
	}
	return &q3aFileSlots[index];
}

static int Q3A_BotLibFSOpenFileCount(void) {
	int i;
	int count = 0;

	for (i = 0; i < Q3A_MAX_FILE_HANDLES; ++i) {
		if (q3aFileSlots[i].open) {
			count++;
		}
	}
	return count;
}

static qboolean Q3A_BotLibImport_HasActiveAASState(int openFileCount) {
	return q3aSmokeStatus.aasLoaded ||
		aasworld.loaded ||
		aasworld.entities != NULL ||
		aasworld.linkheap != NULL ||
		aasworld.arealinkedentities != NULL ||
		q3aMemoryFile.data != NULL ||
		openFileCount > 0 ||
		q3aSmokeStatus.memoryHunkActiveBytes > 0;
}

static qboolean Q3A_BotLibImport_AASStateIsClean(int openFileCount) {
	return !q3aSmokeStatus.aasLoaded &&
		!aasworld.loaded &&
		aasworld.entities == NULL &&
		aasworld.linkheap == NULL &&
		aasworld.arealinkedentities == NULL &&
		q3aMemoryFile.data == NULL &&
		openFileCount == 0 &&
		q3aSmokeStatus.memoryHunkActiveBytes == 0;
}

static int Q3A_BotLibImport_TransientZoneBytes(void) {
	const int transient = q3aSmokeStatus.memoryZoneActiveBytes - q3aSmokeStatus.lifecyclePersistentZoneBytes;
	return transient > 0 ? transient : 0;
}

static void Q3A_BotLibFSCloseSlot(Q3AFileSlot *slot, qboolean countClose) {
	if (slot == NULL || !slot->open) {
		return;
	}

	if (slot->callbackOwned && slot->data != NULL && q3aFilesystemFreeCallback != NULL) {
		q3aFilesystemFreeCallback(slot->data);
	}

	Com_Memset(slot, 0, sizeof(*slot));
	if (countClose) {
		q3aSmokeStatus.filesystemCloseCount++;
	}
}

static void Q3A_BotLibFSCloseAllFiles(void) {
	int i;

	for (i = 0; i < Q3A_MAX_FILE_HANDLES; ++i) {
		Q3A_BotLibFSCloseSlot(&q3aFileSlots[i], qfalse);
	}
}

static Q3AFileSlot *Q3A_BotLibFSAllocateSlot(fileHandle_t *file) {
	int i;

	for (i = 0; i < Q3A_MAX_FILE_HANDLES; ++i) {
		if (!q3aFileSlots[i].open) {
			if (file != NULL) {
				*file = Q3A_MEMORY_FILE_HANDLE + i;
			}
			return &q3aFileSlots[i];
		}
	}
	return NULL;
}

static qboolean Q3A_BotLibFSIsRouteCachePath(const char *qpath) {
	const char *extension;

	if (qpath == NULL) {
		return qfalse;
	}

	extension = strrchr(qpath, '.');
	return extension != NULL && Q_stricmp(extension, ".rcd") == 0;
}

static void Q3A_BotLibImport_FinalizeFilesystemStatus(const char *passedMessage) {
	q3aSmokeStatus.filesystemPassed =
		q3aSmokeStatus.filesystemReadBytes > 0 &&
		(q3aSmokeStatus.filesystemOpenFiles > 0 || q3aSmokeStatus.filesystemOpenMemoryFiles > 0);
	Q3A_BotLibImport_SetFilesystemMessage(
		q3aSmokeStatus.filesystemPassed ? passedMessage : "Q3A BotLib filesystem bridge failed");
}

static int Q3A_BotLibFSOpenFile(const char *qpath, fileHandle_t *file, fsMode_t mode) {
	Q3AFileSlot *slot;
	const unsigned char *data;
	int length;

	if (file != NULL) {
		*file = 0;
	}

	q3aSmokeStatus.filesystemAttempted = qtrue;
	q3aSmokeStatus.filesystemCallbackSet = q3aFilesystemLoadCallback != NULL;
	q3aSmokeStatus.filesystemOpenAttempts++;

	if (mode != FS_READ) {
		q3aSmokeStatus.filesystemOpenFailures++;
		Q3A_BotLibImport_SetFilesystemMessage("Q3A BotLib filesystem rejected write-mode open");
		return -1;
	}

	slot = Q3A_BotLibFSAllocateSlot(file);
	if (slot == NULL) {
		q3aSmokeStatus.filesystemOpenFailures++;
		Q3A_BotLibImport_SetFilesystemMessage("Q3A BotLib filesystem open failed: handle table full");
		return -1;
	}

	data = NULL;
	length = q3aFilesystemLoadCallback != NULL ? q3aFilesystemLoadCallback(qpath, &data) : -1;
	if (length > 0 && data != NULL) {
		slot->data = data;
		slot->length = length;
		slot->offset = 0;
		slot->open = qtrue;
		slot->callbackOwned = qtrue;
		q3aSmokeStatus.filesystemOpenFiles++;
		Q3A_BotLibImport_SetFilesystemMessage("Q3A BotLib filesystem file opened through WORR FS");
		return length;
	}

	if (data != NULL && q3aFilesystemFreeCallback != NULL) {
		q3aFilesystemFreeCallback(data);
	}

	if (q3aMemoryFile.data != NULL &&
		q3aMemoryFile.length > 0 &&
		(q3aMemoryFile.name == NULL || qpath == NULL || Q_stricmp(q3aMemoryFile.name, qpath) == 0)) {
		slot->data = q3aMemoryFile.data;
		slot->length = q3aMemoryFile.length;
		slot->offset = 0;
		slot->open = qtrue;
		slot->callbackOwned = qfalse;
		q3aSmokeStatus.filesystemOpenMemoryFiles++;
		Q3A_BotLibImport_SetFilesystemMessage("Q3A BotLib filesystem file opened from active memory fallback");
		return slot->length;
	}

	if (Q3A_BotLibFSIsRouteCachePath(qpath)) {
		q3aSmokeStatus.filesystemRouteCacheMisses++;
		if (q3aSmokeStatus.filesystemReadBytes > 0) {
			Q3A_BotLibImport_SetFilesystemMessage(
				"Q3A BotLib filesystem optional route-cache miss after successful read");
		} else {
			Q3A_BotLibImport_SetFilesystemMessage("Q3A BotLib filesystem optional route-cache miss");
		}
	} else {
		q3aSmokeStatus.filesystemOpenFailures++;
		Q3A_BotLibImport_SetFilesystemMessage("Q3A BotLib filesystem open failed");
	}
	if (file != NULL) {
		*file = 0;
	}
	return -1;
}

static int Q3A_BotLibFSRead(void *buffer, int len, fileHandle_t f) {
	Q3AFileSlot *slot;
	int remaining;
	int count;

	slot = Q3A_BotLibFSFileForHandle(f);
	if (slot == NULL || buffer == NULL || len < 0) {
		return 0;
	}

	remaining = slot->length - slot->offset;
	if (remaining <= 0) {
		return 0;
	}

	count = len < remaining ? len : remaining;
	memcpy(buffer, slot->data + slot->offset, (size_t)count);
	slot->offset += count;
	q3aSmokeStatus.filesystemReadBytes += count;
	Q3A_BotLibImport_SetFilesystemMessage("Q3A BotLib filesystem read passed");
	return count;
}

static int Q3A_BotLibFSWrite(const void *buffer, int len, fileHandle_t f) {
	(void)buffer;
	(void)f;
	if (len > 0) {
		q3aSmokeStatus.filesystemWriteRejected++;
		Q3A_BotLibImport_SetFilesystemMessage("Q3A BotLib filesystem write rejected");
	}
	return 0;
}

static void Q3A_BotLibFSCloseFile(fileHandle_t f) {
	Q3A_BotLibFSCloseSlot(Q3A_BotLibFSFileForHandle(f), qtrue);
}

static int Q3A_BotLibFSSeek(fileHandle_t f, long offset, int origin) {
	Q3AFileSlot *slot;
	long base;
	long nextOffset;

	slot = Q3A_BotLibFSFileForHandle(f);
	if (slot == NULL) {
		return -1;
	}

	switch (origin) {
	case FS_SEEK_SET:
		base = 0;
		break;
	case FS_SEEK_CUR:
		base = slot->offset;
		break;
	case FS_SEEK_END:
		base = slot->length;
		break;
	default:
		return -1;
	}

	nextOffset = base + offset;
	if (nextOffset < 0 || nextOffset > slot->length) {
		return -1;
	}

	slot->offset = (int)nextOffset;
	q3aSmokeStatus.filesystemSeekCount++;
	return 0;
}

int Q_stricmp(const char *s1, const char *s2) {
#ifdef _WIN32
	return _stricmp(s1, s2);
#else
	return strcasecmp(s1, s2);
#endif
}

void Com_Memset(void *dest, const int val, const size_t count) {
	memset(dest, val, count);
}

void Com_Memcpy(void *dest, const void *src, const size_t count) {
	memcpy(dest, src, count);
}

void QDECL Com_sprintf(char *dest, int size, const char *fmt, ...) {
	va_list args;

	if (dest == NULL || size <= 0) {
		return;
	}

	va_start(args, fmt);
	vsnprintf(dest, (size_t)size, fmt, args);
	va_end(args);

	dest[size - 1] = '\0';
}

static void Q3A_BotLibImport_SetDefaultAASMovementLibVars(void) {
	LibVarSet("phys_friction", "6");
	LibVarSet("phys_stopspeed", "100");
	LibVarSet("phys_gravity", "800");
	LibVarSet("phys_waterfriction", "1");
	LibVarSet("phys_watergravity", "400");
	LibVarSet("phys_maxvelocity", "320");
	LibVarSet("phys_maxwalkvelocity", "300");
	LibVarSet("phys_maxcrouchvelocity", "150");
	LibVarSet("phys_maxswimvelocity", "150");
	LibVarSet("phys_walkaccelerate", "10");
	LibVarSet("phys_airaccelerate", "1");
	LibVarSet("phys_swimaccelerate", "4");
	LibVarSet("phys_maxstep", "18");
	LibVarSet("phys_maxsteepness", "0.7");
	LibVarSet("phys_maxwaterjump", "19");
	LibVarSet("phys_maxbarrier", "32");
	LibVarSet("phys_jumpvel", "270");
	LibVarSet("phys_falldelta5", "40");
	LibVarSet("phys_falldelta10", "60");
	LibVarSet("rs_waterjump", "400");
	LibVarSet("rs_teleport", "50");
	LibVarSet("rs_barrierjump", "100");
	LibVarSet("rs_startcrouch", "300");
	LibVarSet("rs_startgrapple", "500");
	LibVarSet("rs_startwalkoffledge", "70");
	LibVarSet("rs_startjump", "300");
	LibVarSet("rs_rocketjump", "500");
	LibVarSet("rs_bfgjump", "500");
	LibVarSet("rs_jumppad", "250");
	LibVarSet("rs_aircontrolledjumppad", "300");
	LibVarSet("rs_funcbob", "300");
	LibVarSet("rs_startelevator", "50");
	LibVarSet("rs_falldamage5", "300");
	LibVarSet("rs_falldamage10", "500");
	LibVarSet("rs_maxfallheight", "0");
	LibVarSet("rs_maxjumpfallheight", "450");
}

void AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up) {
	float pitch;
	float yaw;
	float roll;
	float sp;
	float sy;
	float sr;
	float cp;
	float cy;
	float cr;

	pitch = DEG2RAD(angles[PITCH]);
	yaw = DEG2RAD(angles[YAW]);
	roll = DEG2RAD(angles[ROLL]);
	sp = sinf(pitch);
	cp = cosf(pitch);
	sy = sinf(yaw);
	cy = cosf(yaw);
	sr = sinf(roll);
	cr = cosf(roll);

	if (forward != NULL) {
		forward[0] = cp * cy;
		forward[1] = cp * sy;
		forward[2] = -sp;
	}
	if (right != NULL) {
		right[0] = cr * sy - sr * sp * cy;
		right[1] = -sr * sp * sy - cr * cy;
		right[2] = -sr * cp;
	}
	if (up != NULL) {
		up[0] = cr * sp * cy + sr * sy;
		up[1] = cr * sp * sy - sr * cy;
		up[2] = cr * cp;
	}
}

static int Q3A_BotLibNearlyEqual(float lhs, float rhs) {
	return fabsf(lhs - rhs) <= 0.001f;
}

static void Q3A_BotLibImport_RunAngleVectorsSmoke(void) {
	vec3_t angles;
	vec3_t forward;
	vec3_t right;
	vec3_t up;

	VectorSet(angles, 0.0f, 90.0f, 0.0f);
	AngleVectors(angles, forward, right, up);

	if (!Q3A_BotLibNearlyEqual(forward[0], 0.0f) ||
		!Q3A_BotLibNearlyEqual(forward[1], 1.0f) ||
		!Q3A_BotLibNearlyEqual(forward[2], 0.0f) ||
		!Q3A_BotLibNearlyEqual(right[0], 1.0f) ||
		!Q3A_BotLibNearlyEqual(right[1], 0.0f) ||
		!Q3A_BotLibNearlyEqual(right[2], 0.0f) ||
		!Q3A_BotLibNearlyEqual(up[0], 0.0f) ||
		!Q3A_BotLibNearlyEqual(up[1], 0.0f) ||
		!Q3A_BotLibNearlyEqual(up[2], 1.0f)) {
		q3aSmokeStatus.angleVectorsSmokePassed = qfalse;
		q3aSmokeStatus.angleVectorsMessage = "Q3A AngleVectors smoke failed";
		return;
	}

	q3aSmokeStatus.angleVectorsSmokePassed = qtrue;
	q3aSmokeStatus.angleVectorsMessage = "Q3A AngleVectors smoke passed";
}

static vec_t Q3A_BotLibSqrt(vec_t value) {
	vec_t estimate;
	int i;

	if (value <= 0.0f) {
		return 0.0f;
	}

	estimate = value > 1.0f ? value : 1.0f;
	for (i = 0; i < 8; ++i) {
		estimate = 0.5f * (estimate + value / estimate);
	}
	return estimate;
}

vec_t VectorNormalize(vec3_t v) {
	const vec_t length = Q3A_BotLibSqrt(DotProduct(v, v));
	if (length > 0.0f) {
		const vec_t inverseLength = 1.0f / length;
		v[0] *= inverseLength;
		v[1] *= inverseLength;
		v[2] *= inverseLength;
	}
	return length;
}

qboolean AAS_EntityCollision(
	int entnum,
	vec3_t start,
	vec3_t boxmins,
	vec3_t boxmaxs,
	vec3_t end,
	int contentmask,
	bsp_trace_t *trace) {
	Q3ABotLibImportTraceResult traceResult;

	if (trace != NULL) {
		Com_Memset(trace, 0, sizeof(*trace));
		trace->fraction = 1.0f;
		trace->ent = entnum;
		VectorCopy(end, trace->endpos);
	}

	q3aSmokeStatus.entityTraceCallbackSet = q3aEntityTraceCallback != NULL;
	q3aSmokeStatus.entityTraceAttempted++;

	if (q3aEntityTraceCallback == NULL) {
		q3aSmokeStatus.entityTraceFailures++;
		Q3A_BotLibImport_SetEntityTraceMessage("Q3A AAS entity trace failed: callback is not registered");
		return qfalse;
	}

	Com_Memset(&traceResult, 0, sizeof(traceResult));
	traceResult.fraction = 1.0f;
	traceResult.entnum = entnum;
	VectorCopy(end, traceResult.endPos);
	if (!q3aEntityTraceCallback(entnum, start, boxmins, boxmaxs, end, contentmask, &traceResult)) {
		q3aSmokeStatus.entityTraceFailures++;
		Q3A_BotLibImport_SetEntityTraceMessage("Q3A AAS entity trace failed");
		return qfalse;
	}

	if (!traceResult.hit) {
		q3aSmokeStatus.entityTraceMisses++;
		Q3A_BotLibImport_SetEntityTraceMessage("Q3A AAS entity trace missed");
		return qfalse;
	}

	if (trace != NULL) {
		trace->allsolid = traceResult.allSolid != 0;
		trace->startsolid = traceResult.startSolid != 0;
		trace->fraction = traceResult.fraction;
		VectorCopy(traceResult.endPos, trace->endpos);
		VectorCopy(traceResult.planeNormal, trace->plane.normal);
		trace->plane.dist = traceResult.planeDist;
		trace->plane.type = (byte)PlaneTypeForNormal(traceResult.planeNormal);
		trace->plane.signbits = (byte)Q3A_BotLibImport_PlaneSignbits(traceResult.planeNormal);
		trace->plane.pad[0] = 0;
		trace->plane.pad[1] = 0;
		trace->surface.name[0] = '\0';
		trace->surface.flags = 0;
		trace->surface.value = 0;
		trace->contents = traceResult.contents;
		trace->ent = traceResult.entnum;
	}

	q3aSmokeStatus.entityTraceHits++;
	Q3A_BotLibImport_SetEntityTraceMessage("Q3A AAS entity trace passed");
	return qtrue;
}

bsp_trace_t AAS_Trace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int passent, int contentmask) {
	(void)passent;
	return Q3A_BotLibImport_TraceQ2Bsp(start, mins, maxs, end, contentmask);
}

int AAS_PointContents(vec3_t point) {
	return Q3A_BotLibImport_PointContentsQ2Bsp(point);
}

qboolean AAS_inPVS(vec3_t p1, vec3_t p2) {
	return Q3A_BotLibImport_PointsVisibleQ2Bsp(p1, p2, Q3A_Q2_DVIS_PVS);
}

qboolean AAS_inPHS(vec3_t p1, vec3_t p2) {
	return Q3A_BotLibImport_PointsVisibleQ2Bsp(p1, p2, Q3A_Q2_DVIS_PHS);
}

static int Q3A_BotLibImport_EnsureBspLeafLinkTable(void) {
	if (!q3aSmokeStatus.bspCollisionLoaded || q3aBspLeafCount <= 0) {
		return qfalse;
	}
	if (q3aBspLeafLinkedEntities != NULL) {
		return qtrue;
	}

	q3aBspLeafLinkedEntities =
		(bsp_link_t **)calloc((size_t)q3aBspLeafCount, sizeof(*q3aBspLeafLinkedEntities));
	if (q3aBspLeafLinkedEntities == NULL) {
		q3aSmokeStatus.bspLeafLinkFailures++;
		Q3A_BotLibImport_SetBspLeafLinkMessage("Q3A BSP leaf entity link failed: allocation failed");
		return qfalse;
	}

	return qtrue;
}

static int Q3A_BotLibImport_BspLeafAlreadyHasEntity(int leafnum, int entnum) {
	bsp_link_t *link;

	if (leafnum < 0 || leafnum >= q3aBspLeafCount || q3aBspLeafLinkedEntities == NULL) {
		return qfalse;
	}

	for (link = q3aBspLeafLinkedEntities[leafnum]; link != NULL; link = link->next_ent) {
		if (link->entnum == entnum) {
			return qtrue;
		}
	}

	return qfalse;
}

static void Q3A_BotLibImport_LinkEntityToLeaf(
	int leafnum,
	int entnum,
	bsp_link_t **entityLeafLinks,
	int *failed) {
	bsp_link_t *link;

	if (failed != NULL && *failed) {
		return;
	}
	if (leafnum < 0 || leafnum >= q3aBspLeafCount || entityLeafLinks == NULL) {
		return;
	}
	if (Q3A_BotLibImport_BspLeafAlreadyHasEntity(leafnum, entnum)) {
		return;
	}

	link = (bsp_link_t *)malloc(sizeof(*link));
	if (link == NULL) {
		if (failed != NULL) {
			*failed = qtrue;
		}
		q3aSmokeStatus.bspLeafLinkFailures++;
		Q3A_BotLibImport_SetBspLeafLinkMessage("Q3A BSP leaf entity link failed: allocation failed");
		return;
	}

	link->entnum = entnum;
	link->leafnum = leafnum;
	link->prev_leaf = NULL;
	link->next_leaf = *entityLeafLinks;
	if (*entityLeafLinks != NULL) {
		(*entityLeafLinks)->prev_leaf = link;
	}
	*entityLeafLinks = link;

	link->prev_ent = NULL;
	link->next_ent = q3aBspLeafLinkedEntities[leafnum];
	if (q3aBspLeafLinkedEntities[leafnum] != NULL) {
		q3aBspLeafLinkedEntities[leafnum]->prev_ent = link;
	}
	q3aBspLeafLinkedEntities[leafnum] = link;

	q3aBspLeafLinkCount++;
	q3aSmokeStatus.bspLeafLinks = q3aBspLeafLinkCount;
}

static void Q3A_BotLibImport_BspLinkEntity_r(
	int nodenum,
	vec3_t absmins,
	vec3_t absmaxs,
	int entnum,
	bsp_link_t **entityLeafLinks,
	int *failed) {
	while (nodenum >= 0) {
		Q3ABspNode *node;
		Q3ABspPlane *plane;
		int side;

		if (nodenum >= q3aBspNodeCount) {
			return;
		}

		node = &q3aBspNodes[nodenum];
		if (node->planenum < 0 || node->planenum >= q3aBspPlaneCount) {
			return;
		}

		plane = &q3aBspPlanes[node->planenum];
		side = Q3A_BotLibImport_BoxOnPlaneSide(absmins, absmaxs, plane);
		if (side == 1) {
			nodenum = node->children[0];
			continue;
		}
		if (side == 2) {
			nodenum = node->children[1];
			continue;
		}

		Q3A_BotLibImport_BspLinkEntity_r(
			node->children[0],
			absmins,
			absmaxs,
			entnum,
			entityLeafLinks,
			failed);
		nodenum = node->children[1];
	}

	Q3A_BotLibImport_LinkEntityToLeaf(-1 - nodenum, entnum, entityLeafLinks, failed);
}

void AAS_UnlinkFromBSPLeaves(bsp_link_t *leaves) {
	bsp_link_t *link;
	bsp_link_t *nextLink;

	for (link = leaves; link != NULL; link = nextLink) {
		nextLink = link->next_leaf;
		if (link->leafnum >= 0 &&
			link->leafnum < q3aBspLeafCount &&
			q3aBspLeafLinkedEntities != NULL) {
			if (link->prev_ent != NULL) {
				link->prev_ent->next_ent = link->next_ent;
			} else if (q3aBspLeafLinkedEntities[link->leafnum] == link) {
				q3aBspLeafLinkedEntities[link->leafnum] = link->next_ent;
			}
			if (link->next_ent != NULL) {
				link->next_ent->prev_ent = link->prev_ent;
			}
		}

		free(link);
		if (q3aBspLeafLinkCount > 0) {
			q3aBspLeafLinkCount--;
		}
	}

	q3aSmokeStatus.bspLeafLinks = q3aBspLeafLinkCount;
}

bsp_link_t *AAS_BSPLinkEntity(vec3_t absmins, vec3_t absmaxs, int entnum, int modelnum) {
	bsp_link_t *leafLinks = NULL;
	int headnode;
	int failed = qfalse;

	(void)modelnum;
	q3aSmokeStatus.bspLeafLinkAttempted = qtrue;
	q3aSmokeStatus.bspBoxEntitiesSmokePassed = qfalse;
	q3aSmokeStatus.bspBoxEntitiesCount = 0;

	if (absmins == NULL || absmaxs == NULL) {
		q3aSmokeStatus.bspLeafLinkFailures++;
		Q3A_BotLibImport_SetBspLeafLinkMessage("Q3A BSP leaf entity link failed: missing bounds");
		return NULL;
	}

	if (!Q3A_BotLibImport_EnsureBspLeafLinkTable()) {
		if (!q3aSmokeStatus.bspCollisionLoaded || q3aBspLeafCount <= 0) {
			q3aSmokeStatus.bspLeafLinkFailures++;
			Q3A_BotLibImport_SetBspLeafLinkMessage("Q3A BSP leaf entity link skipped: collision data is not loaded");
		}
		return NULL;
	}

	headnode = Q3A_BotLibImport_WorldHeadNode();
	if (headnode < 0 || headnode >= q3aBspNodeCount) {
		q3aSmokeStatus.bspLeafLinkFailures++;
		Q3A_BotLibImport_SetBspLeafLinkMessage("Q3A BSP leaf entity link failed: invalid world headnode");
		return NULL;
	}

	Q3A_BotLibImport_BspLinkEntity_r(headnode, absmins, absmaxs, entnum, &leafLinks, &failed);
	if (failed) {
		AAS_UnlinkFromBSPLeaves(leafLinks);
		return NULL;
	}

	Q3A_BotLibImport_SetBspLeafLinkMessage(
		leafLinks != NULL ?
			"Q3A BSP leaf entity link passed: active_links=%d" :
			"Q3A BSP leaf entity link passed without overlapping leaves: active_links=%d",
		q3aBspLeafLinkCount);
	return leafLinks;
}

static int Q3A_BotLibImport_BoxEntitiesContains(const int *list, int count, int entnum) {
	int i;

	for (i = 0; i < count; ++i) {
		if (list[i] == entnum) {
			return qtrue;
		}
	}

	return qfalse;
}

static void Q3A_BotLibImport_BoxEntitiesFromLeaf(int leafnum, int *list, int maxcount, int *count) {
	bsp_link_t *link;

	if (leafnum < 0 ||
		leafnum >= q3aBspLeafCount ||
		q3aBspLeafLinkedEntities == NULL ||
		list == NULL ||
		count == NULL ||
		*count >= maxcount) {
		return;
	}

	for (link = q3aBspLeafLinkedEntities[leafnum]; link != NULL && *count < maxcount; link = link->next_ent) {
		if (!Q3A_BotLibImport_BoxEntitiesContains(list, *count, link->entnum)) {
			list[*count] = link->entnum;
			(*count)++;
		}
	}
}

static void Q3A_BotLibImport_BoxEntities_r(
	int nodenum,
	vec3_t absmins,
	vec3_t absmaxs,
	int *list,
	int maxcount,
	int *count) {
	while (nodenum >= 0 && count != NULL && *count < maxcount) {
		Q3ABspNode *node;
		Q3ABspPlane *plane;
		int side;

		if (nodenum >= q3aBspNodeCount) {
			return;
		}

		node = &q3aBspNodes[nodenum];
		if (node->planenum < 0 || node->planenum >= q3aBspPlaneCount) {
			return;
		}

		plane = &q3aBspPlanes[node->planenum];
		side = Q3A_BotLibImport_BoxOnPlaneSide(absmins, absmaxs, plane);
		if (side == 1) {
			nodenum = node->children[0];
			continue;
		}
		if (side == 2) {
			nodenum = node->children[1];
			continue;
		}

		Q3A_BotLibImport_BoxEntities_r(node->children[0], absmins, absmaxs, list, maxcount, count);
		nodenum = node->children[1];
	}

	if (count != NULL && *count < maxcount) {
		Q3A_BotLibImport_BoxEntitiesFromLeaf(-1 - nodenum, list, maxcount, count);
	}
}

int AAS_BoxEntities(vec3_t absmins, vec3_t absmaxs, int *list, int maxcount) {
	int count = 0;
	int headnode;

	if (list == NULL || maxcount <= 0 || absmins == NULL || absmaxs == NULL) {
		return 0;
	}
	if (!q3aSmokeStatus.bspCollisionLoaded ||
		q3aBspLeafLinkedEntities == NULL ||
		q3aBspLeafLinkCount <= 0) {
		return 0;
	}

	headnode = Q3A_BotLibImport_WorldHeadNode();
	if (headnode < 0 || headnode >= q3aBspNodeCount) {
		return 0;
	}

	Q3A_BotLibImport_BoxEntities_r(headnode, absmins, absmaxs, list, maxcount, &count);
	return count;
}

int AAS_LoadBSPFile(void) {
	return BLERR_NOERROR;
}

void AAS_DumpBSPData(void) {
	Q3A_BotLibImport_ClearBspLeafLinks();
}

static void Q3A_BotLibImport_SetBspEntityMessage(const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(q3aBspEntityMessage, sizeof(q3aBspEntityMessage), fmt, args);
	va_end(args);
	q3aSmokeStatus.bspEntityMessage = q3aBspEntityMessage;
}

static void Q3A_BotLibImport_SetBspModelMessage(const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(q3aBspModelMessage, sizeof(q3aBspModelMessage), fmt, args);
	va_end(args);
	q3aSmokeStatus.bspModelMessage = q3aBspModelMessage;
}

static void Q3A_BotLibImport_SetBspCollisionMessage(const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(q3aBspCollisionMessage, sizeof(q3aBspCollisionMessage), fmt, args);
	va_end(args);
	q3aSmokeStatus.bspCollisionMessage = q3aBspCollisionMessage;
}

static void Q3A_BotLibImport_SetBspLeafLinkMessage(const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(q3aBspLeafLinkMessage, sizeof(q3aBspLeafLinkMessage), fmt, args);
	va_end(args);
	q3aSmokeStatus.bspLeafLinkMessage = q3aBspLeafLinkMessage;
}

static void Q3A_BotLibImport_SetBspVisibilityMessage(const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(q3aBspVisibilityMessage, sizeof(q3aBspVisibilityMessage), fmt, args);
	va_end(args);
	q3aSmokeStatus.bspVisibilityMessage = q3aBspVisibilityMessage;
}

static int Q3A_BotLibImport_ReadLittleInt32(const unsigned char *data) {
	const unsigned int value = (unsigned int)data[0] |
		((unsigned int)data[1] << 8) |
		((unsigned int)data[2] << 16) |
		((unsigned int)data[3] << 24);
	return (int)value;
}

static short Q3A_BotLibImport_ReadLittleInt16(const unsigned char *data) {
	const unsigned int value = (unsigned int)data[0] |
		((unsigned int)data[1] << 8);
	return (short)value;
}

static unsigned short Q3A_BotLibImport_ReadLittleUInt16(const unsigned char *data) {
	const unsigned int value = (unsigned int)data[0] |
		((unsigned int)data[1] << 8);
	return (unsigned short)value;
}

static float Q3A_BotLibImport_ReadLittleFloat(const unsigned char *data) {
	const unsigned int value = (unsigned int)data[0] |
		((unsigned int)data[1] << 8) |
		((unsigned int)data[2] << 16) |
		((unsigned int)data[3] << 24);
	float out;

	memcpy(&out, &value, sizeof(out));
	return out;
}

static void Q3A_BotLibImport_FreeBspEntityData(void) {
	int i;

	if (q3aBspEntities == NULL) {
		q3aBspEntityCount = 0;
		q3aBspEntityPairs = 0;
		return;
	}

	for (i = 0; i < q3aBspEntityCount; ++i) {
		Q3ABspEpair *epair = q3aBspEntities[i].epairs;
		while (epair != NULL) {
			Q3ABspEpair *next = epair->next;
			free(epair->key);
			free(epair->value);
			free(epair);
			epair = next;
		}
	}

	free(q3aBspEntities);
	q3aBspEntities = NULL;
	q3aBspEntityCount = 0;
	q3aBspEntityPairs = 0;
}

void Q3A_BotLibImport_ClearBspEntityData(void) {
	Q3A_BotLibImport_FreeBspEntityData();
	q3aSmokeStatus.bspEntityLoadAttempted = qfalse;
	q3aSmokeStatus.bspEntityLoaded = qfalse;
	q3aSmokeStatus.bspEntityCount = 0;
	q3aSmokeStatus.bspEntityPairs = 0;
	q3aSmokeStatus.bspEntityValueSmokePassed = qfalse;
	q3aSmokeStatus.bspEntityMessage = "Q3A BSP entity data has not run";
}

static void Q3A_BotLibImport_FreeBspModelData(void) {
	free(q3aBspModels);
	q3aBspModels = NULL;
	q3aBspModelCount = 0;
}

void Q3A_BotLibImport_ClearBspModelData(void) {
	Q3A_BotLibImport_FreeBspModelData();
	q3aSmokeStatus.bspModelLoadAttempted = qfalse;
	q3aSmokeStatus.bspModelLoaded = qfalse;
	q3aSmokeStatus.bspModelCount = 0;
	q3aSmokeStatus.bspModelBoundsSmokePassed = qfalse;
	q3aSmokeStatus.bspModelMessage = "Q3A BSP model data has not run";
}

static void Q3A_BotLibImport_ResetBspLeafLinkStatus(const char *message) {
	q3aSmokeStatus.bspLeafLinkAttempted = qfalse;
	q3aSmokeStatus.bspLeafLinks = q3aBspLeafLinkCount;
	q3aSmokeStatus.bspLeafLinkFailures = 0;
	q3aSmokeStatus.bspBoxEntitiesSmokePassed = qfalse;
	q3aSmokeStatus.bspBoxEntitiesCount = 0;
	q3aSmokeStatus.bspLeafLinkMessage = message;
}

static void Q3A_BotLibImport_ClearBspLeafLinks(void) {
	int leafnum;

	if (q3aBspLeafLinkedEntities != NULL) {
		for (leafnum = 0; leafnum < q3aBspLeafCount; ++leafnum) {
			bsp_link_t *link = q3aBspLeafLinkedEntities[leafnum];
			while (link != NULL) {
				bsp_link_t *nextLink = link->next_ent;
				free(link);
				link = nextLink;
			}
			q3aBspLeafLinkedEntities[leafnum] = NULL;
		}
	}

	if (aasworld.entities != NULL && aasworld.maxentities > 0) {
		int entnum;
		for (entnum = 0; entnum < aasworld.maxentities; ++entnum) {
			aasworld.entities[entnum].leaves = NULL;
		}
	}

	q3aBspLeafLinkCount = 0;
	q3aSmokeStatus.bspLeafLinks = 0;
}

static void Q3A_BotLibImport_FreeBspCollisionData(void) {
	Q3A_BotLibImport_ClearBspLeafLinks();
	free(q3aBspLeafLinkedEntities);
	free(q3aBspPlanes);
	free(q3aBspNodes);
	free(q3aBspLeafs);
	free(q3aBspLeafBrushes);
	free(q3aBspBrushSides);
	free(q3aBspBrushes);
	free(q3aBspBrushCheckCounts);
	q3aBspLeafLinkedEntities = NULL;
	q3aBspPlanes = NULL;
	q3aBspNodes = NULL;
	q3aBspLeafs = NULL;
	q3aBspLeafBrushes = NULL;
	q3aBspBrushSides = NULL;
	q3aBspBrushes = NULL;
	q3aBspBrushCheckCounts = NULL;
	q3aBspPlaneCount = 0;
	q3aBspNodeCount = 0;
	q3aBspLeafCount = 0;
	q3aBspLeafBrushCount = 0;
	q3aBspBrushSideCount = 0;
	q3aBspBrushCount = 0;
	q3aBspBrushCheckCountSize = 0;
	q3aBspTraceCheckCount = 1;
	Q3A_BotLibImport_ResetBspLeafLinkStatus("Q3A BSP leaf entity links have not run");
}

void Q3A_BotLibImport_ClearBspCollisionData(void) {
	Q3A_BotLibImport_FreeBspCollisionData();
	q3aSmokeStatus.bspCollisionLoadAttempted = qfalse;
	q3aSmokeStatus.bspCollisionLoaded = qfalse;
	q3aSmokeStatus.bspCollisionPlanes = 0;
	q3aSmokeStatus.bspCollisionNodes = 0;
	q3aSmokeStatus.bspCollisionLeafs = 0;
	q3aSmokeStatus.bspCollisionBrushes = 0;
	q3aSmokeStatus.bspCollisionPointContentsSmokePassed = qfalse;
	q3aSmokeStatus.bspCollisionTraceSmokePassed = qfalse;
	q3aSmokeStatus.bspCollisionMessage = "Q3A BSP collision data has not run";
}

static void Q3A_BotLibImport_FreeBspVisibilityData(void) {
	free(q3aBspVisData);
	free(q3aBspVisOffsets);
	q3aBspVisData = NULL;
	q3aBspVisOffsets = NULL;
	q3aBspVisLength = 0;
	q3aBspVisClusterCount = 0;
}

void Q3A_BotLibImport_ClearBspVisibilityData(void) {
	Q3A_BotLibImport_FreeBspVisibilityData();
	q3aSmokeStatus.bspVisibilityLoadAttempted = qfalse;
	q3aSmokeStatus.bspVisibilityLoaded = qfalse;
	q3aSmokeStatus.bspVisibilityClusters = 0;
	q3aSmokeStatus.bspVisibilityPvsSmokePassed = qfalse;
	q3aSmokeStatus.bspVisibilityPhsSmokePassed = qfalse;
	q3aSmokeStatus.bspVisibilityMessage = "Q3A BSP visibility data has not run";
}

static int Q3A_BotLibImport_VisRowBytes(void) {
	return (q3aBspVisClusterCount + 7) >> 3;
}

static int Q3A_BotLibImport_VisOffset(int cluster, int mode) {
	if (cluster < 0 ||
		cluster >= q3aBspVisClusterCount ||
		mode < Q3A_Q2_DVIS_PVS ||
		mode > Q3A_Q2_DVIS_PHS ||
		q3aBspVisOffsets == NULL) {
		return -1;
	}
	return q3aBspVisOffsets[cluster * 2 + mode];
}

static int Q3A_BotLibImport_DecompressVisByte(int cluster, int mode, int targetByte) {
	const int rowBytes = Q3A_BotLibImport_VisRowBytes();
	int offset = Q3A_BotLibImport_VisOffset(cluster, mode);
	int outByte = 0;

	if (offset < 0 || offset >= q3aBspVisLength || targetByte < 0 || targetByte >= rowBytes) {
		return -1;
	}

	while (outByte < rowBytes && offset < q3aBspVisLength) {
		const int value = q3aBspVisData[offset++];
		if (value != 0) {
			if (outByte == targetByte) {
				return value;
			}
			++outByte;
			continue;
		}

		if (offset >= q3aBspVisLength) {
			return -1;
		}

		{
			const int count = q3aBspVisData[offset++];
			if (count <= 0) {
				return -1;
			}
			if (targetByte >= outByte && targetByte < outByte + count) {
				return 0;
			}
			outByte += count;
		}
	}

	return -1;
}

static int Q3A_BotLibImport_ClusterVisible(int fromCluster, int toCluster, int mode) {
	const int targetByte = toCluster >> 3;
	int value;

	if (fromCluster < 0 ||
		toCluster < 0 ||
		fromCluster >= q3aBspVisClusterCount ||
		toCluster >= q3aBspVisClusterCount) {
		return qfalse;
	}
	if (fromCluster == toCluster) {
		return qtrue;
	}

	value = Q3A_BotLibImport_DecompressVisByte(fromCluster, mode, targetByte);
	if (value < 0) {
		return qfalse;
	}
	return (value & (1 << (toCluster & 7))) != 0;
}

static int Q3A_BotLibImport_CountVisibleClusters(int fromCluster, int mode) {
	const int rowBytes = Q3A_BotLibImport_VisRowBytes();
	int offset = Q3A_BotLibImport_VisOffset(fromCluster, mode);
	int outByte = 0;
	int count = 0;

	if (offset < 0 || offset >= q3aBspVisLength || fromCluster < 0 || fromCluster >= q3aBspVisClusterCount) {
		return -1;
	}

	while (outByte < rowBytes && offset < q3aBspVisLength) {
		const int value = q3aBspVisData[offset++];
		int bit;

		if (value == 0) {
			int skip;
			if (offset >= q3aBspVisLength) {
				return -1;
			}
			skip = q3aBspVisData[offset++];
			if (skip <= 0) {
				return -1;
			}
			outByte += skip;
			continue;
		}

		for (bit = 0; bit < 8; ++bit) {
			const int visibleCluster = outByte * 8 + bit;
			if (visibleCluster >= q3aBspVisClusterCount) {
				break;
			}
			if (value & (1 << bit)) {
				++count;
			}
		}
		++outByte;
	}

	if (outByte < rowBytes) {
		return -1;
	}
	return count;
}

static float Q3A_BotLibImport_ClampFloat(float value, float minValue, float maxValue) {
	if (value < minValue) {
		return minValue;
	}
	if (value > maxValue) {
		return maxValue;
	}
	return value;
}

static void Q3A_BotLibImport_LerpVector(vec3_t from, vec3_t to, float frac, vec3_t out) {
	out[0] = from[0] + (to[0] - from[0]) * frac;
	out[1] = from[1] + (to[1] - from[1]) * frac;
	out[2] = from[2] + (to[2] - from[2]) * frac;
}

static int Q3A_BotLibImport_VectorsEqual(const vec3_t a, const vec3_t b) {
	return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static int Q3A_BotLibImport_PlaneSignbits(const vec3_t normal) {
	int bits = 0;

	if (normal[0] < 0.0f) {
		bits |= 1;
	}
	if (normal[1] < 0.0f) {
		bits |= 2;
	}
	if (normal[2] < 0.0f) {
		bits |= 4;
	}
	return bits;
}

static int Q3A_BotLibImport_PlaneType(const Q3ABspPlane *plane) {
	if (plane->type >= PLANE_X && plane->type <= PLANE_Z) {
		return plane->type;
	}
	return PLANE_NON_AXIAL;
}

static void Q3A_BotLibImport_FillCPlane(cplane_t *out, const Q3ABspPlane *in) {
	VectorCopy(in->normal, out->normal);
	out->dist = in->dist;
	out->type = (byte)Q3A_BotLibImport_PlaneType(in);
	out->signbits = (byte)Q3A_BotLibImport_PlaneSignbits(in->normal);
	out->pad[0] = 0;
	out->pad[1] = 0;
}

static int Q3A_BotLibImport_WorldHeadNode(void) {
	if (q3aBspModels != NULL &&
		q3aBspModelCount > 0 &&
		q3aBspModels[0].headnode >= 0 &&
		q3aBspModels[0].headnode < q3aBspNodeCount) {
		return q3aBspModels[0].headnode;
	}
	return 0;
}

static int Q3A_BotLibImport_EnsureBrushCheckCounts(void) {
	if (q3aBspBrushCount <= 0) {
		return qfalse;
	}
	if (q3aBspBrushCheckCounts != NULL && q3aBspBrushCheckCountSize >= q3aBspBrushCount) {
		return qtrue;
	}

	free(q3aBspBrushCheckCounts);
	q3aBspBrushCheckCounts = (int *)calloc((size_t)q3aBspBrushCount, sizeof(*q3aBspBrushCheckCounts));
	q3aBspBrushCheckCountSize = q3aBspBrushCheckCounts != NULL ? q3aBspBrushCount : 0;
	q3aBspTraceCheckCount = 1;
	return q3aBspBrushCheckCounts != NULL;
}

static void Q3A_BotLibImport_NextCheckCount(void) {
	++q3aBspTraceCheckCount;
	if (q3aBspTraceCheckCount <= 0) {
		memset(
			q3aBspBrushCheckCounts,
			0,
			(size_t)q3aBspBrushCheckCountSize * sizeof(*q3aBspBrushCheckCounts));
		q3aBspTraceCheckCount = 1;
	}
}

static void Q3A_BotLibImport_ClearTrace(bsp_trace_t *trace, vec3_t start) {
	Com_Memset(trace, 0, sizeof(*trace));
	trace->fraction = 1.0f;
	trace->sidenum = -1;
	trace->ent = 0;
	if (start != NULL) {
		VectorCopy(start, trace->endpos);
	}
}

static void Q3A_BotLibImport_SetSurface(bsp_trace_t *trace, const Q3ABspBrushSide *side) {
	trace->sidenum = (int)(side - q3aBspBrushSides);
	trace->surface.name[0] = '\0';
	trace->surface.flags = 0;
	trace->surface.value = 0;
}

static int Q3A_BotLibImport_PointLeafNum(vec3_t point) {
	int nodenum = Q3A_BotLibImport_WorldHeadNode();

	while (nodenum >= 0) {
		Q3ABspNode *node;
		Q3ABspPlane *plane;
		float dist;

		if (nodenum >= q3aBspNodeCount) {
			return 0;
		}

		node = &q3aBspNodes[nodenum];
		if (node->planenum < 0 || node->planenum >= q3aBspPlaneCount) {
			return 0;
		}

		plane = &q3aBspPlanes[node->planenum];
		dist = DotProduct(point, plane->normal) - plane->dist;
		nodenum = node->children[dist < 0.0f];
	}

	return -1 - nodenum;
}

static void Q3A_BotLibImport_ClipBoxToBrush(vec3_t start, vec3_t end, bsp_trace_t *trace, Q3ABspBrush *brush) {
	int i;
	float enterFrac = -1.0f;
	float leaveFrac = 1.0f;
	qboolean getOut = qfalse;
	qboolean startOut = qfalse;
	const Q3ABspPlane *clipPlane = NULL;
	const Q3ABspBrushSide *leadSide = NULL;

	if (brush->numsides <= 0 ||
		brush->firstside < 0 ||
		brush->firstside + brush->numsides > q3aBspBrushSideCount) {
		return;
	}

	for (i = 0; i < brush->numsides; ++i) {
		Q3ABspBrushSide *side = &q3aBspBrushSides[brush->firstside + i];
		Q3ABspPlane *plane;
		int signbits;
		float dist;
		float d1;
		float d2;

		if (side->planenum < 0 || side->planenum >= q3aBspPlaneCount) {
			continue;
		}

		plane = &q3aBspPlanes[side->planenum];
		signbits = Q3A_BotLibImport_PlaneSignbits(plane->normal);
		if (!q3aBspTraceIsPoint) {
			dist = plane->dist - DotProduct(q3aBspTraceOffsets[signbits], plane->normal);
		} else {
			dist = plane->dist;
		}

		d1 = DotProduct(start, plane->normal) - dist;
		d2 = DotProduct(end, plane->normal) - dist;

		if (d2 > 0.0f) {
			getOut = qtrue;
		}
		if (d1 > 0.0f) {
			startOut = qtrue;
		}

		if (d1 > 0.0f && (d2 >= (1.0f / 32.0f) || d2 >= d1)) {
			return;
		}

		if (d1 <= 0.0f && d2 <= 0.0f) {
			continue;
		}

		if (d1 > d2) {
			float frac = (d1 - (1.0f / 32.0f)) / (d1 - d2);
			if (frac < 0.0f) {
				frac = 0.0f;
			}
			if (frac > enterFrac) {
				enterFrac = frac;
				clipPlane = plane;
				leadSide = side;
			}
		} else {
			float frac = (d1 + (1.0f / 32.0f)) / (d1 - d2);
			if (frac > 1.0f) {
				frac = 1.0f;
			}
			if (frac < leaveFrac) {
				leaveFrac = frac;
			}
		}
	}

	if (!startOut) {
		trace->startsolid = qtrue;
		if (!getOut) {
			trace->allsolid = qtrue;
			trace->fraction = 0.0f;
			trace->contents = brush->contents;
		}
		return;
	}

	if (enterFrac < leaveFrac && enterFrac > -1.0f && enterFrac < trace->fraction) {
		trace->fraction = enterFrac;
		if (clipPlane != NULL) {
			Q3A_BotLibImport_FillCPlane(&trace->plane, clipPlane);
		}
		if (leadSide != NULL) {
			Q3A_BotLibImport_SetSurface(trace, leadSide);
		}
		trace->contents = brush->contents;
	}
}

static void Q3A_BotLibImport_TestBoxInBrush(vec3_t point, bsp_trace_t *trace, Q3ABspBrush *brush) {
	int i;

	if (brush->numsides <= 0 ||
		brush->firstside < 0 ||
		brush->firstside + brush->numsides > q3aBspBrushSideCount) {
		return;
	}

	for (i = 0; i < brush->numsides; ++i) {
		Q3ABspBrushSide *side = &q3aBspBrushSides[brush->firstside + i];
		Q3ABspPlane *plane;
		int signbits;
		float dist;
		float d1;

		if (side->planenum < 0 || side->planenum >= q3aBspPlaneCount) {
			continue;
		}

		plane = &q3aBspPlanes[side->planenum];
		signbits = Q3A_BotLibImport_PlaneSignbits(plane->normal);
		dist = plane->dist - DotProduct(q3aBspTraceOffsets[signbits], plane->normal);
		d1 = DotProduct(point, plane->normal) - dist;
		if (d1 > 0.0f) {
			return;
		}
	}

	trace->startsolid = qtrue;
	trace->allsolid = qtrue;
	trace->fraction = 0.0f;
	trace->contents = brush->contents;
}

static void Q3A_BotLibImport_TraceToLeaf(int leafnum) {
	int k;
	Q3ABspLeaf *leaf;

	if (leafnum < 0 || leafnum >= q3aBspLeafCount) {
		return;
	}

	leaf = &q3aBspLeafs[leafnum];
	if (!(leaf->contents & q3aBspTraceContents)) {
		return;
	}

	for (k = 0; k < leaf->numleafbrushes; ++k) {
		const int leafBrushIndex = leaf->firstleafbrush + k;
		int brushnum;
		Q3ABspBrush *brush;

		if (leafBrushIndex < 0 || leafBrushIndex >= q3aBspLeafBrushCount) {
			continue;
		}

		brushnum = q3aBspLeafBrushes[leafBrushIndex];
		if (brushnum < 0 || brushnum >= q3aBspBrushCount) {
			continue;
		}

		if (q3aBspBrushCheckCounts[brushnum] == q3aBspTraceCheckCount) {
			continue;
		}
		q3aBspBrushCheckCounts[brushnum] = q3aBspTraceCheckCount;

		brush = &q3aBspBrushes[brushnum];
		if (!(brush->contents & q3aBspTraceContents)) {
			continue;
		}

		Q3A_BotLibImport_ClipBoxToBrush(q3aBspTraceStart, q3aBspTraceEnd, q3aBspTrace, brush);
		if (q3aBspTrace->fraction == 0.0f) {
			return;
		}
	}
}

static void Q3A_BotLibImport_TestInLeaf(int leafnum) {
	int k;
	Q3ABspLeaf *leaf;

	if (leafnum < 0 || leafnum >= q3aBspLeafCount) {
		return;
	}

	leaf = &q3aBspLeafs[leafnum];
	if (!(leaf->contents & q3aBspTraceContents)) {
		return;
	}

	for (k = 0; k < leaf->numleafbrushes; ++k) {
		const int leafBrushIndex = leaf->firstleafbrush + k;
		int brushnum;
		Q3ABspBrush *brush;

		if (leafBrushIndex < 0 || leafBrushIndex >= q3aBspLeafBrushCount) {
			continue;
		}

		brushnum = q3aBspLeafBrushes[leafBrushIndex];
		if (brushnum < 0 || brushnum >= q3aBspBrushCount) {
			continue;
		}

		if (q3aBspBrushCheckCounts[brushnum] == q3aBspTraceCheckCount) {
			continue;
		}
		q3aBspBrushCheckCounts[brushnum] = q3aBspTraceCheckCount;

		brush = &q3aBspBrushes[brushnum];
		if (!(brush->contents & q3aBspTraceContents)) {
			continue;
		}

		Q3A_BotLibImport_TestBoxInBrush(q3aBspTraceStart, q3aBspTrace, brush);
		if (q3aBspTrace->fraction == 0.0f) {
			return;
		}
	}
}

static int Q3A_BotLibImport_BoxOnPlaneSide(vec3_t mins, vec3_t maxs, Q3ABspPlane *plane) {
	float dist1 = 0.0f;
	float dist2 = 0.0f;
	int i;

	if (plane->type >= PLANE_X && plane->type <= PLANE_Z) {
		if (mins[plane->type] >= plane->dist) {
			return 1;
		}
		if (maxs[plane->type] < plane->dist) {
			return 2;
		}
		return 3;
	}

	for (i = 0; i < 3; ++i) {
		if (plane->normal[i] >= 0.0f) {
			dist1 += plane->normal[i] * maxs[i];
			dist2 += plane->normal[i] * mins[i];
		} else {
			dist1 += plane->normal[i] * mins[i];
			dist2 += plane->normal[i] * maxs[i];
		}
	}

	if (dist2 >= plane->dist) {
		return 1;
	}
	if (dist1 < plane->dist) {
		return 2;
	}
	return 3;
}

static void Q3A_BotLibImport_BoxLeafs_r(int nodenum, vec3_t mins, vec3_t maxs) {
	while (nodenum >= 0) {
		Q3ABspNode *node;
		Q3ABspPlane *plane;
		int side;

		if (nodenum >= q3aBspNodeCount) {
			return;
		}

		node = &q3aBspNodes[nodenum];
		if (node->planenum < 0 || node->planenum >= q3aBspPlaneCount) {
			return;
		}

		plane = &q3aBspPlanes[node->planenum];
		side = Q3A_BotLibImport_BoxOnPlaneSide(mins, maxs, plane);
		if (side == 1) {
			nodenum = node->children[0];
			continue;
		}
		if (side == 2) {
			nodenum = node->children[1];
			continue;
		}

		Q3A_BotLibImport_BoxLeafs_r(node->children[0], mins, maxs);
		nodenum = node->children[1];
	}

	Q3A_BotLibImport_TestInLeaf(-1 - nodenum);
}

static void Q3A_BotLibImport_RecursiveHullCheck(int nodenum, float p1f, float p2f, vec3_t p1, vec3_t p2) {
	Q3ABspNode *node;
	Q3ABspPlane *plane;
	float t1;
	float t2;
	float offset;
	float frac;
	float frac2;
	float idist;
	vec3_t mid;
	int side;
	float midf;

	if (q3aBspTrace->fraction <= p1f) {
		return;
	}

	if (nodenum < 0) {
		Q3A_BotLibImport_TraceToLeaf(-1 - nodenum);
		return;
	}

	if (nodenum >= q3aBspNodeCount) {
		return;
	}

	node = &q3aBspNodes[nodenum];
	if (node->planenum < 0 || node->planenum >= q3aBspPlaneCount) {
		return;
	}

	plane = &q3aBspPlanes[node->planenum];
	if (plane->type >= PLANE_X && plane->type <= PLANE_Z) {
		t1 = p1[plane->type] - plane->dist;
		t2 = p2[plane->type] - plane->dist;
		offset = q3aBspTraceExtents[plane->type];
	} else {
		t1 = DotProduct(p1, plane->normal) - plane->dist;
		t2 = DotProduct(p2, plane->normal) - plane->dist;
		if (q3aBspTraceIsPoint) {
			offset = 0.0f;
		} else {
			offset =
				fabsf(q3aBspTraceExtents[0] * plane->normal[0]) +
				fabsf(q3aBspTraceExtents[1] * plane->normal[1]) +
				fabsf(q3aBspTraceExtents[2] * plane->normal[2]);
		}
	}

	if (t1 >= offset && t2 >= offset) {
		Q3A_BotLibImport_RecursiveHullCheck(node->children[0], p1f, p2f, p1, p2);
		return;
	}
	if (t1 < -offset && t2 < -offset) {
		Q3A_BotLibImport_RecursiveHullCheck(node->children[1], p1f, p2f, p1, p2);
		return;
	}

	if (t1 < t2) {
		idist = 1.0f / (t1 - t2);
		side = 1;
		frac2 = (t1 + offset + (1.0f / 32.0f)) * idist;
		frac = (t1 - offset + (1.0f / 32.0f)) * idist;
	} else if (t1 > t2) {
		idist = 1.0f / (t1 - t2);
		side = 0;
		frac2 = (t1 - offset - (1.0f / 32.0f)) * idist;
		frac = (t1 + offset + (1.0f / 32.0f)) * idist;
	} else {
		side = 0;
		frac = 1.0f;
		frac2 = 0.0f;
	}

	frac = Q3A_BotLibImport_ClampFloat(frac, 0.0f, 1.0f);
	frac2 = Q3A_BotLibImport_ClampFloat(frac2, 0.0f, 1.0f);

	midf = p1f + (p2f - p1f) * frac;
	Q3A_BotLibImport_LerpVector(p1, p2, frac, mid);
	Q3A_BotLibImport_RecursiveHullCheck(node->children[side], p1f, midf, p1, mid);

	midf = p1f + (p2f - p1f) * frac2;
	Q3A_BotLibImport_LerpVector(p1, p2, frac2, mid);
	Q3A_BotLibImport_RecursiveHullCheck(node->children[side ^ 1], midf, p2f, mid, p2);
}

static int Q3A_BotLibImport_PointContentsQ2Bsp(vec3_t point) {
	int leafnum;

	if (!q3aSmokeStatus.bspCollisionLoaded || point == NULL) {
		return 0;
	}

	leafnum = Q3A_BotLibImport_PointLeafNum(point);
	if (leafnum < 0 || leafnum >= q3aBspLeafCount) {
		return 0;
	}

	return q3aBspLeafs[leafnum].contents;
}

static int Q3A_BotLibImport_PointClusterQ2Bsp(vec3_t point) {
	int leafnum;

	if (!q3aSmokeStatus.bspCollisionLoaded || point == NULL) {
		return -1;
	}

	leafnum = Q3A_BotLibImport_PointLeafNum(point);
	if (leafnum < 0 || leafnum >= q3aBspLeafCount) {
		return -1;
	}

	return q3aBspLeafs[leafnum].cluster;
}

static qboolean Q3A_BotLibImport_PointsVisibleQ2Bsp(vec3_t p1, vec3_t p2, int mode) {
	int cluster1;
	int cluster2;

	if (p1 == NULL || p2 == NULL) {
		return qfalse;
	}

	if (!q3aSmokeStatus.bspVisibilityLoaded) {
		return qtrue;
	}

	cluster1 = Q3A_BotLibImport_PointClusterQ2Bsp(p1);
	cluster2 = Q3A_BotLibImport_PointClusterQ2Bsp(p2);
	return Q3A_BotLibImport_ClusterVisible(cluster1, cluster2, mode) ? qtrue : qfalse;
}

static bsp_trace_t Q3A_BotLibImport_TraceQ2Bsp(
	vec3_t start,
	vec3_t mins,
	vec3_t maxs,
	vec3_t end,
	int contentmask) {
	bsp_trace_t trace;
	vec3_t traceMins;
	vec3_t traceMaxs;
	vec3_t zero;
	const vec_t *bounds[2];
	int i;
	int j;

	VectorClear(zero);
	Q3A_BotLibImport_ClearTrace(&trace, start != NULL ? start : zero);
	if (!q3aSmokeStatus.bspCollisionLoaded ||
		start == NULL ||
		end == NULL ||
		!Q3A_BotLibImport_EnsureBrushCheckCounts()) {
		if (end != NULL) {
			VectorCopy(end, trace.endpos);
		}
		return trace;
	}

	if (mins == NULL) {
		mins = zero;
	}
	if (maxs == NULL) {
		maxs = zero;
	}
	VectorCopy(mins, traceMins);
	VectorCopy(maxs, traceMaxs);

	q3aBspTrace = &trace;
	q3aBspTraceContents = contentmask;
	VectorCopy(start, q3aBspTraceStart);
	VectorCopy(end, q3aBspTraceEnd);

	bounds[0] = traceMins;
	bounds[1] = traceMaxs;
	for (i = 0; i < 8; ++i) {
		for (j = 0; j < 3; ++j) {
			q3aBspTraceOffsets[i][j] = bounds[(i >> j) & 1][j];
		}
	}

	Q3A_BotLibImport_NextCheckCount();

	if (Q3A_BotLibImport_VectorsEqual(start, end)) {
		vec3_t c1;
		vec3_t c2;

		for (i = 0; i < 3; ++i) {
			c1[i] = start[i] + traceMins[i] - 1.0f;
			c2[i] = start[i] + traceMaxs[i] + 1.0f;
		}

		Q3A_BotLibImport_BoxLeafs_r(Q3A_BotLibImport_WorldHeadNode(), c1, c2);
		VectorCopy(start, trace.endpos);
		return trace;
	}

	if (Q3A_BotLibImport_VectorsEqual(traceMins, zero) && Q3A_BotLibImport_VectorsEqual(traceMaxs, zero)) {
		q3aBspTraceIsPoint = qtrue;
		VectorClear(q3aBspTraceExtents);
	} else {
		q3aBspTraceIsPoint = qfalse;
		q3aBspTraceExtents[0] = fabsf(traceMins[0]) > fabsf(traceMaxs[0]) ? fabsf(traceMins[0]) : fabsf(traceMaxs[0]);
		q3aBspTraceExtents[1] = fabsf(traceMins[1]) > fabsf(traceMaxs[1]) ? fabsf(traceMins[1]) : fabsf(traceMaxs[1]);
		q3aBspTraceExtents[2] = fabsf(traceMins[2]) > fabsf(traceMaxs[2]) ? fabsf(traceMins[2]) : fabsf(traceMaxs[2]);
	}

	Q3A_BotLibImport_RecursiveHullCheck(Q3A_BotLibImport_WorldHeadNode(), 0.0f, 1.0f, start, end);
	if (trace.fraction == 1.0f) {
		VectorCopy(end, trace.endpos);
	} else {
		Q3A_BotLibImport_LerpVector(start, end, trace.fraction, trace.endpos);
	}
	return trace;
}

static int Q3A_BotLibImport_IsEntityWhitespace(unsigned char value) {
	return value == '\0' || isspace(value);
}

static const char *Q3A_BotLibImport_SkipEntityWhitespace(const char *cursor, const char *end) {
	while (cursor < end && Q3A_BotLibImport_IsEntityWhitespace((unsigned char)*cursor)) {
		++cursor;
	}
	return cursor;
}

static char *Q3A_BotLibImport_DupRange(const char *start, size_t length) {
	char *out = (char *)malloc(length + 1);
	if (out == NULL) {
		return NULL;
	}
	if (length > 0) {
		memcpy(out, start, length);
	}
	out[length] = '\0';
	return out;
}

static char *Q3A_BotLibImport_ParseEntityToken(const char **cursor, const char *end) {
	const char *scan;
	char *out;
	size_t length;

	*cursor = Q3A_BotLibImport_SkipEntityWhitespace(*cursor, end);
	if (*cursor >= end || **cursor == '{' || **cursor == '}') {
		return NULL;
	}

	if (**cursor != '"') {
		scan = *cursor;
		while (scan < end &&
			!Q3A_BotLibImport_IsEntityWhitespace((unsigned char)*scan) &&
			*scan != '{' &&
			*scan != '}') {
			++scan;
		}
		if (scan == *cursor) {
			return NULL;
		}
		out = Q3A_BotLibImport_DupRange(*cursor, (size_t)(scan - *cursor));
		*cursor = scan;
		return out;
	}

	++(*cursor);
	length = 0;
	out = (char *)malloc((size_t)(end - *cursor) + 1);
	if (out == NULL) {
		return NULL;
	}

	while (*cursor < end && **cursor != '"') {
		if (**cursor == '\\' && (*cursor + 1) < end) {
			++(*cursor);
		}
		out[length++] = **cursor;
		++(*cursor);
	}

	if (*cursor >= end) {
		free(out);
		return NULL;
	}

	++(*cursor);
	out[length] = '\0';
	return out;
}

static int Q3A_BotLibImport_AddBspEntity(void) {
	Q3ABspEntity *nextEntities;
	const int entityIndex = q3aBspEntityCount;

	if (q3aBspEntityCount >= Q3A_MAX_BSP_ENTITIES) {
		return -1;
	}

	nextEntities = (Q3ABspEntity *)realloc(
		q3aBspEntities,
		(size_t)(q3aBspEntityCount + 1) * sizeof(*q3aBspEntities));
	if (nextEntities == NULL) {
		return -1;
	}

	q3aBspEntities = nextEntities;
	q3aBspEntities[entityIndex].epairs = NULL;
	++q3aBspEntityCount;
	return entityIndex;
}

static int Q3A_BotLibImport_AddBspEpair(int ent, char *key, char *value) {
	Q3ABspEpair *epair;
	Q3ABspEpair **tail;

	if (ent < 0 || ent >= q3aBspEntityCount || key == NULL || value == NULL) {
		return qfalse;
	}

	epair = (Q3ABspEpair *)malloc(sizeof(*epair));
	if (epair == NULL) {
		return qfalse;
	}

	epair->key = key;
	epair->value = value;
	epair->next = NULL;

	tail = &q3aBspEntities[ent].epairs;
	while (*tail != NULL) {
		tail = &(*tail)->next;
	}
	*tail = epair;
	++q3aBspEntityPairs;
	return qtrue;
}

static int Q3A_BotLibImport_ParseBspEntities(const char *data, int length, const char **errorMessage) {
	const char *cursor = data;
	const char *end = data + length;

	while (1) {
		int ent;

		cursor = Q3A_BotLibImport_SkipEntityWhitespace(cursor, end);
		if (cursor >= end) {
			break;
		}

		if (*cursor != '{') {
			*errorMessage = "expected entity open brace";
			return qfalse;
		}
		++cursor;

		ent = Q3A_BotLibImport_AddBspEntity();
		if (ent < 0) {
			*errorMessage = "could not allocate entity record";
			return qfalse;
		}

		while (1) {
			char *key;
			char *value;

			cursor = Q3A_BotLibImport_SkipEntityWhitespace(cursor, end);
			if (cursor >= end) {
				*errorMessage = "unterminated entity";
				return qfalse;
			}
			if (*cursor == '}') {
				++cursor;
				break;
			}
			if (*cursor == '{') {
				*errorMessage = "unexpected nested entity open brace";
				return qfalse;
			}

			key = Q3A_BotLibImport_ParseEntityToken(&cursor, end);
			value = Q3A_BotLibImport_ParseEntityToken(&cursor, end);
			if (key == NULL || value == NULL) {
				free(key);
				free(value);
				*errorMessage = "expected entity key/value pair";
				return qfalse;
			}
			if (!Q3A_BotLibImport_AddBspEpair(ent, key, value)) {
				free(key);
				free(value);
				*errorMessage = "could not allocate entity key/value pair";
				return qfalse;
			}
		}
	}

	if (q3aBspEntityCount <= 0) {
		*errorMessage = "entity lump is empty";
		return qfalse;
	}

	return qtrue;
}

static int Q3A_BotLibImport_ValueForBspEpairKeyInternal(
	int ent,
	const char *key,
	char *value,
	int size,
	int allowWorldspawn) {
	Q3ABspEpair *epair;

	if (value != NULL && size > 0) {
		value[0] = '\0';
	}

	if (key == NULL || value == NULL || size <= 0) {
		return qfalse;
	}

	if (ent >= q3aBspEntityCount || ent < (allowWorldspawn ? 0 : 1)) {
		return qfalse;
	}

	for (epair = q3aBspEntities[ent].epairs; epair != NULL; epair = epair->next) {
		if (strcmp(epair->key, key) == 0) {
			strncpy(value, epair->value, (size_t)(size - 1));
			value[size - 1] = '\0';
			return qtrue;
		}
	}

	return qfalse;
}

static void Q3A_BotLibImport_RunBspEntitySmoke(const char *name) {
	char classname[128];
	const char *loadName = (name != NULL && name[0] != '\0') ? name : "<memory>";

	q3aSmokeStatus.bspEntityValueSmokePassed = qfalse;
	if (q3aBspEntityCount > 1 &&
		Q3A_BotLibImport_ValueForBspEpairKeyInternal(1, "classname", classname, sizeof(classname), qfalse)) {
		q3aSmokeStatus.bspEntityValueSmokePassed = qtrue;
		Q3A_BotLibImport_SetBspEntityMessage(
			"Q3A BSP entity lump load passed: %s entities=%d epairs=%d first_classname=%s",
			loadName,
			q3aBspEntityCount,
			q3aBspEntityPairs,
			classname);
		return;
	}

	if (Q3A_BotLibImport_ValueForBspEpairKeyInternal(0, "classname", classname, sizeof(classname), qtrue)) {
		q3aSmokeStatus.bspEntityValueSmokePassed = qtrue;
		Q3A_BotLibImport_SetBspEntityMessage(
			"Q3A BSP entity lump load passed: %s entities=%d epairs=%d worldspawn=%s",
			loadName,
			q3aBspEntityCount,
			q3aBspEntityPairs,
			classname);
		return;
	}

	Q3A_BotLibImport_SetBspEntityMessage(
		"Q3A BSP entity lump loaded without a classname smoke value: %s entities=%d epairs=%d",
		loadName,
		q3aBspEntityCount,
		q3aBspEntityPairs);
}

int Q3A_BotLibImport_LoadBspEntityData(const char *name, const void *data, int length) {
	const char *errorMessage = "unknown error";

	if (!q3aSmokeStatus.initialized) {
		Q3A_BotLibImport_Init();
	}

	Q3A_BotLibImport_ClearBspEntityData();
	q3aSmokeStatus.bspEntityLoadAttempted = qtrue;
	q3aSmokeStatus.bspEntityMessage = q3aBspEntityMessage;

	if (data == NULL || length <= 0) {
		Q3A_BotLibImport_SetBspEntityMessage("Q3A BSP entity lump load failed: empty buffer");
		return qfalse;
	}

	if (!Q3A_BotLibImport_ParseBspEntities((const char *)data, length, &errorMessage)) {
		Q3A_BotLibImport_FreeBspEntityData();
		q3aSmokeStatus.bspEntityLoaded = qfalse;
		q3aSmokeStatus.bspEntityCount = 0;
		q3aSmokeStatus.bspEntityPairs = 0;
		q3aSmokeStatus.bspEntityValueSmokePassed = qfalse;
		Q3A_BotLibImport_SetBspEntityMessage(
			"Q3A BSP entity lump load failed: %s",
			errorMessage != NULL ? errorMessage : "unknown error");
		return qfalse;
	}

	q3aSmokeStatus.bspEntityLoaded = qtrue;
	q3aSmokeStatus.bspEntityCount = q3aBspEntityCount;
	q3aSmokeStatus.bspEntityPairs = q3aBspEntityPairs;
	Q3A_BotLibImport_RunBspEntitySmoke(name);
	return qtrue;
}

static int Q3A_BotLibImport_ModelBoundsNonZero(const vec3_t mins, const vec3_t maxs) {
	return mins[0] != 0.0f || mins[1] != 0.0f || mins[2] != 0.0f ||
		maxs[0] != 0.0f || maxs[1] != 0.0f || maxs[2] != 0.0f;
}

static void Q3A_BotLibImport_RunBspModelSmoke(const char *name) {
	vec3_t angles;
	vec3_t mins;
	vec3_t maxs;
	vec3_t origin;
	const char *loadName = (name != NULL && name[0] != '\0') ? name : "<memory>";
	int smokeModel = q3aBspModelCount > 1 ? 1 : 0;

	q3aSmokeStatus.bspModelBoundsSmokePassed = qfalse;
	VectorClear(angles);
	VectorClear(mins);
	VectorClear(maxs);
	VectorClear(origin);

	if (q3aBspModelCount > 0) {
		AAS_BSPModelMinsMaxsOrigin(smokeModel, angles, mins, maxs, origin);
		if (Q3A_BotLibImport_ModelBoundsNonZero(mins, maxs)) {
			q3aSmokeStatus.bspModelBoundsSmokePassed = qtrue;
			Q3A_BotLibImport_SetBspModelMessage(
				"Q3A BSP model lump load passed: %s models=%d smoke_model=%d mins=(%.1f %.1f %.1f) maxs=(%.1f %.1f %.1f)",
				loadName,
				q3aBspModelCount,
				smokeModel,
				mins[0],
				mins[1],
				mins[2],
				maxs[0],
				maxs[1],
				maxs[2]);
			return;
		}
	}

	Q3A_BotLibImport_SetBspModelMessage(
		"Q3A BSP model lump loaded without non-zero bounds: %s models=%d",
		loadName,
		q3aBspModelCount);
}

int Q3A_BotLibImport_LoadBspModelData(const char *name, const void *data, int length) {
	const unsigned char *bytes = (const unsigned char *)data;
	int count;
	int i;

	if (!q3aSmokeStatus.initialized) {
		Q3A_BotLibImport_Init();
	}

	Q3A_BotLibImport_ClearBspModelData();
	q3aSmokeStatus.bspModelLoadAttempted = qtrue;
	q3aSmokeStatus.bspModelMessage = q3aBspModelMessage;

	if (data == NULL || length <= 0) {
		Q3A_BotLibImport_SetBspModelMessage("Q3A BSP model lump load failed: empty buffer");
		return qfalse;
	}

	if ((length % Q3A_Q2_BSP_MODEL_SIZE) != 0) {
		Q3A_BotLibImport_SetBspModelMessage("Q3A BSP model lump load failed: invalid model record size");
		return qfalse;
	}

	count = length / Q3A_Q2_BSP_MODEL_SIZE;
	if (count <= 0 || count > Q3A_MAX_BSP_MODELS) {
		Q3A_BotLibImport_SetBspModelMessage("Q3A BSP model lump load failed: invalid model count");
		return qfalse;
	}

	q3aBspModels = (Q3ABspModel *)malloc((size_t)count * sizeof(*q3aBspModels));
	if (q3aBspModels == NULL) {
		Q3A_BotLibImport_SetBspModelMessage("Q3A BSP model lump load failed: allocation failed");
		return qfalse;
	}

	for (i = 0; i < count; ++i) {
		const unsigned char *record = bytes + i * Q3A_Q2_BSP_MODEL_SIZE;
		int axis;

		for (axis = 0; axis < 3; ++axis) {
			q3aBspModels[i].mins[axis] = Q3A_BotLibImport_ReadLittleFloat(record + axis * 4);
			q3aBspModels[i].maxs[axis] = Q3A_BotLibImport_ReadLittleFloat(record + 12 + axis * 4);
			q3aBspModels[i].origin[axis] = Q3A_BotLibImport_ReadLittleFloat(record + 24 + axis * 4);
		}
		q3aBspModels[i].headnode = Q3A_BotLibImport_ReadLittleInt32(record + 36);
		q3aBspModels[i].firstface = Q3A_BotLibImport_ReadLittleInt32(record + 40);
		q3aBspModels[i].numfaces = Q3A_BotLibImport_ReadLittleInt32(record + 44);
	}

	q3aBspModelCount = count;
	q3aSmokeStatus.bspModelLoaded = qtrue;
	q3aSmokeStatus.bspModelCount = q3aBspModelCount;
	Q3A_BotLibImport_RunBspModelSmoke(name);
	return qtrue;
}

static int Q3A_BotLibImport_ValidateBspCollisionLump(
	const Q3ABspLump *lump,
	int fileLength,
	int recordSize,
	const char *label,
	const char **errorMessage) {
	long long lumpEnd;

	if (lump->offset < 0 || lump->length < 0) {
		*errorMessage = label;
		return qfalse;
	}

	lumpEnd = (long long)lump->offset + (long long)lump->length;
	if (lumpEnd < lump->offset || lumpEnd > fileLength) {
		*errorMessage = label;
		return qfalse;
	}

	if (lump->length <= 0 || recordSize <= 0 || (lump->length % recordSize) != 0) {
		*errorMessage = label;
		return qfalse;
	}

	return qtrue;
}

static int Q3A_BotLibImport_ReadBspCollisionHeader(
	const unsigned char *bytes,
	int length,
	Q3ABspLump lumps[Q3A_Q2_BSP_LUMP_COUNT],
	const char **errorMessage) {
	int i;

	if (bytes == NULL || length < Q3A_Q2_BSP_HEADER_SIZE) {
		*errorMessage = "file is smaller than a Q2 BSP header";
		return qfalse;
	}

	if (Q3A_BotLibImport_ReadLittleInt32(bytes) != Q3A_Q2_BSP_ID) {
		*errorMessage = "BSP ident is not IBSP";
		return qfalse;
	}

	if (Q3A_BotLibImport_ReadLittleInt32(bytes + 4) != Q3A_Q2_BSP_VERSION) {
		*errorMessage = "BSP version is not Q2 IBSP38";
		return qfalse;
	}

	for (i = 0; i < Q3A_Q2_BSP_LUMP_COUNT; ++i) {
		const unsigned char *record = bytes + 8 + i * 8;
		lumps[i].offset = Q3A_BotLibImport_ReadLittleInt32(record);
		lumps[i].length = Q3A_BotLibImport_ReadLittleInt32(record + 4);
		if (lumps[i].offset < 0 || lumps[i].length < 0) {
			*errorMessage = "BSP lump has a negative offset or length";
			return qfalse;
		}
		if ((long long)lumps[i].offset + (long long)lumps[i].length > length) {
			*errorMessage = "BSP lump extends outside the file";
			return qfalse;
		}
	}

	return qtrue;
}

static int Q3A_BotLibImport_FailBspCollisionLoad(const char *errorMessage) {
	Q3A_BotLibImport_FreeBspCollisionData();
	q3aSmokeStatus.bspCollisionLoaded = qfalse;
	q3aSmokeStatus.bspCollisionPlanes = 0;
	q3aSmokeStatus.bspCollisionNodes = 0;
	q3aSmokeStatus.bspCollisionLeafs = 0;
	q3aSmokeStatus.bspCollisionBrushes = 0;
	q3aSmokeStatus.bspCollisionPointContentsSmokePassed = qfalse;
	q3aSmokeStatus.bspCollisionTraceSmokePassed = qfalse;
	Q3A_BotLibImport_SetBspCollisionMessage(
		"Q3A BSP collision load failed: %s",
		errorMessage != NULL ? errorMessage : "unknown error");
	return qfalse;
}

static void Q3A_BotLibImport_RunBspCollisionSmoke(const char *name) {
	const char *loadName = (name != NULL && name[0] != '\0') ? name : "<memory>";
	vec3_t mins;
	vec3_t maxs;
	vec3_t start;
	vec3_t end;
	vec3_t zero;
	bsp_trace_t trace;
	int centerContents;

	q3aSmokeStatus.bspCollisionPointContentsSmokePassed = qfalse;
	q3aSmokeStatus.bspCollisionTraceSmokePassed = qfalse;
	VectorClear(mins);
	VectorClear(maxs);
	VectorClear(start);
	VectorClear(end);
	VectorClear(zero);

	if (q3aBspModelCount > 0 && q3aBspModels != NULL) {
		VectorCopy(q3aBspModels[0].mins, mins);
		VectorCopy(q3aBspModels[0].maxs, maxs);
	}

	start[0] = mins[0] + 1.0f;
	start[1] = mins[1] + 1.0f;
	start[2] = mins[2] + 1.0f;
	end[0] = maxs[0] - 1.0f;
	end[1] = maxs[1] - 1.0f;
	end[2] = maxs[2] - 1.0f;
	centerContents = AAS_PointContents(start);
	trace = AAS_Trace(start, zero, zero, end, -1, Q3A_Q2_CONTENTS_SOLID);

	if (q3aBspLeafCount > 0) {
		q3aSmokeStatus.bspCollisionPointContentsSmokePassed = qtrue;
	}
	if (trace.startsolid || trace.allsolid || trace.fraction < 1.0f) {
		q3aSmokeStatus.bspCollisionTraceSmokePassed = qtrue;
	}

	Q3A_BotLibImport_SetBspCollisionMessage(
		"Q3A BSP collision load passed: %s planes=%d nodes=%d leafs=%d brushes=%d point_contents=%d trace_fraction=%.3f startsolid=%d allsolid=%d",
		loadName,
		q3aBspPlaneCount,
		q3aBspNodeCount,
		q3aBspLeafCount,
		q3aBspBrushCount,
		centerContents,
		trace.fraction,
		trace.startsolid,
		trace.allsolid);
}

int Q3A_BotLibImport_LoadBspCollisionData(const char *name, const void *data, int length) {
	const unsigned char *bytes = (const unsigned char *)data;
	Q3ABspLump lumps[Q3A_Q2_BSP_LUMP_COUNT];
	const char *errorMessage = "unknown error";
	int i;

	if (!q3aSmokeStatus.initialized) {
		Q3A_BotLibImport_Init();
	}

	Q3A_BotLibImport_ClearBspCollisionData();
	q3aSmokeStatus.bspCollisionLoadAttempted = qtrue;
	q3aSmokeStatus.bspCollisionMessage = q3aBspCollisionMessage;

	if (!Q3A_BotLibImport_ReadBspCollisionHeader(bytes, length, lumps, &errorMessage)) {
		return Q3A_BotLibImport_FailBspCollisionLoad(errorMessage);
	}

	if (!Q3A_BotLibImport_ValidateBspCollisionLump(
			&lumps[Q3A_Q2_BSP_LUMP_PLANES],
			length,
			Q3A_Q2_BSP_PLANE_SIZE,
			"plane lump is empty or invalid",
			&errorMessage) ||
		!Q3A_BotLibImport_ValidateBspCollisionLump(
			&lumps[Q3A_Q2_BSP_LUMP_NODES],
			length,
			Q3A_Q2_BSP_NODE_SIZE,
			"node lump is empty or invalid",
			&errorMessage) ||
		!Q3A_BotLibImport_ValidateBspCollisionLump(
			&lumps[Q3A_Q2_BSP_LUMP_LEAFS],
			length,
			Q3A_Q2_BSP_LEAF_SIZE,
			"leaf lump is empty or invalid",
			&errorMessage) ||
		!Q3A_BotLibImport_ValidateBspCollisionLump(
			&lumps[Q3A_Q2_BSP_LUMP_LEAFBRUSHES],
			length,
			Q3A_Q2_BSP_LEAFBRUSH_SIZE,
			"leafbrush lump is empty or invalid",
			&errorMessage) ||
		!Q3A_BotLibImport_ValidateBspCollisionLump(
			&lumps[Q3A_Q2_BSP_LUMP_BRUSHES],
			length,
			Q3A_Q2_BSP_BRUSH_SIZE,
			"brush lump is empty or invalid",
			&errorMessage) ||
		!Q3A_BotLibImport_ValidateBspCollisionLump(
			&lumps[Q3A_Q2_BSP_LUMP_BRUSHSIDES],
			length,
			Q3A_Q2_BSP_BRUSHSIDE_SIZE,
			"brushside lump is empty or invalid",
			&errorMessage)) {
		return Q3A_BotLibImport_FailBspCollisionLoad(errorMessage);
	}

	q3aBspPlaneCount = lumps[Q3A_Q2_BSP_LUMP_PLANES].length / Q3A_Q2_BSP_PLANE_SIZE;
	q3aBspNodeCount = lumps[Q3A_Q2_BSP_LUMP_NODES].length / Q3A_Q2_BSP_NODE_SIZE;
	q3aBspLeafCount = lumps[Q3A_Q2_BSP_LUMP_LEAFS].length / Q3A_Q2_BSP_LEAF_SIZE;
	q3aBspLeafBrushCount = lumps[Q3A_Q2_BSP_LUMP_LEAFBRUSHES].length / Q3A_Q2_BSP_LEAFBRUSH_SIZE;
	q3aBspBrushCount = lumps[Q3A_Q2_BSP_LUMP_BRUSHES].length / Q3A_Q2_BSP_BRUSH_SIZE;
	q3aBspBrushSideCount = lumps[Q3A_Q2_BSP_LUMP_BRUSHSIDES].length / Q3A_Q2_BSP_BRUSHSIDE_SIZE;

	q3aBspPlanes = (Q3ABspPlane *)malloc((size_t)q3aBspPlaneCount * sizeof(*q3aBspPlanes));
	q3aBspNodes = (Q3ABspNode *)malloc((size_t)q3aBspNodeCount * sizeof(*q3aBspNodes));
	q3aBspLeafs = (Q3ABspLeaf *)malloc((size_t)q3aBspLeafCount * sizeof(*q3aBspLeafs));
	q3aBspLeafBrushes =
		(unsigned short *)malloc((size_t)q3aBspLeafBrushCount * sizeof(*q3aBspLeafBrushes));
	q3aBspBrushes = (Q3ABspBrush *)malloc((size_t)q3aBspBrushCount * sizeof(*q3aBspBrushes));
	q3aBspBrushSides =
		(Q3ABspBrushSide *)malloc((size_t)q3aBspBrushSideCount * sizeof(*q3aBspBrushSides));
	if (q3aBspPlanes == NULL ||
		q3aBspNodes == NULL ||
		q3aBspLeafs == NULL ||
		q3aBspLeafBrushes == NULL ||
		q3aBspBrushes == NULL ||
		q3aBspBrushSides == NULL) {
		return Q3A_BotLibImport_FailBspCollisionLoad("allocation failed");
	}

	for (i = 0; i < q3aBspPlaneCount; ++i) {
		const unsigned char *record =
			bytes + lumps[Q3A_Q2_BSP_LUMP_PLANES].offset + i * Q3A_Q2_BSP_PLANE_SIZE;
		q3aBspPlanes[i].normal[0] = Q3A_BotLibImport_ReadLittleFloat(record);
		q3aBspPlanes[i].normal[1] = Q3A_BotLibImport_ReadLittleFloat(record + 4);
		q3aBspPlanes[i].normal[2] = Q3A_BotLibImport_ReadLittleFloat(record + 8);
		q3aBspPlanes[i].dist = Q3A_BotLibImport_ReadLittleFloat(record + 12);
		q3aBspPlanes[i].type = Q3A_BotLibImport_ReadLittleInt32(record + 16);
	}

	for (i = 0; i < q3aBspNodeCount; ++i) {
		const unsigned char *record =
			bytes + lumps[Q3A_Q2_BSP_LUMP_NODES].offset + i * Q3A_Q2_BSP_NODE_SIZE;
		int axis;

		q3aBspNodes[i].planenum = Q3A_BotLibImport_ReadLittleInt32(record);
		q3aBspNodes[i].children[0] = Q3A_BotLibImport_ReadLittleInt32(record + 4);
		q3aBspNodes[i].children[1] = Q3A_BotLibImport_ReadLittleInt32(record + 8);
		for (axis = 0; axis < 3; ++axis) {
			q3aBspNodes[i].mins[axis] = Q3A_BotLibImport_ReadLittleInt16(record + 12 + axis * 2);
			q3aBspNodes[i].maxs[axis] = Q3A_BotLibImport_ReadLittleInt16(record + 18 + axis * 2);
		}
	}

	for (i = 0; i < q3aBspLeafCount; ++i) {
		const unsigned char *record =
			bytes + lumps[Q3A_Q2_BSP_LUMP_LEAFS].offset + i * Q3A_Q2_BSP_LEAF_SIZE;
		int axis;

		q3aBspLeafs[i].contents = Q3A_BotLibImport_ReadLittleInt32(record);
		q3aBspLeafs[i].cluster = Q3A_BotLibImport_ReadLittleInt16(record + 4);
		q3aBspLeafs[i].area = Q3A_BotLibImport_ReadLittleInt16(record + 6);
		for (axis = 0; axis < 3; ++axis) {
			q3aBspLeafs[i].mins[axis] = Q3A_BotLibImport_ReadLittleInt16(record + 8 + axis * 2);
			q3aBspLeafs[i].maxs[axis] = Q3A_BotLibImport_ReadLittleInt16(record + 14 + axis * 2);
		}
		q3aBspLeafs[i].firstleafbrush = Q3A_BotLibImport_ReadLittleUInt16(record + 24);
		q3aBspLeafs[i].numleafbrushes = Q3A_BotLibImport_ReadLittleUInt16(record + 26);
	}

	for (i = 0; i < q3aBspLeafBrushCount; ++i) {
		const unsigned char *record =
			bytes + lumps[Q3A_Q2_BSP_LUMP_LEAFBRUSHES].offset + i * Q3A_Q2_BSP_LEAFBRUSH_SIZE;
		q3aBspLeafBrushes[i] = Q3A_BotLibImport_ReadLittleUInt16(record);
	}

	for (i = 0; i < q3aBspBrushCount; ++i) {
		const unsigned char *record =
			bytes + lumps[Q3A_Q2_BSP_LUMP_BRUSHES].offset + i * Q3A_Q2_BSP_BRUSH_SIZE;
		q3aBspBrushes[i].firstside = Q3A_BotLibImport_ReadLittleInt32(record);
		q3aBspBrushes[i].numsides = Q3A_BotLibImport_ReadLittleInt32(record + 4);
		q3aBspBrushes[i].contents = Q3A_BotLibImport_ReadLittleInt32(record + 8);
	}

	for (i = 0; i < q3aBspBrushSideCount; ++i) {
		const unsigned char *record =
			bytes + lumps[Q3A_Q2_BSP_LUMP_BRUSHSIDES].offset + i * Q3A_Q2_BSP_BRUSHSIDE_SIZE;
		q3aBspBrushSides[i].planenum = Q3A_BotLibImport_ReadLittleUInt16(record);
		q3aBspBrushSides[i].texinfo = Q3A_BotLibImport_ReadLittleInt16(record + 2);
	}

	q3aSmokeStatus.bspCollisionLoaded = qtrue;
	q3aSmokeStatus.bspCollisionPlanes = q3aBspPlaneCount;
	q3aSmokeStatus.bspCollisionNodes = q3aBspNodeCount;
	q3aSmokeStatus.bspCollisionLeafs = q3aBspLeafCount;
	q3aSmokeStatus.bspCollisionBrushes = q3aBspBrushCount;
	Q3A_BotLibImport_RunBspCollisionSmoke(name);
	return qtrue;
}

static int Q3A_BotLibImport_FailBspVisibilityLoad(const char *errorMessage) {
	Q3A_BotLibImport_FreeBspVisibilityData();
	q3aSmokeStatus.bspVisibilityLoaded = qfalse;
	q3aSmokeStatus.bspVisibilityClusters = 0;
	q3aSmokeStatus.bspVisibilityPvsSmokePassed = qfalse;
	q3aSmokeStatus.bspVisibilityPhsSmokePassed = qfalse;
	Q3A_BotLibImport_SetBspVisibilityMessage(
		"Q3A BSP visibility load failed: %s",
		errorMessage != NULL ? errorMessage : "unknown error");
	return qfalse;
}

static int Q3A_BotLibImport_FindVisibilitySmokePoint(vec3_t point, int *cluster) {
	int i;

	if (cluster != NULL) {
		*cluster = -1;
	}
	if (point != NULL) {
		VectorClear(point);
	}

	for (i = 0; i < q3aBspLeafCount; ++i) {
		vec3_t candidate;
		int leafnum;
		int axis;

		if (q3aBspLeafs[i].cluster < 0) {
			continue;
		}

		for (axis = 0; axis < 3; ++axis) {
			candidate[axis] = ((float)q3aBspLeafs[i].mins[axis] + (float)q3aBspLeafs[i].maxs[axis]) * 0.5f;
		}

		leafnum = Q3A_BotLibImport_PointLeafNum(candidate);
		if (leafnum < 0 || leafnum >= q3aBspLeafCount || q3aBspLeafs[leafnum].cluster < 0) {
			continue;
		}

		if (point != NULL) {
			VectorCopy(candidate, point);
		}
		if (cluster != NULL) {
			*cluster = q3aBspLeafs[leafnum].cluster;
		}
		return qtrue;
	}

	return qfalse;
}

static void Q3A_BotLibImport_RunBspVisibilitySmoke(const char *name) {
	const char *loadName = (name != NULL && name[0] != '\0') ? name : "<memory>";
	vec3_t point;
	int cluster = -1;
	int pvsCount = -1;
	int phsCount = -1;

	q3aSmokeStatus.bspVisibilityPvsSmokePassed = qfalse;
	q3aSmokeStatus.bspVisibilityPhsSmokePassed = qfalse;
	VectorClear(point);

	if (Q3A_BotLibImport_FindVisibilitySmokePoint(point, &cluster)) {
		pvsCount = Q3A_BotLibImport_CountVisibleClusters(cluster, Q3A_Q2_DVIS_PVS);
		phsCount = Q3A_BotLibImport_CountVisibleClusters(cluster, Q3A_Q2_DVIS_PHS);
		if (pvsCount > 0 && AAS_inPVS(point, point)) {
			q3aSmokeStatus.bspVisibilityPvsSmokePassed = qtrue;
		}
		if (phsCount > 0 && AAS_inPHS(point, point)) {
			q3aSmokeStatus.bspVisibilityPhsSmokePassed = qtrue;
		}
	}

	Q3A_BotLibImport_SetBspVisibilityMessage(
		"Q3A BSP visibility load passed: %s clusters=%d smoke_cluster=%d pvs_visible=%d phs_visible=%d",
		loadName,
		q3aBspVisClusterCount,
		cluster,
		pvsCount,
		phsCount);
}

int Q3A_BotLibImport_LoadBspVisibilityData(const char *name, const void *data, int length) {
	const unsigned char *bytes = (const unsigned char *)data;
	Q3ABspLump lumps[Q3A_Q2_BSP_LUMP_COUNT];
	const Q3ABspLump *visLump;
	const unsigned char *visBytes;
	const char *errorMessage = "unknown error";
	int headerLength;
	int i;

	if (!q3aSmokeStatus.initialized) {
		Q3A_BotLibImport_Init();
	}

	Q3A_BotLibImport_ClearBspVisibilityData();
	q3aSmokeStatus.bspVisibilityLoadAttempted = qtrue;
	q3aSmokeStatus.bspVisibilityMessage = q3aBspVisibilityMessage;

	if (!q3aSmokeStatus.bspCollisionLoaded || q3aBspLeafs == NULL || q3aBspLeafCount <= 0) {
		return Q3A_BotLibImport_FailBspVisibilityLoad("Q2 BSP leaf data is not loaded");
	}

	if (!Q3A_BotLibImport_ReadBspCollisionHeader(bytes, length, lumps, &errorMessage)) {
		return Q3A_BotLibImport_FailBspVisibilityLoad(errorMessage);
	}

	visLump = &lumps[Q3A_Q2_BSP_LUMP_VISIBILITY];
	if (visLump->offset < 0 || visLump->length < 4 || visLump->offset + visLump->length > length) {
		return Q3A_BotLibImport_FailBspVisibilityLoad("visibility lump is empty or invalid");
	}

	visBytes = bytes + visLump->offset;
	q3aBspVisClusterCount = Q3A_BotLibImport_ReadLittleInt32(visBytes);
	if (q3aBspVisClusterCount <= 0 || q3aBspVisClusterCount > (visLump->length - 4) / 8) {
		return Q3A_BotLibImport_FailBspVisibilityLoad("visibility cluster count is invalid");
	}

	headerLength = 4 + q3aBspVisClusterCount * 8;
	if (headerLength > visLump->length) {
		return Q3A_BotLibImport_FailBspVisibilityLoad("visibility header extends outside the lump");
	}

	q3aBspVisData = (unsigned char *)malloc((size_t)visLump->length);
	q3aBspVisOffsets = (int *)malloc((size_t)q3aBspVisClusterCount * 2 * sizeof(*q3aBspVisOffsets));
	if (q3aBspVisData == NULL || q3aBspVisOffsets == NULL) {
		return Q3A_BotLibImport_FailBspVisibilityLoad("allocation failed");
	}

	memcpy(q3aBspVisData, visBytes, (size_t)visLump->length);
	q3aBspVisLength = visLump->length;

	for (i = 0; i < q3aBspVisClusterCount; ++i) {
		int mode;
		for (mode = 0; mode < 2; ++mode) {
			const int offset = Q3A_BotLibImport_ReadLittleInt32(visBytes + 4 + i * 8 + mode * 4);
			if (offset < headerLength || offset >= visLump->length) {
				return Q3A_BotLibImport_FailBspVisibilityLoad("visibility row offset is invalid");
			}
			q3aBspVisOffsets[i * 2 + mode] = offset;
		}
	}

	q3aSmokeStatus.bspVisibilityLoaded = qtrue;
	q3aSmokeStatus.bspVisibilityClusters = q3aBspVisClusterCount;
	Q3A_BotLibImport_RunBspVisibilitySmoke(name);
	return qtrue;
}

int AAS_NextBSPEntity(int ent) {
	++ent;
	if (ent >= 1 && ent < q3aBspEntityCount) {
		return ent;
	}
	return 0;
}

int AAS_ValueForBSPEpairKey(int ent, char *key, char *value, int size) {
	return Q3A_BotLibImport_ValueForBspEpairKeyInternal(ent, key, value, size, qfalse);
}

int AAS_VectorForBSPEpairKey(int ent, char *key, vec3_t v) {
	char value[128];
	double x;
	double y;
	double z;

	if (v == NULL) {
		return qfalse;
	}

	VectorClear(v);
	if (!AAS_ValueForBSPEpairKey(ent, key, value, sizeof(value))) {
		return qfalse;
	}
	if (sscanf(value, "%lf %lf %lf", &x, &y, &z) != 3) {
		return qfalse;
	}

	VectorSet(v, (vec_t)x, (vec_t)y, (vec_t)z);
	return qtrue;
}

int AAS_FloatForBSPEpairKey(int ent, char *key, float *value) {
	char stringValue[128];

	if (value != NULL) {
		*value = 0.0f;
	}
	if (value == NULL || !AAS_ValueForBSPEpairKey(ent, key, stringValue, sizeof(stringValue))) {
		return qfalse;
	}
	*value = (float)atof(stringValue);
	return qtrue;
}

int AAS_IntForBSPEpairKey(int ent, char *key, int *value) {
	char stringValue[128];

	if (value != NULL) {
		*value = 0;
	}
	if (value == NULL || !AAS_ValueForBSPEpairKey(ent, key, stringValue, sizeof(stringValue))) {
		return qfalse;
	}
	*value = atoi(stringValue);
	return qtrue;
}

static float Q3A_BotLibImport_RadiusFromBounds(const vec3_t mins, const vec3_t maxs) {
	float radiusSquared = 0.0f;
	int i;

	for (i = 0; i < 3; ++i) {
		const float corner = fabsf(mins[i]) > fabsf(maxs[i]) ? fabsf(mins[i]) : fabsf(maxs[i]);
		radiusSquared += corner * corner;
	}

	return Q3A_BotLibSqrt(radiusSquared);
}

void AAS_BSPModelMinsMaxsOrigin(int modelnum, vec3_t angles, vec3_t mins, vec3_t maxs, vec3_t origin) {
	vec3_t modelMins;
	vec3_t modelMaxs;

	VectorClear(modelMins);
	VectorClear(modelMaxs);

	if (modelnum >= 0 && modelnum < q3aBspModelCount && q3aBspModels != NULL) {
		VectorCopy(q3aBspModels[modelnum].mins, modelMins);
		VectorCopy(q3aBspModels[modelnum].maxs, modelMaxs);
	}

	if (angles != NULL && (angles[0] != 0.0f || angles[1] != 0.0f || angles[2] != 0.0f)) {
		const float radius = Q3A_BotLibImport_RadiusFromBounds(modelMins, modelMaxs);
		int i;

		for (i = 0; i < 3; ++i) {
			const float center = (modelMins[i] + modelMaxs[i]) * 0.5f;
			modelMins[i] = center - radius;
			modelMaxs[i] = center + radius;
		}
	}

	if (mins != NULL) {
		VectorCopy(modelMins, mins);
	}
	if (maxs != NULL) {
		VectorCopy(modelMaxs, maxs);
	}
	if (origin != NULL) {
		VectorClear(origin);
	}
}

static int Q3A_BotLibImport_DebugDraw(
	int primitive,
	const vec3_t start,
	const vec3_t end,
	float size,
	int color,
	int secondaryColor) {
	q3aSmokeStatus.debugDrawAttempted = qtrue;
	q3aSmokeStatus.debugDrawCallbackSet = q3aDebugDrawCallback != NULL;

	switch (primitive) {
	case Q3A_BOTLIB_DEBUG_DRAW_LINE:
	case Q3A_BOTLIB_DEBUG_DRAW_PERMANENT_LINE:
		q3aSmokeStatus.debugDrawLines++;
		break;
	case Q3A_BOTLIB_DEBUG_DRAW_CROSS:
		q3aSmokeStatus.debugDrawCrosses++;
		break;
	case Q3A_BOTLIB_DEBUG_DRAW_ARROW:
		q3aSmokeStatus.debugDrawArrows++;
		break;
	case Q3A_BOTLIB_DEBUG_DRAW_CLEAR:
		q3aSmokeStatus.debugDrawClears++;
		break;
	default:
		q3aSmokeStatus.debugDrawFailures++;
		Q3A_BotLibImport_SetDebugDrawMessage("Q3A debug draw bridge failed: unknown primitive");
		return qfalse;
	}

	if (q3aDebugDrawCallback == NULL) {
		q3aSmokeStatus.debugDrawFailures++;
		Q3A_BotLibImport_SetDebugDrawMessage("Q3A debug draw bridge skipped");
		return qfalse;
	}

	if (!q3aDebugDrawCallback(primitive, start, end, size, color, secondaryColor)) {
		q3aSmokeStatus.debugDrawFailures++;
		Q3A_BotLibImport_SetDebugDrawMessage("Q3A debug draw bridge callback rejected primitive");
		return qfalse;
	}

	Q3A_BotLibImport_SetDebugDrawMessage("Q3A debug draw bridge drew primitive");
	return qtrue;
}

static int Q3A_BotLibImport_DebugPolygonCreate(int color, int numPoints, vec3_t *points) {
	int id;

	q3aSmokeStatus.debugPolygonAttempted = qtrue;
	q3aSmokeStatus.debugPolygonCallbackSet = q3aDebugPolygonCallback != NULL;

	if (q3aDebugPolygonCallback == NULL) {
		q3aSmokeStatus.debugPolygonFailures++;
		Q3A_BotLibImport_SetDebugPolygonMessage("Q3A debug polygon bridge skipped");
		return 0;
	}

	if (points == NULL || numPoints < 3) {
		q3aSmokeStatus.debugPolygonFailures++;
		Q3A_BotLibImport_SetDebugPolygonMessage("Q3A debug polygon bridge failed: invalid polygon");
		return 0;
	}

	id = q3aDebugPolygonNextId++;
	if (q3aDebugPolygonNextId <= 0) {
		q3aDebugPolygonNextId = 1;
	}

	q3aSmokeStatus.debugPolygonCreates++;
	q3aSmokeStatus.debugPolygonPoints += numPoints;
	q3aSmokeStatus.debugPolygonLastId = id;

	if (!q3aDebugPolygonCallback(color, numPoints, (const float *)points)) {
		q3aSmokeStatus.debugPolygonFailures++;
		Q3A_BotLibImport_SetDebugPolygonMessage("Q3A debug polygon bridge callback rejected polygon");
		return 0;
	}

	Q3A_BotLibImport_SetDebugPolygonMessage("Q3A debug polygon bridge created polygon");
	return id;
}

static void Q3A_BotLibImport_DebugPolygonDelete(int id) {
	q3aSmokeStatus.debugPolygonAttempted = qtrue;
	q3aSmokeStatus.debugPolygonCallbackSet = q3aDebugPolygonCallback != NULL;

	if (id <= 0) {
		q3aSmokeStatus.debugPolygonFailures++;
		Q3A_BotLibImport_SetDebugPolygonMessage("Q3A debug polygon bridge delete failed: invalid id");
		return;
	}

	q3aSmokeStatus.debugPolygonDeletes++;
	Q3A_BotLibImport_SetDebugPolygonMessage("Q3A debug polygon bridge deleted polygon");
}

static int Q3A_BotLibImport_DebugLineCreate(void) {
	const int id = q3aDebugLineNextId++;

	if (q3aDebugLineNextId <= 0) {
		q3aDebugLineNextId = 1;
	}
	return id;
}

static void Q3A_BotLibImport_DebugLineDelete(int line) {
	if (line <= 0) {
		q3aSmokeStatus.debugDrawFailures++;
		Q3A_BotLibImport_SetDebugDrawMessage("Q3A debug draw bridge delete failed: invalid line");
	}
}

static void Q3A_BotLibImport_DebugLineShow(int line, vec3_t start, vec3_t end, int color) {
	if (line <= 0 || start == NULL || end == NULL) {
		q3aSmokeStatus.debugDrawFailures++;
		Q3A_BotLibImport_SetDebugDrawMessage("Q3A debug draw bridge failed: invalid debug line");
		return;
	}

	Q3A_BotLibImport_DebugDraw(Q3A_BOTLIB_DEBUG_DRAW_LINE, start, end, 0.0f, color, LINECOLOR_NONE);
}

int Sys_MilliSeconds(void) {
	return q3aSmokeStatus.runtimeMilliseconds;
}

void QDECL Log_Write(char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(q3aPrintMessage, sizeof(q3aPrintMessage), fmt, args);
	va_end(args);
}

void QDECL Log_WriteTimeStamped(char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(q3aPrintMessage, sizeof(q3aPrintMessage), fmt, args);
	va_end(args);
}

static void Q3A_BotLibImport_ResetAASSampleStatus(const char *message) {
	q3aSmokeStatus.aasSampleAttempted = qfalse;
	q3aSmokeStatus.aasSamplePassed = qfalse;
	q3aSmokeStatus.aasSampleArea = 0;
	q3aSmokeStatus.aasSamplePointArea = 0;
	q3aSmokeStatus.aasSamplePresenceType = 0;
	q3aSmokeStatus.aasSampleCluster = 0;
	q3aSmokeStatus.aasSampleReachability = 0;
	q3aSmokeStatus.aasSampleMessage = message;
}

static void Q3A_BotLibImport_ResetAASClusterStatus(const char *message) {
	q3aSmokeStatus.aasClusterAttempted = qfalse;
	q3aSmokeStatus.aasClusterPassed = qfalse;
	q3aSmokeStatus.aasClusterArea = 0;
	q3aSmokeStatus.aasClusterCluster = 0;
	q3aSmokeStatus.aasClusterNumClusters = 0;
	q3aSmokeStatus.aasClusterAreas = 0;
	q3aSmokeStatus.aasClusterReachabilityAreas = 0;
	q3aSmokeStatus.aasClusterFailures = 0;
	q3aSmokeStatus.aasClusterMessage = message;
}

static void Q3A_BotLibImport_SetAASClusterMessage(const char *prefix) {
	snprintf(
		q3aAasClusterMessage,
		sizeof(q3aAasClusterMessage),
		"%s: clusters=%d area=%d cluster=%d cluster_areas=%d reachability_areas=%d failures=%d",
		prefix,
		q3aSmokeStatus.aasClusterNumClusters,
		q3aSmokeStatus.aasClusterArea,
		q3aSmokeStatus.aasClusterCluster,
		q3aSmokeStatus.aasClusterAreas,
		q3aSmokeStatus.aasClusterReachabilityAreas,
		q3aSmokeStatus.aasClusterFailures);
	q3aSmokeStatus.aasClusterMessage = q3aAasClusterMessage;
}

static void Q3A_BotLibImport_ResetAASRouteStatus(const char *message) {
	q3aSmokeStatus.aasRouteAttempted = qfalse;
	q3aSmokeStatus.aasRoutePassed = qfalse;
	q3aSmokeStatus.aasRouteStartArea = 0;
	q3aSmokeStatus.aasRouteGoalArea = 0;
	q3aSmokeStatus.aasRouteTravelTime = 0;
	q3aSmokeStatus.aasRouteReachability = 0;
	q3aSmokeStatus.aasRouteEndArea = 0;
	q3aSmokeStatus.aasRouteStopEvent = 0;
	q3aSmokeStatus.aasRouteMessage = message;
}

static void Q3A_BotLibImport_ResetAASAltRouteStatus(const char *message) {
	q3aSmokeStatus.aasAltRouteAttempted = qfalse;
	q3aSmokeStatus.aasAltRoutePassed = qfalse;
	q3aSmokeStatus.aasAltRouteStartArea = 0;
	q3aSmokeStatus.aasAltRouteGoalArea = 0;
	q3aSmokeStatus.aasAltRouteGoals = 0;
	q3aSmokeStatus.aasAltRouteFirstArea = 0;
	q3aSmokeStatus.aasAltRouteFirstStartTravelTime = 0;
	q3aSmokeStatus.aasAltRouteFirstGoalTravelTime = 0;
	q3aSmokeStatus.aasAltRouteFirstExtraTravelTime = 0;
	q3aSmokeStatus.aasAltRouteFailures = 0;
	q3aSmokeStatus.aasAltRouteMessage = message;
}

static void Q3A_BotLibImport_SetAASAltRouteMessage(const char *prefix) {
	snprintf(
		q3aAasAltRouteMessage,
		sizeof(q3aAasAltRouteMessage),
		"%s: start=%d goal=%d goals=%d first_area=%d start_time=%d goal_time=%d extra_time=%d failures=%d",
		prefix,
		q3aSmokeStatus.aasAltRouteStartArea,
		q3aSmokeStatus.aasAltRouteGoalArea,
		q3aSmokeStatus.aasAltRouteGoals,
		q3aSmokeStatus.aasAltRouteFirstArea,
		q3aSmokeStatus.aasAltRouteFirstStartTravelTime,
		q3aSmokeStatus.aasAltRouteFirstGoalTravelTime,
		q3aSmokeStatus.aasAltRouteFirstExtraTravelTime,
		q3aSmokeStatus.aasAltRouteFailures);
	q3aSmokeStatus.aasAltRouteMessage = q3aAasAltRouteMessage;
}

static void Q3A_BotLibImport_ResetAASMovementStatus(const char *message) {
	q3aSmokeStatus.aasMovementAttempted = qfalse;
	q3aSmokeStatus.aasMovementPassed = qfalse;
	q3aSmokeStatus.aasMovementEndArea = 0;
	q3aSmokeStatus.aasMovementStopEvent = 0;
	q3aSmokeStatus.aasMovementFrames = 0;
	q3aSmokeStatus.aasMovementDropToFloorPassed = qfalse;
	q3aSmokeStatus.aasMovementJumpVelocityPassed = qfalse;
	q3aSmokeStatus.aasMovementMessage = message;
}

static void Q3A_BotLibImport_ResetBotClientCommandStatus(const char *message) {
	q3aSmokeStatus.botClientCommandCallbackSet = q3aBotClientCommandCallback != NULL;
	q3aSmokeStatus.botClientCommandAttempted = qfalse;
	q3aSmokeStatus.botClientCommandSmokePassed = qfalse;
	q3aSmokeStatus.botClientCommandClient = -1;
	q3aSmokeStatus.botClientCommandAccepted = 0;
	q3aSmokeStatus.botClientCommandRejected = 0;
	q3aSmokeStatus.botClientCommandFailures = 0;
	q3aSmokeStatus.botClientCommandMessage = message;
}

static void Q3A_BotLibImport_SetBotClientCommandMessage(const char *prefix) {
	snprintf(
		q3aBotClientCommandMessage,
		sizeof(q3aBotClientCommandMessage),
		"%s: callback=%s client=%d accepted=%d rejected=%d failures=%d",
		prefix,
		q3aSmokeStatus.botClientCommandCallbackSet ? "yes" : "no",
		q3aSmokeStatus.botClientCommandClient,
		q3aSmokeStatus.botClientCommandAccepted,
		q3aSmokeStatus.botClientCommandRejected,
		q3aSmokeStatus.botClientCommandFailures);
	q3aSmokeStatus.botClientCommandMessage = q3aBotClientCommandMessage;
}

static void Q3A_BotLibImport_ResetAASStartFrameStatus(const char *message) {
	q3aSmokeStatus.aasStartFrameAttempted = qfalse;
	q3aSmokeStatus.aasStartFramePassed = qfalse;
	q3aSmokeStatus.aasStartFrameResult = 0;
	q3aSmokeStatus.aasStartFrameCount = 0;
	q3aSmokeStatus.aasStartFrameTimeMilliseconds = 0;
	q3aSmokeStatus.aasStartFrameMessage = message;
}

static void Q3A_BotLibImport_ResetEntitySyncStatus(const char *message) {
	q3aSmokeStatus.entitySyncAttempted = qfalse;
	q3aSmokeStatus.entitySyncPassed = qfalse;
	q3aSmokeStatus.entitySyncUpdated = 0;
	q3aSmokeStatus.entitySyncUnlinked = 0;
	q3aSmokeStatus.entitySyncSkipped = 0;
	q3aSmokeStatus.entitySyncFailures = 0;
	q3aSmokeStatus.entitySyncMaxEntities = 0;
	q3aSmokeStatus.entitySyncMessage = message;
}

static void Q3A_BotLibImport_ResetEntityTraceStatus(const char *message) {
	q3aSmokeStatus.entityTraceCallbackSet = q3aEntityTraceCallback != NULL;
	q3aSmokeStatus.entityTraceAttempted = 0;
	q3aSmokeStatus.entityTraceHits = 0;
	q3aSmokeStatus.entityTraceMisses = 0;
	q3aSmokeStatus.entityTraceFailures = 0;
	q3aSmokeStatus.entityTraceMessage = message;
}

static void Q3A_BotLibImport_ResetDebugDrawStatus(const char *message) {
	q3aSmokeStatus.debugDrawCallbackSet = q3aDebugDrawCallback != NULL;
	q3aSmokeStatus.debugDrawAttempted = qfalse;
	q3aSmokeStatus.debugDrawPassed = qfalse;
	q3aSmokeStatus.debugDrawLines = 0;
	q3aSmokeStatus.debugDrawCrosses = 0;
	q3aSmokeStatus.debugDrawArrows = 0;
	q3aSmokeStatus.debugDrawClears = 0;
	q3aSmokeStatus.debugDrawFailures = 0;
	q3aSmokeStatus.debugDrawMessage = message;
}

static void Q3A_BotLibImport_ResetDebugPolygonStatus(const char *message) {
	q3aSmokeStatus.debugPolygonCallbackSet = q3aDebugPolygonCallback != NULL;
	q3aSmokeStatus.debugPolygonAttempted = qfalse;
	q3aSmokeStatus.debugPolygonPassed = qfalse;
	q3aSmokeStatus.debugPolygonCreates = 0;
	q3aSmokeStatus.debugPolygonDeletes = 0;
	q3aSmokeStatus.debugPolygonPoints = 0;
	q3aSmokeStatus.debugPolygonLastId = 0;
	q3aSmokeStatus.debugPolygonFailures = 0;
	q3aSmokeStatus.debugPolygonMessage = message;
}

static void Q3A_BotLibImport_ResetDebugAreaStatus(const char *message) {
	q3aSmokeStatus.debugAreaAttempted = qfalse;
	q3aSmokeStatus.debugAreaPassed = qfalse;
	q3aSmokeStatus.debugAreaArea = 0;
	q3aSmokeStatus.debugAreaLines = 0;
	q3aSmokeStatus.debugAreaPolygonCreates = 0;
	q3aSmokeStatus.debugAreaPolygonDeletes = 0;
	q3aSmokeStatus.debugAreaFailures = 0;
	q3aSmokeStatus.debugAreaMessage = message;
}

static void Q3A_BotLibImport_ResetRouteOverlayStatus(const char *message) {
	q3aSmokeStatus.routeOverlayAttempted = qfalse;
	q3aSmokeStatus.routeOverlayPassed = qfalse;
	q3aSmokeStatus.routeOverlayStartArea = 0;
	q3aSmokeStatus.routeOverlayGoalArea = 0;
	q3aSmokeStatus.routeOverlayEndArea = 0;
	q3aSmokeStatus.routeOverlayTravelTime = 0;
	q3aSmokeStatus.routeOverlayReachability = 0;
	q3aSmokeStatus.routeOverlayLines = 0;
	q3aSmokeStatus.routeOverlayCrosses = 0;
	q3aSmokeStatus.routeOverlayArrows = 0;
	q3aSmokeStatus.routeOverlayClears = 0;
	q3aSmokeStatus.routeOverlayFailures = 0;
	q3aSmokeStatus.routeOverlayMessage = message;
}

static void Q3A_BotLibImport_SetDebugDrawMessage(const char *prefix) {
	snprintf(
		q3aDebugDrawMessage,
		sizeof(q3aDebugDrawMessage),
		"%s: callback=%s lines=%d crosses=%d arrows=%d clears=%d failures=%d",
		prefix,
		q3aSmokeStatus.debugDrawCallbackSet ? "yes" : "no",
		q3aSmokeStatus.debugDrawLines,
		q3aSmokeStatus.debugDrawCrosses,
		q3aSmokeStatus.debugDrawArrows,
		q3aSmokeStatus.debugDrawClears,
		q3aSmokeStatus.debugDrawFailures);
	q3aSmokeStatus.debugDrawMessage = q3aDebugDrawMessage;
}

static void Q3A_BotLibImport_SetDebugPolygonMessage(const char *prefix) {
	snprintf(
		q3aDebugPolygonMessage,
		sizeof(q3aDebugPolygonMessage),
		"%s: callback=%s creates=%d deletes=%d points=%d last_id=%d failures=%d",
		prefix,
		q3aSmokeStatus.debugPolygonCallbackSet ? "yes" : "no",
		q3aSmokeStatus.debugPolygonCreates,
		q3aSmokeStatus.debugPolygonDeletes,
		q3aSmokeStatus.debugPolygonPoints,
		q3aSmokeStatus.debugPolygonLastId,
		q3aSmokeStatus.debugPolygonFailures);
	q3aSmokeStatus.debugPolygonMessage = q3aDebugPolygonMessage;
}

static void Q3A_BotLibImport_SetDebugAreaMessage(const char *prefix) {
	snprintf(
		q3aDebugAreaMessage,
		sizeof(q3aDebugAreaMessage),
		"%s: area=%d lines=%d polygon_creates=%d polygon_deletes=%d failures=%d",
		prefix,
		q3aSmokeStatus.debugAreaArea,
		q3aSmokeStatus.debugAreaLines,
		q3aSmokeStatus.debugAreaPolygonCreates,
		q3aSmokeStatus.debugAreaPolygonDeletes,
		q3aSmokeStatus.debugAreaFailures);
	q3aSmokeStatus.debugAreaMessage = q3aDebugAreaMessage;
}

static void Q3A_BotLibImport_SetRouteOverlayMessage(const char *prefix) {
	snprintf(
		q3aRouteOverlayMessage,
		sizeof(q3aRouteOverlayMessage),
		"%s: callback=%s start=%d goal=%d end=%d travel_time=%d reachability=%d lines=%d crosses=%d arrows=%d clears=%d failures=%d",
		prefix,
		q3aDebugDrawCallback != NULL ? "yes" : "no",
		q3aSmokeStatus.routeOverlayStartArea,
		q3aSmokeStatus.routeOverlayGoalArea,
		q3aSmokeStatus.routeOverlayEndArea,
		q3aSmokeStatus.routeOverlayTravelTime,
		q3aSmokeStatus.routeOverlayReachability,
		q3aSmokeStatus.routeOverlayLines,
		q3aSmokeStatus.routeOverlayCrosses,
		q3aSmokeStatus.routeOverlayArrows,
		q3aSmokeStatus.routeOverlayClears,
		q3aSmokeStatus.routeOverlayFailures);
	q3aSmokeStatus.routeOverlayMessage = q3aRouteOverlayMessage;
}

static void Q3A_BotLibImport_SetEntitySyncMessage(const char *prefix) {
	snprintf(
		q3aEntitySyncMessage,
		sizeof(q3aEntitySyncMessage),
		"%s: updated=%d unlinked=%d skipped=%d failures=%d max=%d",
		prefix,
		q3aSmokeStatus.entitySyncUpdated,
		q3aSmokeStatus.entitySyncUnlinked,
		q3aSmokeStatus.entitySyncSkipped,
		q3aSmokeStatus.entitySyncFailures,
		q3aSmokeStatus.entitySyncMaxEntities);
	q3aSmokeStatus.entitySyncMessage = q3aEntitySyncMessage;
}

static void Q3A_BotLibImport_SetEntityTraceMessage(const char *prefix) {
	snprintf(
		q3aEntityTraceMessage,
		sizeof(q3aEntityTraceMessage),
		"%s: callback=%s attempts=%d hits=%d misses=%d failures=%d",
		prefix,
		q3aSmokeStatus.entityTraceCallbackSet ? "yes" : "no",
		q3aSmokeStatus.entityTraceAttempted,
		q3aSmokeStatus.entityTraceHits,
		q3aSmokeStatus.entityTraceMisses,
		q3aSmokeStatus.entityTraceFailures);
	q3aSmokeStatus.entityTraceMessage = q3aEntityTraceMessage;
}

static int Q3A_BotLibImport_RunEntityTraceSmoke(void) {
	int entnum;
	int tested = 0;

	if (!q3aSmokeStatus.aasLoaded || !aasworld.loaded || aasworld.entities == NULL) {
		Q3A_BotLibImport_SetEntityTraceMessage("Q3A AAS entity trace skipped: AAS is not loaded");
		return qfalse;
	}

	if (q3aEntityTraceCallback == NULL) {
		Q3A_BotLibImport_SetEntityTraceMessage("Q3A AAS entity trace skipped: callback is not registered");
		return qfalse;
	}

	for (entnum = 1; entnum < aasworld.maxentities; ++entnum) {
		aas_entity_t *ent = &aasworld.entities[entnum];
		vec3_t absmins;
		vec3_t absmaxs;
		vec3_t center;
		vec3_t mins;
		vec3_t maxs;
		int i;
		int axis;

		if (!ent->i.valid || (ent->i.solid != SOLID_BBOX && ent->i.solid != SOLID_BSP)) {
			continue;
		}

		for (i = 0; i < 3; ++i) {
			absmins[i] = ent->i.origin[i] + ent->i.mins[i];
			absmaxs[i] = ent->i.origin[i] + ent->i.maxs[i];
			center[i] = (absmins[i] + absmaxs[i]) * 0.5f;
		}
		VectorClear(mins);
		VectorClear(maxs);

		for (axis = 0; axis < 3; ++axis) {
			bsp_trace_t trace;
			vec3_t start;
			vec3_t end;

			VectorCopy(center, start);
			VectorCopy(center, end);
			start[axis] = absmins[axis] - 8.0f;
			end[axis] = absmaxs[axis] + 8.0f;
			tested++;
			if (AAS_EntityCollision(entnum, start, mins, maxs, end, CONTENTS_SOLID | CONTENTS_PLAYERCLIP, &trace)) {
				Q3A_BotLibImport_SetEntityTraceMessage("Q3A AAS entity trace smoke passed");
				return qtrue;
			}

			VectorCopy(center, start);
			VectorCopy(center, end);
			start[axis] = absmaxs[axis] + 8.0f;
			end[axis] = absmins[axis] - 8.0f;
			tested++;
			if (AAS_EntityCollision(entnum, start, mins, maxs, end, CONTENTS_SOLID | CONTENTS_PLAYERCLIP, &trace)) {
				Q3A_BotLibImport_SetEntityTraceMessage("Q3A AAS entity trace smoke passed");
				return qtrue;
			}
		}
	}

	Q3A_BotLibImport_SetEntityTraceMessage(
		tested > 0 ? "Q3A AAS entity trace smoke missed" : "Q3A AAS entity trace skipped: no solid entities");
	return qfalse;
}

static int Q3A_BotLibImport_RunBspLeafLinkSmoke(void) {
	int entnum;
	int bestCount = 0;

	q3aSmokeStatus.bspBoxEntitiesSmokePassed = qfalse;
	q3aSmokeStatus.bspBoxEntitiesCount = 0;

	if (!q3aSmokeStatus.bspCollisionLoaded || q3aBspLeafLinkedEntities == NULL) {
		Q3A_BotLibImport_SetBspLeafLinkMessage("Q3A BSP leaf entity link smoke skipped: collision data is not loaded");
		return qfalse;
	}

	if (q3aBspLeafLinkCount <= 0) {
		Q3A_BotLibImport_SetBspLeafLinkMessage("Q3A BSP leaf entity link smoke skipped: no linked entities");
		return qfalse;
	}

	if (aasworld.entities == NULL || aasworld.maxentities <= 0) {
		Q3A_BotLibImport_SetBspLeafLinkMessage("Q3A BSP leaf entity link smoke skipped: AAS entities are not initialized");
		return qfalse;
	}

	for (entnum = 0; entnum < aasworld.maxentities; ++entnum) {
		aas_entity_t *ent = &aasworld.entities[entnum];
		vec3_t absmins;
		vec3_t absmaxs;
		int list[64];
		int count;
		int i;

		if (!ent->i.valid || ent->leaves == NULL) {
			continue;
		}

		VectorAdd(ent->i.mins, ent->i.origin, absmins);
		VectorAdd(ent->i.maxs, ent->i.origin, absmaxs);
		count = AAS_BoxEntities(absmins, absmaxs, list, (int)(sizeof(list) / sizeof(list[0])));
		if (count > bestCount) {
			bestCount = count;
		}

		for (i = 0; i < count; ++i) {
			if (list[i] == entnum) {
				q3aSmokeStatus.bspBoxEntitiesSmokePassed = qtrue;
				q3aSmokeStatus.bspBoxEntitiesCount = count;
				Q3A_BotLibImport_SetBspLeafLinkMessage(
					"Q3A BSP leaf entity link smoke passed: active_links=%d box_entities=%d ent=%d",
					q3aBspLeafLinkCount,
					count,
					entnum);
				return qtrue;
			}
		}
	}

	q3aSmokeStatus.bspBoxEntitiesCount = bestCount;
	Q3A_BotLibImport_SetBspLeafLinkMessage(
		"Q3A BSP leaf entity link smoke missed: active_links=%d best_box_entities=%d",
		q3aBspLeafLinkCount,
		bestCount);
	return qfalse;
}

static int Q3A_BotLibImport_RunAASSampleSmoke(void) {
	int area;
	int fallbackArea = 0;
	int fallbackPointArea = 0;
	int fallbackPresenceType = 0;
	int fallbackCluster = 0;
	int fallbackReachability = 0;

	q3aSmokeStatus.aasSampleAttempted = qtrue;
	q3aSmokeStatus.aasSamplePassed = qfalse;
	q3aSmokeStatus.aasSampleArea = 0;
	q3aSmokeStatus.aasSamplePointArea = 0;
	q3aSmokeStatus.aasSamplePresenceType = 0;
	q3aSmokeStatus.aasSampleCluster = 0;
	q3aSmokeStatus.aasSampleReachability = 0;

	if (!aasworld.loaded) {
		q3aSmokeStatus.aasSampleMessage = "Q3A AAS area sample failed: AAS is not loaded";
		return qfalse;
	}

	for (area = 1; area < aasworld.numareas; ++area) {
		aas_areainfo_t info;
		int pointArea;
		int reachability;

		Com_Memset(&info, 0, sizeof(info));
		if (!AAS_AreaInfo(area, &info)) {
			continue;
		}

		pointArea = AAS_PointAreaNum(info.center);
		if (pointArea <= 0) {
			continue;
		}

		reachability = AAS_AreaReachability(pointArea);
		if (fallbackArea == 0) {
			fallbackArea = area;
			fallbackPointArea = pointArea;
			fallbackPresenceType = info.presencetype;
			fallbackCluster = info.cluster;
			fallbackReachability = reachability;
		}
		if (reachability <= 0) {
			continue;
		}

		fallbackArea = area;
		fallbackPointArea = pointArea;
		fallbackPresenceType = info.presencetype;
		fallbackCluster = info.cluster;
		fallbackReachability = reachability;
		break;
	}

	if (fallbackArea == 0) {
		q3aSmokeStatus.aasSampleMessage = "Q3A AAS area sample failed: no loaded area center resolved";
		return qfalse;
	}

	q3aSmokeStatus.aasSamplePassed = qtrue;
	q3aSmokeStatus.aasSampleArea = fallbackArea;
	q3aSmokeStatus.aasSamplePointArea = fallbackPointArea;
	q3aSmokeStatus.aasSamplePresenceType = fallbackPresenceType;
	q3aSmokeStatus.aasSampleCluster = fallbackCluster;
	q3aSmokeStatus.aasSampleReachability = fallbackReachability;
	snprintf(
		q3aAasSampleMessage,
		sizeof(q3aAasSampleMessage),
		"Q3A AAS area sample passed: area=%d point_area=%d cluster=%d presence=%d reachability=%d",
		fallbackArea,
		fallbackPointArea,
		fallbackCluster,
		fallbackPresenceType,
		fallbackReachability);
	q3aSmokeStatus.aasSampleMessage = q3aAasSampleMessage;
	return qtrue;
}

static int Q3A_BotLibImport_RunAASClusterSmoke(void) {
	int area;
	int cluster;
	aas_areainfo_t info;

	q3aSmokeStatus.aasClusterAttempted = qtrue;
	q3aSmokeStatus.aasClusterPassed = qfalse;
	q3aSmokeStatus.aasClusterArea = 0;
	q3aSmokeStatus.aasClusterCluster = 0;
	q3aSmokeStatus.aasClusterNumClusters = 0;
	q3aSmokeStatus.aasClusterAreas = 0;
	q3aSmokeStatus.aasClusterReachabilityAreas = 0;
	q3aSmokeStatus.aasClusterFailures = 0;

	if (!aasworld.loaded) {
		q3aSmokeStatus.aasClusterFailures++;
		Q3A_BotLibImport_SetAASClusterMessage("Q3A AAS clustering failed: AAS is not loaded");
		return qfalse;
	}

	AAS_InitClustering();
	q3aSmokeStatus.aasClusterNumClusters = aasworld.numclusters;

	if (aasworld.numclusters <= 0 || aasworld.clusters == NULL || aasworld.areasettings == NULL) {
		q3aSmokeStatus.aasClusterFailures++;
		Q3A_BotLibImport_SetAASClusterMessage("Q3A AAS clustering failed: cluster table is unavailable");
		return qfalse;
	}

	area = q3aSmokeStatus.aasSampleArea;
	Com_Memset(&info, 0, sizeof(info));
	if (area <= 0 || area >= aasworld.numareas || !AAS_AreaInfo(area, &info) || info.cluster <= 0) {
		area = 0;
		for (int candidate = 1; candidate < aasworld.numareas; ++candidate) {
			Com_Memset(&info, 0, sizeof(info));
			if (!AAS_AreaInfo(candidate, &info) || info.cluster <= 0) {
				continue;
			}
			area = candidate;
			break;
		}
	}

	cluster = info.cluster;
	if (area <= 0 || cluster <= 0 || cluster >= aasworld.numclusters) {
		q3aSmokeStatus.aasClusterFailures++;
		Q3A_BotLibImport_SetAASClusterMessage("Q3A AAS clustering failed: sampled area has no valid cluster");
		return qfalse;
	}

	q3aSmokeStatus.aasClusterArea = area;
	q3aSmokeStatus.aasClusterCluster = cluster;
	q3aSmokeStatus.aasClusterAreas = aasworld.clusters[cluster].numareas;
	q3aSmokeStatus.aasClusterReachabilityAreas = aasworld.clusters[cluster].numreachabilityareas;
	q3aSmokeStatus.aasClusterPassed =
		q3aSmokeStatus.aasClusterAreas > 0 &&
		q3aSmokeStatus.aasClusterReachabilityAreas > 0 &&
		q3aSmokeStatus.aasClusterFailures == 0;
	Q3A_BotLibImport_SetAASClusterMessage(
		q3aSmokeStatus.aasClusterPassed ? "Q3A AAS clustering passed" : "Q3A AAS clustering failed");
	return q3aSmokeStatus.aasClusterPassed;
}

static int Q3A_BotLibImport_FindRoutableAreaAfter(int afterArea, int *outArea, vec3_t outOrigin) {
	int area;

	if (outArea != NULL) {
		*outArea = 0;
	}
	if (outOrigin != NULL) {
		VectorClear(outOrigin);
	}

	if (!aasworld.loaded || !aasworld.initialized) {
		return qfalse;
	}

	if (afterArea < 0) {
		afterArea = 0;
	}

	for (area = afterArea + 1; area < aasworld.numareas; ++area) {
		aas_areainfo_t info;
		int pointArea;

		if (!AAS_AreaReachability(area)) {
			continue;
		}

		Com_Memset(&info, 0, sizeof(info));
		if (!AAS_AreaInfo(area, &info)) {
			continue;
		}

		pointArea = AAS_PointAreaNum(info.center);
		if (pointArea <= 0) {
			continue;
		}

		if (outArea != NULL) {
			*outArea = pointArea;
		}
		if (outOrigin != NULL) {
			VectorCopy(info.center, outOrigin);
		}
		return qtrue;
	}

	return qfalse;
}

static float Q3A_BotLibImport_DistanceSquared(const vec3_t a, const vec3_t b) {
	float dx = a[0] - b[0];
	float dy = a[1] - b[1];
	float dz = a[2] - b[2];
	return dx * dx + dy * dy + dz * dz;
}

static int Q3A_BotLibImport_FindRouteAreaForPoint(const vec3_t origin, int *outArea, vec3_t outOrigin) {
	static const float zOffsets[] = { 0.0f, 16.0f, -16.0f, 32.0f, -32.0f, 64.0f, -64.0f };
	int i;
	int area;
	int bestArea = 0;
	float bestDistance = 999999999.0f;
	vec3_t bestOrigin;

	if (outArea != NULL) {
		*outArea = 0;
	}
	if (outOrigin != NULL) {
		VectorClear(outOrigin);
	}
	VectorClear(bestOrigin);

	if (!aasworld.loaded || !aasworld.initialized || origin == NULL) {
		return qfalse;
	}

	for (i = 0; i < (int)(sizeof(zOffsets) / sizeof(zOffsets[0])); ++i) {
		vec3_t candidate;

		VectorCopy(origin, candidate);
		candidate[2] += zOffsets[i];
		area = AAS_PointAreaNum(candidate);
		if (area > 0 && area < aasworld.numareas && AAS_AreaReachability(area)) {
			if (outArea != NULL) {
				*outArea = area;
			}
			if (outOrigin != NULL) {
				VectorCopy(candidate, outOrigin);
			}
			return qtrue;
		}
	}

	for (area = 1; area < aasworld.numareas; ++area) {
		aas_areainfo_t info;
		float distance;

		if (!AAS_AreaReachability(area)) {
			continue;
		}

		Com_Memset(&info, 0, sizeof(info));
		if (!AAS_AreaInfo(area, &info)) {
			continue;
		}

		distance = Q3A_BotLibImport_DistanceSquared(origin, info.center);
		if (distance < bestDistance) {
			bestDistance = distance;
			bestArea = area;
			VectorCopy(info.center, bestOrigin);
		}
	}

	if (bestArea <= 0) {
		return qfalse;
	}

	if (outArea != NULL) {
		*outArea = bestArea;
	}
	if (outOrigin != NULL) {
		VectorCopy(bestOrigin, outOrigin);
	}
	return qtrue;
}

static int Q3A_BotLibImport_TryRouteGoalArea(
	int startArea,
	vec3_t startOrigin,
	int candidateArea,
	int *outGoalArea,
	vec3_t outGoalOrigin,
	int *outTravelTime,
	int *outReachability) {
	aas_areainfo_t goalInfo;
	int pointGoalArea;
	int travelTime;
	int reachability;

	if (candidateArea <= 0 || candidateArea >= aasworld.numareas || candidateArea == startArea) {
		return qfalse;
	}
	if (!AAS_AreaReachability(candidateArea)) {
		return qfalse;
	}

	Com_Memset(&goalInfo, 0, sizeof(goalInfo));
	if (!AAS_AreaInfo(candidateArea, &goalInfo)) {
		return qfalse;
	}

	pointGoalArea = AAS_PointAreaNum(goalInfo.center);
	if (pointGoalArea <= 0 || pointGoalArea == startArea) {
		return qfalse;
	}

	travelTime = AAS_AreaTravelTimeToGoalArea(startArea, startOrigin, pointGoalArea, TFL_DEFAULT);
	reachability = AAS_AreaReachabilityToGoalArea(startArea, startOrigin, pointGoalArea, TFL_DEFAULT);
	if (travelTime <= 0 || reachability <= 0) {
		return qfalse;
	}

	if (outGoalArea != NULL) {
		*outGoalArea = pointGoalArea;
	}
	if (outGoalOrigin != NULL) {
		VectorCopy(goalInfo.center, outGoalOrigin);
	}
	if (outTravelTime != NULL) {
		*outTravelTime = travelTime;
	}
	if (outReachability != NULL) {
		*outReachability = reachability;
	}
	return qtrue;
}

static int Q3A_BotLibImport_FindRouteGoalFrom(
	int startArea,
	vec3_t startOrigin,
	int preferredGoalArea,
	int *outGoalArea,
	vec3_t outGoalOrigin,
	int *outTravelTime,
	int *outReachability) {
	int offset;

	if (outGoalArea != NULL) {
		*outGoalArea = 0;
	}
	if (outGoalOrigin != NULL) {
		VectorClear(outGoalOrigin);
	}
	if (outTravelTime != NULL) {
		*outTravelTime = 0;
	}
	if (outReachability != NULL) {
		*outReachability = 0;
	}

	if (!aasworld.loaded || !aasworld.initialized || startArea <= 0 || startArea >= aasworld.numareas) {
		return qfalse;
	}

	if (preferredGoalArea > 0 &&
		Q3A_BotLibImport_TryRouteGoalArea(
			startArea,
			startOrigin,
			preferredGoalArea,
			outGoalArea,
			outGoalOrigin,
			outTravelTime,
			outReachability)) {
		return qtrue;
	}

	for (offset = 1; offset < aasworld.numareas; ++offset) {
		int candidateArea = startArea + offset;

		while (candidateArea >= aasworld.numareas) {
			candidateArea -= aasworld.numareas - 1;
		}
		if (candidateArea <= 0 || candidateArea == preferredGoalArea) {
			continue;
		}

		if (Q3A_BotLibImport_TryRouteGoalArea(
				startArea,
				startOrigin,
				candidateArea,
				outGoalArea,
				outGoalOrigin,
				outTravelTime,
				outReachability)) {
			return qtrue;
		}
	}

	return qfalse;
}

int Q3A_BotLibImport_BuildRouteSteer(
	const float origin[3],
	int preferredGoalArea,
	Q3ABotLibImportRouteSteerResult *result) {
	int startArea = 0;
	int goalArea = 0;
	int travelTime = 0;
	int reachability = 0;
	vec3_t startOrigin;
	vec3_t goalOrigin;
	aas_reachability_t reach;
	aas_predictroute_t fullRoute;
	aas_predictroute_t stepRoute;
	aas_predictroute_t pointRoute;
	int routePointIndex;

	if (result == NULL) {
		return qfalse;
	}

	Com_Memset(result, 0, sizeof(*result));
	VectorClear(startOrigin);
	VectorClear(goalOrigin);
	Com_Memset(&reach, 0, sizeof(reach));
	Com_Memset(&fullRoute, 0, sizeof(fullRoute));
	Com_Memset(&stepRoute, 0, sizeof(stepRoute));
	Com_Memset(&pointRoute, 0, sizeof(pointRoute));

	if (!aasworld.loaded || !aasworld.initialized || origin == NULL) {
		return qfalse;
	}

	if (!Q3A_BotLibImport_FindRouteAreaForPoint(origin, &startArea, startOrigin)) {
		return qfalse;
	}

	result->startArea = startArea;
	if (!Q3A_BotLibImport_FindRouteGoalFrom(
			startArea,
			startOrigin,
			preferredGoalArea,
			&goalArea,
			goalOrigin,
			&travelTime,
			&reachability)) {
		return qfalse;
	}

	result->goalArea = goalArea;
	result->travelTime = travelTime;
	result->reachability = reachability;
	VectorCopy(goalOrigin, result->goalOrigin);

	if (reachability > 0) {
		AAS_ReachabilityFromNum(reachability, &reach);
		result->reachabilityTravelType = reach.traveltype & TRAVELTYPE_MASK;
		result->reachabilityTravelFlags = AAS_TravelFlagForType(reach.traveltype);
		result->reachabilityEndArea = reach.areanum;
	}

	if (!AAS_PredictRoute(&fullRoute, startArea, startOrigin, goalArea, TFL_DEFAULT, 0, 0, RSE_NONE, 0, 0, 0) ||
		fullRoute.endarea != goalArea) {
		result->routeEndArea = fullRoute.endarea;
		result->stopEvent = fullRoute.stopevent;
		return qfalse;
	}

	for (routePointIndex = 0; routePointIndex < Q3A_BOTLIB_IMPORT_MAX_ROUTE_POINTS; ++routePointIndex) {
		Com_Memset(&pointRoute, 0, sizeof(pointRoute));
		(void)AAS_PredictRoute(
			&pointRoute,
			startArea,
			startOrigin,
			goalArea,
			TFL_DEFAULT,
			routePointIndex + 1,
			0,
			RSE_NONE,
			0,
			0,
			0);
		if (pointRoute.endarea <= 0 || pointRoute.stopevent == RSE_NOROUTE) {
			break;
		}

		VectorCopy(pointRoute.endpos, result->routePoints[result->routePointCount]);
		result->routePointCount++;
		if (pointRoute.endarea == goalArea) {
			break;
		}
	}

	(void)AAS_PredictRoute(&stepRoute, startArea, startOrigin, goalArea, TFL_DEFAULT, 1, 0, RSE_NONE, 0, 0, 0);
	result->routeEndArea = stepRoute.endarea;
	result->stopEvent = stepRoute.stopevent;
	if (stepRoute.endarea <= 0 || stepRoute.stopevent == RSE_NOROUTE) {
		return qfalse;
	}
	VectorCopy(stepRoute.endpos, result->moveTarget);

	result->success = qtrue;
	return qtrue;
}

static int Q3A_BotLibImport_RunAASRouteSmoke(void) {
	int startArea = 0;
	int goalArea = 0;
	int travelTime = 0;
	int reachability = 0;
	int startCandidate;
	aas_predictroute_t route;
	vec3_t startOrigin;
	vec3_t goalOrigin;

	q3aSmokeStatus.aasRouteAttempted = qtrue;
	q3aSmokeStatus.aasRoutePassed = qfalse;
	q3aSmokeStatus.aasRouteStartArea = 0;
	q3aSmokeStatus.aasRouteGoalArea = 0;
	q3aSmokeStatus.aasRouteTravelTime = 0;
	q3aSmokeStatus.aasRouteReachability = 0;
	q3aSmokeStatus.aasRouteEndArea = 0;
	q3aSmokeStatus.aasRouteStopEvent = 0;
	VectorClear(startOrigin);
	VectorClear(goalOrigin);
	Com_Memset(&route, 0, sizeof(route));

	if (!aasworld.loaded || !aasworld.initialized) {
		q3aSmokeStatus.aasRouteMessage = "Q3A AAS route query failed: AAS is not initialized";
		return qfalse;
	}

	for (startCandidate = 1; startCandidate < aasworld.numareas; ++startCandidate) {
		int goalCandidate;

		if (!Q3A_BotLibImport_FindRoutableAreaAfter(startCandidate - 1, &startArea, startOrigin)) {
			break;
		}

		for (goalCandidate = 1; goalCandidate < aasworld.numareas; ++goalCandidate) {
			aas_areainfo_t goalInfo;
			int pointGoalArea;

			if (goalCandidate == startArea || !AAS_AreaReachability(goalCandidate)) {
				continue;
			}

			Com_Memset(&goalInfo, 0, sizeof(goalInfo));
			if (!AAS_AreaInfo(goalCandidate, &goalInfo)) {
				continue;
			}

			pointGoalArea = AAS_PointAreaNum(goalInfo.center);
			if (pointGoalArea <= 0 || pointGoalArea == startArea) {
				continue;
			}

			travelTime = AAS_AreaTravelTimeToGoalArea(startArea, startOrigin, pointGoalArea, TFL_DEFAULT);
			reachability = AAS_AreaReachabilityToGoalArea(startArea, startOrigin, pointGoalArea, TFL_DEFAULT);
			if (travelTime > 0 && reachability > 0) {
				goalArea = pointGoalArea;
				VectorCopy(goalInfo.center, goalOrigin);
				break;
			}
		}

		if (travelTime > 0 && reachability > 0) {
			break;
		}
	}

	if (travelTime <= 0 || reachability <= 0) {
		snprintf(
			q3aAasRouteMessage,
			sizeof(q3aAasRouteMessage),
			"Q3A AAS route query failed: start=%d goal=%d travel_time=%d reachability=%d",
			startArea,
			goalArea,
			travelTime,
			reachability);
		q3aSmokeStatus.aasRouteMessage = q3aAasRouteMessage;
		return qfalse;
	}

	if (!AAS_PredictRoute(&route, startArea, startOrigin, goalArea, TFL_DEFAULT, 0, 0, RSE_NONE, 0, 0, 0) ||
		route.endarea != goalArea) {
		snprintf(
			q3aAasRouteMessage,
			sizeof(q3aAasRouteMessage),
			"Q3A AAS route query failed: prediction ended at %d for start=%d goal=%d travel_time=%d reachability=%d",
			route.endarea,
			startArea,
			goalArea,
			travelTime,
			reachability);
		q3aSmokeStatus.aasRouteEndArea = route.endarea;
		q3aSmokeStatus.aasRouteStopEvent = route.stopevent;
		q3aSmokeStatus.aasRouteMessage = q3aAasRouteMessage;
		return qfalse;
	}

	q3aSmokeStatus.aasRoutePassed = qtrue;
	q3aSmokeStatus.aasRouteStartArea = startArea;
	q3aSmokeStatus.aasRouteGoalArea = goalArea;
	q3aSmokeStatus.aasRouteTravelTime = travelTime;
	q3aSmokeStatus.aasRouteReachability = reachability;
	q3aSmokeStatus.aasRouteEndArea = route.endarea;
	q3aSmokeStatus.aasRouteStopEvent = route.stopevent;
	snprintf(
		q3aAasRouteMessage,
		sizeof(q3aAasRouteMessage),
		"Q3A AAS route query passed: start=%d goal=%d travel_time=%d reachability=%d route_end=%d stop=%d",
		startArea,
		goalArea,
		travelTime,
		reachability,
		route.endarea,
		route.stopevent);
	q3aSmokeStatus.aasRouteMessage = q3aAasRouteMessage;
	return qtrue;
}

static int Q3A_BotLibImport_RunAASAltRouteSmoke(void) {
	enum { Q3A_ALT_ROUTE_MAX_GOALS = 8 };
	aas_areainfo_t startInfo;
	aas_areainfo_t goalInfo;
	aas_altroutegoal_t goals[Q3A_ALT_ROUTE_MAX_GOALS];
	int goalCount;
	int startArea;
	int goalArea;

	q3aSmokeStatus.aasAltRouteAttempted = qtrue;
	q3aSmokeStatus.aasAltRoutePassed = qfalse;
	q3aSmokeStatus.aasAltRouteStartArea = 0;
	q3aSmokeStatus.aasAltRouteGoalArea = 0;
	q3aSmokeStatus.aasAltRouteGoals = 0;
	q3aSmokeStatus.aasAltRouteFirstArea = 0;
	q3aSmokeStatus.aasAltRouteFirstStartTravelTime = 0;
	q3aSmokeStatus.aasAltRouteFirstGoalTravelTime = 0;
	q3aSmokeStatus.aasAltRouteFirstExtraTravelTime = 0;
	q3aSmokeStatus.aasAltRouteFailures = 0;

	if (!aasworld.loaded || !aasworld.initialized) {
		q3aSmokeStatus.aasAltRouteFailures++;
		Q3A_BotLibImport_SetAASAltRouteMessage("Q3A AAS alternative route query failed: AAS is not initialized");
		return qfalse;
	}

	if (!q3aSmokeStatus.aasRoutePassed ||
		q3aSmokeStatus.aasRouteStartArea <= 0 ||
		q3aSmokeStatus.aasRouteGoalArea <= 0) {
		q3aSmokeStatus.aasAltRouteFailures++;
		Q3A_BotLibImport_SetAASAltRouteMessage("Q3A AAS alternative route query failed: route smoke has not passed");
		return qfalse;
	}

	startArea = q3aSmokeStatus.aasRouteStartArea;
	goalArea = q3aSmokeStatus.aasRouteGoalArea;
	Com_Memset(&startInfo, 0, sizeof(startInfo));
	Com_Memset(&goalInfo, 0, sizeof(goalInfo));
	if (!AAS_AreaInfo(startArea, &startInfo) || !AAS_AreaInfo(goalArea, &goalInfo)) {
		q3aSmokeStatus.aasAltRouteFailures++;
		Q3A_BotLibImport_SetAASAltRouteMessage("Q3A AAS alternative route query failed: route endpoints are invalid");
		return qfalse;
	}

	Com_Memset(goals, 0, sizeof(goals));
	goalCount = AAS_AlternativeRouteGoals(
		startInfo.center,
		startArea,
		goalInfo.center,
		goalArea,
		TFL_DEFAULT,
		goals,
		Q3A_ALT_ROUTE_MAX_GOALS,
		ALTROUTEGOAL_ALL);

	q3aSmokeStatus.aasAltRouteStartArea = startArea;
	q3aSmokeStatus.aasAltRouteGoalArea = goalArea;
	if (goalCount < 0 || goalCount > Q3A_ALT_ROUTE_MAX_GOALS) {
		q3aSmokeStatus.aasAltRouteFailures++;
		q3aSmokeStatus.aasAltRouteGoals = goalCount;
		Q3A_BotLibImport_SetAASAltRouteMessage("Q3A AAS alternative route query failed");
		return qfalse;
	}

	q3aSmokeStatus.aasAltRouteGoals = goalCount;
	if (goalCount > 0) {
		if (goals[0].areanum <= 0 || goals[0].areanum >= aasworld.numareas) {
			q3aSmokeStatus.aasAltRouteFailures++;
			Q3A_BotLibImport_SetAASAltRouteMessage("Q3A AAS alternative route query failed: first goal area is invalid");
			return qfalse;
		}
		q3aSmokeStatus.aasAltRouteFirstArea = goals[0].areanum;
		q3aSmokeStatus.aasAltRouteFirstStartTravelTime = goals[0].starttraveltime;
		q3aSmokeStatus.aasAltRouteFirstGoalTravelTime = goals[0].goaltraveltime;
		q3aSmokeStatus.aasAltRouteFirstExtraTravelTime = goals[0].extratraveltime;
	}

	q3aSmokeStatus.aasAltRoutePassed = q3aSmokeStatus.aasAltRouteFailures == 0;
	Q3A_BotLibImport_SetAASAltRouteMessage(
		q3aSmokeStatus.aasAltRoutePassed ? "Q3A AAS alternative route query passed" :
											"Q3A AAS alternative route query failed");
	return q3aSmokeStatus.aasAltRoutePassed;
}

static int Q3A_BotLibImport_FindMovementSmokeOrigin(int *outArea, vec3_t outOrigin) {
	int area;
	vec3_t mins;
	vec3_t maxs;

	AAS_PresenceTypeBoundingBox(PRESENCE_NORMAL, mins, maxs);

	for (area = 1; area < aasworld.numareas; ++area) {
		aas_areainfo_t info;
		vec3_t origin;

		if (!AAS_AreaReachability(area)) {
			continue;
		}

		Com_Memset(&info, 0, sizeof(info));
		if (!AAS_AreaInfo(area, &info) || !(info.presencetype & PRESENCE_NORMAL)) {
			continue;
		}

		VectorCopy(info.center, origin);
		origin[2] += 24.0f;
		if (!AAS_DropToFloor(origin, mins, maxs) || AAS_PointAreaNum(origin) <= 0) {
			continue;
		}

		if (outArea != NULL) {
			*outArea = AAS_PointAreaNum(origin);
		}
		if (outOrigin != NULL) {
			VectorCopy(origin, outOrigin);
		}
		return qtrue;
	}

	return qfalse;
}

static int Q3A_BotLibImport_RunAASMovementSmoke(void) {
	int startArea = 0;
	float jumpVelocity = 0.0f;
	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	vec3_t jumpEnd;

	q3aSmokeStatus.aasMovementAttempted = qtrue;
	q3aSmokeStatus.aasMovementPassed = qfalse;
	q3aSmokeStatus.aasMovementEndArea = 0;
	q3aSmokeStatus.aasMovementStopEvent = 0;
	q3aSmokeStatus.aasMovementFrames = 0;
	q3aSmokeStatus.aasMovementDropToFloorPassed = qfalse;
	q3aSmokeStatus.aasMovementJumpVelocityPassed = qfalse;
	VectorClear(origin);
	VectorClear(velocity);
	VectorClear(cmdmove);
	VectorClear(jumpEnd);
	Com_Memset(&move, 0, sizeof(move));

	if (!aasworld.loaded || !aasworld.initialized) {
		q3aSmokeStatus.aasMovementMessage = "Q3A AAS movement prediction failed: AAS is not initialized";
		return qfalse;
	}

	if (!Q3A_BotLibImport_FindMovementSmokeOrigin(&startArea, origin)) {
		q3aSmokeStatus.aasMovementMessage = "Q3A AAS movement prediction failed: no normal reachable floor origin";
		return qfalse;
	}

	q3aSmokeStatus.aasMovementDropToFloorPassed = qtrue;
	VectorCopy(origin, jumpEnd);
	jumpEnd[0] += 64.0f;
	q3aSmokeStatus.aasMovementJumpVelocityPassed =
		AAS_HorizontalVelocityForJump(270.0f, origin, jumpEnd, &jumpVelocity) != 0;

	cmdmove[0] = 120.0f;
	if (!AAS_PredictClientMovement(
			&move,
			-1,
			origin,
			PRESENCE_NORMAL,
			AAS_OnGround(origin, PRESENCE_NORMAL, -1),
			velocity,
			cmdmove,
			4,
			8,
			0.1f,
			SE_NONE,
			0,
			qfalse)) {
		snprintf(
			q3aAasMovementMessage,
			sizeof(q3aAasMovementMessage),
			"Q3A AAS movement prediction failed: start=%d",
			startArea);
		q3aSmokeStatus.aasMovementMessage = q3aAasMovementMessage;
		return qfalse;
	}

	q3aSmokeStatus.aasMovementEndArea = move.endarea;
	q3aSmokeStatus.aasMovementStopEvent = move.stopevent;
	q3aSmokeStatus.aasMovementFrames = move.frames;
	q3aSmokeStatus.aasMovementPassed =
		move.endarea > 0 && move.frames > 0 && q3aSmokeStatus.aasMovementJumpVelocityPassed;
	snprintf(
		q3aAasMovementMessage,
		sizeof(q3aAasMovementMessage),
		"Q3A AAS movement prediction %s: start=%d end=%d stop=%d frames=%d drop=%s jump=%s",
		q3aSmokeStatus.aasMovementPassed ? "passed" : "failed",
		startArea,
		move.endarea,
		move.stopevent,
		move.frames,
		q3aSmokeStatus.aasMovementDropToFloorPassed ? "yes" : "no",
		q3aSmokeStatus.aasMovementJumpVelocityPassed ? "yes" : "no");
	q3aSmokeStatus.aasMovementMessage = q3aAasMovementMessage;
	return q3aSmokeStatus.aasMovementPassed;
}

int Q3A_BotLibImport_RunDebugDrawSmoke(void) {
	int startArea;
	int goalArea;
	aas_areainfo_t startInfo;
	aas_areainfo_t goalInfo;
	vec3_t start;
	vec3_t end;

	Q3A_BotLibImport_ResetDebugDrawStatus("Q3A debug draw bridge started");
	q3aSmokeStatus.debugDrawAttempted = qtrue;
	q3aSmokeStatus.debugDrawCallbackSet = q3aDebugDrawCallback != NULL;

	if (q3aDebugDrawCallback == NULL) {
		q3aSmokeStatus.debugDrawFailures++;
		Q3A_BotLibImport_SetDebugDrawMessage("Q3A debug draw bridge failed: callback is not registered");
		return qfalse;
	}

	if (!aasworld.loaded || !aasworld.initialized) {
		q3aSmokeStatus.debugDrawFailures++;
		Q3A_BotLibImport_SetDebugDrawMessage("Q3A debug draw bridge failed: AAS is not initialized");
		return qfalse;
	}

	startArea = q3aSmokeStatus.aasRouteStartArea > 0 ? q3aSmokeStatus.aasRouteStartArea : q3aSmokeStatus.aasSampleArea;
	goalArea = q3aSmokeStatus.aasRouteGoalArea > 0 ? q3aSmokeStatus.aasRouteGoalArea : q3aSmokeStatus.aasSampleArea;
	if (startArea <= 0 || goalArea <= 0) {
		q3aSmokeStatus.debugDrawFailures++;
		Q3A_BotLibImport_SetDebugDrawMessage("Q3A debug draw bridge failed: no route/sample areas");
		return qfalse;
	}

	Com_Memset(&startInfo, 0, sizeof(startInfo));
	Com_Memset(&goalInfo, 0, sizeof(goalInfo));
	if (!AAS_AreaInfo(startArea, &startInfo) || !AAS_AreaInfo(goalArea, &goalInfo)) {
		q3aSmokeStatus.debugDrawFailures++;
		Q3A_BotLibImport_SetDebugDrawMessage("Q3A debug draw bridge failed: area info unavailable");
		return qfalse;
	}

	VectorCopy(startInfo.center, start);
	VectorCopy(goalInfo.center, end);
	start[2] += 24.0f;
	end[2] += 24.0f;

	AAS_ClearShownDebugLines();
	q3aSmokeStatus.debugDrawClears++;
	AAS_DebugLine(start, end, LINECOLOR_GREEN);
	AAS_PermanentLine(start, end, LINECOLOR_YELLOW);
	AAS_DrawPermanentCross(start, 8.0f, LINECOLOR_BLUE);
	q3aSmokeStatus.debugDrawCrosses++;
	AAS_DrawArrow(start, end, LINECOLOR_GREEN, LINECOLOR_YELLOW);
	q3aSmokeStatus.debugDrawArrows++;

	q3aSmokeStatus.debugDrawPassed =
		q3aSmokeStatus.debugDrawFailures == 0 &&
		q3aSmokeStatus.debugDrawLines >= 2 &&
		q3aSmokeStatus.debugDrawCrosses >= 1 &&
		q3aSmokeStatus.debugDrawArrows >= 1 &&
		q3aSmokeStatus.debugDrawClears >= 1;
	Q3A_BotLibImport_SetDebugDrawMessage(
		q3aSmokeStatus.debugDrawPassed ? "Q3A debug draw bridge passed" : "Q3A debug draw bridge failed");
	return q3aSmokeStatus.debugDrawPassed;
}

int Q3A_BotLibImport_RunDebugPolygonSmoke(void) {
	int area;
	int polygonId;
	aas_areainfo_t info;
	vec3_t points[4];

	Q3A_BotLibImport_ResetDebugPolygonStatus("Q3A debug polygon bridge started");
	q3aSmokeStatus.debugPolygonAttempted = qtrue;
	q3aSmokeStatus.debugPolygonCallbackSet = q3aDebugPolygonCallback != NULL;

	if (q3aDebugPolygonCallback == NULL) {
		q3aSmokeStatus.debugPolygonFailures++;
		Q3A_BotLibImport_SetDebugPolygonMessage("Q3A debug polygon bridge failed: callback is not registered");
		return qfalse;
	}

	if (!aasworld.loaded || !aasworld.initialized) {
		q3aSmokeStatus.debugPolygonFailures++;
		Q3A_BotLibImport_SetDebugPolygonMessage("Q3A debug polygon bridge failed: AAS is not initialized");
		return qfalse;
	}

	area = q3aSmokeStatus.aasRouteStartArea > 0 ? q3aSmokeStatus.aasRouteStartArea : q3aSmokeStatus.aasSampleArea;
	if (area <= 0) {
		q3aSmokeStatus.debugPolygonFailures++;
		Q3A_BotLibImport_SetDebugPolygonMessage("Q3A debug polygon bridge failed: no sample area");
		return qfalse;
	}

	Com_Memset(&info, 0, sizeof(info));
	if (!AAS_AreaInfo(area, &info)) {
		q3aSmokeStatus.debugPolygonFailures++;
		Q3A_BotLibImport_SetDebugPolygonMessage("Q3A debug polygon bridge failed: area info unavailable");
		return qfalse;
	}

	VectorCopy(info.center, points[0]);
	VectorCopy(info.center, points[1]);
	VectorCopy(info.center, points[2]);
	VectorCopy(info.center, points[3]);
	points[0][0] += 16.0f;
	points[1][1] += 16.0f;
	points[2][0] -= 16.0f;
	points[3][1] -= 16.0f;
	points[0][2] += 32.0f;
	points[1][2] += 32.0f;
	points[2][2] += 32.0f;
	points[3][2] += 32.0f;

	if (botimport.DebugPolygonCreate == NULL || botimport.DebugPolygonDelete == NULL) {
		q3aSmokeStatus.debugPolygonFailures++;
		Q3A_BotLibImport_SetDebugPolygonMessage("Q3A debug polygon bridge failed: botimport callbacks are not installed");
		return qfalse;
	}

	polygonId = botimport.DebugPolygonCreate(LINECOLOR_BLUE, 4, points);
	if (polygonId <= 0) {
		q3aSmokeStatus.debugPolygonFailures++;
		Q3A_BotLibImport_SetDebugPolygonMessage("Q3A debug polygon bridge failed: create returned no id");
		return qfalse;
	}
	botimport.DebugPolygonDelete(polygonId);

	q3aSmokeStatus.debugPolygonPassed =
		q3aSmokeStatus.debugPolygonFailures == 0 &&
		q3aSmokeStatus.debugPolygonCreates >= 1 &&
		q3aSmokeStatus.debugPolygonDeletes >= 1 &&
		q3aSmokeStatus.debugPolygonPoints >= 4 &&
		q3aSmokeStatus.debugPolygonLastId == polygonId;
	Q3A_BotLibImport_SetDebugPolygonMessage(
		q3aSmokeStatus.debugPolygonPassed ? "Q3A debug polygon bridge passed" : "Q3A debug polygon bridge failed");
	return q3aSmokeStatus.debugPolygonPassed;
}

int Q3A_BotLibImport_RunDebugAreaSmoke(void) {
	int area;
	int beforeLines;
	int beforePolygonCreates;
	int beforePolygonDeletes;
	int beforeDrawFailures;
	int beforePolygonFailures;
	aas_areainfo_t info;

	Q3A_BotLibImport_ResetDebugAreaStatus("Q3A AAS debug area helpers started");
	q3aSmokeStatus.debugAreaAttempted = qtrue;

	if (q3aDebugDrawCallback == NULL || q3aDebugPolygonCallback == NULL) {
		q3aSmokeStatus.debugAreaFailures++;
		Q3A_BotLibImport_SetDebugAreaMessage("Q3A AAS debug area helpers failed: callback is not registered");
		return qfalse;
	}

	if (!aasworld.loaded || !aasworld.initialized) {
		q3aSmokeStatus.debugAreaFailures++;
		Q3A_BotLibImport_SetDebugAreaMessage("Q3A AAS debug area helpers failed: AAS is not initialized");
		return qfalse;
	}

	area = q3aSmokeStatus.aasRouteStartArea > 0 ? q3aSmokeStatus.aasRouteStartArea : q3aSmokeStatus.aasSampleArea;
	if (area <= 0) {
		q3aSmokeStatus.debugAreaFailures++;
		Q3A_BotLibImport_SetDebugAreaMessage("Q3A AAS debug area helpers failed: no sample area");
		return qfalse;
	}

	Com_Memset(&info, 0, sizeof(info));
	if (!AAS_AreaInfo(area, &info)) {
		q3aSmokeStatus.debugAreaFailures++;
		Q3A_BotLibImport_SetDebugAreaMessage("Q3A AAS debug area helpers failed: area info unavailable");
		return qfalse;
	}

	AAS_ClearShownDebugLines();
	AAS_ClearShownPolygons();

	beforeLines = q3aSmokeStatus.debugDrawLines;
	beforePolygonCreates = q3aSmokeStatus.debugPolygonCreates;
	beforePolygonDeletes = q3aSmokeStatus.debugPolygonDeletes;
	beforeDrawFailures = q3aSmokeStatus.debugDrawFailures;
	beforePolygonFailures = q3aSmokeStatus.debugPolygonFailures;

	AAS_ShowArea(area, qfalse);
	AAS_ShowAreaPolygons(area, LINECOLOR_BLUE, qfalse);
	AAS_ClearShownPolygons();
	AAS_ClearShownDebugLines();

	q3aSmokeStatus.debugAreaArea = area;
	q3aSmokeStatus.debugAreaLines = q3aSmokeStatus.debugDrawLines - beforeLines;
	q3aSmokeStatus.debugAreaPolygonCreates = q3aSmokeStatus.debugPolygonCreates - beforePolygonCreates;
	q3aSmokeStatus.debugAreaPolygonDeletes = q3aSmokeStatus.debugPolygonDeletes - beforePolygonDeletes;
	q3aSmokeStatus.debugAreaFailures += q3aSmokeStatus.debugDrawFailures - beforeDrawFailures;
	q3aSmokeStatus.debugAreaFailures += q3aSmokeStatus.debugPolygonFailures - beforePolygonFailures;
	q3aSmokeStatus.debugAreaPassed =
		q3aSmokeStatus.debugAreaFailures == 0 &&
		q3aSmokeStatus.debugAreaLines > 0 &&
		q3aSmokeStatus.debugAreaPolygonCreates > 0 &&
		q3aSmokeStatus.debugAreaPolygonDeletes >= q3aSmokeStatus.debugAreaPolygonCreates;
	Q3A_BotLibImport_SetDebugAreaMessage(
		q3aSmokeStatus.debugAreaPassed ? "Q3A AAS debug area helpers passed" : "Q3A AAS debug area helpers failed");
	return q3aSmokeStatus.debugAreaPassed;
}

static int Q3A_BotLibImport_RouteOverlayDraw(
	int primitive,
	const vec3_t start,
	const vec3_t end,
	float size,
	int color,
	int secondaryColor) {
	const int drawn = Q3A_BotLibImport_DebugDraw(primitive, start, end, size, color, secondaryColor);

	switch (primitive) {
	case Q3A_BOTLIB_DEBUG_DRAW_LINE:
	case Q3A_BOTLIB_DEBUG_DRAW_PERMANENT_LINE:
		q3aSmokeStatus.routeOverlayLines++;
		break;
	case Q3A_BOTLIB_DEBUG_DRAW_CROSS:
		q3aSmokeStatus.routeOverlayCrosses++;
		break;
	case Q3A_BOTLIB_DEBUG_DRAW_ARROW:
		q3aSmokeStatus.routeOverlayArrows++;
		break;
	case Q3A_BOTLIB_DEBUG_DRAW_CLEAR:
		q3aSmokeStatus.routeOverlayClears++;
		break;
	default:
		q3aSmokeStatus.routeOverlayFailures++;
		break;
	}

	if (!drawn) {
		q3aSmokeStatus.routeOverlayFailures++;
	}
	return drawn;
}

int Q3A_BotLibImport_RunRouteOverlaySmoke(void) {
	int startArea;
	int goalArea;
	int travelTime;
	int reachability;
	aas_areainfo_t startInfo;
	aas_areainfo_t goalInfo;
	aas_predictroute_t route;
	vec3_t start;
	vec3_t goal;
	vec3_t predictedEnd;
	vec3_t zero;

	Q3A_BotLibImport_ResetRouteOverlayStatus("Q3A route overlay started");
	Q3A_BotLibImport_ResetDebugDrawStatus("Q3A route overlay debug draw started");
	q3aSmokeStatus.routeOverlayAttempted = qtrue;
	q3aSmokeStatus.debugDrawAttempted = qtrue;
	q3aSmokeStatus.debugDrawCallbackSet = q3aDebugDrawCallback != NULL;

	if (q3aDebugDrawCallback == NULL) {
		q3aSmokeStatus.routeOverlayFailures++;
		Q3A_BotLibImport_SetRouteOverlayMessage("Q3A route overlay failed: debug draw callback is not registered");
		return qfalse;
	}

	if (!aasworld.loaded || !aasworld.initialized) {
		q3aSmokeStatus.routeOverlayFailures++;
		Q3A_BotLibImport_SetRouteOverlayMessage("Q3A route overlay failed: AAS is not initialized");
		return qfalse;
	}

	if (!q3aSmokeStatus.aasRoutePassed && !Q3A_BotLibImport_RunAASRouteSmoke()) {
		q3aSmokeStatus.routeOverlayFailures++;
		Q3A_BotLibImport_SetRouteOverlayMessage("Q3A route overlay failed: no routable sample route");
		return qfalse;
	}

	startArea = q3aSmokeStatus.aasRouteStartArea;
	goalArea = q3aSmokeStatus.aasRouteGoalArea;
	q3aSmokeStatus.routeOverlayStartArea = startArea;
	q3aSmokeStatus.routeOverlayGoalArea = goalArea;

	if (startArea <= 0 || goalArea <= 0) {
		q3aSmokeStatus.routeOverlayFailures++;
		Q3A_BotLibImport_SetRouteOverlayMessage("Q3A route overlay failed: route areas are unavailable");
		return qfalse;
	}

	Com_Memset(&startInfo, 0, sizeof(startInfo));
	Com_Memset(&goalInfo, 0, sizeof(goalInfo));
	Com_Memset(&route, 0, sizeof(route));
	if (!AAS_AreaInfo(startArea, &startInfo) || !AAS_AreaInfo(goalArea, &goalInfo)) {
		q3aSmokeStatus.routeOverlayFailures++;
		Q3A_BotLibImport_SetRouteOverlayMessage("Q3A route overlay failed: route area info is unavailable");
		return qfalse;
	}

	travelTime = AAS_AreaTravelTimeToGoalArea(startArea, startInfo.center, goalArea, TFL_DEFAULT);
	reachability = AAS_AreaReachabilityToGoalArea(startArea, startInfo.center, goalArea, TFL_DEFAULT);
	q3aSmokeStatus.routeOverlayTravelTime = travelTime;
	q3aSmokeStatus.routeOverlayReachability = reachability;
	if (travelTime <= 0 || reachability <= 0) {
		q3aSmokeStatus.routeOverlayFailures++;
		Q3A_BotLibImport_SetRouteOverlayMessage("Q3A route overlay failed: route query is not reachable");
		return qfalse;
	}

	if (!AAS_PredictRoute(&route, startArea, startInfo.center, goalArea, TFL_DEFAULT, 0, 0, RSE_NONE, 0, 0, 0)) {
		q3aSmokeStatus.routeOverlayFailures++;
		Q3A_BotLibImport_SetRouteOverlayMessage("Q3A route overlay failed: route prediction failed");
		return qfalse;
	}

	q3aSmokeStatus.routeOverlayEndArea = route.endarea;
	VectorCopy(startInfo.center, start);
	VectorCopy(goalInfo.center, goal);
	VectorCopy(route.endpos, predictedEnd);
	start[2] += 24.0f;
	goal[2] += 24.0f;
	predictedEnd[2] += 24.0f;
	VectorClear(zero);

	Q3A_BotLibImport_RouteOverlayDraw(Q3A_BOTLIB_DEBUG_DRAW_CLEAR, zero, zero, 0.0f, LINECOLOR_NONE, LINECOLOR_NONE);
	Q3A_BotLibImport_RouteOverlayDraw(Q3A_BOTLIB_DEBUG_DRAW_CROSS, start, start, 10.0f, LINECOLOR_BLUE, LINECOLOR_NONE);
	Q3A_BotLibImport_RouteOverlayDraw(Q3A_BOTLIB_DEBUG_DRAW_CROSS, goal, goal, 12.0f, LINECOLOR_GREEN, LINECOLOR_NONE);
	Q3A_BotLibImport_RouteOverlayDraw(
		Q3A_BOTLIB_DEBUG_DRAW_CROSS,
		predictedEnd,
		predictedEnd,
		6.0f,
		LINECOLOR_ORANGE,
		LINECOLOR_NONE);
	Q3A_BotLibImport_RouteOverlayDraw(
		Q3A_BOTLIB_DEBUG_DRAW_LINE,
		start,
		goal,
		0.0f,
		LINECOLOR_GREEN,
		LINECOLOR_NONE);
	Q3A_BotLibImport_RouteOverlayDraw(
		Q3A_BOTLIB_DEBUG_DRAW_LINE,
		predictedEnd,
		goal,
		0.0f,
		LINECOLOR_YELLOW,
		LINECOLOR_NONE);
	Q3A_BotLibImport_RouteOverlayDraw(
		Q3A_BOTLIB_DEBUG_DRAW_ARROW,
		start,
		predictedEnd,
		10.0f,
		LINECOLOR_GREEN,
		LINECOLOR_YELLOW);

	q3aSmokeStatus.routeOverlayPassed =
		q3aSmokeStatus.routeOverlayFailures == 0 &&
		q3aSmokeStatus.routeOverlayEndArea == goalArea &&
		q3aSmokeStatus.routeOverlayLines >= 2 &&
		q3aSmokeStatus.routeOverlayCrosses >= 3 &&
		q3aSmokeStatus.routeOverlayArrows >= 1 &&
		q3aSmokeStatus.routeOverlayClears >= 1;
	q3aSmokeStatus.debugDrawPassed = q3aSmokeStatus.routeOverlayPassed;
	Q3A_BotLibImport_SetRouteOverlayMessage(
		q3aSmokeStatus.routeOverlayPassed ? "Q3A route overlay passed" : "Q3A route overlay failed");
	Q3A_BotLibImport_SetDebugDrawMessage(
		q3aSmokeStatus.routeOverlayPassed ? "Q3A route overlay debug draw passed" : "Q3A route overlay debug draw failed");
	return q3aSmokeStatus.routeOverlayPassed;
}

void Q3A_BotLibImport_Init(void) {
	Q3A_BotLibFSCloseAllFiles();
	Q3A_BotLibReleaseAllAllocations("Q3A BotLib memory allocator reset");
	q3aSmokeStatus.lifecycleInitCount++;
	q3aSmokeStatus.memoryZoneActiveBytes = 0;
	q3aSmokeStatus.memoryZonePeakBytes = 0;
	q3aSmokeStatus.memoryZoneAllocations = 0;
	q3aSmokeStatus.memoryZoneFrees = 0;
	q3aSmokeStatus.memoryHunkActiveBytes = 0;
	q3aSmokeStatus.memoryHunkPeakBytes = 0;
	q3aSmokeStatus.memoryHunkAllocations = 0;
	q3aSmokeStatus.memoryHunkGroupFrees = 0;
	q3aSmokeStatus.memoryFailures = 0;
	q3aSmokeStatus.lifecyclePersistentZoneBytes = q3aSmokeStatus.memoryZoneActiveBytes;
	Q3A_BotLibImport_SetMemoryMessage("Q3A BotLib memory allocator initialized");
	Q3A_BotLibImport_ResetFilesystemStatus("Q3A BotLib filesystem bridge initialized");
	Com_Memset(&botimport, 0, sizeof(botimport));
	q3aDebugLineNextId = 1;
	q3aDebugPolygonNextId = 1;
	q3aSmokeStatus.printCallbackSet = q3aPrintCallback != NULL;
	botimport.Print = Q3A_BotLibPrint;
	botimport.BotClientCommand = Q3A_BotLibClientCommand;
	botimport.GetMemory = Q3A_BotLibGetMemory;
	botimport.FreeMemory = Q3A_BotLibFreeMemory;
	botimport.AvailableMemory = Q3A_BotLibAvailableMemory;
	botimport.HunkAlloc = Q3A_BotLibHunkAlloc;
	botimport.FS_FOpenFile = Q3A_BotLibFSOpenFile;
	botimport.FS_Read = Q3A_BotLibFSRead;
	botimport.FS_Write = Q3A_BotLibFSWrite;
	botimport.FS_FCloseFile = Q3A_BotLibFSCloseFile;
	botimport.FS_Seek = Q3A_BotLibFSSeek;
	botimport.DebugLineCreate = Q3A_BotLibImport_DebugLineCreate;
	botimport.DebugLineDelete = Q3A_BotLibImport_DebugLineDelete;
	botimport.DebugLineShow = Q3A_BotLibImport_DebugLineShow;
	botimport.DebugPolygonCreate = Q3A_BotLibImport_DebugPolygonCreate;
	botimport.DebugPolygonDelete = Q3A_BotLibImport_DebugPolygonDelete;

	q3aSmokeStatus.initialized = qtrue;
	q3aSmokeStatus.availableMemory = Q3A_BotLibAvailableMemory();
	q3aSmokeStatus.message = "Q3A BotLib import callbacks initialized";
	Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle initialized");
	Q3A_BotLibImport_RunAngleVectorsSmoke();
	Q3A_BotLibImport_ResetAASSampleStatus("Q3A AAS area sample has not run");
	Q3A_BotLibImport_ResetAASClusterStatus("Q3A AAS clustering has not run");
	Q3A_BotLibImport_ResetAASRouteStatus("Q3A AAS route query has not run");
	Q3A_BotLibImport_ResetAASAltRouteStatus("Q3A AAS alternative route query has not run");
	Q3A_BotLibImport_ResetAASMovementStatus("Q3A AAS movement prediction has not run");
	Q3A_BotLibImport_ResetBotClientCommandStatus("Q3A BotClientCommand bridge has not run");
	Q3A_BotLibImport_ResetAASStartFrameStatus("Q3A AAS start frame has not run");
	Q3A_BotLibImport_ResetEntitySyncStatus("Q3A AAS entity sync has not run");
	Q3A_BotLibImport_ResetEntityTraceStatus("Q3A AAS entity trace has not run");
	Q3A_BotLibImport_ResetDebugDrawStatus("Q3A debug draw bridge has not run");
	Q3A_BotLibImport_ResetDebugPolygonStatus("Q3A debug polygon bridge has not run");
	Q3A_BotLibImport_ResetDebugAreaStatus("Q3A AAS debug area helpers have not run");
	Q3A_BotLibImport_ResetRouteOverlayStatus("Q3A route overlay has not run");
	Q3A_BotLibImport_ResetBspLeafLinkStatus("Q3A BSP leaf entity links have not run");
}

void Q3A_BotLibImport_SetPrintCallback(Q3ABotLibImportPrintCallback callback) {
	q3aPrintCallback = callback;
	q3aSmokeStatus.printCallbackSet = callback != NULL;
}

void Q3A_BotLibImport_SetBotClientCommandCallback(Q3ABotLibImportBotClientCommandCallback callback) {
	q3aBotClientCommandCallback = callback;
	q3aSmokeStatus.botClientCommandCallbackSet = callback != NULL;
	Q3A_BotLibImport_SetBotClientCommandMessage(
		callback != NULL ? "Q3A BotClientCommand callback registered" : "Q3A BotClientCommand callback cleared");
}

void Q3A_BotLibImport_SetFilesystemCallbacks(
	Q3ABotLibImportFilesystemLoadCallback loadCallback,
	Q3ABotLibImportFilesystemFreeCallback freeCallback) {
	q3aFilesystemLoadCallback = loadCallback;
	q3aFilesystemFreeCallback = freeCallback;
	q3aSmokeStatus.filesystemCallbackSet = loadCallback != NULL;
	Q3A_BotLibImport_SetFilesystemMessage(
		loadCallback != NULL ? "Q3A BotLib filesystem callback registered" : "Q3A BotLib filesystem callback cleared");
}

void Q3A_BotLibImport_SetEntityTraceCallback(Q3ABotLibImportEntityTraceCallback callback) {
	q3aEntityTraceCallback = callback;
	q3aSmokeStatus.entityTraceCallbackSet = callback != NULL;
	Q3A_BotLibImport_SetEntityTraceMessage(
		callback != NULL ? "Q3A AAS entity trace callback registered" : "Q3A AAS entity trace callback cleared");
}

void Q3A_BotLibImport_SetDebugDrawCallback(Q3ABotLibImportDebugDrawCallback callback) {
	q3aDebugDrawCallback = callback;
	q3aSmokeStatus.debugDrawCallbackSet = callback != NULL;
	Q3A_BotLibImport_SetDebugDrawMessage(
		callback != NULL ? "Q3A debug draw callback registered" : "Q3A debug draw callback cleared");
}

void Q3A_BotLibImport_SetDebugPolygonCallback(Q3ABotLibImportDebugPolygonCallback callback) {
	q3aDebugPolygonCallback = callback;
	q3aSmokeStatus.debugPolygonCallbackSet = callback != NULL;
	Q3A_BotLibImport_SetDebugPolygonMessage(
		callback != NULL ? "Q3A debug polygon callback registered" : "Q3A debug polygon callback cleared");
}

void Q3A_BotLibImport_Shutdown(void) {
	q3aSmokeStatus.lifecycleShutdownCount++;
	Q3A_BotLibImport_UnloadAAS();
	Q3A_BotLibFSCloseAllFiles();
	Q3A_BotLibImport_ClearBspEntityData();
	Q3A_BotLibImport_ClearBspModelData();
	Q3A_BotLibImport_ClearBspCollisionData();
	Q3A_BotLibImport_ClearBspVisibilityData();
	LibVarDeAllocAll();
	Q3A_BotLibReleaseAllAllocations("Q3A BotLib memory allocator shut down");
	Com_Memset(&botimport, 0, sizeof(botimport));

	q3aSmokeStatus.initialized = qfalse;
	q3aSmokeStatus.libvarSmokePassed = qfalse;
	q3aSmokeStatus.aasLoadAttempted = qfalse;
	q3aSmokeStatus.aasLoaded = qfalse;
	q3aSmokeStatus.aasLoadResult = BLERR_NOAASFILE;
	q3aSmokeStatus.aasBspChecksum = 0;
	q3aSmokeStatus.aasAreas = 0;
	q3aSmokeStatus.aasReachability = 0;
	q3aSmokeStatus.aasClusters = 0;
	q3aSmokeStatus.angleVectorsSmokePassed = qfalse;
	q3aSmokeStatus.runtimeMilliseconds = 0;
	q3aSmokeStatus.bspEntityLoadAttempted = qfalse;
	q3aSmokeStatus.bspEntityLoaded = qfalse;
	q3aSmokeStatus.bspEntityCount = 0;
	q3aSmokeStatus.bspEntityPairs = 0;
	q3aSmokeStatus.bspEntityValueSmokePassed = qfalse;
	q3aSmokeStatus.bspModelLoadAttempted = qfalse;
	q3aSmokeStatus.bspModelLoaded = qfalse;
	q3aSmokeStatus.bspModelCount = 0;
	q3aSmokeStatus.bspModelBoundsSmokePassed = qfalse;
	q3aSmokeStatus.bspCollisionLoadAttempted = qfalse;
	q3aSmokeStatus.bspCollisionLoaded = qfalse;
	q3aSmokeStatus.bspCollisionPlanes = 0;
	q3aSmokeStatus.bspCollisionNodes = 0;
	q3aSmokeStatus.bspCollisionLeafs = 0;
	q3aSmokeStatus.bspCollisionBrushes = 0;
	q3aSmokeStatus.bspCollisionPointContentsSmokePassed = qfalse;
	q3aSmokeStatus.bspCollisionTraceSmokePassed = qfalse;
	q3aSmokeStatus.bspVisibilityLoadAttempted = qfalse;
	q3aSmokeStatus.bspVisibilityLoaded = qfalse;
	q3aSmokeStatus.bspVisibilityClusters = 0;
	q3aSmokeStatus.bspVisibilityPvsSmokePassed = qfalse;
	q3aSmokeStatus.bspVisibilityPhsSmokePassed = qfalse;
	Q3A_BotLibImport_ResetAASSampleStatus("Q3A AAS area sample shut down");
	Q3A_BotLibImport_ResetAASClusterStatus("Q3A AAS clustering shut down");
	Q3A_BotLibImport_ResetAASRouteStatus("Q3A AAS route query shut down");
	Q3A_BotLibImport_ResetAASAltRouteStatus("Q3A AAS alternative route query shut down");
	Q3A_BotLibImport_ResetAASMovementStatus("Q3A AAS movement prediction shut down");
	Q3A_BotLibImport_ResetBotClientCommandStatus("Q3A BotClientCommand bridge shut down");
	Q3A_BotLibImport_ResetAASStartFrameStatus("Q3A AAS start frame shut down");
	Q3A_BotLibImport_ResetEntitySyncStatus("Q3A AAS entity sync shut down");
	Q3A_BotLibImport_ResetEntityTraceStatus("Q3A AAS entity trace shut down");
	Q3A_BotLibImport_ResetDebugDrawStatus("Q3A debug draw bridge shut down");
	Q3A_BotLibImport_ResetDebugPolygonStatus("Q3A debug polygon bridge shut down");
	Q3A_BotLibImport_ResetDebugAreaStatus("Q3A AAS debug area helpers shut down");
	Q3A_BotLibImport_ResetRouteOverlayStatus("Q3A route overlay shut down");
	Q3A_BotLibImport_ResetBspLeafLinkStatus("Q3A BSP leaf entity links shut down");
	Q3A_BotLibImport_ResetFilesystemStatus("Q3A BotLib filesystem bridge shut down");
	q3aSmokeStatus.availableMemory = 0;
	q3aSmokeStatus.lifecyclePersistentZoneBytes = q3aSmokeStatus.memoryZoneActiveBytes;
	q3aSmokeStatus.memoryMessage = "Q3A BotLib memory allocator shut down";
	q3aSmokeStatus.message = "Q3A BotLib import callbacks shut down";
	Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle shut down");
	q3aSmokeStatus.aasMessage = "Q3A AAS file loader shut down";
	q3aSmokeStatus.aasClusterMessage = "Q3A AAS clustering shut down";
	q3aSmokeStatus.aasAltRouteMessage = "Q3A AAS alternative route query shut down";
	q3aSmokeStatus.aasMovementMessage = "Q3A AAS movement prediction shut down";
	q3aSmokeStatus.botClientCommandMessage = "Q3A BotClientCommand bridge shut down";
	q3aSmokeStatus.entitySyncMessage = "Q3A AAS entity sync shut down";
	q3aSmokeStatus.entityTraceMessage = "Q3A AAS entity trace shut down";
	q3aSmokeStatus.debugDrawMessage = "Q3A debug draw bridge shut down";
	q3aSmokeStatus.debugPolygonMessage = "Q3A debug polygon bridge shut down";
	q3aSmokeStatus.debugAreaMessage = "Q3A AAS debug area helpers shut down";
	q3aSmokeStatus.routeOverlayMessage = "Q3A route overlay shut down";
	q3aSmokeStatus.bspLeafLinkMessage = "Q3A BSP leaf entity links shut down";
	q3aSmokeStatus.angleVectorsMessage = "Q3A AngleVectors smoke shut down";
	q3aSmokeStatus.bspEntityMessage = "Q3A BSP entity data shut down";
	q3aSmokeStatus.bspModelMessage = "Q3A BSP model data shut down";
	q3aSmokeStatus.bspCollisionMessage = "Q3A BSP collision data shut down";
	q3aSmokeStatus.bspVisibilityMessage = "Q3A BSP visibility data shut down";
}

int Q3A_BotLibImport_RunLibVarSmoke(void) {
	char *value;
	float numericValue;

	if (!q3aSmokeStatus.initialized) {
		Q3A_BotLibImport_Init();
	}

	LibVarDeAllocAll();

	value = LibVarString("worr_q3a_libvar_smoke", "1.25");
	if (value == NULL || Q_stricmp(value, "1.25") != 0) {
		q3aSmokeStatus.libvarSmokePassed = qfalse;
		q3aSmokeStatus.message = "Q3A LibVar smoke failed to allocate initial value";
		LibVarDeAllocAll();
		return qfalse;
	}

	LibVarSet("worr_q3a_libvar_smoke", "2.5");
	numericValue = LibVarGetValue("worr_q3a_libvar_smoke");
	if (numericValue < 2.49f || numericValue > 2.51f) {
		q3aSmokeStatus.libvarSmokePassed = qfalse;
		q3aSmokeStatus.message = "Q3A LibVar smoke failed to update numeric value";
		LibVarDeAllocAll();
		return qfalse;
	}

	if (!LibVarChanged("worr_q3a_libvar_smoke")) {
		q3aSmokeStatus.libvarSmokePassed = qfalse;
		q3aSmokeStatus.message = "Q3A LibVar smoke failed to report modified state";
		LibVarDeAllocAll();
		return qfalse;
	}

	LibVarSetNotModified("worr_q3a_libvar_smoke");
	if (LibVarChanged("worr_q3a_libvar_smoke")) {
		q3aSmokeStatus.libvarSmokePassed = qfalse;
		q3aSmokeStatus.message = "Q3A LibVar smoke failed to clear modified state";
		LibVarDeAllocAll();
		return qfalse;
	}

	LibVarDeAllocAll();
	q3aSmokeStatus.libvarSmokePassed = qtrue;
	q3aSmokeStatus.message = "Q3A LibVar smoke passed";
	return qtrue;
}

int Q3A_BotLibImport_RunBotClientCommandSmoke(void) {
	static char smokeCommand[] = "botlib_smoke";

	if (!q3aSmokeStatus.initialized) {
		Q3A_BotLibImport_Init();
	}

	Q3A_BotLibImport_ResetBotClientCommandStatus("Q3A BotClientCommand bridge started");
	if (botimport.BotClientCommand == NULL) {
		q3aSmokeStatus.botClientCommandFailures++;
		Q3A_BotLibImport_SetBotClientCommandMessage("Q3A BotClientCommand bridge failed: botimport callback missing");
		return qfalse;
	}

	botimport.BotClientCommand(0, smokeCommand);
	q3aSmokeStatus.botClientCommandSmokePassed =
		q3aSmokeStatus.botClientCommandCallbackSet &&
		q3aSmokeStatus.botClientCommandAttempted &&
		q3aSmokeStatus.botClientCommandAccepted == 0 &&
		q3aSmokeStatus.botClientCommandRejected == 1 &&
		q3aSmokeStatus.botClientCommandFailures == 0;
	Q3A_BotLibImport_SetBotClientCommandMessage(
		q3aSmokeStatus.botClientCommandSmokePassed ? "Q3A BotClientCommand bridge passed" :
													  "Q3A BotClientCommand bridge failed");
	return q3aSmokeStatus.botClientCommandSmokePassed;
}

void Q3A_BotLibImport_SetMilliseconds(int milliseconds) {
	if (!q3aSmokeStatus.initialized) {
		Q3A_BotLibImport_Init();
	}
	if (milliseconds < 0) {
		milliseconds = 0;
	}
	q3aSmokeStatus.runtimeMilliseconds = milliseconds;
}

int Q3A_BotLibImport_StartFrame(int milliseconds) {
	float time;
	int result;

	Q3A_BotLibImport_SetMilliseconds(milliseconds);
	if (!q3aSmokeStatus.aasLoaded || !aasworld.loaded) {
		q3aSmokeStatus.aasStartFrameMessage = "Q3A AAS start frame skipped: AAS is not loaded";
		return qfalse;
	}

	q3aSmokeStatus.aasStartFrameAttempted = qtrue;
	q3aSmokeStatus.aasStartFramePassed = qfalse;
	q3aSmokeStatus.aasStartFrameTimeMilliseconds = q3aSmokeStatus.runtimeMilliseconds;
	time = (float)q3aSmokeStatus.runtimeMilliseconds * 0.001f;
	result = AAS_StartFrame(time);
	q3aSmokeStatus.aasStartFrameResult = result;
	q3aSmokeStatus.aasStartFrameCount = aasworld.numframes;

	if (result != BLERR_NOERROR) {
		snprintf(
			q3aAasStartFrameMessage,
			sizeof(q3aAasStartFrameMessage),
			"Q3A AAS start frame failed: result=%d time_ms=%d frames=%d",
			result,
			q3aSmokeStatus.aasStartFrameTimeMilliseconds,
			q3aSmokeStatus.aasStartFrameCount);
		q3aSmokeStatus.aasStartFrameMessage = q3aAasStartFrameMessage;
		return qfalse;
	}

	q3aSmokeStatus.aasStartFramePassed = qtrue;
	snprintf(
		q3aAasStartFrameMessage,
		sizeof(q3aAasStartFrameMessage),
		"Q3A AAS start frame passed: result=%d time_ms=%d frames=%d",
		result,
		q3aSmokeStatus.aasStartFrameTimeMilliseconds,
		q3aSmokeStatus.aasStartFrameCount);
	q3aSmokeStatus.aasStartFrameMessage = q3aAasStartFrameMessage;
	return qtrue;
}

int Q3A_BotLibImport_BeginEntitySync(int entityCount) {
	if (!q3aSmokeStatus.initialized) {
		Q3A_BotLibImport_Init();
	}

	q3aSmokeStatus.entitySyncAttempted = qtrue;
	q3aSmokeStatus.entitySyncPassed = qfalse;
	q3aSmokeStatus.entitySyncUpdated = 0;
	q3aSmokeStatus.entitySyncUnlinked = 0;
	q3aSmokeStatus.entitySyncSkipped = 0;
	q3aSmokeStatus.entitySyncFailures = 0;
	q3aSmokeStatus.entitySyncMaxEntities = aasworld.maxentities;
	Q3A_BotLibImport_ResetEntityTraceStatus("Q3A AAS entity trace has not run");

	if (entityCount < 0) {
		entityCount = 0;
	}

	if (!q3aSmokeStatus.aasLoaded || !aasworld.loaded || aasworld.entities == NULL) {
		q3aSmokeStatus.entitySyncSkipped = entityCount;
		Q3A_BotLibImport_SetEntitySyncMessage("Q3A AAS entity sync skipped: AAS is not loaded");
		return qfalse;
	}

	Q3A_BotLibImport_SetEntitySyncMessage("Q3A AAS entity sync started");
	return qtrue;
}

int Q3A_BotLibImport_UpdateEntity(int entnum, const Q3ABotLibImportEntityState *state) {
	bot_entitystate_t q3aState;
	int result;

	if (!q3aSmokeStatus.entitySyncAttempted) {
		Q3A_BotLibImport_BeginEntitySync(entnum + 1);
	}

	if (!q3aSmokeStatus.aasLoaded || !aasworld.loaded || aasworld.entities == NULL) {
		q3aSmokeStatus.entitySyncSkipped++;
		Q3A_BotLibImport_SetEntitySyncMessage("Q3A AAS entity sync skipped: AAS is not loaded");
		return qfalse;
	}

	if (entnum < 0 || entnum >= aasworld.maxentities) {
		q3aSmokeStatus.entitySyncSkipped++;
		Q3A_BotLibImport_SetEntitySyncMessage("Q3A AAS entity sync skipped: entity outside maxentities");
		return qfalse;
	}

	if (state == NULL) {
		result = AAS_UpdateEntity(entnum, NULL);
		if (result == BLERR_NOERROR) {
			q3aSmokeStatus.entitySyncUnlinked++;
			return qtrue;
		}
		q3aSmokeStatus.entitySyncFailures++;
		Q3A_BotLibImport_SetEntitySyncMessage("Q3A AAS entity sync failed");
		return qfalse;
	}

	Com_Memset(&q3aState, 0, sizeof(q3aState));
	q3aState.type = state->type;
	q3aState.flags = state->flags;
	VectorCopy(state->origin, q3aState.origin);
	VectorCopy(state->angles, q3aState.angles);
	VectorCopy(state->oldOrigin, q3aState.old_origin);
	VectorCopy(state->mins, q3aState.mins);
	VectorCopy(state->maxs, q3aState.maxs);
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

	result = AAS_UpdateEntity(entnum, &q3aState);
	if (result == BLERR_NOERROR) {
		q3aSmokeStatus.entitySyncUpdated++;
		return qtrue;
	}

	q3aSmokeStatus.entitySyncFailures++;
	Q3A_BotLibImport_SetEntitySyncMessage("Q3A AAS entity sync failed");
	return qfalse;
}

void Q3A_BotLibImport_FinishEntitySync(void) {
	if (!q3aSmokeStatus.entitySyncAttempted) {
		return;
	}

	q3aSmokeStatus.entitySyncPassed = q3aSmokeStatus.entitySyncFailures == 0;
	Q3A_BotLibImport_SetEntitySyncMessage(
		q3aSmokeStatus.entitySyncPassed ? "Q3A AAS entity sync passed" : "Q3A AAS entity sync failed");
	Q3A_BotLibImport_RunEntityTraceSmoke();
	Q3A_BotLibImport_RunBspLeafLinkSmoke();
}

int Q3A_BotLibImport_LoadAASBuffer(const char *name, const void *data, int length, int bspChecksum) {
	char checksum[32];
	const char *loadName;
	int result;

	if (!q3aSmokeStatus.initialized) {
		Q3A_BotLibImport_Init();
	}

	Q3A_BotLibImport_UnloadAAS();
	q3aSmokeStatus.lifecycleLoadAttempts++;
	Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle load started");

	q3aSmokeStatus.aasLoadAttempted = qtrue;
	q3aSmokeStatus.aasLoaded = qfalse;
	q3aSmokeStatus.aasLoadResult = BLERR_NOAASFILE;
	q3aSmokeStatus.aasBspChecksum = bspChecksum;
	q3aSmokeStatus.aasAreas = 0;
	q3aSmokeStatus.aasReachability = 0;
	q3aSmokeStatus.aasClusters = 0;
	Q3A_BotLibImport_ResetAASSampleStatus("Q3A AAS area sample has not run");
	Q3A_BotLibImport_ResetAASClusterStatus("Q3A AAS clustering has not run");
	Q3A_BotLibImport_ResetAASRouteStatus("Q3A AAS route query has not run");
	Q3A_BotLibImport_ResetAASAltRouteStatus("Q3A AAS alternative route query has not run");
	Q3A_BotLibImport_ResetAASMovementStatus("Q3A AAS movement prediction has not run");
	Q3A_BotLibImport_ResetBotClientCommandStatus("Q3A BotClientCommand bridge has not run");
	Q3A_BotLibImport_ResetAASStartFrameStatus("Q3A AAS start frame has not run");
	Q3A_BotLibImport_ResetEntitySyncStatus("Q3A AAS entity sync has not run");
	Q3A_BotLibImport_ResetEntityTraceStatus("Q3A AAS entity trace has not run");
	Q3A_BotLibImport_ResetDebugDrawStatus("Q3A debug draw bridge has not run");
	Q3A_BotLibImport_ResetDebugPolygonStatus("Q3A debug polygon bridge has not run");
	Q3A_BotLibImport_ResetDebugAreaStatus("Q3A AAS debug area helpers have not run");
	Q3A_BotLibImport_ResetRouteOverlayStatus("Q3A route overlay has not run");
	Q3A_BotLibImport_ResetBspLeafLinkStatus("Q3A BSP leaf entity links have not run");
	Q3A_BotLibFSCloseAllFiles();
	Q3A_BotLibImport_ResetFilesystemStatus("Q3A BotLib filesystem bridge started");

	if (data == NULL || length <= 0) {
		q3aSmokeStatus.aasMessage = "Q3A AAS load failed: empty buffer";
		Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle load failed: empty buffer");
		Q3A_BotLibImport_FinalizeFilesystemStatus("Q3A BotLib filesystem bridge passed");
		return qfalse;
	}

	Q3A_BotLibImport_SetDefaultAASMovementLibVars();
	result = AAS_Setup();
	if (result != BLERR_NOERROR) {
		q3aSmokeStatus.aasLoadResult = result;
		q3aSmokeStatus.aasMessage = "Q3A AAS setup failed";
		Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle load failed: AAS setup failed");
		AAS_Shutdown();
		Q3A_BotLibReleaseHunkAllocations("Q3A BotLib hunk allocations released after failed AAS setup");
		return qfalse;
	}

	loadName = (name != NULL && name[0] != '\0') ? name : "worr-memory.aas";
	snprintf(checksum, sizeof(checksum), "%d", bspChecksum);
	LibVarSet("sv_mapChecksum", checksum);

	q3aMemoryFile.name = loadName;
	q3aMemoryFile.data = (const unsigned char *)data;
	q3aMemoryFile.length = length;

	result = AAS_LoadAASFile((char *)loadName);

	q3aMemoryFile.name = NULL;
	q3aMemoryFile.data = NULL;
	q3aMemoryFile.length = 0;

	q3aSmokeStatus.aasLoadResult = result;
	if (result != BLERR_NOERROR || !aasworld.loaded) {
		if (q3aSmokeStatus.aasMessage == NULL || q3aSmokeStatus.aasMessage[0] == '\0' ||
			Q_stricmp(q3aSmokeStatus.aasMessage, "Q3A AAS file loader has not run") == 0) {
			q3aSmokeStatus.aasMessage = "Q3A AAS file load failed";
		}
		Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle load failed: AAS file load failed");
		AAS_Shutdown();
		Q3A_BotLibReleaseHunkAllocations("Q3A BotLib hunk allocations released after failed AAS load");
		Q3A_BotLibImport_FinalizeFilesystemStatus("Q3A BotLib filesystem bridge passed");
		return qfalse;
	}

	q3aSmokeStatus.aasLoaded = qtrue;
	q3aSmokeStatus.aasAreas = aasworld.numareas;
	q3aSmokeStatus.aasReachability = aasworld.reachabilitysize;
	q3aSmokeStatus.aasClusters = aasworld.numclusters;
	q3aSmokeStatus.aasMessage = "Q3A AAS file load passed";
	AAS_InitAASLinkHeap();
	AAS_InitAASLinkedEntities();
	AAS_InitRouting();
	AAS_InitAlternativeRouting();
	AAS_SetInitialized();
	if (!Q3A_BotLibImport_RunAASSampleSmoke()) {
		q3aSmokeStatus.aasLoaded = qfalse;
		Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle load failed: sample smoke failed");
		AAS_Shutdown();
		Q3A_BotLibReleaseHunkAllocations("Q3A BotLib hunk allocations released after failed AAS sample smoke");
		Q3A_BotLibImport_FinalizeFilesystemStatus("Q3A BotLib filesystem bridge passed");
		return qfalse;
	}
	if (!Q3A_BotLibImport_RunAASClusterSmoke()) {
		q3aSmokeStatus.aasLoaded = qfalse;
		Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle load failed: cluster smoke failed");
		AAS_Shutdown();
		Q3A_BotLibReleaseHunkAllocations("Q3A BotLib hunk allocations released after failed AAS cluster smoke");
		Q3A_BotLibImport_FinalizeFilesystemStatus("Q3A BotLib filesystem bridge passed");
		return qfalse;
	}
	if (!Q3A_BotLibImport_RunAASRouteSmoke()) {
		q3aSmokeStatus.aasLoaded = qfalse;
		Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle load failed: route smoke failed");
		AAS_Shutdown();
		Q3A_BotLibReleaseHunkAllocations("Q3A BotLib hunk allocations released after failed AAS route smoke");
		Q3A_BotLibImport_FinalizeFilesystemStatus("Q3A BotLib filesystem bridge passed");
		return qfalse;
	}
	if (!Q3A_BotLibImport_RunAASAltRouteSmoke()) {
		q3aSmokeStatus.aasLoaded = qfalse;
		Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle load failed: alternative route smoke failed");
		AAS_Shutdown();
		Q3A_BotLibReleaseHunkAllocations("Q3A BotLib hunk allocations released after failed AAS alternative route smoke");
		Q3A_BotLibImport_FinalizeFilesystemStatus("Q3A BotLib filesystem bridge passed");
		return qfalse;
	}
	if (!Q3A_BotLibImport_RunAASMovementSmoke()) {
		q3aSmokeStatus.aasLoaded = qfalse;
		Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle load failed: movement smoke failed");
		AAS_Shutdown();
		Q3A_BotLibReleaseHunkAllocations("Q3A BotLib hunk allocations released after failed AAS movement smoke");
		Q3A_BotLibImport_FinalizeFilesystemStatus("Q3A BotLib filesystem bridge passed");
		return qfalse;
	}
	q3aSmokeStatus.lifecycleLoadSuccesses++;
	Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle load passed");
	Q3A_BotLibImport_FinalizeFilesystemStatus("Q3A BotLib filesystem bridge passed");
	return qtrue;
}

void Q3A_BotLibImport_UnloadAAS(void) {
	const int openFileCountBefore = Q3A_BotLibFSOpenFileCount();
	const qboolean hadActiveAASState = Q3A_BotLibImport_HasActiveAASState(openFileCountBefore);

	if (hadActiveAASState) {
		q3aSmokeStatus.lifecycleActiveUnloads++;
	}

	if (aasworld.loaded || aasworld.entities != NULL || aasworld.linkheap != NULL || aasworld.arealinkedentities != NULL) {
		AAS_Shutdown();
	} else {
		AAS_DumpAASData();
	}
	Q3A_BotLibFSCloseAllFiles();
	Q3A_BotLibReleaseHunkAllocations("Q3A BotLib hunk allocations released after AAS unload");
	q3aMemoryFile.name = NULL;
	q3aMemoryFile.data = NULL;
	q3aMemoryFile.length = 0;
	q3aSmokeStatus.aasLoaded = qfalse;
	q3aSmokeStatus.lifecycleLastUnloadHunkActiveBytes = q3aSmokeStatus.memoryHunkActiveBytes;
	q3aSmokeStatus.lifecycleLastUnloadOpenFiles = Q3A_BotLibFSOpenFileCount();
	if (Q3A_BotLibImport_AASStateIsClean(q3aSmokeStatus.lifecycleLastUnloadOpenFiles)) {
		q3aSmokeStatus.lifecyclePersistentZoneBytes = q3aSmokeStatus.memoryZoneActiveBytes;
	}
	q3aSmokeStatus.lifecycleLastUnloadZoneActiveBytes = Q3A_BotLibImport_TransientZoneBytes();
	if (hadActiveAASState) {
		if (Q3A_BotLibImport_AASStateIsClean(q3aSmokeStatus.lifecycleLastUnloadOpenFiles)) {
			q3aSmokeStatus.lifecycleCleanUnloads++;
			Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle unload clean");
		} else {
			q3aSmokeStatus.lifecycleUnloadFailures++;
			Q3A_BotLibImport_SetLifecycleMessage(
				"Q3A BotLib lifecycle unload left active state: zone=%d hunk=%d files=%d",
				q3aSmokeStatus.lifecycleLastUnloadZoneActiveBytes,
				q3aSmokeStatus.lifecycleLastUnloadHunkActiveBytes,
				q3aSmokeStatus.lifecycleLastUnloadOpenFiles);
		}
	} else {
		Q3A_BotLibImport_SetLifecycleMessage("Q3A BotLib lifecycle unload skipped: no active AAS state");
	}
	q3aSmokeStatus.aasAreas = 0;
	q3aSmokeStatus.aasReachability = 0;
	q3aSmokeStatus.aasClusters = 0;
	Q3A_BotLibImport_ResetAASSampleStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A AAS area sample unloaded" : "Q3A AAS area sample has not run");
	Q3A_BotLibImport_ResetAASClusterStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A AAS clustering unloaded" : "Q3A AAS clustering has not run");
	Q3A_BotLibImport_ResetAASRouteStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A AAS route query unloaded" : "Q3A AAS route query has not run");
	Q3A_BotLibImport_ResetAASAltRouteStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A AAS alternative route query unloaded" :
											"Q3A AAS alternative route query has not run");
	Q3A_BotLibImport_ResetAASMovementStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A AAS movement prediction unloaded" : "Q3A AAS movement prediction has not run");
	Q3A_BotLibImport_ResetBotClientCommandStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A BotClientCommand bridge unloaded" : "Q3A BotClientCommand bridge has not run");
	Q3A_BotLibImport_ResetAASStartFrameStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A AAS start frame unloaded" : "Q3A AAS start frame has not run");
	Q3A_BotLibImport_ResetEntityTraceStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A AAS entity trace unloaded" : "Q3A AAS entity trace has not run");
	Q3A_BotLibImport_ResetDebugDrawStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A debug draw bridge unloaded" : "Q3A debug draw bridge has not run");
	Q3A_BotLibImport_ResetDebugPolygonStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A debug polygon bridge unloaded" : "Q3A debug polygon bridge has not run");
	Q3A_BotLibImport_ResetDebugAreaStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A AAS debug area helpers unloaded" : "Q3A AAS debug area helpers have not run");
	Q3A_BotLibImport_ResetRouteOverlayStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A route overlay unloaded" : "Q3A route overlay has not run");
	Q3A_BotLibImport_ResetBspLeafLinkStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A BSP leaf entity links unloaded" : "Q3A BSP leaf entity links have not run");
	if (q3aSmokeStatus.aasLoadAttempted) {
		q3aSmokeStatus.aasMessage = "Q3A AAS file loader unloaded";
		Q3A_BotLibImport_SetFilesystemMessage("Q3A BotLib filesystem bridge unloaded");
	}
}

const Q3ABotLibImportSmokeStatus *Q3A_BotLibImport_SmokeStatus(void) {
	return &q3aSmokeStatus;
}
