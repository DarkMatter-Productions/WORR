/* FR-10-T10/T14 production collision-loader and SV_Clip parity probe. */

#include "server/rewind_collision.h"

#include "../../src/server/server.h"

#include "common/bsp.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/files.h"
#include "common/utils.h"
#include "common/zone.h"
#include "system/hunk.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "rewind_collision_real_bsp_test:%d: %s\n",     \
                    __LINE__, #expression);                                   \
            exit(EXIT_FAILURE);                                               \
        }                                                                     \
    } while (0)

#define FIXTURE_EPOCH UINT32_C(0x52425031)
#define FIXTURE_SOLID_SURFACE "fixture/solid"
#define FIXTURE_WATER_SURFACE "fixture/water"

server_static_t svs;
server_t sv;

static edict_t fixture_edicts[3];
static game_export_t fixture_game_export;
const game_export_t *ge = &fixture_game_export;

#if USE_DEBUG
static char developer_name[] = "developer";
static char developer_value[] = "0";
static cvar_t developer_cvar = {
    .name = developer_name,
    .string = developer_value,
    .integer = 0,
};
cvar_t *developer = &developer_cvar;
#endif

static char last_error[MAXERRORMSG];

typedef struct fixture_cvar_s {
    cvar_t cvar;
    char name[48];
    char value[48];
} fixture_cvar_t;

static fixture_cvar_t fixture_cvars[8];
static size_t fixture_cvar_count;

/*
 * The real BSP/cmodel/world/provider translation units are linked into this
 * probe.  These small adapters replace only unrelated engine services (VFS,
 * console, and zone ownership) so BSP_Load and CM_LoadMap consume the actual
 * on-disk fixture without booting a renderer, network socket, or game DLL.
 */
void Com_LPrintf(print_type_t type, const char *fmt, ...)
{
    va_list args;
    FILE *stream = type == PRINT_ERROR ? stderr : stdout;

    va_start(args, fmt);
    vfprintf(stream, fmt, args);
    va_end(args);
}

