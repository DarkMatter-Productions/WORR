/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
// cg_view.cpp -- cgame-local first-person view and viewweapon helpers

#include "cg_entity_local.h"

namespace {

constexpr int CG_WEAPON_BOB_DISABLED = 0;
constexpr int CG_WEAPON_BOB_QUAKE3 = 1;
constexpr int CG_WEAPON_BOB_DOOM3 = 2;

constexpr float CG_WEAPON_BOB_MIN_SPEED = 5.0f;
constexpr float CG_WEAPON_BOB_RUN_SPEED = 200.0f;
constexpr float CG_WEAPON_BOB_CROUCH_RATE = 0.5f;
constexpr float CG_WEAPON_BOB_WALK_RATE = 0.3f;
constexpr float CG_WEAPON_BOB_RUN_RATE = 0.4f;
constexpr float CG_WEAPON_BOB_ANGLE_ROLL = 0.005f;
constexpr float CG_WEAPON_BOB_ANGLE_YAW = 0.010f;
constexpr float CG_WEAPON_BOB_ANGLE_PITCH = 0.005f;
constexpr float CG_WEAPON_IDLE_SPEED_BIAS = 40.0f;
constexpr float CG_WEAPON_IDLE_ANGLE_SCALE = 0.010f;
constexpr int CG_WEAPON_LAND_DEFLECT_TIME = 150;
constexpr int CG_WEAPON_LAND_RETURN_TIME = 300;
constexpr float CG_WEAPON_LAND_SCALE = 0.25f;
constexpr float CG_WEAPON_LAND_VELOCITY_MIN = 120.0f;
constexpr float CG_WEAPON_LAND_VELOCITY_SCALE = 0.05f;
constexpr float CG_WEAPON_LAND_CHANGE_MAX = 32.0f;

constexpr int CG_D3_VIEW_ANGLE_HISTORY = 64;
constexpr int CG_D3_VIEW_ANGLE_AVERAGES = 10;
constexpr float CG_D3_VIEW_ANGLE_SCALE = 0.25f;
constexpr float CG_D3_VIEW_ANGLE_MAX = 10.0f;
constexpr int CG_D3_ACCEL_HISTORY = 16;
constexpr float CG_D3_ACCEL_TIME = 400.0f;
constexpr float CG_D3_ACCEL_SCALE = 0.005f;
constexpr float CG_D3_ACCEL_LOG_MIN = 8.0f;
constexpr float CG_D3_ACCEL_COMPONENT_MAX = 400.0f;

struct cg_weapon_accel_t {
    unsigned time = 0;
    vec3_t dir = {};
};

struct cg_weapon_bob_metrics_t {
    float xy_speed = 0.0f;
    float bob_frac_sin = 0.0f;
    int foot = 0;
    int delta_ms = 0;
    bool grounded = false;
    bool moving = false;
    vec3_t velocity = {};
};

struct cg_weapon_bob_state_t {
    bool initialized = false;
    int server_count = 0;
    int last_mode = CG_WEAPON_BOB_DOOM3;
    unsigned last_time = 0;
    int bob_cycle = 0;
    bool previous_grounded = false;
    vec3_t previous_velocity = {};
    unsigned land_time = 0;
    float land_change = 0.0f;
    vec3_t view_angle_history[CG_D3_VIEW_ANGLE_HISTORY] = {};
    int view_angle_count = 0;
    cg_weapon_accel_t accel_history[CG_D3_ACCEL_HISTORY] = {};
    int accel_count = 0;
    bool pose_valid = false;
    unsigned pose_time = 0;
    int pose_mode = CG_WEAPON_BOB_DOOM3;
    vec3_t pose_origin = {};
    vec3_t pose_angles = {};
};

cg_weapon_bob_state_t cg_weapon_bob_state;

static int CG_WeaponBobMode()
{
    cvar_t *weapon_bob = cg_weapon_bob ? cg_weapon_bob : cg_weaponBob;
    if (!weapon_bob)
        return CG_WEAPON_BOB_DOOM3;

    return Cvar_ClampInteger(weapon_bob, CG_WEAPON_BOB_DISABLED, CG_WEAPON_BOB_DOOM3);
}

static float CG_NormalizeAngle180(float angle)
{
    while (angle > 180.0f)
        angle -= 360.0f;
    while (angle < -180.0f)
        angle += 360.0f;
    return angle;
}

static float CG_ClipComponent(float value, float magnitude)
{
    return Q_clipf(value, -magnitude, magnitude);
}

static void CG_GetWeaponVelocity(vec3_t velocity, const player_state_t *ps, const player_state_t *ops)
{
    if (!cls.demo.playback && cl_predict->integer && !(cl.frame.ps.pmove.pm_flags & PMF_NO_PREDICTION)) {
        VectorCopy(cl.predicted_velocity, velocity);
        return;
    }

    LerpVector(ops->pmove.velocity, ps->pmove.velocity, CL_KEYLERPFRAC, velocity);
}

static void CG_ResetWeaponBobState(int mode, const player_state_t *ps, const player_state_t *ops)
{
    memset(&cg_weapon_bob_state, 0, sizeof(cg_weapon_bob_state));
    cg_weapon_bob_state.initialized = true;
    cg_weapon_bob_state.server_count = cl.servercount;
    cg_weapon_bob_state.last_mode = mode;
    cg_weapon_bob_state.previous_grounded = (ps->pmove.pm_flags & PMF_ON_GROUND) != 0;
    CG_GetWeaponVelocity(cg_weapon_bob_state.previous_velocity, ps, ops);
}

static int CG_FrameDeltaMs(unsigned now)
{
    int delta_ms = 0;

    if (cg_weapon_bob_state.last_time && now >= cg_weapon_bob_state.last_time)
        delta_ms = static_cast<int>(now - cg_weapon_bob_state.last_time);
    else
        delta_ms = cl.frametime.time;

    if (delta_ms < 0)
        delta_ms = 0;
    else if (delta_ms > 100)
        delta_ms = 100;

    cg_weapon_bob_state.last_time = now;
    return delta_ms;
}

static void CG_LogDoom3Acceleration(const vec3_t velocity)
{
    vec3_t delta;
    VectorSubtract(velocity, cg_weapon_bob_state.previous_velocity, delta);

    vec3_t local_delta = {
        DotProduct(delta, cl.v_forward),
        DotProduct(delta, cl.v_right),
        DotProduct(delta, cl.v_up)
    };

    if (fabsf(local_delta[0]) < CG_D3_ACCEL_LOG_MIN &&
        fabsf(local_delta[1]) < CG_D3_ACCEL_LOG_MIN &&
        fabsf(local_delta[2]) < CG_D3_ACCEL_LOG_MIN) {
        return;
    }

    cg_weapon_accel_t *acc = &cg_weapon_bob_state.accel_history[cg_weapon_bob_state.accel_count & (CG_D3_ACCEL_HISTORY - 1)];
    cg_weapon_bob_state.accel_count++;
    acc->time = cls.realtime;
    for (int i = 0; i < 3; i++)
        acc->dir[i] = CG_ClipComponent(local_delta[i], CG_D3_ACCEL_COMPONENT_MAX);
}

static void CG_LogDoom3ViewAngles()
{
    VectorCopy(cl.refdef.viewangles,
               cg_weapon_bob_state.view_angle_history[cg_weapon_bob_state.view_angle_count & (CG_D3_VIEW_ANGLE_HISTORY - 1)]);
    cg_weapon_bob_state.view_angle_count++;
}

static void CG_UpdateWeaponBobState(int mode, const player_state_t *ps, const player_state_t *ops,
                                    cg_weapon_bob_metrics_t *metrics)
{
    if (!cg_weapon_bob_state.initialized ||
        cg_weapon_bob_state.server_count != cl.servercount ||
        cg_weapon_bob_state.last_mode != mode) {
        CG_ResetWeaponBobState(mode, ps, ops);
    }

    unsigned now = cls.realtime;
    metrics->delta_ms = CG_FrameDeltaMs(now);
    CG_GetWeaponVelocity(metrics->velocity, ps, ops);
    metrics->xy_speed = sqrtf(metrics->velocity[0] * metrics->velocity[0] +
                              metrics->velocity[1] * metrics->velocity[1]);
    metrics->grounded = (ps->pmove.pm_flags & PMF_ON_GROUND) != 0;
    metrics->moving = metrics->grounded &&
                      metrics->xy_speed > CG_WEAPON_BOB_MIN_SPEED &&
                      ps->pmove.pm_type < PM_DEAD;

    if (metrics->grounded && !cg_weapon_bob_state.previous_grounded &&
        cg_weapon_bob_state.previous_velocity[2] < -CG_WEAPON_LAND_VELOCITY_MIN) {
        float impact = -cg_weapon_bob_state.previous_velocity[2] * CG_WEAPON_LAND_VELOCITY_SCALE;
        cg_weapon_bob_state.land_change = -Q_clipf(impact, 0.0f, CG_WEAPON_LAND_CHANGE_MAX);
        cg_weapon_bob_state.land_time = now;
    }

    if (mode == CG_WEAPON_BOB_QUAKE3) {
        if (metrics->grounded && !metrics->moving) {
            cg_weapon_bob_state.bob_cycle = 0;
        } else if (metrics->moving) {
            float bob_move = (ps->pmove.pm_flags & PMF_DUCKED) ? CG_WEAPON_BOB_CROUCH_RATE :
                (metrics->xy_speed > CG_WEAPON_BOB_RUN_SPEED ? CG_WEAPON_BOB_RUN_RATE : CG_WEAPON_BOB_WALK_RATE);
            cg_weapon_bob_state.bob_cycle =
                static_cast<int>(cg_weapon_bob_state.bob_cycle + bob_move * metrics->delta_ms) & 255;
        }
    } else {
        if (!metrics->moving) {
            cg_weapon_bob_state.bob_cycle = 0;
        } else {
            float bob_move = (ps->pmove.pm_flags & PMF_DUCKED) ? CG_WEAPON_BOB_CROUCH_RATE :
                (metrics->xy_speed > CG_WEAPON_BOB_RUN_SPEED ? CG_WEAPON_BOB_RUN_RATE : CG_WEAPON_BOB_WALK_RATE);
            cg_weapon_bob_state.bob_cycle =
                static_cast<int>(cg_weapon_bob_state.bob_cycle + bob_move * metrics->delta_ms) & 255;
        }
    }

    metrics->foot = (cg_weapon_bob_state.bob_cycle & 128) ? 1 : 0;
    metrics->bob_frac_sin =
        fabsf(sinf(((cg_weapon_bob_state.bob_cycle & 127) / 127.0f) * M_PIf));

    if (mode == CG_WEAPON_BOB_DOOM3) {
        CG_LogDoom3Acceleration(metrics->velocity);
        CG_LogDoom3ViewAngles();
    }

    cg_weapon_bob_state.previous_grounded = metrics->grounded;
    VectorCopy(metrics->velocity, cg_weapon_bob_state.previous_velocity);
}

static float CG_WeaponLandingOffset()
{
    if (!cg_weapon_bob_state.land_time)
        return 0.0f;

    int delta = static_cast<int>(cls.realtime - cg_weapon_bob_state.land_time);
    if (delta < 0)
        return 0.0f;

    if (delta < CG_WEAPON_LAND_DEFLECT_TIME) {
        return cg_weapon_bob_state.land_change * CG_WEAPON_LAND_SCALE *
            (static_cast<float>(delta) / CG_WEAPON_LAND_DEFLECT_TIME);
    }

    if (delta < CG_WEAPON_LAND_DEFLECT_TIME + CG_WEAPON_LAND_RETURN_TIME) {
        return cg_weapon_bob_state.land_change * CG_WEAPON_LAND_SCALE *
            (static_cast<float>(CG_WEAPON_LAND_DEFLECT_TIME + CG_WEAPON_LAND_RETURN_TIME - delta) /
             CG_WEAPON_LAND_RETURN_TIME);
    }

    return 0.0f;
}

static void CG_ApplyWeaponStrideAngles(vec3_t angles, const cg_weapon_bob_metrics_t &metrics)
{
    float side_scale = metrics.foot ? -metrics.xy_speed : metrics.xy_speed;

    angles[ROLL] += side_scale * metrics.bob_frac_sin * CG_WEAPON_BOB_ANGLE_ROLL;
    angles[YAW] += side_scale * metrics.bob_frac_sin * CG_WEAPON_BOB_ANGLE_YAW;
    angles[PITCH] += metrics.xy_speed * metrics.bob_frac_sin * CG_WEAPON_BOB_ANGLE_PITCH;
}

static void CG_ApplyWeaponIdleDrift(vec3_t angles, const cg_weapon_bob_metrics_t &metrics)
{
    float scale = metrics.xy_speed + CG_WEAPON_IDLE_SPEED_BIAS;
    float idle = scale * sinf(cl.time * 0.001f) * CG_WEAPON_IDLE_ANGLE_SCALE;

    angles[ROLL] += idle;
    angles[YAW] += idle;
    angles[PITCH] += idle;
}

static void CG_Doom3TurningOffset(vec3_t offset)
{
    VectorClear(offset);

    if (cg_weapon_bob_state.view_angle_count < CG_D3_VIEW_ANGLE_HISTORY)
        return;

    int current_index = (cg_weapon_bob_state.view_angle_count - 1) & (CG_D3_VIEW_ANGLE_HISTORY - 1);
    const float *current = cg_weapon_bob_state.view_angle_history[current_index];
    int averages = min(CG_D3_VIEW_ANGLE_AVERAGES, CG_D3_VIEW_ANGLE_HISTORY);

    for (int j = 1; j < averages; j++) {
        int index = (cg_weapon_bob_state.view_angle_count - 1 - j) & (CG_D3_VIEW_ANGLE_HISTORY - 1);
        const float *history = cg_weapon_bob_state.view_angle_history[index];

        for (int i = 0; i < 3; i++)
            offset[i] += CG_NormalizeAngle180(history[i] - current[i]) / averages;
    }

    for (int i = 0; i < 3; i++)
        offset[i] = CG_ClipComponent(offset[i] * CG_D3_VIEW_ANGLE_SCALE, CG_D3_VIEW_ANGLE_MAX);
}

static void CG_Doom3AcceleratingOffset(vec3_t offset)
{
    VectorClear(offset);

    int oldest = max(0, cg_weapon_bob_state.accel_count - CG_D3_ACCEL_HISTORY);
    for (int i = cg_weapon_bob_state.accel_count - 1; i >= oldest; i--) {
        const cg_weapon_accel_t *acc = &cg_weapon_bob_state.accel_history[i & (CG_D3_ACCEL_HISTORY - 1)];
        if (!acc->time)
            continue;

        float t = static_cast<float>(cls.realtime - acc->time);
        if (t < 0.0f)
            continue;
        if (t >= CG_D3_ACCEL_TIME)
            break;

        float f = t / CG_D3_ACCEL_TIME;
        f = (cosf(f * 2.0f * M_PIf) - 1.0f) * 0.5f;

        for (int j = 0; j < 3; j++)
            offset[j] += f * CG_D3_ACCEL_SCALE * acc->dir[j];
    }
}

static void CG_ApplyQuake3WeaponBob(vec3_t origin, vec3_t angles,
                                    const player_state_t *ps,
                                    const player_state_t *ops)
{
    for (int i = 0; i < 3; i++) {
        origin[i] = cl.refdef.vieworg[i] + ops->gunoffset[i] +
            CL_KEYLERPFRAC * (ps->gunoffset[i] - ops->gunoffset[i]);
        angles[i] = cl.refdef.viewangles[i] +
            LerpAngle(ops->gunangles[i], ps->gunangles[i], CL_KEYLERPFRAC);
    }
}

static void CG_ApplyDoom3WeaponBob(vec3_t origin, vec3_t angles,
                                   const cg_weapon_bob_metrics_t &metrics)
{
    vec3_t accel_offset;
    CG_Doom3AcceleratingOffset(accel_offset);
    VectorMA(origin, accel_offset[0], cl.v_forward, origin);
    VectorMA(origin, accel_offset[1], cl.v_right, origin);
    VectorMA(origin, accel_offset[2], cl.v_up, origin);

    CG_ApplyWeaponStrideAngles(angles, metrics);

    vec3_t turn_offset;
    CG_Doom3TurningOffset(turn_offset);
    VectorAdd(angles, turn_offset, angles);

    origin[2] += CG_WeaponLandingOffset();
    CG_ApplyWeaponIdleDrift(angles, metrics);
}

} // namespace

