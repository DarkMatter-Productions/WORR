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
// cl.input.c  -- builds an intended movement command to send to the server

#include "client.h"
#include "client/command_identity.h"
#include "client/native_readiness_pilot.h"
#include "client/net_capability.h"
#include "client/snapshot_recovery.h"
#include "client/snapshot_shadow.h"
#include "common/crc.h"
#include "common/net/adaptive_input.h"
#include "common/net/usercmd_delta.h"

static cvar_t    *cl_nodelta;
static cvar_t    *cl_maxpackets;
static cvar_t    *cl_packetdup;
static cvar_t    *cl_adaptive_input;
static cvar_t    *cl_fuzzhack;
#if USE_DEBUG
static cvar_t    *cl_showpackets;
#endif
static cvar_t    *cl_instantpacket;
static cvar_t    *cl_batchcmds;

static cvar_t    *m_filter;
static cvar_t    *m_accel;
static cvar_t    *m_autosens;

static cvar_t    *cl_upspeed;
static cvar_t    *cl_forwardspeed;
static cvar_t    *cl_sidespeed;
static cvar_t    *cl_yawspeed;
static cvar_t    *cl_pitchspeed;
static cvar_t    *cl_run;
static cvar_t    *cl_anglespeedkey;

static cvar_t    *freelook;
static cvar_t    *lookspring;
static cvar_t    *lookstrafe;
static cvar_t    *sensitivity;

static cvar_t    *m_pitch;
static cvar_t    *m_yaw;
static cvar_t    *m_forward;
static cvar_t    *m_side;

static cvar_t    *in_gamepad;
static cvar_t    *in_gamepad_deadzone;
static cvar_t    *in_gamepad_look_deadzone;
static cvar_t    *in_gamepad_trigger_threshold;
static cvar_t    *in_gamepad_yaw;
static cvar_t    *in_gamepad_pitch;
static cvar_t    *in_gamepad_look_sensitivity;
static cvar_t    *in_gamepad_invert_y;
static cvar_t    *in_gamepad_wheel_speed;

typedef struct adaptive_input_runtime_s {
    worr_adaptive_input_config_v1 config;
    worr_adaptive_input_state_v1 state;
    worr_adaptive_input_output_v1 output;
    bool active;
    bool decision_valid;
    uint64_t integration_fallbacks;
} adaptive_input_runtime_t;

static adaptive_input_runtime_t adaptive_input;

void CL_AdaptiveInputReset(void)
{
    memset(&adaptive_input, 0, sizeof(adaptive_input));
    Worr_AdaptiveInputDefaultConfigV1(&adaptive_input.config);
    Worr_AdaptiveInputResetV1(&adaptive_input.state);
}

bool CL_AdaptiveInputGetOutputV1(worr_adaptive_input_output_v1 *output_out)
{
    if (!output_out || !adaptive_input.active ||
        !adaptive_input.decision_valid ||
        (adaptive_input.output.flags &
         WORR_ADAPTIVE_INPUT_OUTPUT_VALID) == 0) {
        return false;
    }
    *output_out = adaptive_input.output;
    return true;
}

static void CL_AdaptiveInputStatus_f(void);
static void CL_NativeReadinessPilotStatus_f(void);
static void CL_NativeInputDeliveryStatus_f(void);
static void CL_NativeEventProbeStatus_f(void);
static void CL_NativeEventProbeCheckpoint_f(void);
static void CL_WorrCapabilityStatus_f(void);

/*
===============================================================================

INPUT SUBSYSTEM

===============================================================================
*/

typedef struct {
    bool        modified;
    bool        grabbed;
    int         old_dx;
    int         old_dy;
} in_state_t;

static in_state_t   input;

typedef struct {
    float       axis[IN_GAMEPAD_AXIS_COUNT];
    bool        trigger_down[2];
} in_gamepad_state_t;

static in_gamepad_state_t gamepad;

static cvar_t    *in_enable;
static cvar_t    *in_grab;

// Windows automation owns a hidden surface through win_headless. Treat that
// state as an engine-level input opt-out so an incomplete test command line
// cannot initialize raw mouse input or later capture the user's cursor.
static bool IN_HeadlessAutomation(void)
{
    return Cvar_VariableInteger("win_headless") != 0;
}

static bool IN_GetCurrentGrab(void)
{
    // IN_Init deliberately leaves in_grab unset for disabled/headless input.
    // Activation still runs while a local map transitions to gameplay, so
    // fail closed instead of dereferencing an unavailable input configuration.
    if (!in_enable || !in_enable->integer || !in_grab ||
        IN_HeadlessAutomation()) {
        return false;
    }

    if (cls.active != ACT_ACTIVATED)
        return false;   // main window doesn't have focus

    if (cls.key_dest & KEY_MENU)
        return false;   // menus own an absolute system pointer

    if (cls.key_dest & KEY_CONSOLE)
        return true;    // console owns a captured, renderer-drawn pointer

    if (r_config.flags & QVF_FULLSCREEN)
        return true;    // full screen

    if (sv_paused->integer)
        return false;   // game paused

    if (cls.state != ca_active)
        return false;   // not connected

    if (in_grab->integer >= 2) {
        if (cls.demo.playback && !Key_IsDown(K_SHIFT))
            return false;   // playing a demo (and not using freelook)

        if (cl.frame.ps.pmove.pm_type == PM_FREEZE)
            return false;   // spectator mode
    }

    if (in_grab->integer >= 1)
        return true;    // regular playing mode

    return false;
}

/*
============
IN_Activate
============
*/
void IN_Activate(void)
{
    if (vid && vid->grab_mouse) {
        input.grabbed = IN_GetCurrentGrab();
        vid->grab_mouse(input.grabbed);
    }
}

bool IN_MouseGrabbed(void)
{
    return input.grabbed;
}

/*
============
IN_Restart_f
============
*/
static void IN_Restart_f(void)
{
    IN_Shutdown();
    IN_Init();
}

/*
============
IN_Frame
============
*/
void IN_Frame(void)
{
    if (input.modified) {
        IN_Restart_f();
    }
    if ((cls.key_dest & KEY_CONSOLE) && input.grabbed && vid &&
        vid->get_mouse_motion) {
        int dx, dy;
        if (vid->get_mouse_motion(&dx, &dy) && (dx || dy))
            Con_MouseMove(dx, dy);
    }
}

/*
================
IN_WarpMouse
================
*/
void IN_WarpMouse(int x, int y)
{
    if (vid && vid->warp_mouse) {
        vid->warp_mouse(x, y);
    }
}

/*
============
IN_Shutdown
============
*/
void IN_Shutdown(void)
{
    if (in_grab) {
        in_grab->changed = NULL;
    }

    IN_GamepadReset(com_eventTime);

    if (vid && vid->shutdown_mouse) {
        vid->shutdown_mouse();
    }

    memset(&input, 0, sizeof(input));
}

static void in_changed_hard(cvar_t *self)
{
    input.modified = true;
}

static void in_changed_soft(cvar_t *self)
{
    IN_Activate();
}

static void in_gamepad_changed(cvar_t *self)
{
    if (!self->integer) {
        IN_GamepadReset(com_eventTime);
    }
}

/*
============
IN_Init
============
*/
void IN_Init(void)
{
    in_enable = Cvar_Get("in_enable", "1", 0);
    in_enable->changed = in_changed_hard;
    if (!in_enable->integer || IN_HeadlessAutomation()) {
        Com_Printf("Mouse input disabled.\n");
        return;
    }

    if (!vid || !vid->init_mouse || !vid->init_mouse()) {
        Cvar_Set("in_enable", "0");
        return;
    }

    in_grab = Cvar_Get("in_grab", "1", 0);
    in_grab->changed = in_changed_soft;

    IN_Activate();
}

static float IN_NormalizeGamepadAxis(int value)
{
    if (value >= 0)
        return (float)value / 32767.0f;
    return (float)value / 32768.0f;
}

static float IN_NormalizeGamepadTrigger(int value)
{
    if (value <= 0)
        return 0.0f;
    return (float)value / 32767.0f;
}

static void IN_ApplyRadialDeadzone(float *x, float *y, float deadzone)
{
    float mag = sqrtf((*x * *x) + (*y * *y));

    if (mag <= deadzone) {
        *x = 0.0f;
        *y = 0.0f;
        return;
    }

    float scale = (mag - deadzone) / (1.0f - deadzone);
    if (mag > 0.0f) {
        scale /= mag;
    }

    *x *= scale;
    *y *= scale;
}

static void IN_GamepadUpdateTrigger(in_gamepad_axis_t axis, unsigned time)
{
    if (!in_gamepad_trigger_threshold)
        return;

    float threshold = Cvar_ClampValue(in_gamepad_trigger_threshold, 0.0f, 1.0f);
    int index = (axis == IN_GAMEPAD_AXIS_LEFT_TRIGGER) ? 0 : 1;
    bool down = gamepad.axis[axis] >= threshold;

    if (down == gamepad.trigger_down[index])
        return;

    gamepad.trigger_down[index] = down;
    Key_Event(index == 0 ? K_LEFT_TRIGGER : K_RIGHT_TRIGGER, down, time);
}

void IN_GamepadAxisEvent(in_gamepad_axis_t axis, int value, unsigned time)
{
    if (!in_gamepad || !in_gamepad->integer)
        return;
    if (axis < 0 || axis >= IN_GAMEPAD_AXIS_COUNT)
        return;

    float normalized;
    if (axis == IN_GAMEPAD_AXIS_LEFT_TRIGGER || axis == IN_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        normalized = IN_NormalizeGamepadTrigger(value);
        gamepad.axis[axis] = Q_clipf(normalized, 0.0f, 1.0f);
        IN_GamepadUpdateTrigger(axis, time);
        return;
    }

    normalized = IN_NormalizeGamepadAxis(value);
    gamepad.axis[axis] = Q_clipf(normalized, -1.0f, 1.0f);
}

