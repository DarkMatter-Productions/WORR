// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "q3a_botlib_import.h"

#include "game/q_shared.h"
#include "game/botlib.h"
#include "game/be_aas.h"
#include "botlib/aasfile.h"
#include "botlib/be_aas_def.h"
#include "botlib/be_aas_bsp.h"
#include "botlib/be_aas_debug.h"
#include "botlib/be_aas_move.h"
#include "botlib/be_aas_reach.h"
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
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

botlib_import_t botimport;
aas_t aasworld;
aas_settings_t aassettings;
int bot_developer = 0;

typedef struct Q3AMemoryFile {
	const char *name;
	const unsigned char *data;
	int length;
	int offset;
	int open;
} Q3AMemoryFile;

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
static char q3aPrintMessage[512];
static char q3aAasMessage[512];
static char q3aAasSampleMessage[512];
static char q3aBspEntityMessage[512];
static char q3aBspModelMessage[512];
static char q3aBspCollisionMessage[512];
static char q3aBspVisibilityMessage[512];

static Q3ABotLibImportSmokeStatus q3aSmokeStatus = {
	.initialized = qfalse,
	.libvarSmokePassed = qfalse,
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
	.bspVisibilityLoadAttempted = qfalse,
	.bspVisibilityLoaded = qfalse,
	.bspVisibilityClusters = 0,
	.bspVisibilityPvsSmokePassed = qfalse,
	.bspVisibilityPhsSmokePassed = qfalse,
	.availableMemory = 0,
	.message = "Q3A BotLib import callbacks are not initialized",
	.aasMessage = "Q3A AAS file loader has not run",
	.aasSampleMessage = "Q3A AAS area sample has not run",
	.angleVectorsMessage = "Q3A AngleVectors smoke has not run",
	.bspEntityMessage = "Q3A BSP entity data has not run",
	.bspModelMessage = "Q3A BSP model data has not run",
	.bspCollisionMessage = "Q3A BSP collision data has not run",
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

static void QDECL Q3A_BotLibPrint(int type, char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(q3aPrintMessage, sizeof(q3aPrintMessage), fmt, args);
	va_end(args);

	if (type >= PRT_ERROR) {
		q3aSmokeStatus.message = q3aPrintMessage;
	}
}

static void *Q3A_BotLibGetMemory(int size) {
	if (size <= 0) {
		size = 1;
	}
	return malloc((size_t)size);
}

static void Q3A_BotLibFreeMemory(void *ptr) {
	free(ptr);
}

static int Q3A_BotLibAvailableMemory(void) {
	return 16 * 1024 * 1024;
}

static void *Q3A_BotLibHunkAlloc(int size) {
	return Q3A_BotLibGetMemory(size);
}

static int Q3A_BotLibFSOpenFile(const char *qpath, fileHandle_t *file, fsMode_t mode) {
	if (file != NULL) {
		*file = 0;
	}

	if (mode != FS_READ || q3aMemoryFile.data == NULL || q3aMemoryFile.length <= 0) {
		return -1;
	}

	if (q3aMemoryFile.name != NULL && qpath != NULL && Q_stricmp(q3aMemoryFile.name, qpath) != 0) {
		return -1;
	}

	q3aMemoryFile.offset = 0;
	q3aMemoryFile.open = qtrue;
	if (file != NULL) {
		*file = Q3A_MEMORY_FILE_HANDLE;
	}
	return q3aMemoryFile.length;
}

static int Q3A_BotLibFSRead(void *buffer, int len, fileHandle_t f) {
	int remaining;
	int count;

	if (f != Q3A_MEMORY_FILE_HANDLE || !q3aMemoryFile.open || buffer == NULL || len < 0) {
		return 0;
	}

	remaining = q3aMemoryFile.length - q3aMemoryFile.offset;
	if (remaining <= 0) {
		return 0;
	}

	count = len < remaining ? len : remaining;
	memcpy(buffer, q3aMemoryFile.data + q3aMemoryFile.offset, (size_t)count);
	q3aMemoryFile.offset += count;
	return count;
}

static int Q3A_BotLibFSWrite(const void *buffer, int len, fileHandle_t f) {
	(void)buffer;
	(void)len;
	(void)f;
	return 0;
}

static void Q3A_BotLibFSCloseFile(fileHandle_t f) {
	if (f == Q3A_MEMORY_FILE_HANDLE) {
		q3aMemoryFile.open = qfalse;
		q3aMemoryFile.offset = 0;
	}
}

static int Q3A_BotLibFSSeek(fileHandle_t f, long offset, int origin) {
	long base;
	long nextOffset;

	if (f != Q3A_MEMORY_FILE_HANDLE || !q3aMemoryFile.open) {
		return -1;
	}

	switch (origin) {
	case FS_SEEK_SET:
		base = 0;
		break;
	case FS_SEEK_CUR:
		base = q3aMemoryFile.offset;
		break;
	case FS_SEEK_END:
		base = q3aMemoryFile.length;
		break;
	default:
		return -1;
	}

	nextOffset = base + offset;
	if (nextOffset < 0 || nextOffset > q3aMemoryFile.length) {
		return -1;
	}

	q3aMemoryFile.offset = (int)nextOffset;
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

static void Q3A_BotLibInitDefaultAASSettings(void) {
	Com_Memset(&aassettings, 0, sizeof(aassettings));
	VectorSet(aassettings.phys_gravitydirection, 0.0f, 0.0f, -1.0f);
	aassettings.phys_friction = 6.0f;
	aassettings.phys_stopspeed = 100.0f;
	aassettings.phys_gravity = 800.0f;
	aassettings.phys_waterfriction = 1.0f;
	aassettings.phys_watergravity = 400.0f;
	aassettings.phys_maxvelocity = 320.0f;
	aassettings.phys_maxwalkvelocity = 300.0f;
	aassettings.phys_maxcrouchvelocity = 150.0f;
	aassettings.phys_maxswimvelocity = 150.0f;
	aassettings.phys_walkaccelerate = 10.0f;
	aassettings.phys_airaccelerate = 1.0f;
	aassettings.phys_swimaccelerate = 4.0f;
	aassettings.phys_maxstep = 18.0f;
	aassettings.phys_maxsteepness = 0.7f;
	aassettings.phys_maxwaterjump = 19.0f;
	aassettings.phys_maxbarrier = 32.0f;
	aassettings.phys_jumpvel = 270.0f;
	aassettings.phys_falldelta5 = 40.0f;
	aassettings.phys_falldelta10 = 60.0f;
	aassettings.rs_waterjump = 400.0f;
	aassettings.rs_teleport = 50.0f;
	aassettings.rs_barrierjump = 100.0f;
	aassettings.rs_startcrouch = 300.0f;
	aassettings.rs_startgrapple = 500.0f;
	aassettings.rs_startwalkoffledge = 70.0f;
	aassettings.rs_startjump = 300.0f;
	aassettings.rs_rocketjump = 500.0f;
	aassettings.rs_bfgjump = 500.0f;
	aassettings.rs_jumppad = 250.0f;
	aassettings.rs_aircontrolledjumppad = 300.0f;
	aassettings.rs_funcbob = 300.0f;
	aassettings.rs_startelevator = 50.0f;
	aassettings.rs_falldamage5 = 300.0f;
	aassettings.rs_falldamage10 = 500.0f;
	aassettings.rs_maxfallheight = 0.0f;
	aassettings.rs_maxjumpfallheight = 450.0f;
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
	(void)start;
	(void)boxmins;
	(void)boxmaxs;
	(void)contentmask;

	if (trace != NULL) {
		Com_Memset(trace, 0, sizeof(*trace));
		trace->fraction = 1.0f;
		trace->ent = entnum;
		VectorCopy(end, trace->endpos);
	}
	return qfalse;
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

static void Q3A_BotLibImport_FreeBspCollisionData(void) {
	free(q3aBspPlanes);
	free(q3aBspNodes);
	free(q3aBspLeafs);
	free(q3aBspLeafBrushes);
	free(q3aBspBrushSides);
	free(q3aBspBrushes);
	free(q3aBspBrushCheckCounts);
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

int AAS_ClientMovementHitBBox(
	aas_clientmove_t *move,
	int entnum,
	vec3_t origin,
	int presencetype,
	int onground,
	vec3_t velocity,
	vec3_t cmdmove,
	int cmdframes,
	int maxframes,
	float frametime,
	vec3_t mins,
	vec3_t maxs,
	int visualize) {
	(void)entnum;
	(void)presencetype;
	(void)onground;
	(void)velocity;
	(void)cmdmove;
	(void)cmdframes;
	(void)maxframes;
	(void)frametime;
	(void)mins;
	(void)maxs;
	(void)visualize;
	if (move != NULL) {
		Com_Memset(move, 0, sizeof(*move));
		if (origin != NULL) {
			VectorCopy(origin, move->endpos);
		}
		move->trace.fraction = 1.0f;
		move->endarea = origin != NULL ? AAS_PointAreaNum(origin) : 0;
	}
	return qfalse;
}

int AAS_PredictClientMovement(
	aas_clientmove_t *move,
	int entnum,
	vec3_t origin,
	int presencetype,
	int onground,
	vec3_t velocity,
	vec3_t cmdmove,
	int cmdframes,
	int maxframes,
	float frametime,
	int stopevent,
	int stopareanum,
	int visualize) {
	(void)entnum;
	(void)presencetype;
	(void)onground;
	(void)velocity;
	(void)cmdmove;
	(void)cmdframes;
	(void)maxframes;
	(void)frametime;
	(void)stopevent;
	(void)stopareanum;
	(void)visualize;
	if (move != NULL) {
		Com_Memset(move, 0, sizeof(*move));
		if (origin != NULL) {
			VectorCopy(origin, move->endpos);
		}
		move->trace.fraction = 1.0f;
		move->endarea = origin != NULL ? AAS_PointAreaNum(origin) : 0;
	}
	return qfalse;
}

int AAS_HorizontalVelocityForJump(float zvel, vec3_t start, vec3_t end, float *velocity) {
	(void)zvel;
	(void)start;
	(void)end;
	if (velocity != NULL) {
		*velocity = 0.0f;
	}
	return qfalse;
}

float AAS_RocketJumpZVelocity(vec3_t origin) {
	(void)origin;
	return 0.0f;
}

float AAS_BFGJumpZVelocity(vec3_t origin) {
	(void)origin;
	return 0.0f;
}

int AAS_DropToFloor(vec3_t origin, vec3_t mins, vec3_t maxs) {
	(void)origin;
	(void)mins;
	(void)maxs;
	return qfalse;
}

void AAS_PermanentLine(vec3_t start, vec3_t end, int color) {
	(void)start;
	(void)end;
	(void)color;
}

void AAS_DrawPermanentCross(vec3_t origin, float size, int color) {
	(void)origin;
	(void)size;
	(void)color;
}

void AAS_DrawArrow(vec3_t start, vec3_t end, int linecolor, int arrowcolor) {
	(void)start;
	(void)end;
	(void)linecolor;
	(void)arrowcolor;
}

int Sys_MilliSeconds(void) {
	return q3aSmokeStatus.runtimeMilliseconds;
}

void QDECL AAS_Error(char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(q3aAasMessage, sizeof(q3aAasMessage), fmt, args);
	va_end(args);

	q3aSmokeStatus.aasMessage = q3aAasMessage;
	if (botimport.Print != NULL) {
		botimport.Print(PRT_ERROR, "%s", q3aAasMessage);
	}
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

void Q3A_BotLibImport_Init(void) {
	Com_Memset(&botimport, 0, sizeof(botimport));
	Q3A_BotLibInitDefaultAASSettings();
	botimport.Print = Q3A_BotLibPrint;
	botimport.GetMemory = Q3A_BotLibGetMemory;
	botimport.FreeMemory = Q3A_BotLibFreeMemory;
	botimport.AvailableMemory = Q3A_BotLibAvailableMemory;
	botimport.HunkAlloc = Q3A_BotLibHunkAlloc;
	botimport.FS_FOpenFile = Q3A_BotLibFSOpenFile;
	botimport.FS_Read = Q3A_BotLibFSRead;
	botimport.FS_Write = Q3A_BotLibFSWrite;
	botimport.FS_FCloseFile = Q3A_BotLibFSCloseFile;
	botimport.FS_Seek = Q3A_BotLibFSSeek;

	q3aSmokeStatus.initialized = qtrue;
	q3aSmokeStatus.availableMemory = Q3A_BotLibAvailableMemory();
	q3aSmokeStatus.message = "Q3A BotLib import memory callbacks initialized";
	Q3A_BotLibImport_RunAngleVectorsSmoke();
	Q3A_BotLibImport_ResetAASSampleStatus("Q3A AAS area sample has not run");
}

void Q3A_BotLibImport_Shutdown(void) {
	Q3A_BotLibImport_UnloadAAS();
	Q3A_BotLibImport_ClearBspEntityData();
	Q3A_BotLibImport_ClearBspModelData();
	Q3A_BotLibImport_ClearBspCollisionData();
	Q3A_BotLibImport_ClearBspVisibilityData();
	LibVarDeAllocAll();
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
	q3aSmokeStatus.availableMemory = 0;
	q3aSmokeStatus.message = "Q3A BotLib import callbacks shut down";
	q3aSmokeStatus.aasMessage = "Q3A AAS file loader shut down";
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

void Q3A_BotLibImport_SetMilliseconds(int milliseconds) {
	if (!q3aSmokeStatus.initialized) {
		Q3A_BotLibImport_Init();
	}
	if (milliseconds < 0) {
		milliseconds = 0;
	}
	q3aSmokeStatus.runtimeMilliseconds = milliseconds;
}

int Q3A_BotLibImport_LoadAASBuffer(const char *name, const void *data, int length, int bspChecksum) {
	char checksum[32];
	const char *loadName;
	int result;

	if (!q3aSmokeStatus.initialized) {
		Q3A_BotLibImport_Init();
	}

	Q3A_BotLibImport_UnloadAAS();

	q3aSmokeStatus.aasLoadAttempted = qtrue;
	q3aSmokeStatus.aasLoaded = qfalse;
	q3aSmokeStatus.aasLoadResult = BLERR_NOAASFILE;
	q3aSmokeStatus.aasBspChecksum = bspChecksum;
	q3aSmokeStatus.aasAreas = 0;
	q3aSmokeStatus.aasReachability = 0;
	q3aSmokeStatus.aasClusters = 0;
	Q3A_BotLibImport_ResetAASSampleStatus("Q3A AAS area sample has not run");

	if (data == NULL || length <= 0) {
		q3aSmokeStatus.aasMessage = "Q3A AAS load failed: empty buffer";
		return qfalse;
	}

	loadName = (name != NULL && name[0] != '\0') ? name : "worr-memory.aas";
	snprintf(checksum, sizeof(checksum), "%d", bspChecksum);
	LibVarSet("sv_mapChecksum", checksum);

	q3aMemoryFile.name = loadName;
	q3aMemoryFile.data = (const unsigned char *)data;
	q3aMemoryFile.length = length;
	q3aMemoryFile.offset = 0;
	q3aMemoryFile.open = qfalse;

	result = AAS_LoadAASFile((char *)loadName);

	q3aMemoryFile.name = NULL;
	q3aMemoryFile.data = NULL;
	q3aMemoryFile.length = 0;
	q3aMemoryFile.offset = 0;
	q3aMemoryFile.open = qfalse;

	q3aSmokeStatus.aasLoadResult = result;
	if (result != BLERR_NOERROR || !aasworld.loaded) {
		if (q3aSmokeStatus.aasMessage == NULL || q3aSmokeStatus.aasMessage[0] == '\0' ||
			Q_stricmp(q3aSmokeStatus.aasMessage, "Q3A AAS file loader has not run") == 0) {
			q3aSmokeStatus.aasMessage = "Q3A AAS file load failed";
		}
		AAS_DumpAASData();
		return qfalse;
	}

	q3aSmokeStatus.aasLoaded = qtrue;
	q3aSmokeStatus.aasAreas = aasworld.numareas;
	q3aSmokeStatus.aasReachability = aasworld.reachabilitysize;
	q3aSmokeStatus.aasClusters = aasworld.numclusters;
	q3aSmokeStatus.aasMessage = "Q3A AAS file load passed";
	if (!Q3A_BotLibImport_RunAASSampleSmoke()) {
		q3aSmokeStatus.aasLoaded = qfalse;
		AAS_DumpAASData();
		return qfalse;
	}
	return qtrue;
}

void Q3A_BotLibImport_UnloadAAS(void) {
	AAS_DumpAASData();
	q3aMemoryFile.name = NULL;
	q3aMemoryFile.data = NULL;
	q3aMemoryFile.length = 0;
	q3aMemoryFile.offset = 0;
	q3aMemoryFile.open = qfalse;
	q3aSmokeStatus.aasLoaded = qfalse;
	q3aSmokeStatus.aasAreas = 0;
	q3aSmokeStatus.aasReachability = 0;
	q3aSmokeStatus.aasClusters = 0;
	Q3A_BotLibImport_ResetAASSampleStatus(
		q3aSmokeStatus.aasLoadAttempted ? "Q3A AAS area sample unloaded" : "Q3A AAS area sample has not run");
	if (q3aSmokeStatus.aasLoadAttempted) {
		q3aSmokeStatus.aasMessage = "Q3A AAS file loader unloaded";
	}
}

const Q3ABotLibImportSmokeStatus *Q3A_BotLibImport_SmokeStatus(void) {
	return &q3aSmokeStatus;
}
