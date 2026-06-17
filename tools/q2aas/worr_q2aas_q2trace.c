/*
 * WORR-native Quake II BSP trace bridge for q2aas reachability generation.
 *
 * Task IDs: FR-04-T11, FR-04-T16, DV-07-T06
 */

#include <math.h>
#include <string.h>

#include "qcommon/q_shared.h"
#include "botlib/botlib.h"
#include "l_qfiles.h"
#include "worr_q2aas_q2trace.h"

#include "q2files.h"
#include "l_bsp_q2.h"
#include "l_log.h"
#include "l_mem.h"

#define WORR_Q2AAS_DIST_EPSILON (1.0f / 32.0f)

unsigned Com_BlockChecksum(const void *buffer, int length);

static qboolean q2_trace_active;
static int q2_trace_checkcount = 1;
static int *q2_brush_checkcounts;
static int q2_brush_checkcount_size;

static bsp_trace_t *q2_trace;
static int q2_trace_contents;
static qboolean q2_trace_ispoint;
static vec3_t q2_trace_start;
static vec3_t q2_trace_end;
static vec3_t q2_trace_extents;
static vec3_t q2_trace_offsets[8];

static float WorrQ2AAS_ClampFloat(float value, float minvalue, float maxvalue)
{
	if (value < minvalue) return minvalue;
	if (value > maxvalue) return maxvalue;
	return value;
}

static void WorrQ2AAS_LerpVector(vec3_t from, vec3_t to, float frac, vec3_t out)
{
	out[0] = from[0] + (to[0] - from[0]) * frac;
	out[1] = from[1] + (to[1] - from[1]) * frac;
	out[2] = from[2] + (to[2] - from[2]) * frac;
}

static int WorrQ2AAS_PlaneSignbits(const vec3_t normal)
{
	int bits = 0;

	if (normal[0] < 0) bits |= 1;
	if (normal[1] < 0) bits |= 2;
	if (normal[2] < 0) bits |= 4;
	return bits;
}

static int WorrQ2AAS_PlaneType(const dplane_t *plane)
{
	if (plane->type >= PLANE_X && plane->type <= PLANE_Z)
		return plane->type;
	return PLANE_NON_AXIAL;
}

static void WorrQ2AAS_FillCPlane(cplane_t *out, const dplane_t *in)
{
	VectorCopy(in->normal, out->normal);
	out->dist = in->dist;
	out->type = WorrQ2AAS_PlaneType(in);
	out->signbits = WorrQ2AAS_PlaneSignbits(in->normal);
	out->pad[0] = 0;
	out->pad[1] = 0;
}

static int WorrQ2AAS_WorldHeadNode(void)
{
	if (nummodels > 0 && dmodels && dmodels[0].headnode >= 0 &&
			dmodels[0].headnode < numnodes)
	{
		return dmodels[0].headnode;
	}
	return 0;
}

static qboolean WorrQ2AAS_EnsureBrushCheckcounts(void)
{
	if (numbrushes <= 0) return qfalse;
	if (q2_brush_checkcounts && q2_brush_checkcount_size >= numbrushes)
		return qtrue;

	if (q2_brush_checkcounts)
	{
		FreeMemory(q2_brush_checkcounts);
		q2_brush_checkcounts = NULL;
		q2_brush_checkcount_size = 0;
	}

	q2_brush_checkcounts = GetClearedMemory(numbrushes * sizeof(*q2_brush_checkcounts));
	q2_brush_checkcount_size = numbrushes;
	q2_trace_checkcount = 1;
	return q2_brush_checkcounts != NULL;
}

static void WorrQ2AAS_NextCheckcount(void)
{
	q2_trace_checkcount++;
	if (q2_trace_checkcount <= 0)
	{
		memset(q2_brush_checkcounts, 0,
				q2_brush_checkcount_size * sizeof(*q2_brush_checkcounts));
		q2_trace_checkcount = 1;
	}
}

static void WorrQ2AAS_ClearTrace(bsp_trace_t *trace, vec3_t start)
{
	memset(trace, 0, sizeof(*trace));
	trace->fraction = 1.0f;
	VectorCopy(start, trace->endpos);
	trace->ent = 0;
}