void IN_GamepadReset(unsigned time)
{
    if (gamepad.trigger_down[0])
        Key_Event(K_LEFT_TRIGGER, false, time);
    if (gamepad.trigger_down[1])
        Key_Event(K_RIGHT_TRIGGER, false, time);

    memset(&gamepad, 0, sizeof(gamepad));
}

bool IN_GamepadEnabled(void)
{
    return in_gamepad && in_gamepad->integer;
}


/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attack, etc), it appends
its key number as a parameter to the command so it can be matched up with
the release.

state bit 0 is the current state of the key
state bit 1 is edge triggered on the up to down transition
state bit 2 is edge triggered on the down to up transition


Key_Event (int key, bool down, unsigned time);

  +mlook src time

===============================================================================
*/

typedef struct {
    int         down[2];        // key nums holding it down
    unsigned    downtime;        // msec timestamp
    unsigned    msec;            // msec down this frame
    int         state;
} kbutton_t;

static kbutton_t    in_klook;
static kbutton_t    in_left, in_right, in_forward, in_back;
static kbutton_t    in_lookup, in_lookdown, in_moveleft, in_moveright;
static kbutton_t    in_strafe, in_speed, in_use, in_attack, in_hook;
static kbutton_t    in_up, in_down;
// Kex stuff
static kbutton_t    in_holster;

static int          in_impulse;
static bool         in_mlooking;

static void KeyDown(kbutton_t *b)
{
    int k;
    char *c;

    c = Cmd_Argv(1);
    if (c[0])
        k = Q_atoi(c);
    else
        k = -1;        // typed manually at the console for continuous down

    if (k == b->down[0] || k == b->down[1])
        return;        // repeating key

    if (!b->down[0])
        b->down[0] = k;
    else if (!b->down[1])
        b->down[1] = k;
    else {
        Com_WPrintf("Three keys down for a button!\n");
        return;
    }

    if (b->state & 1)
        return;        // still down

    // save timestamp
    c = Cmd_Argv(2);
    b->downtime = Q_atoi(c);
    if (!b->downtime) {
        b->downtime = com_eventTime - 100;
    }

    b->state |= 1 + 2;    // down + impulse down
}

static void KeyUp(kbutton_t *b)
{
    int k;
    char *c;
    unsigned uptime;

    c = Cmd_Argv(1);
    if (c[0])
        k = Q_atoi(c);
    else {
        // typed manually at the console, assume for unsticking, so clear all
        b->down[0] = b->down[1] = 0;
        b->state = 0;    // impulse up
        return;
    }

    if (b->down[0] == k)
        b->down[0] = 0;
    else if (b->down[1] == k)
        b->down[1] = 0;
    else
        return;        // key up without corresponding down (menu pass through)
    if (b->down[0] || b->down[1])
        return;        // some other key is still holding it down

    if (!(b->state & 1))
        return;        // still up (this should not happen)

    // save timestamp
    c = Cmd_Argv(2);
    uptime = Q_atoi(c);
    if (!uptime) {
        b->msec += 10;
    } else if (uptime > b->downtime) {
        b->msec += uptime - b->downtime;
    }

    b->state &= ~1;        // now up
}

static void KeyClear(kbutton_t *b)
{
    b->msec = 0;
    b->state &= ~2;        // clear impulses
    if (b->state & 1) {
        b->downtime = com_eventTime; // still down
    }
}

static void IN_KLookDown(void) { KeyDown(&in_klook); }
static void IN_KLookUp(void) { KeyUp(&in_klook); }

static inline void CL_CheckInstantPacket(void)
{
    if (cl_instantpacket->integer && cls.state == ca_active && !cls.demo.playback) {
        cl.sendPacketNow = true;
    }
}

static void IN_UpDown(void)
{
    KeyDown(&in_up);
    CL_CheckInstantPacket();
}
static void IN_UpUp(void)
{
    KeyUp(&in_up);
    CL_CheckInstantPacket();
}
static void IN_DownDown(void)
{
    KeyDown(&in_down);
    CL_CheckInstantPacket();
}
static void IN_DownUp(void)
{
    KeyUp(&in_down);
    CL_CheckInstantPacket();
}
static void IN_LeftDown(void) { KeyDown(&in_left); }
static void IN_LeftUp(void) { KeyUp(&in_left); }
static void IN_RightDown(void) { KeyDown(&in_right); }
static void IN_RightUp(void) { KeyUp(&in_right); }
static void IN_ForwardDown(void) { KeyDown(&in_forward); }
static void IN_ForwardUp(void) { KeyUp(&in_forward); }
static void IN_BackDown(void) { KeyDown(&in_back); }
static void IN_BackUp(void) { KeyUp(&in_back); }
static void IN_LookupDown(void) { KeyDown(&in_lookup); }
static void IN_LookupUp(void) { KeyUp(&in_lookup); }
static void IN_LookdownDown(void) { KeyDown(&in_lookdown); }
static void IN_LookdownUp(void) { KeyUp(&in_lookdown); }
static void IN_MoveleftDown(void) { KeyDown(&in_moveleft); }
static void IN_MoveleftUp(void) { KeyUp(&in_moveleft); }
static void IN_MoverightDown(void) { KeyDown(&in_moveright); }
static void IN_MoverightUp(void) { KeyUp(&in_moveright); }
static void IN_SpeedDown(void) { KeyDown(&in_speed); }
static void IN_SpeedUp(void) { KeyUp(&in_speed); }
static void IN_StrafeDown(void) { KeyDown(&in_strafe); }
static void IN_StrafeUp(void) { KeyUp(&in_strafe); }

static void IN_AttackDown(void)
{
    if (cgame && cgame->Wheel_IsOpen && cgame->Wheel_IsOpen())
        return;

    KeyDown(&in_attack);
    CL_CheckInstantPacket();
}

static void IN_AttackUp(void)
{
    KeyUp(&in_attack);
    // Release-driven weapons (notably hand grenades) must not wait behind an
    // avoidable packet cadence after the player has let go. This still sends
    // the normal client-built user command; it neither initializes physical
    // input nor changes server weapon authority.
    CL_CheckInstantPacket();
}

static void IN_HookDown(void)
{
    KeyDown(&in_hook);
    // A Hook press is an ordinary user-command edge. Prompt delivery changes
    // neither input-device initialization nor the headless input policy.
    CL_CheckInstantPacket();
}

static void IN_HookUp(void)
{
    KeyUp(&in_hook);
    CL_CheckInstantPacket();
}

static void IN_UseDown(void)
{
    KeyDown(&in_use);
    CL_CheckInstantPacket();
}

static void IN_UseUp(void)
{
    KeyUp(&in_use);
}

static void IN_Impulse(void)
{
    in_impulse = Q_atoi(Cmd_Argv(1));
}

static void IN_CenterView(void)
{
    cl.viewangles[PITCH] = -cl.frame.ps.pmove.delta_angles[PITCH];
}

static void IN_MLookDown(void)
{
    in_mlooking = true;
}

static void IN_MLookUp(void)
{
    in_mlooking = false;

    if (!freelook->integer && lookspring->integer)
        IN_CenterView();
}

static void IN_HolsterDown(void) { KeyDown(&in_holster); }
static void IN_HolsterUp(void) { KeyUp(&in_holster); }
static void IN_WheelDown(void) { if (cgame && cgame->Wheel_Open) cgame->Wheel_Open(false); }
static void IN_WheelUp(void) { if (cgame && cgame->Wheel_Close) cgame->Wheel_Close(true); }
static void IN_Wheel2Down(void) { if (cgame && cgame->Wheel_Open) cgame->Wheel_Open(true); }
static void IN_Wheel2Up(void) { if (cgame && cgame->Wheel_Close) cgame->Wheel_Close(true); }

static void IN_WeapNext(void)
{
    if (cl.game_api != Q2PROTO_GAME_RERELEASE) {
        Cbuf_AddText(&cmd_buffer, "weapnext\n");
        return;
    }

    if (cgame && cgame->Wheel_WeapNext)
        cgame->Wheel_WeapNext();
    else
        Cbuf_AddText(&cmd_buffer, "weapnext\n");
}

static void IN_WeapPrev(void)
{
    if (cl.game_api != Q2PROTO_GAME_RERELEASE) {
        Cbuf_AddText(&cmd_buffer, "weapprev\n");
        return;
    }

    if (cgame && cgame->Wheel_WeapPrev)
        cgame->Wheel_WeapPrev();
    else
        Cbuf_AddText(&cmd_buffer, "weapprev\n");
}

/*
===============
CL_KeyState

Returns the fraction of the frame that the key was down
===============
*/
static float CL_KeyState(const kbutton_t *key)
{
    unsigned msec = key->msec;

    if (key->state & 1) {
        // still down
        if (com_eventTime > key->downtime) {
            msec += com_eventTime - key->downtime;
        }
    }

    // special case for instant packet
    if (!cl.cmd.msec) {
        return (float)(key->state & 1);
    }

    return Q_clipf((float)msec / cl.cmd.msec, 0, 1);
}

//==========================================================================

static float autosens_x;
static float autosens_y;

static bool IN_GamepadActive(void)
{
    if (!in_gamepad || !in_gamepad->integer)
        return false;
    if (cls.key_dest & (KEY_MENU | KEY_CONSOLE | KEY_MESSAGE))
        return false;
    return true;
}

static void CL_GamepadMove(void)
{
    if (!IN_GamepadActive())
        return;

    float lx = gamepad.axis[IN_GAMEPAD_AXIS_LEFTX];
    float ly = gamepad.axis[IN_GAMEPAD_AXIS_LEFTY];
    float deadzone = in_gamepad_deadzone ? Cvar_ClampValue(in_gamepad_deadzone, 0.0f, 0.95f) : 0.2f;

    IN_ApplyRadialDeadzone(&lx, &ly, deadzone);

    float forward = -ly;
    float side = lx;

    cl.gamepadmove[0] = forward * cl_forwardspeed->value;
    cl.gamepadmove[1] = side * cl_sidespeed->value;

    if ((in_speed.state & 1) ^ cl_run->integer) {
        cl.gamepadmove[0] *= 2.0f;
        cl.gamepadmove[1] *= 2.0f;
    }

    cl.localmove[0] += cl.gamepadmove[0];
    cl.localmove[1] += cl.gamepadmove[1];
}

