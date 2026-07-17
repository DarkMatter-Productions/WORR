/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "gl.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

#define GL_GPU_TIMER_LATENCY 4

typedef struct {
    GLuint queries[GL_GPU_PROFILE_COUNT][GL_GPU_TIMER_LATENCY];
    bool pending[GL_GPU_PROFILE_COUNT][GL_GPU_TIMER_LATENCY];
    unsigned frame[GL_GPU_PROFILE_COUNT][GL_GPU_TIMER_LATENCY];
    uint64_t last_ns[GL_GPU_PROFILE_COUNT];
    GLuint frame_queries[GL_GPU_TIMER_LATENCY][2];
    bool frame_pending[GL_GPU_TIMER_LATENCY];
    bool frame_started[GL_GPU_TIMER_LATENCY];
    unsigned frame_timer_frame[GL_GPU_TIMER_LATENCY];
    uint64_t last_frame_ns;
    bool initialized;
    bool active;
    glGpuProfileScope_t active_scope;
} glGpuProfileState_t;

glFrameTelemetry_t gl_telemetry;

static cvar_t *gl_cpu_timers;
static cvar_t *gl_gpu_timers;
static cvar_t *gl_debug_markers;
static cvar_t *gl_profile_log;

static uint64_t gl_profile_cpu_us[GL_CPU_PROFILE_COUNT];
static uint32_t gl_profile_gpu_available_mask;
static bool gl_profile_gpu_frame_available;
static int gl_profile_gpu_missed;
static unsigned gl_profile_frame_number;
static glGpuProfileState_t gl_gpu_profile;

static uint64_t GL_ProfileNowUsec(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)now.QuadPart * 1000000ULL / (uint64_t)freq.QuadPart;
#else
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
#endif
}

static bool GL_ProfileCpuEnabled(void)
{
    return gl_cpu_timers &&
           (gl_cpu_timers->integer ||
            (gl_profile_log && gl_profile_log->integer > 0));
}

static bool GL_ProfileGpuEnabled(void)
{
    return gl_gpu_timers && gl_gpu_timers->integer &&
           gl_gpu_profile.initialized &&
           (gl_config.caps & QGL_CAP_TIMER_QUERY) &&
           qglBeginQuery && qglGetQueryObjectuiv && qglGetQueryObjectui64v;
}

static bool GL_ProfileGpuFrameEnabled(void)
{
    return GL_ProfileGpuEnabled() && qglQueryCounter;
}

void GL_ProfileInit(void)
{
    gl_cpu_timers = Cvar_Get("gl_cpu_timers", "0", 0);
    gl_gpu_timers = Cvar_Get("gl_gpu_timers", "0", 0);
    gl_debug_markers = Cvar_Get("gl_debug_markers", "1", 0);
    gl_profile_log = Cvar_Get("gl_profile_log", "0", CVAR_NOARCHIVE);

    if (gl_gpu_profile.initialized)
        return;

    if (!(gl_config.caps & QGL_CAP_TIMER_QUERY) || !qglGenQueries ||
        !qglGetQueryObjectui64v)
        return;

    for (int i = 0; i < GL_GPU_PROFILE_COUNT; i++)
        qglGenQueries(GL_GPU_TIMER_LATENCY, gl_gpu_profile.queries[i]);

    qglGenQueries(GL_GPU_TIMER_LATENCY * 2,
                  &gl_gpu_profile.frame_queries[0][0]);

    gl_gpu_profile.initialized = true;
    gl_gpu_profile.active = false;
}

void GL_ProfileShutdown(void)
{
    if (gl_gpu_profile.initialized && qglDeleteQueries) {
        for (int i = 0; i < GL_GPU_PROFILE_COUNT; i++)
            qglDeleteQueries(GL_GPU_TIMER_LATENCY, gl_gpu_profile.queries[i]);
        qglDeleteQueries(GL_GPU_TIMER_LATENCY * 2,
                          &gl_gpu_profile.frame_queries[0][0]);
    }

    memset(&gl_gpu_profile, 0, sizeof(gl_gpu_profile));
    memset(&gl_telemetry, 0, sizeof(gl_telemetry));
}