q_noreturn void Com_Error(error_type_t code, const char *fmt, ...)
{
    va_list args;

    (void)code;
    fprintf(stderr, "fatal: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

void Com_SetLastError(const char *message)
{
    Q_strlcpy(last_error, message ? message : "", sizeof(last_error));
}

const char *Com_GetLastError(void)
{
    return last_error;
}

void Com_PageInMemory(void *buffer, size_t size)
{
    volatile const byte *bytes = buffer;
    volatile byte sink = 0;

    for (size_t offset = 0; offset < size; offset += 4096)
        sink ^= bytes[offset];
    (void)sink;
}

void Cmd_AddCommand(const char *name, xcommand_t function)
{
    (void)name;
    (void)function;
}

int Cmd_Argc(void)
{
    return 0;
}

cvar_t *Cvar_Get(const char *name, const char *value, int flags)
{
    fixture_cvar_t *entry;

    for (size_t index = 0; index < fixture_cvar_count; ++index) {
        if (!strcmp(fixture_cvars[index].name, name))
            return &fixture_cvars[index].cvar;
    }
    CHECK(fixture_cvar_count < q_countof(fixture_cvars));
    entry = &fixture_cvars[fixture_cvar_count++];
    memset(entry, 0, sizeof(*entry));
    Q_strlcpy(entry->name, name, sizeof(entry->name));
    Q_strlcpy(entry->value, value, sizeof(entry->value));
    entry->cvar.name = entry->name;
    entry->cvar.string = entry->value;
    entry->cvar.flags = (cvar_flags_t)flags;
    entry->cvar.value = (float)atof(value);
    entry->cvar.integer = atoi(value);
    return &entry->cvar;
}

void *Z_Malloc(size_t size)
{
    void *pointer = malloc(size);
    CHECK(pointer != NULL);
    return pointer;
}

void *Z_Mallocz(size_t size)
{
    void *pointer = calloc(1, size);
    CHECK(pointer != NULL);
    return pointer;
}

void *Z_TagMalloc(size_t size, memtag_t tag)
{
    (void)tag;
    return Z_Malloc(size);
}

void *Z_TagMallocz(size_t size, memtag_t tag)
{
    (void)tag;
    return Z_Mallocz(size);
}

void *Z_Realloc(void *pointer, size_t size)
{
    void *result = realloc(pointer, size);
    CHECK(result != NULL || size == 0);
    return result;
}

void Z_Free(void *pointer)
{
    free(pointer);
}

int FS_LoadFileEx(const char *path, void **buffer, unsigned flags, memtag_t tag)
{
    FILE *file;
    long length;
    byte *data;
    size_t read_length;

    (void)flags;
    (void)tag;
    file = fopen(path, "rb");
    if (!file)
        return Q_ERR(errno);
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0 || length > INT_MAX) {
        fclose(file);
        return Q_ERR(EIO);
    }
    if (!buffer) {
        fclose(file);
        return (int)length;
    }
    data = Z_Malloc((size_t)length ? (size_t)length : 1u);
    read_length = fread(data, 1, (size_t)length, file);
    fclose(file);
    if (read_length != (size_t)length) {
        Z_Free(data);
        return Q_ERR(EIO);
    }
    *buffer = data;
    return (int)length;
}

int FS_WriteFile(const char *path, const void *data, size_t length)
{
    (void)path;
    (void)data;
    (void)length;
    return Q_ERR(EPERM);
}

bool Com_ParseMapName(char *output, const char *input, size_t output_size)
{
    (void)output;
    (void)input;
    (void)output_size;
    return false;
}

static bool float_exact(float lhs, float rhs)
{
    uint32_t lhs_bits;
    uint32_t rhs_bits;

    memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
    memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
    return lhs_bits == rhs_bits;
}

static bool vector_exact(const vec3_t lhs, const vec3_t rhs)
{
    return float_exact(lhs[0], rhs[0]) && float_exact(lhs[1], rhs[1]) &&
           float_exact(lhs[2], rhs[2]);
}

static bool plane_exact(const cplane_t *lhs, const cplane_t *rhs)
{
    return vector_exact(lhs->normal, rhs->normal) &&
           float_exact(lhs->dist, rhs->dist) && lhs->type == rhs->type &&
           lhs->signbits == rhs->signbits;
}

static void check_trace_parity(const trace_t *actual, const trace_t *reference,
                               const edict_t *clip)
{
    CHECK(actual->allsolid == reference->allsolid);
    CHECK(actual->startsolid == reference->startsolid);
    CHECK(float_exact(actual->fraction, reference->fraction));
    CHECK(vector_exact(actual->endpos, reference->endpos));
    CHECK(plane_exact(&actual->plane, &reference->plane));
    CHECK(actual->surface == reference->surface);
    CHECK(actual->contents == reference->contents);
    CHECK(actual->ent == NULL);
    CHECK(reference->ent == clip);
    CHECK(plane_exact(&actual->plane2, &reference->plane2));
    CHECK(actual->surface2 == reference->surface2);
}

static void local_to_world(const vec3_t local, const vec3_t origin,
                           const vec3_t angles, vec3_t world)
{
    vec3_t transformed;

    VectorCopy(local, transformed);
    if (!VectorEmpty(angles)) {
        vec3_t axis[3];
        AnglesToAxis(angles, axis);
        TransposeAxis(axis);
        RotatePoint(transformed, axis);
    }
    VectorAdd(transformed, origin, world);
}

typedef enum fixture_expectation_e {
    FIXTURE_EXPECT_HIT,
    FIXTURE_EXPECT_START_SOLID,
    FIXTURE_EXPECT_ALL_SOLID,
    FIXTURE_EXPECT_MISS,
} fixture_expectation_t;

typedef struct fixture_case_s {
    const char *name;
    uint32_t game_model_index;
    contents_t contents_mask;
    vec3_t origin;
    vec3_t angles;
    vec3_t local_start;
    vec3_t local_end;
    vec3_t mins;
    vec3_t maxs;
    fixture_expectation_t expectation;
    contents_t expected_contents;
    const char *expected_surface;
    bool expect_secondary_plane;
} fixture_case_t;

static const fixture_case_t fixture_cases[] = {
    {
        "translated-ray-hit", 2, CONTENTS_SOLID,
        { 128.0f, -64.0f, 32.0f }, { 0.0f, 0.0f, 0.0f },
        { -64.0f, 0.0f, 0.0f }, { 64.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
        FIXTURE_EXPECT_HIT, CONTENTS_SOLID, FIXTURE_SOLID_SURFACE, false,
    },
    {
        "rotated-ray-hit", 2, CONTENTS_SOLID,
        { -96.0f, 72.0f, 48.0f }, { 0.0f, 37.0f, 0.0f },
        { 0.0f, -72.0f, 1.0f }, { 0.0f, 72.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
        FIXTURE_EXPECT_HIT, CONTENTS_SOLID, FIXTURE_SOLID_SURFACE, false,
    },
    {
        "translated-box-hit", 2, CONTENTS_SOLID,
        { 48.0f, 160.0f, -24.0f }, { 0.0f, 0.0f, 0.0f },
        { -72.0f, 8.0f, 0.0f }, { 72.0f, 8.0f, 0.0f },
        { -4.0f, -6.0f, -2.0f }, { 4.0f, 6.0f, 2.0f },
        FIXTURE_EXPECT_HIT, CONTENTS_SOLID, FIXTURE_SOLID_SURFACE, false,
    },
    {
        "rotated-box-hit", 2, CONTENTS_SOLID,
        { -140.0f, -92.0f, 70.0f }, { 11.0f, 41.0f, 7.0f },
        { -80.0f, 5.0f, -1.0f }, { 80.0f, 5.0f, -1.0f },
        { -3.0f, -5.0f, -4.0f }, { 3.0f, 5.0f, 4.0f },
        FIXTURE_EXPECT_HIT, CONTENTS_SOLID, FIXTURE_SOLID_SURFACE, false,
    },
    {
        "corner-two-plane-ray", 2, CONTENTS_SOLID,
        { 18.0f, -188.0f, 11.0f }, { 0.0f, 23.0f, 0.0f },
        { -64.0f, -90.0f, 0.0f }, { 64.0f, 90.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
        FIXTURE_EXPECT_HIT, CONTENTS_SOLID, FIXTURE_SOLID_SURFACE, true,
    },
    {
        "start-solid-exit", 2, CONTENTS_SOLID,
        { 208.0f, 96.0f, 40.0f }, { 0.0f, 61.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }, { 72.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
        FIXTURE_EXPECT_START_SOLID, 0, NULL, false,
    },
    {
        "all-solid-stationary-box", 2, CONTENTS_SOLID,
        { -208.0f, 12.0f, -30.0f }, { 0.0f, 29.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
        { -2.0f, -3.0f, -2.0f }, { 2.0f, 3.0f, 2.0f },
        FIXTURE_EXPECT_ALL_SOLID, CONTENTS_SOLID, NULL, false,
    },
    {
        "solid-mask-rejection", 2, CONTENTS_WATER,
        { 80.0f, 204.0f, 24.0f }, { 0.0f, 17.0f, 0.0f },
        { -72.0f, 0.0f, 0.0f }, { 72.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
        FIXTURE_EXPECT_MISS, 0, NULL, false,
    },
    {
        "rotated-water-ray-hit", 3, CONTENTS_WATER,
        { -20.0f, 232.0f, 54.0f }, { 0.0f, 53.0f, 0.0f },
        { -72.0f, 0.0f, 0.0f }, { 72.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
        FIXTURE_EXPECT_HIT, CONTENTS_WATER, FIXTURE_WATER_SURFACE, false,
    },
    {
        "water-mask-rejection", 3, CONTENTS_SOLID,
        { 276.0f, -40.0f, 18.0f }, { 0.0f, 71.0f, 0.0f },
        { -72.0f, 0.0f, 0.0f }, { 72.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
        FIXTURE_EXPECT_MISS, 0, NULL, false,
    },
};

static void configure_clip(edict_t *clip,
                           const worr_rewind_collision_asset_v1 *asset,
                           const fixture_case_t *test_case)
{
    memset(clip, 0, sizeof(*clip));
    clip->inuse = true;
    clip->linked = true;
    clip->linkcount = 73;
    clip->solid = SOLID_BSP;
    clip->s.number = 1;
    clip->s.modelindex = (int)asset->source_model_index;
    VectorCopy(test_case->origin, clip->s.origin);
    VectorCopy(test_case->angles, clip->s.angles);
    VectorCopy(asset->local_mins, clip->mins);
    VectorCopy(asset->local_maxs, clip->maxs);

    memset(&sv.entities[1], 0, sizeof(sv.entities[1]));
    List_Init(&sv.entities[1].area);
    sv.entities[1].solid32 = PACKED_BSP;
    sv.entities[1].num_clusters = 2;
    sv.entities[1].clusternums[0] = 17;
    sv.entities[1].clusternums[1] = 23;
}

static void check_live_state_unchanged(const edict_t *edict_before,
                                       const server_entity_t *server_before)
{
    CHECK(memcmp(edict_before, &fixture_edicts[1], sizeof(*edict_before)) == 0);
    CHECK(memcmp(server_before, &sv.entities[1], sizeof(*server_before)) == 0);
}

static void run_case(const worr_rewind_collision_import_v1 *api,
                     const fixture_case_t *test_case)
{
    worr_rewind_collision_asset_v1 asset;
    worr_rewind_collision_trace_request_v1 request;
    edict_t edict_before;
    server_entity_t server_before;
    trace_t actual;
    trace_t reference;
    edict_t *clip = &fixture_edicts[1];

    memset(&asset, 0, sizeof(asset));
    CHECK(api->ResolveInlineBrush(FIXTURE_EPOCH,
                                  test_case->game_model_index, &asset));
    configure_clip(clip, &asset, test_case);
    edict_before = *clip;
    server_before = sv.entities[1];

    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.schema_version = WORR_REWIND_COLLISION_SCHEMA_VERSION;
    request.asset = asset.handle;
    request.contents_mask = (uint32_t)test_case->contents_mask;
    local_to_world(test_case->local_start, test_case->origin,
                   test_case->angles, request.start);
    local_to_world(test_case->local_end, test_case->origin,
                   test_case->angles, request.end);
    VectorCopy(test_case->mins, request.mins);
    VectorCopy(test_case->maxs, request.maxs);
    VectorCopy(test_case->origin, request.origin);
    VectorCopy(test_case->angles, request.angles);

    memset(&actual, 0xa5, sizeof(actual));
    CHECK(api->TraceTransformed(&request, &actual));
    check_live_state_unchanged(&edict_before, &server_before);

    reference = SV_Clip(request.start, request.mins, request.maxs,
                        request.end, clip, test_case->contents_mask);
    check_live_state_unchanged(&edict_before, &server_before);
    check_trace_parity(&actual, &reference, clip);

    switch (test_case->expectation) {
    case FIXTURE_EXPECT_HIT:
        CHECK(!actual.startsolid);
        CHECK(!actual.allsolid);
        CHECK(actual.fraction > 0.0f && actual.fraction < 1.0f);
        CHECK(actual.contents == test_case->expected_contents);
        CHECK(actual.surface != NULL);
        CHECK(!strcmp(actual.surface->name, test_case->expected_surface));
        CHECK(actual.surface->id ==
              (test_case->game_model_index == 2 ? 1u : 2u));
        CHECK(actual.surface->flags ==
              (test_case->game_model_index == 2 ? SURF_SLICK : SURF_WARP));
        CHECK(actual.surface->value ==
              (test_case->game_model_index == 2 ? 101 : 202));
        CHECK(!VectorEmpty(actual.plane.normal));
        CHECK(fabsf(VectorLength(actual.plane.normal) - 1.0f) < 0.00001f);
        break;
    case FIXTURE_EXPECT_START_SOLID:
        CHECK(actual.startsolid);
        CHECK(!actual.allsolid);
        CHECK(float_exact(actual.fraction, 1.0f));
        break;
    case FIXTURE_EXPECT_ALL_SOLID:
        CHECK(actual.startsolid);
        CHECK(actual.allsolid);
        CHECK(float_exact(actual.fraction, 0.0f));
        CHECK(actual.contents == test_case->expected_contents);
        break;
    case FIXTURE_EXPECT_MISS:
        CHECK(!actual.startsolid);
        CHECK(!actual.allsolid);
        CHECK(float_exact(actual.fraction, 1.0f));
        CHECK(actual.contents == 0);
        break;
    }

    if (test_case->expect_secondary_plane) {
        CHECK(!VectorEmpty(actual.plane2.normal));
        CHECK(actual.surface2 != NULL);
    }
    printf("  parity %-26s fraction=%.9g start=%u all=%u contents=%#x\n",
           test_case->name, actual.fraction, actual.startsolid,
           actual.allsolid, (unsigned)actual.contents);
}

static void check_rejection_unchanged(
    const worr_rewind_collision_import_v1 *api,
    const worr_rewind_collision_trace_request_v1 *request)
{
    trace_t output;
    trace_t before;
    edict_t edict_before = fixture_edicts[1];
    server_entity_t server_before = sv.entities[1];

    memset(&output, 0x6d, sizeof(output));
    before = output;
    CHECK(!api->TraceTransformed(request, &output));
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);
    check_live_state_unchanged(&edict_before, &server_before);
}

static void run_identity_rejection_cases(
    const worr_rewind_collision_import_v1 *api)
{
    worr_rewind_collision_asset_v1 asset;
    worr_rewind_collision_asset_v1 water_asset;
    worr_rewind_collision_trace_request_v1 request;
    uint32_t saved_epoch;
    int saved_checksum;

    memset(&asset, 0, sizeof(asset));
    memset(&water_asset, 0, sizeof(water_asset));
    CHECK(api->ResolveInlineBrush(FIXTURE_EPOCH, 2, &asset));
    CHECK(api->ResolveInlineBrush(FIXTURE_EPOCH, 3, &water_asset));
    CHECK(asset.handle.asset_hash != water_asset.handle.asset_hash);

    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.schema_version = WORR_REWIND_COLLISION_SCHEMA_VERSION;
    request.asset = asset.handle;
    request.contents_mask = CONTENTS_SOLID;
    VectorSet(request.start, -64.0f, 0.0f, 0.0f);
    VectorSet(request.end, 64.0f, 0.0f, 0.0f);

    request.asset.asset_hash ^= UINT64_C(0x0100000000000000);
    check_rejection_unchanged(api, &request);
    request.asset = asset.handle;

    --request.asset.map_epoch;
    check_rejection_unchanged(api, &request);
    request.asset = asset.handle;

    saved_epoch = sv.worr_snapshot_epoch;
    ++sv.worr_snapshot_epoch;
    check_rejection_unchanged(api, &request);
    sv.worr_snapshot_epoch = saved_epoch;

    saved_checksum = sv.cm.checksum;
    sv.cm.checksum ^= 0x01010101;
    check_rejection_unchanged(api, &request);
    sv.cm.checksum = saved_checksum;

    puts("  rejection stale-epoch/hash/map-epoch/map-checksum transactional");
}

static void run_gameplay_visibility_case(void)
{
    const vec3_t shooter_origin = {96.0f, 192.0f, 64.0f};
    const vec3_t target_origin = {352.0f, 192.0f, 64.0f};
    const mleaf_t *shooter_leaf;
    const mleaf_t *target_leaf;
    visrow_t pvs;
    visrow_t phs;

    shooter_leaf = CM_PointLeaf(&sv.cm, shooter_origin);
    target_leaf = CM_PointLeaf(&sv.cm, target_origin);
    CHECK(shooter_leaf != NULL);
    CHECK(target_leaf != NULL);
    CHECK(shooter_leaf->area == 1);
    CHECK(target_leaf->area == 1);
    CHECK(shooter_leaf->cluster == 0);
    CHECK(target_leaf->cluster == 0);
    CHECK(CM_AreasConnected(
        &sv.cm, shooter_leaf->area, target_leaf->area));

    memset(&pvs, 0, sizeof(pvs));
    memset(&phs, 0, sizeof(phs));
    BSP_ClusterVis(sv.cm.cache, &pvs, shooter_leaf->cluster, DVIS_PVS);
    BSP_ClusterVis(sv.cm.cache, &phs, shooter_leaf->cluster, DVIS_PHS);
    CHECK(Q_IsBitSet(pvs.b, target_leaf->cluster));
    CHECK(Q_IsBitSet(phs.b, target_leaf->cluster));

    puts("  gameplay visibility area=1 cluster=0 PVS/PHS connected");
}

int main(int argc, char **argv)
{
    const worr_rewind_collision_import_v1 *api;
    worr_rewind_collision_map_v1 map;
    worr_rewind_collision_asset_v1 solid_asset;
    worr_rewind_collision_asset_v1 water_asset;
    int result;

    CHECK(argc == 2);
    Hunk_Init();
    BSP_Init();
    CM_Init();

    memset(&svs, 0, sizeof(svs));
    memset(&sv, 0, sizeof(sv));
    memset(fixture_edicts, 0, sizeof(fixture_edicts));
    memset(&fixture_game_export, 0, sizeof(fixture_game_export));
    fixture_game_export.edicts = fixture_edicts;
    fixture_game_export.edict_size = sizeof(fixture_edicts[0]);
    fixture_game_export.num_edicts = q_countof(fixture_edicts);
    fixture_game_export.max_edicts = q_countof(fixture_edicts);

    result = CM_LoadMap(&sv.cm, argv[1]);
    if (result != Q_ERR_SUCCESS) {
        fprintf(stderr, "CM_LoadMap(%s) failed: %s (%s)\n", argv[1],
                BSP_ErrorString(result), Com_GetLastError());
        return EXIT_FAILURE;
    }
    CHECK(sv.cm.cache != NULL);
    CHECK(sv.cm.cache->nummodels == 3);
    /* One world-water brush plus the solid and water inline model brushes. */
    CHECK(sv.cm.cache->numbrushes == 3);
    CHECK(sv.cm.cache->numtexinfo == 2);
    CHECK(sv.cm.cache->numareas == 2);
    CHECK(sv.cm.cache->vis != NULL);
    CHECK(sv.cm.cache->vis->numclusters == 1);

    run_gameplay_visibility_case();

    sv.state = ss_game;
    sv.worr_snapshot_epoch = FIXTURE_EPOCH;
    svs.csr.extended = true;
    svs.csr.max_models = MAX_MODELS;

    api = SV_RewindCollisionImportV1();
    CHECK(api != NULL);
    CHECK(api->struct_size == sizeof(*api));
    CHECK(api->api_version == WORR_REWIND_COLLISION_API_VERSION);

    memset(&map, 0, sizeof(map));
    CHECK(api->GetMapIdentity(&map));
    CHECK(map.map_epoch == FIXTURE_EPOCH);
    CHECK(map.map_checksum == (uint32_t)sv.cm.checksum);
    CHECK(map.inline_model_count == 2);

    memset(&solid_asset, 0, sizeof(solid_asset));
    memset(&water_asset, 0, sizeof(water_asset));
    CHECK(api->ResolveInlineBrush(FIXTURE_EPOCH, 2, &solid_asset));
    CHECK(api->ResolveInlineBrush(FIXTURE_EPOCH, 3, &water_asset));
    CHECK(solid_asset.kind == WORR_REWIND_COLLISION_ASSET_INLINE_BRUSH);
    CHECK(water_asset.kind == WORR_REWIND_COLLISION_ASSET_INLINE_BRUSH);
    CHECK(solid_asset.handle.asset_id == 1);
    CHECK(water_asset.handle.asset_id == 2);
    CHECK(!strcmp(sv.cm.cache->texinfo[0].c.name,
                  FIXTURE_SOLID_SURFACE));
    CHECK(!strcmp(sv.cm.cache->texinfo[1].c.name,
                  FIXTURE_WATER_SURFACE));

    printf("rewind_collision_real_bsp_test: checksum=%#x models=%u cases=%zu\n",
           map.map_checksum, map.inline_model_count,
           q_countof(fixture_cases));
    for (size_t index = 0; index < q_countof(fixture_cases); ++index)
        run_case(api, &fixture_cases[index]);
    run_identity_rejection_cases(api);

    CM_FreeMap(&sv.cm);
    puts("rewind_collision_real_bsp_test: ok");
    return EXIT_SUCCESS;
}