static void CL_GamepadLook(int msec)
{
    if (!IN_GamepadActive())
        return;
    if (msec <= 0)
        return;

    float rx = gamepad.axis[IN_GAMEPAD_AXIS_RIGHTX];
    float ry = gamepad.axis[IN_GAMEPAD_AXIS_RIGHTY];
    float deadzone = in_gamepad_look_deadzone ? Cvar_ClampValue(in_gamepad_look_deadzone, 0.0f, 0.95f) : 0.15f;

    IN_ApplyRadialDeadzone(&rx, &ry, deadzone);

    if (!rx && !ry)
        return;

    bool wheel_open = cgame && cgame->Wheel_IsOpen && cgame->Wheel_IsOpen();
    if (wheel_open && cgame->Wheel_Input) {
        float wheel_speed = in_gamepad_wheel_speed ? Cvar_ClampValue(in_gamepad_wheel_speed, 0.0f, 10000.0f) : 0.0f;
        if (wheel_speed > 0.0f) {
            float frame = (float)msec * 0.001f;
            int dx = Q_rint(rx * wheel_speed * frame);
            int dy = Q_rint(ry * wheel_speed * frame);
            if (dx || dy)
                cgame->Wheel_Input(dx, dy);
        }
        return;
    }

    float look_scale = in_gamepad_look_sensitivity ? Cvar_ClampValue(in_gamepad_look_sensitivity, 0.0f, 10.0f) : 1.0f;
    float yaw_speed = in_gamepad_yaw ? Cvar_ClampValue(in_gamepad_yaw, 0.0f, 1000.0f) : 140.0f;
    float pitch_speed = in_gamepad_pitch ? Cvar_ClampValue(in_gamepad_pitch, 0.0f, 1000.0f) : 150.0f;
    float invert = (in_gamepad_invert_y && in_gamepad_invert_y->integer) ? -1.0f : 1.0f;
    float frame = (float)msec * 0.001f;

    cl.viewangles[YAW] -= rx * yaw_speed * look_scale * frame;
    cl.viewangles[PITCH] += ry * pitch_speed * look_scale * frame * invert;
}

/*
================
CL_MouseMove
================
*/
static void CL_MouseMove(void)
{
    int dx, dy;
    float mx, my;
    float speed;

    if (!vid || !vid->get_mouse_motion) {
        return;
    }
    if (cls.key_dest & KEY_CONSOLE) {
        if (vid->get_mouse_motion(&dx, &dy) && (dx || dy))
            Con_MouseMove(dx, dy);
        input.old_dx = input.old_dy = 0;
        return;
    }
    if (cls.key_dest & (KEY_MENU | KEY_MESSAGE)) {
        return;
    }
    if (!vid->get_mouse_motion(&dx, &dy)) {
        return;
    }

    if (m_filter->integer) {
        mx = (dx + input.old_dx) * 0.5f;
        my = (dy + input.old_dy) * 0.5f;
    } else {
        mx = dx;
        my = dy;
    }

    input.old_dx = dx;
    input.old_dy = dy;

    bool wheel_open = cgame && cgame->Wheel_IsOpen && cgame->Wheel_IsOpen();
    if (wheel_open && cgame->Wheel_Input)
        cgame->Wheel_Input(dx, dy);

    if (!mx && !my) {
        return;
    }

    Cvar_ClampValue(m_accel, 0, 1);

    speed = sqrtf(mx * mx + my * my);
    speed = sensitivity->value + speed * m_accel->value;

    mx *= speed;
    my *= speed;

    if (m_autosens->integer) {
        mx *= cl.fov_x * autosens_x;
        my *= cl.fov_y * autosens_y;
    }

// add mouse X/Y movement
    if ((in_strafe.state & 1) || (lookstrafe->integer && !in_mlooking)) {
        cl.mousemove[1] += m_side->value * mx;
    } else if (!wheel_open) {
        cl.viewangles[YAW] -= m_yaw->value * mx;
    }

    if ((in_mlooking || freelook->integer) && !(in_strafe.state & 1)) {
        if (!wheel_open) {
            cl.viewangles[PITCH] += m_pitch->value * my;
        }
    } else {
        cl.mousemove[0] -= m_forward->value * my;
    }
}


/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
static void CL_AdjustAngles(int msec)
{
    float speed;

    if (in_speed.state & 1)
        speed = msec * cl_anglespeedkey->value * 0.001f;
    else
        speed = msec * 0.001f;

    if (!(in_strafe.state & 1)) {
        cl.viewangles[YAW] -= speed * cl_yawspeed->value * CL_KeyState(&in_right);
        cl.viewangles[YAW] += speed * cl_yawspeed->value * CL_KeyState(&in_left);
    }
    if (in_klook.state & 1) {
        cl.viewangles[PITCH] -= speed * cl_pitchspeed->value * CL_KeyState(&in_forward);
        cl.viewangles[PITCH] += speed * cl_pitchspeed->value * CL_KeyState(&in_back);
    }

    cl.viewangles[PITCH] -= speed * cl_pitchspeed->value * CL_KeyState(&in_lookup);
    cl.viewangles[PITCH] += speed * cl_pitchspeed->value * CL_KeyState(&in_lookdown);
}

/*
================
CL_BaseMove

Build the intended movement vector
================
*/
static void CL_BaseMove(vec2_t move)
{
    if (in_strafe.state & 1) {
        move[1] += cl_sidespeed->value * CL_KeyState(&in_right);
        move[1] -= cl_sidespeed->value * CL_KeyState(&in_left);
    }

    move[1] += cl_sidespeed->value * CL_KeyState(&in_moveright);
    move[1] -= cl_sidespeed->value * CL_KeyState(&in_moveleft);

    if (!(in_klook.state & 1)) {
        move[0] += cl_forwardspeed->value * CL_KeyState(&in_forward);
        move[0] -= cl_forwardspeed->value * CL_KeyState(&in_back);
    }

// adjust for speed key / running
    if ((in_speed.state & 1) ^ cl_run->integer) {
        Vector2Scale(move, 2, move);
    }
}

static void CL_ClampSpeed(vec2_t move)
{
    const float speed = 400;    // default (maximum) running speed

    move[0] = Q_clipf(move[0], -speed, speed);
    move[1] = Q_clipf(move[1], -speed, speed);
}

static void CL_ClampPitch(void)
{
    float pitch, angle;

    pitch = cl.frame.ps.pmove.delta_angles[PITCH];
    angle = cl.viewangles[PITCH] + pitch;

    if (angle < -180)
        angle += 360; // wrapped
    if (angle > 180)
        angle -= 360; // wrapped

    angle = Q_clipf(angle, -89, 89);
    cl.viewangles[PITCH] = angle - pitch;
}

/*
=================
CL_UpdateCmd

Updates msec, angles and builds interpolated movement vector for local prediction.
Doesn't touch command forward/side/upmove, these are filled by CL_FinalizeCmd.
=================
*/
void CL_UpdateCmd(int msec)
{
    Vector2Clear(cl.localmove);
    Vector2Clear(cl.gamepadmove);

    if (sv_paused->integer) {
        return;
    }

    // add to milliseconds of time to apply the move
    cl.cmd.msec += msec;

    // adjust viewangles
    CL_AdjustAngles(msec);

    // get basic movement from keyboard, including jump/crouch
    CL_BaseMove(cl.localmove);
    CL_GamepadMove();
    CL_GamepadLook(msec);
    if (in_up.state & 3)
        cl.cmd.buttons |= BUTTON_JUMP;
    if (in_down.state & 3)
        cl.cmd.buttons |= BUTTON_CROUCH;

    // allow mice to add to the move
    CL_MouseMove();

    // add accumulated mouse forward/side movement
    cl.localmove[0] += cl.mousemove[0];
    cl.localmove[1] += cl.mousemove[1];

    // clamp to server defined max speed
    CL_ClampSpeed(cl.localmove);

    CL_ClampPitch();

    cl.cmd.angles[0] = cl.viewangles[0];
    cl.cmd.angles[1] = cl.viewangles[1];
    cl.cmd.angles[2] = cl.viewangles[2];
}

static void m_autosens_changed(cvar_t *self)
{
    float fov;

    if (self->value > 90.0f && self->value <= 179.0f)
        fov = self->value;
    else
        fov = 90.0f;

    autosens_x = 1.0f / fov;
    autosens_y = 1.0f / V_CalcFov(fov, 4, 3);
}