static void WorrQ2AAS_SetSurface(bsp_trace_t *trace, const dbrushside_t *side)
{
	int texinfo_index = side->texinfo;

	trace->sidenum = (int)(side - dbrushsides);
	trace->surface.name[0] = '\0';
	trace->surface.flags = 0;
	trace->surface.value = 0;

	if (texinfo_index < 0 || texinfo_index >= numtexinfo)
		return;

	strncpy(trace->surface.name, texinfo[texinfo_index].texture,
			sizeof(trace->surface.name) - 1);
	trace->surface.name[sizeof(trace->surface.name) - 1] = '\0';
	trace->surface.flags = texinfo[texinfo_index].flags;
	trace->surface.value = texinfo[texinfo_index].value;
}

static int WorrQ2AAS_PointLeafNum(vec3_t point)
{
	int nodenum = WorrQ2AAS_WorldHeadNode();

	while (nodenum >= 0)
	{
		dnode_t *node;
		dplane_t *plane;
		float dist;

		if (nodenum >= numnodes)
			return 0;

		node = &dnodes[nodenum];
		if (node->planenum < 0 || node->planenum >= numplanes)
			return 0;

		plane = &dplanes[node->planenum];
		dist = DotProduct(point, plane->normal) - plane->dist;
		nodenum = node->children[dist < 0];
	}

	return -1 - nodenum;
}

static void WorrQ2AAS_ClipBoxToBrush(vec3_t start, vec3_t end,
		bsp_trace_t *trace, dbrush_t *brush)
{
	int i;
	float enterfrac = -1.0f;
	float leavefrac = 1.0f;
	float d1, d2, dist, f;
	qboolean getout = qfalse;
	qboolean startout = qfalse;
	const dplane_t *clipplane = NULL;
	const dbrushside_t *leadside = NULL;

	if (!brush->numsides)
		return;

	for (i = 0; i < brush->numsides; i++)
	{
		dbrushside_t *side;
		dplane_t *plane;
		int signbits;

		side = &dbrushsides[brush->firstside + i];
		if (side->planenum < 0 || side->planenum >= numplanes)
			continue;

		plane = &dplanes[side->planenum];
		signbits = WorrQ2AAS_PlaneSignbits(plane->normal);

		if (!q2_trace_ispoint)
			dist = plane->dist - DotProduct(q2_trace_offsets[signbits], plane->normal);
		else
			dist = plane->dist;

		d1 = DotProduct(start, plane->normal) - dist;
		d2 = DotProduct(end, plane->normal) - dist;

		if (d2 > 0) getout = qtrue;
		if (d1 > 0) startout = qtrue;

		if (d1 > 0 && (d2 >= WORR_Q2AAS_DIST_EPSILON || d2 >= d1))
			return;

		if (d1 <= 0 && d2 <= 0)
			continue;

		if (d1 > d2)
		{
			f = (d1 - WORR_Q2AAS_DIST_EPSILON) / (d1 - d2);
			if (f < 0) f = 0;
			if (f > enterfrac)
			{
				enterfrac = f;
				clipplane = plane;
				leadside = side;
			}
		}
		else
		{
			f = (d1 + WORR_Q2AAS_DIST_EPSILON) / (d1 - d2);
			if (f > 1) f = 1;
			if (f < leavefrac)
				leavefrac = f;
		}
	}

	if (!startout)
	{
		trace->startsolid = qtrue;
		if (!getout)
		{
			trace->allsolid = qtrue;
			trace->fraction = 0;
			trace->contents = brush->contents;
		}
		return;
	}

	if (enterfrac < leavefrac && enterfrac > -1 && enterfrac < trace->fraction)
	{
		trace->fraction = enterfrac;
		if (clipplane)
			WorrQ2AAS_FillCPlane(&trace->plane, clipplane);
		if (leadside)
			WorrQ2AAS_SetSurface(trace, leadside);
		trace->contents = brush->contents;
	}
}

static void WorrQ2AAS_TestBoxInBrush(vec3_t point, bsp_trace_t *trace,
		dbrush_t *brush)
{
	int i;

	if (!brush->numsides)
		return;

	for (i = 0; i < brush->numsides; i++)
	{
		dbrushside_t *side;
		dplane_t *plane;
		int signbits;
		float dist;
		float d1;

		side = &dbrushsides[brush->firstside + i];
		if (side->planenum < 0 || side->planenum >= numplanes)
			continue;

		plane = &dplanes[side->planenum];
		signbits = WorrQ2AAS_PlaneSignbits(plane->normal);
		dist = plane->dist - DotProduct(q2_trace_offsets[signbits], plane->normal);
		d1 = DotProduct(point, plane->normal) - dist;

		if (d1 > 0)
			return;
	}

	trace->startsolid = qtrue;
	trace->allsolid = qtrue;
	trace->fraction = 0;
	trace->contents = brush->contents;
}