void CG_View_CalcWeaponPose(vec3_t origin, vec3_t angles,
                            const player_state_t *ps,
                            const player_state_t *ops,
                            bool skip_bob)
{
    int mode = skip_bob ? CG_WEAPON_BOB_DISABLED : CG_WeaponBobMode();
    if (cg_weapon_bob_state.pose_valid &&
        cg_weapon_bob_state.pose_time == cls.realtime &&
        cg_weapon_bob_state.pose_mode == mode) {
        VectorCopy(cg_weapon_bob_state.pose_origin, origin);
        VectorCopy(cg_weapon_bob_state.pose_angles, angles);
        return;
    }

    VectorCopy(cl.refdef.vieworg, origin);
    VectorCopy(cl.refdef.viewangles, angles);

    if (mode == CG_WEAPON_BOB_DISABLED) {
        CG_ResetWeaponBobState(mode, ps, ops);
    } else if (mode == CG_WEAPON_BOB_QUAKE3) {
        if (!cg_weapon_bob_state.initialized ||
            cg_weapon_bob_state.server_count != cl.servercount ||
            cg_weapon_bob_state.last_mode != mode) {
            CG_ResetWeaponBobState(mode, ps, ops);
        }

        CG_ApplyQuake3WeaponBob(origin, angles, ps, ops);
    } else {
        cg_weapon_bob_metrics_t metrics = {};
        CG_UpdateWeaponBobState(mode, ps, ops, &metrics);
        CG_ApplyDoom3WeaponBob(origin, angles, metrics);
    }

    cg_weapon_bob_state.pose_valid = true;
    cg_weapon_bob_state.pose_time = cls.realtime;
    cg_weapon_bob_state.pose_mode = mode;
    VectorCopy(origin, cg_weapon_bob_state.pose_origin);
    VectorCopy(angles, cg_weapon_bob_state.pose_angles);
}