static const cmdreg_t c_input[] = {
    { "centerview", IN_CenterView },
    { "+moveup", IN_UpDown },
    { "-moveup", IN_UpUp },
    { "+movedown", IN_DownDown },
    { "-movedown", IN_DownUp },
    { "+left", IN_LeftDown },
    { "-left", IN_LeftUp },
    { "+right", IN_RightDown },
    { "-right", IN_RightUp },
    { "+forward", IN_ForwardDown },
    { "-forward", IN_ForwardUp },
    { "+back", IN_BackDown },
    { "-back", IN_BackUp },
    { "+lookup", IN_LookupDown },
    { "-lookup", IN_LookupUp },
    { "+lookdown", IN_LookdownDown },
    { "-lookdown", IN_LookdownUp },
    { "+strafe", IN_StrafeDown },
    { "-strafe", IN_StrafeUp },
    { "+moveleft", IN_MoveleftDown },
    { "-moveleft", IN_MoveleftUp },
    { "+moveright", IN_MoverightDown },
    { "-moveright", IN_MoverightUp },
    { "+speed", IN_SpeedDown },
    { "-speed", IN_SpeedUp },
    { "+attack", IN_AttackDown },
    { "-attack", IN_AttackUp },
    { "+hook", IN_HookDown },
    { "-hook", IN_HookUp },
    { "+use", IN_UseDown },
    { "-use", IN_UseUp },
    { "impulse", IN_Impulse },
    { "+klook", IN_KLookDown },
    { "-klook", IN_KLookUp },
    { "+mlook", IN_MLookDown },
    { "-mlook", IN_MLookUp },
    { "in_restart", IN_Restart_f },
    // Kex stuff
    { "+holster", IN_HolsterDown },
    { "-holster", IN_HolsterUp },
    { "+wheel", IN_WheelDown },
    { "-wheel", IN_WheelUp },
    { "+wheel2", IN_Wheel2Down },
    { "-wheel2", IN_Wheel2Up },
    { "cl_weapnext", IN_WeapNext },
    { "cl_weapprev", IN_WeapPrev },
    { "cl_adaptive_input_status", CL_AdaptiveInputStatus_f },
    { "cl_snapshot_shadow_status", CL_SnapshotShadowStatus_f },
    { "cl_snapshot_recovery_status", CL_SnapshotRecoveryStatus_f },
    { "cl_worr_capability_status", CL_WorrCapabilityStatus_f },
    { "cl_worr_native_shadow_status", CL_NativeReadinessPilotStatus_f },
    { "cl_worr_native_input_delivery_status",
      CL_NativeInputDeliveryStatus_f },
    { "cl_worr_native_event_probe_status", CL_NativeEventProbeStatus_f },
    { "cl_worr_native_event_probe_checkpoint",
      CL_NativeEventProbeCheckpoint_f },
    { NULL }
};

/*
============
CL_RegisterInput
============
*/
void CL_RegisterInput(void)
{
    Cmd_Register(c_input);

    cl_nodelta = Cvar_Get("cl_nodelta", "0", 0);
    cl_maxpackets = Cvar_Get("cl_maxpackets", "60", 0);
    cl_fuzzhack = Cvar_Get("cl_fuzzhack", "0", 0);
    cl_packetdup = Cvar_Get("cl_packetdup", "1", 0);
    cl_adaptive_input =
        Cvar_Get("cl_adaptive_input", "0", CVAR_ARCHIVE);
#if USE_DEBUG
    cl_showpackets = Cvar_Get("cl_showpackets", "0", 0);
#endif
    cl_instantpacket = Cvar_Get("cl_instantpacket", "1", 0);
    cl_batchcmds = Cvar_Get("cl_batchcmds", "1", 0);
    CL_SnapshotRecoveryInit();

    CL_AdaptiveInputReset();

    cl_upspeed = Cvar_Get("cl_upspeed", "200", 0);
    cl_forwardspeed = Cvar_Get("cl_forwardspeed", "200", 0);
    cl_sidespeed = Cvar_Get("cl_sidespeed", "200", 0);
    cl_yawspeed = Cvar_Get("cl_yawspeed", "140", 0);
    cl_pitchspeed = Cvar_Get("cl_pitchspeed", "150", CVAR_CHEAT);
    cl_anglespeedkey = Cvar_Get("cl_anglespeedkey", "1.5", CVAR_CHEAT);
    cl_run = Cvar_Get("cl_run", "1", CVAR_ARCHIVE);

    freelook = Cvar_Get("freelook", "1", CVAR_ARCHIVE);
    lookspring = Cvar_Get("lookspring", "0", CVAR_ARCHIVE);
    lookstrafe = Cvar_Get("lookstrafe", "0", CVAR_ARCHIVE);
    sensitivity = Cvar_Get("sensitivity", "3", CVAR_ARCHIVE);

    m_pitch = Cvar_Get("m_pitch", "0.022", CVAR_ARCHIVE);
    m_yaw = Cvar_Get("m_yaw", "0.022", 0);
    m_forward = Cvar_Get("m_forward", "1", 0);
    m_side = Cvar_Get("m_side", "1", 0);
    m_filter = Cvar_Get("m_filter", "0", 0);
    m_accel = Cvar_Get("m_accel", "0", 0);
    m_autosens = Cvar_Get("m_autosens", "0", 0);
    m_autosens->changed = m_autosens_changed;
    m_autosens_changed(m_autosens);

    in_gamepad = Cvar_Get("in_gamepad", "1", CVAR_ARCHIVE);
    in_gamepad->changed = in_gamepad_changed;
    in_gamepad_deadzone = Cvar_Get("in_gamepad_deadzone", "0.2", CVAR_ARCHIVE);
    in_gamepad_look_deadzone = Cvar_Get("in_gamepad_look_deadzone", "0.15", CVAR_ARCHIVE);
    in_gamepad_trigger_threshold = Cvar_Get("in_gamepad_trigger_threshold", "0.2", CVAR_ARCHIVE);
    in_gamepad_yaw = Cvar_Get("in_gamepad_yaw", "140", CVAR_ARCHIVE);
    in_gamepad_pitch = Cvar_Get("in_gamepad_pitch", "150", CVAR_ARCHIVE);
    in_gamepad_look_sensitivity = Cvar_Get("in_gamepad_look_sensitivity", "1.0", CVAR_ARCHIVE);
    in_gamepad_invert_y = Cvar_Get("in_gamepad_invert_y", "0", CVAR_ARCHIVE);
    in_gamepad_wheel_speed = Cvar_Get("in_gamepad_wheel_speed", "1200", CVAR_ARCHIVE);
}

/*
=================
CL_FinalizeCmd

Builds the actual movement vector for sending to server. Assumes that msec
and angles are already set for this frame by CL_UpdateCmd.
=================
*/
void CL_FinalizeCmd(void)
{
    vec2_t move;
    bool allow_attack = true;

    // command buffer ticks in sync with cl_maxfps
    Cbuf_Frame(&cmd_buffer);
    Cbuf_Frame(&cl_cmdbuf);

    if (cls.state != ca_active) {
        goto clear; // not talking to a server
    }

    if (sv_paused->integer) {
        goto clear;
    }

//
// figure button bits
//
    if (cgame && cgame->Wheel_AllowAttack)
        allow_attack = cgame->Wheel_AllowAttack();
    if (in_attack.state & 3 && allow_attack)
        cl.cmd.buttons |= BUTTON_ATTACK;
    if (in_hook.state & 3)
        cl.cmd.buttons |= BUTTON_HOOK;
    if (in_use.state & 3)
        cl.cmd.buttons |= BUTTON_USE;
    if (in_holster.state & 3)
        cl.cmd.buttons |= BUTTON_HOLSTER;
    if (in_up.state & 3)
        cl.cmd.buttons |= BUTTON_JUMP;
    if (in_down.state & 3)
        cl.cmd.buttons |= BUTTON_CROUCH;

    if (cgame && cgame->Wheel_ApplyButtons)
        cgame->Wheel_ApplyButtons(&cl.cmd.buttons);

    if (cls.key_dest == KEY_GAME && Key_AnyKeyDown()) {
        cl.cmd.buttons |= BUTTON_ANY;
    }

    if (cl.cmd.msec > 250) {
        cl.cmd.msec = 100;        // time was unreasonable
    }

    // rebuild the movement vector
    Vector2Clear(move);

    // get basic movement from keyboard
    CL_BaseMove(move);

    // add gamepad forward/side movement
    move[0] += cl.gamepadmove[0];
    move[1] += cl.gamepadmove[1];

    // add mouse forward/side movement
    move[0] += cl.mousemove[0];
    move[1] += cl.mousemove[1];

    // clamp to server defined max speed
    CL_ClampSpeed(move);

    // store the movement vector
    cl.cmd.forwardmove = move[0];
    cl.cmd.sidemove = move[1];

    // update wheels before we save it off
    if (cgame && cgame->WeaponBar_Input)
        cgame->WeaponBar_Input(&cl.frame.ps, &cl.cmd.buttons);

    /* Store exactly the command representation prediction and wire share. */
    if (!NetUsercmd_CanonicalizeForTransport(
            &cl.cmd, cls.q2proto_ctx.features.has_upmove)) {
        Com_WPrintf("%s: rejected non-canonical user command\n", __func__);
        goto clear;
    }

    {
        // save this command off for prediction
        cl.cmdNumber++;
        cl.cmds[cl.cmdNumber & CMD_MASK] = cl.cmd;
        const bool identity_finalized =
            CL_CommandIdentityFinalize(cl.cmdNumber);
        if (!identity_finalized && CL_NetCapabilityHas(
                WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1)) {
            Com_Error(ERR_DROP, "%s: canonical command identity exhausted",
                      __func__);
        }
        if (identity_finalized) {
            worr_command_id_v1 command_id = {};
            worr_prediction_command_v1 prediction_command = {};
            if (CL_CommandIdentityForNumber(cl.cmdNumber, &command_id) &&
                NetUsercmd_ToPredictionCommandV1(
                    &cl.cmd, &prediction_command) &&
                CL_CommandIdentityRetainCommand(cl.cmdNumber,
                                                &prediction_command)) {
                CL_NativeReadinessPilotObserveFinalizedCommand(
                    cl.cmdNumber, &command_id, &prediction_command);
            } else {
                if (CL_NetCapabilityHas(
                        WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1)) {
                    Com_Error(ERR_DROP,
                              "%s: failed to retain canonical command record",
                              __func__);
                }
                CL_NativeReadinessPilotObserveFinalizedCommand(
                    cl.cmdNumber, nullptr, nullptr);
            }
        }
    }

clear:
    // clear pending cmd
    memset(&cl.cmd, 0, sizeof(cl.cmd));

    // clear all states
    cl.mousemove[0] = 0;
    cl.mousemove[1] = 0;
    cl.gamepadmove[0] = 0;
    cl.gamepadmove[1] = 0;

    in_attack.state &= ~2;
    in_hook.state &= ~2;
    in_use.state &= ~2;
    in_holster.state &= ~2;

    KeyClear(&in_right);
    KeyClear(&in_left);

    KeyClear(&in_moveright);
    KeyClear(&in_moveleft);

    KeyClear(&in_up);
    KeyClear(&in_down);

    KeyClear(&in_forward);
    KeyClear(&in_back);

    KeyClear(&in_lookup);
    KeyClear(&in_lookdown);

    in_impulse = 0;
}