static void WorrQ2AAS_TraceToLeaf(int leafnum)
{
	int k;
	dleaf_t *leaf;

	if (leafnum < 0 || leafnum >= numleafs)
		return;

	leaf = &dleafs[leafnum];
	if (!(leaf->contents & q2_trace_contents))
		return;

	for (k = 0; k < leaf->numleafbrushes; k++)
	{
		int brushnum;
		dbrush_t *brush;

		if (leaf->firstleafbrush + k < 0 ||
				leaf->firstleafbrush + k >= numleafbrushes)
			continue;

		brushnum = dleafbrushes[leaf->firstleafbrush + k];
		if (brushnum < 0 || brushnum >= numbrushes)
			continue;

		if (q2_brush_checkcounts[brushnum] == q2_trace_checkcount)
			continue;
		q2_brush_checkcounts[brushnum] = q2_trace_checkcount;

		brush = &dbrushes[brushnum];
		if (!(brush->contents & q2_trace_contents))
			continue;

		WorrQ2AAS_ClipBoxToBrush(q2_trace_start, q2_trace_end, q2_trace, brush);
		if (!q2_trace->fraction)
			return;
	}
}

static void WorrQ2AAS_TestInLeaf(int leafnum)
{
	int k;
	dleaf_t *leaf;

	if (leafnum < 0 || leafnum >= numleafs)
		return;

	leaf = &dleafs[leafnum];
	if (!(leaf->contents & q2_trace_contents))
		return;

	for (k = 0; k < leaf->numleafbrushes; k++)
	{
		int brushnum;
		dbrush_t *brush;

		if (leaf->firstleafbrush + k < 0 ||
				leaf->firstleafbrush + k >= numleafbrushes)
			continue;

		brushnum = dleafbrushes[leaf->firstleafbrush + k];
		if (brushnum < 0 || brushnum >= numbrushes)
			continue;

		if (q2_brush_checkcounts[brushnum] == q2_trace_checkcount)
			continue;
		q2_brush_checkcounts[brushnum] = q2_trace_checkcount;

		brush = &dbrushes[brushnum];
		if (!(brush->contents & q2_trace_contents))
			continue;

		WorrQ2AAS_TestBoxInBrush(q2_trace_start, q2_trace, brush);
		if (!q2_trace->fraction)
			return;
	}
}

static int WorrQ2AAS_BoxOnPlaneSide(vec3_t mins, vec3_t maxs, dplane_t *plane)
{
	float dist1 = 0;
	float dist2 = 0;
	int i;

	if (plane->type >= PLANE_X && plane->type <= PLANE_Z)
	{
		if (mins[plane->type] >= plane->dist) return 1;
		if (maxs[plane->type] < plane->dist) return 2;
		return 3;
	}

	for (i = 0; i < 3; i++)
	{
		if (plane->normal[i] >= 0)
		{
			dist1 += plane->normal[i] * maxs[i];
			dist2 += plane->normal[i] * mins[i];
		}
		else
		{
			dist1 += plane->normal[i] * mins[i];
			dist2 += plane->normal[i] * maxs[i];
		}
	}

	if (dist2 >= plane->dist) return 1;
	if (dist1 < plane->dist) return 2;
	return 3;
}

static void WorrQ2AAS_BoxLeafs_r(int nodenum, vec3_t mins, vec3_t maxs)
{
	while (nodenum >= 0)
	{
		dnode_t *node;
		dplane_t *plane;
		int side;

		if (nodenum >= numnodes)
			return;

		node = &dnodes[nodenum];
		if (node->planenum < 0 || node->planenum >= numplanes)
			return;

		plane = &dplanes[node->planenum];
		side = WorrQ2AAS_BoxOnPlaneSide(mins, maxs, plane);

		if (side == 1)
		{
			nodenum = node->children[0];
			continue;
		}
		if (side == 2)
		{
			nodenum = node->children[1];
			continue;
		}

		WorrQ2AAS_BoxLeafs_r(node->children[0], mins, maxs);
		nodenum = node->children[1];
	}

	WorrQ2AAS_TestInLeaf(-1 - nodenum);
}

