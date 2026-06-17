/*
 * WORR-native Quake II BSP trace bridge for q2aas reachability generation.
 *
 * Task IDs: FR-04-T11, FR-04-T16, DV-07-T06
 */

#ifndef WORR_Q2AAS_Q2TRACE_H
#define WORR_Q2AAS_Q2TRACE_H

#include "qcommon/q_shared.h"

#ifndef BSPTRACE
typedef struct bsp_trace_s bsp_trace_t;
#endif
typedef struct quakefile_s quakefile_t;

qboolean WorrQ2AAS_BeginQ2Reach(quakefile_t *qf, int *checksum);
void WorrQ2AAS_EndQ2Reach(void);
qboolean WorrQ2AAS_Q2TraceActive(void);

char *WorrQ2AAS_EntityString(void);
int WorrQ2AAS_PointContents(vec3_t point);
void WorrQ2AAS_Trace(bsp_trace_t *trace, vec3_t start, vec3_t mins, vec3_t maxs,
		vec3_t end, int passent, int contentmask);
void WorrQ2AAS_BSPModelMinsMaxsOrigin(int modelnum, vec3_t angles,
		vec3_t outmins, vec3_t outmaxs, vec3_t origin);

#endif