static bool CL_AdaptiveInputEvaluate(bool transport_supported)
{
    worr_adaptive_input_observation_v1 observation = {};
    uint32_t queued_commands;
    uint32_t unacknowledged_packets;
    uint32_t result;

    Cvar_ClampInteger(cl_adaptive_input, 0, 1);
    if (!cl_adaptive_input->integer || !transport_supported) {
        if (adaptive_input.active) {
            Worr_AdaptiveInputResetV1(&adaptive_input.state);
            memset(&adaptive_input.output, 0,
                   sizeof(adaptive_input.output));
        }
        adaptive_input.active = false;
        adaptive_input.decision_valid = false;
        return false;
    }

    if (!adaptive_input.active) {
        Worr_AdaptiveInputDefaultConfigV1(&adaptive_input.config);
        Worr_AdaptiveInputResetV1(&adaptive_input.state);
        memset(&adaptive_input.output, 0,
               sizeof(adaptive_input.output));
        adaptive_input.active = true;
        adaptive_input.decision_valid = false;
    }

    if (cl_maxpackets->integer != 0 && cl_maxpackets->integer < 10)
        Cvar_Set("cl_maxpackets", "10");
    Cvar_ClampInteger(cl_packetdup, 0, MAX_PACKET_FRAMES - 1);

    queued_commands = cl.cmdNumber - cl.lastTransmitCmdNumberReal;
    if (queued_commands > CMD_BACKUP)
        queued_commands = CMD_BACKUP;
    if (cls.netchan.outgoing_sequence >=
        cls.netchan.incoming_acknowledged) {
        unacknowledged_packets = cls.netchan.outgoing_sequence -
                                 cls.netchan.incoming_acknowledged;
        /* outgoing_sequence names the next packet, so the acknowledged
         * distance includes one packet that has not been sent yet. */
        if (unacknowledged_packets != 0 &&
            !cls.netchan.fragment_pending)
            --unacknowledged_packets;
    } else {
        unacknowledged_packets = 0;
    }
    if (unacknowledged_packets > CMD_BACKUP)
        unacknowledged_packets = CMD_BACKUP;

    observation.struct_size = sizeof(observation);
    observation.schema_version = WORR_ADAPTIVE_INPUT_VERSION;
    observation.sample_time_ms = cls.realtime;
    /* netchan.total_received includes inferred drops.  The policy core takes
     * successful and dropped counters separately; unsigned subtraction also
     * preserves the successful counter across ordinary 32-bit wrap. */
    observation.total_received_packets =
        cls.netchan.total_received - cls.netchan.total_dropped;
    observation.total_dropped_packets = cls.netchan.total_dropped;
    observation.smoothed_rtt_ms =
        (uint32_t)Q_clip(cls.measure.ping, 0, 60000);
    observation.queued_commands = queued_commands;
    observation.unacknowledged_packets = unacknowledged_packets;
    observation.rate_bytes_per_second =
        info_rate
            ? (uint32_t)Q_clip(info_rate->integer, 0, 1073741824)
            : 0;
    observation.configured_packets_per_second =
        (uint32_t)max(cl_maxpackets->integer, 0);
    observation.configured_redundancy_frames =
        (uint32_t)cl_packetdup->integer;
    observation.maximum_redundancy_frames = MAX_PACKET_FRAMES - 1;
    if (cls.q2proto_ctx.features.batch_move && cl_batchcmds->integer) {
        observation.flags |=
            WORR_ADAPTIVE_INPUT_OBSERVATION_BATCH_REDUNDANCY;
    }

    result = Worr_AdaptiveInputEvaluateV1(
        &adaptive_input.state, &adaptive_input.config, &observation,
        &adaptive_input.output);
    switch (result) {
    case WORR_ADAPTIVE_INPUT_OK:
    case WORR_ADAPTIVE_INPUT_HELD:
    case WORR_ADAPTIVE_INPUT_COUNTER_RESET:
    case WORR_ADAPTIVE_INPUT_CLOCK_RESET:
        adaptive_input.decision_valid =
            (adaptive_input.output.flags &
             WORR_ADAPTIVE_INPUT_OUTPUT_VALID) != 0;
        return adaptive_input.decision_valid;
    default:
        adaptive_input.decision_valid = false;
        if (adaptive_input.integration_fallbacks != UINT64_MAX)
            ++adaptive_input.integration_fallbacks;
        return false;
    }
}

static void CL_NativeReadinessPilotStatus_f(void)
{
    cl_native_readiness_pilot_status_v1 status = {};
    if (!CL_NativeReadinessPilotGetStatusV1(&status)) {
        Com_Printf("WORR_NATIVE_CLIENT_STATUS_V1 schema=1 failures=1 "
                   "last_failure=4294967295\n");
        return;
    }

    Com_Printf(
        "WORR_NATIVE_CLIENT_STATUS_V1 schema=%u enabled=%u mode=%u "
        "hooks=%u capability_confirmed=%u readiness_phase=%u "
        "official_epoch=%u transport_epoch=%u protocol=%u "
        "public_mask=0x%02x private_mask=0x%02x probe_hold=%u "
        "cancelled_through_epoch=%u cancellation_barriers=%llu "
        "cancelled_transports=%llu cancelled_command_tx=%llu "
        "cancelled_event_rx=%llu cancelled_event_receipts=%llu "
        "stale_cancelled_carriers=%llu "
        "stale_cancelled_readiness_records=%llu "
        "challenges=%llu client_ready_queued=%llu server_active=%llu "
        "proof_enqueued=%llu retained=%llu retained_highwater=%llu "
        "retained_releases=%llu tx_first_sends=%llu tx_retries=%llu "
        "tx_handoffs=%llu ack_carriers=%llu "
        "acknowledged_reliable=%llu drains=%llu failures=%llu "
        "last_failure=%u\n",
        (unsigned)status.schema_version, (unsigned)status.enabled,
        (unsigned)status.mode, (unsigned)status.hooks,
        (unsigned)status.capability_confirmed,
        (unsigned)status.readiness_phase, (unsigned)status.official_epoch,
        (unsigned)status.transport_epoch, (unsigned)status.protocol,
        (unsigned)status.public_mask, (unsigned)status.private_mask,
        (unsigned)status.probe_hold,
        (unsigned)status.cancelled_through_transport_epoch,
        (unsigned long long)status.cancellation_barriers,
        (unsigned long long)status.cancelled_transports,
        (unsigned long long)status.cancelled_command_tx,
        (unsigned long long)status.cancelled_event_rx,
        (unsigned long long)status.cancelled_event_receipts,
        (unsigned long long)status.stale_cancelled_carriers,
        (unsigned long long)status.stale_cancelled_readiness_records,
        (unsigned long long)status.challenges,
        (unsigned long long)status.client_ready_queued,
        (unsigned long long)status.server_active,
        (unsigned long long)status.proof_enqueued,
        (unsigned long long)status.retained,
        (unsigned long long)status.retained_highwater,
        (unsigned long long)status.retained_releases,
        (unsigned long long)status.tx_first_sends,
        (unsigned long long)status.tx_retries,
        (unsigned long long)status.tx_handoffs,
        (unsigned long long)status.ack_carriers,
        (unsigned long long)status.acknowledged_reliable,
        (unsigned long long)status.drains,
        (unsigned long long)status.failures,
        (unsigned)status.last_failure);
    CL_NativeReadinessPilotSnapshotStatus_f();
}

static void CL_WorrCapabilityStatus_f(void)
{
    worr_net_capability_state_v1 state = {};
    const bool valid = CL_NetCapabilityGetState(&state);

    Com_Printf(
        "WORR_CAPABILITY_CLIENT_STATUS_V1 schema=1 valid=%u phase=%u "
        "protocol=%d epoch=%u offered=0x%02x supported=0x%02x "
        "peer_supported=0x%02x negotiated=0x%02x\n",
        valid ? 1u : 0u,
        valid ? (unsigned)state.phase : 0u,
        cls.serverProtocol,
        valid ? (unsigned)state.connection_epoch : 0u,
        valid ? (unsigned)state.offered : 0u,
        valid ? (unsigned)state.supported : 0u,
        valid ? (unsigned)state.peer_supported : 0u,
        valid ? (unsigned)state.negotiated : 0u);
}

static void CL_NativeInputDeliveryStatus_f(void)
{
    cl_native_input_batch_status_v1 status = {};
    if (!CL_NativeReadinessPilotGetInputBatchStatusV1(&status)) {
        Com_Printf("WORR_NATIVE_INPUT_DELIVERY_STATUS_V1 schema=1 "
                   "failures=1\n");
        return;
    }
    Com_Printf(
        "WORR_NATIVE_INPUT_DELIVERY_STATUS_V1 schema=%u requested=%u "
        "confirmed=%u enabled=%u drained=%u official_epoch=%u "
        "transport_epoch=%u received_sequence=%u candidates=%u "
        "active_first=%u active_last=%u active_commands=%u "
        "active_handoffs=%u candidates_collected=%llu plans=%llu "
        "batches_encoded=%llu prepare_fallbacks=%llu "
        "first_handoffs=%llu retry_handoffs=%llu "
        "batches_acknowledged=%llu commands_acknowledged=%llu "
        "retry_exhaustions=%llu failures=%llu\n",
        (unsigned)status.schema_version, (unsigned)status.requested,
        (unsigned)status.confirmed, (unsigned)status.enabled,
        (unsigned)status.drained, (unsigned)status.official_epoch,
        (unsigned)status.transport_epoch,
        (unsigned)status.received_sequence,
        (unsigned)status.candidate_count,
        (unsigned)status.active_first_sequence,
        (unsigned)status.active_last_sequence,
        (unsigned)status.active_command_count,
        (unsigned)status.active_handoffs,
        (unsigned long long)status.candidates_collected,
        (unsigned long long)status.plans,
        (unsigned long long)status.batches_encoded,
        (unsigned long long)status.prepare_fallbacks,
        (unsigned long long)status.first_handoffs,
        (unsigned long long)status.retry_handoffs,
        (unsigned long long)status.batches_acknowledged,
        (unsigned long long)status.commands_acknowledged,
        (unsigned long long)status.retry_exhaustions,
        (unsigned long long)status.failures);
}