static void GL_ProfilePollGpuQueries(void)
{
    if (!gl_gpu_profile.initialized || !qglGetQueryObjectuiv ||
        !qglGetQueryObjectui64v)
        return;

    for (int scope = 0; scope < GL_GPU_PROFILE_COUNT; scope++) {
        for (int slot = 0; slot < GL_GPU_TIMER_LATENCY; slot++) {
            if (!gl_gpu_profile.pending[scope][slot])
                continue;

            if (gl_profile_frame_number - gl_gpu_profile.frame[scope][slot] < 2)
                continue;

            GLuint available = 0;
            qglGetQueryObjectuiv(gl_gpu_profile.queries[scope][slot],
                                 GL_QUERY_RESULT_AVAILABLE, &available);
            if (!available)
                continue;

            GLuint64 elapsed = 0;
            qglGetQueryObjectui64v(gl_gpu_profile.queries[scope][slot],
                                   GL_QUERY_RESULT, &elapsed);
            gl_gpu_profile.last_ns[scope] = elapsed;
            gl_gpu_profile.pending[scope][slot] = false;
            gl_profile_gpu_available_mask |= BIT(scope);
        }
    }
    for (int slot = 0; slot < GL_GPU_TIMER_LATENCY; slot++) {
        if (!gl_gpu_profile.frame_pending[slot])
            continue;

        if (gl_profile_frame_number - gl_gpu_profile.frame_timer_frame[slot] < 2)
            continue;

        GLuint available[2] = {0, 0};
        qglGetQueryObjectuiv(gl_gpu_profile.frame_queries[slot][0],
                             GL_QUERY_RESULT_AVAILABLE, &available[0]);
        qglGetQueryObjectuiv(gl_gpu_profile.frame_queries[slot][1],
                             GL_QUERY_RESULT_AVAILABLE, &available[1]);
        if (!available[0] || !available[1])
            continue;

        GLuint64 timestamps[2] = {0, 0};
        qglGetQueryObjectui64v(gl_gpu_profile.frame_queries[slot][0],
                               GL_QUERY_RESULT, &timestamps[0]);
        qglGetQueryObjectui64v(gl_gpu_profile.frame_queries[slot][1],
                               GL_QUERY_RESULT, &timestamps[1]);
        gl_gpu_profile.frame_pending[slot] = false;
        if (timestamps[1] < timestamps[0]) {
            gl_profile_gpu_missed++;
            continue;
        }

        gl_gpu_profile.last_frame_ns = timestamps[1] - timestamps[0];
        gl_profile_gpu_frame_available = true;
    }
}

void GL_ProfileBeginFrame(void)
{
    gl_profile_frame_number++;
    memset(gl_profile_cpu_us, 0, sizeof(gl_profile_cpu_us));
    gl_profile_gpu_available_mask = 0;
    gl_profile_gpu_frame_available = false;
    gl_profile_gpu_missed = 0;
    GL_ProfilePollGpuQueries();
}

static int GL_ProfilePendingGpuQueries(void)
{
    int pending = 0;

    if (!gl_gpu_profile.initialized)
        return 0;

    for (int scope = 0; scope < GL_GPU_PROFILE_COUNT; scope++)
        for (int slot = 0; slot < GL_GPU_TIMER_LATENCY; slot++)
            pending += gl_gpu_profile.pending[scope][slot] ? 1 : 0;

    for (int slot = 0; slot < GL_GPU_TIMER_LATENCY; slot++)
        pending += gl_gpu_profile.frame_pending[slot] ? 1 : 0;

    return pending;
}

static void GL_ProfileCaptureTelemetry(void)
{
    gl_telemetry.counters = c;
    gl_telemetry.frame_number = gl_profile_frame_number;
    gl_telemetry.nodes_visible = glr.nodes_visible;
    gl_telemetry.total_entities = glr.fd.num_entities;
    gl_telemetry.total_dlights = glr.fd.num_dlights;
    gl_telemetry.total_particles = glr.fd.num_particles;
    gl_telemetry.shaders_used = gl_static.use_shaders;
    gl_telemetry.gpu_lerp_used = gl_static.use_gpu_lerp;
    gl_telemetry.world_static_vbo_used =
        gl_static.world.buffer && !gl_static.world.vertices;
    gl_telemetry.ppl_used = (glr.ppl_bits & GLS_DYNAMIC_LIGHTS) != 0;
    memcpy(gl_telemetry.cpu_us, gl_profile_cpu_us,
           sizeof(gl_telemetry.cpu_us));
    memcpy(gl_telemetry.gpu_ns, gl_gpu_profile.last_ns,
           sizeof(gl_telemetry.gpu_ns));
    gl_telemetry.gpu_frame_ns = gl_gpu_profile.last_frame_ns;
    gl_telemetry.gpu_available_mask = gl_profile_gpu_available_mask;
    gl_telemetry.gpu_frame_valid = gl_profile_gpu_frame_available;
    gl_telemetry.gpu_pending = GL_ProfilePendingGpuQueries();
    gl_telemetry.gpu_missed = gl_profile_gpu_missed;
}

static float GL_ProfileUsecToMs(uint64_t usec)
{
    return (float)((double)usec / 1000.0);
}

static float GL_ProfileNsToMs(uint64_t ns)
{
    return (float)((double)ns / 1000000.0);
}