static void WorrQ2AAS_RecursiveHullCheck(int nodenum, float p1f, float p2f,
		vec3_t p1, vec3_t p2)
{
	dnode_t *node;
	dplane_t *plane;
	float t1, t2, offset;
	float frac, frac2;
	float idist;
	vec3_t mid;
	int side;
	float midf;

	if (q2_trace->fraction <= p1f)
		return;

	if (nodenum < 0)
	{
		WorrQ2AAS_TraceToLeaf(-1 - nodenum);
		return;
	}

	if (nodenum >= numnodes)
		return;

	node = &dnodes[nodenum];
	if (node->planenum < 0 || node->planenum >= numplanes)
		return;

	plane = &dplanes[node->planenum];

	if (plane->type >= PLANE_X && plane->type <= PLANE_Z)
	{
		t1 = p1[plane->type] - plane->dist;
		t2 = p2[plane->type] - plane->dist;
		offset = q2_trace_extents[plane->type];
	}
	else
	{
		t1 = DotProduct(p1, plane->normal) - plane->dist;
		t2 = DotProduct(p2, plane->normal) - plane->dist;
		if (q2_trace_ispoint)
			offset = 0;
		else
			offset = fabs(q2_trace_extents[0] * plane->normal[0]) +
					fabs(q2_trace_extents[1] * plane->normal[1]) +
					fabs(q2_trace_extents[2] * plane->normal[2]);
	}

	if (t1 >= offset && t2 >= offset)
	{
		WorrQ2AAS_RecursiveHullCheck(node->children[0], p1f, p2f, p1, p2);
		return;
	}
	if (t1 < -offset && t2 < -offset)
	{
		WorrQ2AAS_RecursiveHullCheck(node->children[1], p1f, p2f, p1, p2);
		return;
	}

	if (t1 < t2)
	{
		idist = 1.0f / (t1 - t2);
		side = 1;
		frac2 = (t1 + offset + WORR_Q2AAS_DIST_EPSILON) * idist;
		frac = (t1 - offset + WORR_Q2AAS_DIST_EPSILON) * idist;
	}
	else if (t1 > t2)
	{
		idist = 1.0f / (t1 - t2);
		side = 0;
		frac2 = (t1 - offset - WORR_Q2AAS_DIST_EPSILON) * idist;
		frac = (t1 + offset + WORR_Q2AAS_DIST_EPSILON) * idist;
	}
	else
	{
		side = 0;
		frac = 1;
		frac2 = 0;
	}

	frac = WorrQ2AAS_ClampFloat(frac, 0, 1);
	frac2 = WorrQ2AAS_ClampFloat(frac2, 0, 1);

	midf = p1f + (p2f - p1f) * frac;
	WorrQ2AAS_LerpVector(p1, p2, frac, mid);
	WorrQ2AAS_RecursiveHullCheck(node->children[side], p1f, midf, p1, mid);

	midf = p1f + (p2f - p1f) * frac2;
	WorrQ2AAS_LerpVector(p1, p2, frac2, mid);
	WorrQ2AAS_RecursiveHullCheck(node->children[side ^ 1], midf, p2f, mid, p2);
}

qboolean WorrQ2AAS_BeginQ2Reach(quakefile_t *qf, int *checksum)
{
	void *buffer = NULL;
	int length = 0;

	if (!dnodes || !dleafs || !dplanes || !dbrushes || !dbrushsides ||
			!dleafbrushes || !dmodels)
	{
		Log_Print("WORR Q2 trace adapter unavailable: Q2 BSP data is not loaded\n");
		q2_trace_active = qfalse;
		return qfalse;
	}
	if (!WorrQ2AAS_EnsureBrushCheckcounts())
	{
		Log_Print("WORR Q2 trace adapter unavailable: no Q2 brushes loaded\n");
		q2_trace_active = qfalse;
		return qfalse;
	}

	if (checksum)
	{
		*checksum = 0;
		if (qf)
		{
			length = LoadQuakeFile(qf, &buffer);
			if (buffer && length > 0)
			{
				*checksum = LittleLong(Com_BlockChecksum(buffer, length));
				FreeMemory(buffer);
			}
		}
	}

	q2_trace_active = qtrue;
	Log_Print("using WORR Q2 BSP trace adapter for reachability\n");
	return qtrue;
}

void WorrQ2AAS_EndQ2Reach(void)
{
	q2_trace_active = qfalse;
}