static void CL_NativeEventProbeStatus_f(void)
{
    worr_cgame_native_event_probe_status_v1 status = {};
    if (!CL_CGameNativeEventProbeGetStatus(&status) ||
        status.struct_size != sizeof(status) ||
        status.schema_version !=
            WORR_CGAME_NATIVE_EVENT_PROBE_STATUS_VERSION ||
        status.kind_count != WORR_CGAME_NATIVE_EVENT_PROBE_KIND_COUNT) {
        Com_Printf("WORR_NATIVE_EVENT_PROBE_STATUS_V1 valid=0\n");
        return;
    }

    Com_Printf(
        "WORR_NATIVE_EVENT_PROBE_STATUS_V1 valid=1 schema=%u size=%u "
        "kind_count=%u map_generation=%u map_end_count=%u map_active=%u "
        "probe_requested=%u probe_latched=%u probe_active=%u "
        "effect_authority_enabled=%u resources_required=%u "
        "legacy_owner_active=%u raw_pending_count=%u authority_epoch=%u "
        "authority_requires_resync=%u authority_degraded=%u "
        "raw_action_records=%llu raw_action_chain_hash=%016llx "
        "raw_effect_dispatches=%llu raw_effect_chain_hash=%016llx "
        "raw_effect_suppressions=%llu raw_pair_failures=%llu "
        "probe_action_commits=%llu probe_action_chain_hash=%016llx "
        "probe_effects_suppressed=%llu probe_nonvisual_commits=%llu "
        "native_effect_dispatches=%llu native_effect_chain_hash=%016llx "
        "presenter_commit_mismatches=%llu "
        "authoritative_presentations=%llu authoritative_duplicates=%llu "
        "authoritative_conflicts=%llu authority_ref_body_joins=%llu "
        "legacy_ref_body_mismatches=%llu "
        "raw_k0=%llu raw_k1=%llu raw_k2=%llu raw_k3=%llu raw_k4=%llu "
        "raw_k5=%llu raw_k6=%llu raw_k7=%llu probe_k0=%llu probe_k1=%llu "
        "probe_k2=%llu probe_k3=%llu probe_k4=%llu probe_k5=%llu "
        "probe_k6=%llu probe_k7=%llu\n",
        (unsigned)status.schema_version, (unsigned)status.struct_size,
        (unsigned)status.kind_count, (unsigned)status.map_generation,
        (unsigned)status.map_end_count, (unsigned)status.map_active,
        (unsigned)status.probe_requested, (unsigned)status.probe_latched,
        (unsigned)status.probe_active,
        (unsigned)status.effect_authority_enabled,
        (unsigned)status.resources_required,
        (unsigned)status.legacy_owner_active,
        (unsigned)status.raw_pending_count, (unsigned)status.authority_epoch,
        (unsigned)status.authority_requires_resync,
        (unsigned)status.authority_degraded,
        (unsigned long long)status.raw_action_records,
        (unsigned long long)status.raw_action_chain_hash,
        (unsigned long long)status.raw_effect_dispatches,
        (unsigned long long)status.raw_effect_chain_hash,
        (unsigned long long)status.raw_effect_suppressions,
        (unsigned long long)status.raw_pair_failures,
        (unsigned long long)status.probe_action_commits,
        (unsigned long long)status.probe_action_chain_hash,
        (unsigned long long)status.probe_effects_suppressed,
        (unsigned long long)status.probe_nonvisual_commits,
        (unsigned long long)status.native_effect_dispatches,
        (unsigned long long)status.native_effect_chain_hash,
        (unsigned long long)status.presenter_commit_mismatches,
        (unsigned long long)status.authoritative_presentations,
        (unsigned long long)status.authoritative_duplicates,
        (unsigned long long)status.authoritative_conflicts,
        (unsigned long long)status.authority_ref_body_joins,
        (unsigned long long)status.legacy_ref_body_mismatches,
        (unsigned long long)status.raw_action_by_kind[0],
        (unsigned long long)status.raw_action_by_kind[1],
        (unsigned long long)status.raw_action_by_kind[2],
        (unsigned long long)status.raw_action_by_kind[3],
        (unsigned long long)status.raw_action_by_kind[4],
        (unsigned long long)status.raw_action_by_kind[5],
        (unsigned long long)status.raw_action_by_kind[6],
        (unsigned long long)status.raw_action_by_kind[7],
        (unsigned long long)status.probe_action_by_kind[0],
        (unsigned long long)status.probe_action_by_kind[1],
        (unsigned long long)status.probe_action_by_kind[2],
        (unsigned long long)status.probe_action_by_kind[3],
        (unsigned long long)status.probe_action_by_kind[4],
        (unsigned long long)status.probe_action_by_kind[5],
        (unsigned long long)status.probe_action_by_kind[6],
        (unsigned long long)status.probe_action_by_kind[7]);
}

static bool CL_ParseNativeEventProbeDecimal(
    const char *text, uint64_t maximum, uint64_t *value_out)
{
    if (!text || !text[0] || !value_out)
        return false;
    uint64_t value = 0;
    for (const unsigned char *cursor =
             reinterpret_cast<const unsigned char *>(text);
         *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9')
            return false;
        const uint64_t digit = *cursor - '0';
        if (value > (maximum - digit) / 10u)
            return false;
        value = value * 10u + digit;
    }
    *value_out = value;
    return true;
}

static void CL_PrintNativeEventProbeCheckpoint(
    bool valid,
    const worr_cgame_native_event_probe_checkpoint_receipt_v1 *receipt)
{
    const worr_cgame_native_event_probe_checkpoint_receipt_v1 invalid = {
        sizeof(worr_cgame_native_event_probe_checkpoint_receipt_v1),
        WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_VERSION,
        WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_INVALID_ARGUMENT,
        0,
        0,
        0,
        0,
    };
    const auto *row = receipt ? receipt : &invalid;
    Com_Printf(
        "WORR_NATIVE_EVENT_PROBE_CHECKPOINT_V1 valid=%u schema=%u "
        "size=%u result=%u map_generation=%u authority_epoch=%u "
        "checkpoint_id=%llu\n",
        valid ? 1u : 0u, (unsigned)row->schema_version,
        (unsigned)row->struct_size, (unsigned)row->result,
        (unsigned)row->observed_map_generation,
        (unsigned)row->observed_authority_epoch,
        (unsigned long long)row->checkpoint_id);
}

static void CL_NativeEventProbeCheckpoint_f(void)
{
    uint64_t map_generation = 0;
    uint64_t authority_epoch = 0;
    uint64_t checkpoint_id = 0;
    if (Cmd_Argc() != 4 ||
        !CL_ParseNativeEventProbeDecimal(
            Cmd_Argv(1), UINT32_MAX, &map_generation) ||
        !CL_ParseNativeEventProbeDecimal(
            Cmd_Argv(2), UINT32_MAX, &authority_epoch) ||
        !CL_ParseNativeEventProbeDecimal(
            Cmd_Argv(3), UINT64_MAX, &checkpoint_id)) {
        CL_PrintNativeEventProbeCheckpoint(false, NULL);
        return;
    }

    worr_cgame_native_event_probe_checkpoint_receipt_v1 receipt = {};
    const bool invoked = CL_CGameNativeEventProbeCheckpoint(
        (uint32_t)map_generation, (uint32_t)authority_epoch,
        checkpoint_id, &receipt);
    const bool valid = invoked && receipt.struct_size == sizeof(receipt) &&
        receipt.schema_version ==
            WORR_CGAME_NATIVE_EVENT_PROBE_CHECKPOINT_VERSION &&
        receipt.reserved0 == 0;
    CL_PrintNativeEventProbeCheckpoint(valid, valid ? &receipt : NULL);
}