static void GL_ProfileLogTelemetry(void)
{
    int interval;

    if (!gl_profile_log)
        return;

    interval = Cvar_ClampInteger(gl_profile_log, 0, 1000000);
    if (interval <= 0)
        return;
    if (gl_telemetry.frame_number % (unsigned)interval)
        return;

    Com_Printf("GL profile frame %u: cpu_ms frame=%.3f mark_leaves=%.3f "
               "mark_lights=%.3f world_node=%.3f solid=%.3f push_lights=%.3f "
               "upload_lm=%.3f particles=%.3f beams=%.3f alias=%.3f "
               "postfx=%.3f\n",
               gl_telemetry.frame_number,
               GL_ProfileCpuMilliseconds(GL_CPU_PROFILE_FRAME),
               GL_ProfileCpuMilliseconds(GL_CPU_PROFILE_MARK_LEAVES),
               GL_ProfileCpuMilliseconds(GL_CPU_PROFILE_MARK_LIGHTS),
               GL_ProfileCpuMilliseconds(GL_CPU_PROFILE_WORLD_NODE),
               GL_ProfileCpuMilliseconds(GL_CPU_PROFILE_DRAW_SOLID_FACES),
               GL_ProfileCpuMilliseconds(GL_CPU_PROFILE_PUSH_LIGHTS),
               GL_ProfileCpuMilliseconds(GL_CPU_PROFILE_UPLOAD_LIGHTMAPS),
               GL_ProfileCpuMilliseconds(GL_CPU_PROFILE_PARTICLES),
               GL_ProfileCpuMilliseconds(GL_CPU_PROFILE_BEAMS),
               GL_ProfileCpuMilliseconds(GL_CPU_PROFILE_ALIAS_MODELS),
               GL_ProfileCpuMilliseconds(GL_CPU_PROFILE_POSTFX));

    Com_Printf("GL profile counters: nodes_visible=%d faces=%d/%d tris=%d "
               "batches=%d 2d=%d tex_switches=%d tex_uploads=%d lm_texels=%d "
               "array_binds=%d occlusion=%d dlights(total=%d used=%d "
               "uploads=%d culled=%d entculled=%d) stream(v=%llu i=%llu "
               "tex=%llu) fast_paths(shaders=%d gpu_lerp=%d world_vbo=%d "
               "ppl=%d)\n",
               gl_telemetry.nodes_visible,
               gl_telemetry.counters.facesDrawn,
               gl_telemetry.counters.facesMarked,
               gl_telemetry.counters.trisDrawn,
               gl_telemetry.counters.batchesDrawn,
               gl_telemetry.counters.batchesDrawn2D,
               gl_telemetry.counters.texSwitches,
               gl_telemetry.counters.texUploads,
               gl_telemetry.counters.lightTexels,
               gl_telemetry.counters.vertexArrayBinds,
               gl_telemetry.counters.occlusionQueries,
               gl_telemetry.counters.dlightsTotal,
               gl_telemetry.counters.dlightsUsed,
               gl_telemetry.counters.dlightUploads,
               gl_telemetry.counters.dlightsCulled,
               gl_telemetry.counters.dlightsEntCulled,
               (unsigned long long)gl_telemetry.counters.streamedVertexBytes,
               (unsigned long long)gl_telemetry.counters.streamedIndexBytes,
               (unsigned long long)gl_telemetry.counters.textureUploadBytes,
               gl_telemetry.shaders_used,
               gl_telemetry.gpu_lerp_used,
               gl_telemetry.world_static_vbo_used,
               gl_telemetry.ppl_used);

    if (gl_gpu_timers && gl_gpu_timers->integer) {
        Com_Printf("GL profile gpu_frame_ms=%.3f gpu_frame_valid=%d "
                   "gpu_ms world=%.3f lightmap=%.3f effects=%.3f "
                   "transparent=%.3f postfx=%.3f pending=%d missed=%d "
                   "available_mask=0x%x\n",
                   GL_ProfileGpuFrameMilliseconds(), gl_telemetry.gpu_frame_valid,
                   GL_ProfileGpuMilliseconds(GL_GPU_PROFILE_WORLD_OPAQUE),
                   GL_ProfileGpuMilliseconds(GL_GPU_PROFILE_LIGHTMAP_UPDATE),
                   GL_ProfileGpuMilliseconds(GL_GPU_PROFILE_EFFECTS),
                   GL_ProfileGpuMilliseconds(GL_GPU_PROFILE_TRANSPARENT),
                   GL_ProfileGpuMilliseconds(GL_GPU_PROFILE_POSTFX),
                   gl_telemetry.gpu_pending, gl_telemetry.gpu_missed,
                   gl_telemetry.gpu_available_mask);
    }

    // Keep interval telemetry machine-readable so paired capture tooling does
    // not need to drive a console command once per sampled frame.
    GL_PrintStats();
}