qboolean WorrQ2AAS_Q2TraceActive(void)
{
	return q2_trace_active;
}

char *WorrQ2AAS_EntityString(void)
{
	if (!dentdata) return "";
	return dentdata;
}

int WorrQ2AAS_PointContents(vec3_t point)
{
	int leafnum;

	if (!q2_trace_active)
		return 0;

	leafnum = WorrQ2AAS_PointLeafNum(point);
	if (leafnum < 0 || leafnum >= numleafs)
		return 0;

	return dleafs[leafnum].contents;
}

void WorrQ2AAS_Trace(bsp_trace_t *trace, vec3_t start, vec3_t mins, vec3_t maxs,
		vec3_t end, int passent, int contentmask)
{
	vec3_t trace_mins;
	vec3_t trace_maxs;
	const vec_t *bounds[2];
	int i, j;

	(void) passent;

	WorrQ2AAS_ClearTrace(trace, start);
	if (!q2_trace_active || !WorrQ2AAS_EnsureBrushCheckcounts())
		return;

	if (!mins) mins = vec3_origin;
	if (!maxs) maxs = vec3_origin;
	VectorCopy(mins, trace_mins);
	VectorCopy(maxs, trace_maxs);

	q2_trace = trace;
	q2_trace_contents = contentmask;
	VectorCopy(start, q2_trace_start);
	VectorCopy(end, q2_trace_end);

	bounds[0] = trace_mins;
	bounds[1] = trace_maxs;
	for (i = 0; i < 8; i++)
		for (j = 0; j < 3; j++)
			q2_trace_offsets[i][j] = bounds[(i >> j) & 1][j];

	WorrQ2AAS_NextCheckcount();

	if (VectorCompare(start, end))
	{
		vec3_t c1, c2;

		for (i = 0; i < 3; i++)
		{
			c1[i] = start[i] + trace_mins[i] - 1;
			c2[i] = start[i] + trace_maxs[i] + 1;
		}

		WorrQ2AAS_BoxLeafs_r(WorrQ2AAS_WorldHeadNode(), c1, c2);
		VectorCopy(start, trace->endpos);
		return;
	}

	if (VectorCompare(trace_mins, vec3_origin) &&
			VectorCompare(trace_maxs, vec3_origin))
	{
		q2_trace_ispoint = qtrue;
		VectorClear(q2_trace_extents);
	}
	else
	{
		q2_trace_ispoint = qfalse;
		q2_trace_extents[0] = fabs(trace_mins[0]) > fabs(trace_maxs[0]) ?
				fabs(trace_mins[0]) : fabs(trace_maxs[0]);
		q2_trace_extents[1] = fabs(trace_mins[1]) > fabs(trace_maxs[1]) ?
				fabs(trace_mins[1]) : fabs(trace_maxs[1]);
		q2_trace_extents[2] = fabs(trace_mins[2]) > fabs(trace_maxs[2]) ?
				fabs(trace_mins[2]) : fabs(trace_maxs[2]);
	}

	WorrQ2AAS_RecursiveHullCheck(WorrQ2AAS_WorldHeadNode(), 0, 1, start, end);

	if (trace->fraction == 1)
		VectorCopy(end, trace->endpos);
	else
		WorrQ2AAS_LerpVector(start, end, trace->fraction, trace->endpos);
}

void WorrQ2AAS_BSPModelMinsMaxsOrigin(int modelnum, vec3_t angles,
		vec3_t outmins, vec3_t outmaxs, vec3_t origin)
{
	vec3_t mins;
	vec3_t maxs;
	float max;
	int i;

	if (modelnum < 0 || modelnum >= nummodels || !dmodels)
	{
		VectorClear(mins);
		VectorClear(maxs);
	}
	else
	{
		VectorCopy(dmodels[modelnum].mins, mins);
		VectorCopy(dmodels[modelnum].maxs, maxs);
	}

	if (angles[0] || angles[1] || angles[2])
	{
		max = RadiusFromBounds(mins, maxs);
		for (i = 0; i < 3; i++)
		{
			float center = (mins[i] + maxs[i]) * 0.5f;
			mins[i] = center - max;
			maxs[i] = center + max;
		}
	}

	if (outmins) VectorCopy(mins, outmins);
	if (outmaxs) VectorCopy(maxs, outmaxs);
	if (origin) VectorClear(origin);
}