static void CL_AdaptiveInputStatus_f(void)
{
    static const struct {
        uint32_t bit;
        const char *name;
    } reason_names[] = {
        { WORR_ADAPTIVE_INPUT_REASON_COLD_START, "cold_start" },
        { WORR_ADAPTIVE_INPUT_REASON_LOSS_ELEVATED, "loss_elevated" },
        { WORR_ADAPTIVE_INPUT_REASON_LOSS_SEVERE, "loss_severe" },
        { WORR_ADAPTIVE_INPUT_REASON_RTT_HIGH, "rtt_high" },
        { WORR_ADAPTIVE_INPUT_REASON_JITTER_HIGH, "jitter_high" },
        { WORR_ADAPTIVE_INPUT_REASON_COMMAND_QUEUE, "command_queue" },
        { WORR_ADAPTIVE_INPUT_REASON_COMMAND_QUEUE_CRITICAL,
          "command_queue_critical" },
        { WORR_ADAPTIVE_INPUT_REASON_ACK_BACKLOG, "ack_backlog" },
        { WORR_ADAPTIVE_INPUT_REASON_ACK_BACKLOG_CRITICAL,
          "ack_backlog_critical" },
        { WORR_ADAPTIVE_INPUT_REASON_RATE_CONSTRAINED,
          "rate_constrained" },
        { WORR_ADAPTIVE_INPUT_REASON_RATE_CRITICAL, "rate_critical" },
        { WORR_ADAPTIVE_INPUT_REASON_RECOVERY_HOLD, "recovery_hold" },
        { WORR_ADAPTIVE_INPUT_REASON_COUNTER_RESET, "counter_reset" },
        { WORR_ADAPTIVE_INPUT_REASON_CLOCK_RESET, "clock_reset" },
        { WORR_ADAPTIVE_INPUT_REASON_PACING_CLAMPED, "pacing_clamped" },
        { WORR_ADAPTIVE_INPUT_REASON_REDUNDANCY_CLAMPED,
          "redundancy_clamped" },
        { WORR_ADAPTIVE_INPUT_REASON_NO_BATCH_REDUNDANCY,
          "no_batch_redundancy" },
        { WORR_ADAPTIVE_INPUT_REASON_NO_PACING_LIMIT,
          "no_pacing_limit" },
        { WORR_ADAPTIVE_INPUT_REASON_INSUFFICIENT_LOSS_SAMPLE,
          "insufficient_loss_sample" },
    };
    worr_adaptive_input_telemetry_v1 telemetry = {};
    size_t i;

    Com_Printf(
        "adaptive input: enabled=%d active=%d decision_valid=%d "
        "fallbacks=%llu\n",
        cl_adaptive_input ? cl_adaptive_input->integer : 0,
        adaptive_input.active ? 1 : 0,
        adaptive_input.decision_valid ? 1 : 0,
        (unsigned long long)adaptive_input.integration_fallbacks);
    if (!adaptive_input.active || !adaptive_input.decision_valid) {
        Com_Printf("  legacy policy: cl_maxpackets=%d cl_packetdup=%d\n",
                   cl_maxpackets ? cl_maxpackets->integer : 0,
                   cl_packetdup ? cl_packetdup->integer : 0);
        return;
    }
    if (Worr_AdaptiveInputGetTelemetryV1(
            &adaptive_input.state, &telemetry) != WORR_ADAPTIVE_INPUT_OK) {
        Com_Printf("  telemetry unavailable\n");
        return;
    }
    Com_Printf(
        "  decision=%llu pps=%u interval=%ums redundancy=%u "
        "loss=%u.%02u%% rtt=%ums jitter=%ums queue=%u ack_backlog=%u "
        "rate=%u result=%u\n",
        (unsigned long long)adaptive_input.output.decision_serial,
        adaptive_input.output.packets_per_second,
        adaptive_input.output.send_interval_ms,
        adaptive_input.output.redundancy_frames,
        adaptive_input.output.smoothed_loss_basis_points / 100,
        adaptive_input.output.smoothed_loss_basis_points % 100,
        adaptive_input.output.smoothed_rtt_ms,
        adaptive_input.output.smoothed_jitter_ms,
        adaptive_input.output.queued_commands,
        adaptive_input.output.unacknowledged_packets,
        adaptive_input.output.rate_bytes_per_second,
        adaptive_input.output.result);
    Com_Printf(
        "  evaluations=%llu windows=%llu held=%llu recovery_holds=%llu "
        "counter_resets=%llu clock_resets=%llu received=%llu dropped=%llu\n",
        (unsigned long long)telemetry.evaluate_calls,
        (unsigned long long)telemetry.window_evaluations,
        (unsigned long long)telemetry.held_evaluations,
        (unsigned long long)telemetry.recovery_holds,
        (unsigned long long)telemetry.counter_resets,
        (unsigned long long)telemetry.clock_resets,
        (unsigned long long)telemetry.cumulative_window_received,
        (unsigned long long)telemetry.cumulative_window_dropped);
    Com_Printf("  reasons:");
    if (!adaptive_input.output.reason_mask)
        Com_Printf(" stable");
    for (i = 0; i < q_countof(reason_names); ++i) {
        if (adaptive_input.output.reason_mask & reason_names[i].bit)
            Com_Printf(" %s", reason_names[i].name);
    }
    Com_Printf(" (0x%08x)\n", adaptive_input.output.reason_mask);
}

static inline bool ready_to_send(bool apply_adaptive_policy)
{
    unsigned msec;
    const bool adaptive =
        CL_AdaptiveInputEvaluate(apply_adaptive_policy);

    if (cl.sendPacketNow) {
        return true;
    }
    if (cls.netchan.message.cursize || cls.netchan.reliable_ack_pending) {
        return true;
    }
    /* Zero is an immediate operator request for unlimited pacing, including
     * while the 100 ms adaptive decision window is being held. */
    if (!cl_maxpackets->integer) {
        return true;
    }
    if (adaptive && adaptive_input.output.packets_per_second == 0) {
        return true;
    }

    if (adaptive) {
        msec = adaptive_input.output.send_interval_ms;
    } else {
        if (cl_maxpackets->integer < 10) {
            Cvar_Set("cl_maxpackets", "10");
        }
        msec = 1000 / cl_maxpackets->integer;
        if (msec) {
            msec = 100 / (100 / msec);
        }
    }
    if (cls.realtime - cl.lastTransmitTime < msec) {
        return false;
    }

    return true;
}

static inline bool ready_to_send_hacked(void)
{
    if (!cl_fuzzhack->integer) {
        /* The non-batched fake-drop path deliberately never carries an
         * adaptive decision. Suspend state from a prior batch connection even
         * when its historical limiter exits early. */
        CL_AdaptiveInputEvaluate(false);
        return true; // packet drop hack disabled
    }

    if (cl.cmdNumber - cl.lastTransmitCmdNumberReal > 2) {
        return true; // can't drop more than 2 cmds
    }

    return ready_to_send(false);
}

/* Stage the identity tuple and its carrier away from msg_write.  q2proto's
 * byte writer may otherwise clear an overflowing sizebuf after the tuple has
 * been emitted, leaving either a partial header or a header without a move. */
static bool CL_WriteCommandCarrier(
    const q2proto_clc_message_t *move_message,
    bool include_identity, uint32_t first_command,
    uint32_t command_count)
{
    byte staging_data[MAX_PACKETLEN_WRITABLE];
    sizebuf_t staging;
    q2protoio_ioarg_t staging_io = {};
    uintptr_t staging_arg;

    if (!move_message)
        return false;
    SZ_InitWrite(&staging, staging_data, sizeof(staging_data));
    staging_io.sz_write = &staging;
    staging_io.max_msg_len = sizeof(staging_data);
    staging_arg = reinterpret_cast<uintptr_t>(&staging_io);
    if (include_identity &&
        !CL_CommandIdentityWriteSideband(
            staging_arg, first_command, command_count)) {
        return false;
    }
    if (q2proto_client_write(&cls.q2proto_ctx, staging_arg,
                             move_message) != Q2P_ERR_SUCCESS ||
        staging.overflowed ||
        staging.cursize >
            q2protoio_write_available(Q2PROTO_IOARG_CLIENT_WRITE)) {
        return false;
    }
    SZ_Write(&msg_write, staging.data, staging.cursize);
    return !msg_write.overflowed;
}

/*
=================
CL_SendDefaultCmd
=================
*/
static void CL_SendDefaultCmd(void)
{
    usercmd_t *cmd, *oldcmd;
    client_history_t *history;

    // archive this packet
    history = &cl.history[cls.netchan.outgoing_sequence & CMD_MASK];
    history->cmdNumber = cl.cmdNumber;
    history->sent = cls.realtime;    // for ping calculation
    history->rcvd = 0;

    cl.lastTransmitCmdNumber = cl.cmdNumber;

    // see if we are ready to send this packet
    if (!ready_to_send_hacked()) {
        cls.netchan.outgoing_sequence++; // just drop the packet
        return;
    }

    cl.lastTransmitTime = cls.realtime;
    cl.lastTransmitCmdNumberReal = cl.cmdNumber;

    // begin a client move command
    q2proto_clc_message_t move_message = {.type = Q2P_CLC_MOVE};

    // let the server know what the last frame we
    // got was, so the next message can be delta compressed
    move_message.move.lastframe = CL_SnapshotRecoverySelectLastFrame(
        cl_nodelta->integer || !cl.frame.valid /*|| cls.demowaiting*/
            ? -1
            : cl.frame.number);

    // send this and the previous cmds in the message, so
    // if the last packet was dropped, it can be recovered
    cmd = &cl.cmds[(cl.cmdNumber - 2) & CMD_MASK];
    if (!NetUsercmd_BuildDelta(&move_message.move.moves[0], NULL, cmd,
                               cl.lightlevel,
                               cls.q2proto_ctx.features.has_upmove)) {
        Com_Error(ERR_DROP, "%s: invalid oldest user command", __func__);
    }
    oldcmd = cmd;

    cmd = &cl.cmds[(cl.cmdNumber - 1) & CMD_MASK];
    if (!NetUsercmd_BuildDelta(&move_message.move.moves[1], oldcmd, cmd,
                               cl.lightlevel,
                               cls.q2proto_ctx.features.has_upmove)) {
        Com_Error(ERR_DROP, "%s: invalid previous user command", __func__);
    }
    oldcmd = cmd;

    cmd = &cl.cmds[cl.cmdNumber & CMD_MASK];
    if (!NetUsercmd_BuildDelta(&move_message.move.moves[2], oldcmd, cmd,
                               cl.lightlevel,
                               cls.q2proto_ctx.features.has_upmove)) {
        Com_Error(ERR_DROP, "%s: invalid current user command", __func__);
    }

    move_message.move.sequence = cls.netchan.outgoing_sequence;

    const bool include_identity =
        CL_NetCapabilityHas(
            WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1) &&
        cl.cmdNumber >= WORR_LEGACY_COMMAND_MOVE_COUNT;
    const uint32_t canonical_first =
        cl.cmdNumber - (WORR_LEGACY_COMMAND_MOVE_COUNT - 1u);
    if (!CL_WriteCommandCarrier(
            &move_message, include_identity, canonical_first,
            WORR_LEGACY_COMMAND_MOVE_COUNT)) {
        Com_Error(ERR_DROP, "%s: failed to encode command carrier",
                  __func__);
    }
    if (include_identity) {
        CL_NativeReadinessPilotObserveEncodedCommandRange(
            canonical_first, WORR_LEGACY_COMMAND_MOVE_COUNT);
    }

    P_FRAMES++;

    //
    // deliver the message
    //
#if USE_DEBUG
    int cursize = Netchan_Transmit(&cls.netchan, msg_write.cursize, msg_write.data, 1);
    if (cl_showpackets->integer) {
        Com_Printf("%i ", cursize);
    }
#else
    Netchan_Transmit(&cls.netchan, msg_write.cursize, msg_write.data, 1);
#endif

    SZ_Clear(&msg_write);
}