void GL_ProfileEndFrame(void)
{
    GL_ProfilePollGpuQueries();
    GL_ProfileCaptureTelemetry();
    GL_ProfileLogTelemetry();
}

void GL_ProfileGpuFrameBegin(void)
{
    if (!GL_ProfileGpuFrameEnabled())
        return;

    int slot = gl_profile_frame_number % GL_GPU_TIMER_LATENCY;
    if (gl_gpu_profile.frame_pending[slot] ||
        gl_gpu_profile.frame_started[slot]) {
        gl_profile_gpu_missed++;
        return;
    }

    qglQueryCounter(gl_gpu_profile.frame_queries[slot][0], GL_TIMESTAMP);
    gl_gpu_profile.frame_started[slot] = true;
}

void GL_ProfileGpuFrameEnd(void)
{
    if (!GL_ProfileGpuFrameEnabled())
        return;

    int slot = gl_profile_frame_number % GL_GPU_TIMER_LATENCY;
    if (!gl_gpu_profile.frame_started[slot])
        return;

    qglQueryCounter(gl_gpu_profile.frame_queries[slot][1], GL_TIMESTAMP);
    gl_gpu_profile.frame_pending[slot] = true;
    gl_gpu_profile.frame_timer_frame[slot] = gl_profile_frame_number;
    gl_gpu_profile.frame_started[slot] = false;
}

glCpuProfileGuard_t GL_ProfileCpuBegin(glCpuProfileScope_t scope)
{
    glCpuProfileGuard_t guard = { scope, 0, false };

    if ((unsigned)scope >= GL_CPU_PROFILE_COUNT || !GL_ProfileCpuEnabled())
        return guard;

    guard.start_us = GL_ProfileNowUsec();
    guard.active = true;
    return guard;
}

void GL_ProfileCpuEnd(glCpuProfileGuard_t *guard)
{
    if (!guard || !guard->active)
        return;

    gl_profile_cpu_us[guard->scope] += GL_ProfileNowUsec() - guard->start_us;
    guard->active = false;
}

glGpuProfileGuard_t GL_ProfileGpuBegin(glGpuProfileScope_t scope)
{
    glGpuProfileGuard_t guard = { scope, -1, false };

    if ((unsigned)scope >= GL_GPU_PROFILE_COUNT || !GL_ProfileGpuEnabled())
        return guard;

    if (gl_gpu_profile.active) {
        gl_profile_gpu_missed++;
        return guard;
    }

    int slot = gl_profile_frame_number % GL_GPU_TIMER_LATENCY;
    if (gl_gpu_profile.pending[scope][slot]) {
        gl_profile_gpu_missed++;
        return guard;
    }

    qglBeginQuery(GL_TIME_ELAPSED, gl_gpu_profile.queries[scope][slot]);
    gl_gpu_profile.active = true;
    gl_gpu_profile.active_scope = scope;
    guard.slot = slot;
    guard.active = true;
    return guard;
}

void GL_ProfileGpuEnd(glGpuProfileGuard_t *guard)
{
    if (!guard || !guard->active)
        return;

    if (!gl_gpu_profile.active || gl_gpu_profile.active_scope != guard->scope) {
        guard->active = false;
        return;
    }

    qglEndQuery(GL_TIME_ELAPSED);
    gl_gpu_profile.pending[guard->scope][guard->slot] = true;
    gl_gpu_profile.frame[guard->scope][guard->slot] = gl_profile_frame_number;
    gl_gpu_profile.active = false;
    guard->active = false;
}

glDebugGroup_t GL_DebugGroupBegin(const char *name)
{
    glDebugGroup_t group = { false };

    if (!gl_debug_markers || !gl_debug_markers->integer ||
        !(gl_config.caps & QGL_CAP_DEBUG_GROUPS) || !qglPushDebugGroup)
        return group;

    qglPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
    group.active = true;
    return group;
}

void GL_DebugGroupEnd(glDebugGroup_t *group)
{
    if (!group || !group->active)
        return;

    if (qglPopDebugGroup)
        qglPopDebugGroup();
    group->active = false;
}

float GL_ProfileCpuMilliseconds(glCpuProfileScope_t scope)
{
    if ((unsigned)scope >= GL_CPU_PROFILE_COUNT)
        return 0.0f;
    return GL_ProfileUsecToMs(gl_telemetry.cpu_us[scope]);
}

float GL_ProfileGpuMilliseconds(glGpuProfileScope_t scope)
{
    if ((unsigned)scope >= GL_GPU_PROFILE_COUNT)
        return 0.0f;
    return GL_ProfileNsToMs(gl_telemetry.gpu_ns[scope]);
}
float GL_ProfileGpuFrameMilliseconds(void)
{
    return GL_ProfileNsToMs(gl_telemetry.gpu_frame_ns);
}
