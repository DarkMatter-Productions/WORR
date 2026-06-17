// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Q3ABotLibImportSmokeStatus {
	int initialized;
	int libvarSmokePassed;
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
	int bspVisibilityLoadAttempted;
	int bspVisibilityLoaded;
	int bspVisibilityClusters;
	int bspVisibilityPvsSmokePassed;
	int bspVisibilityPhsSmokePassed;
	int availableMemory;
	const char *message;
	const char *aasMessage;
	const char *aasSampleMessage;
	const char *angleVectorsMessage;
	const char *bspEntityMessage;
	const char *bspModelMessage;
	const char *bspCollisionMessage;
	const char *bspVisibilityMessage;
} Q3ABotLibImportSmokeStatus;

void Q3A_BotLibImport_Init(void);
void Q3A_BotLibImport_Shutdown(void);
int Q3A_BotLibImport_RunLibVarSmoke(void);
int Q3A_BotLibImport_LoadAASBuffer(const char *name, const void *data, int length, int bspChecksum);
int Q3A_BotLibImport_LoadBspEntityData(const char *name, const void *data, int length);
int Q3A_BotLibImport_LoadBspModelData(const char *name, const void *data, int length);
int Q3A_BotLibImport_LoadBspCollisionData(const char *name, const void *data, int length);
int Q3A_BotLibImport_LoadBspVisibilityData(const char *name, const void *data, int length);
void Q3A_BotLibImport_SetMilliseconds(int milliseconds);
void Q3A_BotLibImport_ClearBspEntityData(void);
void Q3A_BotLibImport_ClearBspModelData(void);
void Q3A_BotLibImport_ClearBspCollisionData(void);
void Q3A_BotLibImport_ClearBspVisibilityData(void);
void Q3A_BotLibImport_UnloadAAS(void);
const Q3ABotLibImportSmokeStatus *Q3A_BotLibImport_SmokeStatus(void);

#ifdef __cplusplus
}
#endif