/*
=================
CL_SendBatchedCmd
=================
*/
static void CL_SendBatchedCmd(void)
{
    int i, j, seq, numCmds, numDups;
    usercmd_t *cmd, *oldcmd;
    client_history_t *history, *oldest;
    uint32_t canonicalFirstCmd = 0;
    uint32_t canonicalCmdCount = 0;
#if USE_DEBUG
    int totalCmds = 0;
    int totalMsec = 0;
#endif

    // see if we are ready to send this packet
    if (!ready_to_send(true)) {
        return;
    }

    // archive this packet
    seq = cls.netchan.outgoing_sequence;
    history = &cl.history[seq & CMD_MASK];
    history->cmdNumber = cl.cmdNumber;
    history->sent = cls.realtime;    // for ping calculation
    history->rcvd = 0;

    cl.lastTransmitTime = cls.realtime;
    cl.lastTransmitCmdNumber = cl.cmdNumber;
    cl.lastTransmitCmdNumberReal = cl.cmdNumber;

    q2proto_clc_message_t move_message = {.type = Q2P_CLC_BATCH_MOVE, .batch_move = {0}};
    move_message.batch_move.lastframe = CL_SnapshotRecoverySelectLastFrame(
        cl_nodelta->integer || !cl.frame.valid /*|| cls.demowaiting*/
            ? -1
            : cl.frame.number);

    Cvar_ClampInteger(cl_packetdup, 0, MAX_PACKET_FRAMES - 1);
    if (cl_adaptive_input->integer && adaptive_input.decision_valid &&
        (adaptive_input.output.flags &
         WORR_ADAPTIVE_INPUT_OUTPUT_REDUNDANCY_AVAILABLE)) {
        numDups = (int)min(
            adaptive_input.output.redundancy_frames,
            (uint32_t)(MAX_PACKET_FRAMES - 1));
    } else {
        numDups = cl_packetdup->integer;
    }

    move_message.batch_move.num_dups = numDups;

    // send this and the previous cmds in the message, so
    // if the last packet was dropped, it can be recovered
    oldcmd = NULL;
    q2proto_clc_batch_move_frame_t *frame = move_message.batch_move.batch_frames;
    for (i = seq - numDups; i <= seq; i++) {
        oldest = &cl.history[(i - 1) & CMD_MASK];
        history = &cl.history[i & CMD_MASK];

        numCmds = history->cmdNumber - oldest->cmdNumber;
        if (numCmds >= MAX_PACKET_USERCMDS) {
            Com_WPrintf("%s: MAX_PACKET_USERCMDS exceeded\n", __func__);
            MSG_BeginWriting();
            break;
        }
        if (numCmds > 0 && canonicalCmdCount == 0)
            canonicalFirstCmd = oldest->cmdNumber + 1u;
        canonicalCmdCount += static_cast<uint32_t>(numCmds);
#if USE_DEBUG
        totalCmds += numCmds;
#endif
        frame->num_cmds = numCmds;
        // MSG_WriteBits(numCmds, 5);
        q2proto_clc_move_delta_t *move = frame->moves;
        for (j = oldest->cmdNumber + 1; j <= history->cmdNumber; j++) {
            cmd = &cl.cmds[j & CMD_MASK];
#if USE_DEBUG
            totalMsec += cmd->msec;
#endif
            if (!NetUsercmd_BuildDelta(
                    move, oldcmd, cmd, cl.lightlevel,
                    cls.q2proto_ctx.features.has_upmove)) {
                Com_Error(ERR_DROP, "%s: invalid batched user command",
                          __func__);
            }
            oldcmd = cmd;
            ++move;
        }
        ++frame;
    }

    const bool include_identity =
        CL_NetCapabilityHas(
            WORR_NET_CAP_LEGACY_COMMAND_SIDEBAND_V1) &&
        canonicalCmdCount != 0;
    if (!CL_WriteCommandCarrier(
            &move_message, include_identity, canonicalFirstCmd,
            canonicalCmdCount)) {
        Com_Error(ERR_DROP, "%s: failed to encode batched command carrier",
                  __func__);
    }
    if (include_identity) {
        CL_NativeReadinessPilotObserveEncodedCommandRange(
            canonicalFirstCmd, canonicalCmdCount);
    }

    P_FRAMES++;

    //
    // deliver the message
    //
#if USE_DEBUG
    int cursize = Netchan_Transmit(&cls.netchan, msg_write.cursize, msg_write.data, 1);
    if (cl_showpackets->integer == 1) {
        Com_Printf("%i(%i) ", cursize, totalCmds);
    } else if (cl_showpackets->integer == 2) {
        Com_Printf("%i(%i) ", cursize, totalMsec);
    } else if (cl_showpackets->integer == 3) {
        Com_Printf(" | ");
    }
#else
    Netchan_Transmit(&cls.netchan, msg_write.cursize, msg_write.data, 1);
#endif

    SZ_Clear(&msg_write);
}

static void CL_SendKeepAlive(void)
{
    client_history_t *history;

    // archive this packet
    history = &cl.history[cls.netchan.outgoing_sequence & CMD_MASK];
    history->cmdNumber = cl.cmdNumber;
    history->sent = cls.realtime;    // for ping calculation
    history->rcvd = 0;

    cl.lastTransmitTime = cls.realtime;
    cl.lastTransmitCmdNumber = cl.cmdNumber;
    cl.lastTransmitCmdNumberReal = cl.cmdNumber;

#if USE_DEBUG
    int cursize = Netchan_Transmit(&cls.netchan, 0, NULL, 1);
    if (cl_showpackets->integer) {
        Com_Printf("%i ", cursize);
    }
#else
    Netchan_Transmit(&cls.netchan, 0, NULL, 1);
#endif
}

static void CL_SendUserinfo(void)
{
    char userinfo[MAX_INFO_STRING];
    cvar_t *var;
    int i;

    if (cls.userinfo_modified == MAX_PACKET_USERINFOS) {
        size_t len = Cvar_BitInfo(userinfo, CVAR_USERINFO);
        Com_DDPrintf("%s: %u: full update\n", __func__, com_framenum);
        q2proto_clc_message_t message = {.type = Q2P_CLC_USERINFO};
        message.userinfo.str.str = userinfo;
        message.userinfo.str.len = len;
        q2proto_client_write(&cls.q2proto_ctx, Q2PROTO_IOARG_CLIENT_WRITE, &message);
        MSG_FlushTo(&cls.netchan.message);
    } else if (cls.q2proto_ctx.features.userinfo_delta) {
        Com_DDPrintf("%s: %u: %d updates\n", __func__, com_framenum,
                     cls.userinfo_modified);
        q2proto_clc_message_t message = {.type = Q2P_CLC_USERINFO_DELTA};
        for (i = 0; i < cls.userinfo_modified; i++) {
            var = cls.userinfo_updates[i];
            message.userinfo_delta.name = q2proto_make_string(var->name);
            if (var->flags & CVAR_USERINFO) {
                message.userinfo_delta.value = q2proto_make_string(var->string);
            } else {
                // no longer in userinfo
                message.userinfo_delta.value.len = 0;
            }
            q2proto_client_write(&cls.q2proto_ctx, Q2PROTO_IOARG_CLIENT_WRITE, &message);
        }
        MSG_FlushTo(&cls.netchan.message);
    } else {
        Com_WPrintf("%s: update count is %d, should never happen.\n",
                    __func__, cls.userinfo_modified);
    }
}

static void CL_SendReliable(void)
{
    if (Netchan_SeqTooBig(&cls.netchan)) {
        Com_Error(ERR_DROP, "Outgoing sequence too big");
    }

    if (cls.userinfo_modified) {
        CL_SendUserinfo();
        cls.userinfo_modified = 0;
    }

    if (cls.netchan.message.overflowed) {
        SZ_Clear(&cls.netchan.message);
        Com_Error(ERR_DROP, "Reliable message overflowed");
    }
}

void CL_SendCmd(void)
{
    bool native_output_due;

    if (cls.state < ca_connected) {
        return; // not talking to a server
    }

    // generate usercmds while playing a demo, but do not send them
    if (cls.demo.playback) {
        return;
    }

    native_output_due = CL_NativeReadinessPilotOutputDue();

    if (cls.state != ca_active || sv_paused->integer) {
        // send a userinfo update if needed
        CL_SendReliable();

        // Keep native retries and event receipts live while paused or before
        // the first active user command, independently of netchan cadence.
        if (native_output_due || Netchan_ShouldUpdate(&cls.netchan)) {
            CL_SendKeepAlive();
        }

        cl.sendPacketNow = false;
        return;
    }

    // are there any new usercmds to send after all?
    if (cl.lastTransmitCmdNumber == cl.cmdNumber) {
        if (native_output_due) {
            CL_SendReliable();
            CL_SendKeepAlive();
            cl.sendPacketNow = false;
        }
        return; // nothing to send
    }

    // send a userinfo update if needed
    CL_SendReliable();

    if (cls.q2proto_ctx.features.batch_move && cl_batchcmds->integer) {
        CL_SendBatchedCmd();
    } else {
        CL_SendDefaultCmd();
    }
    
    if (cgame && cgame->WeaponBar_ClearInput)
        cgame->WeaponBar_ClearInput();
    if (cgame && cgame->Wheel_ClearInput)
        cgame->Wheel_ClearInput();

    cl.sendPacketNow = false;
}
